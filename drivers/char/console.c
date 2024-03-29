/*
 *  linux/drivers/char/console.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 */

/*
 *	console.c
 *
 * This module exports the console io functions:
 * 
 *     'void do_keyboard_interrupt(void)'
 *
 *     'int vc_allocate(unsigned int console)' 
 *     'int vc_cons_allocated(unsigned int console)'
 *     'int vc_resize(unsigned long lines, unsigned long cols)'
 *     'void vc_disallocate(unsigned int currcons)'
 *
 *     'long con_init(long)'
 *     'int con_open(struct tty_struct *tty, struct file * filp)'
 *     'void con_write(struct tty_struct * tty)'
 *     'void console_print(const char * b)'
 *     'void update_screen(int new_console)'
 *
 *     'void do_blank_screen(int)'
 *     'void do_unblank_screen(void)'
 *     'void poke_blanked_console(void)'
 *     'void scrollback(int lines)'
 *     'void scrollfront(int lines)'
 *     'int do_screendump(int arg, int mode)'
 *
 *     'int con_get_font(char *)' 
 *     'int con_set_font(char *)' 
 *     'int con_get_trans(char *)'
 *     'int con_set_trans(char *)'
 *
 *     'int set_selection(const int arg)'
 *     'int paste_selection(struct tty_struct *tty)'
 *     'int sel_loadlut(const int arg)'
 *     'int mouse_reporting(void)'
 * 
 * Hopefully this will be a rather complete VT102 implementation.
 *
 * Beeping thanks to John T Kohl.
 * 
 * Virtual Consoles, Screen Blanking, Screen Dumping, Color, Graphics
 *   Chars, and VT100 enhancements by Peter MacDonald.
 *
 * Copy and paste function by Andrew Haylett,
 *   some enhancements by Alessandro Rubini.
 *
 * User definable mapping table and font loading by Eugene G. Crosser,
 * <crosser@pccross.msk.su>
 *
 * Code to check for different video-cards mostly by Galen Hunt,
 * <g-hunt@ee.utah.edu>
 *
 * Rudimentary ISO 10646/Unicode/UTF-8 character set support by
 * Markus Kuhn, <mskuhn@immd4.informatik.uni-erlangen.de>.
 *
 * Dynamic allocation of consoles, aeb@cwi.nl, May 1994
 * Resizing of consoles, aeb, 940926
 *
 * Code for xterm like mouse click reporting by Peter Orbaek 20-Jul-94
 * <poe@daimi.aau.dk>
 *
 */

#define BLANK 0x0020
#define CAN_LOAD_EGA_FONTS    /* undefine if the user must not do this */

/*
 *  NOTE!!! We sometimes disable and enable interrupts for a short while
 * (to put a word in video IO), but this will work even for keyboard
 * interrupts. We know interrupts aren't enabled when getting a keyboard
 * interrupt, as we use trap-gates. Hopefully all is well.
 */

#include <linux/sched.h>
#include <linux/timer.h>
#include <linux/interrupt.h>
#include <linux/tty.h>
#include <linux/tty_flip.h>
#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/kd.h>
#include <linux/malloc.h>
#include <linux/major.h>

#include <asm/io.h>
#include <asm/system.h>
#include <asm/segment.h>
#include <asm/bitops.h>

#include "kbd_kern.h"
#include "vt_kern.h"

#ifndef MIN
#define MIN(a,b)	((a) < (b) ? (a) : (b))
#endif

struct tty_driver console_driver;
static int console_refcount;
static struct tty_struct *console_table[MAX_NR_CONSOLES];
static struct termios *console_termios[MAX_NR_CONSOLES];
static struct termios *console_termios_locked[MAX_NR_CONSOLES];

#ifdef CONFIG_SELECTION
#include <linux/ctype.h>

/* Routines for selection control. */
int set_selection(const int arg, struct tty_struct *tty);
int paste_selection(struct tty_struct *tty);
static void clear_selection(void);
static void highlight_pointer(const int currcons, const int where);

/* Variables for selection control. */
/* Use a dynamic buffer, instead of static (Dec 1994) */
static int sel_cons = 0;
static int sel_start = -1;
static int sel_end;
static char *sel_buffer = NULL;
#endif /* CONFIG_SELECTION */

#define NPAR 16

static void con_setsize(unsigned long rows, unsigned long cols);
static void vc_init(unsigned int console, unsigned long rows, unsigned long cols,
		    int do_clear);
static void get_scrmem(int currcons);
static void set_scrmem(int currcons, long offset);
static void set_origin(int currcons);
static void blank_screen(void);
static void unblank_screen(void);
void poke_blanked_console(void);
static void gotoxy(int currcons, int new_x, int new_y);
static void save_cur(int currcons);
static inline void set_cursor(int currcons);
static void reset_terminal(int currcons, int do_clear);
extern void reset_vc(unsigned int new_console);
extern void vt_init(void);
extern void register_console(void (*proc)(const char *));
extern void vesa_blank(void);
extern void vesa_unblank(void);
extern void compute_shiftstate(void);
extern int conv_uni_to_pc(unsigned long ucs);

/* Description of the hardware situation */
static unsigned char	video_type;		/* Type of display being used	*/
static unsigned long	video_mem_base;		/* Base of video memory		*/
static unsigned long	video_mem_term;		/* End of video memory		*/
static unsigned char	video_page;		/* Initial video page (unused)  */
       unsigned short	video_port_reg;		/* Video register select port	*/
       unsigned short	video_port_val;		/* Video register value port	*/
static unsigned long	video_num_columns;	/* Number of text columns	*/
static unsigned long	video_num_lines;	/* Number of text lines		*/
static unsigned long	video_size_row;
static unsigned long	video_screen_size;
static int can_do_color = 0;
static int printable = 0;			/* Is console ready for printing? */

static unsigned short *vc_scrbuf[MAX_NR_CONSOLES];

static int console_blanked = 0;
static int blankinterval = 10*60*HZ;
static long blank_origin, blank__origin, unblank_origin;

struct vc_data {
	unsigned long	vc_screenbuf_size;
	unsigned short	vc_video_erase_char;	/* Background erase character */
	unsigned char	vc_attr;		/* Current attributes */
	unsigned char	vc_def_color;		/* Default colors */
	unsigned char	vc_color;		/* Foreground & background */
	unsigned char	vc_s_color;		/* Saved foreground & background */
	unsigned char	vc_ulcolor;		/* Colour for underline mode */
	unsigned char	vc_halfcolor;		/* Colour for half intensity mode */
	unsigned long	vc_origin;		/* Used for EGA/VGA fast scroll	*/
	unsigned long	vc_scr_end;		/* Used for EGA/VGA fast scroll	*/
	unsigned long	vc_pos;
	unsigned long	vc_x,vc_y;
	unsigned long	vc_top,vc_bottom;
	unsigned long	vc_state;
	unsigned long	vc_npar,vc_par[NPAR];
	unsigned long	vc_video_mem_start;	/* Start of video RAM		*/
	unsigned long	vc_video_mem_end;	/* End of video RAM (sort of)	*/
	unsigned long	vc_saved_x;
	unsigned long	vc_saved_y;
	/* mode flags */
	unsigned long	vc_charset	: 1;	/* Character set G0 / G1 */
	unsigned long	vc_s_charset	: 1;	/* Saved character set */
	unsigned long	vc_disp_ctrl	: 1;	/* Display chars < 32? */
	unsigned long	vc_toggle_meta	: 1;	/* Toggle high bit? */
	unsigned long	vc_decscnm	: 1;	/* Screen Mode */
	unsigned long	vc_decom	: 1;	/* Origin Mode */
	unsigned long	vc_decawm	: 1;	/* Autowrap Mode */
	unsigned long	vc_deccm	: 1;	/* Cursor Visible */
	unsigned long	vc_decim	: 1;	/* Insert Mode */
	unsigned long	vc_deccolm	: 1;	/* 80/132 Column Mode */
	/* attribute flags */
	unsigned long	vc_intensity	: 2;	/* 0=half-bright, 1=normal, 2=bold */
	unsigned long	vc_underline	: 1;
	unsigned long	vc_blink	: 1;
	unsigned long	vc_reverse	: 1;
	unsigned long	vc_s_intensity	: 2;	/* saved rendition */
	unsigned long	vc_s_underline	: 1;
	unsigned long	vc_s_blink	: 1;
	unsigned long	vc_s_reverse	: 1;
	/* misc */
	unsigned long	vc_ques		: 1;
	unsigned long	vc_need_wrap	: 1;
	unsigned long	vc_has_scrolled : 1;	/* Info for unblank_screen */
	unsigned long	vc_kmalloced	: 1;	/* kfree_s() needed */
	unsigned long	vc_report_mouse : 2;
	unsigned char	vc_utf		: 1;	/* Unicode UTF-8 encoding */
	unsigned char	vc_utf_count;
	unsigned long	vc_utf_char;
	unsigned long	vc_tab_stop[5];		/* Tab stops. 160 columns. */
	unsigned char * vc_translate;
	unsigned char *	vc_G0_charset;
	unsigned char *	vc_G1_charset;
	unsigned char *	vc_saved_G0;
	unsigned char *	vc_saved_G1;
	/* additional information is in vt_kern.h */
};

static struct vc {
	struct vc_data *d;

	/* might add  scrmem, vt_struct, kbd  at some time,
	   to have everything in one place - the disadvantage
	   would be that vc_cons etc can no longer be static */
} vc_cons [MAX_NR_CONSOLES];

