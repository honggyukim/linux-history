/*
 * linux/ipc/shm.c
 * Copyright (C) 1992, 1993 Krishna Balasubramanian
 *         Many improvements/fixes by Bruno Haible.
 * Replaced `struct shm_desc' by `struct vm_area_struct', July 1994.
 */

#include <linux/errno.h>
#include <asm/segment.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/ipc.h>
#include <linux/shm.h>
#include <linux/stat.h>
#include <linux/malloc.h>

extern int ipcperms (struct ipc_perm *ipcp, short shmflg);
extern unsigned int get_swap_page (void);
static int findkey (key_t key);
static int newseg (key_t key, int shmflg, int size);
static int shm_map (struct vm_area_struct *shmd, int remap);
static void killseg (int id);
static void shm_open (struct vm_area_struct *shmd);
static void shm_close (struct vm_area_struct *shmd);
static unsigned long shm_swap_in (struct vm_area_struct *, unsigned long, unsigned long);

static int shm_tot = 0; /* total number of shared memory pages */
static int shm_rss = 0; /* number of shared memory pages that are in memory */
static int shm_swp = 0; /* number of shared memory pages that are in swap */
static int max_shmid = 0; /* every used id is <= max_shmid */
static struct wait_queue *shm_lock = NULL; /* calling findkey() may need to wait */
static struct shmid_ds *shm_segs[SHMMNI];

static unsigned short shm_seq = 0; /* incremented, for recognizing stale ids */

/* some statistics */
static ulong swap_attempts = 0;
static ulong swap_successes = 0;
static ulong used_segs = 0;

void shm_init (void)
{
	int id;

	for (id = 0; id < SHMMNI; id++)
		shm_segs[id] = (struct shmid_ds *) IPC_UNUSED;
	shm_tot = shm_rss = shm_seq = max_shmid = used_segs = 0;
	shm_lock = NULL;
	return;
}

static int findkey (key_t key)
{
	int id;
	struct shmid_ds *shp;

	for (id = 0; id <= max_shmid; id++) {
		while ((shp = shm_segs[id]) == IPC_NOID)
			sleep_on (&shm_lock);
		if (shp == IPC_UNUSED)
			continue;
		if (key == shp->shm_perm.key)
			return id;
	}
	return -1;
}

/*
 * allocate new shmid_ds and pgtable. protected by shm_segs[id] = NOID.
 */
static int newseg (key_t key, int shmflg, int size)
{
	struct shmid_ds *shp;
	int numpages = (size + PAGE_SIZE -1) >> PAGE_SHIFT;
	int id, i;

	if (size < SHMMIN)
		return -EINVAL;
	if (shm_tot + numpages >= SHMALL)
		return -ENOSPC;
	for (id = 0; id < SHMMNI; id++)
		if (shm_segs[id] == IPC_UNUSED) {
			shm_segs[id] = (struct shmid_ds *) IPC_NOID;
			goto found;
		}
	return -ENOSPC;

found:
	shp = (struct shmid_ds *) kmalloc (sizeof (*shp), GFP_KERNEL);
	if (!shp) {
		shm_segs[id] = (struct shmid_ds *) IPC_UNUSED;
		if (shm_lock)
			wake_up (&shm_lock);
		return -ENOMEM;
	}

	shp->shm_pages = (ulong *) kmalloc (numpages*sizeof(ulong),GFP_KERNEL);
	if (!shp->shm_pages) {
		shm_segs[id] = (struct shmid_ds *) IPC_UNUSED;
		if (shm_lock)
			wake_up (&shm_lock);
		kfree(shp);
		return -ENOMEM;
	}

	for (i = 0; i < numpages; shp->shm_pages[i++] = 0);
	shm_tot += numpages;
	shp->shm_perm.key = key;
	shp->shm_perm.mode = (shmflg & S_IRWXUGO);
	shp->shm_perm.cuid = shp->shm_perm.uid = current->euid;
	shp->shm_perm.cgid = shp->shm_perm.gid = current->egid;
	shp->shm_perm.seq = shm_seq;
	shp->shm_segsz = size;
	shp->shm_cpid = current->pid;
	shp->attaches = NULL;
	shp->shm_lpid = shp->shm_nattch = 0;
	shp->shm_atime = shp->shm_dtime = 0;
	shp->shm_ctime = CURRENT_TIME;
	shp->shm_npages = numpages;

	if (id > max_shmid)
		max_shmid = id;
	shm_segs[id] = shp;
	used_segs++;
	if (shm_lock)
		wake_up (&shm_lock);
	return (unsigned int) shp->shm_perm.seq * SHMMNI + id;
}

