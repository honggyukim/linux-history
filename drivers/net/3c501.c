/* 3c501.c: A 3Com 3c501 ethernet driver for linux. */
/*
    Written 1992,1993,1994  Donald Becker

    Copyright 1993 United States Government as represented by the
    Director, National Security Agency.  This software may be used and
    distributed according to the terms of the GNU Public License,
    incorporated herein by reference.

    This is a device driver for the 3Com Etherlink 3c501.
    Do not purchase this card, even as a joke.  It's performance is horrible,
    and it breaks in many ways.  

    The author may be reached as becker@CESDIS.gsfc.nasa.gov, or C/O
    Center of Excellence in Space Data and Information Sciences
       Code 930.5, Goddard Space Flight Center, Greenbelt MD 20771
       
    Fixed (again!) the missing interrupt locking on TX/RX shifting.
    		Alan Cox <Alan.Cox@linux.org>
    		
    Some notes on this thing if you have to hack it.  [Alan]
    
    1]	Some documentation is available from 3Com. Due to the boards age
    	standard responses when you ask for this will range from 'be serious'
    	to 'give it to a museum'. The documentation is incomplete and mostly
    	of historical interest anyway.
    	
    2]  The basic system is a single buffer which can be used to receive or
    	transmit a packet. A third command mode exists when you are setting
    	things up.
    	
    3]	If its transmitting its not receiving and vice versa. In fact the 
    	time to get the board back into useful state after an operation is
    	quite large.
    	
    4]	The driver works by keeping the board in receive mode waiting for a
    	packet to arrive. When one arrives it is copied out of the buffer
    	and delivered to the kernel. The card is reloaded and off we go.
    	
    5]	When transmitting dev->tbusy is set and the card is reset (from
    	receive mode) [possibly losing a packet just received] to command
    	mode. A packet is loaded and transmit mode triggered. The interrupt
    	handler runs different code for transmit interrupts and can handle
    	returning to receive mode or retransmissions (yes you have to help
    	out with those too).
    	
    Problems:
    	There are a wide variety of undocumented error returns from the card
    and you basically have to kick the board and pray if they turn up. Most 
    only occur under extreme load or if you do something the board doesn't
    like (eg touching a register at the wrong time).
    
    	The driver is less efficient than it could be. It switches through
    receive mode even if more transmits are queued. If this worries you buy
    a real ethernet card.
    
    	The combination of slow receive restart and no real multicast
    filter makes the board unusable with a kernel compiled for IP
    multicasting in a real multicast environment. Thats down to the board, 
    but even with no multicast programs running a multicast IP kernel is
    in group 224.0.0.1 and you will therefore be listening to all multicasts.
    One nv conference running over that ethernet and you can give up.
    
*/

static char *version =
    "3c501.c: 9/23/94 Donald Becker (becker@cesdis.gsfc.nasa.gov).\n";

/*
  Braindamage remaining:
  The 3c501 board.
  */

#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/ptrace.h>
#include <linux/fcntl.h>
#include <linux/ioport.h>
#include <linux/interrupt.h>
#include <linux/malloc.h>
#include <linux/string.h>
#include <linux/ioport.h>
#include <linux/errno.h>
#include <linux/config.h>

#include <asm/bitops.h>
#include <asm/io.h>

#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>

#ifdef MODULE
#include <linux/module.h>
#include <linux/version.h>
#endif

extern struct device *init_etherdev(struct device *dev, int sizeof_private,
				    unsigned long *mem_startp);

/* A zero-terminated list of I/O addresses to be probed.
   The 3c501 can be at many locations, but here are the popular ones. */
static unsigned int netcard_portlist[] =
   { 0x280, 0x300, 0};


