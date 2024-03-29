/*
 *  linux/arch/i386/mm/fault.c
 *
 *  Copyright (C) 1995  Linus Torvalds
 */

#include <linux/config.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/head.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/ptrace.h>
#include <linux/mman.h>

#include <asm/system.h>
#include <asm/segment.h>

extern unsigned long pg0[1024];		/* page table for 0-4MB for everybody */

extern void scsi_mem_init(unsigned long);
extern void sound_mem_init(void);
extern void die_if_kernel(char *,struct pt_regs *,long);
extern void show_net_buffers(void);

/*
 * Define this if things work differently on a i386 and a i486:
 * it will (on a i486) warn about kernel memory accesses that are
 * done without a 'verify_area(VERIFY_WRITE,..)'
 */
#undef CONFIG_TEST_VERIFY_AREA

/*
 * This routine handles page faults.  It determines the address,
 * and the problem, and then passes it off to one of the appropriate
 * routines.
 */
asmlinkage void do_page_fault(struct pt_regs *regs, unsigned long error_code)
{
	struct vm_area_struct * vma;
	unsigned long address;
	unsigned long page;

	/* get the address */
	__asm__("movl %%cr2,%0":"=r" (address));
	for (vma = current->mm->mmap ; ; vma = vma->vm_next) {
		if (!vma)
			goto bad_area;
		if (vma->vm_end > address)
			break;
	}
	if (vma->vm_start <= address)
		goto good_area;
	if (!(vma->vm_flags & VM_GROWSDOWN))
		goto bad_area;
	if (vma->vm_end - address > current->rlim[RLIMIT_STACK].rlim_cur)
		goto bad_area;
	vma->vm_offset -= vma->vm_start - (address & PAGE_MASK);
	vma->vm_start = (address & PAGE_MASK);
/*
 * Ok, we have a good vm_area for this memory access, so
 * we can handle it..
 */
good_area:
	if (regs->eflags & VM_MASK) {
		unsigned long bit = (address - 0xA0000) >> PAGE_SHIFT;
		if (bit < 32)
			current->tss.screen_bitmap |= 1 << bit;
	}
	if (!(vma->vm_page_prot & PAGE_USER))
		goto bad_area;
	if (error_code & PAGE_PRESENT) {
		if (!(vma->vm_page_prot & (PAGE_RW | PAGE_COW)))
			goto bad_area;
#ifdef CONFIG_TEST_VERIFY_AREA
		if (regs->cs == KERNEL_CS)
			printk("WP fault at %08x\n", regs->eip);
#endif
		do_wp_page(vma, address, error_code);
		return;
	}
	do_no_page(vma, address, error_code);
	return;

/*
 * Something tried to access memory that isn't in our memory map..
 * Fix it, but check if it's kernel or user first..
 */
bad_area:
	if (error_code & PAGE_USER) {
		current->tss.cr2 = address;
		current->tss.error_code = error_code;
		current->tss.trap_no = 14;
		send_sig(SIGSEGV, current, 1);
		return;
	}
/*
 * Oops. The kernel tried to access some bad page. We'll have to
 * terminate things with extreme prejudice.
 */
	if (wp_works_ok < 0 && address == TASK_SIZE && (error_code & PAGE_PRESENT)) {
		wp_works_ok = 1;
		pg0[0] = PAGE_SHARED;
		invalidate();
		printk("This processor honours the WP bit even when in supervisor mode. Good.\n");
		return;
	}
	if ((unsigned long) (address-TASK_SIZE) < PAGE_SIZE) {
		printk(KERN_ALERT "Unable to handle kernel NULL pointer dereference");
		pg0[0] = PAGE_SHARED;
	} else
		printk(KERN_ALERT "Unable to handle kernel paging request");
	printk(" at virtual address %08lx\n",address);
	__asm__("movl %%cr3,%0" : "=r" (page));
	printk(KERN_ALERT "current->tss.cr3 = %08lx, %%cr3 = %08lx\n",
		current->tss.cr3, page);
	page = ((unsigned long *) page)[address >> 22];
	printk(KERN_ALERT "*pde = %08lx\n", page);
	if (page & PAGE_PRESENT) {
		page &= PAGE_MASK;
		address &= 0x003ff000;
		page = ((unsigned long *) page)[address >> PAGE_SHIFT];
		printk(KERN_ALERT "*pte = %08lx\n", page);
	}
	die_if_kernel("Oops", regs, error_code);
	do_exit(SIGKILL);
}