int sys_shmget (key_t key, int size, int shmflg)
{
	struct shmid_ds *shp;
	int id = 0;

	if (size < 0 || size > SHMMAX)
		return -EINVAL;
	if (key == IPC_PRIVATE)
		return newseg(key, shmflg, size);
	if ((id = findkey (key)) == -1) {
		if (!(shmflg & IPC_CREAT))
			return -ENOENT;
		return newseg(key, shmflg, size);
	}
	if ((shmflg & IPC_CREAT) && (shmflg & IPC_EXCL))
		return -EEXIST;
	shp = shm_segs[id];
	if (shp->shm_perm.mode & SHM_DEST)
		return -EIDRM;
	if (size > shp->shm_segsz)
		return -EINVAL;
	if (ipcperms (&shp->shm_perm, shmflg))
		return -EACCES;
	return (unsigned int) shp->shm_perm.seq * SHMMNI + id;
}

/*
 * Only called after testing nattch and SHM_DEST.
 * Here pages, pgtable and shmid_ds are freed.
 */
static void killseg (int id)
{
	struct shmid_ds *shp;
	int i, numpages;
	ulong page;

	shp = shm_segs[id];
	if (shp == IPC_NOID || shp == IPC_UNUSED) {
		printk ("shm nono: killseg called on unused seg id=%d\n", id);
		return;
	}
	shp->shm_perm.seq++;     /* for shmat */
	shm_seq = (shm_seq+1) % ((unsigned)(1<<31)/SHMMNI); /* increment, but avoid overflow */
	shm_segs[id] = (struct shmid_ds *) IPC_UNUSED;
	used_segs--;
	if (id == max_shmid)
		while (max_shmid && (shm_segs[--max_shmid] == IPC_UNUSED));
	if (!shp->shm_pages) {
		printk ("shm nono: killseg shp->pages=NULL. id=%d\n", id);
		return;
	}
	numpages = shp->shm_npages;
	for (i = 0; i < numpages ; i++) {
		if (!(page = shp->shm_pages[i]))
			continue;
		if (page & PAGE_PRESENT) {
			free_page (page & PAGE_MASK);
			shm_rss--;
		} else {
			swap_free (page);
			shm_swp--;
		}
	}
	kfree(shp->shm_pages);
	shm_tot -= numpages;
	kfree(shp);
	return;
}

