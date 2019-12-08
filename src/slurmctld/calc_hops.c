#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <math.h>

#include "slurm/slurm.h"

#include "src/common/slurm_topology.h"
#include "src/common/switch.h"
#include "src/common/node_conf.h"

#include "src/slurmctld/job_scheduler.h"
#include "src/slurmctld/proc_req.h"
#include "src/slurmctld/slurmctld.h"

extern int switch_levels;
extern struct switch_record *switch_record_table;
extern int nodes_per_switch;

/* cnt is total node count */

float recursive(int arr[], int size, int start, int cnt){
	float hops = 0;
	float max_hops =0;
	int i=0;
	float c =0;
	for (i =start; i<start +(size/2); i++){
		if ( i+(size/2) < cnt ){
			if (arr[i] == arr [i + (size/2)]){
				c = (switch_record_table[arr[i]].comm_jobs)/(float)nodes_per_switch ;
				hops=2 + 2*c;
				debug("%d<->%d : Comm_Jobs=%d Contention=%f Hops=%f Switch =%d",
					i,i+(size/2),switch_record_table[arr[i]].comm_jobs,c,hops,arr[i]);
			}
			else{
				c = (switch_record_table[arr[i]].comm_jobs + switch_record_table[arr[i+(size/2)]].comm_jobs)/(2*(float)nodes_per_switch);
				hops=2*(switch_levels+1) + 2*(switch_levels+1)*c ;
                                debug("%d<->%d : Comm_jobs=%d,%d Switch =%d,%d Contention=%f Hops=%f",
					i,i+(size/2),switch_record_table[arr[i]].comm_jobs,
					switch_record_table[arr[i+(size/2)]].comm_jobs,
					arr[i],arr[i+(size/2)],c,hops);
			}
		}
		else
			continue;
		if (hops > max_hops)
			max_hops = hops;
	}
	return max_hops;
}

float reduce(int arr[], int size, int start, int cnt){
	float hops = 0;
	float max_hops = 0;
	int i = 0;
	float c = 0;
	for (i=start; i < cnt-size; i+=2*size){
		if (arr[i] == arr[i+size]){
                                c = (switch_record_table[arr[i]].comm_jobs)/(float)nodes_per_switch ;
                                hops=2 + 2*c;
                                debug("%d<->%d : Comm_Jobs=%d Contention=%f Hops=%f Switch =%d",
                                        i,i+size,switch_record_table[arr[i]].comm_jobs,c,hops,arr[i]);
		}
		else{
                                c = (switch_record_table[arr[i]].comm_jobs + switch_record_table[arr[i+size]].comm_jobs)/((float)nodes_per_switch);
                                hops=2*(switch_levels+1) + 2*(switch_levels+1)*c ;
                                debug("%d<->%d : Comm_jobs=%d,%d Switch =%d,%d Contention=%f Hops=%f",
                                        i,i+size,switch_record_table[arr[i]].comm_jobs,
                                        switch_record_table[arr[i+size]].comm_jobs,
                                        arr[i],arr[i+size],c,hops);

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
	int i, begin, end;
	int size = job_ptr->node_cnt;
	int switches[size];
	int index = 0;
	struct node_record *node_ptr;

        size = pow(2,ceil(log(size)/log(2)));
        debug("Original size is %d, switch levels= %d",size,switch_levels);


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
		debug("Node name = %s , switches[%d]=%d",
			node_ptr->name,index,switches[index]);
		index+=1;
	}

	float hops = 0;
	float max_hops =0;

// Calculate Hops for recursive halving
	float rec_hops =0;
	int rec_size = size;
	int msize =1; // Message size for recursive halving calculations
	debug("Calculating recursive hops");
	while(rec_size > 1){
		max_hops = 0;
		for (i=0; i<job_ptr->node_cnt; i+= rec_size){
			hops = recursive(switches,rec_size,i,job_ptr->node_cnt);
			if (hops > max_hops)
				max_hops = hops;
		}
		debug(" rec_hops = %d x %f ",msize,max_hops);
		rec_hops += msize * max_hops;
		msize = msize * 2;
		rec_size = rec_size /2;
	}

// Calculate Hops for reduce
	float red_hops =0;
	int red_size = 1;
	debug("Calculating reduce hops");
	while(red_size < size){
		red_hops += reduce(switches,red_size,0,job_ptr->node_cnt);
		red_size *=2;
	}

	char temp[50];
	
	if (job_ptr->comment)
		sprintf(temp,"%s %"PRIu32" %s %f %f",job_ptr->name,job_ptr->job_id,job_ptr->comment,rec_hops,red_hops);
	else 
		sprintf(temp,"%s %"PRIu32" 0 %f %f",job_ptr->name,job_ptr->job_id,rec_hops,red_hops);

	debug("Recursive hops = %f Reduce Hops = %f temp: %s",rec_hops,red_hops,temp);
	fputs(temp,f);
	fprintf(f,"\n");
	fclose(f);
}
