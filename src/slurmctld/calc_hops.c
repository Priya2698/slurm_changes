#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <math.h>

#include "slurm/slurm.h"

#include "src/common/slurm_topology.h"
#include "src/common/switch.h"
#include "src/common/node_conf.h"
#include "src/common/xmalloc.h"

#include "src/slurmctld/job_scheduler.h"
#include "src/slurmctld/proc_req.h"
#include "src/slurmctld/slurmctld.h"
#include "src/slurmctld/calc_hops.h"
#include "src/plugins/select/linear/select_linear.h"

extern int switch_levels;
extern struct switch_record *switch_record_table;
extern int switch_record_cnt;
uint32_t* node_cnt;

#define SWITCH_ORDER_SIZE 100
extern struct table *alloc_node_table;
extern struct table *switch_idx_table;

// Functions for maintaining a hashmap
/*struct table *createTable(int size){
    struct table *t = (struct table*)malloc(sizeof(struct table));
    t->size = size;
    t->list = (struct node**)malloc(sizeof(struct node*)*size);
    int i;
    for(i=0;i<size;i++)
        t->list[i] = NULL;
    return t;
}*/
uint32_t hashCode(struct table *t,uint32_t key){
    if(key<0)
        return -(key%t->size);
    return key%t->size;
}
void insert(struct table *t,uint32_t key,int* val, int n){
    uint32_t pos = hashCode(t,key);
    struct node *list = t->list[pos];
    struct node *newNode = (struct node*)malloc(sizeof(struct node));
    newNode->key = key;
    for(int i=0;i<n;i++)
        newNode->val[i] = val[i];
    newNode->n = n;
    newNode->next = list;
    t->list[pos] = newNode;
}
int lookup(struct table *t,uint32_t key, int*arr, int *n){
    uint32_t pos = hashCode(t,key);
    debug("JobId=%d Pos=%d",key,pos);
    struct node *list = t->list[pos];
    struct node *temp = list;
    while(temp){
        if(temp->key==key){
            *n = temp->n;
            for(int i=0;i<*n;i++)
                arr[i] = temp->val[i];
            return 0;
        }
        temp = temp->next;
    }
    return -1;
}
/*void delete_table(struct table* t){
	if(t){
		for (int i=0;i<t->size;i++)
                	free(t->list[i]);
        	free(t);
	}
}*/
// To sort array in descending order
int desc_cmp(const void *a, const void *b){
        int idxa = *(int *)a;
        int idxb = *(int *)b; 
        if (node_cnt[idxa] != node_cnt[idxb])
                return node_cnt[idxa] > node_cnt[idxb] ? -1 : 1;
        else
                return switch_record_table[idxa].comm_jobs < switch_record_table[idxb].comm_jobs ? -1 : 1;
}
// To sort array in increasing order 
int inc_cmp(const void *a, const void *b){
	int idxa = *(int *)a;
	int idxb = *(int *)b;
	if (node_cnt[idxa] == 0)
		return 1;
	else if (node_cnt[idxb] == 0)
		return -1;
	else
		if(node_cnt[idxa] != node_cnt[idxb])
			return node_cnt[idxa] < node_cnt[idxb] ? -1 : 1;
		else
			return switch_record_table[idxa].comm_jobs > switch_record_table[idxb].comm_jobs ? -1 : 1;
}
// For balanced allocation in select/linear
void balanced_alloc(struct job_record *job_ptr,uint32_t* switch_node_cnt,
	       	int* switch_idx, uint32_t want_nodes, int* switch_alloc_nodes){

        uint32_t curr_size = want_nodes;
        uint32_t rem_nodes = want_nodes;
        int i, nalloc;
        uint32_t* free_nodes;

        free_nodes = xcalloc(switch_record_cnt, sizeof(uint32_t));
	
        // Sort the switch_node_cnt array
        for(i=0; i<switch_record_cnt; i++){
                switch_idx[i] = i;
                free_nodes[i] = 0;
                switch_alloc_nodes[i] = 0;
        }
        node_cnt = switch_node_cnt;
        if (job_ptr->comment && strcmp(job_ptr->comment,"1")==0)
       		qsort(switch_idx,switch_record_cnt, sizeof(*switch_idx), desc_cmp);
	else 
		qsort(switch_idx,switch_record_cnt, sizeof(*switch_idx), inc_cmp);
        for(i=0; i<switch_record_cnt; i++)
                free_nodes[i] = switch_node_cnt[switch_idx[i]];

	if (job_ptr->comment && strcmp(job_ptr->comment,"1")==0){
        	// Forward pass to allocate nodes equally
        	for(i=0; (i<switch_record_cnt && rem_nodes && free_nodes[i]); i++){
                	while (curr_size > free_nodes[i])
                        	curr_size /= 2;
                	nalloc = (curr_size < rem_nodes) ? curr_size:rem_nodes;
                	debug("%s: found switch %d for allocation: nodes %d "
                      		"allocated %u ", __func__,switch_idx[i], free_nodes[i], nalloc);
                	switch_alloc_nodes[i] = nalloc;
                	free_nodes[i]-=nalloc;
                	rem_nodes-=nalloc;
        	}

        	//Backtrack if more nodes required
        	if (rem_nodes)
        	        i--;
        	while(rem_nodes>0 && i>=0){
                	nalloc = (free_nodes[i] < rem_nodes) ? free_nodes[i]:rem_nodes;
                	debug("%s: found switch %d for allocation: nodes %d "
                      		"allocated %u ", __func__,switch_idx[i], free_nodes[i], nalloc);
                	switch_alloc_nodes[i] +=nalloc;
                	free_nodes[i]-=nalloc;
                	rem_nodes-=nalloc;
			i--;
        	}
	}
	else{
		for(i=0; (i<switch_record_cnt && rem_nodes && free_nodes[i]); i++){
			nalloc = (free_nodes[i] < rem_nodes) ? free_nodes[i]:rem_nodes;
			debug("%s: found switch %d for allocation: nodes %d "
                                "allocated %u ", __func__,switch_idx[i], free_nodes[i], nalloc);
			switch_alloc_nodes[i] = nalloc;
			free_nodes[i]-=nalloc;
			rem_nodes-=nalloc;
		}
	}

//	debug("Balanced allocation complete");
	xfree(free_nodes);
        return;
}

