/*
 *  linux/kernel/exit.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 */

#define DEBUG_PROC_TREE

#include <linux/wait.h>
#include <linux/errno.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/resource.h>
#include <linux/mm.h>
#include <linux/tty.h>
#include <linux/malloc.h>

#include <asm/segment.h>
extern void sem_exit (void);

int getrusage(struct task_struct *, int, struct rusage *);

static int generate(unsigned long sig, struct task_struct * p)
{
	unsigned long mask = 1 << (sig-1);
	struct sigaction * sa = sig + p->sigaction - 1;

	/* always generate signals for traced processes ??? */
	if (p->flags & PF_PTRACED) {
		p->signal |= mask;
		return 1;
	}
	/* don't bother with ignored signals (but SIGCHLD is special) */
	if (sa->sa_handler == SIG_IGN && sig != SIGCHLD)
		return 0;
	/* some signals are ignored by default.. (but SIGCONT already did its deed) */
	if ((sa->sa_handler == SIG_DFL) &&
	    (sig == SIGCONT || sig == SIGCHLD || sig == SIGWINCH))
		return 0;
	p->signal |= mask;
	return 1;
}

int send_sig(unsigned long sig,struct task_struct * p,int priv)
{
	if (!p || sig > 32)
		return -EINVAL;
	if (!priv && ((sig != SIGCONT) || (current->session != p->session)) &&
	    (current->euid != p->euid) && (current->uid != p->uid) && !suser())
		return -EPERM;
	if (!sig)
		return 0;
	/*
	 * Forget it if the process is already zombie'd.
	 */
	if (p->state == TASK_ZOMBIE)
		return 0;
	if ((sig == SIGKILL) || (sig == SIGCONT)) {
		if (p->state == TASK_STOPPED)
			p->state = TASK_RUNNING;
		p->exit_code = 0;
		p->signal &= ~( (1<<(SIGSTOP-1)) | (1<<(SIGTSTP-1)) |
				(1<<(SIGTTIN-1)) | (1<<(SIGTTOU-1)) );
	}
	/* Depends on order SIGSTOP, SIGTSTP, SIGTTIN, SIGTTOU */
	if ((sig >= SIGSTOP) && (sig <= SIGTTOU)) 
		p->signal &= ~(1<<(SIGCONT-1));
	/* Actually generate the signal */
	generate(sig,p);
	return 0;
}

void notify_parent(struct task_struct * tsk)
{
	if (tsk->p_pptr == task[1])
		tsk->exit_signal = SIGCHLD;
	send_sig(tsk->exit_signal, tsk->p_pptr, 1);
	wake_up_interruptible(&tsk->p_pptr->wait_chldexit);
}

void release(struct task_struct * p)
{
	int i;

	if (!p)
		return;
	if (p == current) {
		printk("task releasing itself\n");
		return;
	}
	for (i=1 ; i<NR_TASKS ; i++)
		if (task[i] == p) {
			task[i] = NULL;
			REMOVE_LINKS(p);
			if (STACK_MAGIC != *(unsigned long *)p->kernel_stack_page)
				printk(KERN_ALERT "release: %s kernel stack corruption. Aiee\n", p->comm);
			free_page(p->kernel_stack_page);
			free_page((long) p);
			return;
		}
	panic("trying to release non-existent task");
}

#ifdef DEBUG_PROC_TREE
/*
 * Check to see if a task_struct pointer is present in the task[] array
 * Return 0 if found, and 1 if not found.
 */
int bad_task_ptr(struct task_struct *p)
{
	int 	i;

	if (!p)
		return 0;
	for (i=0 ; i<NR_TASKS ; i++)
		if (task[i] == p)
			return 0;
	return 1;
}
	
/*
 * This routine scans the pid tree and makes sure the rep invariant still
 * holds.  Used for debugging only, since it's very slow....
 *
 * It looks a lot scarier than it really is.... we're doing nothing more
 * than verifying the doubly-linked list found in p_ysptr and p_osptr, 
 * and checking it corresponds with the process tree defined by p_cptr and 
 * p_pptr;
 */
