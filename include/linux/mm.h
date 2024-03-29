#ifndef _LINUX_MM_H
#define _LINUX_MM_H

#include <asm/page.h>

#include <linux/sched.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/string.h>

#define VERIFY_READ 0
#define VERIFY_WRITE 1

extern int verify_area(int, const void *, unsigned long);

/*
 * Linux kernel virtual memory manager primitives.
 * The idea being to have a "virtual" mm in the same way
 * we have a virtual fs - giving a cleaner interface to the
 * mm details, and allowing different kinds of memory mappings
 * (from shared memory to executable loading to arbitrary
 * mmap() functions).
 */

/*
 * This struct defines a memory VMM memory area. There is one of these
 * per VM-area/task.  A VM area is any part of the process virtual memory
 * space that has a special rule for the page-fault handlers (ie a shared
 * library, the executable area etc).
 */
struct vm_area_struct {
	struct task_struct * vm_task;		/* VM area parameters */
	unsigned long vm_start;
	unsigned long vm_end;
	unsigned short vm_page_prot;
	unsigned short vm_flags;
/* linked list of VM areas per task, sorted by address */
	struct vm_area_struct * vm_next;
/* for areas with inode, the circular list inode->i_mmap */
/* for shm areas, the linked list of attaches */
/* otherwise unused */
	struct vm_area_struct * vm_next_share;
	struct vm_area_struct * vm_prev_share;
/* more */
	struct vm_operations_struct * vm_ops;
	unsigned long vm_offset;
	struct inode * vm_inode;
	unsigned long vm_pte;			/* shared mem */
};

/*
 * vm_flags..
 */
#define VM_READ		0x0001	/* currently active flags */
#define VM_WRITE	0x0002
#define VM_EXEC		0x0004
#define VM_SHARED	0x0008

#define VM_MAYREAD	0x0010	/* limits for mprotect() etc */
#define VM_MAYWRITE	0x0020
#define VM_MAYEXEC	0x0040
#define VM_MAYSHARE	0x0080

#define VM_GROWSDOWN	0x0100	/* general info on the segment */
#define VM_GROWSUP	0x0200
#define VM_SHM		0x0400
#define VM_DENYWRITE	0x0800	/* ETXTBSY on write attempts.. */

#define VM_EXECUTABLE	0x1000

#define VM_STACK_FLAGS	0x0177

/*
 * These are the virtual MM functions - opening of an area, closing and
 * unmapping it (needed to keep files on disk up-to-date etc), pointer
 * to the functions called when a no-page or a wp-page exception occurs. 
 */
struct vm_operations_struct {
	void (*open)(struct vm_area_struct * area);
	void (*close)(struct vm_area_struct * area);
	void (*unmap)(struct vm_area_struct *area, unsigned long, size_t);
	void (*protect)(struct vm_area_struct *area, unsigned long, size_t, unsigned int newprot);
	void (*sync)(struct vm_area_struct *area, unsigned long, size_t, unsigned int flags);
	void (*advise)(struct vm_area_struct *area, unsigned long, size_t, unsigned int advise);
	unsigned long (*nopage)(struct vm_area_struct * area, unsigned long address,
		unsigned long page, int error_code);
	unsigned long (*wppage)(struct vm_area_struct * area, unsigned long address,
		unsigned long page);
	void (*swapout)(struct vm_area_struct *,  unsigned long, unsigned long *);
	unsigned long (*swapin)(struct vm_area_struct *, unsigned long, unsigned long);
};

extern unsigned long __bad_page(void);
extern unsigned long __bad_pagetable(void);
extern unsigned long __zero_page(void);

#define BAD_PAGETABLE __bad_pagetable()
#define BAD_PAGE __bad_page()
#define ZERO_PAGE __zero_page()

/* planning stage.. */
#define P_DIRTY		0x0001
#define P_LOCKED	0x0002
#define P_UPTODATE	0x0004
#define P_RESERVED	0x8000

struct page_info {
	unsigned short flags;
	unsigned short count;
	struct inode * inode;
	unsigned long offset;
	struct page_info * next_same_inode;
	struct page_info * prev_same_inode;
	struct page_info * next_hash;
	struct page_info * prev_hash;
	struct wait_queue *wait;
};
/* end of planning stage */

#ifdef __KERNEL__

/*
 * Free area management
 */

extern int nr_swap_pages;
extern int nr_free_pages;
extern int min_free_pages;

#define NR_MEM_LISTS 6

struct mem_list {
	struct mem_list * next;
	struct mem_list * prev;
};

extern struct mem_list free_area_list[NR_MEM_LISTS];
extern unsigned char * free_area_map[NR_MEM_LISTS];

/*
 * This is timing-critical - most of the time in getting a new page
 * goes to clearing the page. If you want a page without the clearing
 * overhead, just use __get_free_page() directly..
 */
#define __get_free_page(priority) __get_free_pages((priority),0)
extern unsigned long __get_free_pages(int priority, unsigned long gfporder);
extern unsigned long __get_dma_pages(int priority, unsigned long gfporder);
extern inline unsigned long get_free_page(int priority)
{
	unsigned long page;

	page = __get_free_page(priority);
	if (page)
		memset((void *) page, 0, PAGE_SIZE);
	return page;
}

/* memory.c & swap.c*/

#define free_page(addr) free_pages((addr),0)
extern void free_pages(unsigned long addr, unsigned long order);

extern void show_free_areas(void);
extern unsigned long put_dirty_page(struct task_struct * tsk,unsigned long page,
	unsigned long address);