#define screenbuf_size	(vc_cons[currcons].d->vc_screenbuf_size)
#define origin		(vc_cons[currcons].d->vc_origin)
#define scr_end		(vc_cons[currcons].d->vc_scr_end)
#define pos		(vc_cons[currcons].d->vc_pos)
#define top		(vc_cons[currcons].d->vc_top)
#define bottom		(vc_cons[currcons].d->vc_bottom)
#define x		(vc_cons[currcons].d->vc_x)
#define y		(vc_cons[currcons].d->vc_y)
#define vc_state	(vc_cons[currcons].d->vc_state)
#define npar		(vc_cons[currcons].d->vc_npar)
#define par		(vc_cons[currcons].d->vc_par)
#define ques		(vc_cons[currcons].d->vc_ques)
#define attr		(vc_cons[currcons].d->vc_attr)
#define saved_x		(vc_cons[currcons].d->vc_saved_x)
#define saved_y		(vc_cons[currcons].d->vc_saved_y)
#define translate	(vc_cons[currcons].d->vc_translate)
#define G0_charset	(vc_cons[currcons].d->vc_G0_charset)
#define G1_charset	(vc_cons[currcons].d->vc_G1_charset)
#define saved_G0	(vc_cons[currcons].d->vc_saved_G0)
#define saved_G1	(vc_cons[currcons].d->vc_saved_G1)
#define utf		(vc_cons[currcons].d->vc_utf)
#define utf_count	(vc_cons[currcons].d->vc_utf_count)
#define utf_char	(vc_cons[currcons].d->vc_utf_char)
#define video_mem_start	(vc_cons[currcons].d->vc_video_mem_start)
#define video_mem_end	(vc_cons[currcons].d->vc_video_mem_end)
#define video_erase_char (vc_cons[currcons].d->vc_video_erase_char)	
#define disp_ctrl	(vc_cons[currcons].d->vc_disp_ctrl)
#define toggle_meta	(vc_cons[currcons].d->vc_toggle_meta)
#define decscnm		(vc_cons[currcons].d->vc_decscnm)
#define decom		(vc_cons[currcons].d->vc_decom)
#define decawm		(vc_cons[currcons].d->vc_decawm)
#define deccm		(vc_cons[currcons].d->vc_deccm)
#define decim		(vc_cons[currcons].d->vc_decim)
#define deccolm	 	(vc_cons[currcons].d->vc_deccolm)
#define need_wrap	(vc_cons[currcons].d->vc_need_wrap)
#define has_scrolled	(vc_cons[currcons].d->vc_has_scrolled)
#define kmalloced	(vc_cons[currcons].d->vc_kmalloced)
#define report_mouse	(vc_cons[currcons].d->vc_report_mouse)
#define color		(vc_cons[currcons].d->vc_color)
#define s_color		(vc_cons[currcons].d->vc_s_color)
#define def_color	(vc_cons[currcons].d->vc_def_color)
#define	foreground	(color & 0x0f)
#define background	(color & 0xf0)
#define charset		(vc_cons[currcons].d->vc_charset)
#define s_charset	(vc_cons[currcons].d->vc_s_charset)
#define	intensity	(vc_cons[currcons].d->vc_intensity)
#define	underline	(vc_cons[currcons].d->vc_underline)
#define	blink		(vc_cons[currcons].d->vc_blink)
#define	reverse		(vc_cons[currcons].d->vc_reverse)
#define	s_intensity	(vc_cons[currcons].d->vc_s_intensity)
#define	s_underline	(vc_cons[currcons].d->vc_s_underline)
#define	s_blink		(vc_cons[currcons].d->vc_s_blink)
#define	s_reverse	(vc_cons[currcons].d->vc_s_reverse)
#define	ulcolor		(vc_cons[currcons].d->vc_ulcolor)
#define	halfcolor	(vc_cons[currcons].d->vc_halfcolor)
#define tab_stop	(vc_cons[currcons].d->vc_tab_stop)

#define vcmode		(vt_cons[currcons]->vc_mode)
#define structsize	(sizeof(struct vc_data) + sizeof(struct vt_struct))

static void memsetw(void * s, unsigned short c, unsigned int count)
{
	unsigned short * addr = (unsigned short *) s;

	count /= 2;
	while (count) {
		count--;
		*(addr++) = c;
	}
}

int vc_cons_allocated(unsigned int i)
{
	return (i < MAX_NR_CONSOLES && vc_cons[i].d);
}

int vc_allocate(unsigned int i)		/* return 0 on success */
{
	if (i >= MAX_NR_CONSOLES)
	  return -ENODEV;
	if (!vc_cons[i].d) {
	    long p, q;

	    /* prevent users from taking too much memory */
	    if (i >= MAX_NR_USER_CONSOLES && !suser())
	      return -EPERM;

	    /* due to the granularity of kmalloc, we waste some memory here */
	    /* the alloc is done in two steps, to optimize the common situation
	       of a 25x80 console (structsize=216, video_screen_size=4000) */
	    q = (long) kmalloc(video_screen_size, GFP_KERNEL);
	    if (!q)
	      return -ENOMEM;
	    p = (long) kmalloc(structsize, GFP_KERNEL);
	    if (!p) {
		kfree_s((char *) q, video_screen_size);
		return -ENOMEM;
	    }

	    vc_cons[i].d = (struct vc_data *) p;
	    p += sizeof(struct vc_data);
	    vt_cons[i] = (struct vt_struct *) p;
	    vc_scrbuf[i] = (unsigned short *) q;
	    vc_cons[i].d->vc_kmalloced = 1;
	    vc_cons[i].d->vc_screenbuf_size = video_screen_size;
	    vc_init (i, video_num_lines, video_num_columns, 1);
	}
	return 0;
}

/*
 * Change # of rows and columns (0 means unchanged)
 * [this is to be used together with some user program
 * like resize that changes the hardware videomode]
 */
int vc_resize(unsigned long lines, unsigned long cols)
{
	unsigned long cc, ll, ss, sr;
	unsigned long occ, oll, oss, osr;
	unsigned short *p;
	unsigned int currcons, i;
	unsigned short *newscreens[MAX_NR_CONSOLES];
	long ol, nl, rlth, rrem;

	cc = (cols ? cols : video_num_columns);
	ll = (lines ? lines : video_num_lines);
	sr = cc << 1;
	ss = sr * ll;

	if (ss > video_mem_term - video_mem_base)
	  return -ENOMEM;

	/*
	 * Some earlier version had all consoles of potentially
	 * different sizes, but that was really messy.
	 * So now we only change if there is room for all consoles
	 * of the same size.
	 */
	for (currcons = 0; currcons < MAX_NR_CONSOLES; currcons++) {
	    if (!vc_cons_allocated(currcons))
	      newscreens[currcons] = 0;
	    else {
		p = (unsigned short *) kmalloc(ss, GFP_USER);
		if (!p) {
		    for (i = 0; i< currcons; i++)
		      if (newscreens[i])
			kfree_s(newscreens[i], ss);
		    return -ENOMEM;
		}
		newscreens[currcons] = p;
	    }
	}

	get_scrmem(fg_console);

	oll = video_num_lines;
	occ = video_num_columns;
	osr = video_size_row;
	oss = video_screen_size;

	video_num_lines = ll;
	video_num_columns = cc;
	video_size_row = sr;
	video_screen_size = ss;

	for (currcons = 0; currcons < MAX_NR_CONSOLES; currcons++) {
	    if (!vc_cons_allocated(currcons))
	      continue;

	    rlth = MIN(osr, sr);
	    rrem = sr - rlth;
	    ol = origin;
	    nl = (long) newscreens[currcons];
	    if (ll < oll)
	      ol += (oll - ll) * osr;

	    while (ol < scr_end) {
		memcpy((void *) nl, (void *) ol, rlth);
		if (rrem)
		  memsetw((void *)(nl + rlth), video_erase_char, rrem);
		ol += osr;
		nl += sr;
	    }

	    if (kmalloced)
	      kfree_s(vc_scrbuf[currcons], screenbuf_size);
	    vc_scrbuf[currcons] = newscreens[currcons];
	    kmalloced = 1;
	    screenbuf_size = ss;

	    origin = video_mem_start = (long) vc_scrbuf[currcons];
	    scr_end = video_mem_end = video_mem_start + ss;

	    if (scr_end > nl)
	      memsetw((void *) nl, video_erase_char, scr_end - nl);

	    /* do part of a reset_terminal() */
	    top = 0;
	    bottom = video_num_lines;
	    gotoxy(currcons, x, y);
	    save_cur(currcons);
	}

	set_scrmem(fg_console, 0);
	set_origin(fg_console);
	set_cursor(fg_console);

	return 0;
}

void vc_disallocate(unsigned int currcons)
{
	if (vc_cons_allocated(currcons)) {
	    if (kmalloced)
	      kfree_s(vc_scrbuf[currcons], screenbuf_size);
	    if (currcons >= MIN_NR_CONSOLES)
	      kfree_s(vc_cons[currcons].d, structsize);
	    vc_cons[currcons].d = 0;
	}
}


#define set_kbd(x) set_vc_kbd_mode(kbd_table+currcons,x)
#define clr_kbd(x) clr_vc_kbd_mode(kbd_table+currcons,x)
#define is_kbd(x) vc_kbd_mode(kbd_table+currcons,x)

#define decarm		VC_REPEAT
#define decckm		VC_CKMODE
#define kbdapplic	VC_APPLIC
#define lnm		VC_CRLF

/*
 * this is what the terminal answers to a ESC-Z or csi0c query.
 */
#define VT100ID "\033[?1;2c"
#define VT102ID "\033[?6c"

