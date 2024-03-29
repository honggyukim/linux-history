/*  ewrk3.c: A DIGITAL EtherWORKS 3 ethernet driver for linux.

    Written 1994 by David C. Davies.

    Copyright 1994 Digital Equipment Corporation.

    This software may be used and distributed according to the terms of
    the GNU Public License, incorporated herein by reference.

    This driver is written for the Digital Equipment Corporation series
    of EtherWORKS ethernet cards:

	DE203 Turbo (BNC)
	DE204 Turbo (TP)
	DE205 Turbo (TP BNC)

    The driver has been tested on a relatively busy  network using the DE205
    card and benchmarked with 'ttcp': it transferred 16M  of data at 975kB/s
    (7.8Mb/s) to a DECstation 5000/200.

    The author may    be  reached as davies@wanton.lkg.dec.com  or   Digital
    Equipment Corporation, 550 King Street, Littleton MA 01460.

    =========================================================================
    This driver has been written  substantially  from scratch, although  its
    inheritance of style and stack interface from 'depca.c' and in turn from
    Donald Becker's 'lance.c' should be obvious.

    The  DE203/4/5 boards  all  use a new proprietary   chip in place of the
    LANCE chip used in prior cards  (DEPCA, DE100, DE200/1/2, DE210, DE422).
    Use the depca.c driver in the standard distribution  for the LANCE based
    cards from DIGITAL; this driver will not work with them.

    The DE203/4/5 cards have 2  main modes: shared memory  and I/O only. I/O
    only makes  all the card accesses through  I/O transactions and  no high
    (shared)  memory is used. This  mode provides a >48% performance penalty
    and  is deprecated in this  driver,  although allowed to provide initial
    setup when hardstrapped.

    The shared memory mode comes in 3 flavours: 2kB, 32kB and 64kB. There is
    no point in using any mode other than the 2kB  mode - their performances
    are virtually identical, although the driver has  been tested in the 2kB
    and 32kB modes. I would suggest you uncomment the line:

                             FORCE_2K_MODE;

    to allow the driver to configure the card as a  2kB card at your current
    base  address, thus leaving more  room to clutter  your  system box with
    other memory hungry boards.

    Upto 21 ISA and 7 EISA cards can be supported under this driver, limited
    primarily by the  available  IRQ   lines.   I have   checked   different
    configurations  of multiple depca  cards and  ewrk3 cards  and  have not
    found a problem yet (provided you have at least depca.c v0.38) ...

    The board IRQ setting   must be at  an unused  IRQ which is  auto-probed
    using  Donald  Becker's autoprobe  routines.   All  these cards   are at
    {5,10,11,15}.

    No 16MB memory  limitation should exist with this  driver as DMA is  not
    used and the common memory area is in low memory on the network card (my
    current system has 20MB and I've not had problems yet).

    The ability to load  this driver as a  loadable module has been included
    and used  extensively during the  driver development (to save those long
    reboot sequences). To utilise this ability, you have to do 8 things:

    0) have a copy of the loadable modules code installed on your system.
    1) copy ewrk3.c from the  /linux/drivers/net directory to your favourite
    temporary directory.
    2) edit the  source code near  line 1340 to reflect  the I/O address and
    IRQ you're using.
    3) compile  ewrk3.c, but include -DMODULE in  the command line to ensure
    that the correct bits are compiled (see end of source code).
    4) if you are wanting to add a new  card, goto 5. Otherwise, recompile a
    kernel with the ewrk3 configuration turned off and reboot.
    5) insmod ewrk3.o
    6) run the net startup bits for your new eth?? interface manually 
    (usually /etc/rc.inet[12] at boot time). 
    7) enjoy!
    
    [Alan Cox: Changed this so you can insmod ewrk3.o irq=x io=y]

    Note that autoprobing is not allowed in loadable modules - the system is
    already up and running and you're messing with interrupts.

    To unload a module, turn off the associated interface 
    'ifconfig eth?? down' then 'rmmod ewrk3'.

    Promiscuous   mode has been  turned  off  in this driver,   but  all the
    multicast  address bits  have been   turned on. This  improved the  send
    performance on a busy network by about 13%.

    Ioctl's have now been provided (primarily because  I wanted to grab some
    packet size statistics). They  are patterned after 'plipconfig.c' from a
    suggestion by Alan Cox.  Using these  ioctls, you can enable promiscuous
    mode, add/delete multicast  addresses, change the hardware address,  get
    packet size distribution statistics and muck around with the control and
    status register. I'll add others if and when the need arises.

    TO DO:
    ------


    Revision History
    ----------------

    Version   Date        Description
  
      0.1     26-aug-94   Initial writing. ALPHA code release.
      0.11    31-aug-94   Fixed: 2k mode memory base calc., 
                                 LeMAC version calc.,
				 IRQ vector assignments during autoprobe.
      0.12    31-aug-94   Tested working on LeMAC2 (DE20[345]-AC) card.
                          Fixed up MCA hash table algorithm.
      0.20     4-sep-94   Added IOCTL functionality.
      0.21    14-sep-94   Added I/O mode.
      0.21axp 15-sep-94   Special version for ALPHA AXP Linux V1.0
      0.22    16-sep-94   Added more IOCTLs & tidied up.
      0.23    21-sep-94   Added transmit cut through
      0.24    31-oct-94   Added uid checks in some ioctls
      0.30     1-nov-94   BETA code release
      0.31     5-dec-94   Added check/snarf_region code.

    =========================================================================
*/

static char *version = "ewrk3.c:v0.31 12/5/94 davies@wanton.lkg.dec.com\n";

#include <stdarg.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/string.h>
#include <linux/ptrace.h>
#include <linux/errno.h>
#include <linux/ioport.h>
#include <linux/malloc.h>
#include <linux/interrupt.h>
#include <asm/bitops.h>
#include <asm/io.h>
#include <asm/dma.h>
#include <asm/segment.h>

#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>

#include <linux/time.h>
#include <linux/types.h>
#include <linux/unistd.h>

#ifdef MODULE
#include <linux/module.h>
#include <linux/version.h>
#endif /* MODULE */

#include "ewrk3.h"

#ifdef EWRK3_DEBUG
static int ewrk3_debug = EWRK3_DEBUG;
#else
static int ewrk3_debug = 1;
#endif

#ifndef PROBE_LENGTH
#define PROBE_LENGTH    32
#endif

#ifndef PROBE_SEQUENCE
#define PROBE_SEQUENCE "FF0055AAFF0055AA"
#endif

#ifndef EWRK3_SIGNATURE
#define EWRK3_SIGNATURE {"DE203","DE204","DE205",""}
#define EWRK3_NAME_LENGTH 8
#endif

#ifndef EWRK3_RAM_BASE_ADDRESSES
#define EWRK3_RAM_BASE_ADDRESSES {0xc0000,0xd0000,0x00000}
#endif

/*
** Sets up the search areas for the autoprobe. You can disable an area
** by writing a zero into the corresponding bit position in EWRK3_IO_SEARCH.
** The LSb -> I/O 0x100. Each bit increments the I/O location searched by 0x20.
** Bit 24 -> I/O 0x400.
**
** By default, probes at locations:
**             0x1e0   (may conflict with hard disk)
**             0x320   (may conflict with hard disk)
**             0x3e0   (may conflict with floppy disk)
**
** are disabled.
*/

#define EWRK3_IO_BASE 0x100             /* Start address for probe search */
#define EWRK3_IOP_INC 0x20              /* I/O address increment */
#define EWRK3_IO_SEARCH 0x007dff7f      /* probe search mask */
static long mem_chkd = EWRK3_IO_SEARCH; /* holds which I/O addrs should be */
				        /* checked, for multi-EWRK3 case */

#ifndef MAX_NUM_EWRK3S
#define MAX_NUM_EWRK3S 21
#endif

#ifndef EWRK3_EISA_IO_PORTS 
#define EWRK3_EISA_IO_PORTS 0x0c00      /* I/O port base address, slot 0 */
#endif

#ifndef MAX_EISA_SLOTS
#define MAX_EISA_SLOTS 8
#define EISA_SLOT_INC 0x1000
#endif

#ifndef CRC_POLYNOMIAL
#define CRC_POLYNOMIAL 0x04c11db7       /* Ethernet CRC polynomial */
#endif /* CRC_POLYNOMIAL */