float expected_fatrecursive(int arr[], int comm_jobs[], int size, int start, uint32_t cnt){
        float hops = 0;
        float max_hops = 0;
        int i = 0;
        float c=0, c1=0, c2=0, c3=0;
        for (i =start; i<start +(size/2); i++){
                if ( i+(size/2) < cnt ){
                        if (arr[i] == arr [i + (size/2)]){
                                c = (comm_jobs[i])/((float)switch_record_table[arr[i]].num_nodes) ;
                                hops=2 + 2*c;
                       /*         debug("%d<->%d : Comm_Jobs=%d Contention=%f Hops=%f Switch =%d",
                                        i,i+(size/2),switch_record_table[arr[i]].comm_jobs,c,hops,arr[i]);*/
                        }
                        else{
                                c1 = (comm_jobs[i])/((float)switch_record_table[arr[i]].num_nodes);
                                c2 = (comm_jobs[i+(size/2)])/((float)switch_record_table[arr[i+(size/2)]].num_nodes);
                                c3 = (comm_jobs[i] + comm_jobs[i+(size/2)])/((float)switch_record_table[arr[i]].num_nodes + 
						(float)switch_record_table[arr[i+(size/2)]].num_nodes);
                                c = c1+c2+c3/2;
                                hops=2*(switch_levels+1) + 2*(switch_levels+1)*c ;
                       /*         debug("%d<->%d : Comm_jobs=%d,%d Switch =%d,%d Contention=%f Hops=%f",
                                        i,i+(size/2),switch_record_table[arr[i]].comm_jobs,
                                        switch_record_table[arr[i+(size/2)]].comm_jobs,
                                        arr[i],arr[i+(size/2)],c,hops);*/
                        }
                }
                else
                        continue;
                if (hops > max_hops)
                        max_hops = hops;
        }
        return max_hops;
}