static unsigned char * translations[] = {
/* 8-bit Latin-1 mapped to the PC character set: '\0' means non-printable */
(unsigned char *)
	"\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0"
	"\0\0\0\0\0\0\0\0\0\0\376\0\0\0\0\0"
	" !\"#$%&'()*+,-./0123456789:;<=>?"
	"@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_"
	"`abcdefghijklmnopqrstuvwxyz{|}~\0"
	"\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0"
	"\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0"
	"\377\255\233\234\376\235\174\025\376\376\246\256\252\055\376\376"
	"\370\361\375\376\376\346\024\371\376\376\247\257\254\253\376\250"
	"\376\376\376\376\216\217\222\200\376\220\376\376\376\376\376\376"
	"\376\245\376\376\376\376\231\376\350\376\376\376\232\376\376\341"
	"\205\240\203\376\204\206\221\207\212\202\210\211\215\241\214\213"
	"\376\244\225\242\223\376\224\366\355\227\243\226\201\376\376\230",
/* vt100 graphics */
(unsigned char *)
	"\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0"
	"\0\0\0\0\0\0\0\0\0\0\376\0\0\0\0\0"
	" !\"#$%&'()*+,-./0123456789:;<=>?"
	"@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^ "
	"\004\261\007\007\007\007\370\361\007\007\331\277\332\300\305\304"
	"\304\304\137\137\303\264\301\302\263\363\362\343\330\234\007\0"
	"\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0"
	"\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0"
	"\377\255\233\234\376\235\174\025\376\376\246\256\252\055\376\376"
	"\370\361\375\376\376\346\024\371\376\376\247\257\254\253\376\250"
	"\376\376\376\376\216\217\222\200\376\220\376\376\376\376\376\376"
	"\376\245\376\376\376\376\231\376\376\376\376\376\232\376\376\341"
	"\205\240\203\376\204\206\221\207\212\202\210\211\215\241\214\213"
	"\376\244\225\242\223\376\224\366\376\227\243\226\201\376\376\230",
/* IBM graphics: minimal translations (BS, CR, LF, LL, SO, SI and ESC) */
(unsigned char *)
	"\000\001\002\003\004\005\006\007\000\011\000\013\000\000\000\000"
	"\020\021\022\023\024\025\026\027\030\031\032\000\034\035\036\037"
	"\040\041\042\043\044\045\046\047\050\051\052\053\054\055\056\057"
	"\060\061\062\063\064\065\066\067\070\071\072\073\074\075\076\077"
	"\100\101\102\103\104\105\106\107\110\111\112\113\114\115\116\117"
	"\120\121\122\123\124\125\126\127\130\131\132\133\134\135\136\137"
	"\140\141\142\143\144\145\146\147\150\151\152\153\154\155\156\157"
	"\160\161\162\163\164\165\166\167\170\171\172\173\174\175\176\177"
	"\200\201\202\203\204\205\206\207\210\211\212\213\214\215\216\217"
	"\220\221\222\223\224\225\226\227\230\231\232\233\234\235\236\237"
	"\240\241\242\243\244\245\246\247\250\251\252\253\254\255\256\257"
	"\260\261\262\263\264\265\266\267\270\271\272\273\274\275\276\277"
	"\300\301\302\303\304\305\306\307\310\311\312\313\314\315\316\317"
	"\320\321\322\323\324\325\326\327\330\331\332\333\334\335\336\337"
	"\340\341\342\343\344\345\346\347\350\351\352\353\354\355\356\357"
	"\360\361\362\363\364\365\366\367\370\371\372\373\374\375\376\377",
 /* USER: customizable mappings, initialized as the previous one (IBM) */
(unsigned char *)
	"\000\001\002\003\004\005\006\007\010\011\000\013\000\000\016\017"
	"\020\021\022\023\024\025\026\027\030\031\032\000\034\035\036\037"
	"\040\041\042\043\044\045\046\047\050\051\052\053\054\055\056\057"
	"\060\061\062\063\064\065\066\067\070\071\072\073\074\075\076\077"
	"\100\101\102\103\104\105\106\107\110\111\112\113\114\115\116\117"
	"\120\121\122\123\124\125\126\127\130\131\132\133\134\135\136\137"
	"\140\141\142\143\144\145\146\147\150\151\152\153\154\155\156\157"
	"\160\161\162\163\164\165\166\167\170\171\172\173\174\175\176\177"
	"\200\201\202\203\204\205\206\207\210\211\212\213\214\215\216\217"
	"\220\221\222\223\224\225\226\227\230\231\232\233\234\235\236\237"
	"\240\241\242\243\244\245\246\247\250\251\252\253\254\255\256\257"
	"\260\261\262\263\264\265\266\267\270\271\272\273\274\275\276\277"
	"\300\301\302\303\304\305\306\307\310\311\312\313\314\315\316\317"
	"\320\321\322\323\324\325\326\327\330\331\332\333\334\335\336\337"
	"\340\341\342\343\344\345\346\347\350\351\352\353\354\355\356\357"
	"\360\361\362\363\364\365\366\367\370\371\372\373\374\375\376\377"
};

#define NORM_TRANS (translations[0])
#define GRAF_TRANS (translations[1])
#define NULL_TRANS (translations[2])
#define USER_TRANS (translations[3])

static unsigned char color_table[] = { 0, 4, 2, 6, 1, 5, 3, 7,
				       8,12,10,14, 9,13,11,15 };

/*
 * gotoxy() must verify all boundaries, because the arguments
 * might also be negative. If the given position is out of
 * bounds, the cursor is placed at the nearest margin.
 */
static void gotoxy(int currcons, int new_x, int new_y)
{
	int max_y;

	if (new_x < 0)
		x = 0;
	else
		if (new_x >= video_num_columns)
			x = video_num_columns - 1;
		else
			x = new_x;
 	if (decom) {
		new_y += top;
		max_y = bottom;
	} else
		max_y = video_num_lines;
	if (new_y < 0)
		y = 0;
	else
		if (new_y >= max_y)
			y = max_y - 1;
		else
			y = new_y;
	pos = origin + y*video_size_row + (x<<1);
	need_wrap = 0;
}

/*
 * *Very* limited hardware scrollback support..
 */
static unsigned short __real_origin;
static unsigned short __origin;		/* offset of currently displayed screen */

static inline void __set_origin(unsigned short offset)
{
	unsigned long flags;
#ifdef CONFIG_SELECTION
	clear_selection();
#endif /* CONFIG_SELECTION */
	save_flags(flags); cli();
	__origin = offset;
	outb_p(12, video_port_reg);
	outb_p(offset >> 8, video_port_val);
	outb_p(13, video_port_reg);
	outb_p(offset, video_port_val);
	restore_flags(flags);
}

void scrollback(int lines)
{
	if (!lines)
		lines = video_num_lines/2;
	lines *= video_num_columns;
	lines = __origin - lines;
	if (lines < 0)
		lines = 0;
	__set_origin(lines);
}

void scrollfront(int lines)
{
	if (!lines)
		lines = video_num_lines/2;
	lines *= video_num_columns;
	lines = __origin + lines;
	if (lines > __real_origin)
		lines = __real_origin;
	__set_origin(lines);
}

static void set_origin(int currcons)
{
	if (video_type != VIDEO_TYPE_EGAC && video_type != VIDEO_TYPE_EGAM)
		return;
	if (currcons != fg_console || console_blanked || vcmode == KD_GRAPHICS)
		return;
	__real_origin = (origin-video_mem_base) >> 1;
	__set_origin(__real_origin);
}

/*
 * Put the cursor just beyond the end of the display adaptor memory.
 */
static inline void hide_cursor(void)
{
  /* This is inefficient, we could just put the cursor at 0xffff,
     but perhaps the delays due to the inefficiency are useful for
     some hardware... */
	outb_p(14, video_port_reg);
	outb_p(0xff&((video_mem_term-video_mem_base)>>9), video_port_val);
	outb_p(15, video_port_reg);
	outb_p(0xff&((video_mem_term-video_mem_base)>>1), video_port_val);
}

static inline void set_cursor(int currcons)
{
	unsigned long flags;

	if (currcons != fg_console || console_blanked || vcmode == KD_GRAPHICS)
		return;
	if (__real_origin != __origin)
		__set_origin(__real_origin);
	save_flags(flags); cli();
	if (deccm) {
		outb_p(14, video_port_reg);
		outb_p(0xff&((pos-video_mem_base)>>9), video_port_val);
		outb_p(15, video_port_reg);
		outb_p(0xff&((pos-video_mem_base)>>1), video_port_val);
	} else
		hide_cursor();
	restore_flags(flags);
}

static void scrup(int currcons, unsigned int t, unsigned int b)
{
	int hardscroll = 1;

	if (b > video_num_lines || t >= b)
		return;
	if (video_type != VIDEO_TYPE_EGAC && video_type != VIDEO_TYPE_EGAM)
		hardscroll = 0;
	else if (t || b != video_num_lines)
		hardscroll = 0;
	if (hardscroll) {
		origin += video_size_row;
		pos += video_size_row;
		scr_end += video_size_row;
		if (scr_end > video_mem_end) {
			unsigned short * d = (unsigned short *) video_mem_start;
			unsigned short * s = (unsigned short *) origin;
			unsigned int count;

			count = (video_num_lines-1)*video_num_columns;
			while (count) {
				count--;
				*(d++) = *(s++);
			}
			count = video_num_columns;
			while (count) {
				count--;
				*(d++) = video_erase_char;
			}
			scr_end -= origin-video_mem_start;
			pos -= origin-video_mem_start;
			origin = video_mem_start;
			has_scrolled = 1;
		} else {
			unsigned short * d;
			unsigned int count;

			d = (unsigned short *) (scr_end - video_size_row);
			count = video_num_columns;
			while (count) {
				count--;
				*(d++) = video_erase_char;
			}
		}
		set_origin(currcons);
	} else {
		unsigned short * d = (unsigned short *) (origin+video_size_row*t);
		unsigned short * s = (unsigned short *) (origin+video_size_row*(t+1));
		unsigned int count = (b-t-1) * video_num_columns;

		while (count) {
			count--;
			*(d++) = *(s++);
		}
		count = video_num_columns;
		while (count) {
			count--;
			*(d++) = video_erase_char;
		}
	}
}

static void scrdown(int currcons, unsigned int t, unsigned int b)
{
	unsigned short *d, *s;
	unsigned int count;

	if (b > video_num_lines || t >= b)
		return;
	d = (unsigned short *) (origin+video_size_row*b);
	s = (unsigned short *) (origin+video_size_row*(b-1));
	count = (b-t-1)*video_num_columns;
	while (count) {
		count--;
		*(--d) = *(--s);
	}
	count = video_num_columns;
	while (count) {
		count--;
		*(--d) = video_erase_char;
	}
	has_scrolled = 1;
}

static void lf(int currcons)
{
    	/* don't scroll if above bottom of scrolling region, or
	 * if below scrolling region
	 */
    	if (y+1 == bottom)
		scrup(currcons,top,bottom);
	else if (y < video_num_lines-1) {
	    	y++;
		pos += video_size_row;
	}
	need_wrap = 0;
}

static void ri(int currcons)
{
    	/* don't scroll if below top of scrolling region, or
	 * if above scrolling region
	 */
	if (y == top)
		scrdown(currcons,top,bottom);
	else if (y > 0) {
		y--;
		pos -= video_size_row;
	}
	need_wrap = 0;
}

static inline void cr(int currcons)
{
	pos -= x<<1;
	need_wrap = x = 0;
}

static inline void bs(int currcons)
{
	if (x) {
		pos -= 2;
		x--;
		need_wrap = 0;
	}
}

static inline void del(int currcons)
{
	/* ignored */
}

static void csi_J(int currcons, int vpar)
{
	unsigned long count;
	unsigned short * start;

	switch (vpar) {
		case 0:	/* erase from cursor to end of display */
			count = (scr_end-pos)>>1;
			start = (unsigned short *) pos;
			break;
		case 1:	/* erase from start to cursor */
			count = ((pos-origin)>>1)+1;
			start = (unsigned short *) origin;
			break;
		case 2: /* erase whole display */
			count = video_num_columns * video_num_lines;
			start = (unsigned short *) origin;
			break;
		default:
			return;
	}
	while (count) {
		count--;
		*(start++) = video_erase_char;
	}
	need_wrap = 0;
}