/*
 * BAD_PAGE is the page that is used for page faults when linux
 * is out-of-memory. Older versions of linux just did a
 * do_exit(), but using this instead means there is less risk
 * for a process dying in kernel mode, possibly leaving a inode
 * unused etc..
 *
 * BAD_PAGETABLE is the accompanying page-table: it is initialized
 * to point to BAD_PAGE entries.
 *
 * ZERO_PAGE is a special page that is used for zero-initialized
 * data and COW.
 */
unsigned long __bad_pagetable(void)
{
	extern char empty_bad_page_table[PAGE_SIZE];

	__asm__ __volatile__("cld ; rep ; stosl":
		:"a" (BAD_PAGE + PAGE_TABLE),
		 "D" ((long) empty_bad_page_table),
		 "c" (PTRS_PER_PAGE)
		:"di","cx");
	return (unsigned long) empty_bad_page_table;
}

unsigned long __bad_page(void)
{
	extern char empty_bad_page[PAGE_SIZE];

	__asm__ __volatile__("cld ; rep ; stosl":
		:"a" (0),
		 "D" ((long) empty_bad_page),
		 "c" (PTRS_PER_PAGE)
		:"di","cx");
	return (unsigned long) empty_bad_page;
}

unsigned long __zero_page(void)
{
	extern char empty_zero_page[PAGE_SIZE];

	__asm__ __volatile__("cld ; rep ; stosl":
		:"a" (0),
		 "D" ((long) empty_zero_page),
		 "c" (PTRS_PER_PAGE)
		:"di","cx");
	return (unsigned long) empty_zero_page;
}

void show_mem(void)
{
	int i,free = 0,total = 0,reserved = 0;
	int shared = 0;

	printk("Mem-info:\n");
	show_free_areas();
	printk("Free swap:       %6dkB\n",nr_swap_pages<<(PAGE_SHIFT-10));
	i = high_memory >> PAGE_SHIFT;
	while (i-- > 0) {
		total++;
		if (mem_map[i] & MAP_PAGE_RESERVED)
			reserved++;
		else if (!mem_map[i])
			free++;
		else
			shared += mem_map[i]-1;
	}
	printk("%d pages of RAM\n",total);
	printk("%d free pages\n",free);
	printk("%d reserved pages\n",reserved);
	printk("%d pages shared\n",shared);
	show_buffers();
#ifdef CONFIG_NET
	show_net_buffers();
#endif
}

extern unsigned long free_area_init(unsigned long, unsigned long);

/*
 * paging_init() sets up the page tables - note that the first 4MB are
 * already mapped by head.S.
 *
 * This routines also unmaps the page at virtual kernel address 0, so
 * that we can trap those pesky NULL-reference errors in the kernel.
 */
