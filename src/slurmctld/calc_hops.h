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

extern int switch_levels;
extern int switch_record_cnt;
extern void balanced_alloc(uint32_t* switch_node_cnt, int* switch_idx,
                uint32_t want_nodes, uint32_t* switch_alloc_nodes);
extern void hop(struct job_record * job_ptr);