float expected_hops(struct job_record *job_ptr, int *switch_alloc_nodes,
			int *switch_idx, uint32_t want_nodes){
	int i,j,k=0;
	uint32_t size = want_nodes;
        int switches[size];
	int comm_jobs[size];
	float hops = 0;
        float max_hops =0;
	debug("Original size:%d, switch levels:%d",size,switch_levels);
// Generate required arrays
	debug("Generating arrays");
	for(i=0; i<switch_record_cnt && k<size;i++){
		j = 0;
		debug ("i:%d switch_idx:%d switch_alloc_nodes:%d",i,
				switch_idx[i],switch_alloc_nodes[i]);
		while(j < switch_alloc_nodes[i]){
			switches[k] = switch_idx[i];
			if (job_ptr->comment && strcmp(job_ptr->comment,"1")==0)
				comm_jobs[k] = switch_record_table[switch_idx[i]].comm_jobs
							+ switch_alloc_nodes[i];
			else
				comm_jobs[k] = switch_record_table[switch_idx[i]].comm_jobs;
		//	debug("Index=%d Switch=%d Comm_jobs=%d",k,switches[k],comm_jobs[k]);
			k++;
			j++;		
		}
	}
	size = pow(2,ceil(log(size)/log(2)));
// Calculate Hops for recursive halving
        float rec_fathops =0;
        uint32_t rec_size = size;
        int msize = 1; // Message size for recursive halving calculations
        //debug("Expected fat tree recursive hops");
        while(rec_size > 1){
                max_hops = 0;
                for (i=0; i<want_nodes; i+= rec_size){
                        hops = expected_fatrecursive(switches,comm_jobs,rec_size,i,want_nodes);
                        if (hops > max_hops)
                                max_hops = hops;
                }
                //debug(" rec_fathops = %d x %f ",msize,max_hops);
                rec_fathops += msize * max_hops;
                msize = msize * 2;
                rec_size = rec_size /2;
        }
	return rec_fathops;
}

/* cnt is total node count */
float fatrecursive(int arr[], int size, int start, int cnt){
	float hops = 0;
	float max_hops = 0;
	int i = 0;
	float c=0, c1=0, c2=0, c3=0;
	for (i =start; i<start +(size/2); i++){
		if ( i+(size/2) < cnt ){
			if (arr[i] == arr [i + (size/2)]){
				c = (switch_record_table[arr[i]].comm_jobs)/((float)switch_record_table[arr[i]].num_nodes) ;
				hops=2 + 2*c;
				/*debug("%d<->%d : Comm_Jobs=%d Contention=%f Hops=%f Switch =%d",
					i,i+(size/2),switch_record_table[arr[i]].comm_jobs,c,hops,arr[i]);*/
			}
			else{
				c1 = (switch_record_table[arr[i]].comm_jobs)/((float)switch_record_table[arr[i]].num_nodes);
				c2 = (switch_record_table[arr[i+(size/2)]].comm_jobs)/((float)switch_record_table[arr[i+(size/2)]].num_nodes);
				c3 = (switch_record_table[arr[i]].comm_jobs + switch_record_table[arr[i+(size/2)]].comm_jobs)/
					((float)switch_record_table[arr[i]].num_nodes + (float)switch_record_table[arr[i+(size/2)]].num_nodes);
                                c = c1+c2+c3/2;
				hops=2*(switch_levels+1) + 2*(switch_levels+1)*c ;
                                /*debug("%d<->%d : Comm_jobs=%d,%d Switch =%d,%d Contention=%f Hops=%f",
					i,i+(size/2),switch_record_table[arr[i]].comm_jobs,
					switch_record_table[arr[i+(size/2)]].comm_jobs,
					arr[i],arr[i+(size/2)],c,hops);*/
			}
		}
		else
			continue;
		if (hops > max_hops)
			max_hops = hops;
	}
	return max_hops;
}
float treerecursive(int arr[], int size, int start, int cnt){
        float hops = 0;
        float max_hops =0;
        int i=0;
        float c=0, c1=0, c2=0, c3=0;
	for (i =start; i<start +(size/2); i++){
                if ( i+(size/2) < cnt ){
                        if (arr[i] == arr [i + (size/2)]){
			  	c = (switch_record_table[arr[i]].comm_jobs)/((float)switch_record_table[arr[i]].num_nodes) ;
				hops=2 + 2*c;
                                /*debug("%d<->%d : Comm_Jobs=%d Contention=%f Hops=%f Switch =%d",
                                        i,i+(size/2),switch_record_table[arr[i]].comm_jobs,c,hops,arr[i]);*/
                        }
                        else{
                                c1 = (switch_record_table[arr[i]].comm_jobs)/((float)switch_record_table[arr[i]].num_nodes);
                                c2 = (switch_record_table[arr[i+(size/2)]].comm_jobs)/((float)switch_record_table[arr[i+(size/2)]].num_nodes);
                                c3 = (switch_record_table[arr[i]].comm_jobs + switch_record_table[arr[i+(size/2)]].comm_jobs)/
                                        ((float)switch_record_table[arr[i]].num_nodes + (float)switch_record_table[arr[i+(size/2)]].num_nodes);
                                c = c1+c2+c3;
				hops=2*(switch_levels+1) + 2*(switch_levels+1)*c ;
                                /*debug("%d<->%d : Comm_jobs=%d,%d Switch =%d,%d Contention=%f Hops=%f",
                                        i,i+(size/2),switch_record_table[arr[i]].comm_jobs,
                                        switch_record_table[arr[i+(size/2)]].comm_jobs,
                                        arr[i],arr[i+(size/2)],c,hops);*/
                        }
                }
                else    
                        continue;
                if (hops > max_hops)
                        max_hops = hops;
        }
        return max_hops;
}