int sys_shmctl (int shmid, int cmd, struct shmid_ds *buf)
{
	struct shmid_ds tbuf;
	struct shmid_ds *shp;
	struct ipc_perm *ipcp;
	int id, err;

	if (cmd < 0 || shmid < 0)
		return -EINVAL;
	if (cmd == IPC_SET) {
		if (!buf)
			return -EFAULT;
		err = verify_area (VERIFY_READ, buf, sizeof (*buf));
		if (err)
			return err;
		memcpy_fromfs (&tbuf, buf, sizeof (*buf));
	}

	switch (cmd) { /* replace with proc interface ? */
	case IPC_INFO:
	{
		struct shminfo shminfo;
		if (!buf)
			return -EFAULT;
		shminfo.shmmni = SHMMNI;
		shminfo.shmmax = SHMMAX;
		shminfo.shmmin = SHMMIN;
		shminfo.shmall = SHMALL;
		shminfo.shmseg = SHMSEG;
		err = verify_area (VERIFY_WRITE, buf, sizeof (struct shminfo));
		if (err)
			return err;
		memcpy_tofs (buf, &shminfo, sizeof(struct shminfo));
		return max_shmid;
	}
	case SHM_INFO:
	{
		struct shm_info shm_info;
		if (!buf)
			return -EFAULT;
		err = verify_area (VERIFY_WRITE, buf, sizeof (shm_info));
		if (err)
			return err;
		shm_info.used_ids = used_segs;
		shm_info.shm_rss = shm_rss;
		shm_info.shm_tot = shm_tot;
		shm_info.shm_swp = shm_swp;
		shm_info.swap_attempts = swap_attempts;
		shm_info.swap_successes = swap_successes;
		memcpy_tofs (buf, &shm_info, sizeof(shm_info));
		return max_shmid;
	}
	case SHM_STAT:
		if (!buf)
			return -EFAULT;
		err = verify_area (VERIFY_WRITE, buf, sizeof (*buf));
		if (err)
			return err;
		if (shmid > max_shmid)
			return -EINVAL;
		shp = shm_segs[shmid];
		if (shp == IPC_UNUSED || shp == IPC_NOID)
			return -EINVAL;
		if (ipcperms (&shp->shm_perm, S_IRUGO))
			return -EACCES;
		id = (unsigned int) shp->shm_perm.seq * SHMMNI + shmid;
		tbuf.shm_perm   = shp->shm_perm;
		tbuf.shm_segsz  = shp->shm_segsz;
		tbuf.shm_atime  = shp->shm_atime;
		tbuf.shm_dtime  = shp->shm_dtime;
		tbuf.shm_ctime  = shp->shm_ctime;
		tbuf.shm_cpid   = shp->shm_cpid;
		tbuf.shm_lpid   = shp->shm_lpid;
		tbuf.shm_nattch = shp->shm_nattch;
		memcpy_tofs (buf, &tbuf, sizeof(*buf));
		return id;
	}

	shp = shm_segs[id = (unsigned int) shmid % SHMMNI];
	if (shp == IPC_UNUSED || shp == IPC_NOID)
		return -EINVAL;
	if (shp->shm_perm.seq != (unsigned int) shmid / SHMMNI)
		return -EIDRM;
	ipcp = &shp->shm_perm;

	switch (cmd) {
	case SHM_UNLOCK:
		if (!suser())
			return -EPERM;
		if (!(ipcp->mode & SHM_LOCKED))
			return -EINVAL;
		ipcp->mode &= ~SHM_LOCKED;
		break;
	case SHM_LOCK:
/* Allow superuser to lock segment in memory */
/* Should the pages be faulted in here or leave it to user? */
/* need to determine interaction with current->swappable */
		if (!suser())
			return -EPERM;
		if (ipcp->mode & SHM_LOCKED)
			return -EINVAL;
		ipcp->mode |= SHM_LOCKED;
		break;
	case IPC_STAT:
		if (ipcperms (ipcp, S_IRUGO))
			return -EACCES;
		if (!buf)
			return -EFAULT;
		err = verify_area (VERIFY_WRITE, buf, sizeof (*buf));
		if (err)
			return err;
		tbuf.shm_perm   = shp->shm_perm;
		tbuf.shm_segsz  = shp->shm_segsz;
		tbuf.shm_atime  = shp->shm_atime;
		tbuf.shm_dtime  = shp->shm_dtime;
		tbuf.shm_ctime  = shp->shm_ctime;
		tbuf.shm_cpid   = shp->shm_cpid;
		tbuf.shm_lpid   = shp->shm_lpid;
		tbuf.shm_nattch = shp->shm_nattch;
		memcpy_tofs (buf, &tbuf, sizeof(*buf));
		break;
	case IPC_SET:
		if (suser() || current->euid == shp->shm_perm.uid ||
		    current->euid == shp->shm_perm.cuid) {
			ipcp->uid = tbuf.shm_perm.uid;
			ipcp->gid = tbuf.shm_perm.gid;
			ipcp->mode = (ipcp->mode & ~S_IRWXUGO)
				| (tbuf.shm_perm.mode & S_IRWXUGO);
			shp->shm_ctime = CURRENT_TIME;
			break;
		}
		return -EPERM;
	case IPC_RMID:
		if (suser() || current->euid == shp->shm_perm.uid ||
		    current->euid == shp->shm_perm.cuid) {
			shp->shm_perm.mode |= SHM_DEST;
			if (shp->shm_nattch <= 0)
				killseg (id);
			break;
		}
		return -EPERM;
	default:
		return -EINVAL;
	}
	return 0;
}

