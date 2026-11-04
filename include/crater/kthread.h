#pragma once

#include <crater/sched.h>

struct thread* kthread_create(int flags, int (*threadfn)(void*), void* arg, const char* fmt, ...);
int kthread_run(struct thread* thread, int prio);
void kthread_destroy(struct thread* thread);
void kthread_detach(struct thread* thread);
_Noreturn void kthread_exit(int exit);