/*
** EtherWORKS 3 shared memory window sizes
*/
#define IO_ONLY         0x00
#define SHMEM_2K        0x800
#define SHMEM_32K       0x8000
#define SHMEM_64K       0x10000

/*
** EtherWORKS 3 IRQ ENABLE/DISABLE
*/
static unsigned char irq_mask = TNEM|TXDM|RNEM|RXDM;

#define ENABLE_IRQs \
  icr |= irq_mask;\
  outb(icr, EWRK3_ICR)                      /* Enable the IRQs */

#define DISABLE_IRQs \
  icr = inb(EWRK3_ICR);\
  icr &= ~irq_mask;\
  outb(icr, EWRK3_ICR)                      /* Disable the IRQs */

/*
** EtherWORKS 3 START/STOP
*/
#define START_EWRK3 \
  csr = inb(EWRK3_CSR);\
  csr &= ~(TXD|RXD);\
  outb(csr, EWRK3_CSR)                      /* Enable the TX and/or RX */

#define STOP_EWRK3 \
  csr = (TXD|RXD);\
  outb(csr, EWRK3_CSR)                      /* Disable the TX and/or RX */

/*
** The EtherWORKS 3 private structure
*/
#define EWRK3_PKT_STAT_SZ 16
#define EWRK3_PKT_BIN_SZ  128           /* Should be >=100 unless you
                                           increase EWRK3_PKT_STAT_SZ */

struct ewrk3_private {
    long shmem_base;                    /* Shared memory start address */
    long shmem_length;                  /* Shared memory window length */
    struct enet_statistics stats;       /* Public stats */
    struct {
      unsigned long bins[EWRK3_PKT_STAT_SZ]; /* Private stats counters */
      unsigned long unicast;
      unsigned long multicast;
      unsigned long broadcast;
      unsigned long excessive_collisions;
      unsigned long tx_underruns;
      unsigned long excessive_underruns;
    } pktStats;
    short mPage;                        /* Maximum 2kB Page number */
    unsigned char lemac;                /* Chip rev. level */
    unsigned char hard_strapped;        /* Don't allow a full open */
    unsigned char lock;                 /* Lock the page register */
    unsigned char txc;                  /* Transmit cut through */
};

/*
** Force the EtherWORKS 3 card to be in 2kB MODE
*/
#define FORCE_2K_MODE \
  shmem_length = SHMEM_2K;\
  outb(((mem_start - 0x80000) >> 11), EWRK3_MBR)

/*
** Public Functions
*/
static int ewrk3_open(struct device *dev);
static int ewrk3_queue_pkt(struct sk_buff *skb, struct device *dev);
static void ewrk3_interrupt(int reg_ptr);
static int ewrk3_close(struct device *dev);
static struct enet_statistics *ewrk3_get_stats(struct device *dev);
static void set_multicast_list(struct device *dev, int num_addrs, void *addrs);
static int ewrk3_ioctl(struct device *dev, struct ifreq *rq, int cmd);

/*
** Private functions
*/
static int  ewrk3_hw_init(struct device *dev, short iobase);
static void ewrk3_init(struct device *dev);
static int  ewrk3_rx(struct device *dev);
static int  ewrk3_tx(struct device *dev);

static void EthwrkSignature(char * name, char *eeprom_image);
static int  DevicePresent(short iobase);
static void SetMulticastFilter(struct device *dev, int num_addrs, char *addrs, char *multicast_table);

static int  Read_EEPROM(short iobase, unsigned char eaddr);
static int  Write_EEPROM(short data, short iobase, unsigned char eaddr);
static unsigned char aprom_crc (struct device *dev, unsigned char *eeprom_image, char chipType);

#ifndef MODULE
static struct device *isa_probe(struct device *dev);
static struct device *eisa_probe(struct device *dev);
static struct device *alloc_device(struct device *dev, int iobase);

static int num_ewrk3s = 0, num_eth = 0;
static unsigned char irq[] = {5,0,10,3,11,9,15,12};

#else
int  init_module(void);
void cleanup_module(void);

#endif /* MODULE */

static int autoprobed = 0;

/*
** Miscellaneous defines...
*/
#define INIT_EWRK3 {\
    int i;\
    outb(EEPROM_INIT, EWRK3_IOPR);\
    for (i=0;i<5000;i++) inb(EWRK3_CSR);\
		   }




int ewrk3_probe(struct device *dev)
{
  int base_addr = dev->base_addr;
  int status = -ENODEV;
#ifndef MODULE
  struct device *eth0;
#endif

  if (base_addr > 0x0ff) {	      /* Check a single specified location. */
    if (!autoprobed) {                /* Module or fixed location */
      if (!check_region(base_addr, EWRK3_IOP_INC)) {
	if (((mem_chkd >> ((base_addr - EWRK3_IO_BASE)/ EWRK3_IOP_INC))&0x01)==1) {
	  if (DevicePresent(base_addr) == 0) {      /* Is EWRK3 really here? */
	    request_region(base_addr, EWRK3_IOP_INC,"ewrk3"); /* Register I/O region */
	    status = ewrk3_hw_init(dev, base_addr);
	  } else {
	    printk("ewrk3_probe(): No device found\n");
	    mem_chkd &= ~(0x01 << ((base_addr - EWRK3_IO_BASE)/EWRK3_IOP_INC));
	  }
	}
      } else {
	printk("%s: ewrk3_probe(): Detected a device already registered at 0x%02x\n", dev->name, base_addr);
	mem_chkd &= ~(0x01 << ((base_addr - EWRK3_IO_BASE)/EWRK3_IOP_INC));
      }
    } else {                          /* already know what ewrk3 h/w is here */
      status = ewrk3_hw_init(dev, base_addr);
    }
  } else if (base_addr > 0) {         /* Don't probe at all. */
    status = -ENXIO;

#ifdef MODULE
  } else {
    printk("Autoprobing is not supported when loading a module based driver.\n");
    status = -EIO;
#else
  } else if (!autoprobed) {           /* First probe for the EWRK3 test */
                                      /* pattern in ROM */
    eth0=isa_probe(dev);
    eth0=eisa_probe(eth0);
    if (dev->priv) status=0;
    autoprobed = 1;
  } else {
    status = -ENXIO;
#endif /* MODULE */
    
  }

  if (status) dev->base_addr = base_addr;

  return status;
}