void audit_ptree(void)
{
	int	i;

	for (i=1 ; i<NR_TASKS ; i++) {
		if (!task[i])
			continue;
		if (bad_task_ptr(task[i]->p_pptr))
			printk("Warning, pid %d's parent link is bad\n",
				task[i]->pid);
		if (bad_task_ptr(task[i]->p_cptr))
			printk("Warning, pid %d's child link is bad\n",
				task[i]->pid);
		if (bad_task_ptr(task[i]->p_ysptr))
			printk("Warning, pid %d's ys link is bad\n",
				task[i]->pid);
		if (bad_task_ptr(task[i]->p_osptr))
			printk("Warning, pid %d's os link is bad\n",
				task[i]->pid);
		if (task[i]->p_pptr == task[i])
			printk("Warning, pid %d parent link points to self\n",
				task[i]->pid);
		if (task[i]->p_cptr == task[i])
			printk("Warning, pid %d child link points to self\n",
				task[i]->pid);
		if (task[i]->p_ysptr == task[i])
			printk("Warning, pid %d ys link points to self\n",
				task[i]->pid);
		if (task[i]->p_osptr == task[i])
			printk("Warning, pid %d os link points to self\n",
				task[i]->pid);
		if (task[i]->p_osptr) {
			if (task[i]->p_pptr != task[i]->p_osptr->p_pptr)
				printk(
			"Warning, pid %d older sibling %d parent is %d\n",
				task[i]->pid, task[i]->p_osptr->pid,
				task[i]->p_osptr->p_pptr->pid);
			if (task[i]->p_osptr->p_ysptr != task[i])
				printk(
		"Warning, pid %d older sibling %d has mismatched ys link\n",
				task[i]->pid, task[i]->p_osptr->pid);
		}
		if (task[i]->p_ysptr) {
			if (task[i]->p_pptr != task[i]->p_ysptr->p_pptr)
				printk(
			"Warning, pid %d younger sibling %d parent is %d\n",
				task[i]->pid, task[i]->p_osptr->pid,
				task[i]->p_osptr->p_pptr->pid);
			if (task[i]->p_ysptr->p_osptr != task[i])
				printk(
		"Warning, pid %d younger sibling %d has mismatched os link\n",
				task[i]->pid, task[i]->p_ysptr->pid);
		}
		if (task[i]->p_cptr) {
			if (task[i]->p_cptr->p_pptr != task[i])
				printk(
			"Warning, pid %d youngest child %d has mismatched parent link\n",
				task[i]->pid, task[i]->p_cptr->pid);
			if (task[i]->p_cptr->p_ysptr)
				printk(
			"Warning, pid %d youngest child %d has non-NULL ys link\n",
				task[i]->pid, task[i]->p_cptr->pid);
		}
	}
}
#endif /* DEBUG_PROC_TREE */

/*
 * This checks not only the pgrp, but falls back on the pid if no
 * satisfactory pgrp is found. I dunno - gdb doesn't work correctly
 * without this...
 */
int session_of_pgrp(int pgrp)
{
	struct task_struct *p;
	int fallback;

	fallback = -1;
	for_each_task(p) {
 		if (p->session <= 0)
 			continue;
		if (p->pgrp == pgrp)
			return p->session;
		if (p->pid == pgrp)
			fallback = p->session;
	}
	return fallback;
}

/*
 * kill_pg() sends a signal to a process group: this is what the tty
 * control characters do (^C, ^Z etc)
 */
int kill_pg(int pgrp, int sig, int priv)
{
	struct task_struct *p;
	int err,retval = -ESRCH;
	int found = 0;

	if (sig<0 || sig>32 || pgrp<=0)
		return -EINVAL;
	for_each_task(p) {
		if (p->pgrp == pgrp) {
			if ((err = send_sig(sig,p,priv)) != 0)
				retval = err;
			else
				found++;
		}
	}
	return(found ? 0 : retval);
}

/*
 * kill_sl() sends a signal to the session leader: this is used
 * to send SIGHUP to the controlling process of a terminal when
 * the connection is lost.
 */
int kill_sl(int sess, int sig, int priv)
{
	struct task_struct *p;
	int err,retval = -ESRCH;
	int found = 0;

	if (sig<0 || sig>32 || sess<=0)
		return -EINVAL;
	for_each_task(p) {
		if (p->session == sess && p->leader) {
			if ((err = send_sig(sig,p,priv)) != 0)
				retval = err;
			else
				found++;
		}
	}
	return(found ? 0 : retval);
}

int kill_proc(int pid, int sig, int priv)
{
 	struct task_struct *p;

	if (sig<0 || sig>32)
		return -EINVAL;
	for_each_task(p) {
		if (p && p->pid == pid)
			return send_sig(sig,p,priv);
	}
	return(-ESRCH);
}

/*
 * POSIX specifies that kill(-1,sig) is unspecified, but what we have
 * is probably wrong.  Should make it like BSD or SYSV.
 */
asmlinkage int sys_kill(int pid,int sig)
{
	int err, retval = 0, count = 0;

	if (!pid)
		return(kill_pg(current->pgrp,sig,0));
	if (pid == -1) {
		struct task_struct * p;
		for_each_task(p) {
			if (p->pid > 1 && p != current) {
				++count;
				if ((err = send_sig(sig,p,0)) != -EPERM)
					retval = err;
			}
		}
		return(count ? retval : -ESRCH);
	}
	if (pid < 0) 
		return(kill_pg(-pid,sig,0));
	/* Normal kill */
	return(kill_proc(pid,sig,0));
}

