#ifndef __ALPHA_SYSTEM_H
#define __ALPHA_SYSTEM_H

/*
 * System defines.. Note that this is included both from .c and .S
 * files, so it does only defines, not any C code.
 */

/*
 * We leave one page for the initial stack page, and one page for
 * the initial process structure. Also, the console eats 3 MB for
 * the initial bootloader (one of which we can reclaim later).
 * So the initial load address is 0xfffffc0000304000UL
 */
#define INIT_PCB	0xfffffc0000300000
#define INIT_STACK	0xfffffc0000302000
#define START_ADDR	0xfffffc0000304000
#define START_SIZE	(32*1024)

/*
 * Common PAL-code
 */
#define PAL_halt	  0
#define PAL_cflush	  1
#define PAL_draina	  2
#define PAL_cobratt	  9
#define PAL_bpt		128
#define PAL_bugchk	129
#define PAL_chmk	131
#define PAL_callsys	131
#define PAL_imb		134
#define PAL_rduniq	158
#define PAL_wruniq	159
#define PAL_gentrap	170
#define PAL_nphalt	190

/*
 * VMS specific PAL-code
 */
#define PAL_swppal	10
#define PAL_mfpr_vptb	41

/*
 * OSF specific PAL-code
 */
#define PAL_mtpr_mces	17
#define PAL_wrfen	43
#define PAL_wrvptptr	45
#define PAL_jtopal	46
#define PAL_swpctx	48
#define PAL_wrval	49
#define PAL_rdval	50
#define PAL_tbi		51
#define PAL_wrent	52
#define PAL_swpipl	53
#define PAL_rdps	54
#define PAL_wrkgp	55
#define PAL_wrusp	56
#define PAL_wrperfmon	57
#define PAL_rdusp	58
#define PAL_whami	60
#define PAL_rtsys	61
#define PAL_rti		63

#ifndef __ASSEMBLY__

extern void wrent(void *, unsigned long);
extern void wrkgp(unsigned long);

#define halt() __asm__ __volatile__(".long 0");
#define move_to_user_mode() halt()
#define switch_to(x) halt()

#ifndef mb
#define mb() __asm__ __volatile__("mb": : :"memory")
#endif

#define swpipl(__new_ipl) \
({ unsigned long __old_ipl; \
__asm__ __volatile__( \
	"bis %1,%1,$16\n\t" \
	".long 53\n\t" \
	"bis $0,$0,%0" \
	: "=r" (__old_ipl) \
	: "r" (__new_ipl) \
	: "$0", "$1", "$16", "$22", "$23", "$24", "$25"); \
__old_ipl; })

#define cli()			swpipl(7)
#define sti()			swpipl(0)
#define save_flags(flags)	do { flags = swpipl(7); } while (0)
#define restore_flags(flags)	swpipl(flags)

extern inline unsigned long xchg_u32(int * m, unsigned long val)
{
	unsigned long dummy, dummy2;

	__asm__ __volatile__(
		"\n1:\t"
		"ldl_l %0,%1\n\t"
		"bis %2,%2,%3\n\t"
		"stl_c %3,%1\n\t"
		"beq %3,1b\n"
		: "=r" (val), "=m" (*m), "=r" (dummy), "=r" (dummy2)
		: "1" (*m), "2" (val));
	return val;
}

extern inline unsigned long xchg_u64(long * m, unsigned long val)
{
	unsigned long dummy, dummy2;

	__asm__ __volatile__(
		"\n1:\t"
		"ldq_l %0,%1\n\t"
		"bis %2,%2,%3\n\t"
		"stq_c %3,%1\n\t"
		"beq %3,1b\n"
		: "=r" (val), "=m" (*m), "=r" (dummy), "=r" (dummy2)
		: "1" (*m), "2" (val));
	return val;
}

extern inline void * xchg_ptr(void *m, void *val)
{
	return (void *) xchg_u64((long *) m, (unsigned long) val);
}

#endif /* __ASSEMBLY__ */

#endif
