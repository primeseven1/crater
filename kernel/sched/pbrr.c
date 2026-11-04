#include <crater/slab.h>
#include <crater/panic.h>
#include <crater/sched_policy.h>

#define PBRR_PRIO_COUNT 32
#define PBRR_MIN_PRIO 0
#define PBRR_MAX_PRIO 31
#define PBRR_PRIO_GROUP_SHIFT 3
#define DEFAULT_SLICE_TICKS 10

struct rr_thread {
	struct thread thread;
	unsigned long slice_left;
	int prio;
	struct list_node link;
};

struct rr_runqueue {
	struct list_head queues[PBRR_PRIO_COUNT];
	u32 active_bitmap;
	int prio_budget[PBRR_PRIO_COUNT];
};

/* Sanity check */
static_assert(PBRR_PRIO_COUNT <= (sizeof(((struct rr_runqueue*)0)->active_bitmap)) * 8, "pbrr bitmap too small");

static inline int prio_weight(int p) {
	return 1ul << (p >> PBRR_PRIO_GROUP_SHIFT);
}

static inline void reset_budgets(struct rr_runqueue* rrq) {
	for (int p = 0; p < PBRR_PRIO_COUNT; p++)
		rrq->prio_budget[p] = prio_weight(p);
}

static int scale_prio(int posix_prio) {
	int span_posix = (SCHED_PRIO_MAX - SCHED_PRIO_MIN);
	int span_pbrr = (PBRR_MAX_PRIO - PBRR_MIN_PRIO);
	int scaled0 = ((posix_prio - SCHED_PRIO_MIN) * span_pbrr + span_posix / 2) / span_posix;
	int scaled = PBRR_MIN_PRIO + scaled0;
	bug(scaled < PBRR_MIN_PRIO || scaled > PBRR_MAX_PRIO);
	return scaled;
}

static struct slab_cache* rr_thread_cache;

static struct thread* pbrr_alloc(void) {
	struct rr_thread* rr_thread = slab_cache_alloc(rr_thread_cache);
	return rr_thread ? &rr_thread->thread : NULL;
}

static void pbrr_free(struct thread* thread) {
	struct rr_thread* rr_thread = container_of(thread, struct rr_thread, thread);
	slab_cache_free(rr_thread_cache, rr_thread);
}

static int pbrr_attach(struct runqueue* rq, struct thread* thread, int posix_prio) {
	(void)rq;
	struct rr_thread* rr_thread = container_of(thread, struct rr_thread, thread);
	int rr_prio = scale_prio(posix_prio);

	THREAD_HOLD(thread);
	rr_thread->slice_left = DEFAULT_SLICE_TICKS;
	rr_thread->prio = rr_prio;
	list_node_init(&rr_thread->link);

	return 0;
}

static void pbrr_detach(struct runqueue* rq, struct thread* thread) {
	(void)rq;
	THREAD_RELEASE(thread);
}

static int pbrr_init(struct runqueue* rq) {
	if (!rr_thread_cache) {
		rr_thread_cache = slab_cache_create(sizeof(struct rr_thread), alignof(struct rr_thread), MM_ZONE_NORMAL, NULL, NULL);
		if (!rr_thread_cache)
			return -ENOMEM;
	}

	rq->policy_priv = kmalloc(sizeof(struct rr_runqueue), MM_ZONE_NORMAL);
	if (!rq->policy_priv)
		return -ENOMEM;

	struct rr_runqueue* const rr_rq = rq->policy_priv;
	for (size_t i = 0; i < ARRAY_SIZE(rr_rq->queues); i++)
		list_head_init(&rr_rq->queues[i]);
	rr_rq->active_bitmap = 0;
	reset_budgets(rr_rq);

	return 0;
}

static int pbrr_enqueue(struct runqueue* rq, struct thread* thread) {
	struct rr_thread* rr_thread = container_of(thread, struct rr_thread, thread);
	if (list_node_linked(&rr_thread->link))
		return -EALREADY;

	struct rr_runqueue* rr_rq = rq->policy_priv;
	list_add_tail(&rr_rq->queues[rr_thread->prio], &rr_thread->link);
	rr_rq->active_bitmap |= (1ul << rr_thread->prio);

	return 0;
}

static int pbrr_dequeue(struct runqueue* rq, struct thread* thread) {
	struct rr_thread* rr_thread = container_of(thread, struct rr_thread, thread);
	if (!list_node_linked(&rr_thread->link))
		return -ENOENT;

	struct rr_runqueue* rr_rq = rq->policy_priv;
	int rr_prio = rr_thread->prio;
	list_remove(&rr_thread->link);
	if (list_empty(&rr_rq->queues[rr_prio]))
		rr_rq->active_bitmap &= ~(1ul << rr_prio);

	return 0;
}

