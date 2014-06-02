/*
 * Completely Fair Scheduling (CFS) Class (SCHED_NORMAL/SCHED_BATCH)
 *
 *  Copyright (C) 2007 Red Hat, Inc., Ingo Molnar <mingo@redhat.com>
 *
 *  Interactivity improvements by Mike Galbraith
 *  (C) 2007 Mike Galbraith <efault@gmx.de>
 *
 *  Various enhancements by Dmitry Adamushko.
 *  (C) 2007 Dmitry Adamushko <dmitry.adamushko@gmail.com>
 *
 *  Group scheduling enhancements by Srivatsa Vaddagiri
 *  Copyright IBM Corporation, 2007
 *  Author: Srivatsa Vaddagiri <vatsa@linux.vnet.ibm.com>
 *
 *  Scaled math optimizations by Thomas Gleixner
 *  Copyright (C) 2007, Thomas Gleixner <tglx@linutronix.de>
 *
 *  Adaptive scheduling granularity, math enhancements by Peter Zijlstra
 *  Copyright (C) 2007 Red Hat, Inc., Peter Zijlstra <pzijlstr@redhat.com>
 */

/*
 * Targeted preemption latency for CPU-bound tasks:
 * (default: 20ms * (1 + ilog(ncpus)), units: nanoseconds)
 *
 * NOTE: this latency value is not the same as the concept of
 * 'timeslice length' - timeslices in CFS are of variable length
 * and have no persistent notion like in traditional, time-slice
 * based scheduling concepts.
 *
 * (to see the precise effective timeslice length of your workload,
 *  run vmstat and monitor the context-switches (cs) field)
 */
/* 调度延迟，CFS进程在此调度周期内分配时间 */
unsigned int sysctl_sched_latency = 20000000ULL;

/*
 * Minimal preemption granularity for CPU-bound tasks:
 * (default: 4 msec * (1 + ilog(ncpus)), units: nanoseconds)
 */
/* 最小运行周期，如果队列中可运行进程数量较多，则可能扩大调度延迟周期 */
unsigned int sysctl_sched_min_granularity = 4000000ULL;

/*
 * is kept at sysctl_sched_latency / sysctl_sched_min_granularity
 */
static unsigned int sched_nr_latency = 5;

/*
 * After fork, child runs first. (default) If set to 0 then
 * parent will (try to) run first.
 */
/* 新创建的进程是否应当在父进程之前运行 */
const_debug unsigned int sysctl_sched_child_runs_first = 1;

/*
 * sys_sched_yield() compat mode
 *
 * This option switches the agressive yield implementation of the
 * old scheduler back on.
 */
unsigned int __read_mostly sysctl_sched_compat_yield;

/*
 * SCHED_BATCH wake-up granularity.
 * (default: 10 msec * (1 + ilog(ncpus)), units: nanoseconds)
 *
 * This option delays the preemption effects of decoupled workloads
 * and reduces their over-scheduling. Synchronous workloads will still
 * have immediate wakeup/sleep latencies.
 */
unsigned int sysctl_sched_batch_wakeup_granularity = 10000000UL;

/*
 * SCHED_OTHER wake-up granularity.
 * (default: 10 msec * (1 + ilog(ncpus)), units: nanoseconds)
 *
 * This option delays the preemption effects of decoupled workloads
 * and reduces their over-scheduling. Synchronous workloads will still
 * have immediate wakeup/sleep latencies.
 */
unsigned int sysctl_sched_wakeup_granularity = 10000000UL;

const_debug unsigned int sysctl_sched_migration_cost = 500000UL;

/**************************************************************
 * CFS operations on generic schedulable entities:
 */

#ifdef CONFIG_FAIR_GROUP_SCHED

/* cpu runqueue to which this cfs_rq is attached */
static inline struct rq *rq_of(struct cfs_rq *cfs_rq)
{
	return cfs_rq->rq;
}

/* An entity is a task if it doesn't "own" a runqueue */
#define entity_is_task(se)	(!se->my_q)

#else	/* CONFIG_FAIR_GROUP_SCHED */

static inline struct rq *rq_of(struct cfs_rq *cfs_rq)
{
	return container_of(cfs_rq, struct rq, cfs);
}

#define entity_is_task(se)	1

#endif	/* CONFIG_FAIR_GROUP_SCHED */

static inline struct task_struct *task_of(struct sched_entity *se)
{
	return container_of(se, struct task_struct, se);
}


/**************************************************************
 * Scheduling class tree data structure manipulation methods:
 */

static inline u64 max_vruntime(u64 min_vruntime, u64 vruntime)
{
	s64 delta = (s64)(vruntime - min_vruntime);
	if (delta > 0)
		min_vruntime = vruntime;

	return min_vruntime;
}

static inline u64 min_vruntime(u64 min_vruntime, u64 vruntime)
{
	s64 delta = (s64)(vruntime - min_vruntime);
	if (delta < 0)
		min_vruntime = vruntime;

	return min_vruntime;
}

