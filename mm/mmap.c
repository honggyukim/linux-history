/*
 *	linux/mm/mmap.c
 *
 * Written by obz.
 */
#include <linux/stat.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/shm.h>
#include <linux/errno.h>
#include <linux/mman.h>
#include <linux/string.h>
#include <linux/malloc.h>

#include <asm/segment.h>
#include <asm/system.h>

static int anon_map(struct inode *, struct file *, struct vm_area_struct *);

/*
 * description of effects of mapping type and prot in current implementation.
 * this is due to the limited x86 page protection hardware.  The expected
 * behavior is in parens:
 *
 * map_type	prot
 *		PROT_NONE	PROT_READ	PROT_WRITE	PROT_EXEC
 * MAP_SHARED	r: (no) no	r: (yes) yes	r: (no) yes	r: (no) yes
 *		w: (no) no	w: (no) no	w: (yes) yes	w: (no) no
 *		x: (no) no	x: (no) yes	x: (no) yes	x: (yes) yes
 *		
 * MAP_PRIVATE	r: (no) no	r: (yes) yes	r: (no) yes	r: (no) yes
 *		w: (no) no	w: (no) no	w: (copy) copy	w: (no) no
 *		x: (no) no	x: (no) yes	x: (no) yes	x: (yes) yes
 *
 */

int do_mmap(struct file * file, unsigned long addr, unsigned long len,
	unsigned long prot, unsigned long flags, unsigned long off)
{
	int mask, error;
	struct vm_area_struct * vma;

	if ((len = PAGE_ALIGN(len)) == 0)
		return addr;

	if (addr > TASK_SIZE || len > TASK_SIZE || addr > TASK_SIZE-len)
		return -EINVAL;

	/* offset overflow? */
	if (off + len < off)
		return -EINVAL;

	/*
	 * do simple checking here so the lower-level routines won't have
	 * to. we assume access permissions have been handled by the open
	 * of the memory object, so we don't do any here.
	 */

	if (file != NULL) {
		switch (flags & MAP_TYPE) {
		case MAP_SHARED:
			if ((prot & PROT_WRITE) && !(file->f_mode & 2))
				return -EACCES;
			/* fall through */
		case MAP_PRIVATE:
			if (!(file->f_mode & 1))
				return -EACCES;
			break;

		default:
			return -EINVAL;
		}
		if ((flags & MAP_DENYWRITE) && (file->f_inode->i_wcount > 0))
			return -ETXTBSY;
	} else if ((flags & MAP_TYPE) == MAP_SHARED)
		return -EINVAL;

	/*
	 * obtain the address to map to. we verify (or select) it and ensure
	 * that it represents a valid section of the address space.
	 */

	if (flags & MAP_FIXED) {
		if (addr & ~PAGE_MASK)
			return -EINVAL;
		if (len > TASK_SIZE || addr > TASK_SIZE - len)
			return -EINVAL;
	} else {
		addr = get_unmapped_area(len);
		if (!addr)
			return -ENOMEM;
	}

	/*
	 * determine the object being mapped and call the appropriate
	 * specific mapper. the address has already been validated, but
	 * not unmapped, but the maps are removed from the list.
	 */
	if (file && (!file->f_op || !file->f_op->mmap))
		return -ENODEV;
	mask = PAGE_PRESENT;
	if (prot & (PROT_READ | PROT_EXEC))
		mask |= PAGE_READONLY;
	if (prot & PROT_WRITE)
		if ((flags & MAP_TYPE) == MAP_PRIVATE)
			mask |= PAGE_COPY;
		else
			mask |= PAGE_SHARED;

	vma = (struct vm_area_struct *)kmalloc(sizeof(struct vm_area_struct),
		GFP_KERNEL);
	if (!vma)
		return -ENOMEM;

	vma->vm_task = current;
	vma->vm_start = addr;
	vma->vm_end = addr + len;
	vma->vm_page_prot = mask;
	vma->vm_flags = prot & (VM_READ | VM_WRITE | VM_EXEC);
	vma->vm_flags |= flags & (VM_GROWSDOWN | VM_DENYWRITE | VM_EXECUTABLE);

	if (file) {
		if (file->f_mode & 1)
			vma->vm_flags |= VM_MAYREAD | VM_MAYWRITE | VM_MAYEXEC;
		if (flags & MAP_SHARED) {
			vma->vm_flags |= VM_SHARED | VM_MAYSHARE;
			if (!(file->f_mode & 2))
				vma->vm_flags &= ~VM_MAYWRITE;
		}
	} else
		vma->vm_flags |= VM_MAYREAD | VM_MAYWRITE | VM_MAYEXEC;
	vma->vm_ops = NULL;
	vma->vm_offset = off;
	vma->vm_inode = NULL;
	vma->vm_pte = 0;

	do_munmap(addr, len);	/* Clear old maps */

	if (file)
		error = file->f_op->mmap(file->f_inode, file, vma);
	else
		error = anon_map(NULL, NULL, vma);
	
	if (error) {
		kfree(vma);
		return error;
	}
	insert_vm_struct(current, vma);
	merge_segments(current->mm->mmap);
	return addr;
}