/* Index to functions. */
int el1_probe(struct device *dev);
static int  el1_probe1(struct device *dev, int ioaddr);
static int  el_open(struct device *dev);
static int  el_start_xmit(struct sk_buff *skb, struct device *dev);
static void el_interrupt(int reg_ptr);
static void el_receive(struct device *dev);
static void el_reset(struct device *dev);
static int  el1_close(struct device *dev);
static struct enet_statistics *el1_get_stats(struct device *dev);
static void set_multicast_list(struct device *dev, int num_addrs, void *addrs);

#define EL1_IO_EXTENT	16

#ifndef EL_DEBUG
#define EL_DEBUG  2	/* use 0 for production, 1 for devel., >2 for debug */
#endif			/* Anything above 5 is wordy death! */
static int el_debug = EL_DEBUG;
 
/* Board-specific info in dev->priv. */
struct net_local {
    struct enet_statistics stats;
    int tx_pkt_start;		/* The length of the current Tx packet. */
    int collisions;		/* Tx collisions this packet */
};


#define RX_STATUS (ioaddr + 0x06)
#define RX_CMD	  RX_STATUS
#define TX_STATUS (ioaddr + 0x07)
#define TX_CMD	  TX_STATUS
#define GP_LOW 	  (ioaddr + 0x08)
#define GP_HIGH   (ioaddr + 0x09)
#define RX_BUF_CLR (ioaddr + 0x0A)
#define RX_LOW	  (ioaddr + 0x0A)
#define RX_HIGH   (ioaddr + 0x0B)
#define SAPROM	  (ioaddr + 0x0C)
#define AX_STATUS (ioaddr + 0x0E)
#define AX_CMD	  AX_STATUS
#define DATAPORT  (ioaddr + 0x0F)
#define TX_RDY 0x08		/* In TX_STATUS */

#define EL1_DATAPTR	0x08
#define EL1_RXPTR	0x0A
#define EL1_SAPROM	0x0C
#define EL1_DATAPORT 	0x0f

/* Writes to the ax command register. */
#define AX_OFF	0x00			/* Irq off, buffer access on */
#define AX_SYS  0x40			/* Load the buffer */
#define AX_XMIT 0x44			/* Transmit a packet */
#define AX_RX	0x48			/* Receive a packet */
#define AX_LOOP	0x0C			/* Loopback mode */
#define AX_RESET 0x80

/* Normal receive mode written to RX_STATUS.  We must intr on short packets
   to avoid bogus rx lockups. */
#define RX_NORM 0xA8		/* 0x68 == all addrs, 0xA8 only to me. */
#define RX_PROM 0x68		/* Senior Prom, uhmm promiscuous mode. */
#define RX_MULT 0xE8		/* Accept multicast packets. */
#define TX_NORM 0x0A	/* Interrupt on everything that might hang the chip */

/* TX_STATUS register. */
#define TX_COLLISION 0x02
#define TX_16COLLISIONS 0x04
#define TX_READY 0x08

#define RX_RUNT 0x08
#define RX_MISSED 0x01		/* Missed a packet due to 3c501 braindamage. */
#define RX_GOOD	0x30		/* Good packet 0x20, or simple overflow 0x10. */


/* The boilerplate probe code. */
#ifdef HAVE_DEVLIST
struct netdev_entry el1_drv =
{"3c501", el1_probe1, EL1_IO_EXTENT, netcard_portlist};
#else
int
el1_probe(struct device *dev)
{
    int i;
    int base_addr = dev ? dev->base_addr : 0;

    if (base_addr > 0x1ff)	/* Check a single specified location. */
	return el1_probe1(dev, base_addr);
    else if (base_addr != 0)	/* Don't probe at all. */
	return ENXIO;

    for (i = 0; netcard_portlist[i]; i++) {
	int ioaddr = netcard_portlist[i];
	if (check_region(ioaddr, EL1_IO_EXTENT))
	    continue;
	if (el1_probe1(dev, ioaddr) == 0)
	    return 0;
    }

    return ENODEV;
}
#endif