/* 进程在红黑树中排序的标准 */
static inline s64 entity_key(struct cfs_rq *cfs_rq, struct sched_entity *se)
{
	return se->vruntime - cfs_rq->min_vruntime;
}

/*
 * Enqueue an entity into the rb-tree:
 */
static void __enqueue_entity(struct cfs_rq *cfs_rq, struct sched_entity *se)
{
	struct rb_node **link = &cfs_rq->tasks_timeline.rb_node;
	struct rb_node *parent = NULL;
	struct sched_entity *entry;
	s64 key = entity_key(cfs_rq, se);
	int leftmost = 1;

	/*
	 * Find the right place in the rbtree:
	 */
	while (*link) {
		parent = *link;
		entry = rb_entry(parent, struct sched_entity, run_node);
		/*
		 * We dont care about collisions. Nodes with
		 * the same key stay together.
		 */
		if (key < entity_key(cfs_rq, entry)) {
			link = &parent->rb_left;
		} else {
			link = &parent->rb_right;
			leftmost = 0;
		}
	}

	/*
	 * Maintain a cache of leftmost tree entries (it is frequently
	 * used):
	 */
	if (leftmost)
		cfs_rq->rb_leftmost = &se->run_node;

	rb_link_node(&se->run_node, parent, link);
	rb_insert_color(&se->run_node, &cfs_rq->tasks_timeline);
}

static void __dequeue_entity(struct cfs_rq *cfs_rq, struct sched_entity *se)
{
	if (cfs_rq->rb_leftmost == &se->run_node)
		cfs_rq->rb_leftmost = rb_next(&se->run_node);

	rb_erase(&se->run_node, &cfs_rq->tasks_timeline);
}

static inline struct rb_node *first_fair(struct cfs_rq *cfs_rq)
{
	return cfs_rq->rb_leftmost;
}

static struct sched_entity *__pick_next_entity(struct cfs_rq *cfs_rq)
{
	return rb_entry(first_fair(cfs_rq), struct sched_entity, run_node);
}

static inline struct sched_entity *__pick_last_entity(struct cfs_rq *cfs_rq)
{
	struct rb_node **link = &cfs_rq->tasks_timeline.rb_node;
	struct sched_entity *se = NULL;
	struct rb_node *parent;

	while (*link) {
		parent = *link;
		se = rb_entry(parent, struct sched_entity, run_node);
		link = &parent->rb_right;
	}

	return se;
}

/**************************************************************
 * Scheduling class statistics methods:
 */

#ifdef CONFIG_SCHED_DEBUG
int sched_nr_latency_handler(struct ctl_table *table, int write,
		struct file *filp, void __user *buffer, size_t *lenp,
		loff_t *ppos)
{
	int ret = proc_dointvec_minmax(table, write, filp, buffer, lenp, ppos);

	if (ret || !write)
		return ret;

	sched_nr_latency = DIV_ROUND_UP(sysctl_sched_latency,
					sysctl_sched_min_granularity);

	return 0;
}
#endif

/*
 * The idea is to set a period in which each task runs once.
 *
 * When there are too many tasks (sysctl_sched_nr_latency) we have to stretch
 * this period because otherwise the slices get too small.
 *
 * p = (nr <= nl) ? l : l*nr/nl
 */
/* 确定CFS调度器的延迟周期长度，通常是sysctl_sched_latency */
static u64 __sched_period(unsigned long nr_running)
{
	u64 period = sysctl_sched_latency;
	unsigned long nr_latency = sched_nr_latency;

	if (unlikely(nr_running > nr_latency)) {/* 如果进程数量超过nr_latency，则同比例扩大延迟周期 */
		period *= nr_running;
		do_div(period, nr_latency);
	}

	return period;
}

/*
 * We calculate the wall-time slice from the period by taking a part
 * proportional to the weight.
 *
 * s = p*w/rw
 */
/**
 * 将延迟周期内的时间分配到某个活动进程。
 * 注:实际运行时间
 */
static u64 sched_slice(struct cfs_rq *cfs_rq, struct sched_entity *se)
{
	/* 计算延迟周期 */
	u64 slice = __sched_period(cfs_rq->nr_running);

	/* 根据进程在队列中的权重比例，确定分配给该进程的时间 */
	slice *= se->load.weight;
	do_div(slice, cfs_rq->load.weight);

	return slice;
}

/*
 * We calculate the vruntime slice.
 *
 * vs = s/w = p/rw
 */
static u64 __sched_vslice(unsigned long rq_weight, unsigned long nr_running)
{
	u64 vslice = __sched_period(nr_running);

	vslice *= NICE_0_LOAD;
	do_div(vslice, rq_weight);

	return vslice;
}

/* 计算队列的虚拟调度延迟时间 */
static u64 sched_vslice(struct cfs_rq *cfs_rq)
{
	return __sched_vslice(cfs_rq->load.weight, cfs_rq->nr_running);
}