static int highest_ready_prio_budget(struct rr_runqueue* rrq) {
	unsigned long bm = rrq->active_bitmap;
	while (bm) {
		int p = (int)((sizeof(unsigned long) * 8) - 1 - __builtin_clzl(bm));
		if (rrq->prio_budget[p] > 0)
			return p;
		bm &= ~(1ul << p);
	}
	return -1;
}

static struct rr_thread* pop_head_and_maybe_clear(struct rr_runqueue* rrq, int prio) {
	struct list_head* const head = &rrq->queues[prio];
	if (list_empty(head))
		return NULL;

	struct rr_thread* const rr_thread = list_first_entry(head, struct rr_thread, link);
	list_remove(&rr_thread->link);
	if (list_empty(head))
		rrq->active_bitmap &= ~(1ul << prio);

	return rr_thread;
}

static struct thread* pbrr_pick_next(struct runqueue* rq) {
	struct rr_runqueue* const rr_rq = rq->policy_priv;

	int p = highest_ready_prio_budget(rr_rq);
	if (p < 0) {
		reset_budgets(rr_rq);
		p = highest_ready_prio_budget(rr_rq);
		if (p < 0)
			return NULL;
	}

	struct rr_thread* const next_rr_thread = pop_head_and_maybe_clear(rr_rq, p);
	bug(next_rr_thread == NULL); /* Well, I guess the bitmap lied to us!! */
	if (rr_rq->prio_budget[p] > 0)
		rr_rq->prio_budget[p]--;

	next_rr_thread->slice_left = DEFAULT_SLICE_TICKS;
	return &next_rr_thread->thread;
}

static int pbrr_change_prio(struct runqueue* rq, struct thread* thread, int posix_prio) {
	int prio = scale_prio(posix_prio);

	struct rr_runqueue* const rr_rq = rq->policy_priv;
	struct rr_thread* const rr_thread = container_of(thread, struct rr_thread, thread);

	if (rr_thread->prio == prio)
		return 0;
	if (list_node_linked(&rr_thread->link)) {
		list_remove(&rr_thread->link);
		if (list_empty(&rr_rq->queues[rr_thread->prio]))
			rr_rq->active_bitmap &= ~(1ul << rr_thread->prio);
		list_add_tail(&rr_rq->queues[prio], &rr_thread->link);
		rr_rq->active_bitmap |= (1ul << prio);
	}
	rr_thread->prio = prio;

	return 0;
}

static bool pbrr_tick(struct runqueue* rq, struct thread* current) {
	(void)rq;
	struct rr_thread* rr_current = container_of(current, struct rr_thread, thread);
	if (rr_current->slice_left == 0)
		return true;
	if (--rr_current->slice_left == 0)
		return true;
	return false;
}

static void pbrr_yield(struct runqueue* rq, struct thread* current) {
	(void)rq;
	const int state = atomic_load(&current->state);
	if (state != THREAD_RUNNING && state != THREAD_READY)
		return;

	bug(pbrr_enqueue(rq, current) != 0);
}

static const struct sched_policy_ops pbrr_ops = {
	.init = pbrr_init,
	.alloc = pbrr_alloc,
	.free = pbrr_free,
	.attach = pbrr_attach,
	.detach = pbrr_detach,
	.enqueue = pbrr_enqueue,
	.dequeue = pbrr_dequeue,
	.pick_next = pbrr_pick_next,
	.change_prio = pbrr_change_prio,
	.tick = pbrr_tick,
	.yield = pbrr_yield
};

static struct sched_policy __sched_policy pbrr = {
	.name = "pbrr",
	.desc = "Priority-based round robin",
	.ops = &pbrr_ops
};

/* RR uses PBRR, but just puts everything into the same queue */

static int rr_attach(struct runqueue* rq, struct thread* thread, int posix_prio) {
	(void)posix_prio;
	return pbrr_attach(rq, thread, SCHED_PRIO_MAX);
}

static int rr_change_prio(struct runqueue* rq, struct thread* thread, int posix_prio) {
	(void)rq;
	(void)thread;
	(void)posix_prio;
	return -ENOTSUP;
}

static const struct sched_policy_ops rr_ops = {
	.init = pbrr_init,
	.alloc = pbrr_alloc,
	.free = pbrr_free,
	.attach = rr_attach,
	.detach = pbrr_detach,
	.enqueue = pbrr_enqueue,
	.dequeue = pbrr_dequeue,
	.pick_next = pbrr_pick_next,
	.change_prio = rr_change_prio,
	.tick = pbrr_tick,
	.yield = pbrr_yield
};

static struct sched_policy __sched_policy rr = {
	.name = "rr",
	.desc = "Round robin",
	.ops = &rr_ops
};