/* The actual probe. */ 
static int
el1_probe1(struct device *dev, int ioaddr)
{
    char *mname;		/* Vendor name */
    unsigned char station_addr[6];
    int autoirq = 0;
    int i;

    /* Read the station address PROM data from the special port.  */
    for (i = 0; i < 6; i++) {
	outw(i, ioaddr + EL1_DATAPTR);
	station_addr[i] = inb(ioaddr + EL1_SAPROM);
    }
    /* Check the first three octets of the S.A. for 3Com's prefix, or
       for the Sager NP943 prefix. */ 
    if (station_addr[0] == 0x02  &&  station_addr[1] == 0x60
	&& station_addr[2] == 0x8c) {
	mname = "3c501";
    } else if (station_addr[0] == 0x00  &&  station_addr[1] == 0x80
	&& station_addr[2] == 0xC8) {
	mname = "NP943";
    } else
	return ENODEV;

    /* Grab the region so we can find the another board if autoIRQ fails. */
    request_region(ioaddr, EL1_IO_EXTENT,"3c501");

    if (dev == NULL)
	dev = init_etherdev(0, sizeof(struct net_local), 0);

    /* We auto-IRQ by shutting off the interrupt line and letting it float
       high. */
    if (dev->irq < 2) {

	autoirq_setup(2);

	inb(RX_STATUS);		/* Clear pending interrupts. */
	inb(TX_STATUS);
	outb(AX_LOOP + 1, AX_CMD);

	outb(0x00, AX_CMD);

	autoirq = autoirq_report(1);

	if (autoirq == 0) {
	    printk("%s probe at %#x failed to detect IRQ line.\n",
		   mname, ioaddr);
	    return EAGAIN;
	}
    }

    outb(AX_RESET+AX_LOOP, AX_CMD);			/* Loopback mode. */

    dev->base_addr = ioaddr;
    memcpy(dev->dev_addr, station_addr, ETH_ALEN);
    if (dev->mem_start & 0xf)
	el_debug = dev->mem_start & 0x7;
    if (autoirq)
	dev->irq = autoirq;

    printk("%s: %s EtherLink at %#x, using %sIRQ %d.\n",
	   dev->name, mname, dev->base_addr,
	   autoirq ? "auto":"assigned ", dev->irq);
	   
#ifdef CONFIG_IP_MULTICAST
    printk("WARNING: Use of the 3c501 in a multicast kernel is NOT recommended.\n");
#endif    

    if (el_debug)
	printk("%s", version);

    /* Initialize the device structure. */
    if (dev->priv == NULL)
	dev->priv = kmalloc(sizeof(struct net_local), GFP_KERNEL);
    memset(dev->priv, 0, sizeof(struct net_local));

    /* The EL1-specific entries in the device structure. */
    dev->open = &el_open;
    dev->hard_start_xmit = &el_start_xmit;
    dev->stop = &el1_close;
    dev->get_stats = &el1_get_stats;
    dev->set_multicast_list = &set_multicast_list;
    /* Setup the generic properties */
    ether_setup(dev);

    return 0;
}

/* Open/initialize the board. */
static int
el_open(struct device *dev)
{
    int ioaddr = dev->base_addr;

    if (el_debug > 2)
	printk("%s: Doing el_open()...", dev->name);

    if (request_irq(dev->irq, &el_interrupt, 0, "3c501")) {
	return -EAGAIN;
    }
    irq2dev_map[dev->irq] = dev;

    el_reset(dev);

    dev->start = 1;

    outb(AX_RX, AX_CMD);	/* Aux control, irq and receive enabled */
#ifdef MODULE
    MOD_INC_USE_COUNT;
#endif       
    return 0;
}