static u64 sched_vslice_add(struct cfs_rq *cfs_rq, struct sched_entity *se)
{
	return __sched_vslice(cfs_rq->load.weight + se->load.weight,
			cfs_rq->nr_running + 1);
}

/*
 * Update the current task's runtime statistics. Skip current tasks that
 * are not in our scheduling class.
 */
static inline void
__update_curr(struct cfs_rq *cfs_rq, struct sched_entity *curr,
	      unsigned long delta_exec)
{
	unsigned long delta_exec_weighted;
	u64 vruntime;

	schedstat_set(curr->exec_max, max((u64)delta_exec, curr->exec_max));

	/* 更新进程的执行时间 */
	curr->sum_exec_runtime += delta_exec;
	schedstat_add(cfs_rq, exec_clock, delta_exec);
	/* 开始计算权重时间 */
	delta_exec_weighted = delta_exec;
	/* 对于nice==0的进程来说，虚拟权重时间与实际运行时间相等 */
	if (unlikely(curr->load.weight != NICE_0_LOAD)) {
		/* 对其他优先级来说，根据进程的负荷权重重新确定时间 */
		delta_exec_weighted = calc_delta_fair(delta_exec_weighted,
							&curr->load);
	}
	/* 递增进程虚拟运行时间 */
	curr->vruntime += delta_exec_weighted;

	/*
	 * maintain cfs_rq->min_vruntime to be a monotonic increasing
	 * value tracking the leftmost vruntime in the tree.
	 */
	if (first_fair(cfs_rq)) {/* 有等待调度的进程 */
		/* 取当前进程的虚拟运行时间和树中最小虚拟运行时间的最小值 */
		vruntime = min_vruntime(curr->vruntime,
				__pick_next_entity(cfs_rq)->vruntime);
	} else
		vruntime = curr->vruntime;

	/* 确保min_vruntime是单调递增的 */
	cfs_rq->min_vruntime =
		max_vruntime(cfs_rq->min_vruntime, vruntime);
}

/* 更新CFS进程运行时间，在周期性调度器及其他地方调用 */
static void update_curr(struct cfs_rq *cfs_rq)
{
	/* 获得当前任务和就绪队列时钟 */
	struct sched_entity *curr = cfs_rq->curr;
	u64 now = rq_of(cfs_rq)->clock;
	unsigned long delta_exec;

	if (unlikely(!curr))/* 没有CFS进程在运行，直接退出 */
		return;

	/*
	 * Get the amount of time the current task was running
	 * since the last time we changed load (this cannot
	 * overflow on 32 bits):
	 */
	/* 计算当前时间和上一次更新负荷统计量的时间差 */
	delta_exec = (unsigned long)(now - curr->exec_start);

	/* 更新统计时间 */
	__update_curr(cfs_rq, curr, delta_exec);
	curr->exec_start = now;

	if (entity_is_task(curr)) {
		struct task_struct *curtask = task_of(curr);

		cpuacct_charge(curtask, delta_exec);
	}
}

static inline void
update_stats_wait_start(struct cfs_rq *cfs_rq, struct sched_entity *se)
{
	schedstat_set(se->wait_start, rq_of(cfs_rq)->clock);
}

/*
 * Task is being enqueued - update stats:
 */
static void update_stats_enqueue(struct cfs_rq *cfs_rq, struct sched_entity *se)
{
	/*
	 * Are we enqueueing a waiting task? (for current tasks
	 * a dequeue/enqueue event is a NOP)
	 */
	if (se != cfs_rq->curr)
		update_stats_wait_start(cfs_rq, se);
}

static void
update_stats_wait_end(struct cfs_rq *cfs_rq, struct sched_entity *se)
{
	schedstat_set(se->wait_max, max(se->wait_max,
			rq_of(cfs_rq)->clock - se->wait_start));
	schedstat_set(se->wait_start, 0);
}

static inline void
update_stats_dequeue(struct cfs_rq *cfs_rq, struct sched_entity *se)
{
	/*
	 * Mark the end of the wait period if dequeueing a
	 * waiting task:
	 */
	if (se != cfs_rq->curr)
		update_stats_wait_end(cfs_rq, se);
}

/*
 * We are picking a new current task - update its stats:
 */
static inline void
update_stats_curr_start(struct cfs_rq *cfs_rq, struct sched_entity *se)
{
	/*
	 * We are starting a new run period:
	 */
	se->exec_start = rq_of(cfs_rq)->clock;
}

/**************************************************
 * Scheduling class queueing methods:
 */

static void
account_entity_enqueue(struct cfs_rq *cfs_rq, struct sched_entity *se)
{
	update_load_add(&cfs_rq->load, se->load.weight);
	cfs_rq->nr_running++;
	se->on_rq = 1;
}

static void
account_entity_dequeue(struct cfs_rq *cfs_rq, struct sched_entity *se)
{
	update_load_sub(&cfs_rq->load, se->load.weight);
	cfs_rq->nr_running--;
	se->on_rq = 0;
}