float fatreduce(int arr[], int size, int start, int cnt){
	float hops = 0;
	float max_hops = 0;
	int i = 0;
	float c=0, c1=0, c2=0, c3=0;
	for (i=start; i < cnt-size; i+=2*size){
		if (arr[i] == arr[i+size]){
                                c = (switch_record_table[arr[i]].comm_jobs)/((float)switch_record_table[arr[i]].num_nodes) ;
		       		hops=2 + 2*c;
                                /*debug("%d<->%d : Comm_Jobs=%d Contention=%f Hops=%f Switch =%d",
                                        i,i+size,switch_record_table[arr[i]].comm_jobs,c,hops,arr[i]);*/
		}
		else{
				c1 = (switch_record_table[arr[i]].comm_jobs)/((float)switch_record_table[arr[i]].num_nodes);
				c2 = (switch_record_table[arr[i+size]].comm_jobs)/((float)switch_record_table[arr[i+size]].num_nodes);
				c3 = (switch_record_table[arr[i]].comm_jobs + switch_record_table[arr[i+size]].comm_jobs)/
					((float)switch_record_table[arr[i]].num_nodes + (float)switch_record_table[arr[i+size]].num_nodes);
				c = c1+c2+c3/2;
		       		hops=2*(switch_levels+1) + 2*(switch_levels+1)*c ;
                                /*debug("%d<->%d : Comm_jobs=%d,%d Switch =%d,%d Contention=%f Hops=%f",
                                        i,i+size,switch_record_table[arr[i]].comm_jobs,
                                        switch_record_table[arr[i+size]].comm_jobs,
                                        arr[i],arr[i+size],c,hops);*/
		}
		
		if (hops > max_hops)
			max_hops = hops;
	}
	return max_hops;
}

float treereduce(int arr[], int size, int start, int cnt){
        float hops = 0;
        float max_hops = 0;
        int i = 0;
        float c=0, c1=0, c2=0, c3=0;
        for (i=start; i < cnt-size; i+=2*size){
                if (arr[i] == arr[i+size]){
                                c = (switch_record_table[arr[i]].comm_jobs)/((float)switch_record_table[arr[i]].num_nodes) ;
				hops=2 + 2*c;
                                /*debug("%d<->%d : Comm_Jobs=%d Contention=%f Hops=%f Switch =%d",
                                        i,i+size,switch_record_table[arr[i]].comm_jobs,c,hops,arr[i]);*/
                }
                else{
				c1 = (switch_record_table[arr[i]].comm_jobs)/((float)switch_record_table[arr[i]].num_nodes);
                                c2 = (switch_record_table[arr[i+size]].comm_jobs)/((float)switch_record_table[arr[i+size]].num_nodes);
                                c3 = (switch_record_table[arr[i]].comm_jobs + switch_record_table[arr[i+size]].comm_jobs)/
                                        ((float)switch_record_table[arr[i]].num_nodes + (float)switch_record_table[arr[i+size]].num_nodes);
                                c = c1+c2+c3;
                                hops=2*(switch_levels+1) + 2*(switch_levels+1)*c ;
                                /*debug("%d<->%d : Comm_jobs=%d,%d Switch =%d,%d Contention=%f Hops=%f",
                                        i,i+size,switch_record_table[arr[i]].comm_jobs,
                                        switch_record_table[arr[i+size]].comm_jobs,
                                        arr[i],arr[i+size],c,hops);*/
                }

                if (hops > max_hops)
                        max_hops = hops;
        }
        return max_hops;
}


