# File: drivers/net/Makefile
#
# Makefile for the Linux network (ethercard) device drivers.
#

# This will go away in some future future: hidden configuration files
# are difficult for users to deal with.
include CONFIG
include MODULES

NETDRV_OBJS := Space.o auto_irq.o net_init.o loopback.o
CFLAGS := $(CFLAGS) -I../../net/inet
CPP := $(CPP) -I../../net/inet

.c.o:
	$(CC) $(CFLAGS) -c $<


# The point of the makefile...
all: net.a

Space.o: Space.c ../../include/linux/autoconf.h
	$(CC) $(CFLAGS) $(OPTS) -c $<

net_init.o: ../../include/linux/autoconf.h

ifdef CONFIG_SK_G16
NETDRV_OBJS := $(NETDRV_OBJS) sk_g16.o
endif

ifdef CONFIG_WD80x3
NETDRV_OBJS := $(NETDRV_OBJS) wd.o
CONFIG_8390 = CONFIG_8390
wd.o:	wd.c CONFIG
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WD_OPTS) -c $<
endif

ifdef CONFIG_EL2
NETDRV_OBJS := $(NETDRV_OBJS) 3c503.o
CONFIG_8390 = CONFIG_8390
3c503.o:	3c503.c CONFIG
	$(CC) $(CPPFLAGS) $(CFLAGS) $(EL2_OPTS) -c $<
endif

ifdef CONFIG_NE2000
NETDRV_OBJS := $(NETDRV_OBJS) ne.o
CONFIG_8390 = CONFIG_8390
ne.o:	ne.c CONFIG
	$(CC) $(CPPFLAGS) $(CFLAGS) $(NE_OPTS) -c $<
endif

ifdef CONFIG_HPLAN
NETDRV_OBJS := $(NETDRV_OBJS) hp.o
CONFIG_8390 = CONFIG_8390
hp.o:	hp.c CONFIG
	$(CC) $(CPPFLAGS) $(CFLAGS) $(HP_OPTS) -c $<
endif

ifdef CONFIG_HPLAN_PLUS
NETDRV_OBJS := $(NETDRV_OBJS) hp-plus.o
CONFIG_8390 = CONFIG_8390
endif

ifdef CONFIG_ULTRA
NETDRV_OBJS := $(NETDRV_OBJS) smc-ultra.o
CONFIG_8390 = CONFIG_8390
endif

ifdef CONFIG_E2100
NETDRV_OBJS := $(NETDRV_OBJS) e2100.o
CONFIG_8390 = CONFIG_8390
endif

ifdef CONFIG_PLIP
NETDRV_OBJS := $(NETDRV_OBJS) plip.o
plip.o:	plip.c CONFIG
	$(CC) $(CPPFLAGS) $(CFLAGS) $(PLIP_OPTS) -c $<
endif

ifdef CONFIG_PPP
NETDRV_OBJS := $(NETDRV_OBJS) ppp.o slhc.o
endif

ifdef CONFIG_SLIP
NETDRV_OBJS := $(NETDRV_OBJS) slip.o slhc.o
slip.o:	slip.c CONFIG
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $<
endif

ifdef CONFIG_DE650
NETDRV_OBJS := $(NETDRV_OBJS) de650.o
CONFIG_8390 = CONFIG_8390
endif
ifdef CONFIG_3C589
NETDRV_OBJS := $(NETDRV_OBJS) 3c589.o
endif

ifdef CONFIG_DUMMY
NETDRV_OBJS := $(NETDRV_OBJS) dummy.o
dummy.o: dummy.c CONFIG
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $<
endif

ifdef CONFIG_DE600
NETDRV_OBJS := $(NETDRV_OBJS) de600.o
endif
de600.o: de600.c CONFIG
	$(CC) $(CPPFLAGS) $(CFLAGS) $(DE600_OPTS) -c $<
	
ifdef CONFIG_DE620
NETDRV_OBJS := $(NETDRV_OBJS) de620.o
endif
de620.o: de620.c CONFIG
	$(CC) $(CPPFLAGS) $(CFLAGS) $(DE620_OPTS) -c $<
	
ifdef CONFIG_AT1500
NETDRV_OBJS := $(NETDRV_OBJS) lance.o
endif
ifdef CONFIG_LANCE
NETDRV_OBJS := $(NETDRV_OBJS) lance.o
endif
ifdef CONFIG_AT1700
NETDRV_OBJS := $(NETDRV_OBJS) at1700.o
endif
ifdef CONFIG_EL1
NETDRV_OBJS := $(NETDRV_OBJS) 3c501.o
endif
ifdef CONFIG_EL16
NETDRV_OBJS := $(NETDRV_OBJS) 3c507.o
endif
ifdef CONFIG_EL3
NETDRV_OBJS := $(NETDRV_OBJS) 3c509.o
endif
ifdef CONFIG_EEXPRESS
NETDRV_OBJS := $(NETDRV_OBJS) eexpress.o
endif
ifdef CONFIG_ZNET
NETDRV_OBJS := $(NETDRV_OBJS) znet.o
endif
ifdef CONFIG_DEPCA
NETDRV_OBJS := $(NETDRV_OBJS) depca.o
depca.o: depca.c CONFIG
	$(CC) $(CPPFLAGS) $(CFLAGS) $(DEPCA_OPTS) -c $<
endif
ifdef CONFIG_EWRK3
NETDRV_OBJS := $(NETDRV_OBJS) ewrk3.o
ewrk3.o: ewrk3.c CONFIG
	$(CC) $(CPPFLAGS) $(CFLAGS) $(EWRK3_OPTS) -c $<
endif
ifdef CONFIG_ATP
NETDRV_OBJS := $(NETDRV_OBJS) atp.o
endif
ifdef CONFIG_NI52
NETDRV_OBJS := $(NETDRV_OBJS) ni52.o
endif
ifdef CONFIG_NI65
NETDRV_OBJS := $(NETDRV_OBJS) ni65.o
endif
ifdef CONFIG_ELPLUS
NETDRV_OBJS := $(NETDRV_OBJS) 3c505.o
endif
ifdef CONFIG_AC3200
NETDRV_OBJS := $(NETDRV_OBJS) ac3200.o
CONFIG_8390 = CONFIG_8390
endif
ifdef CONFIG_APRICOT
NETDRV_OBJS := $(NETDRV_OBJS) apricot.o
endif

ifdef CONFIG_8390
NETDRV_OBJS := $(NETDRV_OBJS) 8390.o
endif

ifdef CONFIG_ARCNET
NETDRV_OBJS := $(NETDRV_OBJS) arcnet.o
endif

ifdef CONFIG_PI
NETDRV_OBJS := $(NETDRV_OBJS) pi2.o
CONFIG_PI = CONFIG_PI
pi2.o:  pi2.c CONFIG
	$(CC) $(CPPFLAGS) $(CFLAGS) $(PI_OPTS) -c $<
endif

net.a: $(NETDRV_OBJS)
	rm -f net.a
	ar rcs net.a $(NETDRV_OBJS)

clean:
	rm -f core *.o *.a *.s

dep:
	$(CPP) -M *.c > .depend

tar:

ifdef MODULES

modules: $(MODULES)
	echo $(MODULES) > ../../modules/NET_MODULES
	cd ../../modules; \
		for i in $(MODULES); do ln -sf ../drivers/net/$$i .; done

else

modules:

endif

# include a dependency file if one exists

ifeq (.depend,$(wildcard .depend))
include .depend
endif