static void enqueue_sleeper(struct cfs_rq *cfs_rq, struct sched_entity *se)
{
#ifdef CONFIG_SCHEDSTATS
	if (se->sleep_start) {
		u64 delta = rq_of(cfs_rq)->clock - se->sleep_start;

		if ((s64)delta < 0)
			delta = 0;

		if (unlikely(delta > se->sleep_max))
			se->sleep_max = delta;

		se->sleep_start = 0;
		se->sum_sleep_runtime += delta;
	}
	if (se->block_start) {
		u64 delta = rq_of(cfs_rq)->clock - se->block_start;

		if ((s64)delta < 0)
			delta = 0;

		if (unlikely(delta > se->block_max))
			se->block_max = delta;

		se->block_start = 0;
		se->sum_sleep_runtime += delta;

		/*
		 * Blocking time is in units of nanosecs, so shift by 20 to
		 * get a milliseconds-range estimation of the amount of
		 * time that the task spent sleeping:
		 */
		if (unlikely(prof_on == SLEEP_PROFILING)) {
			struct task_struct *tsk = task_of(se);

			profile_hits(SLEEP_PROFILING, (void *)get_wchan(tsk),
				     delta >> 20);
		}
	}
#endif
}

static void check_spread(struct cfs_rq *cfs_rq, struct sched_entity *se)
{
#ifdef CONFIG_SCHED_DEBUG
	s64 d = se->vruntime - cfs_rq->min_vruntime;

	if (d < 0)
		d = -d;

	if (d > 3*sysctl_sched_latency)
		schedstat_inc(cfs_rq, nr_spread_over);
#endif
}

/**
 * initial表示是否为新创建的进程。
 */
static void
place_entity(struct cfs_rq *cfs_rq, struct sched_entity *se, int initial)
{
	u64 vruntime;

	/* 将队列的min_vruntime作为基准虚拟时间，以保证所有进程在延迟周期内运行一次 */
	vruntime = cfs_rq->min_vruntime;

	if (sched_feat(TREE_AVG)) {
		struct sched_entity *last = __pick_last_entity(cfs_rq);
		if (last) {
			vruntime += last->vruntime;
			vruntime >>= 1;
		}
	} else if (sched_feat(APPROX_AVG) && cfs_rq->nr_running)
		vruntime += sched_vslice(cfs_rq)/2;

	/*
	 * The 'current' period is already promised to the current tasks,
	 * however the extra weight of the new task will slow them down a
	 * little, place the new task so that it fits in the slot that
	 * stays open at the end.
	 */
	if (initial && sched_feat(START_DEBIT))
		vruntime += sched_vslice_add(cfs_rq, se);

	if (!initial) {
		/* sleeps upto a single latency don't count. */
		if (sched_feat(NEW_FAIR_SLEEPERS) && entity_is_task(se))
			/* 将基准时间减去延迟周期，以确保新创建的进程只有在当前延迟周期结束后再运行 */
			vruntime -= sysctl_sched_latency;

		/* ensure we never gain time by being placed backwards. */
		vruntime = max_vruntime(se->vruntime, vruntime);
	}

	se->vruntime = vruntime;
}

static void
enqueue_entity(struct cfs_rq *cfs_rq, struct sched_entity *se, int wakeup)
{
	/*
	 * Update run-time statistics of the 'current'.
	 */
	/* 更新当前进程的运行时间 */
	update_curr(cfs_rq);

	if (wakeup) {/* 进程被唤醒，重新入队 */
		/* 确定进程正确的虚拟运行时间 */
		place_entity(cfs_rq, se, 0);
		enqueue_sleeper(cfs_rq, se);
	}

	update_stats_enqueue(cfs_rq, se);
	check_spread(cfs_rq, se);
	if (se != cfs_rq->curr)/* 根据进程的虚拟运行时间，将进程放置到红黑树中 */
		__enqueue_entity(cfs_rq, se);
	account_entity_enqueue(cfs_rq, se);
}

static void
dequeue_entity(struct cfs_rq *cfs_rq, struct sched_entity *se, int sleep)
{
	/*
	 * Update run-time statistics of the 'current'.
	 */
	update_curr(cfs_rq);

	update_stats_dequeue(cfs_rq, se);
	if (sleep) {
#ifdef CONFIG_SCHEDSTATS
		if (entity_is_task(se)) {
			struct task_struct *tsk = task_of(se);

			if (tsk->state & TASK_INTERRUPTIBLE)
				se->sleep_start = rq_of(cfs_rq)->clock;
			if (tsk->state & TASK_UNINTERRUPTIBLE)
				se->block_start = rq_of(cfs_rq)->clock;
		}
#endif
	}

	if (se != cfs_rq->curr)
		__dequeue_entity(cfs_rq, se);
	account_entity_dequeue(cfs_rq, se);
}

/*
 * Preempt the current task with a newly woken task if needed:
 */