static void csi_K(int currcons, int vpar)
{
	unsigned long count;
	unsigned short * start;

	switch (vpar) {
		case 0:	/* erase from cursor to end of line */
			count = video_num_columns-x;
			start = (unsigned short *) pos;
			break;
		case 1:	/* erase from start of line to cursor */
			start = (unsigned short *) (pos - (x<<1));
			count = x+1;
			break;
		case 2: /* erase whole line */
			start = (unsigned short *) (pos - (x<<1));
			count = video_num_columns;
			break;
		default:
			return;
	}
	while (count) {
		count--;
		*(start++) = video_erase_char;
	}
	need_wrap = 0;
}

static void csi_X(int currcons, int vpar) /* erase the following vpar positions */
{					  /* not vt100? */
	unsigned long count;
	unsigned short * start;

	if (!vpar)
		vpar++;

	start = (unsigned short *) pos;
	count = (vpar > video_num_columns-x) ? (video_num_columns-x) : vpar;

	while (count) {
		count--;
		*(start++) = video_erase_char;
	}
	need_wrap = 0;
}

static void update_attr(int currcons)
{
	attr = color;
	if (can_do_color) {
		if (underline)
			attr = (attr & 0xf0) | ulcolor;
		else if (intensity == 0)
			attr = (attr & 0xf0) | halfcolor;
	}
	if (reverse ^ decscnm)
		attr = (attr & 0x88) | (((attr >> 4) | (attr << 4)) & 0x77);
	if (blink)
		attr ^= 0x80;
	if (intensity == 2)
		attr ^= 0x08;
	if (!can_do_color) {
		if (underline)
			attr = (attr & 0xf8) | 0x01;
		else if (intensity == 0)
			attr = (attr & 0xf0) | 0x08;
	}
	if (decscnm)
		video_erase_char = (((color & 0x88) | (((color >> 4) | (color << 4)) & 0x77)) << 8) | ' ';
	else
		video_erase_char = (color << 8) | ' ';
}

static void default_attr(int currcons)
{
	intensity = 1;
	underline = 0;
	reverse = 0;
	blink = 0;
	color = def_color;
}

static void csi_m(int currcons)
{
	int i;

	for (i=0;i<=npar;i++)
		switch (par[i]) {
			case 0:	/* all attributes off */
				default_attr(currcons);
				break;
			case 1:
				intensity = 2;
				break;
			case 2:
				intensity = 0;
				break;
			case 4:
				underline = 1;
				break;
			case 5:
				blink = 1;
				break;
			case 7:
				reverse = 1;
				break;
			case 10: /* ANSI X3.64-1979 (SCO-ish?)
				  * Select primary font, don't display
				  * control chars if defined, don't set
				  * bit 8 on output.
				  */
				translate = (charset == 0
						? G0_charset
						: G1_charset);
				disp_ctrl = 0;
				toggle_meta = 0;
				break;
			case 11: /* ANSI X3.64-1979 (SCO-ish?)
				  * Select first alternate font, let's
				  * chars < 32 be displayed as ROM chars.
				  */
				translate = NULL_TRANS;
				disp_ctrl = 1;
				toggle_meta = 0;
				break;
			case 12: /* ANSI X3.64-1979 (SCO-ish?)
				  * Select second alternate font, toggle
				  * high bit before displaying as ROM char.
				  */
				translate = NULL_TRANS;
				disp_ctrl = 1;
				toggle_meta = 1;
				break;
			case 21:
			case 22:
				intensity = 1;
				break;
			case 24:
				underline = 0;
				break;
			case 25:
				blink = 0;
				break;
			case 27:
				reverse = 0;
				break;
			case 38: /* ANSI X3.64-1979 (SCO-ish?)
				  * Enables underscore, white foreground
				  * with white underscore (Linux - use
				  * default foreground).
				  */
				color = (def_color & 0x0f) | background;
				underline = 1;
				break;
			case 39: /* ANSI X3.64-1979 (SCO-ish?)
				  * Disable underline option.
				  * Reset colour to default? It did this
				  * before...
				  */
				color = (def_color & 0x0f) | background;
				underline = 0;
				break;
			case 49:
				color = (def_color & 0xf0) | foreground;
				break;
			default:
				if (par[i] >= 30 && par[i] <= 37)
					color = color_table[par[i]-30]
						| background; 
				else if (par[i] >= 40 && par[i] <= 47)
					color = (color_table[par[i]-40]<<4)
						| foreground;
				break;
		}
	update_attr(currcons);
}

static void respond_string(char * p, struct tty_struct * tty)
{
	while (*p) {
		tty_insert_flip_char(tty, *p, 0);
		p++;
	}
	tty_schedule_flip(tty);
}

static void cursor_report(int currcons, struct tty_struct * tty)
{
	char buf[40];

	sprintf(buf, "\033[%ld;%ldR", y + (decom ? top+1 : 1), x+1);
	respond_string(buf, tty);
}

#ifdef CONFIG_SELECTION
static void mouse_report(int currcons, struct tty_struct * tty,
			 int butt, int mrx, int mry)
{
	char buf[8];

	sprintf(buf, "\033[M%c%c%c", (char)(' ' + butt), (char)('!' + mrx),
		(char)('!' + mry));
	respond_string(buf, tty);
}
#endif

static inline void status_report(int currcons, struct tty_struct * tty)
{
	respond_string("\033[0n", tty);	/* Terminal ok */
}

static inline void respond_ID(int currcons, struct tty_struct * tty)
{
	respond_string(VT102ID, tty);
}

static void invert_screen(int currcons) {
	unsigned char *p;

	if (can_do_color)
		for (p = (unsigned char *)origin+1; p < (unsigned char *)scr_end; p+=2)
			*p = (*p & 0x88) | (((*p >> 4) | (*p << 4)) & 0x77);
	else
		for (p = (unsigned char *)origin+1; p < (unsigned char *)scr_end; p+=2)
			*p ^= *p & 0x07 == 1 ? 0x70 : 0x77;
}

static void set_mode(int currcons, int on_off)
{
	int i;

	for (i=0; i<=npar; i++)
		if (ques) switch(par[i]) {	/* DEC private modes set/reset */
			case 1:			/* Cursor keys send ^[Ox/^[[x */
				if (on_off)
					set_kbd(decckm);
				else
					clr_kbd(decckm);
				break;
			case 3:	/* 80/132 mode switch unimplemented */
				deccolm = on_off;
#if 0
				(void) vc_resize(video_num_lines, deccolm ? 132 : 80);
				/* this alone does not suffice; some user mode
				   utility has to change the hardware regs */
#endif
				break;
			case 5:			/* Inverted screen on/off */
				if (decscnm != on_off) {
					decscnm = on_off;
					invert_screen(currcons);
					update_attr(currcons);
				}
				break;
			case 6:			/* Origin relative/absolute */
				decom = on_off;
				gotoxy(currcons,0,0);
				break;
			case 7:			/* Autowrap on/off */
				decawm = on_off;
				break;
			case 8:			/* Autorepeat on/off */
				if (on_off)
					set_kbd(decarm);
				else
					clr_kbd(decarm);
				break;
			case 9:
				report_mouse = on_off ? 1 : 0;
				break;
			case 25:		/* Cursor on/off */
				deccm = on_off;
				set_cursor(currcons);
				break;
			case 1000:
				report_mouse = on_off ? 2 : 0;
				break;
		} else switch(par[i]) {		/* ANSI modes set/reset */
			case 4:			/* Insert Mode on/off */
				decim = on_off;
				break;
			case 20:		/* Lf, Enter == CrLf/Lf */
				if (on_off)
					set_kbd(lnm);
				else
					clr_kbd(lnm);
				break;
		}
}

static void setterm_command(int currcons)
{
	switch(par[0]) {
		case 1:	/* set color for underline mode */
			if (can_do_color && par[1] < 16) {
				ulcolor = color_table[par[1]];
				if (underline)
					update_attr(currcons);
			}
			break;
		case 2:	/* set color for half intensity mode */
			if (can_do_color && par[1] < 16) {
				halfcolor = color_table[par[1]];
				if (intensity == 0)
					update_attr(currcons);
			}
			break;
		case 8:	/* store colors as defaults */
			def_color = attr;
			default_attr(currcons);
			update_attr(currcons);
			break;
		case 9:	/* set blanking interval */
			blankinterval = ((par[1] < 60) ? par[1] : 60) * 60 * HZ;
			poke_blanked_console();
			break;
	}
}

static void insert_char(int currcons)
{
	unsigned int i = x;
	unsigned short tmp, old = video_erase_char;
	unsigned short * p = (unsigned short *) pos;

	while (i++ < video_num_columns) {
		tmp = *p;
		*p = old;
		old = tmp;
		p++;
	}
	need_wrap = 0;
}

static void insert_line(int currcons)
{
	scrdown(currcons,y,bottom);
	need_wrap = 0;
}

static void delete_char(int currcons)
{
	unsigned int i = x;
	unsigned short * p = (unsigned short *) pos;

	while (++i < video_num_columns) {
		*p = *(p+1);
		p++;
	}
	*p = video_erase_char;
	need_wrap = 0;
}

static void delete_line(int currcons)
{
	scrup(currcons,y,bottom);
	need_wrap = 0;
}

static void csi_at(int currcons, unsigned int nr)
{
	if (nr > video_num_columns)
		nr = video_num_columns;
	else if (!nr)
		nr = 1;
	while (nr--)
		insert_char(currcons);
}

static void csi_L(int currcons, unsigned int nr)
{
	if (nr > video_num_lines)
		nr = video_num_lines;
	else if (!nr)
		nr = 1;
	while (nr--)
		insert_line(currcons);
}

static void csi_P(int currcons, unsigned int nr)
{
	if (nr > video_num_columns)
		nr = video_num_columns;
	else if (!nr)
		nr = 1;
	while (nr--)
		delete_char(currcons);
}

static void csi_M(int currcons, unsigned int nr)
{
	if (nr > video_num_lines)
		nr = video_num_lines;
	else if (!nr)
		nr=1;
	while (nr--)
		delete_line(currcons);
}

static void save_cur(int currcons)
{
	saved_x		= x;
	saved_y		= y;
	s_intensity	= intensity;
	s_underline	= underline;
	s_blink		= blink;
	s_reverse	= reverse;
	s_charset	= charset;
	s_color		= color;
	saved_G0	= G0_charset;
	saved_G1	= G1_charset;
}

static void restore_cur(int currcons)
{
	gotoxy(currcons,saved_x,saved_y);
	intensity	= s_intensity;
	underline	= s_underline;
	blink		= s_blink;
	reverse		= s_reverse;
	charset		= s_charset;
	color		= s_color;
	G0_charset	= saved_G0;
	G1_charset	= saved_G1;
	translate	= charset ? G1_charset : G0_charset;
	update_attr(currcons);
	need_wrap = 0;
}

