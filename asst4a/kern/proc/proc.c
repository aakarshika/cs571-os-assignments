/*
 * Copyright (c) 2013
 *	The President and Fellows of Harvard College.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE UNIVERSITY AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE UNIVERSITY OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
* Process support.
*
* There is (intentionally) not much here; you will need to add stuff
* and maybe change around what's already present.
*
* p_lock is intended to be held when manipulating the pointers in the
* proc structure, not while doing any significant work with the
* things they point to. Rearrange this (and/or change it to be a
* regular lock) as needed.
*
* Unless you're implementing multithreaded user processes, the only
* process that will have more than one thread is the kernel process.
*/

#include <types.h>
#include <kern/errno.h>
#include <spl.h>

#define PROCINLINE

#include <proc.h>
#include <current.h>
#include <addrspace.h>
#include <vnode.h>
#include <filetable.h>
#include <synch.h>
#include <array.h>

/* The process for the kernel; this holds all the kernel-only threads.
 */
struct proc *kproc;

struct lock *p_table_lock;
struct semaphore* p_kern_sem;

static struct procarray p_table;
static int n_pr;
/*
 * Create a proc structure.
 */
static
struct proc *
proc_create(const char *name)
{
    
	struct proc *proc;
	int newpid=0;
	proc = kmalloc(sizeof(*proc));
	if (proc == NULL) {
		return NULL;
	}
	proc->p_name = kstrdup(name);
	if (proc->p_name == NULL) {
		kfree(proc);
		return NULL;
	}

	threadarray_init(&proc->p_threads);
	spinlock_init(&proc->p_lock);

	/* VM fields */
	proc->p_addrspace = NULL;

	/* VFS fields */
	proc->p_cwd = NULL;
	proc->p_filetable = NULL;

	//init for struct

	proc-> p_sem = sem_create("WaitExitSem",0);
    	proc->p_parent = NULL;
    	proc->p_children = array_create();
    	proc->p_exitval = 0;
    	proc->p_finished = false;
    
    	newpid = proc_addnew_ptable(proc);
    	proc->p_pid = newpid;
        //DEBUG(DB_EXEC, "User process with pid %u created.\n",newpid);

	
	return proc;
}

/*
 * Destroy a proc structure.
 *
 * Note: nothing currently calls this. Your wait/exit code will
 * probably want to do so.
 */
void
proc_destroy(struct proc *proc)
{
	/*
	 * You probably want to destroy and null out much of the
	 * process (particularly the address space) at exit time if
	 * your wait/exit design calls for the process structure to
	 * hang around beyond process exit. Some wait/exit designs
	 * do, some don't.
	 */

	KASSERT(proc != NULL);
	KASSERT(proc != kproc);
	int i,procpid;
	int* pidkid;
	struct proc* parent = proc->p_parent;
	struct proc* kid = NULL;
	/*
	 * We don't take p_lock in here because we must have the only
	 * reference to this structure. (Otherwise it would be
	 * incorrect to destroy it.)
	 */

	/* VFS fields */
	if (proc->p_cwd) {
		VOP_DECREF(proc->p_cwd);
		proc->p_cwd = NULL;
	}
	if (proc->p_filetable) {
		filetable_destroy(proc->p_filetable);
		proc->p_filetable = NULL;
	}

	/* VM fields */
	if (proc->p_addrspace) {
		/*
		 * If p is the current process, remove it safely from
		 * p_addrspace before destroying it. This makes sure
		 * we don't try to activate the address space while
		 * it's being destroyed.
		 *
		 * Also explicitly deactivate, because setting the
		 * address space to NULL won't necessarily do that.
		 *
		 * (When the address space is NULL, it means the
		 * process is kernel-only; in that case it is normally
		 * ok if the MMU and MMU- related data structures
		 * still refer to the address space of the last
		 * process that had one. Then you save work if that
		 * process is the next one to run, which isn't
		 * uncommon. However, here we're going to destroy the
		 * address space, so we need to make sure that nothing
		 * in the VM system still refers to it.)
		 *
		 * The call to as_deactivate() must come after we
		 * clear the address space, or a timer interrupt might
		 * reactivate the old address space again behind our
		 * back.
		 *
		 * If p is not the current process, still remove it
		 * from p_addrspace before destroying it as a
		 * precaution. Note that if p is not the current
		 * process, in order to be here p must either have
		 * never run (e.g. cleaning up after fork failed) or
		 * have finished running and exited. It is quite
		 * incorrect to destroy the proc structure of some
		 * random other process while it's still running...
		 */
		struct addrspace *as;
		
		if (proc == curproc) {
			as = proc_setas(NULL);
			as_deactivate();
		}
		else {
			as = proc->p_addrspace;
			proc->p_addrspace = NULL;
		}
		as_destroy(as);
	}

	threadarray_cleanup(&proc->p_threads);

	kfree(proc->p_name);
	
	spinlock_acquire(&proc->p_lock);
	//if children: 
	if((int)array_num(proc->p_children) > 0)
	{
		for( i=0; i< (int)array_num(proc->p_children); i++)
		{
        	
			pidkid = array_get(proc->p_children, (unsigned)i);
		
			kid = proc_get_process((int)pidkid);
			kid->p_parent = NULL;
		}
	}
	spinlock_release(&proc->p_lock);
	
	//if parent exists, remove me from parents children array
	if(parent != NULL)
	{
		spinlock_acquire(&parent->p_lock);
	        //DEBUG(DB_EXEC, "My siblings: %d \n", array_num(parent->p_children));
    		for (i = 0; i < (int) array_num(parent->p_children); i++)
	    	{
	                pidkid = array_get(parent->p_children, i);
    			// from all my parent's children, find me
			if( (int)pidkid == proc->p_pid)
	        	{//remove me from the list
	        		array_remove(parent->p_children, i);
	        		break;
	    		}
	    	}
	    	spinlock_release(&parent->p_lock);
		
	}
	procpid= proc->p_pid; // no significance except debugging
	spinlock_cleanup(&proc->p_lock);
	
	if(parent == NULL)
	{ //destroying everything if parent is also null.
		sem_destroy(proc->p_sem);
		lock_acquire(p_table_lock);
	       	procarray_set(&p_table, proc->p_pid, NULL);
	       	lock_release(p_table_lock);
		
		kfree(proc->p_name);
		kfree(proc);
	}

	lock_acquire(p_table_lock);
	
    
	KASSERT(n_pr > 0);
	
    	n_pr--; //total processes in the system
	//DEBUG(DB_EXEC, "-n %u with process %u gone\n",n_pr, procpid);
    	if (n_pr == 1)//if only kernel process left
    	{
        	V(p_kern_sem);
    	}
    	lock_release(p_table_lock);
}