/**
 * 处理CFS进程抢占，确保没有任何一个进程可以比延迟周期中确定的份额运行得更长
 */
static void
check_preempt_tick(struct cfs_rq *cfs_rq, struct sched_entity *curr)
{
	unsigned long ideal_runtime, delta_exec;

	/* 计算进程的份额 */
	ideal_runtime = sched_slice(cfs_rq, curr);
	/* 计算已经运行的时间 */
	delta_exec = curr->sum_exec_runtime - curr->prev_sum_exec_runtime;
	/* 进程运行的时间比期望的份额更长 */
	if (delta_exec > ideal_runtime)
		/* 发出重新调度请求 */
		resched_task(rq_of(cfs_rq)->curr);
}

static void
set_next_entity(struct cfs_rq *cfs_rq, struct sched_entity *se)
{
	/* 'current' is not kept within the tree. */
	if (se->on_rq) {/* 进程在就绪队列中，不是当前运行进程 */
		/*
		 * Any task has to be enqueued before it get to execute on
		 * a CPU. So account for the time it spent waiting on the
		 * runqueue.
		 */
		update_stats_wait_end(cfs_rq, se);
		/* 从就绪队列中移除，如果移除的进程是最左边的进程，还需要修改最左边节点 */
		__dequeue_entity(cfs_rq, se);
	}

	update_stats_curr_start(cfs_rq, se);
	/* 记录CFS队列中的当前进程 */
	cfs_rq->curr = se;
#ifdef CONFIG_SCHEDSTATS
	/*
	 * Track our maximum slice length, if the CPU's load is at
	 * least twice that of our own weight (i.e. dont track it
	 * when there are only lesser-weight tasks around):
	 */
	if (rq_of(cfs_rq)->load.weight >= 2*se->load.weight) {
		se->slice_max = max(se->slice_max,
			se->sum_exec_runtime - se->prev_sum_exec_runtime);
	}
#endif
	se->prev_sum_exec_runtime = se->sum_exec_runtime;
}

static struct sched_entity *pick_next_entity(struct cfs_rq *cfs_rq)
{
	struct sched_entity *se = NULL;

	if (first_fair(cfs_rq)) {/* 红黑树中有结点 */
		/* 从红黑树中取出最左边结点的sched_entity */
		se = __pick_next_entity(cfs_rq);
		/* 将选择的进程标记为运行进程  */
		set_next_entity(cfs_rq, se);
	}

	return se;
}

static void put_prev_entity(struct cfs_rq *cfs_rq, struct sched_entity *prev)
{
	/*
	 * If still on the runqueue then deactivate_task()
	 * was not called and update_curr() has to be done:
	 */
	if (prev->on_rq)
		update_curr(cfs_rq);

	check_spread(cfs_rq, prev);
	if (prev->on_rq) {
		update_stats_wait_start(cfs_rq, prev);
		/* Put 'current' back into the tree. */
		__enqueue_entity(cfs_rq, prev);
	}
	cfs_rq->curr = NULL;
}

static void entity_tick(struct cfs_rq *cfs_rq, struct sched_entity *curr)
{
	/*
	 * Update run-time statistics of the 'current'.
	 */
	/* 更新当前进程的运行时间 */
	update_curr(cfs_rq);

	/* 如果队列上可用进程数量不足两个，则不必进行切换 */
	if (cfs_rq->nr_running > 1 || !sched_feat(WAKEUP_PREEMPT))
		/* 否则，处理CFS进程抢占 */
		check_preempt_tick(cfs_rq, curr);
}

/**************************************************
 * CFS operations on tasks:
 */

#ifdef CONFIG_FAIR_GROUP_SCHED

/* Walk up scheduling entities hierarchy */
#define for_each_sched_entity(se) \
		for (; se; se = se->parent)

static inline struct cfs_rq *task_cfs_rq(struct task_struct *p)
{
	return p->se.cfs_rq;
}

/* runqueue on which this entity is (to be) queued */
static inline struct cfs_rq *cfs_rq_of(struct sched_entity *se)
{
	return se->cfs_rq;
}

/* runqueue "owned" by this group */
static inline struct cfs_rq *group_cfs_rq(struct sched_entity *grp)
{
	return grp->my_q;
}

/* Given a group's cfs_rq on one cpu, return its corresponding cfs_rq on
 * another cpu ('this_cpu')
 */
static inline struct cfs_rq *cpu_cfs_rq(struct cfs_rq *cfs_rq, int this_cpu)
{
	return cfs_rq->tg->cfs_rq[this_cpu];
}

/* Iterate thr' all leaf cfs_rq's on a runqueue */
#define for_each_leaf_cfs_rq(rq, cfs_rq) \
	list_for_each_entry(cfs_rq, &rq->leaf_cfs_rq_list, leaf_cfs_rq_list)

/* Do the two (enqueued) entities belong to the same group ? */
static inline int
is_same_group(struct sched_entity *se, struct sched_entity *pse)
{
	if (se->cfs_rq == pse->cfs_rq)
		return 1;

	return 0;
}