static int
el_start_xmit(struct sk_buff *skb, struct device *dev)
{
    struct net_local *lp = (struct net_local *)dev->priv;
    int ioaddr = dev->base_addr;
    unsigned long flags;

    if (dev->tbusy) {
	if (jiffies - dev->trans_start < 20) {
	    if (el_debug > 2)
		printk(" transmitter busy, deferred.\n");
	    return 1;
	}
	if (el_debug)
	    printk ("%s: transmit timed out, txsr %#2x axsr=%02x rxsr=%02x.\n",
		    dev->name, inb(TX_STATUS), inb(AX_STATUS), inb(RX_STATUS));
	lp->stats.tx_errors++;
	outb(TX_NORM, TX_CMD);
	outb(RX_NORM, RX_CMD);
	outb(AX_OFF, AX_CMD);	/* Just trigger a false interrupt. */
	outb(AX_RX, AX_CMD);	/* Aux control, irq and receive enabled */
	dev->tbusy = 0;
	dev->trans_start = jiffies;
    }

    if (skb == NULL) {
	dev_tint(dev);
	return 0;
    }

    save_flags(flags);
    /* Avoid incoming interrupts between us flipping tbusy and flipping
       mode as the driver assumes tbusy is a faithful indicator of card
       state */
    cli();
    /* Avoid timer-based retransmission conflicts. */
    if (set_bit(0, (void*)&dev->tbusy) != 0)
    {
    	restore_flags(flags);
	printk("%s: Transmitter access conflict.\n", dev->name);
    }
    else {
	int gp_start = 0x800 - (ETH_ZLEN < skb->len ? skb->len : ETH_ZLEN);
	unsigned char *buf = skb->data;

	lp->tx_pkt_start = gp_start;
    	lp->collisions = 0;

	/*
	 *	Command mode with status cleared should [in theory]
	 *	mean no more interrupts can be pending on the card.
	 */
	outb(AX_SYS, AX_CMD);
	inb(RX_STATUS);
	inb(TX_STATUS);
	/* 
	 *	Turn interrupts back on while we spend a pleasant afternoon
	 *	loading bytes into the board 
	 */
	restore_flags(flags);
	outw(0x00, RX_BUF_CLR);		/* Set rx packet area to 0. */
	outw(gp_start, GP_LOW);		/* aim - packet will be loaded into buffer start */
	outsb(DATAPORT,buf,skb->len);	/* load buffer (usual thing each byte increments the pointer) */
	outw(gp_start, GP_LOW);		/* the board reuses the same register */
	outb(AX_XMIT, AX_CMD);		/* fire ... Trigger xmit.  */
	dev->trans_start = jiffies;
    }

    if (el_debug > 2)
	printk(" queued xmit.\n");
    dev_kfree_skb (skb, FREE_WRITE);
    return 0;
}


/* The typical workload of the driver:
   Handle the ether interface interrupts. */
