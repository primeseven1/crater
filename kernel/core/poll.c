#include <crater/poll.h>
#include <crater/vfs.h>
#include <crater/slab.h>
#include <crater/timekeeper.h>

struct poll_chunk {
	size_t count;
	struct poll_entry entries[56];
	struct list_node link;
};

static bool poll_wake_cb(struct waitq_entry* wait, unsigned long key) {
	struct poll_entry* entry = container_of(wait, struct poll_entry, wqe);
	struct poll_table* pt = wait->private;

	/* A wakeup without a key could be anything, so it's never filtered */
	if (key && (key & (unsigned long)entry->events) == 0)
		return false;

	unsigned long irq_flags;
	spinlock_acquire_irq_save(&pt->wq.lock, &irq_flags);
	pt->triggered = true;
	waitq_wake_locked(&pt->wq, 0, 0);
	spinlock_release_irq_restore(&pt->wq.lock, &irq_flags);

	return true;
}

void poll_table_init(struct poll_table* pt) {
	waitq_init(&pt->wq);
	pt->triggered = false;
	pt->registering = true;
	pt->events = ~0;
	pt->error = 0;
	list_head_init(&pt->chunks);
}

static inline void poll_entry_remove(struct poll_entry* entry) {
	waitq_remove(entry->wq, &entry->wqe);
}

void poll_table_destroy(struct poll_table* pt) {
	struct poll_chunk* chunk, *tmp;
	list_for_each_entry_safe(chunk, tmp, &pt->chunks, link) {
		for (unsigned int i = 0; i < chunk->count; i++)
			poll_entry_remove(&chunk->entries[i]);
		list_remove(&chunk->link);
		kfree(chunk);
	}
}

static struct poll_entry* poll_table_new_entry(struct poll_table* pt) {
	/* The newest chunk is always first, and the only one that can have room */
	struct poll_chunk* chunk = NULL;
	if (!list_empty(&pt->chunks))
		chunk = list_first_entry(&pt->chunks, struct poll_chunk, link);
	if (!chunk || chunk->count == ARRAY_SIZE(chunk->entries)) {
		chunk = kmalloc(sizeof(*chunk), MM_ZONE_NORMAL);
		if (!chunk)
			return NULL;
		chunk->count = 0;
		list_node_init(&chunk->link);
		list_add(&pt->chunks, &chunk->link);
	}
	return &chunk->entries[chunk->count++];
}

void poll_table_add(struct poll_table* pt, struct waitq* wq) {
	if (!pt || !pt->registering || !wq)
		return;

	struct poll_entry* entry = poll_table_new_entry(pt);
	if (!entry) {
		pt->error = -ENOMEM;
		pt->registering = false;
		return;
	}

	entry->wq = wq;
	entry->events = pt->events;
	waitq_entry_init(&entry->wqe, current_thread(), poll_wake_cb, pt);
	waitq_add(wq, &entry->wqe);
}

static inline bool poll_table_take_trigger(struct poll_table* pt) {
	if (!pt->triggered)
		return false;
	pt->triggered = false;
	return true;
}

int poll_table_sleep(struct poll_table* pt, time_t us, int flags) {
	if (us < 0)
		return waitq_wait_event(&pt->wq, poll_table_take_trigger(pt), NULL, NULL, flags);
	return waitq_wait_event_timed(&pt->wq, poll_table_take_trigger(pt), NULL, NULL, us, flags);
}

static int poll_scan(struct poll_vnode* vnodes, size_t count, struct poll_table* pt) {
	int ready = 0;
	for (size_t i = 0; i < count; i++) {
		vnodes[i].revents = 0;
		if (!vnodes[i].vnode)
			continue;

		short events = vfs_poll(vnodes[i].vnode, pt, vnodes[i].events);
		if (events) {
			vnodes[i].revents = events;
			ready++;
			pt->registering = false; /* Not going to sleep, so there is no point in adding the rest */
		}
	}

	return ready;
}

int poll_vnodes(struct poll_vnode* vnodes, size_t count, time_t us, int flags) {
	if (count > INT_MAX)
		return -EINVAL;

	struct poll_table pt;
	poll_table_init(&pt);

	bool timed_out = false;
	struct timespec deadline = { .tv_sec = 0, .tv_nsec = 0 };
	if (us == 0) {
		timed_out = true;
		pt.registering = false;
	} else if (us > 0) {
		deadline = timespec_add(time_fromboot(), timespec_from_us(us));
	}

	int ready;
	int err = 0;
	while (1) {
		/* Only the first scan adds waitqueues, they stay in the poll table until it's destroyed */
		ready = poll_scan(vnodes, count, &pt);
		pt.registering = false;
		if (ready || timed_out)
			break;

		err = pt.error;
		if (err)
			break;

		time_t remaining = -1;
		if (us > 0) {
			const struct timespec now = time_fromboot();
			if (timespec_cmp(now, deadline) >= 0)
				break;
			remaining = timespec_us(timespec_sub(deadline, now));
			if (remaining == 0)
				remaining = 1;
		}

		err = poll_table_sleep(&pt, remaining, flags);
		if (err) {
			if (err == -ETIME)
				timed_out = true; /* Do one last scan */
			else
				break;
			err = 0;
		}
	}

	poll_table_destroy(&pt);
	return ready ? ready : err;
}