static inline struct sched_entity *parent_entity(struct sched_entity *se)
{
	return se->parent;
}

#else	/* CONFIG_FAIR_GROUP_SCHED */

#define for_each_sched_entity(se) \
		for (; se; se = NULL)

static inline struct cfs_rq *task_cfs_rq(struct task_struct *p)
{
	return &task_rq(p)->cfs;
}

static inline struct cfs_rq *cfs_rq_of(struct sched_entity *se)
{
	struct task_struct *p = task_of(se);
	struct rq *rq = task_rq(p);

	return &rq->cfs;
}

/* runqueue "owned" by this group */
static inline struct cfs_rq *group_cfs_rq(struct sched_entity *grp)
{
	return NULL;
}

static inline struct cfs_rq *cpu_cfs_rq(struct cfs_rq *cfs_rq, int this_cpu)
{
	return &cpu_rq(this_cpu)->cfs;
}

#define for_each_leaf_cfs_rq(rq, cfs_rq) \
		for (cfs_rq = &rq->cfs; cfs_rq; cfs_rq = NULL)

static inline int
is_same_group(struct sched_entity *se, struct sched_entity *pse)
{
	return 1;
}

static inline struct sched_entity *parent_entity(struct sched_entity *se)
{
	return NULL;
}

#endif	/* CONFIG_FAIR_GROUP_SCHED */

/*
 * The enqueue_task method is called before nr_running is
 * increased. Here we update the fair scheduling stats and
 * then put the task into the rbtree:
 */
/**
 * 将进程加入到CFS队列中
 * wakeup表示是否第一次进入队列
 */
static void enqueue_task_fair(struct rq *rq, struct task_struct *p, int wakeup)
{
	struct cfs_rq *cfs_rq;
	struct sched_entity *se = &p->se;

	/* 遍历调度实体的cgroups层次结构，将每一个层次中的对象都加入到队列中 */
	for_each_sched_entity(se) {
		if (se->on_rq)/* 调度实体已经在队列中，直接退出 */
			break;
		cfs_rq = cfs_rq_of(se);
		/* 将进程添加到队列中 */
		enqueue_entity(cfs_rq, se, wakeup);
		wakeup = 1;
	}
}

/*
 * The dequeue_task method is called before nr_running is
 * decreased. We remove the task from the rbtree and
 * update the fair scheduling stats:
 */
static void dequeue_task_fair(struct rq *rq, struct task_struct *p, int sleep)
{
	struct cfs_rq *cfs_rq;
	struct sched_entity *se = &p->se;

	for_each_sched_entity(se) {
		cfs_rq = cfs_rq_of(se);
		dequeue_entity(cfs_rq, se, sleep);
		/* Don't dequeue parent if it has other entities besides us */
		if (cfs_rq->load.weight)
			break;
		sleep = 1;
	}
}

/*
 * sched_yield() support is very simple - we dequeue and enqueue.
 *
 * If compat_yield is turned on then we requeue to the end of the tree.
 */
static void yield_task_fair(struct rq *rq)
{
	struct task_struct *curr = rq->curr;
	struct cfs_rq *cfs_rq = task_cfs_rq(curr);
	struct sched_entity *rightmost, *se = &curr->se;

	/*
	 * Are we the only task in the tree?
	 */
	if (unlikely(cfs_rq->nr_running == 1))
		return;

	if (likely(!sysctl_sched_compat_yield) && curr->policy != SCHED_BATCH) {
		__update_rq_clock(rq);
		/*
		 * Update run-time statistics of the 'current'.
		 */
		update_curr(cfs_rq);

		return;
	}
	/*
	 * Find the rightmost entry in the rbtree:
	 */
	rightmost = __pick_last_entity(cfs_rq);
	/*
	 * Already in the rightmost position?
	 */
	if (unlikely(rightmost->vruntime < se->vruntime))
		return;

	/*
	 * Minimally necessary key value to be last in the tree:
	 * Upon rescheduling, sched_class::put_prev_task() will place
	 * 'current' within the tree based on its new key value.
	 */
	se->vruntime = rightmost->vruntime + 1;
}

/*
 * Preempt the current task with a newly woken task if needed:
 */
/**
 * 当进程被try_to_wake_up和wake_up_new_task唤醒时，调用此函数检查是否可以抢占当前进程
 */