/*
 * Determine if a process group is "orphaned", according to the POSIX
 * definition in 2.2.2.52.  Orphaned process groups are not to be affected
 * by terminal-generated stop signals.  Newly orphaned process groups are 
 * to receive a SIGHUP and a SIGCONT.
 * 
 * "I ask you, have you ever known what it is to be an orphan?"
 */
int is_orphaned_pgrp(int pgrp)
{
	struct task_struct *p;

	for_each_task(p) {
		if ((p->pgrp != pgrp) || 
		    (p->state == TASK_ZOMBIE) ||
		    (p->p_pptr->pid == 1))
			continue;
		if ((p->p_pptr->pgrp != pgrp) &&
		    (p->p_pptr->session == p->session))
			return 0;
	}
	return(1);	/* (sighing) "Often!" */
}

static int has_stopped_jobs(int pgrp)
{
	struct task_struct * p;

	for_each_task(p) {
		if (p->pgrp != pgrp)
			continue;
		if (p->state == TASK_STOPPED)
			return(1);
	}
	return(0);
}

static void forget_original_parent(struct task_struct * father)
{
	struct task_struct * p;

	for_each_task(p) {
		if (p->p_opptr == father)
			if (task[1])
				p->p_opptr = task[1];
			else
				p->p_opptr = task[0];
	}
}

static void exit_mm(void)
{
	struct vm_area_struct * mpnt;

	mpnt = current->mm->mmap;
	current->mm->mmap = NULL;
	while (mpnt) {
		struct vm_area_struct * next = mpnt->vm_next;
		if (mpnt->vm_ops && mpnt->vm_ops->close)
			mpnt->vm_ops->close(mpnt);
		remove_shared_vm_struct(mpnt);
		if (mpnt->vm_inode)
			iput(mpnt->vm_inode);
		kfree(mpnt);
		mpnt = next;
	}

	free_page_tables(current);
}

static void exit_files(void)
{
	int i;

	for (i=0 ; i<NR_OPEN ; i++)
		if (current->files->fd[i])
			sys_close(i);
}

static void exit_fs(void)
{
	iput(current->fs->pwd);
	current->fs->pwd = NULL;
	iput(current->fs->root);
	current->fs->root = NULL;
}

NORET_TYPE void do_exit(long code)
{
	struct task_struct *p;

	if (intr_count) {
		printk("Aiee, killing interrupt handler\n");
		intr_count = 0;
	}
fake_volatile:
	if (current->semundo)
		sem_exit();
	exit_mm();
	exit_files();
	exit_fs();
	exit_thread();
	forget_original_parent(current);
	/* 
	 * Check to see if any process groups have become orphaned
	 * as a result of our exiting, and if they have any stopped
	 * jobs, send them a SIGUP and then a SIGCONT.  (POSIX 3.2.2.2)
	 *
	 * Case i: Our father is in a different pgrp than we are
	 * and we were the only connection outside, so our pgrp
	 * is about to become orphaned.
 	 */
	if ((current->p_pptr->pgrp != current->pgrp) &&
	    (current->p_pptr->session == current->session) &&
	    is_orphaned_pgrp(current->pgrp) &&
	    has_stopped_jobs(current->pgrp)) {
		kill_pg(current->pgrp,SIGHUP,1);
		kill_pg(current->pgrp,SIGCONT,1);
	}
	/* Let father know we died */
	notify_parent(current);
	
	/*
	 * This loop does two things:
	 * 
  	 * A.  Make init inherit all the child processes
	 * B.  Check to see if any process groups have become orphaned
	 *	as a result of our exiting, and if they have any stopped
	 *	jobs, send them a SIGHUP and then a SIGCONT.  (POSIX 3.2.2.2)
	 */
	while ((p = current->p_cptr) != NULL) {
		current->p_cptr = p->p_osptr;
		p->p_ysptr = NULL;
		p->flags &= ~(PF_PTRACED|PF_TRACESYS);
		if (task[1] && task[1] != current)
			p->p_pptr = task[1];
		else
			p->p_pptr = task[0];
		p->p_osptr = p->p_pptr->p_cptr;
		p->p_osptr->p_ysptr = p;
		p->p_pptr->p_cptr = p;
		if (p->state == TASK_ZOMBIE)
			notify_parent(p);
		/*
		 * process group orphan check
		 * Case ii: Our child is in a different pgrp 
		 * than we are, and it was the only connection
		 * outside, so the child pgrp is now orphaned.
		 */
		if ((p->pgrp != current->pgrp) &&
		    (p->session == current->session) &&
		    is_orphaned_pgrp(p->pgrp) &&
		    has_stopped_jobs(p->pgrp)) {
			kill_pg(p->pgrp,SIGHUP,1);
			kill_pg(p->pgrp,SIGCONT,1);
		}
	}
	if (current->leader)
		disassociate_ctty(1);
	if (last_task_used_math == current)
		last_task_used_math = NULL;
	current->state = TASK_ZOMBIE;
	current->exit_code = code;
	current->mm->rss = 0;
#ifdef DEBUG_PROC_TREE
	audit_ptree();
#endif
	if (current->exec_domain && current->exec_domain->use_count)
		(*current->exec_domain->use_count)--;
	if (current->binfmt && current->binfmt->use_count)
		(*current->binfmt->use_count)--;
	schedule();
/*
 * In order to get rid of the "volatile function does return" message
 * I did this little loop that confuses gcc to think do_exit really
 * is volatile. In fact it's schedule() that is volatile in some
 * circumstances: when current->state = ZOMBIE, schedule() never
 * returns.
 *
 * In fact the natural way to do all this is to have the label and the
 * goto right after each other, but I put the fake_volatile label at
 * the start of the function just in case something /really/ bad
 * happens, and the schedule returns. This way we can try again. I'm
 * not paranoid: it's just that everybody is out to get me.
 */
	goto fake_volatile;
}

