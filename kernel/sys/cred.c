#include <crater/cred.h>
#include <crater/proc.h>

static struct cred kernel_cred = {
	.uid = 0, .euid = 0, .suid = 0,
	.gid = 0, .egid = 0, .sgid = 0
};

struct cred* current_cred(void) {
	struct thread* thread = current_thread();
	struct proc* proc = likely(thread) ? atomic_load(&thread->proc) : NULL;
	return likely(proc) ? &proc->cred : &kernel_cred;
}