/*
 * Get an address range which is currently unmapped.
 * For mmap() without MAP_FIXED and shmat() with addr=0.
 * Return value 0 means ENOMEM.
 */
unsigned long get_unmapped_area(unsigned long len)
{
	struct vm_area_struct * vmm;
	unsigned long gap_start = 0, gap_end;

	for (vmm = current->mm->mmap; ; vmm = vmm->vm_next) {
		if (gap_start < SHM_RANGE_START)
			gap_start = SHM_RANGE_START;
		if (!vmm || ((gap_end = vmm->vm_start) > SHM_RANGE_END))
			gap_end = SHM_RANGE_END;
		gap_start = PAGE_ALIGN(gap_start);
		gap_end &= PAGE_MASK;
		if ((gap_start <= gap_end) && (gap_end - gap_start >= len))
			return gap_start;
		if (!vmm)
			return 0;
		gap_start = vmm->vm_end;
	}
}

asmlinkage int sys_mmap(unsigned long *buffer)
{
	int error;
	unsigned long flags;
	struct file * file = NULL;

	error = verify_area(VERIFY_READ, buffer, 6*sizeof(long));
	if (error)
		return error;
	flags = get_fs_long(buffer+3);
	if (!(flags & MAP_ANONYMOUS)) {
		unsigned long fd = get_fs_long(buffer+4);
		if (fd >= NR_OPEN || !(file = current->files->fd[fd]))
			return -EBADF;
	}
	return do_mmap(file, get_fs_long(buffer), get_fs_long(buffer+1),
		get_fs_long(buffer+2), flags, get_fs_long(buffer+5));
}

/*
 * Normal function to fix up a mapping
 * This function is the default for when an area has no specific
 * function.  This may be used as part of a more specific routine.
 * This function works out what part of an area is affected and
 * adjusts the mapping information.  Since the actual page
 * manipulation is done in do_mmap(), none need be done here,
 * though it would probably be more appropriate.
 *
 * By the time this function is called, the area struct has been
 * removed from the process mapping list, so it needs to be
 * reinserted if necessary.
 *
 * The 4 main cases are:
 *    Unmapping the whole area
 *    Unmapping from the start of the segment to a point in it
 *    Unmapping from an intermediate point to the end
 *    Unmapping between to intermediate points, making a hole.
 *
 * Case 4 involves the creation of 2 new areas, for each side of
 * the hole.
 */