enum { ESnormal, ESesc, ESsquare, ESgetpars, ESgotpars, ESfunckey, 
	EShash, ESsetG0, ESsetG1, ESpercent, ESignore };

static void reset_terminal(int currcons, int do_clear)
{
	top		= 0;
	bottom		= video_num_lines;
	vc_state	= ESnormal;
	ques		= 0;
	translate	= NORM_TRANS;
	G0_charset	= NORM_TRANS;
	G1_charset	= GRAF_TRANS;
	charset		= 0;
	need_wrap	= 0;
	report_mouse	= 0;
	utf             = 0;
	utf_count       = 0;

	disp_ctrl	= 1;
	toggle_meta	= 0;

	decscnm		= 0;
	decom		= 0;
	decawm		= 1;
	deccm		= 1;
	decim		= 0;

	set_kbd(decarm);
	clr_kbd(decckm);
	clr_kbd(kbdapplic);
	clr_kbd(lnm);
	kbd_table[currcons].lockstate = 0;
	kbd_table[currcons].ledmode = LED_SHOW_FLAGS;
	kbd_table[currcons].ledflagstate = kbd_table[currcons].default_ledflagstate;
	set_leds();

	default_attr(currcons);
	update_attr(currcons);

	tab_stop[0]	= 0x01010100;
	tab_stop[1]	=
	tab_stop[2]	=
	tab_stop[3]	=
	tab_stop[4]	= 0x01010101;

	gotoxy(currcons,0,0);
	save_cur(currcons);
	if (do_clear)
	    csi_J(currcons,2);
}

/*
 * Turn the Scroll-Lock LED on when the tty is stopped
 */
static void con_stop(struct tty_struct *tty)
{
	int console_num;
	if (!tty)
		return;
	console_num = MINOR(tty->device) - (tty->driver.minor_start);
	if (!vc_cons_allocated(console_num))
		return;
	set_vc_kbd_led(kbd_table + console_num, VC_SCROLLOCK);
	set_leds();
}

/*
 * Turn the Scroll-Lock LED off when the console is started
 */
static void con_start(struct tty_struct *tty)
{
	int console_num;
	if (!tty)
		return;
	console_num = MINOR(tty->device) - (tty->driver.minor_start);
	if (!vc_cons_allocated(console_num))
		return;
	clr_vc_kbd_led(kbd_table + console_num, VC_SCROLLOCK);
	set_leds();
}

static int con_write(struct tty_struct * tty, int from_user,
		     unsigned char *buf, int count)
{
	int c, tc, ok, n = 0;
	unsigned int currcons;
	struct vt_struct *vt = (struct vt_struct *)tty->driver_data;

	currcons = vt->vc_num;
	if (!vc_cons_allocated(currcons)) {
	    /* could this happen? */
	    static int error = 0;
	    if (!error) {
		error = 1;
		printk("con_write: tty %d not allocated\n", currcons+1);
	    }
	    return 0;
	}
#ifdef CONFIG_SELECTION
	/* clear the selection */
	if (currcons == sel_cons)
		clear_selection();
#endif /* CONFIG_SELECTION */
	disable_bh(KEYBOARD_BH);
	while (!tty->stopped &&	count) {
		c = from_user ? get_fs_byte(buf) : *buf;
		buf++; n++; count--;

		if (utf) {
		    /* Combine UTF-8 into Unicode */
		    /* Incomplete characters silently ignored */
		    if(c > 0x7f) {   
			/* UTF-8 to Latin-1 decoding */
			if (utf_count > 0 && (c & 0xc0) == 0x80) {
				utf_char = (utf_char << 6) | (c & 0x3f);
				utf_count--;
				if (utf_count == 0)
				    c = utf_char;
				else continue;
			} else {
				if ((c & 0xe0) == 0xc0) {
				    utf_count = 1;
				    utf_char = (c & 0x1f);
				} else if ((c & 0xf0) == 0xe0) {
				    utf_count = 2;
				    utf_char = (c & 0x0f);
				} else
				    utf_count = 0;
				continue;
			}
		    } else
			utf_count = 0;

		    /* Now try to find out how to display it */
		    if (c > 0xff) {
			tc = conv_uni_to_pc(c);
			if (tc == -2)
			  continue;
			vc_state = ESnormal;
			if (tc == -1)
			  tc = 0376; 	/* small square: symbol not found */
			ok = 1;
		    } else {
			tc = NORM_TRANS[c];
			ok = 0;
		    }
		} else {	/* no utf */
		    tc = translate[toggle_meta ? (c|0x80) : c];
		    ok = 0;
		}

		/* Can print ibm (even if 0), and latin1 provided
		   it is a printing char or control chars are printed ^@ */
		if (!ok && tc && (c >= 32 || (disp_ctrl && (c&0x7f) != 27)))
		    ok = 1;

		if (vc_state == ESnormal && ok) {
			if (need_wrap) {
				cr(currcons);
				lf(currcons);
			}
			if (decim)
				insert_char(currcons);
			*(unsigned short *) pos = (attr << 8) + tc;
			if (x == video_num_columns - 1)
				need_wrap = decawm;
			else {
				x++;
				pos+=2;
			}
			continue;
		}

		/*
		 *  Control characters can be used in the _middle_
		 *  of an escape sequence.
		 */
		switch (c) {
			case 7:
				kd_mksound(0x637, HZ/8);
				continue;
			case 8:
				bs(currcons);
				continue;
			case 9:
				pos -= (x << 1);
				while (x < video_num_columns - 1) {
					x++;
					if (tab_stop[x >> 5] & (1 << (x & 31)))
						break;
				}
				pos += (x << 1);
				continue;
			case 10: case 11: case 12:
				lf(currcons);
				if (!is_kbd(lnm))
					continue;
			case 13:
				cr(currcons);
				continue;
			case 14:
				charset = 1;
				translate = G1_charset;
				continue;
			case 15:
				charset = 0;
				translate = G0_charset;
				continue;
			case 24: case 26:
				vc_state = ESnormal;
				continue;
			case 27:
				vc_state = ESesc;
				continue;
			case 127:
				del(currcons);
				continue;
			case 128+27:
				vc_state = ESsquare;
				continue;
		}
		switch(vc_state) {
			case ESesc:
				vc_state = ESnormal;
				switch (c) {
				  case '[':
					vc_state = ESsquare;
					continue;
				  case '%':
					vc_state = ESpercent;
					continue;
				  case 'E':
					cr(currcons);
					lf(currcons);
					continue;
				  case 'M':
					ri(currcons);
					continue;
				  case 'D':
					lf(currcons);
					continue;
				  case 'H':
					tab_stop[x >> 5] |= (1 << (x & 31));
					continue;
				  case 'Z':
					respond_ID(currcons,tty);
					continue;
				  case '7':
					save_cur(currcons);
					continue;
				  case '8':
					restore_cur(currcons);
					continue;
				  case '(':
					vc_state = ESsetG0;
					continue;
				  case ')':
					vc_state = ESsetG1;
					continue;
				  case '#':
					vc_state = EShash;
					continue;
				  case 'c':
					reset_terminal(currcons,1);
					continue;
				  case '>':  /* Numeric keypad */
					clr_kbd(kbdapplic);
					continue;
				  case '=':  /* Appl. keypad */
					set_kbd(kbdapplic);
				 	continue;
				}	
				continue;
			case ESsquare:
				for(npar = 0 ; npar < NPAR ; npar++)
					par[npar] = 0;
				npar = 0;
				vc_state = ESgetpars;
				if (c == '[') { /* Function key */
					vc_state=ESfunckey;
					continue;
				}
				ques = (c=='?');
				if (ques)
					continue;
			case ESgetpars:
				if (c==';' && npar<NPAR-1) {
					npar++;
					continue;
				} else if (c>='0' && c<='9') {
					par[npar] *= 10;
					par[npar] += c-'0';
					continue;
				} else vc_state=ESgotpars;
			case ESgotpars:
				vc_state = ESnormal;
				switch(c) {
					case 'h':
						set_mode(currcons,1);
						continue;
					case 'l':
						set_mode(currcons,0);
						continue;
					case 'n':
						if (!ques)
							if (par[0] == 5)
								status_report(currcons,tty);
							else if (par[0] == 6)
								cursor_report(currcons,tty);
						continue;
				}
				if (ques) {
					ques = 0;
					continue;
				}
				switch(c) {
					case 'G': case '`':
						if (par[0]) par[0]--;
						gotoxy(currcons,par[0],y);
						continue;
					case 'A':
						if (!par[0]) par[0]++;
						gotoxy(currcons,x,y-par[0]);
						continue;
					case 'B': case 'e':
						if (!par[0]) par[0]++;
						gotoxy(currcons,x,y+par[0]);
						continue;
					case 'C': case 'a':
						if (!par[0]) par[0]++;
						gotoxy(currcons,x+par[0],y);
						continue;
					case 'D':
						if (!par[0]) par[0]++;
						gotoxy(currcons,x-par[0],y);
						continue;
					case 'E':
						if (!par[0]) par[0]++;
						gotoxy(currcons,0,y+par[0]);
						continue;
					case 'F':
						if (!par[0]) par[0]++;
						gotoxy(currcons,0,y-par[0]);
						continue;
					case 'd':
						if (par[0]) par[0]--;
						gotoxy(currcons,x,par[0]);
						continue;
					case 'H': case 'f':
						if (par[0]) par[0]--;
						if (par[1]) par[1]--;
						gotoxy(currcons,par[1],par[0]);
						continue;
					case 'J':
						csi_J(currcons,par[0]);
						continue;
					case 'K':
						csi_K(currcons,par[0]);
						continue;
					case 'L':
						csi_L(currcons,par[0]);
						continue;
					case 'M':
						csi_M(currcons,par[0]);
						continue;
					case 'P':
						csi_P(currcons,par[0]);
						continue;
					case 'c':
						if (!par[0])
							respond_ID(currcons,tty);
						continue;
					case 'g':
						if (!par[0])
							tab_stop[x >> 5] &= ~(1 << (x & 31));
						else if (par[0] == 3) {
							tab_stop[0] =
							tab_stop[1] =
							tab_stop[2] =
							tab_stop[3] =
							tab_stop[4] = 0;
						}
						continue;
					case 'm':
						csi_m(currcons);
						continue;
					case 'q': /* DECLL - but only 3 leds */
						/* map 0,1,2,3 to 0,1,2,4 */
						if (par[0] < 4)
						  setledstate(kbd_table + currcons,
							      (par[0] < 3) ? par[0] : 4);
						continue;
					case 'r':
						if (!par[0])
							par[0]++;
						if (!par[1])
							par[1] = video_num_lines;
						/* Minimum allowed region is 2 lines */
						if (par[0] < par[1] &&
						    par[1] <= video_num_lines) {
							top=par[0]-1;
							bottom=par[1];
							gotoxy(currcons,0,0);
						}
						continue;
					case 's':
						save_cur(currcons);
						continue;
					case 'u':
						restore_cur(currcons);
						continue;
					case 'X':
						csi_X(currcons, par[0]);
						continue;
					case '@':
						csi_at(currcons,par[0]);
						continue;
					case ']': /* setterm functions */
						setterm_command(currcons);
						continue;
				}
				continue;
			case ESpercent:
				vc_state = ESnormal;
				switch (c) {
				  case '@':  /* defined in ISO 2022 */
					utf = 0;
					continue;
				  case '8':
					/* ISO/ECMA hasn't yet registered an
					   official ESC sequence for UTF-8,
					   so this one (ESC %8) will likely
					   change in the future. */
					utf = 1;
					continue;
				}
				continue;
			case ESfunckey:
				vc_state = ESnormal;
				continue;
			case EShash:
				vc_state = ESnormal;
				if (c == '8') {
					/* DEC screen alignment test. kludge :-) */
					video_erase_char =
						(video_erase_char & 0xff00) | 'E';
					csi_J(currcons, 2);
					video_erase_char =
						(video_erase_char & 0xff00) | ' ';
				}
				continue;
			case ESsetG0:
				if (c == '0')
					G0_charset = GRAF_TRANS;
				else if (c == 'B')
					G0_charset = NORM_TRANS;
				else if (c == 'U')
					G0_charset = NULL_TRANS;
				else if (c == 'K')
					G0_charset = USER_TRANS;
				if (charset == 0)
					translate = G0_charset;
				vc_state = ESnormal;
				continue;
			case ESsetG1:
				if (c == '0')
					G1_charset = GRAF_TRANS;
				else if (c == 'B')
					G1_charset = NORM_TRANS;
				else if (c == 'U')
					G1_charset = NULL_TRANS;
				else if (c == 'K')
					G1_charset = USER_TRANS;
				if (charset == 1)
					translate = G1_charset;
				vc_state = ESnormal;
				continue;
			default:
				vc_state = ESnormal;
		}
	}
	if (vcmode != KD_GRAPHICS)
		set_cursor(currcons);
	enable_bh(KEYBOARD_BH);
	return n;
}