static int
ewrk3_hw_init(struct device *dev, short iobase)
{
  struct ewrk3_private *lp;
  int i, status=0;
  unsigned long mem_start, shmem_length;
  char name[EWRK3_NAME_LENGTH + 1];
  unsigned char cr, cmr, icr, nicsr, lemac, hard_strapped = 0;
  unsigned char eeprom_image[EEPROM_MAX], chksum, eisa_cr = 0;

  /*
  ** Stop the EWRK3. Enable the DBR ROM. Disable interrupts and remote boot.
  ** This also disables the EISA_ENABLE bit in the EISA Control Register.
  */
  if (iobase > 0x400) eisa_cr = inb(EISA_CR);
  INIT_EWRK3;

  nicsr = inb(EWRK3_CSR);

  /*
  ** Disable & mask all board interrupts
  */
  DISABLE_IRQs;

  if (nicsr == TXD|RXD) {

    /*
    ** Check that the EEPROM is alive and well and not living on Pluto...
    */
    for (chksum=0, i=0; i<EEPROM_MAX; i+=2) {
      union {
	short val;
	char c[2];
      } tmp;

      tmp.val = (short)Read_EEPROM(iobase, (i>>1));
      eeprom_image[i] = tmp.c[0];
      eeprom_image[i+1] = tmp.c[1];

      chksum += eeprom_image[i] + eeprom_image[i+1];
    }

    if (chksum != 0) {                             /* Bad EEPROM Data! */
      printk("%s: Device has a bad on-board EEPROM.\n", dev->name);
      status = -ENXIO;
    } else {
      /* 
      ** Now find out what kind of EWRK3 we have.
      */
      EthwrkSignature(name, eeprom_image);

      if (*name != '\0') {                         /* found a EWRK3 device */
	dev->base_addr = iobase;
      
	if (iobase > 0x400) {
	  outb(eisa_cr, EISA_CR);                  /* Rewrite the EISA CR */
	}

	lemac = eeprom_image[EEPROM_CHIPVER];
	cmr = inb(EWRK3_CMR);

	if (((lemac == LeMAC) && ((cmr & NO_EEPROM) != NO_EEPROM)) ||
	    ((lemac == LeMAC2) && !(cmr & HS))) {
	  printk("%s: %s at %#3x", dev->name, name, iobase);
	  hard_strapped = 1;
	} else if ((iobase&0x0fff)==EWRK3_EISA_IO_PORTS) {
	                                           /* EISA slot address */
	  printk("%s: %s at %#3x (EISA slot %d)", 
	                         dev->name, name, iobase, ((iobase>>12)&0x0f));
	} else {                                   /* ISA port address */
	  printk("%s: %s at %#3x", dev->name, name, iobase);
	}
	
	if (!status) {
	  printk(", h/w address ");
	  if (lemac == LeMAC2) {
	    for (i = 0;i < ETH_ALEN - 1;i++) { /* get the ethernet address */
	      printk("%2.2x:", dev->dev_addr[i] = 
		                              eeprom_image[EEPROM_PADDR0 + i]);
	      outb(eeprom_image[EEPROM_PADDR0 + i], EWRK3_PAR0 + i);
	    }
	    printk("%2.2x,\n",dev->dev_addr[i] = eeprom_image[EEPROM_PADDR0 + i]);
	    outb(eeprom_image[EEPROM_PADDR0 + i], EWRK3_PAR0 + i);
	  } else {
	    DevicePresent(iobase);          /* needed after the EWRK3_INIT */
	    for (i = 0; i < ETH_ALEN - 1; i++) { /* get the ethernet addr. */
	      printk("%2.2x:", dev->dev_addr[i] = inb(EWRK3_APROM));
	      outb(dev->dev_addr[i], EWRK3_PAR0 + i);
	    }
	    printk("%2.2x,\n", dev->dev_addr[i] = inb(EWRK3_APROM));
	    outb(dev->dev_addr[i], EWRK3_PAR0 + i);
	  }

	  if (aprom_crc(dev, eeprom_image, lemac)) {
	    printk("      which has an EEPROM CRC error.\n");
	    status = -ENXIO;
	  } else {
	    if (lemac == LeMAC2) {            /* Special LeMAC2 CMR things */
	      cmr &= ~(RA | WB | LINK | POLARITY | _0WS);         
	      if (eeprom_image[EEPROM_MISC0] & READ_AHEAD)    cmr |= RA;
	      if (eeprom_image[EEPROM_MISC0] & WRITE_BEHIND)  cmr |= WB;
	      if (eeprom_image[EEPROM_NETMAN0] & NETMAN_POL)  cmr |= POLARITY;
	      if (eeprom_image[EEPROM_NETMAN0] & NETMAN_LINK) cmr |= LINK;
	      if (eeprom_image[EEPROM_MISC0] & _0WS_ENA)      cmr |= _0WS;
	    }
	    if (eeprom_image[EEPROM_SETUP] & SETUP_DRAM)      cmr |= DRAM;
	    outb(cmr, EWRK3_CMR);

	    cr = inb(EWRK3_CR);               /* Set up the Control Register */
	    cr |= eeprom_image[EEPROM_SETUP] & SETUP_APD;
	    if (cr & SETUP_APD) cr |= eeprom_image[EEPROM_SETUP] & SETUP_PS;
	    cr |= eeprom_image[EEPROM_MISC0] & FAST_BUS;
	    cr |= eeprom_image[EEPROM_MISC0] & ENA_16;
	    outb(cr, EWRK3_CR);

	    /* 
	    ** Determine the base address and window length for the EWRK3
	    ** RAM from the memory base register.
	    */
	    mem_start = inb(EWRK3_MBR);
	    shmem_length = 0;
	    if (mem_start != 0) {
	      if ((mem_start >= 0x0a) && (mem_start <= 0x0f)) {
		mem_start *= SHMEM_64K;
		shmem_length = SHMEM_64K;
	      } else if ((mem_start >= 0x14) && (mem_start <= 0x1f)) {
		mem_start *= SHMEM_32K;
		shmem_length = SHMEM_32K;
	      } else if ((mem_start >= 0x40) && (mem_start <= 0xff)) {
		mem_start = mem_start * SHMEM_2K + 0x80000;
		shmem_length = SHMEM_2K;
	      } else {
		status = -ENXIO;
	      }
	    }
	  
	    /*
	    ** See the top of this source code for comments about
	    ** uncommenting this line.
	    */
/*	    FORCE_2K_MODE;*/

	    if (!status) {
	      if (hard_strapped) {
		printk("      is hard strapped.\n");
	      } else if (mem_start) {
		printk("      has a %dk RAM window", (int)(shmem_length >> 10));
		printk(" at 0x%.5lx", mem_start);
	      } else {
		printk("      is in I/O only mode");
	      }
	    
	      /* private area & initialise */
	      dev->priv = (void *) kmalloc(sizeof(struct ewrk3_private), 
					                           GFP_KERNEL);
	      lp = (struct ewrk3_private *)dev->priv;
	      memset(dev->priv, 0, sizeof(struct ewrk3_private));
	      lp->shmem_base = mem_start;
	      lp->shmem_length = shmem_length;
	      lp->lemac = lemac;
	      lp->hard_strapped = hard_strapped;

	      lp->mPage = 64;
	      if (cmr & DRAM) lp->mPage <<= 1 ;     /* 2 DRAMS on module */ 

	      if (!hard_strapped) {
		/*
		** Enable EWRK3 board interrupts for autoprobing
		*/
		icr |= IE;	                   /* Enable interrupts */
		outb(icr, EWRK3_ICR);
	    
		/* The DMA channel may be passed in on this parameter. */
		dev->dma = 0;
	
		/* To auto-IRQ we enable the initialization-done and DMA err,
		   interrupts. For now we will always get a DMA error. */
		if (dev->irq < 2) {
#ifndef MODULE
		  unsigned char irqnum;
	      
		  autoirq_setup(0);

		  /* 
		  ** Trigger a TNE interrupt.
		  */
		  icr |=TNEM;
		  outb(1,EWRK3_TDQ);          /* Write to the TX done queue */
		  outb(icr, EWRK3_ICR);       /* Unmask the TXD interrupt */
	      
		  irqnum = irq[((icr & IRQ_SEL) >> 4)];
	      
		  dev->irq = autoirq_report(1);
		  if ((dev->irq) && (irqnum == dev->irq)) {
		    printk(" and uses IRQ%d.\n", dev->irq);
		  } else {
		    if (!dev->irq) {
		      printk(" and failed to detect IRQ line.\n");
		    } else if ((irqnum == 1) && (lemac == LeMAC2)) {
		      printk(" and an illegal IRQ line detected.\n");
		    } else {
		      printk(", but incorrect IRQ line detected.\n");
		    }
		    status = -ENXIO;
		  }
		
		  DISABLE_IRQs;                 /* Mask all interrupts */

#endif /* MODULE */
		} else {
		  printk(" and requires IRQ%d.\n", dev->irq);
		}
	      }
	    } else {
	      status = -ENXIO;
	    }
	  }
	}
      } else {
	status = -ENXIO;
      }
    }

    if (!status) {
      if (ewrk3_debug > 0) {
	printk(version);
      }
      
      /* The EWRK3-specific entries in the device structure. */
      dev->open = &ewrk3_open;
      dev->hard_start_xmit = &ewrk3_queue_pkt;
      dev->stop = &ewrk3_close;
      dev->get_stats = &ewrk3_get_stats;
#ifdef HAVE_MULTICAST
      dev->set_multicast_list = &set_multicast_list;
#endif
      dev->do_ioctl = &ewrk3_ioctl;

      dev->mem_start = 0;
	
      /* Fill in the generic field of the device structure. */
      ether_setup(dev);
    }
  } else {
    status = -ENXIO;
  }

  return status;
}


