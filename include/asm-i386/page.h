#ifndef _I386_PAGE_H
#define _I386_PAGE_H

#define invalidate() \
__asm__ __volatile__("movl %%cr3,%%eax\n\tmovl %%eax,%%cr3": : :"ax")

			/* PAGE_SHIFT determines the page size */
#define PAGE_SHIFT			12
#define PGDIR_SHIFT			22
#define PAGE_SIZE			(1UL << PAGE_SHIFT)
#define PGDIR_SIZE			(1UL << PGDIR_SHIFT)

#ifdef __KERNEL__

			/* number of bits that fit into a memory pointer */
#define BITS_PER_PTR			(8*sizeof(unsigned long))
			/* to mask away the intra-page address bits */
#define PAGE_MASK			(~(PAGE_SIZE-1))
			/* to mask away the intra-page address bits */
#define PGDIR_MASK			(~(PGDIR_SIZE-1))
			/* to align the pointer to the (next) page boundary */
#define PAGE_ALIGN(addr)		(((addr)+PAGE_SIZE-1)&PAGE_MASK)
			/* to align the pointer to a pointer address */
#define PTR_MASK			(~(sizeof(void*)-1))

					/* sizeof(void*)==1<<SIZEOF_PTR_LOG2 */
					/* 64-bit machines, beware!  SRB. */
#define SIZEOF_PTR_LOG2			2

/* to find an entry in a page-table-directory */
#define PAGE_DIR_OFFSET(tsk,address) \
((((unsigned long)(address)) >> 22) + (unsigned long *) (tsk)->tss.cr3)

/* to find an entry in a page-table */
#define PAGE_PTR(address)		\
((unsigned long)(address)>>(PAGE_SHIFT-SIZEOF_PTR_LOG2)&PTR_MASK&~PAGE_MASK)

/* the no. of pointers that fit on a page */
#define PTRS_PER_PAGE			(PAGE_SIZE/sizeof(void*))

/* to set the page-dir */
#define SET_PAGE_DIR(tsk,pgdir) \
do { \
	(tsk)->tss.cr3 = (unsigned long) (pgdir); \
	if ((tsk) == current) \
		__asm__ __volatile__("movl %0,%%cr3": :"a" ((tsk)->tss.cr3)); \
} while (0)

#endif /* __KERNEL__ */

#endif /* _I386_PAGE_H */
