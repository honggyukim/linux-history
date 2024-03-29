#ifndef _KBD_KERN_H
#define _KBD_KERN_H

#include <linux/interrupt.h>
#include <linux/keyboard.h>

extern char *func_table[MAX_NR_FUNC];
extern char func_buf[];
extern char *funcbufptr;
extern int funcbufsize, funcbufleft;

/*
 * kbd->xxx contains the VC-local things (flag settings etc..)
 *
 * Note: externally visible are LED_SCR, LED_NUM, LED_CAP defined in kd.h
 *       The code in KDGETLED / KDSETLED depends on the internal and
 *       external order being the same.
 *
 * Note: lockstate is used as index in the array key_map.
 */
struct kbd_struct {

	unsigned char lockstate;
/* 8 modifiers - the names do not have any meaning at all;
   they can be associated to arbitrarily chosen keys */
#define VC_SHIFTLOCK	KG_SHIFT	/* shift lock mode */
#define VC_ALTGRLOCK	KG_ALTGR	/* altgr lock mode */
#define VC_CTRLLOCK	KG_CTRL 	/* control lock mode */
#define VC_ALTLOCK	KG_ALT  	/* alt lock mode */
#define VC_SHIFTLLOCK	KG_SHIFTL	/* shiftl lock mode */
#define VC_SHIFTRLOCK	KG_SHIFTR	/* shiftr lock mode */
#define VC_CTRLLLOCK	KG_CTRLL 	/* ctrll lock mode */
#define VC_CTRLRLOCK	KG_CTRLR 	/* ctrlr lock mode */

	unsigned char ledmode:2; 	/* one 2-bit value */
#define LED_SHOW_FLAGS 0        /* traditional state */
#define LED_SHOW_IOCTL 1        /* only change leds upon ioctl */
#define LED_SHOW_MEM 2          /* `heartbeat': peek into memory */

	unsigned char ledflagstate:3;	/* flags, not lights */
	unsigned char default_ledflagstate:3;
#define VC_SCROLLOCK	0	/* scroll-lock mode */
#define VC_NUMLOCK	1	/* numeric lock mode */
#define VC_CAPSLOCK	2	/* capslock mode */

	unsigned char kbdmode:2;	/* one 2-bit value */
#define VC_XLATE	0	/* translate keycodes using keymap */
#define VC_MEDIUMRAW	1	/* medium raw (keycode) mode */
#define VC_RAW		2	/* raw (scancode) mode */
#define VC_UNICODE	3	/* Unicode mode */

	unsigned char modeflags:5;
#define VC_APPLIC	0	/* application key mode */
#define VC_CKMODE	1	/* cursor key mode */
#define VC_REPEAT	2	/* keyboard repeat */
#define VC_CRLF		3	/* 0 - enter sends CR, 1 - enter sends CRLF */
#define VC_META		4	/* 0 - meta, 1 - meta=prefix with ESC */
};

extern struct kbd_struct kbd_table[];

extern unsigned long kbd_init(unsigned long);

extern unsigned char getledstate(void);
extern void setledstate(struct kbd_struct *kbd, unsigned int led);

extern inline void set_leds(void)
{
       /* con_init calls (indirectly) set_leds before kbd_init
          has been called; ignore these early calls */
       if (bh_base[KEYBOARD_BH].routine)
               mark_bh(KEYBOARD_BH);
}

extern inline int vc_kbd_mode(struct kbd_struct * kbd, int flag)
{
	return ((kbd->modeflags >> flag) & 1);
}

extern inline int vc_kbd_led(struct kbd_struct * kbd, int flag)
{
	return ((kbd->ledflagstate >> flag) & 1);
}

extern inline void set_vc_kbd_mode(struct kbd_struct * kbd, int flag)
{
	kbd->modeflags |= 1 << flag;
}

extern inline void set_vc_kbd_led(struct kbd_struct * kbd, int flag)
{
	kbd->ledflagstate |= 1 << flag;
}

extern inline void clr_vc_kbd_mode(struct kbd_struct * kbd, int flag)
{
	kbd->modeflags &= ~(1 << flag);
}

extern inline void clr_vc_kbd_led(struct kbd_struct * kbd, int flag)
{
	kbd->ledflagstate &= ~(1 << flag);
}

extern inline void chg_vc_kbd_lock(struct kbd_struct * kbd, int flag)
{
	kbd->lockstate ^= 1 << flag;
}

extern inline void chg_vc_kbd_mode(struct kbd_struct * kbd, int flag)
{
	kbd->modeflags ^= 1 << flag;
}

extern inline void chg_vc_kbd_led(struct kbd_struct * kbd, int flag)
{
	kbd->ledflagstate ^= 1 << flag;
}

#define U(x) ((x) ^ 0xf000)

#endif