static int
ewrk3_open(struct device *dev)
{
  struct ewrk3_private *lp = (struct ewrk3_private *)dev->priv;
  int i, iobase = dev->base_addr;
  int status = 0;
  unsigned char icr, csr;

  /*
  ** Stop the TX and RX...
  */
  STOP_EWRK3;

  if (!lp->hard_strapped) {
    if (request_irq(dev->irq, &ewrk3_interrupt, 0, "ewrk3")) {
      printk("ewrk3_open(): Requested IRQ%d is busy\n",dev->irq);
      status = -EAGAIN;
    } else {

      irq2dev_map[dev->irq] = dev;

      /* 
      ** Re-initialize the EWRK3... 
      */
      ewrk3_init(dev);

      if (ewrk3_debug > 1){
	printk("%s: ewrk3 open with irq %d\n",dev->name,dev->irq);
	printk("\tphysical address: ");
	for (i=0;i<6;i++){
	  printk("%2.2x:",(short)dev->dev_addr[i]);
	}
	printk("\n");
	printk("\tchecked memory: 0x%08lx\n",mem_chkd);
	if (lp->shmem_length == 0) {
	  printk("\tno shared memory, I/O only mode\n");
	} else {
	  printk("\tstart of shared memory: 0x%08lx\n",lp->shmem_base);
	  printk("\twindow length: 0x%04lx\n",lp->shmem_length);
	}
	printk("\t# of DRAMS: %d\n",((inb(EWRK3_CMR) & 0x02) ? 2 : 1));
	printk("\tcsr:  0x%02x\n", inb(EWRK3_CSR));
	printk("\tcr:   0x%02x\n", inb(EWRK3_CR));
	printk("\ticr:  0x%02x\n", inb(EWRK3_ICR));
	printk("\tcmr:  0x%02x\n", inb(EWRK3_CMR));
	printk("\tfmqc: 0x%02x\n", inb(EWRK3_FMQC));
      }

      dev->tbusy = 0;                         
      dev->start = 1;
      dev->interrupt = UNMASK_INTERRUPTS;

      /*
      ** Unmask EWRK3 board interrupts
      */
      icr = inb(EWRK3_ICR);
      ENABLE_IRQs;

    }
  } else {
    dev->start = 0;
    dev->tbusy = 1;
    printk("%s: ewrk3 available for hard strapped set up only.\n", dev->name);
    printk("      Run the 'ewrk3setup' utility or remove the hard straps.\n");
  }

#ifdef MODULE
    MOD_INC_USE_COUNT;
#endif       


  return status;
}

/*
** Initialize the EtherWORKS 3 operating conditions
*/
static void
ewrk3_init(struct device *dev)
{
  struct ewrk3_private *lp = (struct ewrk3_private *)dev->priv;
  char csr, page;
  short iobase = dev->base_addr;
  
  /* 
  ** Enable all multicasts 
  */
  set_multicast_list(dev, HASH_TABLE_LEN, NULL);

  /*
  ** Clean out any remaining entries in all the queues here
  */
  while (inb(EWRK3_TQ));
  while (inb(EWRK3_TDQ));
  while (inb(EWRK3_RQ));
  while (inb(EWRK3_FMQ));

  /*
  ** Write a clean free memory queue
  */
  for (page=1;page<lp->mPage;page++) {      /* Write the free page numbers */
    outb(page, EWRK3_FMQ);                  /* to the Free Memory Queue */
  }

  lp->lock = 0;                             /* Ensure there are no locks */

  START_EWRK3;                              /* Enable the TX and/or RX */
}

/* 
** Writes a socket buffer to the free page queue
*/
static int
ewrk3_queue_pkt(struct sk_buff *skb, struct device *dev)
{
  struct ewrk3_private *lp = (struct ewrk3_private *)dev->priv;
  int iobase = dev->base_addr;
  int status = 0;
  unsigned char icr, csr;

  /* Transmitter timeout, serious problems. */
  if (dev->tbusy || lp->lock) {
    int tickssofar = jiffies - dev->trans_start;
    if (tickssofar < 10) {
      status = -1;
    } else if (!lp->hard_strapped) {
      printk("%s: transmit timed/locked out, status %04x, resetting.\n",
	                                           dev->name, inb(EWRK3_CSR));
	
      /*
      ** Mask all board interrupts
      */
      DISABLE_IRQs;

      /*
      ** Stop the TX and RX...
      */
      STOP_EWRK3;

      ewrk3_init(dev);

      /*
      ** Unmask EWRK3 board interrupts
      */
      ENABLE_IRQs;

      dev->tbusy=0;
      dev->trans_start = jiffies;
    }
  } else if (skb == NULL) {
    dev_tint(dev);
  } else if (skb->len > 0) {

    /* 
    ** Block a timer-based transmit from overlapping.  This could better be
    ** done with atomic_swap(1, dev->tbusy), but set_bit() works as well. 
    */
    if (set_bit(0, (void*)&dev->tbusy) != 0)
      printk("%s: Transmitter access conflict.\n", dev->name);

    DISABLE_IRQs;                      /* So that the page # remains correct */
    
    /* 
    ** Get a free page from the FMQ when resources are available
    */
    if (inb(EWRK3_FMQC) > 0) {
      unsigned char *buf;
      unsigned char page;

      if ((page = inb(EWRK3_FMQ)) < lp->mPage) {
	buf = NULL;

	/*
	** Set up shared memory window and pointer into the window
	*/
	while (set_bit(0, (void *)&lp->lock) != 0); /* Wait for lock to free */
	if (lp->shmem_length == IO_ONLY) {
	  outb(page, EWRK3_IOPR);
	} else if (lp->shmem_length == SHMEM_2K) {
	  buf = (char *) lp->shmem_base;
	  outb(page, EWRK3_MPR);
	} else if (lp->shmem_length == SHMEM_32K) {
	  buf = (char *)((((short)page << 11) & 0x7800) + lp->shmem_base);
	  outb((page >> 4), EWRK3_MPR);
	} else if (lp->shmem_length == SHMEM_64K) {
	  buf = (char *)((((short)page << 11) & 0xf800) + lp->shmem_base);
	  outb((page >> 5), EWRK3_MPR);
	} else {
	  status = -1;
	  printk("%s: Oops - your private data area is hosed!\n",dev->name);
	}

	if (!status) {

          /* 
	  ** Set up the buffer control structures and copy the data from
	  ** the socket buffer to the shared memory .
	  */

	  if (lp->shmem_length == IO_ONLY) {
	    int i;
	    unsigned char *p = skb->data;
	    
	    outb((char)(QMODE | PAD | IFC), EWRK3_DATA);
	    outb((char)(skb->len & 0xff), EWRK3_DATA);
	    outb((char)((skb->len >> 8) & 0xff), EWRK3_DATA);
	    outb((char)0x04, EWRK3_DATA);
	    for (i=0; i<skb->len; i++) {
	      outb(*p++, EWRK3_DATA);
	    }
	    outb(page, EWRK3_TQ);                     /* Start sending pkt */
	  } else {
	    *buf++ = (char)(QMODE | PAD | IFC);       /* control byte */
	    *buf++ = (char)(skb->len & 0xff);         /* length (16 bit xfer)*/
	    if (lp->txc) {
	      *buf++ = (char)(((skb->len >> 8) & 0xff) | XCT);
	      *buf++ = 0x04;                          /* index byte */
	      *(buf + skb->len) = 0x00;               /* Write the XCT flag */
	      memcpy(buf, skb->data, PRELOAD);        /* Write PRELOAD bytes */
	      outb(page, EWRK3_TQ);                   /* Start sending pkt */
	      memcpy(buf + PRELOAD, skb->data + PRELOAD, skb->len - PRELOAD);
	      *(buf + skb->len) = 0xff;               /* Write the XCT flag */
	    } else {
	      *buf++ = (char)((skb->len >> 8) & 0xff);
	      *buf++ = 0x04;                          /* index byte */
	      memcpy(buf, skb->data, skb->len);       /* Write data bytes */
	      outb(page, EWRK3_TQ);                   /* Start sending pkt */
	    }
	  }

	  dev->trans_start = jiffies;

	  dev_kfree_skb (skb, FREE_WRITE);

        } else {              /* return unused page to the free memory queue */
	  outb(page, EWRK3_FMQ);
	}
	lp->lock = 0;         /* unlock the page register */
      } else {
	printk("ewrk3_queue_pkt(): Invalid free memory page (%d).\n",
	                                                 (unsigned char) page);
      }
    } else {
      printk("ewrk3_queue_pkt(): No free resources...\n");
      printk("ewrk3_queue_pkt(): CSR: %02x ICR: %02x FMQC: %02x\n",inb(EWRK3_CSR),inb(EWRK3_ICR),inb(EWRK3_FMQC));
    }
    
    /* Check for free resources: clear 'tbusy' if there are some */
    if (inb(EWRK3_FMQC) > 0) {
      dev->tbusy = 0;
    }

    ENABLE_IRQs;
  }

  return status;
}