/*
 * The per process internal structure for managing segments is
 * `struct vm_area_struct'.
 * A shmat will add to and shmdt will remove from the list.
 * shmd->vm_task	the attacher
 * shmd->vm_start	virt addr of attach, multiple of SHMLBA
 * shmd->vm_end		multiple of SHMLBA
 * shmd->vm_next	next attach for task
 * shmd->vm_next_share	next attach for segment
 * shmd->vm_offset	offset into segment
 * shmd->vm_pte		signature for this attach
 */

static struct vm_operations_struct shm_vm_ops = {
	shm_open,		/* open */
	shm_close,		/* close */
	NULL,			/* unmap */
	NULL,			/* protect */
	NULL,			/* sync */
	NULL,			/* advise */
	NULL,			/* nopage (done with swapin) */
	NULL,			/* wppage */
	NULL,			/* swapout (hardcoded right now) */
	shm_swap_in		/* swapin */
};

/*
 * check range is unmapped, ensure page tables exist
 * mark page table entries with shm_sgn.
 * if remap != 0 the range is remapped.
 */
static int shm_map (struct vm_area_struct *shmd, int remap)
{
	unsigned long *page_table;
	unsigned long tmp, shm_sgn;

	/* check that the range is unmapped */
	if (!remap)
		for (tmp = shmd->vm_start; tmp < shmd->vm_end; tmp += PAGE_SIZE) {
			page_table = PAGE_DIR_OFFSET(shmd->vm_task,tmp);
			if (*page_table & PAGE_PRESENT) {
				page_table = (ulong *) (PAGE_MASK & *page_table);
				page_table += ((tmp >> PAGE_SHIFT) & (PTRS_PER_PAGE-1));
				if (*page_table) {
					/* printk("shmat() -> EINVAL because address 0x%lx is already mapped.\n",tmp); */
					return -EINVAL;
				}
			}
		}

	/* clear old mappings */
	do_munmap(shmd->vm_start, shmd->vm_end - shmd->vm_start);

	/* add new mapping */
	insert_vm_struct(current, shmd);
	merge_segments(current->mm->mmap);

	/* check that the range has page_tables */
	for (tmp = shmd->vm_start; tmp < shmd->vm_end; tmp += PAGE_SIZE) {
		page_table = PAGE_DIR_OFFSET(shmd->vm_task,tmp);
		if (*page_table & PAGE_PRESENT) {
			page_table = (ulong *) (PAGE_MASK & *page_table);
			page_table += ((tmp >> PAGE_SHIFT) & (PTRS_PER_PAGE-1));
			if (*page_table) {
				if (*page_table & PAGE_PRESENT) {
					--current->mm->rss;
					free_page (*page_table & PAGE_MASK);
				}
				else
					swap_free (*page_table);
				*page_table = 0;
			}
		} else {
			unsigned long new_pt;
			if (!(new_pt = get_free_page(GFP_KERNEL)))
				return -ENOMEM;
			*page_table = new_pt | PAGE_TABLE;
			tmp |= ((PAGE_SIZE << 10) - PAGE_SIZE);
		}
	}

	/* map page range */
	shm_sgn = shmd->vm_pte + ((shmd->vm_offset >> PAGE_SHIFT) << SHM_IDX_SHIFT);
	for (tmp = shmd->vm_start; tmp < shmd->vm_end; tmp += PAGE_SIZE,
	     shm_sgn += (1 << SHM_IDX_SHIFT)) {
		page_table = PAGE_DIR_OFFSET(shmd->vm_task,tmp);
		page_table = (ulong *) (PAGE_MASK & *page_table);
		page_table += (tmp >> PAGE_SHIFT) & (PTRS_PER_PAGE-1);
		*page_table = shm_sgn;
	}
	invalidate();
	return 0;
}

/*
 * Fix shmaddr, allocate descriptor, map shm, add attach descriptor to lists.
 */