void unmap_fixup(struct vm_area_struct *area,
		 unsigned long addr, size_t len)
{
	struct vm_area_struct *mpnt;
	unsigned long end = addr + len;

	if (addr < area->vm_start || addr >= area->vm_end ||
	    end <= area->vm_start || end > area->vm_end ||
	    end < addr)
	{
		printk("unmap_fixup: area=%lx-%lx, unmap %lx-%lx!!\n",
		       area->vm_start, area->vm_end, addr, end);
		return;
	}

	/* Unmapping the whole area */
	if (addr == area->vm_start && end == area->vm_end) {
		if (area->vm_ops && area->vm_ops->close)
			area->vm_ops->close(area);
		if (area->vm_inode)
			iput(area->vm_inode);
		return;
	}

	/* Work out to one of the ends */
	if (addr >= area->vm_start && end == area->vm_end)
		area->vm_end = addr;
	if (addr == area->vm_start && end <= area->vm_end) {
		area->vm_offset += (end - area->vm_start);
		area->vm_start = end;
	}

	/* Unmapping a hole */
	if (addr > area->vm_start && end < area->vm_end)
	{
		/* Add end mapping -- leave beginning for below */
		mpnt = (struct vm_area_struct *)kmalloc(sizeof(*mpnt), GFP_KERNEL);

		if (!mpnt)
			return;
		*mpnt = *area;
		mpnt->vm_offset += (end - area->vm_start);
		mpnt->vm_start = end;
		if (mpnt->vm_inode)
			mpnt->vm_inode->i_count++;
		if (mpnt->vm_ops && mpnt->vm_ops->open)
			mpnt->vm_ops->open(mpnt);
		area->vm_end = addr;	/* Truncate area */
		insert_vm_struct(current, mpnt);
	}

	/* construct whatever mapping is needed */
	mpnt = (struct vm_area_struct *)kmalloc(sizeof(*mpnt), GFP_KERNEL);
	if (!mpnt)
		return;
	*mpnt = *area;
	if (mpnt->vm_ops && mpnt->vm_ops->open)
		mpnt->vm_ops->open(mpnt);
	if (area->vm_ops && area->vm_ops->close) {
		area->vm_end = area->vm_start;
		area->vm_ops->close(area);
	}
	insert_vm_struct(current, mpnt);
}

asmlinkage int sys_munmap(unsigned long addr, size_t len)
{
	return do_munmap(addr, len);
}

/*
 * Munmap is split into 2 main parts -- this part which finds
 * what needs doing, and the areas themselves, which do the
 * work.  This now handles partial unmappings.
 * Jeremy Fitzhardine <jeremy@sw.oz.au>
 */
int do_munmap(unsigned long addr, size_t len)
{
	struct vm_area_struct *mpnt, **npp, *free;

	if ((addr & ~PAGE_MASK) || addr > TASK_SIZE || len > TASK_SIZE-addr)
		return -EINVAL;

	if ((len = PAGE_ALIGN(len)) == 0)
		return 0;

	/*
	 * Check if this memory area is ok - put it on the temporary
	 * list if so..  The checks here are pretty simple --
	 * every area affected in some way (by any overlap) is put
	 * on the list.  If nothing is put on, nothing is affected.
	 */
	npp = &current->mm->mmap;
	free = NULL;
	for (mpnt = *npp; mpnt != NULL; mpnt = *npp) {
		unsigned long end = addr+len;

		if ((addr < mpnt->vm_start && end <= mpnt->vm_start) ||
		    (addr >= mpnt->vm_end && end > mpnt->vm_end))
		{
			npp = &mpnt->vm_next;
			continue;
		}

		*npp = mpnt->vm_next;
		mpnt->vm_next = free;
		free = mpnt;
	}

	if (free == NULL)
		return 0;

	/*
	 * Ok - we have the memory areas we should free on the 'free' list,
	 * so release them, and unmap the page range..
	 * If the one of the segments is only being partially unmapped,
	 * it will put new vm_area_struct(s) into the address space.
	 */
	while (free) {
		unsigned long st, end;

		mpnt = free;
		free = free->vm_next;

		remove_shared_vm_struct(mpnt);

		st = addr < mpnt->vm_start ? mpnt->vm_start : addr;
		end = addr+len;
		end = end > mpnt->vm_end ? mpnt->vm_end : end;

		if (mpnt->vm_ops && mpnt->vm_ops->unmap)
			mpnt->vm_ops->unmap(mpnt, st, end-st);

		unmap_fixup(mpnt, st, end-st);
		kfree(mpnt);
	}

	unmap_page_range(addr, len);
	return 0;
}

/*
 * Insert vm structure into process list sorted by address
 * and into the inode's i_mmap ring.
 */