/*
** The EWRK3 interrupt handler. 
*/
static void
ewrk3_interrupt(int reg_ptr)
{
    int irq = -(((struct pt_regs *)reg_ptr)->orig_eax+2);
    struct device *dev = (struct device *)(irq2dev_map[irq]);
    struct ewrk3_private *lp;
    int iobase;
    unsigned char icr, cr, csr;

    if (dev == NULL) {
	printk ("ewrk3_interrupt(): irq %d for unknown device.\n", irq);
    } else {
      lp = (struct ewrk3_private *)dev->priv;
      iobase = dev->base_addr;

      if (dev->interrupt)
	printk("%s: Re-entering the interrupt handler.\n", dev->name);

      dev->interrupt = MASK_INTERRUPTS;

      /* get the interrupt information */
      csr = inb(EWRK3_CSR);

      /* 
      ** Mask the EWRK3 board interrupts and turn on the LED 
      */
      DISABLE_IRQs;

      cr = inb(EWRK3_CR);
      cr |= LED;
      outb(cr, EWRK3_CR);

      if (csr & RNE)		  /* Rx interrupt (packet[s] arrived) */
	ewrk3_rx(dev);

      if (csr & TNE) 	          /* Tx interrupt (packet sent) */
        ewrk3_tx(dev);

      /*
      ** Now deal with the TX/RX disable flags. These are set when there
      ** are no more resources. If resources free up then enable these
      ** interrupts, otherwise mask them - failure to do this will result
      ** in the system hanging in an interrupt loop.
      */
      if (inb(EWRK3_FMQC)) {      /* any resources available? */
	irq_mask |= TXDM|RXDM;    /* enable the interrupt source */
	csr &= ~(TXD|RXD);        /* ensure restart of a stalled TX or RX */
	outb(csr, EWRK3_CSR);
	dev->tbusy = 0;           /* clear TX busy flag */
	mark_bh(NET_BH);
      } else {
	irq_mask &= ~(TXDM|RXDM); /* disable the interrupt source */
      }

      /* Unmask the EWRK3 board interrupts and turn off the LED */
      cr &= ~LED;
      outb(cr, EWRK3_CR);

      dev->interrupt = UNMASK_INTERRUPTS;

      ENABLE_IRQs;
    }

    return;
}

static int
ewrk3_rx(struct device *dev)
{
  struct ewrk3_private *lp = (struct ewrk3_private *)dev->priv;
  int i, iobase = dev->base_addr;
  unsigned char page, tmpPage = 0, tmpLock = 0, *buf;
  int status = 0;

  while (inb(EWRK3_RQC) && !status) {        /* Whilst there's incoming data */
    if ((page = inb(EWRK3_RQ)) < lp->mPage) {/* Get next entry's buffer page */
      buf = NULL;

      /*
      ** Preempt any process using the current page register. Check for
      ** an existing lock to reduce time taken in I/O transactions.
      */
      if ((tmpLock = set_bit(0, (void *)&lp->lock)) == 1) {   /* Assert lock */
	if (lp->shmem_length == IO_ONLY) {              /* Get existing page */
	  tmpPage = inb(EWRK3_IOPR);
	} else {
	  tmpPage = inb(EWRK3_MPR);
	}
      }

      /*
      ** Set up shared memory window and pointer into the window
      */
      if (lp->shmem_length == IO_ONLY) {
	outb(page, EWRK3_IOPR);
      } else if (lp->shmem_length == SHMEM_2K) {
	buf = (char *) lp->shmem_base;
	outb(page, EWRK3_MPR);
      } else if (lp->shmem_length == SHMEM_32K) {
	buf = (char *)((((short)page << 11) & 0x7800) + lp->shmem_base);
	outb((page >> 4), EWRK3_MPR);
      } else if (lp->shmem_length == SHMEM_64K) {
	buf = (char *)((((short)page << 11) & 0xf800) + lp->shmem_base);
	outb((page >> 5), EWRK3_MPR);
      } else {
	status = -1;
	printk("%s: Oops - your private data area is hosed!\n",dev->name);
      }

      if (!status) {
	char rx_status;
	int pkt_len;

	if (lp->shmem_length == IO_ONLY) {
	  rx_status = inb(EWRK3_DATA);
	  pkt_len = inb(EWRK3_DATA);
	  pkt_len |= ((unsigned short)inb(EWRK3_DATA) << 8);
	} else {
	  rx_status = (char)(*buf++);
	  pkt_len = (short)(*buf+((*(buf+1))<<8));
	  buf+=3;
	}

	if (!(rx_status & ROK)) {	    /* There was an error. */
	  lp->stats.rx_errors++;            /* Update the error stats. */
	  if (rx_status & DBE) lp->stats.rx_frame_errors++;
	  if (rx_status & CRC) lp->stats.rx_crc_errors++;
	  if (rx_status & PLL) lp->stats.rx_fifo_errors++;
	} else {
	  struct sk_buff *skb;

          if ((skb = alloc_skb(pkt_len, GFP_ATOMIC)) != NULL) {
	    skb->len = pkt_len;
	    skb->dev = dev;

	    if (lp->shmem_length == IO_ONLY) {
	      unsigned char *p = skb->data;

	      *p = inb(EWRK3_DATA);         /* dummy read */
	      for (i=0; i<skb->len; i++) {
		*p++ = inb(EWRK3_DATA);
	      }
	    } else {
	      memcpy(skb->data, buf, pkt_len);
	    }

	    /* 
	    ** Notify the upper protocol layers that there is another 
	    ** packet to handle
	    */
	    netif_rx(skb);

	    /*
	    ** Update stats
	    */
	    lp->stats.rx_packets++;
	    for (i=1; i<EWRK3_PKT_STAT_SZ-1; i++) {
	      if (pkt_len < i*EWRK3_PKT_BIN_SZ) {
		lp->pktStats.bins[i]++;
		i = EWRK3_PKT_STAT_SZ;
	      }
	    }
	    buf = skb->data;                  /* Look at the dest addr */
	    if (buf[0] & 0x01) {              /* Multicast/Broadcast */
	      if ((*(long *)&buf[0] == -1) && (*(short *)&buf[4] == -1)) {
		lp->pktStats.broadcast++;
	      } else {
		lp->pktStats.multicast++;
	      }
	    } else if ((*(long *)&buf[0] == *(long *)&dev->dev_addr[0]) &&
		       (*(short *)&buf[4] == *(short *)&dev->dev_addr[4])) {
	      lp->pktStats.unicast++;
	    }

	    lp->pktStats.bins[0]++;           /* Duplicates stats.rx_packets */
	    if (lp->pktStats.bins[0] == 0) {  /* Reset counters */
	      memset(&lp->pktStats, 0, sizeof(lp->pktStats));
	    }
	  } else {
	    printk("%s: Insufficient memory; nuking packet.\n", dev->name);
	    lp->stats.rx_dropped++;	      /* Really, deferred. */
	    break;
	  }
        }
      }
      /*
      ** Return the received buffer to the free memory queue
      */
      outb(page, EWRK3_FMQ);

      if (tmpLock) {                          /* If a lock was preempted */
	if (lp->shmem_length == IO_ONLY) {    /* Replace old page */
	  outb(tmpPage, EWRK3_IOPR);
	} else {
	  outb(tmpPage, EWRK3_MPR);
	}
      }
      lp->lock = 0;                           /* Unlock the page register */
    } else {
      printk("ewrk3_rx(): Illegal page number, page %d\n",page);
      printk("ewrk3_rx(): CSR: %02x ICR: %02x FMQC: %02x\n",inb(EWRK3_CSR),inb(EWRK3_ICR),inb(EWRK3_FMQC));
    }
  }
  return status;
}

/*
** Buffer sent - check for TX buffer errors.
*/
static int
ewrk3_tx(struct device *dev)
{
  struct ewrk3_private *lp = (struct ewrk3_private *)dev->priv;
  int iobase = dev->base_addr;
  unsigned char tx_status;

  while ((tx_status = inb(EWRK3_TDQ)) > 0) {  /* Whilst there's old buffers */
    if (tx_status & VSTS) {                   /* The status is valid */
      if (tx_status & MAC_TXE) {
	lp->stats.tx_errors++;
	if (tx_status & MAC_NCL)    lp->stats.tx_carrier_errors++;
	if (tx_status & MAC_LCL)    lp->stats.tx_window_errors++;
	if (tx_status & MAC_CTU) {
	  if ((tx_status & MAC_COLL) ^ MAC_XUR) {
	    lp->pktStats.tx_underruns++;
	  } else {
	    lp->pktStats.excessive_underruns++;
	  }
	} else 	if (tx_status & MAC_COLL) {
	  if ((tx_status & MAC_COLL) ^ MAC_XCOLL) {
	    lp->stats.collisions++;
	  } else {
	    lp->pktStats.excessive_collisions++;
	  }
	}
      } else {
	lp->stats.tx_packets++;
      }
    }
  }

  return 0;
}

