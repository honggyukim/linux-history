#
# Makefile for the kernel character device drivers.
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#
# Note 2! The CFLAGS definitions are now inherited from the
# parent makes..
#

.c.s:
	$(CC) $(CFLAGS) -S $<
.s.o:
	$(AS) -c -o $*.o $<
.c.o:
	$(CC) $(CFLAGS) -c $<

OBJS  = tty_io.o n_tty.o console.o keyboard.o serial.o \
	tty_ioctl.o pty.o vt.o mem.o \
	defkeymap.o uni_to_437.o vesa_blank.o

SRCS  = tty_io.c n_tty.c console.c keyboard.c serial.c \
	tty_ioctl.c pty.c vt.c mem.c \
	defkeymap.c uni_to_437.c vesa_blank.c


ifdef CONFIG_CYCLADES
OBJS := $(OBJS) cyclades.o
SRCS := $(SRCS) cyclades.c
endif

ifdef CONFIG_ATIXL_BUSMOUSE
M = y
OBJS := $(OBJS) atixlmouse.o
SRCS := $(SRCS) atixlmouse.c
endif

ifdef CONFIG_BUSMOUSE
M = y
OBJS := $(OBJS) busmouse.o
SRCS := $(SRCS) busmouse.c
endif

ifdef CONFIG_PRINTER
OBJS := $(OBJS) lp.o
SRCS := $(SRCS) lp.c
endif

ifdef CONFIG_MS_BUSMOUSE
M = y
OBJS := $(OBJS) msbusmouse.o
SRCS := $(SRCS) msbusmouse.c
endif

ifdef CONFIG_82C710_MOUSE
CONFIG_PSMOUSE = CONFIG_PSMOUSE
endif

ifdef CONFIG_PSMOUSE
M = y
OBJS := $(OBJS) psaux.o
SRCS := $(SRCS) psaux.c
endif

ifdef CONFIG_QIC02_TAPE
OBJS := $(OBJS) tpqic02.o 
SRCS := $(SRCS) tpqic02.c
endif

ifdef M
OBJS := $(OBJS) mouse.o
SRCS := $(SRCS) mouse.c
endif

MODULES := lp.o

all: char.a

char.a: $(OBJS)
	$(AR) rcs char.a $(OBJS)
	sync	

ifdef MODULES

modules: $(MODULES)
	(cd ../../modules;for i in $(MODULES); do ln -sf ../drivers/char/$$i .; done)

else

modules:

endif

dep:
	$(CPP) -M $(SRCS) > .depend

dummy:

#
# include a dependency file if one exists
#
ifeq (.depend,$(wildcard .depend))
include .depend
endif
