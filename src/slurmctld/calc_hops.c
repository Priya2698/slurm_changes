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
int calc_hop(int arr[], int size, int start, int cnt){
	long int hops = 0;
	int i=0;
	for (i =start; i<start +(size/2); i++){
		if ( i+(size/2) < cnt ){
			if (arr[i] == arr [i + (size/2)])
				hops+=2;
			else
				hops+=2*switch_levels;
		}
		else
			continue;
	}
	return hops;
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
	long int avg_hops = 0;
	size = pow(2,ceil(log(size)/log(2)));
	debug("Original size is %d",size);
	while(size > 1){
		for (i=0; i<job_ptr->node_cnt; i+= size)
			avg_hops += calc_hop(switches,size,i,job_ptr->node_cnt);
		size = size /2;
	}
	char temp[50];
	
	if (job_ptr->comment)
		sprintf(temp,"%s %"PRIu32" %s %ld",job_ptr->name,job_ptr->job_id,job_ptr->comment,avg_hops);
	else 
		sprintf(temp,"%s %"PRIu32" 0 %ld",job_ptr->name,job_ptr->job_id,avg_hops);

	debug("The hops are %ld and temp is %s",avg_hops,temp);
	fputs(temp,f);
	fprintf(f,"\n");
	fclose(f);
}