static int
ewrk3_close(struct device *dev)
{
  struct ewrk3_private *lp = (struct ewrk3_private *)dev->priv;
  int iobase = dev->base_addr;
  unsigned char icr, csr;

  dev->start = 0;
  dev->tbusy = 1;

  if (ewrk3_debug > 1) {
    printk("%s: Shutting down ethercard, status was %2.2x.\n",
	   dev->name, inb(EWRK3_CSR));
  }

  /* 
  ** We stop the EWRK3 here... mask interrupts and stop TX & RX
  */
  DISABLE_IRQs;

  STOP_EWRK3;

  /*
  ** Clean out the TX and RX queues here (note that one entry
  ** may get added to either the TXD or RX queues if the the TX or RX
  ** just starts processing a packet before the STOP_EWRK3 command
  ** is received. This will be flushed in the ewrk3_open() call).
  */
  while (inb(EWRK3_TQ));
  while (inb(EWRK3_TDQ));
  while (inb(EWRK3_RQ));

  if (!lp->hard_strapped) {
    free_irq(dev->irq);
    
    irq2dev_map[dev->irq] = 0;
  }

#ifdef MODULE
  MOD_DEC_USE_COUNT;
#endif    

  return 0;
}

static struct enet_statistics *
ewrk3_get_stats(struct device *dev)
{
  struct ewrk3_private *lp = (struct ewrk3_private *)dev->priv;

  /* Null body since there is no framing error counter */
    
  return &lp->stats;
}

/*
** Set or clear the multicast filter for this adaptor.
** num_addrs == -1	Promiscuous mode, receive all packets
** num_addrs == 0	Normal mode, clear multicast list
** num_addrs > 0	Multicast mode, receive normal and MC packets, and do
** 			best-effort filtering.
*/
static void
set_multicast_list(struct device *dev, int num_addrs, void *addrs)
{
  struct ewrk3_private *lp = (struct ewrk3_private *)dev->priv;
  int iobase = dev->base_addr;
  char *multicast_table;
  unsigned char csr;

  csr = inb(EWRK3_CSR);

  if (lp->shmem_length == IO_ONLY) {
    multicast_table = (char *) PAGE0_HTE;
  } else {
    multicast_table = (char *)(lp->shmem_base + PAGE0_HTE);
  }

  if (num_addrs >= 0) {
    SetMulticastFilter(dev, num_addrs, (char *)addrs, multicast_table);
    csr &= ~PME;
    csr |= MCE;
    outb(csr, EWRK3_CSR);
  } else {                             /* set promiscuous mode */
    csr |= PME;
    csr &= ~MCE;
    outb(csr, EWRK3_CSR);
  }
}

/*
** Calculate the hash code and update the logical address filter
** from a list of ethernet multicast addresses.
** Derived from a 'C' program in the AMD data book:
** "Am79C90 CMOS Local Area Network Controller for Ethernet (C-LANCE)", 
** Pub #17781, Rev. A, May 1993
**
*/
static void SetMulticastFilter(struct device *dev, int num_addrs, char *addrs, char *multicast_table)
{
  struct ewrk3_private *lp = (struct ewrk3_private *)dev->priv;
  int iobase = dev->base_addr;
  char j, ctrl, bit, octet;
  short *p = (short *) multicast_table;
  unsigned short hashcode;
  int i;
  long int crc, poly = (long int) CRC_POLYNOMIAL;

  while (set_bit(0, (void *)&lp->lock) != 0); /* Wait for lock to free */

  if (lp->shmem_length == IO_ONLY) {
    outb(0, EWRK3_IOPR);
    outw((short)((long)multicast_table), EWRK3_PIR1);
  } else {
    outb(0, EWRK3_MPR);
  }

  if (num_addrs == HASH_TABLE_LEN) {
    for (i=0; i<(HASH_TABLE_LEN >> 3); i++) {
      if (lp->shmem_length == IO_ONLY) {
	outb(0xff, EWRK3_DATA);
      } else {                /* memset didn't work here */
	*p++ = 0xffff;
	i++;
      }
    }
  } else if (num_addrs == 0) {
    if (lp->shmem_length == IO_ONLY) {
      for (i=0; i<(HASH_TABLE_LEN >> 3); i++) {
	outb(0x00, EWRK3_DATA);
      } 
    } else {
      memset(multicast_table, 0, (HASH_TABLE_LEN >> 3));
    }
  } else {
    for (i=0;i<num_addrs;i++) {              /* for each address in the list */
      if (((char) *(addrs+ETH_ALEN*i) & 0x01) == 1) {/* multicast address? */ 
	crc = (long int) 0xffffffff;         /* init CRC for each address */
	for (octet=0;octet<ETH_ALEN;octet++) { /* for each address octet */
	  for(j=0;j<8;j++) {                 /* process each address bit */
	    bit = (((char)* (addrs+ETH_ALEN*i+octet)) >> j) & 0x01;
	    ctrl = ((crc < 0) ? 1 : 0);      /* shift the control bit */
	    crc <<= 1;                       /* shift the CRC */
	    if (bit ^ ctrl) {                /* (bit) XOR (control bit) */
	      crc ^= poly;                   /* (CRC) XOR (polynomial) */
	    }
	  }
	}
	hashcode = ((crc >>= 23) & 0x01);    /* hashcode is 9 MSb of CRC ... */
	for (j=0;j<8;j++) {                  /* ... in reverse order. */
	  hashcode <<= 1;
	  crc >>= 1;
	  hashcode |= (crc & 0x01);
	}                                      
                                      
	octet = hashcode >> 3;               /* bit[3-8] -> octet in filter */
	                                     /* bit[0-2] -> bit in octet */
	if (lp->shmem_length == IO_ONLY) {
	  unsigned char tmp;

	  outw((short)((long)multicast_table) + octet, EWRK3_PIR1);
	  tmp = inb(EWRK3_DATA);
	  tmp |= (1 << (hashcode & 0x07));
	  outw((short)((long)multicast_table) + octet, EWRK3_PIR1);
	  outb(tmp, EWRK3_DATA); 
	} else {
	  multicast_table[octet] |= (1 << (hashcode & 0x07));
	}
      }
    }
  }

  lp->lock = 0;                              /* Unlock the page register */

  return;
}

#ifndef MODULE
/*
** ISA bus I/O device probe
*/
static struct device *isa_probe(struct device *dev)
{
  int i, iobase, status;
  unsigned long int tmp = mem_chkd;

  for (status = -ENODEV, iobase = EWRK3_IO_BASE,i = 0; 
       i < 24;
       iobase += EWRK3_IOP_INC, i++) {
    if (tmp & 0x01) {
      /* Anything else registered here? */
      if (!check_region(iobase, EWRK3_IOP_INC)) {    
	if (DevicePresent(iobase) == 0) {
/*
** Device found. Mark its (I/O) location for future reference. Only 24
** EtherWORKS devices can exist between 0x100 and 0x3e0.
*/
	  request_region(iobase, EWRK3_IOP_INC,"ewrk3");
	  if (num_ewrk3s > 0) {        /* only gets here in autoprobe */
	    dev = alloc_device(dev, iobase);
	  } else {
	    if ((status = ewrk3_hw_init(dev, iobase)) == 0) {
	      num_ewrk3s++;
	    }
	  }
	  num_eth++;
	} else {
	  mem_chkd &= ~(0x01 << ((iobase - EWRK3_IO_BASE)/EWRK3_IOP_INC));
	}
      } else {
	printk("%s: ewrk3_probe(): Detected a device already registered at 0x%02x\n", dev->name, iobase);
	mem_chkd &= ~(0x01 << ((iobase - EWRK3_IO_BASE)/EWRK3_IOP_INC));
      }
    }
    tmp >>= 1;
  }

  return dev;
}

/*
** EISA bus I/O device probe. Probe from slot 1 since slot 0 is usually
** the motherboard.
*/
static struct device *eisa_probe(struct device *dev)
{
  int i, iobase = EWRK3_EISA_IO_PORTS;
  int status;