/*
 * Create the process structure for the kernel.
 */
void
proc_bootstrap(void)
{
	//global initialisations
	procarray_init(&p_table);
	p_table_lock = lock_create("ProcessTableLock");
	
	if(p_table_lock == NULL)
	    panic("p_table_lock creation failed");
        p_kern_sem = sem_create("WaitingKernelSem",0);
        if(p_kern_sem == NULL)
        {
            panic("p_kern_sem creation failed\n");
        }

        n_pr =0;

	kproc = proc_create("kernel");
	if(kproc == NULL) {
	    panic("proc_create for kproc failed\n");
	}
	kprintf("Prcoess Bootstrap successful");
}

/*
 * Create a fresh proc for use by runprogram.
 *
 * It will have no address space and will inherit the current
 * process's (that is, the kernel menu's) current directory.
 *
 * It will be given no filetable. The filetable will be initialized in
 * runprogram().
 */
struct proc *
proc_create_runprogram(const char *name)
{
	struct proc *newproc;

	newproc = proc_create(name);
	if (newproc == NULL) {
		return NULL;
	}

	/* VM fields */

	newproc->p_addrspace = NULL;

	/* VFS fields */

	/*
	 * Lock the current process to copy its current directory.
	 * (We don't need to lock the new process, though, as we have
	 * the only reference to it.)
	 */
	spinlock_acquire(&curproc->p_lock);
	if (curproc->p_cwd != NULL) {
		VOP_INCREF(curproc->p_cwd);
		newproc->p_cwd = curproc->p_cwd;
	}
	spinlock_release(&curproc->p_lock);

	return newproc;
}

/*
 * Clone the current process.
 *
 * The new thread is given a copy of the caller's file handles if RET
 * is not null. (If RET is null, what we're creating is a kernel-only
 * thread and it doesn't need an address space or file handles.)
 * However, the new thread always inherits its current working
 * directory from the caller. The new thread is given no address space
 * (the caller decides that).
 */
int
proc_fork(struct proc **ret)
{
	struct proc *proc;
	struct filetable *tbl;
	int result;

	proc = proc_create(curproc->p_name);
	if (proc == NULL) {
		return ENOMEM;
	}

	/* VM fields */
	/* do not clone address space -- let caller decide on that */

	/* VFS fields */
	tbl = curproc->p_filetable;
	if (tbl != NULL) {
		result = filetable_copy(tbl, &proc->p_filetable);
		if (result) {
			as_destroy(proc->p_addrspace);
			proc->p_addrspace = NULL;
			proc_destroy(proc);
			return result;
		}
	}

	spinlock_acquire(&curproc->p_lock);
	/* we don't need to lock proc->p_lock as we have the only reference */
	if (curproc->p_cwd != NULL) {
		VOP_INCREF(curproc->p_cwd);
		proc->p_cwd = curproc->p_cwd;
	}
	
	spinlock_release(&curproc->p_lock);
	
	*ret = proc;
	return 0;
}

