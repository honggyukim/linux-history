#ifndef __ALPHA_IO_H
#define __ALPHA_IO_H

/*
 * Defines for the AlphaPC EISA IO and memory address space.
 */

#ifndef mb
#define mb() __asm__ __volatile__("mb": : :"memory")
#endif

/*
 * NOTE! Currently it never uses the HAE register, so these work only
 * for the low 25 bits of EISA addressing.  That covers all of the IO
 * address space (16 bits), and most of the "normal" EISA memory space.
 * I'll fix it eventually, but I'll need to come up with a clean way
 * to handle races with interrupt services wanting to change HAE...
 */

/*
 * NOTE 2! The memory operations do not set any memory barriers, as it's
 * not needed for cases like a frame buffer that is essentially memory-like.
 * You need to do them by hand if the operations depend on ordering.
 *
 * Similarly, the port IO operations do a "mb" only after a write operation:
 * if an mb is needed before (as in the case of doing memory mapped IO
 * first, and then a port IO operation to the same device), it needs to be
 * done by hand.
 *
 * After the above has bitten me 100 times, I'll give up and just do the
 * mb all the time, but right now I'm hoping this will work out.  Avoiding
 * mb's may potentially be a noticeable speed improvement, but I can't
 * honestly say I've tested it.
 *
 * Handling interrupts that need to do mb's to synchronize to non-interrupts
 * is another fun race area.  Don't do it (because if you do, I'll have to
 * do *everything* with interrupts disabled, ugh).
 */

/*
 * Virtual -> physical identity mapping starts at this offset
 */
#define IDENT_ADDR	(0xfffffc0000000000UL)

/*
 * EISA Interrupt Acknowledge address
 */
#define EISA_INTA		(IDENT_ADDR + 0x100000000UL)

/*
 * FEPROM addresses
 */
#define EISA_FEPROM0		(IDENT_ADDR + 0x180000000UL)
#define EISA_FEPROM1		(IDENT_ADDR + 0x1A0000000UL)

/*
 * VL82C106 base address
 */
#define EISA_VL82C106		(IDENT_ADDR + 0x1C0000000UL)

/*
 * EISA "Host Address Extension" address (bits 25-31 of the EISA address)
 */
#define EISA_HAE		(IDENT_ADDR + 0x1D0000000UL)

/*
 * "SYSCTL" register address
 */
#define EISA_SYSCTL		(IDENT_ADDR + 0x1E0000000UL)

/*
 * "spare" register address
 */
#define EISA_SPARE		(IDENT_ADDR + 0x1F0000000UL)

/*
 * EISA memory address offset
 */
#define EISA_MEM		(IDENT_ADDR + 0x200000000UL)

/*
 * EISA IO address offset
 */
#define EISA_IO			(IDENT_ADDR + 0x300000000UL)

/*
 * IO functions
 *
 * The "local" functions are those that don't go out to the EISA bus,
 * but instead act on the VL82C106 chip directly.. This is mainly the
 * keyboard, RTC,  printer and first two serial lines..
 */
extern inline unsigned long inb_local(unsigned long addr)
{
	long result = *(volatile int *) ((addr << 9) + EISA_VL82C106);
	return 0xffUL & result;
}

extern inline void outb_local(unsigned char b, unsigned long addr)
{
	*(volatile unsigned int *) ((addr << 9) + EISA_VL82C106) = b;
	mb();
}

extern inline unsigned int inb(unsigned long addr)
{
	long result = *(volatile int *) ((addr << 7) + EISA_IO + 0x00);
	result >>= (addr & 3) * 8;
	return 0xffUL & result;
}

extern inline unsigned int inw(unsigned long addr)
{
	long result = *(volatile int *) ((addr << 7) + EISA_IO + 0x20);
	result >>= (addr & 3) * 8;
	return 0xffffUL & result;
}

extern inline unsigned int inl(unsigned long addr)
{
	return *(volatile unsigned int *) ((addr << 7) + EISA_IO + 0x60);
}

extern inline void outb(unsigned char b, unsigned long addr)
{
	*(volatile unsigned int *) ((addr << 7) + EISA_IO + 0x00) = b * 0x01010101;
	mb();
}

extern inline void outw(unsigned short b, unsigned long addr)
{
	*(volatile unsigned int *) ((addr << 7) + EISA_IO + 0x20) = b * 0x00010001;
	mb();
}

extern inline void outl(unsigned int b, unsigned long addr)
{
	*(volatile unsigned int *) ((addr << 7) + EISA_IO + 0x60) = b;
	mb();
}

/*
 * Memory functions
 */
extern inline unsigned long readb(unsigned long addr)
{
	long result = *(volatile int *) ((addr << 7) + EISA_MEM + 0x00);
	result >>= (addr & 3) * 8;
	return 0xffUL & result;
}

extern inline unsigned long readw(unsigned long addr)
{
	long result = *(volatile int *) ((addr << 7) + EISA_MEM + 0x20);
	result >>= (addr & 3) * 8;
	return 0xffffUL & result;
}

extern inline unsigned long readl(unsigned long addr)
{
	return *(volatile unsigned int *) ((addr << 7) + EISA_MEM + 0x60);
}

extern inline void writeb(unsigned short b, unsigned long addr)
{
	*(volatile unsigned int *) ((addr << 7) + EISA_MEM + 0x00) = b * 0x01010101;
}

extern inline void writew(unsigned short b, unsigned long addr)
{
	*(volatile unsigned int *) ((addr << 7) + EISA_MEM + 0x20) = b * 0x00010001;
}

extern inline void writel(unsigned int b, unsigned long addr)
{
	*(volatile unsigned int *) ((addr << 7) + EISA_MEM + 0x60) = b;
}

#define inb_p inb
#define outb_p outb

#endif