  iobase+=EISA_SLOT_INC;            /* get the first slot address */
  for (status = -ENODEV, i=1; i<MAX_EISA_SLOTS; i++, iobase+=EISA_SLOT_INC) {

    /* Anything else registered here? */
    if (!check_region(iobase, EWRK3_IOP_INC)) {
      if (DevicePresent(iobase) == 0) {
/*
** Device found. Mark its slot location for future reference. Only 7
** EtherWORKS devices can exist in EISA space....
*/
	mem_chkd |= (0x01 << (i + 24));
	request_region(iobase, EWRK3_IOP_INC,"ewrk3");
	if (num_ewrk3s > 0) {        /* only gets here in autoprobe */
	  dev = alloc_device(dev, iobase);
	} else {
	  if ((status = ewrk3_hw_init(dev, iobase)) == 0) {
	    num_ewrk3s++;
	  }
	}
	num_eth++;
      }
    }
  }
  return dev;
}

/*
** Allocate the device by pointing to the next available space in the
** device structure. Should one not be available, it is created.
*/
static struct device *alloc_device(struct device *dev, int iobase)
{
  /*
  ** Check the device structures for an end of list or unused device
  */
  while (dev->next != NULL) {
    if (dev->next->base_addr == 0xffe0) break;
    dev = dev->next;         /* walk through eth device list */
    num_eth++;               /* increment eth device number */
  }

  /*
  ** If no more device structures, malloc one up. If memory could
  ** not be allocated, print an error message.
  */
  if (dev->next == NULL) {
    dev->next = (struct device *)kmalloc(sizeof(struct device) + 8,
					 GFP_KERNEL);
    if (dev->next == NULL) {
      printk("eth%d: Device not initialised, insufficient memory\n",
	     num_eth);
    }
  }
  
  /*
  ** If the memory was allocated, point to the new memory area
  ** and initialize it (name, I/O address, next device (NULL) and
  ** initialisation probe routine).
  */
  if ((dev->next != NULL) &&
      (num_eth > 0) && (num_eth < 9999)) {
    dev = dev->next;                    /* point to the new device */
    dev->name = (char *)(dev + sizeof(struct device));
    sprintf(dev->name,"eth%d", num_eth);/* New device name */
    dev->base_addr = iobase;            /* assign the io address */
    dev->next = NULL;                   /* mark the end of list */
    dev->init = &ewrk3_probe;           /* initialisation routine */
    num_ewrk3s++;
  }

  return dev;
}
#endif    /* MODULE */

/*
** Read the EWRK3 EEPROM using this routine
*/
static int Read_EEPROM(short iobase, unsigned char eaddr)
{
  int i;

  outb((eaddr & 0x3f), EWRK3_PIR1);     /* set up 6 bits of address info */
  outb(EEPROM_RD, EWRK3_IOPR);          /* issue read command */
  for (i=0;i<5000;i++) inb(EWRK3_CSR);  /* wait 1msec */

  return inw(EWRK3_EPROM1);             /* 16 bits data return */
}

/*
** Write the EWRK3 EEPROM using this routine
*/
static int Write_EEPROM(short data, short iobase, unsigned char eaddr)
{
  int i;

  outb(EEPROM_WR_EN, EWRK3_IOPR);       /* issue write enable command */
  for (i=0;i<5000;i++) inb(EWRK3_CSR);  /* wait 1msec */
  outw(data, EWRK3_EPROM1);             /* write data to register */
  outb((eaddr & 0x3f), EWRK3_PIR1);     /* set up 6 bits of address info */
  outb(EEPROM_WR, EWRK3_IOPR);          /* issue write command */
  for (i=0;i<75000;i++) inb(EWRK3_CSR); /* wait 15msec */
  outb(EEPROM_WR_DIS, EWRK3_IOPR);      /* issue write disable command */
  for (i=0;i<5000;i++) inb(EWRK3_CSR);  /* wait 1msec */

  return 0;
}

/*
** Look for a particular board name in the on-board EEPROM.
*/
static void EthwrkSignature(char *name, char *eeprom_image)
{
  unsigned long i,j,k;
  char signatures[][EWRK3_NAME_LENGTH] = EWRK3_SIGNATURE;

  strcpy(name, "");
  for (i=0;*signatures[i] != '\0' && *name == '\0';i++) {
    for (j=EEPROM_PNAME7,k=0;j<=EEPROM_PNAME0 && k<strlen(signatures[i]);j++) {
      if (signatures[i][k] == eeprom_image[j]) {          /* track signature */
	k++;
      } else {                         /* lost signature; begin search again */
	k=0;
      }
    }
    if (k == strlen(signatures[i])) {
      for (k=0; k<EWRK3_NAME_LENGTH; k++) {
	name[k] = eeprom_image[EEPROM_PNAME7 + k];
	name[EWRK3_NAME_LENGTH] = '\0';
      }
    }
  }

  return;                                   /* return the device name string */
}

/*
** Look for a special sequence in the Ethernet station address PROM that
** is common across all EWRK3 products.
*/

static int DevicePresent(short iobase)
{
  static short fp=1,sigLength=0;
  static char devSig[] = PROBE_SEQUENCE;
  char data;
  int i, j, status = 0;
  static char asc2hex(char value);

/* 
** Convert the ascii signature to a hex equivalent & pack in place 
*/
  if (fp) {                               /* only do this once!... */
    for (i=0,j=0;devSig[i] != '\0' && !status;i+=2,j++) {
      if ((devSig[i]=asc2hex(devSig[i]))>=0) {
	devSig[i]<<=4;
	if((devSig[i+1]=asc2hex(devSig[i+1]))>=0){
	  devSig[j]=devSig[i]+devSig[i+1];
	} else {
	  status= -1;
	}
      } else {
	status= -1;
      }
    }
    sigLength=j;
    fp = 0;
  }

/* 
** Search the Ethernet address ROM for the signature. Since the ROM address
** counter can start at an arbitrary point, the search must include the entire
** probe sequence length plus the (length_of_the_signature - 1).
** Stop the search IMMEDIATELY after the signature is found so that the
** PROM address counter is correctly positioned at the start of the
** ethernet address for later read out.
*/
  if (!status) {
    for (i=0,j=0;j<sigLength && i<PROBE_LENGTH+sigLength-1;i++) {
      data = inb(EWRK3_APROM);
      if (devSig[j] == data) {    /* track signature */
	j++;
      } else {                    /* lost signature; begin search again */
	j=0;
      }
    }

    if (j!=sigLength) {
      status = -ENODEV;           /* search failed */
    }
  }

  return status;
}

static unsigned char aprom_crc(struct device *dev, unsigned char *eeprom_image, char chipType)
{
  long k;
  unsigned short j,chksum;
  unsigned char crc, lfsr, sd, status = 0;
  int iobase = dev->base_addr;

  if (chipType == LeMAC2) {
    for (crc=0x6a, j=0; j<ETH_ALEN; j++) {
      for (sd=inb(EWRK3_PAR0+j), k=0; k<8; k++, sd >>= 1) {
	lfsr = ((((crc & 0x02) >> 1) ^ (crc & 0x01)) ^ (sd & 0x01)) << 7;
	crc = (crc >> 1) + lfsr;
      }
    }
    if (crc != eeprom_image[EEPROM_PA_CRC]) status = -1;
  } else {
    for (k=0,j=0;j<3;j++) {
      k <<= 1 ;
      if (k > 0xffff) k-=0xffff;
      k += inw(EWRK3_PAR0 + (j<<1));
      if (k > 0xffff) k-=0xffff;
    }
    if (k == 0xffff) k=0;
    chksum = inb(EWRK3_APROM);
    chksum |= (inb(EWRK3_APROM)<<8);
    if (k != chksum) status = -1;
  }

  return status;
}

