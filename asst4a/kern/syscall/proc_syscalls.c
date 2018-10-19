#include <types.h>
#include <kern/errno.h>
#include <kern/syscall.h>
#include <kern/fcntl.h>
#include <kern/limits.h>
#include <kern/seek.h>
#include <kern/stat.h>
#include <kern/wait.h>
#include <lib.h>
#include <uio.h>
#include <proc.h>
#include <vfs.h>
#include <vnode.h>
#include <openfile.h>
#include <filetable.h>
#include <current.h>
#include <synch.h>
#include <copyinout.h>
#include <mips/trapframe.h>
#include <addrspace.h>
#include <thread.h>
#include <syscall.h>


int sys_getpid(int *retval)
{
	KASSERT(curproc != NULL);
	*retval = curproc->p_pid;
	return 0;
}


void sys__exit(int exitcode)
{
    	struct addrspace* as;
	struct proc* p = curproc;

    	//DEBUG(DB_EXEC, "_exit(): process %u\n",p->p_pid);
    
	KASSERT(p->p_addrspace != NULL);

    
    	as_deactivate();
    	as = proc_setas(NULL);
    	as_destroy(as);

    	proc_remthread(curthread);

    	spinlock_acquire(&p->p_lock);

    	p->p_exitval = _MKWAIT_EXIT(exitcode);
    	p->p_finished = true;
    	spinlock_release(&p->p_lock);

    	V( p->p_sem );
	//DEBUG(DB_EXEC, "_exit(): V p_sem %u\n",p->p_sem->sem_count);


    	proc_destroy(p);
    	thread_exit();
    	panic("_exit: thread could not exit()\n");
}


int sys_waitpid(pid_t pid,
            userptr_t status,
            int options,
	    int* retval
            )
{
   
    int result;

    struct proc* wp = curproc; // waiting  process
    struct proc* child = proc_get_process(pid);

	if(status == NULL)
		return EFAULT;
	if (options != 0)
        	return EINVAL;
	if(child == NULL) 
        {
            DEBUG(DB_EXEC,"waitpid: invalid child pid\n");
            return ECHILD;
        }
	if (wp != kproc && child->p_parent != wp)
    	{
        	//given pid is not of this process's child and current waiting process is not kernel process
        	DEBUG(DB_EXEC,"waitpid: ECHILD\n");
        	return(ECHILD);
    	}
    
	//waiting on child:
        while(!child->p_finished);
	P(child->p_sem); //for wait-exit logic
   	//DEBUG(DB_EXEC, "P sem %u\n",child->p_sem->sem_count); 

    	result = copyout(&child->p_exitval,status,sizeof(int));
    	if(result)
    	{
        	return(result);
    	}
    
	*retval = (int)pid;
    	return 0;
}


int sys_fork(struct trapframe* p_tf, int *retval)
{
    //DEBUG(DB_EXEC,"fork: entering\n");
    
	KASSERT(curproc != NULL);
    	KASSERT(sizeof(struct trapframe)==(37*4));
   
    	char* child_name = kmalloc(sizeof(char)* NAME_MAX);
    	strcpy(child_name, curproc->p_name);
    	strcat(child_name, "_c");

	    //Fork process:
    	struct proc* child_proc = NULL;
    	proc_fork(&child_proc);
    	if (child_proc == NULL)
    	{
        	return ENOMEM;
    	}
	strcpy(child_proc->p_name, child_name);

    	KASSERT(child_proc->p_pid > 0);
    
    	//strcpy(child_proc->p_name, strcat(curproc->p_name,"_c"));

    	struct addrspace* child_as = kmalloc(sizeof(struct addrspace));
    	if (child_as == NULL)
    	{
        	kfree(child_name);
	        proc_destroy(child_proc);
        	return ENOMEM;
    	}
    	int result = as_copy(curproc->p_addrspace, &child_as);
    	if (result)
    	{
        	kfree(child_name);
	        as_destroy(child_as);
	        proc_destroy(child_proc);
        	return result;
    	}
 	//DEBUG(DB_EXEC,"fork: addrspace copied to child as\n");
    	struct trapframe* child_tf = kmalloc(sizeof(struct trapframe));
    
	if (child_tf == NULL)
    	{
        	kfree(child_name);
	        as_destroy(child_as);
	        proc_destroy(child_proc);
        	return ENOMEM;
    	}

    	child_proc->p_addrspace = child_as;
	memcpy(child_tf, p_tf, sizeof(struct trapframe));
    	// DEBUG(DB_EXEC,"fork: tf copied\n");
    	filetable_copy(curproc->p_filetable,&child_proc->p_filetable);
    	child_proc->p_parent = curproc;
	// DEBUG(DB_EXEC,"fork: filetable copied\n");
    
    	result = (int)array_add(curproc->p_children, (void*)child_proc->p_pid, NULL);
    	if (result)
    	{
        	DEBUG(DB_EXEC,"fork: couldnt add to array\n");
       	 	return result;
    	}
        DEBUG(DB_EXEC,"fork: p %u has child %u\n",curproc->p_pid,child_proc->p_pid);
    	result = thread_fork(child_name, child_proc, &enter_forked_process, child_tf, 1);
    
    	if (result)
    	{
        	kfree(child_name);
	        kfree(child_tf);
	        as_destroy(child_as);
	        proc_destroy(child_proc);
	        return ENOMEM;
    	}
    
    	*retval = child_proc->p_pid;
    
    	// DEBUG(DB_EXEC,"fork: successful\n");
    	return 0;
}

