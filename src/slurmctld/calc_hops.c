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

#include "src/sacctmgr/sacctmgr.h"

extern int switch_levels;
extern struct switch_record *switch_record_table;
extern int nodes_per_switch;

float calc_hop(int arr[], int size, int start, int cnt){
	float hops = 0;
	float max_hops =0;
	int i=0;
	float c =0;
	for (i =start; i<start +(size/2); i++){
		if ( i+(size/2) < cnt ){
			if (arr[i] == arr [i + (size/2)]){
				c = (switch_record_table[arr[i]].comm_jobs)/(float)nodes_per_switch ;
				hops=2 + 2*c;
				debug("comm_jobs = %d at switch =%d",switch_record_table[arr[i]].comm_jobs,arr[i]);
                                debug("contention is %f hops are %f",c,hops);

			}
			else{
				c = (switch_record_table[arr[i]].comm_jobs + switch_record_table[arr[i+1]].comm_jobs) /((float)nodes_per_switch);
				hops=2*switch_levels + 2*switch_levels*c ;
                                debug("comm_jobs = %d at switch =%d",switch_record_table[arr[i]].comm_jobs,arr[i]);
                                debug("contention is %f hops are %f",c,hops);

			}
		}
		else
			continue;
		if (hops > max_hops)
			max_hops = hops;
	}
	return max_hops;
}

void hop(struct job_record *job_ptr)
{
	FILE *f;
	f = fopen ("/home/priya/workload/hops.txt", "a");
	int i, begin, end;
	int size = job_ptr->node_cnt;
	int switches[size];
	int index = 0;
	struct node_record *node_ptr;

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
	float total_hops =0;
	size = pow(2,ceil(log(size)/log(2)));
	debug("Original size is %d",size);
	while(size > 1){
		for (i=0; i<job_ptr->node_cnt; i+= size){
			hops = calc_hop(switches,size,i,job_ptr->node_cnt);
			if (hops > max_hops)
				max_hops = hops;
		}
		total_hops += max_hops;
		size = size /2;
	}
	char temp[50];
	
	if (job_ptr->comment)
		sprintf(temp,"%s %"PRIu32" %s %f",job_ptr->name,job_ptr->job_id,job_ptr->comment,total_hops);
	else 
		sprintf(temp,"%s %"PRIu32" 0 %f",job_ptr->name,job_ptr->job_id,total_hops);

	debug("The hops are %f and temp is %s",total_hops,temp);
	fputs(temp,f);
	fprintf(f,"\n");
	fclose(f);
}