static void check_preempt_wakeup(struct rq *rq, struct task_struct *p)
{
	struct task_struct *curr = rq->curr;
	struct cfs_rq *cfs_rq = task_cfs_rq(curr);
	struct sched_entity *se = &curr->se, *pse = &p->se;
	unsigned long gran;

	/* 新唤醒的进程是实时进程 */
	if (unlikely(rt_prio(p->prio))) {
		update_rq_clock(rq);
		update_curr(cfs_rq);
		/* 实时进程总是抢占CFS进程 */
		resched_task(curr);
		return;
	}
	/*
	 * Batch tasks do not preempt (their preemption is driven by
	 * the tick):
	 */
	/* 新进程是BATCH进程，它不抢占其他进程 */
	if (unlikely(p->policy == SCHED_BATCH))
		return;

	/* 没有配置唤醒抢占功能 */
	if (!sched_feat(WAKEUP_PREEMPT))
		return;

	while (!is_same_group(se, pse)) {
		se = parent_entity(se);
		pse = parent_entity(pse);
	}

	/* 计算最小运行时间，默认是4ms */
	gran = sysctl_sched_wakeup_granularity;
	/* 计算最小运行时间对应的虚拟时间 */
	if (unlikely(se->load.weight != NICE_0_LOAD))
		gran = calc_delta_fair(gran, &se->load);

	/* 
	 * 新进程的虚拟运行时间加上最小运行时间仍然小于当前进程的配额
	 * 说明新进程优先级较高，抢占当前进程 
	 */
	if (pse->vruntime + gran < se->vruntime)
		resched_task(curr);
}

/**
 * 选择下一个将要运行的进程
 */
static struct task_struct *pick_next_task_fair(struct rq *rq)
{
	struct cfs_rq *cfs_rq = &rq->cfs;
	struct sched_entity *se;

	if (unlikely(!cfs_rq->nr_running))/* 没有可运行的进程 */
		return NULL;

	do {
		/* 选择红黑树最左边的进程 */
		se = pick_next_entity(cfs_rq);
		cfs_rq = group_cfs_rq(se);
	} while (cfs_rq);

	return task_of(se);
}

/*
 * Account for a descheduled task:
 */
static void put_prev_task_fair(struct rq *rq, struct task_struct *prev)
{
	struct sched_entity *se = &prev->se;
	struct cfs_rq *cfs_rq;

	for_each_sched_entity(se) {
		cfs_rq = cfs_rq_of(se);
		put_prev_entity(cfs_rq, se);
	}
}

#ifdef CONFIG_SMP
/**************************************************
 * Fair scheduling class load-balancing methods:
 */

/*
 * Load-balancing iterator. Note: while the runqueue stays locked
 * during the whole iteration, the current task might be
 * dequeued so the iterator has to be dequeue-safe. Here we
 * achieve that by always pre-iterating before returning
 * the current task:
 */
static struct task_struct *
__load_balance_iterator(struct cfs_rq *cfs_rq, struct rb_node *curr)
{
	struct task_struct *p;

	if (!curr)
		return NULL;

	p = rb_entry(curr, struct task_struct, se.run_node);
	cfs_rq->rb_load_balance_curr = rb_next(curr);

	return p;
}

static struct task_struct *load_balance_start_fair(void *arg)
{
	struct cfs_rq *cfs_rq = arg;

	return __load_balance_iterator(cfs_rq, first_fair(cfs_rq));
}

static struct task_struct *load_balance_next_fair(void *arg)
{
	struct cfs_rq *cfs_rq = arg;

	return __load_balance_iterator(cfs_rq, cfs_rq->rb_load_balance_curr);
}

#ifdef CONFIG_FAIR_GROUP_SCHED
static int cfs_rq_best_prio(struct cfs_rq *cfs_rq)
{
	struct sched_entity *curr;
	struct task_struct *p;

	if (!cfs_rq->nr_running)
		return MAX_PRIO;

	curr = cfs_rq->curr;
	if (!curr)
		curr = __pick_next_entity(cfs_rq);

	p = task_of(curr);

	return p->prio;
}
#endif

static unsigned long
load_balance_fair(struct rq *this_rq, int this_cpu, struct rq *busiest,
		  unsigned long max_load_move,
		  struct sched_domain *sd, enum cpu_idle_type idle,
		  int *all_pinned, int *this_best_prio)
{
	struct cfs_rq *busy_cfs_rq;
	long rem_load_move = max_load_move;
	struct rq_iterator cfs_rq_iterator;

	cfs_rq_iterator.start = load_balance_start_fair;
	cfs_rq_iterator.next = load_balance_next_fair;

	for_each_leaf_cfs_rq(busiest, busy_cfs_rq) {
#ifdef CONFIG_FAIR_GROUP_SCHED
		struct cfs_rq *this_cfs_rq;
		long imbalance;
		unsigned long maxload;

		this_cfs_rq = cpu_cfs_rq(busy_cfs_rq, this_cpu);

		imbalance = busy_cfs_rq->load.weight - this_cfs_rq->load.weight;
		/* Don't pull if this_cfs_rq has more load than busy_cfs_rq */
		if (imbalance <= 0)
			continue;

		/* Don't pull more than imbalance/2 */
		imbalance /= 2;
		maxload = min(rem_load_move, imbalance);

		*this_best_prio = cfs_rq_best_prio(this_cfs_rq);
#else
# define maxload rem_load_move
#endif
		/*
		 * pass busy_cfs_rq argument into
		 * load_balance_[start|next]_fair iterators
		 */
		cfs_rq_iterator.arg = busy_cfs_rq;
		rem_load_move -= balance_tasks(this_rq, this_cpu, busiest,
					       maxload, sd, idle, all_pinned,
					       this_best_prio,
					       &cfs_rq_iterator);

		if (rem_load_move <= 0)
			break;
	}

	return max_load_move - rem_load_move;
}

