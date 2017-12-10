/* e2100.c: A Cabletron E2100 series ethernet driver for linux. */
/*
	Written 1993-1994 by Donald Becker.

	Copyright 1994 by Donald Becker.
	Copyright 1993 United States Government as represented by the
	Director, National Security Agency.  This software may be used and
	distributed according to the terms of the GNU Public License,
	incorporated herein by reference.

	This is a driver for the Cabletron E2100 series ethercards.

	The Author may be reached as becker@cesdis.gsfc.nasa.gov, or
	C/O Code 930.5, Goddard Space Flight Center, Greenbelt MD 20771

	The E2100 series ethercard is a fairly generic shared memory 8390
	implementation.  The only unusual aspect is the way the shared memory
	registers are set: first you do an inb() in what is normally the
	station address region, and the low three bits of next outb() *address*
	is used	as the write value for that register.  Either someone wasn't
	too used to dem bit en bites, or they were trying to obfuscate the
	programming	interface.

	There is an additional complication when setting the window on the packet
	buffer.  You must first do a read into the packet buffer region with the
	low 8 address bits the address setting the page for the start of the packet
	buffer window, and then do the above operation.  See mem_on() for details.

	One bug on the chip is that even a hard reset won't disable the memory
	window, usually resulting in a hung machine if mem_off() isn't called.
	If this happens, you must power down the machine for about 30 seconds.
*/

static char *version =
	"e2100.c:v1.01 7/21/94 Donald Becker (becker@cesdis.gsfc.nasa.gov)\n";

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <asm/io.h>
#include <asm/system.h>
#include <linux/ioport.h>

#include <linux/netdevice.h>
#include "8390.h"

static int e21_probe_list[] = {0x300, 0x280, 0x380, 0x220, 0};

/* Offsets from the base_addr.
   Read from the ASIC register, and the low three bits of the next outb()
   address is used to set the corresponding register. */
#define E21_NIC_OFFSET  0		/* Offset to the 8390 NIC. */
#define E21_ASIC		0x10
#define E21_MEM_ENABLE	0x10
#define  E21_MEM_ON		0x05	/* Enable memory in 16 bit mode. */
#define  E21_MEM_ON_8	0x07	/* Enable memory in  8 bit mode. */
#define E21_MEM_BASE	0x11	
#define E21_IRQ_LOW		0x12	/* The low three bits of the IRQ number. */
#define E21_IRQ_HIGH	0x14	/* The high IRQ bit and media select ...  */
#define E21_MEDIA		0x14	/* (alias). */
#define  E21_ALT_IFPORT 0x02	/* Set to use the other (BNC,AUI) port. */
#define  E21_BIG_MEM	0x04	/* Use a bigger (64K) buffer (we don't) */
#define E21_SAPROM		0x10	/* Offset to station address data. */
#define E21_IO_EXTENT	 0x20

extern inline void mem_on(short port, volatile char *mem_base,
						  unsigned char start_page )
{
	/* This is a little weird: set the shared memory window by doing a
	   read.  The low address bits specify the starting page. */
	mem_base[start_page];
	inb(port + E21_MEM_ENABLE);
	outb(E21_MEM_ON, port + E21_MEM_ENABLE + E21_MEM_ON);
}

extern inline void mem_off(short port)
{
	inb(port + E21_MEM_ENABLE);
	outb(0x00, port + E21_MEM_ENABLE);
}

/* In other drivers I put the TX pages first, but the E2100 window circuitry
   is designed to have a 4K Tx region last. The windowing circuitry wraps the
   window at 0x2fff->0x0000 so that the packets at e.g. 0x2f00 in the RX ring
   appear contiguously in the window. */
#define E21_RX_START_PG		0x00	/* First page of RX buffer */
#define E21_RX_STOP_PG		0x30	/* Last page +1 of RX ring */
#define E21_BIG_RX_STOP_PG	0xF0	/* Last page +1 of RX ring */
#define E21_TX_START_PG		E21_RX_STOP_PG	/* First page of TX buffer */

int e2100_probe(struct device *dev);
int e21_probe1(struct device *dev, int ioaddr);

static int e21_open(struct device *dev);
static void e21_reset_8390(struct device *dev);
static int e21_block_input(struct device *dev, int count,
						   char *buf, int ring_offset);