int sys_shmat (int shmid, char *shmaddr, int shmflg, ulong *raddr)
{
	struct shmid_ds *shp;
	struct vm_area_struct *shmd;
	int err;
	unsigned int id;
	unsigned long addr;

	if (shmid < 0) {
		/* printk("shmat() -> EINVAL because shmid = %d < 0\n",shmid); */
		return -EINVAL;
	}

	shp = shm_segs[id = (unsigned int) shmid % SHMMNI];
	if (shp == IPC_UNUSED || shp == IPC_NOID) {
		/* printk("shmat() -> EINVAL because shmid = %d is invalid\n",shmid); */
		return -EINVAL;
	}

	if (!(addr = (ulong) shmaddr)) {
		if (shmflg & SHM_REMAP)
			return -EINVAL;
		if (!(addr = get_unmapped_area(shp->shm_segsz)))
			return -ENOMEM;
	} else if (addr & (SHMLBA-1)) {
		if (shmflg & SHM_RND)
			addr &= ~(SHMLBA-1);       /* round down */
		else
			return -EINVAL;
	}
	if ((addr > current->mm->start_stack - 16384 - PAGE_SIZE*shp->shm_npages)) {
		/* printk("shmat() -> EINVAL because segment intersects stack\n"); */
		return -EINVAL;
	}
	if (!(shmflg & SHM_REMAP))
		for (shmd = current->mm->mmap; shmd; shmd = shmd->vm_next)
			if (!(addr >= shmd->vm_end || addr + shp->shm_segsz <= shmd->vm_start)) {
				/* printk("shmat() -> EINVAL because the interval [0x%lx,0x%lx) intersects an already mapped interval [0x%lx,0x%lx).\n",
					addr, addr + shp->shm_segsz, shmd->vm_start, shmd->vm_end); */
				return -EINVAL;
			}

	if (ipcperms(&shp->shm_perm, shmflg & SHM_RDONLY ? S_IRUGO : S_IRUGO|S_IWUGO))
		return -EACCES;
	if (shp->shm_perm.seq != (unsigned int) shmid / SHMMNI)
		return -EIDRM;

	shmd = (struct vm_area_struct *) kmalloc (sizeof(*shmd), GFP_KERNEL);
	if (!shmd)
		return -ENOMEM;
	if ((shp != shm_segs[id]) || (shp->shm_perm.seq != (unsigned int) shmid / SHMMNI)) {
		kfree(shmd);
		return -EIDRM;
	}

	shmd->vm_pte = (SHM_SWP_TYPE << 1) | (id << SHM_ID_SHIFT);
	shmd->vm_start = addr;
	shmd->vm_end = addr + shp->shm_npages * PAGE_SIZE;
	shmd->vm_task = current;
	shmd->vm_page_prot = (shmflg & SHM_RDONLY) ? PAGE_READONLY : PAGE_SHARED;
	shmd->vm_flags = VM_SHM | VM_MAYSHARE | VM_SHARED
			 | VM_MAYREAD | VM_MAYEXEC | VM_READ | VM_EXEC
			 | ((shmflg & SHM_RDONLY) ? 0 : VM_MAYWRITE | VM_WRITE);
	shmd->vm_next_share = NULL;
	shmd->vm_inode = NULL;
	shmd->vm_offset = 0;
	shmd->vm_ops = &shm_vm_ops;

	shp->shm_nattch++;            /* prevent destruction */
	if ((err = shm_map (shmd, shmflg & SHM_REMAP))) {
		if (--shp->shm_nattch <= 0 && shp->shm_perm.mode & SHM_DEST)
			killseg(id);
		kfree(shmd);
		return err;
	}

	shmd->vm_next_share = shp->attaches;
	shp->attaches = shmd;
	shp->shm_lpid = current->pid;
	shp->shm_atime = CURRENT_TIME;

	*raddr = addr;
	return 0;
}

/* This is called by fork, once for every shm attach. */
static void shm_open (struct vm_area_struct *shmd)
{
	unsigned int id;
	struct shmid_ds *shp;

	id = (shmd->vm_pte >> SHM_ID_SHIFT) & SHM_ID_MASK;
	shp = shm_segs[id];
	if (shp == IPC_UNUSED) {
		printk("shm_open: unused id=%d PANIC\n", id);
		return;
	}
	shmd->vm_next_share = shp->attaches;
	shp->attaches = shmd;
	shp->shm_nattch++;
	shp->shm_atime = CURRENT_TIME;
	shp->shm_lpid = current->pid;
}

