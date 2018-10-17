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
    return curproc->p_pid;
}


void sys__exit(int exitcode)
{
    struct addrspace* as;
    struct proc* p = curproc;
    //DEBUG(DB_EXEC, "sys_exit(): process %u exiting with code %d\n",p->p_pid,exitcode);
    KASSERT(p->p_addrspace != NULL);
    
    as_deactivate();
    as = proc_setas(NULL);
    as_destroy(as);
    
    proc_remthread(curthread);

    spinlock_acquire(&p->p_lock);
    p->p_exitval = _MKWAIT_EXIT(exitcode);
    p->p_finished = true;

    V( p->p_sem );
    //cv_broadcast(p->p_waitpid_cv, p->p_waitpid_lk);
    
    spinlock_release(&p->p_lock);
    
    proc_destroy(p);
    thread_exit();
    panic("_exit: thread could not exit()\n");
}


int sys_waitpid(pid_t pid,
            userptr_t status,
            int options
            )
{
    int exitval;
    int result;

    if (options != 0)
    {
        return(EINVAL);
    }

    struct proc* wp = curproc; // waiting  process
    struct proc* child = proc_get_process(pid);

    if(child == NULL) 
        {
            kprintf("invalid child pid");
            return(ECHILD);
        }


    if (wp != kproc && child->p_parent != wp)
    {
        //given pid is not of this process's child and current waiting process is not kernel process
        kprintf("waitpid(): ECHILD\n");
        return(ECHILD);
    }


    //waiting on child:
    kprintf("Parent waiting on child. waitpid");

    spinlock_acquire(&child->p_lock);
    P(wp->p_sem);
    KASSERT(wp->p_finished);
    exitval = child->p_exitval;
    spinlock_release(&child->p_lock);

    result = copyout(&exitval,status,sizeof(int));
    if(result)
    {
        return(result);
    }

    return 0;
}


int sys_fork(struct trapframe* p_tf, int *retval)
{
    kprintf("fork: entering\n");
    KASSERT(curproc != NULL);
    KASSERT(sizeof(struct trapframe)==(37*4));
    
    //Fork process:
    struct proc* child_proc = NULL;
    proc_fork(&child_proc);

    if (child_proc == NULL)
    {
        return ENOMEM;
    }

    KASSERT(child_proc->p_pid > 0);
    

    //strcpy(child_proc->p_name, strcat(curproc->p_name,"_c"));

    char* child_name = kmalloc(sizeof(char)* NAME_MAX);
    strcpy(child_name, curproc->p_name);
    strcat(child_name, "_c");

    struct addrspace* child_as = kmalloc(sizeof(struct addrspace));
    if (child_as == NULL)
    {
        
        proc_destroy(child_proc);
        return ENOMEM;
    }

    
    int result = as_copy(curproc->p_addrspace, &child_as);
    if (result)
    {
        
        as_destroy(child_as);
        proc_destroy(child_proc);
        return result;
    }
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
    
    filetable_copy(curproc->p_filetable,&child_proc->p_filetable);
    child_proc->p_parent = curproc;
    
    result = procarray_add(curproc->p_children, child_proc, NULL);
    if (result)
    {
        kprintf("couldnt add to array");
        return result;
    }
    
    result = thread_fork(child_name, child_proc, &enter_forked_process, child_tf, 1);
    
    if (result)
    {
        
        kfree(child_tf);
        as_destroy(child_as);
        proc_destroy(child_proc);
        return ENOMEM;
    }
    
    *retval = child_proc->p_pid;
    
    kprintf("fork successful");
	kprintf("*ret val = child pid %d\n",*retval);
    return 0;
}