static void e21_block_output(struct device *dev, int count,
							 const unsigned char *buf, const start_page);
static int e21_close(struct device *dev);


/*  Probe for the E2100 series ethercards.  These cards have an 8390 at the
	base address and the station address at both offset 0x10 and 0x18.  I read
	the station address from offset 0x18 to avoid the dataport of NE2000
	ethercards, and look for Ctron's unique ID (first three octets of the
	station address).
 */

int e2100_probe(struct device *dev)
{
	int *port;
	int base_addr = dev->base_addr;

	if (base_addr > 0x1ff)		/* Check a single specified location. */
		return e21_probe1(dev, base_addr);
	else if (base_addr != 0)	/* Don't probe at all. */
		return ENXIO;

	for (port = e21_probe_list; *port; port++) {
		if (check_region(*port, E21_IO_EXTENT))
			continue;
		if (e21_probe1(dev, *port) == 0)
			return 0;
	}

	return ENODEV;
}

int e21_probe1(struct device *dev, int ioaddr)
{
	int i, status;
	unsigned char *station_addr = dev->dev_addr;

	/* First check the station address for the Ctron prefix. */
	if (inb(ioaddr + E21_SAPROM + 0) != 0x00
		|| inb(ioaddr + E21_SAPROM + 1) != 0x00
		|| inb(ioaddr + E21_SAPROM + 2) != 0x1d)
		return ENODEV;

	/* Verify by making certain that there is a 8390 at there. */
	outb(E8390_NODMA + E8390_STOP, ioaddr);
	SLOW_DOWN_IO;
	status = inb(ioaddr);
	if (status != 0x21 && status != 0x23)
		return ENODEV;

	/* Grab the region so we can find a different board if IRQ select fails. */
	snarf_region(ioaddr, E21_IO_EXTENT);

	/* Read the station address PROM.  */
	for (i = 0; i < 6; i++)
		station_addr[i] = inb(ioaddr + E21_SAPROM + i);

	inb(ioaddr + E21_MEDIA); 		/* Point to media selection. */
	outb(0, ioaddr + E21_ASIC); 	/* and disable the secondary interface. */

	printk("%s: E21** at %#3x,", dev->name, ioaddr);
	for (i = 0; i < 6; i++)
		printk(" %02X", station_addr[i]);

	if (dev->irq < 2) {
		int irqlist[] = {15,11,10,12,5,9,3,4}, i;
		for (i = 0; i < 8; i++)
			if (request_irq (irqlist[i], NULL, 0, "bogus") != -EBUSY) {
				dev->irq = irqlist[i];
				break;
			}
		if (i >= 8) {
			printk(" unable to get IRQ %d.\n", dev->irq);
			return EAGAIN;
		}
	} else if (dev->irq == 2)	/* Fixup luser bogosity: IRQ2 is really IRQ9 */
		dev->irq = 9;

	/* The 8390 is at the base address. */
	dev->base_addr = ioaddr;

	ethdev_init(dev);

	ei_status.name = "E2100";
	ei_status.word16 = 1;
	ei_status.tx_start_page = E21_TX_START_PG;
	ei_status.rx_start_page = E21_RX_START_PG;
	ei_status.stop_page = E21_RX_STOP_PG;
    ei_status.saved_irq = dev->irq;

	/* Check the media port used.  The port can be passed in on the
	   low mem_end bits. */
	if (dev->mem_end & 15)
		dev->if_port = dev->mem_end & 7;
	else {
		dev->if_port = 0;
		inb(ioaddr + E21_MEDIA); 	/* Turn automatic media detection on. */
		for(i = 0; i < 6; i++)
			if (station_addr[i] != inb(ioaddr + E21_SAPROM + 8 + i)) {
				dev->if_port = 1;
				break;
			}
	}

	/* Never map in the E21 shared memory unless you are actively using it.
	   Also, the shared memory has effective only one setting -- spread all
	   over the 128K region! */
	if (dev->mem_start == 0)
		dev->mem_start = 0xd0000;
	
#ifdef notdef
	/* These values are unused.  The E2100 has a 2K window into the packet
	   buffer.  The window can be set to start on any page boundary. */
	dev->rmem_start = dev->mem_start + TX_PAGES*256;
	dev->mem_end = dev->rmem_end = dev->mem_start + 2*1024;
#endif

	printk(", IRQ %d, %s media, memory @ %#lx.\n", dev->irq,
		   dev->if_port ? "secondary" : "primary", dev->mem_start);

	if (ei_debug > 0)
		printk(version);

	ei_status.reset_8390 = &e21_reset_8390;
	ei_status.block_input = &e21_block_input;
	ei_status.block_output = &e21_block_output;
	dev->open = &e21_open;
	dev->stop = &e21_close;
	NS8390_init(dev, 0);

	return 0;
}