extern void free_page_tables(struct task_struct * tsk);
extern void clear_page_tables(struct task_struct * tsk);
extern int copy_page_tables(struct task_struct * to);
extern int clone_page_tables(struct task_struct * to);
extern int unmap_page_range(unsigned long from, unsigned long size);
extern int remap_page_range(unsigned long from, unsigned long to, unsigned long size, int mask);
extern int zeromap_page_range(unsigned long from, unsigned long size, int mask);

extern void do_wp_page(struct vm_area_struct * vma, unsigned long address,
	unsigned long error_code);
extern void do_no_page(struct vm_area_struct * vma, unsigned long address,
	unsigned long error_code);

extern unsigned long paging_init(unsigned long start_mem, unsigned long end_mem);
extern void mem_init(unsigned long low_start_mem,
		     unsigned long start_mem, unsigned long end_mem);
extern void show_mem(void);
extern void oom(struct task_struct * task);
extern void si_meminfo(struct sysinfo * val);

/* vmalloc.c */

extern void * vmalloc(unsigned long size);
extern void vfree(void * addr);
extern int vread(char *buf, char *addr, int count);

/* swap.c */

extern void swap_free(unsigned long page_nr);
extern unsigned long swap_duplicate(unsigned long page_nr);
extern unsigned long swap_in(unsigned long entry);
extern void si_swapinfo(struct sysinfo * val);
extern void rw_swap_page(int rw, unsigned long nr, char * buf);

/* mmap.c */
extern int do_mmap(struct file * file, unsigned long addr, unsigned long len,
	unsigned long prot, unsigned long flags, unsigned long off);
extern void merge_segments(struct vm_area_struct *);
extern void insert_vm_struct(struct task_struct *, struct vm_area_struct *);
extern void remove_shared_vm_struct(struct vm_area_struct *);
extern int do_munmap(unsigned long, size_t);
extern unsigned long get_unmapped_area(unsigned long);

#define read_swap_page(nr,buf) \
	rw_swap_page(READ,(nr),(buf))
#define write_swap_page(nr,buf) \
	rw_swap_page(WRITE,(nr),(buf))

extern unsigned long high_memory;

#define MAP_NR(addr) ((addr) >> PAGE_SHIFT)
#define MAP_PAGE_RESERVED (1<<15)

extern unsigned short * mem_map;

#define PAGE_PRESENT	0x001
#define PAGE_RW		0x002
#define PAGE_USER	0x004
#define PAGE_PWT	0x008	/* 486 only - not used currently */
#define PAGE_PCD	0x010	/* 486 only - not used currently */
#define PAGE_ACCESSED	0x020
#define PAGE_DIRTY	0x040
#define PAGE_COW	0x200	/* implemented in software (one of the AVL bits) */

#define PAGE_PRIVATE	(PAGE_PRESENT | PAGE_RW | PAGE_USER | PAGE_ACCESSED | PAGE_COW)
#define PAGE_SHARED	(PAGE_PRESENT | PAGE_RW | PAGE_USER | PAGE_ACCESSED)
#define PAGE_COPY	(PAGE_PRESENT | PAGE_USER | PAGE_ACCESSED | PAGE_COW)
#define PAGE_READONLY	(PAGE_PRESENT | PAGE_USER | PAGE_ACCESSED)
#define PAGE_TABLE	(PAGE_PRESENT | PAGE_RW | PAGE_USER | PAGE_ACCESSED)

#define GFP_BUFFER	0x00
#define GFP_ATOMIC	0x01
#define GFP_USER	0x02
#define GFP_KERNEL	0x03
#define GFP_NOBUFFER	0x04
#define GFP_NFS		0x05

/* Flag - indicates that the buffer will be suitable for DMA.  Ignored on some
   platforms, used as appropriate on others */

#define GFP_DMA		0x80

/*
 * vm_ops not present page codes for shared memory.
 *
 * Will go away eventually..
 */
#define SHM_SWP_TYPE 0x41
extern void shm_no_page (ulong *);

/*
 * swap cache stuff (in swap.c)
 */
#define SWAP_CACHE_INFO

extern unsigned long * swap_cache;

#ifdef SWAP_CACHE_INFO
extern unsigned long swap_cache_add_total;
extern unsigned long swap_cache_add_success;
extern unsigned long swap_cache_del_total;
extern unsigned long swap_cache_del_success;
extern unsigned long swap_cache_find_total;
extern unsigned long swap_cache_find_success;
#endif

extern inline unsigned long in_swap_cache(unsigned long addr)
{
	return swap_cache[addr >> PAGE_SHIFT]; 
}

extern inline long find_in_swap_cache (unsigned long addr)
{
	unsigned long entry;

#ifdef SWAP_CACHE_INFO
	swap_cache_find_total++;
#endif
	entry = (unsigned long) xchg_ptr(swap_cache + (addr >> PAGE_SHIFT), NULL);
#ifdef SWAP_CACHE_INFO
	if (entry)
		swap_cache_find_success++;
#endif	
	return entry;
}

extern inline int delete_from_swap_cache(unsigned long addr)
{
	unsigned long entry;
	
#ifdef SWAP_CACHE_INFO
	swap_cache_del_total++;
#endif	
	entry = (unsigned long) xchg_ptr(swap_cache + (addr >> PAGE_SHIFT), NULL);
	if (entry)  {
#ifdef SWAP_CACHE_INFO
		swap_cache_del_success++;
#endif
		swap_free(entry);
		return 1;
	}
	return 0;
}

#endif /* __KERNEL__ */

#endif