asmlinkage int sys_exit(int error_code)
{
	do_exit((error_code&0xff)<<8);
}

asmlinkage int sys_wait4(pid_t pid,unsigned long * stat_addr, int options, struct rusage * ru)
{
	int flag, retval;
	struct wait_queue wait = { current, NULL };
	struct task_struct *p;

	if (stat_addr) {
		flag = verify_area(VERIFY_WRITE, stat_addr, 4);
		if (flag)
			return flag;
	}
	add_wait_queue(&current->wait_chldexit,&wait);
repeat:
	flag=0;
 	for (p = current->p_cptr ; p ; p = p->p_osptr) {
		if (pid>0) {
			if (p->pid != pid)
				continue;
		} else if (!pid) {
			if (p->pgrp != current->pgrp)
				continue;
		} else if (pid != -1) {
			if (p->pgrp != -pid)
				continue;
		}
		/* wait for cloned processes iff the __WCLONE flag is set */
		if ((p->exit_signal != SIGCHLD) ^ ((options & __WCLONE) != 0))
			continue;
		flag = 1;
		switch (p->state) {
			case TASK_STOPPED:
				if (!p->exit_code)
					continue;
				if (!(options & WUNTRACED) && !(p->flags & PF_PTRACED))
					continue;
				if (stat_addr)
					put_fs_long((p->exit_code << 8) | 0x7f,
						stat_addr);
				p->exit_code = 0;
				if (ru != NULL)
					getrusage(p, RUSAGE_BOTH, ru);
				retval = p->pid;
				goto end_wait4;
			case TASK_ZOMBIE:
				current->cutime += p->utime + p->cutime;
				current->cstime += p->stime + p->cstime;
				current->mm->cmin_flt += p->mm->min_flt + p->mm->cmin_flt;
				current->mm->cmaj_flt += p->mm->maj_flt + p->mm->cmaj_flt;
				if (ru != NULL)
					getrusage(p, RUSAGE_BOTH, ru);
				flag = p->pid;
				if (stat_addr)
					put_fs_long(p->exit_code, stat_addr);
				if (p->p_opptr != p->p_pptr) {
					REMOVE_LINKS(p);
					p->p_pptr = p->p_opptr;
					SET_LINKS(p);
					notify_parent(p);
				} else
					release(p);
#ifdef DEBUG_PROC_TREE
				audit_ptree();
#endif
				retval = flag;
				goto end_wait4;
			default:
				continue;
		}
	}
	if (flag) {
		retval = 0;
		if (options & WNOHANG)
			goto end_wait4;
		current->state=TASK_INTERRUPTIBLE;
		schedule();
		current->signal &= ~(1<<(SIGCHLD-1));
		retval = -ERESTARTSYS;
		if (current->signal & ~current->blocked)
			goto end_wait4;
		goto repeat;
	}
	retval = -ECHILD;
end_wait4:
	remove_wait_queue(&current->wait_chldexit,&wait);
	return retval;
}

/*
 * sys_waitpid() remains for compatibility. waitpid() should be
 * implemented by calling sys_wait4() from libc.a.
 */
asmlinkage int sys_waitpid(pid_t pid,unsigned long * stat_addr, int options)
{
	return sys_wait4(pid, stat_addr, options, NULL);
}