void hop(struct job_record *job_ptr)
{
	FILE *f;
	f = fopen ("/home/ubuntu/workload/hops.txt", "a");
	int i,j, begin, end,k=0;
	int size = job_ptr->node_cnt;
	int switches[size];
	int index = 0;
	struct node_record *node_ptr;
	int *switch_idx;
	int *switch_alloc_nodes;
	int switch_result;
	int node_result;
	int *n = (int*)malloc(sizeof(int));	
        debug("Original size:%d, switch levels:%d",size,switch_levels);

	switch_idx = xcalloc(switch_record_cnt, sizeof(int));
        switch_alloc_nodes = xcalloc(switch_record_cnt, sizeof(int));
	
	switch_result = lookup(switch_idx_table,job_ptr->job_id,switch_idx,n);
	node_result = lookup(alloc_node_table,job_ptr->job_id,switch_alloc_nodes,n);	
	
	if(switch_result == 0 && node_result == 0){
		if (*n != switch_record_cnt)
			debug("Arrays are of inconsistent size");
		debug("Generating arrays");
        	for(i=0; i<switch_record_cnt && k<size;i++){
                	j = 0;
               		debug ("i:%d switch_idx:%d switch_alloc_nodes:%d",i,
                                switch_idx[i],switch_alloc_nodes[i]);
                	while(j < switch_alloc_nodes[i]){
                        	switches[k] = switch_idx[i];
                        k++;j++;
                	}
        	}	
	}
	else {
		debug("THIS SHOULD NEVER HAPPEN: KEY WAS NOT FOUND");
		begin = bit_ffs(job_ptr->node_bitmap);
        	if (begin >=0 )
                	end = bit_fls(job_ptr->node_bitmap);
        	else
                	end = -1;
       		for (i=begin; i<=end; i++){
                	if(!bit_test(job_ptr->node_bitmap, i))
                        	continue;
                	node_ptr = node_record_table_ptr + i;
                	switches[index]= node_ptr->leaf_switch;
                	//debug("Node name = %s , switches[%d]=%d",
                	//node_ptr->name,index,switches[index]);
                	index+=1;
        	}
	}
	size = pow(2,ceil(log(size)/log(2)));
	float hops = 0;
	float max_hops =0;

// Calculate Hops for recursive halving
	float rec_fathops =0;
	int rec_size = size;
	int msize =1; // Message size for recursive halving calculations
	debug("Calculating fat tree recursive hops");
	while(rec_size > 1){
		max_hops = 0;
		for (i=0; i<job_ptr->node_cnt; i+= rec_size){
			hops = fatrecursive(switches,rec_size,i,job_ptr->node_cnt);
			if (hops > max_hops)
				max_hops = hops;
		}
		debug(" rec_fathops = %d x %f ",msize,max_hops);
		rec_fathops += msize * max_hops;
		msize = msize * 2;
		rec_size = rec_size /2;
	}

	float rec_treehops =0;
        rec_size = size;
        msize =1; // Message size for recursive halving calculations
        //debug("Calculating tree recursive hops");
        while(rec_size > 1){
                max_hops = 0;
                for (i=0; i<job_ptr->node_cnt; i+= rec_size){
                        hops = treerecursive(switches,rec_size,i,job_ptr->node_cnt);
                        if (hops > max_hops)
                                max_hops = hops;
                }
                //debug(" rec_treehops = %d x %f ",msize,max_hops);
                rec_treehops += msize * max_hops;
                msize = msize * 2;
                rec_size = rec_size /2;
        }



// Calculate Hops for reduce
	float red_fathops =0;
	int red_size = 1;
	//debug("Calculating fat tree reduce hops");
	while(red_size < size){
		red_fathops += fatreduce(switches,red_size,0,job_ptr->node_cnt);
		red_size *=2;
	}
        float red_treehops =0;
        red_size = 1;
        //debug("Calculating tree reduce hops");
        while(red_size < size){
                red_treehops += treereduce(switches,red_size,0,job_ptr->node_cnt);
                red_size *=2;
        }

	char temp[100];
	
	if (job_ptr->comment)
		sprintf(temp,"%s %"PRIu32" %s %f %f %f %f ",job_ptr->name,job_ptr->job_id,job_ptr->comment,rec_fathops,rec_treehops,red_fathops,red_treehops);
	else 
		sprintf(temp,"%s %"PRIu32" 0 %f %f %f %f ",job_ptr->name,job_ptr->job_id,rec_fathops,rec_treehops,red_fathops,red_treehops);

	debug("Recursive FatHops:%f TreeHops:%f | Reduce FatHops = %f Treehops =%f | temp: %s",rec_fathops,rec_treehops,red_fathops,red_treehops,temp);
	fputs(temp,f);
	fprintf(f,"\n");
	fclose(f);
	free(n);
	xfree(switch_idx);
	xfree(switch_alloc_nodes);
}
