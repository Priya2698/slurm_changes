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

#include "src/plugins/select/linear/select_linear.h"
#define SWITCH_ORDER_SIZE 100

struct node {
    uint32_t key;
    int val[SWITCH_ORDER_SIZE];
    int n;
    struct node *next;
};
struct table{
    int size;
    struct node **list;
};

extern struct table* alloc_node_table;
extern struct table* switch_idx_table;
extern int switch_levels;
extern int switch_record_cnt;
extern void balanced_alloc(struct job_record *job_ptr,uint32_t* switch_node_cnt,
	       	int* switch_idx, uint32_t want_nodes, int* switch_alloc_nodes);
extern void hop(struct job_record * job_ptr);
extern float expected_hops(struct job_record *job_ptr, int *switch_alloc_nodes,
                        int *switch_idx, uint32_t want_nodes);
//extern struct table *createTable(int size);
extern void insert(struct table *t,uint32_t key,int* val, int n);
extern int lookup(struct table *t,uint32_t key, int*arr, int *n);
//extern void delete_table(struct table* t);