unsigned long paging_init(unsigned long start_mem, unsigned long end_mem)
{
	unsigned long * pg_dir;
	unsigned long * pg_table;
	unsigned long tmp;
	unsigned long address;

/*
 * Physical page 0 is special; it's not touched by Linux since BIOS
 * and SMM (for laptops with [34]86/SL chips) may need it.  It is read
 * and write protected to detect null pointer references in the
 * kernel.
 */
#if 0
	memset((void *) 0, 0, PAGE_SIZE);
#endif
	start_mem = PAGE_ALIGN(start_mem);
	address = 0;
	pg_dir = swapper_pg_dir;
	while (address < end_mem) {
		tmp = *(pg_dir + 768);		/* at virtual addr 0xC0000000 */
		if (!tmp) {
			tmp = start_mem | PAGE_TABLE;
			*(pg_dir + 768) = tmp;
			start_mem += PAGE_SIZE;
		}
		*pg_dir = tmp;			/* also map it in at 0x0000000 for init */
		pg_dir++;
		pg_table = (unsigned long *) (tmp & PAGE_MASK);
		for (tmp = 0 ; tmp < PTRS_PER_PAGE ; tmp++,pg_table++) {
			if (address < end_mem)
				*pg_table = address | PAGE_SHARED;
			else
				*pg_table = 0;
			address += PAGE_SIZE;
		}
	}
	invalidate();
	return free_area_init(start_mem, end_mem);
}

void mem_init(unsigned long start_low_mem,
	      unsigned long start_mem, unsigned long end_mem)
{
	int codepages = 0;
	int reservedpages = 0;
	int datapages = 0;
	unsigned long tmp;
	extern int etext;

	end_mem &= PAGE_MASK;
	high_memory = end_mem;

	/* mark usable pages in the mem_map[] */
	start_low_mem = PAGE_ALIGN(start_low_mem);
	start_mem = PAGE_ALIGN(start_mem);

	/*
	 * IBM messed up *AGAIN* in their thinkpad: 0xA0000 -> 0x9F000.
	 * They seem to have done something stupid with the floppy
	 * controller as well..
	 */
	while (start_low_mem < 0x9f000) {
		mem_map[MAP_NR(start_low_mem)] = 0;
		start_low_mem += PAGE_SIZE;
	}

	while (start_mem < high_memory) {
		mem_map[MAP_NR(start_mem)] = 0;
		start_mem += PAGE_SIZE;
	}
#ifdef CONFIG_SCSI
	scsi_mem_init(high_memory);
#endif
#ifdef CONFIG_SOUND
	sound_mem_init();
#endif
	for (tmp = 0 ; tmp < high_memory ; tmp += PAGE_SIZE) {
		if (mem_map[MAP_NR(tmp)]) {
			if (tmp >= 0xA0000 && tmp < 0x100000)
				reservedpages++;
			else if (tmp < (unsigned long) &etext)
				codepages++;
			else
				datapages++;
			continue;
		}
		mem_map[MAP_NR(tmp)] = 1;
		free_page(tmp);
	}
	tmp = nr_free_pages << PAGE_SHIFT;
	printk("Memory: %luk/%luk available (%dk kernel code, %dk reserved, %dk data)\n",
		tmp >> 10,
		high_memory >> 10,
		codepages << (PAGE_SHIFT-10),
		reservedpages << (PAGE_SHIFT-10),
		datapages << (PAGE_SHIFT-10));
/* test if the WP bit is honoured in supervisor mode */
	wp_works_ok = -1;
	pg0[0] = PAGE_READONLY;
	invalidate();
	__asm__ __volatile__("movb 0,%%al ; movb %%al,0": : :"ax", "memory");
	pg0[0] = 0;
	invalidate();
	if (wp_works_ok < 0)
		wp_works_ok = 0;
#ifdef CONFIG_TEST_VERIFY_AREA
	wp_works_ok = 0;
#endif
	return;
}

void si_meminfo(struct sysinfo *val)
{
	int i;

	i = high_memory >> PAGE_SHIFT;
	val->totalram = 0;
	val->sharedram = 0;
	val->freeram = nr_free_pages << PAGE_SHIFT;
	val->bufferram = buffermem;
	while (i-- > 0)  {
		if (mem_map[i] & MAP_PAGE_RESERVED)
			continue;
		val->totalram++;
		if (!mem_map[i])
			continue;
		val->sharedram += mem_map[i]-1;
	}
	val->totalram <<= PAGE_SHIFT;
	val->sharedram <<= PAGE_SHIFT;
	return;
}