static int con_write_room(struct tty_struct *tty)
{
	if (tty->stopped)
		return 0;
	return 4096;		/* No limit, really; we're not buffering */
}

static int con_chars_in_buffer(struct tty_struct *tty)
{
	return 0;		/* we're not buffering */
}

void poke_blanked_console(void)
{
	timer_active &= ~(1<<BLANK_TIMER);
	if (vt_cons[fg_console]->vc_mode == KD_GRAPHICS)
		return;
	if (console_blanked) {
		timer_table[BLANK_TIMER].expires = 0;
		timer_active |= 1<<BLANK_TIMER;
	} else if (blankinterval) {
		timer_table[BLANK_TIMER].expires = jiffies + blankinterval;
		timer_active |= 1<<BLANK_TIMER;
	}
}

void console_print(const char * b)
{
	int currcons = fg_console;
	unsigned char c;
	static int printing = 0;

	if (!printable || printing)
		return;	 /* console not yet initialized */
	printing = 1;

	if (!vc_cons_allocated(currcons)) {
		/* impossible */
		printk("console_print: tty %d not allocated ??\n", currcons+1);
		return;
	}

	while ((c = *(b++)) != 0) {
		if (c == 10 || c == 13 || need_wrap) {
			if (c != 13)
				lf(currcons);
			cr(currcons);
			if (c == 10 || c == 13)
				continue;
		}
		*(unsigned short *) pos = (attr << 8) + c;
		if (x == video_num_columns - 1) {
			need_wrap = 1;
			continue;
		}
		x++;
		pos+=2;
	}
	set_cursor(currcons);
	poke_blanked_console();
	printing = 0;
}

/*
 * con_throttle and con_unthrottle are only used for
 * paste_selection(), which has to stuff in a large number of
 * characters...
 */
static void con_throttle(struct tty_struct *tty)
{
}

static void con_unthrottle(struct tty_struct *tty)
{
	struct vt_struct *vt = (struct vt_struct *) tty->driver_data;

	wake_up_interruptible(&vt->paste_wait);
}

static void vc_init(unsigned int currcons, unsigned long rows, unsigned long cols, int do_clear)
{
	long base = (long) vc_scrbuf[currcons];

	video_num_columns = cols;
	video_num_lines = rows;
	video_size_row = cols<<1;
	video_screen_size = video_num_lines * video_size_row;

	pos = origin = video_mem_start = base;
	scr_end = base + video_screen_size;
	video_mem_end = base + video_screen_size;
	reset_vc(currcons);
	def_color       = 0x07;   /* white */
	ulcolor		= 0x0f;   /* bold white */
	halfcolor       = 0x08;   /* grey */
	vt_cons[currcons]->paste_wait = 0;
	reset_terminal(currcons, do_clear);
}

static void con_setsize(unsigned long rows, unsigned long cols)
{
	video_num_lines = rows;
	video_num_columns = cols;
	video_size_row = 2 * cols;
	video_screen_size = video_num_lines * video_size_row;
}

/*
 *  long con_init(long);
 *
 * This routine initializes console interrupts, and does nothing
 * else. If you want the screen to clear, call tty_write with
 * the appropriate escape-sequence.
 *
 * Reads the information preserved by setup.s to determine the current display
 * type and sets everything accordingly.
 */
long con_init(long kmem_start)
{
	char *display_desc = "????";
	int currcons = 0;
	int orig_x = ORIG_X;
	int orig_y = ORIG_Y;

	memset(&console_driver, 0, sizeof(struct tty_driver));
	console_driver.magic = TTY_DRIVER_MAGIC;
	console_driver.name = "tty";
	console_driver.name_base = 1;
	console_driver.major = TTY_MAJOR;
	console_driver.minor_start = 1;
	console_driver.num = MAX_NR_CONSOLES;
	console_driver.type = TTY_DRIVER_TYPE_CONSOLE;
	console_driver.init_termios = tty_std_termios;
	console_driver.flags = TTY_DRIVER_REAL_RAW;
	console_driver.refcount = &console_refcount;
	console_driver.table = console_table;
	console_driver.termios = console_termios;
	console_driver.termios_locked = console_termios_locked;

	console_driver.open = con_open;
	console_driver.write = con_write;
	console_driver.write_room = con_write_room;
	console_driver.chars_in_buffer = con_chars_in_buffer;
	console_driver.ioctl = vt_ioctl;
	console_driver.stop = con_stop;
	console_driver.start = con_start;
	console_driver.throttle = con_throttle;
	console_driver.unthrottle = con_unthrottle;
	
	if (tty_register_driver(&console_driver))
		panic("Couldn't register console driver\n");
	
	con_setsize(ORIG_VIDEO_LINES, ORIG_VIDEO_COLS);
	video_page = ORIG_VIDEO_PAGE; 			/* never used */

	timer_table[BLANK_TIMER].fn = blank_screen;
	timer_table[BLANK_TIMER].expires = 0;
	if (blankinterval) {
		timer_table[BLANK_TIMER].expires = jiffies+blankinterval;
		timer_active |= 1<<BLANK_TIMER;
	}
	
	if (ORIG_VIDEO_MODE == 7)	/* Is this a monochrome display? */
	{
		video_mem_base = 0xb0000;
		video_port_reg = 0x3b4;
		video_port_val = 0x3b5;
		if ((ORIG_VIDEO_EGA_BX & 0xff) != 0x10)
		{
			video_type = VIDEO_TYPE_EGAM;
			video_mem_term = 0xb8000;
			display_desc = "EGA+";
		}
		else
		{
			video_type = VIDEO_TYPE_MDA;
			video_mem_term = 0xb2000;
			display_desc = "*MDA";
		}
	}
	else				/* If not, it is color. */
	{
		can_do_color = 1;
		video_mem_base = 0xb8000;
		video_port_reg	= 0x3d4;
		video_port_val	= 0x3d5;
		if ((ORIG_VIDEO_EGA_BX & 0xff) != 0x10)
		{
			video_type = VIDEO_TYPE_EGAC;
			video_mem_term = 0xc0000;
			display_desc = "EGA+";
		}
		else
		{
			video_type = VIDEO_TYPE_CGA;
			video_mem_term = 0xba000;
			display_desc = "*CGA";
		}
	}
	
	/* Initialize the variables used for scrolling (mostly EGA/VGA)	*/

	/* Due to kmalloc roundup allocating statically is more efficient -
	   so provide MIN_NR_CONSOLES for people with very little memory */
	for (currcons = 0; currcons < MIN_NR_CONSOLES; currcons++) {
		vc_cons[currcons].d = (struct vc_data *) kmem_start;
		kmem_start += sizeof(struct vc_data);
		vt_cons[currcons] = (struct vt_struct *) kmem_start;
		kmem_start += sizeof(struct vt_struct);
		vc_scrbuf[currcons] = (unsigned short *) kmem_start;
		kmem_start += video_screen_size;
		kmalloced = 0;
		screenbuf_size = video_screen_size;
		vc_init(currcons, video_num_lines, video_num_columns, currcons);
	}

	currcons = fg_console = 0;

	video_mem_start = video_mem_base;
	video_mem_end = video_mem_term;
	origin = video_mem_start;
	scr_end	= video_mem_start + video_num_lines * video_size_row;
	gotoxy(currcons,orig_x,orig_y);
	printable = 1;
	printk("Console: %s %s %ldx%ld, %d virtual console%s (max %d)\n",
		can_do_color ? "colour" : "mono",
		display_desc,
		video_num_columns,video_num_lines,
		MIN_NR_CONSOLES, (MIN_NR_CONSOLES == 1) ? "" : "s", MAX_NR_CONSOLES);
	register_console(console_print);
	return kmem_start;
}

static void get_scrmem(int currcons)
{
	memcpy((void *)vc_scrbuf[currcons], (void *)origin, video_screen_size);
	origin = video_mem_start = (unsigned long)vc_scrbuf[currcons];
	scr_end = video_mem_end = video_mem_start + video_screen_size;
	pos = origin + y*video_size_row + (x<<1);
}