/*
 * remove the attach descriptor shmd.
 * free memory for segment if it is marked destroyed.
 * The descriptor has already been removed from the current->mm->mmap list
 * and will later be kfree()d.
 */
static void shm_close (struct vm_area_struct *shmd)
{
	struct vm_area_struct **shmdp;
	struct shmid_ds *shp;
	int id;

	unmap_page_range (shmd->vm_start, shmd->vm_end - shmd->vm_start);

	/* remove from the list of attaches of the shm segment */
	id = (shmd->vm_pte >> SHM_ID_SHIFT) & SHM_ID_MASK;
	shp = shm_segs[id];
	for (shmdp = &shp->attaches; *shmdp; shmdp = &(*shmdp)->vm_next_share)
		if (*shmdp == shmd) {
			*shmdp = shmd->vm_next_share;
			goto found;
		}
	printk("shm_close: shm segment (id=%d) attach list inconsistent\n",id);
	printk("shm_close: %d %08lx-%08lx %c%c%c%c %08lx %08lx\n",
		shmd->vm_task->pid, shmd->vm_start, shmd->vm_end,
		shmd->vm_flags & VM_READ ? 'r' : '-',
		shmd->vm_flags & VM_WRITE ? 'w' : '-',
		shmd->vm_flags & VM_EXEC ? 'x' : '-',
		shmd->vm_flags & VM_SHARED ? 's' : 'p',
		shmd->vm_offset, shmd->vm_pte);

 found:
  	shp->shm_lpid = current->pid;
	shp->shm_dtime = CURRENT_TIME;
	if (--shp->shm_nattch <= 0 && shp->shm_perm.mode & SHM_DEST)
		killseg (id);
}

/*
 * detach and kill segment if marked destroyed.
 * The work is done in shm_close.
 */
int sys_shmdt (char *shmaddr)
{
	struct vm_area_struct *shmd, *shmdnext;

	for (shmd = current->mm->mmap; shmd; shmd = shmdnext) {
		shmdnext = shmd->vm_next;
		if (shmd->vm_ops == &shm_vm_ops
		    && shmd->vm_start - shmd->vm_offset == (ulong) shmaddr)
			do_munmap(shmd->vm_start, shmd->vm_end - shmd->vm_start);
	}
	return 0;
}

/*
 * page not present ... go through shm_pages
 */
static unsigned long shm_swap_in(struct vm_area_struct * shmd, unsigned long offset, unsigned long code)
{
	unsigned long page;
	struct shmid_ds *shp;
	unsigned int id, idx;

	id = (code >> SHM_ID_SHIFT) & SHM_ID_MASK;
	if (id != ((shmd->vm_pte >> SHM_ID_SHIFT) & SHM_ID_MASK)) {
		printk ("shm_swap_in: code id = %d and shmd id = %ld differ\n",
			id, (shmd->vm_pte >> SHM_ID_SHIFT) & SHM_ID_MASK);
		return BAD_PAGE | PAGE_SHARED;
	}
	if (id > max_shmid) {
		printk ("shm_swap_in: id=%d too big. proc mem corrupted\n", id);
		return BAD_PAGE | PAGE_SHARED;
	}
	shp = shm_segs[id];
	if (shp == IPC_UNUSED || shp == IPC_NOID) {
		printk ("shm_swap_in: id=%d invalid. Race.\n", id);
		return BAD_PAGE | PAGE_SHARED;
	}
	idx = (code >> SHM_IDX_SHIFT) & SHM_IDX_MASK;
	if (idx != (offset >> PAGE_SHIFT)) {
		printk ("shm_swap_in: code idx = %u and shmd idx = %lu differ\n",
			idx, offset >> PAGE_SHIFT);
		return BAD_PAGE | PAGE_SHARED;
	}
	if (idx >= shp->shm_npages) {
		printk ("shm_swap_in : too large page index. id=%d\n", id);
		return BAD_PAGE | PAGE_SHARED;
	}

	if (!(shp->shm_pages[idx] & PAGE_PRESENT)) {
		if (!(page = get_free_page(GFP_KERNEL))) {
			oom(current);
			return BAD_PAGE | PAGE_SHARED;
		}
		if (shp->shm_pages[idx] & PAGE_PRESENT) {
			free_page (page);
			goto done;
		}
		if (shp->shm_pages[idx]) {
			read_swap_page (shp->shm_pages[idx], (char *) page);
			if (shp->shm_pages[idx] & PAGE_PRESENT)  {
				free_page (page);
				goto done;
			}
			swap_free (shp->shm_pages[idx]);
			shm_swp--;
		}
		shm_rss++;
		shp->shm_pages[idx] = page | (PAGE_SHARED | PAGE_DIRTY);
	} else
		--current->mm->maj_flt;  /* was incremented in do_no_page */

done:
	current->mm->min_flt++;
	page = shp->shm_pages[idx];
	page &= ~(PAGE_RW & ~shmd->vm_page_prot);  /* write-protect */
	mem_map[MAP_NR(page)]++;
	return page;
}