static void
el_interrupt(int reg_ptr)
{
    int irq = -(((struct pt_regs *)reg_ptr)->orig_eax+2);
    struct device *dev = (struct device *)(irq2dev_map[irq]);
    struct net_local *lp;
    int ioaddr;
    int axsr;			/* Aux. status reg. */

    if (dev == NULL  ||  dev->irq != irq) {
	printk ("3c501 driver: irq %d for unknown device.\n", irq);
	return;
    }

    ioaddr = dev->base_addr;
    lp = (struct net_local *)dev->priv;
    axsr = inb(AX_STATUS);

    if (el_debug > 3)
      printk("%s: el_interrupt() aux=%#02x", dev->name, axsr);
    if (dev->interrupt)
	printk("%s: Reentering the interrupt driver!\n", dev->name);
    dev->interrupt = 1;

    if (dev->tbusy) {
    
    	/*
    	 *	Board in transmit mode.
    	 */
    	 
	int txsr = inb(TX_STATUS);

	if (el_debug > 6)
	    printk(" txsr=%02x gp=%04x rp=%04x", txsr, inw(GP_LOW),
		   inw(RX_LOW));

	if ((axsr & 0x80) && (txsr & TX_READY) == 0) {
	/*
	 *	FIXME: is there a logic to whether to keep on trying or
	 *	reset immediately ?
	 */
	    printk("%s: Unusual interrupt during Tx, txsr=%02x axsr=%02x"
		   " gp=%03x rp=%03x.\n", dev->name, txsr, axsr,
		   inw(ioaddr + EL1_DATAPTR), inw(ioaddr + EL1_RXPTR));
	    dev->tbusy = 0;
	    mark_bh(NET_BH);
	} else if (txsr & TX_16COLLISIONS) {
	/*
	 *	Timed out
	 */
	    if (el_debug)
		printk("%s: Transmit failed 16 times, ethernet jammed?\n",
		       dev->name);
	    outb(AX_SYS, AX_CMD);
	    lp->stats.tx_aborted_errors++;
	} else if (txsr & TX_COLLISION) {	/* Retrigger xmit. */
	    if (el_debug > 6)
		printk(" retransmitting after a collision.\n");
	/*
	 *	Poor little chip can't reset its own start pointer
	 */
	    outb(AX_SYS, AX_CMD);
	    outw(lp->tx_pkt_start, GP_LOW);
	    outb(AX_XMIT, AX_CMD);
	    lp->stats.collisions++;
	    dev->interrupt = 0;
	    return;
	} else {
	/*
	 *	It worked.. we will now fall through and receive
	 */
	    lp->stats.tx_packets++;
	    if (el_debug > 6)
		printk(" Tx succeeded %s\n",
		       (txsr & TX_RDY) ? "." : "but tx is busy!");
	/*
	 *	This is safe the interrupt is atomic WRT itself.
	 */
	    dev->tbusy = 0;
	    mark_bh(NET_BH);	/* In case more to transmit */
	}
    } else {
    
    	/*
    	 *	In receive mode.
    	 */
    	 
	int rxsr = inb(RX_STATUS);
	if (el_debug > 5)
	    printk(" rxsr=%02x txsr=%02x rp=%04x", rxsr, inb(TX_STATUS),
		   inw(RX_LOW));

	/*
	 *	Just reading rx_status fixes most errors. 
	 */
	if (rxsr & RX_MISSED)
	    lp->stats.rx_missed_errors++;
	if (rxsr & RX_RUNT) {	/* Handled to avoid board lock-up. */
	    lp->stats.rx_length_errors++;
	    if (el_debug > 5) printk(" runt.\n");
	} else if (rxsr & RX_GOOD) {
	/*
	 *	Receive worked.
	 */
	    el_receive(dev);
	} else {			/* Nothing?  Something is broken! */
	    if (el_debug > 2)
		printk("%s: No packet seen, rxsr=%02x **resetting 3c501***\n",
		       dev->name, rxsr);
	    el_reset(dev);
	}
	if (el_debug > 3)
	    printk(".\n");
    }

    /*
     *	Move into receive mode 
     */
    outb(AX_RX, AX_CMD);
    outw(0x00, RX_BUF_CLR);
    inb(RX_STATUS);		/* Be certain that interrupts are cleared. */
    inb(TX_STATUS);
    dev->interrupt = 0;
    return;
}


/* We have a good packet. Well, not really "good", just mostly not broken.
   We must check everything to see if it is good. */
static void
el_receive(struct device *dev)
{
    struct net_local *lp = (struct net_local *)dev->priv;
    int ioaddr = dev->base_addr;
    int pkt_len;
    struct sk_buff *skb;

    pkt_len = inw(RX_LOW);

    if (el_debug > 4)
      printk(" el_receive %d.\n", pkt_len);

    if ((pkt_len < 60)  ||  (pkt_len > 1536)) {
	if (el_debug)
	  printk("%s: bogus packet, length=%d\n", dev->name, pkt_len);
	lp->stats.rx_over_errors++;
	return;
    }
    
    /*
     *	Command mode so we can empty the buffer
     */
     
    outb(AX_SYS, AX_CMD);

    skb = alloc_skb(pkt_len, GFP_ATOMIC);
    /*
     *	Start of frame
     */
    outw(0x00, GP_LOW);
    if (skb == NULL) {
	printk("%s: Memory squeeze, dropping packet.\n", dev->name);
	lp->stats.rx_dropped++;
	return;
    } else {
	skb->len = pkt_len;
	skb->dev = dev;

	/*
	 *	The read increments through the bytes. The interrupt
	 *	handler will fix the pointer when it returns to 
	 *	receive mode.
	 */
	 
	insb(DATAPORT, skb->data, pkt_len);

	netif_rx(skb);
	lp->stats.rx_packets++;
    }
    return;
}