static int
move_one_task_fair(struct rq *this_rq, int this_cpu, struct rq *busiest,
		   struct sched_domain *sd, enum cpu_idle_type idle)
{
	struct cfs_rq *busy_cfs_rq;
	struct rq_iterator cfs_rq_iterator;

	cfs_rq_iterator.start = load_balance_start_fair;
	cfs_rq_iterator.next = load_balance_next_fair;

	for_each_leaf_cfs_rq(busiest, busy_cfs_rq) {
		/*
		 * pass busy_cfs_rq argument into
		 * load_balance_[start|next]_fair iterators
		 */
		cfs_rq_iterator.arg = busy_cfs_rq;
		if (iter_move_one_task(this_rq, this_cpu, busiest, sd, idle,
				       &cfs_rq_iterator))
		    return 1;
	}

	return 0;
}
#endif

/*
 * scheduler tick hitting a task of our scheduling class:
 */
/* 处理CFS调度器的周期性调度 */
static void task_tick_fair(struct rq *rq, struct task_struct *curr)
{
	struct cfs_rq *cfs_rq;
	struct sched_entity *se = &curr->se;

	for_each_sched_entity(se) {
		cfs_rq = cfs_rq_of(se);
		/* 实际工作由entity_tick完成 */
		entity_tick(cfs_rq, se);
	}
}

#define swap(a, b) do { typeof(a) tmp = (a); (a) = (b); (b) = tmp; } while (0)

/*
 * Share the fairness runtime between parent and child, thus the
 * total amount of pressure for CPU stays equal - new tasks
 * get a chance to run but frequent forkers are not allowed to
 * monopolize the CPU. Note: the parent runqueue is locked,
 * the child is not running yet.
 */
/**
 * 创建新进程时，
 */
static void task_new_fair(struct rq *rq, struct task_struct *p)
{
	struct cfs_rq *cfs_rq = task_cfs_rq(p);
	struct sched_entity *se = &p->se, *curr = cfs_rq->curr;
	int this_cpu = smp_processor_id();

	sched_info_queued(p);

	/* 更新当前进程的运行时间 */
	update_curr(cfs_rq);
	/* 将新进程放置到红黑树中，initial参数表示是新创建的进程 */
	place_entity(cfs_rq, se, 1);

	/* 'curr' will be NULL if the child belongs to a different group */
	if (sysctl_sched_child_runs_first && this_cpu == task_cpu(p) &&/* 子进程先运行，子进程与父进程在同一CPU上 */
			curr && curr->vruntime < se->vruntime) {/* 当前进程的虚拟运行时间小于子进程的运行时间 */
		/*
		 * Upon rescheduling, sched_class::put_prev_task() will place
		 * 'current' within the tree based on its new key value.
		 */
		/* 交换二者的运行时间，随后会将当前进程插入到红黑树合适的位置 */
		swap(curr->vruntime, se->vruntime);
	}

	/* 将新进程加入到运行队列并重新调度 */
	enqueue_task_fair(rq, p, 0);
	resched_task(rq->curr);
}

/* Account for a task changing its policy or group.
 *
 * This routine is mostly called to set cfs_rq->curr field when a task
 * migrates between groups/classes.
 */
static void set_curr_task_fair(struct rq *rq)
{
	struct sched_entity *se = &rq->curr->se;

	for_each_sched_entity(se)
		set_next_entity(cfs_rq_of(se), se);
}

/*
 * All the scheduling class methods:
 */
static const struct sched_class fair_sched_class = {
	.next			= &idle_sched_class,
	.enqueue_task		= enqueue_task_fair,
	.dequeue_task		= dequeue_task_fair,
	.yield_task		= yield_task_fair,

	.check_preempt_curr	= check_preempt_wakeup,

	.pick_next_task		= pick_next_task_fair,
	.put_prev_task		= put_prev_task_fair,

#ifdef CONFIG_SMP
	.load_balance		= load_balance_fair,
	.move_one_task		= move_one_task_fair,
#endif

	.set_curr_task          = set_curr_task_fair,
	.task_tick		= task_tick_fair,
	.task_new		= task_new_fair,
};

#ifdef CONFIG_SCHED_DEBUG
static void print_cfs_stats(struct seq_file *m, int cpu)
{
	struct cfs_rq *cfs_rq;

#ifdef CONFIG_FAIR_GROUP_SCHED
	print_cfs_rq(m, cpu, &cpu_rq(cpu)->cfs);
#endif
	for_each_leaf_cfs_rq(cpu_rq(cpu), cfs_rq)
		print_cfs_rq(m, cpu, cfs_rq);
}
#endif