/*
 * Goes through counter = (shm_rss << prio) present shm pages.
 */
static unsigned long swap_id = 0; /* currently being swapped */
static unsigned long swap_idx = 0; /* next to swap */

int shm_swap (int prio)
{
	unsigned long page;
	struct shmid_ds *shp;
	struct vm_area_struct *shmd;
	unsigned int swap_nr;
	unsigned long id, idx, invalid = 0;
	int counter;

	counter = shm_rss >> prio;
	if (!counter || !(swap_nr = get_swap_page()))
		return 0;

 check_id:
	shp = shm_segs[swap_id];
	if (shp == IPC_UNUSED || shp == IPC_NOID || shp->shm_perm.mode & SHM_LOCKED ) {
		swap_idx = 0;
		if (++swap_id > max_shmid)
			swap_id = 0;
		goto check_id;
	}
	id = swap_id;

 check_table:
	idx = swap_idx++;
	if (idx >= shp->shm_npages) {
		swap_idx = 0;
		if (++swap_id > max_shmid)
			swap_id = 0;
		goto check_id;
	}

	page = shp->shm_pages[idx];
	if (!(page & PAGE_PRESENT))
		goto check_table;
	swap_attempts++;

	if (--counter < 0) { /* failed */
		if (invalid)
			invalidate();
		swap_free (swap_nr);
		return 0;
	}
	for (shmd = shp->attaches; shmd; shmd = shmd->vm_next_share) {
		unsigned long tmp, *pte;
		if ((shmd->vm_pte >> SHM_ID_SHIFT & SHM_ID_MASK) != id) {
			printk ("shm_swap: id=%ld does not match shmd->vm_pte.id=%ld\n", id, shmd->vm_pte >> SHM_ID_SHIFT & SHM_ID_MASK);
			continue;
		}
		tmp = shmd->vm_start + (idx << PAGE_SHIFT) - shmd->vm_offset;
		if (!(tmp >= shmd->vm_start && tmp < shmd->vm_end))
			continue;
		pte = PAGE_DIR_OFFSET(shmd->vm_task,tmp);
		if (!(*pte & PAGE_PRESENT)) {
			printk("shm_swap: bad pgtbl! id=%ld start=%lx idx=%ld\n",
					id, shmd->vm_start, idx);
			*pte = 0;
			continue;
		}
		pte = (ulong *) (PAGE_MASK & *pte);
		pte += ((tmp >> PAGE_SHIFT) & (PTRS_PER_PAGE-1));
		tmp = *pte;
		if (!(tmp & PAGE_PRESENT))
			continue;
		if (tmp & PAGE_ACCESSED) {
			*pte &= ~PAGE_ACCESSED;
			continue;
		}
		*pte = shmd->vm_pte | idx << SHM_IDX_SHIFT;
		mem_map[MAP_NR(page)]--;
		shmd->vm_task->mm->rss--;
		invalid++;
	}

	if (mem_map[MAP_NR(page)] != 1)
		goto check_table;
	page &= PAGE_MASK;
	shp->shm_pages[idx] = swap_nr;
	if (invalid)
		invalidate();
	write_swap_page (swap_nr, (char *) page);
	free_page (page);
	swap_successes++;
	shm_swp++;
	shm_rss--;
	return 1;
}