static void set_scrmem(int currcons, long offset)
{
#ifdef CONFIG_HGA
  /* This works with XFree86 1.2, 1.3 and 2.0
     This code could be extended and made more generally useful if we could
     determine the actual video mode. It appears that this should be
     possible on a genuine Hercules card, but I (WM) haven't been able to
     read from any of the required registers on my clone card.
     */
	/* This code should work with Hercules and MDA cards. */
	if (video_type == VIDEO_TYPE_MDA)
	  {
	    if (vcmode == KD_TEXT)
	      {
		/* Ensure that the card is in text mode. */
		int	i;
		static char herc_txt_tbl[12] = {
		  0x61,0x50,0x52,0x0f,0x19,6,0x19,0x19,2,0x0d,0x0b,0x0c };
		outb_p(0, 0x3bf);  /* Back to power-on defaults */
		outb_p(0, 0x3b8);  /* Blank the screen, select page 0, etc */
		for ( i = 0 ; i < 12 ; i++ )
		  {
		    outb_p(i, 0x3b4);
		    outb_p(herc_txt_tbl[i], 0x3b5);
		  }
	      }
#define HGA_BLINKER_ON 0x20
#define HGA_SCREEN_ON  8
	    /* Make sure that the hardware is not blanked */
	    outb_p(HGA_BLINKER_ON | HGA_SCREEN_ON, 0x3b8);
	  }
#endif CONFIG_HGA

	if (video_mem_term - video_mem_base < offset + video_screen_size)
	  offset = 0;	/* strange ... */
	memcpy((void *)(video_mem_base + offset), (void *) origin, video_screen_size);
	video_mem_start = video_mem_base;
	video_mem_end = video_mem_term;
	origin = video_mem_base + offset;
	scr_end = origin + video_screen_size;
	pos = origin + y*video_size_row + (x<<1);
}

void do_blank_screen(int nopowersave)
{
	int currcons;

	if (console_blanked)
		return;

	timer_active &= ~(1<<BLANK_TIMER);
	timer_table[BLANK_TIMER].fn = unblank_screen;

	/* try not to lose information by blanking, and not to waste memory */
	currcons = fg_console;
	has_scrolled = 0;
	blank__origin = __origin;
	blank_origin = origin;
	set_origin(fg_console);
	get_scrmem(fg_console);
	unblank_origin = origin;
	memsetw((void *)blank_origin, BLANK, video_mem_term-blank_origin);
	hide_cursor();
	console_blanked = fg_console + 1;

	if(!nopowersave)
	    vesa_blank();
}

void do_unblank_screen(void)
{
	int currcons;
	int resetorg;
	long offset;

	if (!console_blanked)
		return;
	if (!vc_cons_allocated(fg_console)) {
		/* impossible */
		printk("unblank_screen: tty %d not allocated ??\n", fg_console+1);
		return;
	}
	timer_table[BLANK_TIMER].fn = blank_screen;
	if (blankinterval) {
		timer_table[BLANK_TIMER].expires = jiffies + blankinterval;
		timer_active |= 1<<BLANK_TIMER;
	}

	currcons = fg_console;
	offset = 0;
	resetorg = 0;
	if (console_blanked == fg_console + 1 && origin == unblank_origin
	    && !has_scrolled) {
		/* try to restore the exact situation before blanking */
		resetorg = 1;
		offset = (blank_origin - video_mem_base)
			- (unblank_origin - video_mem_start);
	}

	console_blanked = 0;
	set_scrmem(fg_console, offset);
	set_origin(fg_console);
	set_cursor(fg_console);
	if (resetorg)
		__set_origin(blank__origin);

	vesa_unblank();
}

/*
 * If a blank_screen is due to a timer, then a power save is allowed.
 * If it is related to console_switching, then avoid vesa_blank().
 */
static void blank_screen(void)
{
	do_blank_screen(0);
}

static void unblank_screen(void)
{
	do_unblank_screen();
}

void update_screen(int new_console)
{
	static int lock = 0;

	if (new_console == fg_console || lock)
		return;
	if (!vc_cons_allocated(new_console)) {
		/* strange ... */
		printk("update_screen: tty %d not allocated ??\n", new_console+1);
		return;
	}
	lock = 1;
#ifdef CONFIG_SELECTION
	clear_selection();
#endif /* CONFIG_SELECTION */
	if (!console_blanked)
		get_scrmem(fg_console);
	else
		console_blanked = -1;	   /* no longer of the form console+1 */
	fg_console = new_console; /* this is the only (nonzero) assignment to fg_console */
				  /* consequently, fg_console will always be allocated */
	set_scrmem(fg_console, 0); 
	set_origin(fg_console);
	set_cursor(fg_console);
	set_leds();
	compute_shiftstate();
	lock = 0;
}

/*
 * do_screendump is used for three tasks:
 *   if (mode==0) is the old ioctl(TIOCLINUX,0)
 *   if (mode==1) dumps wd,hg, cursor position, and all the char-attr pairs
 *   if (mode==2) restores what mode1 got.
 * the new modes are needed for a fast and complete dump-restore cycle,
 * needed to implement root-window menus in text mode (A Rubini Nov 1994)
 */
int do_screendump(unsigned long arg, int mode)
{
	char *sptr, *buf = (char *)arg;
	int currcons, l, chcount;

	l = verify_area(VERIFY_READ, buf, 2);
	if (l)
		return l;
	currcons = get_fs_byte(buf+1);
	currcons = (currcons ? currcons-1 : fg_console);
	if (!vc_cons_allocated(currcons))
		return -EIO;
	
	/* mode 0 needs 2+wd*ht, modes 1 and 2 need 4+2*wd*ht */
	chcount=video_num_columns*video_num_lines;
	l = verify_area(mode==2 ? VERIFY_READ :VERIFY_WRITE,
		buf, (2+chcount)*(mode ? 2 : 1));
	if (l)
		return l;
	if (mode<2) {
	put_fs_byte((char)(video_num_lines),buf++);   
	put_fs_byte((char)(video_num_columns),buf++);
	    }
#ifdef CONFIG_SELECTION
	clear_selection();
#endif
	switch(mode) {
	    case 0:
			sptr = (char *) origin;
			for (l=chcount; l>0 ; l--, sptr++)
				put_fs_byte(*sptr++,buf++);	
			break;
	    case 1:
			put_fs_byte((char)x,buf++); put_fs_byte((char)y,buf++); 
			memcpy_tofs(buf,(char *)origin,2*chcount);
			break;
	    case 2:
			gotoxy(currcons, get_fs_byte(buf+2), get_fs_byte(buf+3));
			buf+=4; /* ioctl#, console#, x,y */
			memcpy_fromfs((char *)origin,buf,2*chcount);
			break;
	    }
	return(0);
}

/*
 * Allocate the console screen memory.
 */
int con_open(struct tty_struct *tty, struct file * filp)
{
	unsigned int	idx;
	int i;

	idx = MINOR(tty->device) - tty->driver.minor_start;
	
	i = vc_allocate(idx);
	if (i)
		return i;

	vt_cons[idx]->vc_num = idx;
	tty->driver_data = vt_cons[idx];
	
	if (!tty->winsize.ws_row && !tty->winsize.ws_col) {
		tty->winsize.ws_row = video_num_lines;
		tty->winsize.ws_col = video_num_columns;
	}
	return 0;
}

#ifdef CONFIG_SELECTION
/* correction factor for when screen is hardware-scrolled */
#define	hwscroll_offset (currcons == fg_console ? ((__real_origin - __origin) << 1) : 0)

/* set reverse video on characters s-e of console with selection. */
static void highlight(const int currcons, const int s, const int e)
{
	unsigned char *p, *p1, *p2;

	p1 = (unsigned char *)origin - hwscroll_offset + s + 1;
	p2 = (unsigned char *)origin - hwscroll_offset + e + 1;

	for (p = p1; p <= p2; p += 2)
		*p = (*p & 0x88) | ((*p << 4) & 0x70) | ((*p >> 4) & 0x07);
}

/* use complementary color to show the pointer */
static void highlight_pointer(const int currcons, const int where)
{
	unsigned char *p;
	static unsigned char *prev=NULL;

	if (where==-1) /* remove the pointer */
	{
		if (prev)
		{
			*prev ^= 0x77;
			prev=NULL;
		}
	}
	else
	{
		p = (unsigned char *)origin - hwscroll_offset + where + 1;
		*p ^= 0x77;
		if (prev) *prev ^= 0x77; /* remove the previous one */
		prev=p;
	}
}


/*
 * This function uses a 128-bit look up table
 * WARNING: This depends on both endianness and the ascii code
 */
static unsigned long inwordLut[4]={
  0x00000000, /* control chars     */
  0x03FF0000, /* digits            */
  0x87FFFFFE, /* uppercase and '_' */
  0x07FFFFFE  /* lowercase         */
};
static inline int inword(const char c) {
   return ( inwordLut[(c>>5)&3] >> (c&0x1F) ) & 1;
}

/* set inwordLut contents. Invoked by ioctl(). */
int sel_loadlut(const int arg)
{
    memcpy_fromfs(inwordLut,(unsigned long *)(arg+4),16);
    return 0;
}

/* does screen address p correspond to character at LH/RH edge of screen? */
static inline int atedge(const int p)
{
	return (!(p % video_size_row) || !((p + 2) % video_size_row));
}

/* constrain v such that v <= u */
static inline unsigned short limit(const unsigned short v, const unsigned short u)
{
/* gcc miscompiles the ?: operator, so don't use it.. */
	if (v > u)
		return u;
	return v;
}

/* invoked via ioctl(TIOCLINUX) */
int mouse_reporting(void)
{
	int currcons = fg_console;

	return report_mouse;
}