void insert_vm_struct(struct task_struct *t, struct vm_area_struct *vmp)
{
	struct vm_area_struct **p, *mpnt, *share;
	struct inode * inode;

	p = &t->mm->mmap;
	while ((mpnt = *p) != NULL) {
		if (mpnt->vm_start > vmp->vm_start)
			break;
		if (mpnt->vm_end > vmp->vm_start)
			printk("insert_vm_struct: overlapping memory areas\n");
		p = &mpnt->vm_next;
	}
	vmp->vm_next = mpnt;
	*p = vmp;

	inode = vmp->vm_inode;
	if (!inode)
		return;

	/* insert vmp into inode's circular share list */
	if ((share = inode->i_mmap)) {
		vmp->vm_next_share = share->vm_next_share;
		vmp->vm_next_share->vm_prev_share = vmp;
		share->vm_next_share = vmp;
		vmp->vm_prev_share = share;
	} else
		inode->i_mmap = vmp->vm_next_share = vmp->vm_prev_share = vmp;
}

/*
 * Remove one vm structure from the inode's i_mmap ring.
 */
void remove_shared_vm_struct(struct vm_area_struct *mpnt)
{
	struct inode * inode = mpnt->vm_inode;

	if (!inode)
		return;

	if (mpnt->vm_next_share == mpnt) {
		if (inode->i_mmap != mpnt)
			printk("Inode i_mmap ring corrupted\n");
		inode->i_mmap = NULL;
		return;
	}

	if (inode->i_mmap == mpnt)
		inode->i_mmap = mpnt->vm_next_share;

	mpnt->vm_prev_share->vm_next_share = mpnt->vm_next_share;
	mpnt->vm_next_share->vm_prev_share = mpnt->vm_prev_share;
}

/*
 * Merge a list of memory segments if possible.
 * Redundant vm_area_structs are freed.
 * This assumes that the list is ordered by address.
 */
void merge_segments(struct vm_area_struct *mpnt)
{
	struct vm_area_struct *prev, *next;

	if (mpnt == NULL)
		return;
	
	for(prev = mpnt, mpnt = mpnt->vm_next;
	    mpnt != NULL;
	    prev = mpnt, mpnt = next)
	{
		next = mpnt->vm_next;

		/*
		 * To share, we must have the same inode, operations.. 
		 */
		if (mpnt->vm_inode != prev->vm_inode)
			continue;
		if (mpnt->vm_pte != prev->vm_pte)
			continue;
		if (mpnt->vm_ops != prev->vm_ops)
			continue;
		if (mpnt->vm_page_prot != prev->vm_page_prot ||
		    mpnt->vm_flags != prev->vm_flags)
			continue;
		if (prev->vm_end != mpnt->vm_start)
			continue;
		/*
		 * and if we have an inode, the offsets must be contiguous..
		 */
		if ((mpnt->vm_inode != NULL) || (mpnt->vm_flags & VM_SHM)) {
			if (prev->vm_offset + prev->vm_end - prev->vm_start != mpnt->vm_offset)
				continue;
		}

		/*
		 * merge prev with mpnt and set up pointers so the new
		 * big segment can possibly merge with the next one.
		 * The old unused mpnt is freed.
		 */
		prev->vm_end = mpnt->vm_end;
		prev->vm_next = mpnt->vm_next;
		if (mpnt->vm_ops && mpnt->vm_ops->close) {
			mpnt->vm_offset += mpnt->vm_end - mpnt->vm_start;
			mpnt->vm_start = mpnt->vm_end;
			mpnt->vm_ops->close(mpnt);
		}
		remove_shared_vm_struct(mpnt);
		if (mpnt->vm_inode)
			mpnt->vm_inode->i_count--;
		kfree_s(mpnt, sizeof(*mpnt));
		mpnt = prev;
	}
}

/*
 * Map memory not associated with any file into a process
 * address space.  Adjacent memory is merged.
 */
static int anon_map(struct inode *ino, struct file * file, struct vm_area_struct * vma)
{
	if (zeromap_page_range(vma->vm_start, vma->vm_end - vma->vm_start, vma->vm_page_prot))
		return -ENOMEM;
	return 0;
}