static void 
el_reset(struct device *dev)
{
    int ioaddr = dev->base_addr;

    if (el_debug> 2)
	printk("3c501 reset...");
    outb(AX_RESET, AX_CMD);	/* Reset the chip */
    outb(AX_LOOP, AX_CMD);	/* Aux control, irq and loopback enabled */
    {
	int i;
	for (i = 0; i < 6; i++)	/* Set the station address. */
	    outb(dev->dev_addr[i], ioaddr + i);
    }
    
    outw(0, RX_BUF_CLR);		/* Set rx packet area to 0. */
    cli();			/* Avoid glitch on writes to CMD regs */
    outb(TX_NORM, TX_CMD);		/* tx irq on done, collision */
    outb(RX_NORM, RX_CMD);	/* Set Rx commands. */
    inb(RX_STATUS);		/* Clear status. */
    inb(TX_STATUS);
    dev->interrupt = 0;
    dev->tbusy = 0;
    sti();
}

static int
el1_close(struct device *dev)
{
    int ioaddr = dev->base_addr;

    if (el_debug > 2)
	printk("%s: Shutting down ethercard at %#x.\n", dev->name, ioaddr);

    dev->tbusy = 1;
    dev->start = 0;

    /* Free and disable the IRQ. */
    free_irq(dev->irq);
    outb(AX_RESET, AX_CMD);	/* Reset the chip */
    irq2dev_map[dev->irq] = 0;

#ifdef MODULE
    MOD_DEC_USE_COUNT;
#endif    
    return 0;
}

static struct enet_statistics *
el1_get_stats(struct device *dev)
{
    struct net_local *lp = (struct net_local *)dev->priv;
    return &lp->stats;
}

/* Set or clear the multicast filter for this adaptor.
   num_addrs == -1	Promiscuous mode, receive all packets
   num_addrs == 0	Normal mode, clear multicast list
   num_addrs > 0	Multicast mode, receive normal and MC packets, and do
			best-effort filtering.
 */
static void
set_multicast_list(struct device *dev, int num_addrs, void *addrs)
{
    int ioaddr = dev->base_addr;

    if (num_addrs > 0) {
	outb(RX_MULT, RX_CMD);
	inb(RX_STATUS);		/* Clear status. */
    } else if (num_addrs < 0) {
	outb(RX_PROM, RX_CMD);
	inb(RX_STATUS);
    } else {
	outb(RX_NORM, RX_CMD);
	inb(RX_STATUS);
    }
}
#ifdef MODULE
char kernel_version[] = UTS_RELEASE;
static struct device dev_3c501 = {
	"        " /*"3c501"*/, 
		0, 0, 0, 0,
	 	0x280, 5,
	 	0, 0, 0, NULL, el1_probe };

int io=0x280;
int irq=5;
	
int
init_module(void)
{
	dev_3c501.irq=irq;
	dev_3c501.base_addr=io;
	if (register_netdev(&dev_3c501) != 0)
		return -EIO;
	return 0;
}

void
cleanup_module(void)
{
	if (MOD_IN_USE)
		printk("3c501: device busy, remove delayed\n");
	else
	{
		unregister_netdev(&dev_3c501);
	}
}
#endif /* MODULE */

/*
 * Local variables:
 *  compile-command: "gcc -D__KERNEL__ -Wall -Wstrict-prototypes -O6 -fomit-frame-pointer  -m486 -c -o 3c501.o 3c501.c"
 *  kept-new-versions: 5
 * End:
 */