/*
 * Add a thread to a process. Either the thread or the process might
 * or might not be current.
 *
 * Turn off interrupts on the local cpu while changing t_proc, in
 * case it's current, to protect against the as_activate call in
 * the timer interrupt context switch, and any other implicit uses
 * of "curproc".
 */
int
proc_addthread(struct proc *proc, struct thread *t)
{
	int result;
	int spl;

	KASSERT(t->t_proc == NULL);

	spinlock_acquire(&proc->p_lock);
	result = threadarray_add(&proc->p_threads, t, NULL);
	spinlock_release(&proc->p_lock);
	if (result) {
		return result;
	}
	spl = splhigh();
	t->t_proc = proc;
	splx(spl);
	return 0;
}

/*
 * Remove a thread from its process. Either the thread or the process
 * might or might not be current.
 *
 * Turn off interrupts on the local cpu while changing t_proc, in
 * case it's current, to protect against the as_activate call in
 * the timer interrupt context switch, and any other implicit uses
 * of "curproc".
 */
void
proc_remthread(struct thread *t)
{
	struct proc *proc;
	unsigned i, num;
	int spl;

	proc = t->t_proc;
	if(proc != NULL)
	{
	spinlock_acquire(&proc->p_lock);
	/* ugh: find the thread in the array */
	num = threadarray_num(&proc->p_threads);
	for (i=0; i<num; i++) {
		if (threadarray_get(&proc->p_threads, i) == t) {
			threadarray_remove(&proc->p_threads, i);
			spinlock_release(&proc->p_lock);
			spl = splhigh();
			t->t_proc = NULL;
			splx(spl);
			return;
		}
	}
	/* Did not find it. */
	spinlock_release(&proc->p_lock);
	panic("Thread (%p) has escaped from its process (%p)\n", t, proc);
	}
}

/*
 * Fetch the address space of (the current) process.
 *
 * Caution: address spaces aren't refcounted. If you implement
 * multithreaded processes, make sure to set up a refcount scheme or
 * some other method to make this safe. Otherwise the returned address
 * space might disappear under you.
 */
struct addrspace *
proc_getas(void)
{
	struct addrspace *as;
	struct proc *proc = curproc;

	if (proc == NULL) {
		return NULL;
	}

	spinlock_acquire(&proc->p_lock);
	as = proc->p_addrspace;
	spinlock_release(&proc->p_lock);
	return as;
}

/*
 * Change the address space of (the current) process. Return the old
 * one for later restoration or disposal.
 */
struct addrspace *
proc_setas(struct addrspace *newas)
{
	struct addrspace *oldas;
	struct proc *proc = curproc;

	KASSERT(proc != NULL);

	spinlock_acquire(&proc->p_lock);
	oldas = proc->p_addrspace;
	proc->p_addrspace = newas;
	spinlock_release(&proc->p_lock);
	return oldas;
}



struct proc
*proc_get_process(int pid)
{
	struct proc* p = NULL;
	lock_acquire(p_table_lock);
	for (int i=0; i<(int)procarray_num(&p_table); i++)
	{
		p = procarray_get(&p_table,(unsigned)i);
		if (p->p_pid == pid)
			break;
	}
	lock_release(p_table_lock);
	return p;
}



int proc_addnew_ptable(struct  proc *proc)
{
	//(void)proc;
	int in;
	int i;
	unsigned ret=0;
	
	DEBUG(DB_EXEC, "+npr %u, array num %u\n", n_pr, procarray_num(&p_table));
	if(n_pr==0)
	{
		//lock_acquire(p_table_lock);
		procarray_add(&p_table, proc, &ret);
		in = (int) ret;
		n_pr++;
		//lock_release(p_table_lock);
		return in;
	}

	else
	{
		//user process
		lock_acquire(p_table_lock);
		for (i=0; i< (int)procarray_num(&p_table); i++)
		{
			struct proc* p=procarray_get(&p_table, i);
			if(p == NULL)
			{
				in=i;
				procarray_set(&p_table, in, proc);
				n_pr++;
				lock_release(p_table_lock);
				return in;
			}
		}
		//no space found
		if((int)procarray_num(&p_table) == n_pr) // increase size if table is full
		{
			int res= procarray_setsize(&p_table, n_pr+1);
			KASSERT(res == 0);
		}
		in = n_pr; //put process at next index
		n_pr++;
		procarray_set(&p_table, in, proc);
		lock_release(p_table_lock);
	}	

	return in;	
}