static int
e21_open(struct device *dev)
{
	short ioaddr = dev->base_addr;

	if (request_irq(dev->irq, ei_interrupt, 0, "e2100")) {
		return EBUSY;
	}
	irq2dev_map[dev->irq] = dev;

	/* Set the interrupt line and memory base on the hardware. */
	inb(ioaddr + E21_IRQ_LOW);
	outb(0, ioaddr + E21_ASIC + (dev->irq & 7));
	inb(ioaddr + E21_IRQ_HIGH); 			/* High IRQ bit, and if_port. */
	outb(0, ioaddr + E21_ASIC + (dev->irq > 7 ? 1:0)
		   + (dev->if_port ? E21_ALT_IFPORT : 0));
	inb(ioaddr + E21_MEM_BASE);
	outb(0, ioaddr + E21_ASIC + ((dev->mem_start >> 17) & 7));

	return ei_open(dev);
}

static void
e21_reset_8390(struct device *dev)
{
	short ioaddr = dev->base_addr;

	outb(0x01, ioaddr);
	if (ei_debug > 1) printk("resetting the E2180x3 t=%ld...", jiffies);
	ei_status.txing = 0;

	/* Set up the ASIC registers, just in case something changed them. */

	if (ei_debug > 1) printk("reset done\n");
	return;
}

/*  Block input and output are easy on shared memory ethercards.
	The E21xx makes block_input() especially easy by wrapping the top
	ring buffer to the bottom automatically. */
static int
e21_block_input(struct device *dev, int count, char *buf, int ring_offset)
{
	short ioaddr = dev->base_addr;
	char *shared_mem = (char *)dev->mem_start;
	int start_page = (ring_offset>>8);

	mem_on(ioaddr, shared_mem, start_page);

	/* We'll always get a 4 byte header read first. */
	if (count == 4)
		((int*)buf)[0] = ((int*)shared_mem)[0];
	else
		memcpy(buf, shared_mem + (ring_offset & 0xff), count);

	/* Turn off memory access: we would need to reprogram the window anyway. */
	mem_off(ioaddr);

	return 0;
}

static void
e21_block_output(struct device *dev, int count, const unsigned char *buf,
				 int start_page)
{
	short ioaddr = dev->base_addr;
	volatile char *shared_mem = (char *)dev->mem_start;

	/* Set the shared memory window start by doing a read, with the low address
	   bits specifying the starting page. */
	*(shared_mem + start_page);
	mem_on(ioaddr, shared_mem, start_page);

	memcpy((char*)shared_mem, buf, count);
	mem_off(ioaddr);
}

static int
e21_close(struct device *dev)
{
	short ioaddr = dev->base_addr;

	if (ei_debug > 1)
		printk("%s: Shutting down ethercard.\n", dev->name);

    free_irq(dev->irq);
    dev->irq = ei_status.saved_irq;

	/* Shut off the interrupt line and secondary interface. */
	inb(ioaddr + E21_IRQ_LOW);
	outb(0, ioaddr + E21_ASIC);
	inb(ioaddr + E21_IRQ_HIGH); 			/* High IRQ bit, and if_port. */
	outb(0, ioaddr + E21_ASIC);

    irq2dev_map[dev->irq] = NULL;

	NS8390_init(dev, 0);

	/* Double-check that the memory has been turned off, because really
	   really bad things happen if it isn't. */
	mem_off(ioaddr);

	return 0;
}

#ifdef HAVE_DEVLIST
struct netdev_entry e21_drv =
{"e21", e21_probe1, E21_IO_EXTENT, e21_probe_list};
#endif


/*
 * Local variables:
 *  compile-command: "gcc -D__KERNEL__ -I/usr/src/linux/net/inet -Wall -Wstrict-prototypes -O6 -m486 -c e2100.c"
 *  version-control: t
 *  tab-width: 4
 *  kept-new-versions: 5
 * End:
 */
