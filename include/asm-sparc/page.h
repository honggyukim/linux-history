/* page.h:  Various defines and such for MMU operations on the Sparc for
            the Linux kernel.

   Copyright (C) 1994 David S. Miller (davem@caip.rutgers.edu)
*/

#ifndef _SPARC_PAGE_H
#define _SPARC_PAGE_H

#include <asm/asi.h>        /* for get/set segmap/pte routines */
#include <asm/contregs.h>   /* for switch_to_context */

/* The current va context is global and known, so all that is needed to
 * do an invalidate is flush the VAC.
 */

#define invalidate() flush_vac_context()  /* how conveeeiiiiinnnient :> */


#define PAGE_SHIFT   12             /* This is the virtual page... */
#define PGDIR_SHIFT  18             /* This is the virtual segment */
#define PAGE_SIZE    4096
#define PGDIR_SIZE   (1UL << PGDIR_SHIFT)

#ifdef __KERNEL__

#define BITS_PER_PTR      (8*sizeof(unsigned long))   /* better check this stuff */
#define PAGE_MASK         (~(PAGE_SIZE-1))
#define PGDIR_MASK        (~(PGDIR_SIZE-1))
#define PAGE_ALIGN(addr)  (((addr)+PAGE_SIZE-1)&PAGE_MASK)
#define PGDIR_ALIGN(addr) (((addr)+PGDIR_SIZE-1)&PGDIR_MASK)
#define PTR_MASK          (~(sizeof(void*)-1))


#define SIZEOF_PTR_LOG2   2

/* The rest is kind of funky because on the sparc, the offsets into the mmu 
 * entries are encoded in magic alternate address space tables. I will 
 * probably find some nifty inline assembly routines to do the equivalent. 
 * Much thought must go into this code.   (davem@caip.rutgers.edu)
 */

#define PAGE_DIR_OFFSET(base, address)   ((void *) 0)
#define PAGE_PTR(address)                ((void *) 0)
#define PTRS_PER_PAGE                    (64)  /* 64 pte's per phys_seg */

/* Bitfields within a Sparc sun4c PTE (page table entry). */

#define PTE_V     0x80000000   /* valid bit */
#define PTE_ACC   0x60000000   /* access bits */
#define PTE_W     0x40000000   /* writable bit */
#define PTE_P     0x20000000   /* privileged page */
#define PTE_NC    0x10000000   /* page is non-cacheable */
#define PTE_TYP   0x0c000000   /* page type field */
#define PTE_RMEM  0x00000000   /* type == on board real memory */
#define PTE_IO    0x04000000   /* type == i/o area */
#define PTE_VME16 0x08000000   /* type == 16-bit VME area */
#define PTE_VME32 0x0c000000   /* type == 32-bit VME area */
#define PTE_R     0x02000000   /* page has been referenced */
#define PTE_M     0x01000000   /* page has been modified */
#define PTE_RESV  0x00f80000   /* reserved bits */
#define PTE_PHYPG 0x0007ffff   /* phys pg number, sun4c only uses 16bits */

/* termed a 'page table' in the linux kernel, a segmap entry is obtained
 * with the following macro
 */

#ifndef __ASSEMBLY__ /* for head.S */
extern __inline__ unsigned long get_segmap(unsigned long addr)
{
  register unsigned long entry;

  __asm__ __volatile__("lduha [%1] 0x3, %0" : 
		       "=r" (entry) :
		       "r" (addr)); 

  return entry;
}

extern __inline__ void put_segmap(unsigned long* addr, unsigned long entry)
{

  __asm__ __volatile__("stha %1, [%0] 0x3" : : "r" (addr), "r" (entry));

  return;
}

extern __inline__ unsigned long get_pte(unsigned long addr)
{
  register unsigned long entry;

  __asm__ __volatile__("lda [%1] 0x4, %0" : 
		       "=r" (entry) :
		       "r" (addr));
  return entry;
}

extern __inline__ void put_pte(unsigned long addr, unsigned long entry)
{
  __asm__ __volatile__("sta %1, [%0] 0x4" : :
		       "r" (addr), 
		       "r" (entry));

  return;
}

extern __inline__ void switch_to_context(int context)
{
  __asm__ __volatile__("stba %0, [%1] 0x2" : :
		       "r" (context),
		       "r" (0x30000000));		       

  return;
}

extern __inline__ int get_context(void)
{
  register int ctx;

  __asm__ __volatile__("lduba [%1] 0x2, %0" :
		       "=r" (ctx) :
		       "r" (0x30000000));

  return ctx;
}

#endif /* !(__ASSEMBLY__) */

#endif /* __KERNEL__ */

#endif /* _SPARC_PAGE_H */