/* set the current selection. Invoked by ioctl(). */
int set_selection(const int arg, struct tty_struct *tty)
{
	unsigned short *args, xs, ys, xe, ye;
	int currcons = fg_console;
	int sel_mode, new_sel_start, new_sel_end, spc;
	char *bp, *obp, *spos;
	int i, ps, pe;
	char *off = (char *)origin - hwscroll_offset;

	unblank_screen();
	args = (unsigned short *)(arg + 1);
	xs = get_fs_word(args++) - 1;
	ys = get_fs_word(args++) - 1;
	xe = get_fs_word(args++) - 1;
	ye = get_fs_word(args++) - 1;
	sel_mode = get_fs_word(args);

	xs = limit(xs, video_num_columns - 1);
	ys = limit(ys, video_num_lines - 1);
	xe = limit(xe, video_num_columns - 1);
	ye = limit(ye, video_num_lines - 1);
	ps = ys * video_size_row + (xs << 1);
	pe = ye * video_size_row + (xe << 1);

	if (report_mouse && (sel_mode & 16)) {
		mouse_report(currcons, tty, sel_mode & 15, xs, ys);
		return 0;
	}

	if (ps > pe)	/* make sel_start <= sel_end */
	{
		int tmp = ps;
		ps = pe;
		pe = tmp;
	}

	switch (sel_mode)
	{
		case 0:	/* character-by-character selection */
			new_sel_start = ps;
			new_sel_end = pe;
			break;
		case 1:	/* word-by-word selection */
			spc = isspace(*(off + ps));
			for (new_sel_start = ps; ; ps -= 2)
			{
				if ((spc && !isspace(*(off + ps))) ||
				    (!spc && !inword(*(off + ps))))
					break;
				new_sel_start = ps;
				if (!(ps % video_size_row))
					break;
			}
			spc = isspace(*(off + pe));
			for (new_sel_end = pe; ; pe += 2)
			{
				if ((spc && !isspace(*(off + pe))) ||
				    (!spc && !inword(*(off + pe))))
					break;
				new_sel_end = pe;
				if (!((pe + 2) % video_size_row))
					break;
			}
			break;
		case 2:	/* line-by-line selection */
			new_sel_start = ps - ps % video_size_row;
			new_sel_end = pe + video_size_row
				    - pe % video_size_row - 2;
			break;
		case 3: /* pointer highlight */
			if (sel_cons != currcons)
			{
				clear_selection();
				sel_cons = currcons;
			}
			highlight_pointer(sel_cons,pe);
			return 0; /* nothing more */
		default:
			return -EINVAL;
	}
	/* remove the pointer */
	highlight_pointer(sel_cons,-1);
	/* select to end of line if on trailing space */
	if (new_sel_end > new_sel_start &&
		!atedge(new_sel_end) && isspace(*(off + new_sel_end)))
	{
		for (pe = new_sel_end + 2; ; pe += 2)
		{
			if (!isspace(*(off + pe)) || atedge(pe))
				break;
		}
		if (isspace(*(off + pe)))
			new_sel_end = pe;
	}
	if (sel_cons != currcons)
	{
		clear_selection();
		sel_cons = currcons;
	}
	if (sel_start == -1)	/* no current selection */
		highlight(sel_cons, new_sel_start, new_sel_end);
	else if (new_sel_start == sel_start)
	{
		if (new_sel_end == sel_end)	/* no action required */
			return 0;
		else if (new_sel_end > sel_end)	/* extend to right */
			highlight(sel_cons, sel_end + 2, new_sel_end);
		else				/* contract from right */
			highlight(sel_cons, new_sel_end + 2, sel_end);
	}
	else if (new_sel_end == sel_end)
	{
		if (new_sel_start < sel_start)	/* extend to left */
			highlight(sel_cons, new_sel_start, sel_start - 2);
		else				/* contract from left */
			highlight(sel_cons, sel_start, new_sel_start - 2);
	}
	else	/* some other case; start selection from scratch */
	{
		clear_selection();
		highlight(sel_cons, new_sel_start, new_sel_end);
	}
	sel_start = new_sel_start;
	sel_end = new_sel_end;

	/* realloc the buffer (it seems to be efficient, anyway) */
	if (sel_buffer) kfree(sel_buffer);
	sel_buffer = kmalloc((sel_end-sel_start)/2+2, GFP_KERNEL);
	if (!sel_buffer)
	{
		printk("selection: kmalloc() failed\n");
		clear_selection();
		return (0); /* is it right? */
	}
	obp = bp = sel_buffer;
	for (i = sel_start; i <= sel_end; i += 2)
	{
		spos = (char *)off + i;
		*bp++ = *spos;
		if (!isspace(*spos))
			obp = bp;
		if (! ((i + 2) % video_size_row))
		{
			/* strip trailing blanks from line and add newline,
			   unless non-space at end of line. */
			if (obp != bp)
			{
				bp = obp;
				*bp++ = '\r';
			}
			obp = bp;
		}
	}
	*bp = '\0';
	return 0;
}

/* insert the contents of the selection buffer into the queue of the
   tty associated with the current console. Invoked by ioctl(). */
int paste_selection(struct tty_struct *tty)
{
	struct wait_queue wait = { current, NULL };
	char	*bp = sel_buffer;
	int	c, l;
	struct vt_struct *vt = (struct vt_struct *) tty->driver_data;
	
	if (!bp || !bp[0])
		return 0;
	unblank_screen();
	c = strlen(sel_buffer);
	current->state = TASK_INTERRUPTIBLE;
	add_wait_queue(&vt->paste_wait, &wait);
	while (c) {
		if (test_bit(TTY_THROTTLED, &tty->flags)) {
			schedule();
			continue;
		}
		l = MIN(c, tty->ldisc.receive_room(tty));
		tty->ldisc.receive_buf(tty, bp, 0, l);
		c -= l;
		bp += l;
	}
	current->state = TASK_RUNNING;
	return 0;
}

/* remove the current selection highlight, if any, from the console holding
   the selection. */
static void clear_selection()
{
	highlight_pointer(sel_cons, -1); /* hide the pointer */
	if (sel_start != -1)
	{
		highlight(sel_cons, sel_start, sel_end);
		sel_start = -1;
	}
}
#endif /* CONFIG_SELECTION */

/*
 * PIO_FONT support.
 *
 * The font loading code goes back to the codepage package by
 * Joel Hoffman (joel@wam.umd.edu). (He reports that the original
 * reference is: "From: p. 307 of _Programmer's Guide to PC & PS/2
 * Video Systems_ by Richard Wilton. 1987.  Microsoft Press".)
 *
 * Change for certain monochrome monitors by Yury Shevchuck
 * (sizif@botik.yaroslavl.su).
 */

#define colourmap ((char *)0xa0000)
/* Pauline Middelink <middelin@polyware.iaf.nl> reports that we
   should use 0xA0000 for the bwmap as well.. */
#define blackwmap ((char *)0xa0000)
#define cmapsz 8192
#define seq_port_reg (0x3c4)
#define seq_port_val (0x3c5)
#define gr_port_reg (0x3ce)
#define gr_port_val (0x3cf)

static int set_get_font(char * arg, int set)
{
#ifdef CAN_LOAD_EGA_FONTS
	int i;
	char *charmap;
	int beg;

	/* no use to "load" CGA... */

	if (video_type == VIDEO_TYPE_EGAC) {
		charmap = colourmap;
		beg = 0x0e;
	} else if (video_type == VIDEO_TYPE_EGAM) {
		charmap = blackwmap;
		beg = 0x0a;
	} else
		return -EINVAL;

	i = verify_area(set ? VERIFY_READ : VERIFY_WRITE, (void *)arg, cmapsz);
	if (i)
		return i;

	cli();
	outb_p( 0x00, seq_port_reg );   /* First, the sequencer */
	outb_p( 0x01, seq_port_val );   /* Synchronous reset */
	outb_p( 0x02, seq_port_reg );
	outb_p( 0x04, seq_port_val );   /* CPU writes only to map 2 */
	outb_p( 0x04, seq_port_reg );
	outb_p( 0x07, seq_port_val );   /* Sequential addressing */
	outb_p( 0x00, seq_port_reg );
	outb_p( 0x03, seq_port_val );   /* Clear synchronous reset */

	outb_p( 0x04, gr_port_reg );    /* Now, the graphics controller */
	outb_p( 0x02, gr_port_val );    /* select map 2 */
	outb_p( 0x05, gr_port_reg );
	outb_p( 0x00, gr_port_val );    /* disable odd-even addressing */
	outb_p( 0x06, gr_port_reg );
	outb_p( 0x00, gr_port_val );    /* map start at A000:0000 */
	sti();

	if (set)
		for (i=0; i<cmapsz ; i++)
			*(charmap+i) = get_fs_byte(arg+i);
	else
		for (i=0; i<cmapsz ; i++)
			put_fs_byte(*(charmap+i), arg+i);

	cli();
	outb_p( 0x00, seq_port_reg );   /* First, the sequencer */
	outb_p( 0x01, seq_port_val );   /* Synchronous reset */
	outb_p( 0x02, seq_port_reg );
	outb_p( 0x03, seq_port_val );   /* CPU writes to maps 0 and 1 */
	outb_p( 0x04, seq_port_reg );
	outb_p( 0x03, seq_port_val );   /* odd-even addressing */
	outb_p( 0x00, seq_port_reg );
	outb_p( 0x03, seq_port_val );   /* clear synchronous reset */

	outb_p( 0x04, gr_port_reg );    /* Now, the graphics controller */
	outb_p( 0x00, gr_port_val );    /* select map 0 for CPU */
	outb_p( 0x05, gr_port_reg );
	outb_p( 0x10, gr_port_val );    /* enable even-odd addressing */
	outb_p( 0x06, gr_port_reg );
	outb_p( beg, gr_port_val );     /* map starts at b800:0 or b000:0 */
	sti();

	return 0;
#else
	return -EINVAL;
#endif
}

/*
 * Load font into the EGA/VGA character generator. arg points to a 8192
 * byte map, 32 bytes per character. Only first H of them are used for
 * 8xH fonts (0 < H <= 32).
 */

int con_set_font (char *arg)
{
	return set_get_font (arg,1);
}

int con_get_font (char *arg)
{
	return set_get_font (arg,0);
}

/*
 * Load customizable translation table (USER_TRANS[]). All checks are here,
 * so we need only include 'return con_set_trans(arg)' in the ioctl handler
 * arg points to a 256 byte translation table.
 */
int con_set_trans(char * arg)
{
	int i;

	i = verify_area(VERIFY_READ, (void *)arg, E_TABSZ);
	if (i)
		return i;

	for (i=0; i<E_TABSZ ; i++) USER_TRANS[i] = get_fs_byte(arg+i);
	USER_TRANS[012]=0;
	USER_TRANS[014]=0;
	USER_TRANS[015]=0;
	USER_TRANS[033]=0;
	return 0;
}

int con_get_trans(char * arg)
{
	int i;

	i = verify_area(VERIFY_WRITE, (void *)arg, E_TABSZ);
	if (i)
		return i;

	for (i=0; i<E_TABSZ ; i++) put_fs_byte(USER_TRANS[i],arg+i);
	return 0;
}