/*
** Perform IOCTL call functions here. Some are privileged operations and the
** effective uid is checked in those cases.
*/
static int ewrk3_ioctl(struct device *dev, struct ifreq *rq, int cmd)
{
  struct ewrk3_private *lp = (struct ewrk3_private *)dev->priv;
  struct ewrk3_ioctl *ioc = (struct ewrk3_ioctl *) &rq->ifr_data;
  int i, j, iobase = dev->base_addr, status = 0;
  unsigned char csr;
  union {
    unsigned char addr[HASH_TABLE_LEN * ETH_ALEN];
    unsigned short val[(HASH_TABLE_LEN * ETH_ALEN) >> 1];
  } tmp;

  switch(ioc->cmd) {
  case EWRK3_GET_HWADDR:             /* Get the hardware address */
    for (i=0; i<ETH_ALEN; i++) {
      tmp.addr[i] = dev->dev_addr[i];
    }
    ioc->len = ETH_ALEN;
    memcpy_tofs(ioc->data, tmp.addr, ioc->len);

    break;
  case EWRK3_SET_HWADDR:             /* Set the hardware address */
    if (suser()) {
      csr = inb(EWRK3_CSR);
      csr |= (TXD|RXD);
      outb(csr, EWRK3_CSR);                  /* Disable the TX and RX */

      memcpy_fromfs(tmp.addr,ioc->data,ETH_ALEN);
      for (i=0; i<ETH_ALEN; i++) {
	dev->dev_addr[i] = tmp.addr[i];
	outb(tmp.addr[i], EWRK3_PAR0 + i);
      }

      csr &= ~(TXD|RXD);                       /* Enable the TX and RX */
      outb(csr, EWRK3_CSR);
    } else {
      status = -EPERM;
    }

    break;
  case EWRK3_SET_PROM:               /* Set Promiscuous Mode */
    if (suser()) {
      csr = inb(EWRK3_CSR);
      csr |= PME;
      csr &= ~MCE;
      outb(csr, EWRK3_CSR);
    } else {
      status = -EPERM;
    }

    break;
  case EWRK3_CLR_PROM:               /* Clear Promiscuous Mode */
    if (suser()) {
      csr = inb(EWRK3_CSR);
      csr &= ~PME;
      outb(csr, EWRK3_CSR);
    } else {
      status = -EPERM;
    }

    break;
  case EWRK3_SAY_BOO:                /* Say "Boo!" to the kernel log file */
    printk("%s: Boo!\n", dev->name);

    break;
  case EWRK3_GET_MCA:                /* Get the multicast address table */
    while (set_bit(0, (void *)&lp->lock) != 0); /* Wait for lock to free */
    if (lp->shmem_length == IO_ONLY) {
      outb(0, EWRK3_IOPR);
      outw(PAGE0_HTE, EWRK3_PIR1);
      for (i=0; i<(HASH_TABLE_LEN >> 3); i++) {
	tmp.addr[i] = inb(EWRK3_DATA);
      }
    } else {
      outb(0, EWRK3_MPR);
      memcpy(tmp.addr, (char *)(lp->shmem_base + PAGE0_HTE), (HASH_TABLE_LEN >> 3));
    }
    ioc->len = (HASH_TABLE_LEN >> 3);
    memcpy_tofs(ioc->data, tmp.addr, ioc->len); 
    lp->lock = 0;                               /* Unlock the page register */

    break;
  case EWRK3_SET_MCA:                /* Set a multicast address */
    if (suser()) {
      if (ioc->len != HASH_TABLE_LEN) {         /* MCA changes */
	memcpy_fromfs(tmp.addr, ioc->data, ETH_ALEN * ioc->len);
      }
      set_multicast_list(dev, ioc->len, tmp.addr);
    } else {
      status = -EPERM;
    }

    break;
  case EWRK3_CLR_MCA:                /* Clear all multicast addresses */
    if (suser()) {
      set_multicast_list(dev, 0, NULL);
    } else {
      status = -EPERM;
    }

    break;
  case EWRK3_MCA_EN:                 /* Enable multicast addressing */
    if (suser()) {
      csr = inb(EWRK3_CSR);
      csr |= MCE;
      csr &= ~PME;
      outb(csr, EWRK3_CSR);
    } else {
      status = -EPERM;
    }

    break;
  case EWRK3_GET_STATS:              /* Get the driver statistics */
    cli();
    memcpy_tofs(ioc->data, &lp->pktStats, sizeof(lp->pktStats)); 
    ioc->len = EWRK3_PKT_STAT_SZ;
    sti();

    break;
  case EWRK3_CLR_STATS:              /* Zero out the driver statistics */
    if (suser()) {
      cli();
      memset(&lp->pktStats, 0, sizeof(lp->pktStats));
      sti();
    } else {
      status = -EPERM;
    }

    break;
  case EWRK3_GET_CSR:                /* Get the CSR Register contents */
    tmp.addr[0] = inb(EWRK3_CSR);
    memcpy_tofs(ioc->data, tmp.addr, 1);

    break;
  case EWRK3_SET_CSR:                /* Set the CSR Register contents */
    if (suser()) {
      memcpy_fromfs(tmp.addr, ioc->data, 1);
      outb(tmp.addr[0], EWRK3_CSR);
    } else {
      status = -EPERM;
    }

    break;
  case EWRK3_GET_EEPROM:             /* Get the EEPROM contents */
    if (suser()) {
      for (i=0; i<(EEPROM_MAX>>1); i++) {
	tmp.val[i] = (short)Read_EEPROM(iobase, i);
      }
      i = EEPROM_MAX;
      tmp.addr[i++] = inb(EWRK3_CMR);            /* Config/Management Reg. */
      for (j=0;j<ETH_ALEN;j++) {
	tmp.addr[i++] = inb(EWRK3_PAR0 + j);
      }
      ioc->len = EEPROM_MAX + 1 + ETH_ALEN;
      memcpy_tofs(ioc->data, tmp.addr, ioc->len);
    } else {
      status = -EPERM;
    }

    break;
  case EWRK3_SET_EEPROM:             /* Set the EEPROM contents */
    if (suser()) {
      memcpy_fromfs(tmp.addr, ioc->data, EEPROM_MAX);
      for (i=0; i<(EEPROM_MAX>>1); i++) {
	Write_EEPROM(tmp.val[i], iobase, i);
      }
    } else {
      status = -EPERM;
    }

    break;
  case EWRK3_GET_CMR:                /* Get the CMR Register contents */
    tmp.addr[0] = inb(EWRK3_CMR);
    memcpy_tofs(ioc->data, tmp.addr, 1);

    break;
  case EWRK3_SET_TX_CUT_THRU:        /* Set TX cut through mode */
    if (suser()) {
      lp->txc = 1;
    } else {
      status = -EPERM;
    }

    break;
  case EWRK3_CLR_TX_CUT_THRU:        /* Clear TX cut through mode */
    if (suser()) {
      lp->txc = 0;
    } else {
      status = -EPERM;
    }

    break;
  default:
    status = -EOPNOTSUPP;
  }

  return status;
}

static char asc2hex(char value)
{
  value -= 0x30;                  /* normalise to 0..9 range */
  if (value >= 0) {
    if (value > 9) {              /* but may not be 10..15 */
      value &= 0x1f;              /* make A..F & a..f be the same */
      value -= 0x07;              /* normalise to 10..15 range */
      if ((value < 0x0a) || (value > 0x0f)) { /* if outside range then... */
	value = -1;               /* ...signal error */
      }
    }
  } else {                        /* outside 0..9 range... */
    value = -1;                   /* ...signal error */
  }
  return value;                   /* return hex char or error */
}

#ifdef MODULE
char kernel_version[] = UTS_RELEASE;
static struct device thisEthwrk = {
  "        ", /* device name inserted by /linux/drivers/net/net_init.c */
  0, 0, 0, 0,
  0x300, 5,  /* I/O address, IRQ */
  0, 0, 0, NULL, ewrk3_probe };
	
	
int io=0x300;	/* <--- EDIT THESE LINES FOR YOUR CONFIGURATION */
int irq=5;	/* or use the insmod io= irq= options 		*/

int
init_module(void)
{
  thisEthwrk.base_addr=io;
  thisEthwrk.irq=irq;
  if (register_netdev(&thisEthwrk) != 0)
    return -EIO;
  return 0;
}

void
cleanup_module(void)
{
  if (MOD_IN_USE) {
    printk("%s: device busy, remove delayed\n",thisEthwrk.name);
  } else {
    unregister_netdev(&thisEthwrk);
  }
}
#endif /* MODULE */


/*
 * Local variables:
 *  kernel-compile-command: "gcc -D__KERNEL__ -I/usr/src/linux/net/inet -Wall -Wstrict-prototypes -O2 -m486 -c ewrk3.c"
 *
 *  module-compile-command: "gcc -D__KERNEL__ -DMODULE -I/usr/src/linux/net/inet -Wall -Wstrict-prototypes -O2 -m486 -c ewrk3.c"
 * End:
 */



