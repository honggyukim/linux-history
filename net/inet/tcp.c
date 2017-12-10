/*
 * INET		An implementation of the TCP/IP protocol suite for the LINUX
 *		operating system.  INET is implemented using the  BSD Socket
 *		interface as the means of communication with the user level.
 *
 *		Implementation of the Transmission Control Protocol(TCP).
 *
 * Version:	@(#)tcp.c	1.0.16	05/25/93
 *
 * Authors:	Ross Biro, <bir7@leland.Stanford.Edu>
 *		Fred N. van Kempen, <waltje@uWalt.NL.Mugnet.ORG>
 *		Mark Evans, <evansmp@uhura.aston.ac.uk>
 *		Corey Minyard <wf-rch!minyard@relay.EU.net>
 *		Florian La Roche, <flla@stud.uni-sb.de>
 *		Charles Hedrick, <hedrick@klinzhai.rutgers.edu>
 *		Linus Torvalds, <torvalds@cs.helsinki.fi>
 *		Alan Cox, <gw4pts@gw4pts.ampr.org>
 *		Matthew Dillon, <dillon@apollo.west.oic.com>
 *		Arnt Gulbrandsen, <agulbra@no.unit.nvg>
 *
 * Fixes:	
 *		Alan Cox	:	Numerous verify_area() calls
 *		Alan Cox	:	Set the ACK bit on a reset
 *		Alan Cox	:	Stopped it crashing if it closed while sk->inuse=1
 *					and was trying to connect (tcp_err()).
 *		Alan Cox	:	All icmp error handling was broken
 *					pointers passed where wrong and the
 *					socket was looked up backwards. Nobody
 *					tested any icmp error code obviously.
 *		Alan Cox	:	tcp_err() now handled properly. It wakes people
 *					on errors. select behaves and the icmp error race
 *					has gone by moving it into sock.c
 *		Alan Cox	:	tcp_reset() fixed to work for everything not just
 *					packets for unknown sockets.
 *		Alan Cox	:	tcp option processing.
 *		Alan Cox	:	Reset tweaked (still not 100%) [Had syn rule wrong]
 *		Herp Rosmanith  :	More reset fixes
 *		Alan Cox	:	No longer acks invalid rst frames. Acking
 *					any kind of RST is right out.
 *		Alan Cox	:	Sets an ignore me flag on an rst receive
 *					otherwise odd bits of prattle escape still
 *		Alan Cox	:	Fixed another acking RST frame bug. Should stop
 *					LAN workplace lockups.
 *		Alan Cox	: 	Some tidyups using the new skb list facilities
 *		Alan Cox	:	sk->keepopen now seems to work
 *		Alan Cox	:	Pulls options out correctly on accepts
 *		Alan Cox	:	Fixed assorted sk->rqueue->next errors
 *		Alan Cox	:	PSH doesn't end a TCP read. Switched a bit to skb ops.
 *		Alan Cox	:	Tidied tcp_data to avoid a potential nasty.
 *		Alan Cox	:	Added some better commenting, as the tcp is hard to follow
 *		Alan Cox	:	Removed incorrect check for 20 * psh
 *	Michael O'Reilly	:	ack < copied bug fix.
 *	Johannes Stille		:	Misc tcp fixes (not all in yet).
 *		Alan Cox	:	FIN with no memory -> CRASH
 *		Alan Cox	:	Added socket option proto entries. Also added awareness of them to accept.
 *		Alan Cox	:	Added TCP options (SOL_TCP)
 *		Alan Cox	:	Switched wakeup calls to callbacks, so the kernel can layer network sockets.
 *		Alan Cox	:	Use ip_tos/ip_ttl settings.
 *		Alan Cox	:	Handle FIN (more) properly (we hope).
 *		Alan Cox	:	RST frames sent on unsynchronised state ack error/
 *		Alan Cox	:	Put in missing check for SYN bit.
 *		Alan Cox	:	Added tcp_select_window() aka NET2E 
 *					window non shrink trick.
 *		Alan Cox	:	Added a couple of small NET2E timer fixes
 *		Charles Hedrick :	TCP fixes
 *		Toomas Tamm	:	TCP window fixes
 *		Alan Cox	:	Small URG fix to rlogin ^C ack fight
 *		Charles Hedrick	:	Rewrote most of it to actually work
 *		Linus		:	Rewrote tcp_read() and URG handling
 *					completely
 *		Gerhard Koerting:	Fixed some missing timer handling
 *		Matthew Dillon  :	Reworked TCP machine states as per RFC
 *		Gerhard Koerting:	PC/TCP workarounds
 *		Adam Caldwell	:	Assorted timer/timing errors
 *		Matthew Dillon	:	Fixed another RST bug
 *		Alan Cox	:	Move to kernel side addressing changes.
 *		Alan Cox	:	Beginning work on TCP fastpathing (not yet usable)
 *		Arnt Gulbrandsen:	Turbocharged tcp_check() routine.
 *		Alan Cox	:	TCP fast path debugging
 *		Alan Cox	:	Window clamping
 *		Michael Riepe	:	Bug in tcp_check()
 *		Matt Dillon	:	More TCP improvements and RST bug fixes
 *		Matt Dillon	:	Yet more small nasties remove from the TCP code
 *					(Be very nice to this man if tcp finally works 100%) 8)
 *		Alan Cox	:	BSD accept semantics. 
 *		Alan Cox	:	Reset on closedown bug.
 *	Peter De Schrijver	:	ENOTCONN check missing in tcp_sendto().
 *		Michael Pall	:	Handle select() after URG properly in all cases.
 *		Michael Pall	:	Undo the last fix in tcp_read_urg() (multi URG PUSH broke rlogin).
 *		Michael Pall	:	Fix the multi URG PUSH problem in tcp_readable(), select() after URG works now.
 *		Michael Pall	:	recv(...,MSG_OOB) never blocks in the BSD api.
 *		Alan Cox	:	Changed the semantics of sk->socket to 
 *					fix a race and a signal problem with
 *					accept() and async I/O.
 *		Alan Cox	:	Relaxed the rules on tcp_sendto().
 *		Yury Shevchuk	:	Really fixed accept() blocking problem.
 *		Craig I. Hagan  :	Allow for BSD compatible TIME_WAIT for
 *					clients/servers which listen in on
 *					fixed ports.
 *		Alan Cox	:	Cleaned the above up and shrank it to
 *					a sensible code size.
 *		Alan Cox	:	Self connect lockup fix.
 *		Alan Cox	:	No connect to multicast.
 *		Ross Biro	:	Close unaccepted children on master
 *					socket close.
 *		Alan Cox	:	Reset tracing code.
 *		Alan Cox	:	Spurious resets on shutdown.
 *
 *
 * To Fix:
 *			Fast path the code. Two things here - fix the window calculation
 *		so it doesn't iterate over the queue, also spot packets with no funny
 *		options arriving in order and process directly.
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or(at your option) any later version.
 *
 * Description of States:
 *
 *	TCP_SYN_SENT		sent a connection request, waiting for ack
 *
 *	TCP_SYN_RECV		received a connection request, sent ack,
 *				waiting for final ack in three-way handshake.
 *
 *	TCP_ESTABLISHED		connection established
 *
 *	TCP_FIN_WAIT1		our side has shutdown, waiting to complete
 *				transmission of remaining buffered data
 *
 *	TCP_FIN_WAIT2		all buffered data sent, waiting for remote
 *				to shutdown
 *
 *	TCP_CLOSING		both sides have shutdown but we still have
 *				data we have to finish sending
 *
 *	TCP_TIME_WAIT		timeout to catch resent junk before entering
 *				closed, can only be entered from FIN_WAIT2
 *				or CLOSING.  Required because the other end
 *				may not have gotten our last ACK causing it
 *				to retransmit the data packet (which we ignore)
 *
 *	TCP_CLOSE_WAIT		remote side has shutdown and is waiting for
 *				us to finish writing our data and to shutdown
 *				(we have to close() to move on to LAST_ACK)
 *
 *	TCP_LAST_ACK		out side has shutdown after remote has
 *				shutdown.  There may still be data in our
 *				buffer that we have to finish sending
 *		
 *	TCP_CLOSE		socket is finished
 */
#include <linux/types.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/string.h>
#include <linux/socket.h>
#include <linux/sockios.h>
#include <linux/termios.h>
#include <linux/in.h>
#include <linux/fcntl.h>
#include <linux/inet.h>
#include <linux/netdevice.h>
#include "snmp.h"
#include "ip.h"
#include "protocol.h"
#include "icmp.h"
#include "tcp.h"
#include <linux/skbuff.h>
#include "sock.h"
#include "route.h"
#include <linux/errno.h>
#include <linux/timer.h>
#include <asm/system.h>
#include <asm/segment.h>
#include <linux/mm.h>

#undef TCP_FASTPATH

#define SEQ_TICK 3
unsigned long seq_offset;
struct tcp_mib	tcp_statistics;

static void tcp_close(struct sock *sk, int timeout);

#ifdef TCP_FASTPATH
unsigned long tcp_rx_miss=0, tcp_rx_hit1=0, tcp_rx_hit2=0;
#endif


static __inline__ int min(unsigned int a, unsigned int b)
{
	if (a < b) 
		return(a);
	return(b);
}

#undef STATE_TRACE

static __inline__ void tcp_set_state(struct sock *sk, int state)
{
	if(sk->state==TCP_ESTABLISHED)
		tcp_statistics.TcpCurrEstab--;
#ifdef STATE_TRACE
	if(sk->debug)
		printk("TCP sk=%s, State %d -> %d\n",sk, sk->state,state);
#endif	
	sk->state=state;
	if(state==TCP_ESTABLISHED)
		tcp_statistics.TcpCurrEstab++;
}

/* This routine picks a TCP windows for a socket based on
   the following constraints
   
   1. The window can never be shrunk once it is offered (RFC 793)
   2. We limit memory per socket
   
   For now we use NET2E3's heuristic of offering half the memory
   we have handy. All is not as bad as this seems however because
   of two things. Firstly we will bin packets even within the window
   in order to get the data we are waiting for into the memory limit.
   Secondly we bin common duplicate forms at receive time
   
   Better heuristics welcome
*/
   
int tcp_select_window(struct sock *sk)
{
	int new_window = sk->prot->rspace(sk);
	
	if(sk->window_clamp)
		new_window=min(sk->window_clamp,new_window);
/*
 * two things are going on here.  First, we don't ever offer a
 * window less than min(sk->mss, MAX_WINDOW/2).  This is the
 * receiver side of SWS as specified in RFC1122.
 * Second, we always give them at least the window they
 * had before, in order to avoid retracting window.  This
 * is technically allowed, but RFC1122 advises against it and
 * in practice it causes trouble.
 */
	if (new_window < min(sk->mss, MAX_WINDOW/2) || new_window < sk->window)
		return(sk->window);
	return(new_window);
}

/*
 *	Find someone to 'accept'. Must be called with
 *	sk->inuse=1 or cli()
 */ 

static struct sk_buff *tcp_find_established(struct sock *s)
{
	struct sk_buff *p=skb_peek(&s->receive_queue);
	if(p==NULL)
		return NULL;
	do
	{
		if(p->sk->state == TCP_ESTABLISHED || p->sk->state >= TCP_FIN_WAIT1)
			return p;
		p=p->next;
	}
	while(p!=skb_peek(&s->receive_queue));
	return NULL;
}


/* 
 *	This routine closes sockets which have been at least partially
 *	opened, but not yet accepted. Currently it is only called by
 *	tcp_close, and timeout mirrors the value there. 
 */

static void tcp_close_pending (struct sock *sk, int timeout) 
{
	unsigned long flags;
	struct sk_buff *p, *old_p;

	save_flags(flags);
	cli(); 
	p=skb_peek(&sk->receive_queue);

	if(p==NULL) 
	{
		restore_flags(flags);
		return;
	}

	do
	{
		tcp_close (p->sk, timeout);
		skb_unlink (p);
		old_p = p;
		p=p->next;
		kfree_skb(old_p, FREE_READ);
	}
	while(p!=skb_peek(&sk->receive_queue));

	restore_flags(flags);
	return;
}

static struct sk_buff *tcp_dequeue_established(struct sock *s)
{
	struct sk_buff *skb;
	unsigned long flags;
	save_flags(flags);
	cli(); 
	skb=tcp_find_established(s);
	if(skb!=NULL)
		skb_unlink(skb);	/* Take it off the queue */
	restore_flags(flags);
	return skb;
}


/*
 *	Enter the time wait state. 
 */

static void tcp_time_wait(struct sock *sk)
{
	tcp_set_state(sk,TCP_TIME_WAIT);
	sk->shutdown = SHUTDOWN_MASK;
	if (!sk->dead)
		sk->state_change(sk);
	reset_timer(sk, TIME_CLOSE, TCP_TIMEWAIT_LEN);
}

/*
 *	A timer event has trigger a tcp retransmit timeout. The
 *	socket xmit queue is ready and set up to send. Because
 *	the ack receive code keeps the queue straight we do
 *	nothing clever here.
 */

static void tcp_retransmit(struct sock *sk, int all)
{
	if (all) 
	{
		ip_retransmit(sk, all);
		return;
	}

	sk->ssthresh = sk->cong_window >> 1; /* remember window where we lost */
	/* sk->ssthresh in theory can be zero.  I guess that's OK */
	sk->cong_count = 0;

	sk->cong_window = 1;

	/* Do the actual retransmit. */
	ip_retransmit(sk, all);
}


/*
 * This routine is called by the ICMP module when it gets some
 * sort of error condition.  If err < 0 then the socket should
 * be closed and the error returned to the user.  If err > 0
 * it's just the icmp type << 8 | icmp code.  After adjustment
 * header points to the first 8 bytes of the tcp header.  We need
 * to find the appropriate port.
 */

void tcp_err(int err, unsigned char *header, unsigned long daddr,
	unsigned long saddr, struct inet_protocol *protocol)
{
	struct tcphdr *th;
	struct sock *sk;
	struct iphdr *iph=(struct iphdr *)header;
  
	header+=4*iph->ihl;
   

	th =(struct tcphdr *)header;
	sk = get_sock(&tcp_prot, th->source, daddr, th->dest, saddr);

	if (sk == NULL) 
		return;
  
	if(err<0)
	{
	  	sk->err = -err;
	  	sk->error_report(sk);
	  	return;
	}

	if ((err & 0xff00) == (ICMP_SOURCE_QUENCH << 8)) 
	{
		/*
		 * FIXME:
		 * For now we will just trigger a linear backoff.
		 * The slow start code should cause a real backoff here.
		 */
		if (sk->cong_window > 4)
			sk->cong_window--;
		return;
	}

/*	sk->err = icmp_err_convert[err & 0xff].errno;  -- moved as TCP should hide non fatals internally (and does) */

	/*
	 * If we've already connected we will keep trying
	 * until we time out, or the user gives up.
	 */

	if (icmp_err_convert[err & 0xff].fatal || sk->state == TCP_SYN_SENT) 
	{
		if (sk->state == TCP_SYN_SENT) 
		{
			tcp_statistics.TcpAttemptFails++;
			tcp_set_state(sk,TCP_CLOSE);
			sk->error_report(sk);		/* Wake people up to see the error (see connect in sock.c) */
		}
		sk->err = icmp_err_convert[err & 0xff].errno;		
	}
	return;
}


/*
 *	Walk down the receive queue counting readable data until we hit the end or we find a gap
 *	in the received data queue (ie a frame missing that needs sending to us)
 */

static int tcp_readable(struct sock *sk)
{
	unsigned long counted;
	unsigned long amount;
	struct sk_buff *skb;
	int sum;
	unsigned long flags;

	if(sk && sk->debug)
	  	printk("tcp_readable: %p - ",sk);

	save_flags(flags);
	cli();
	if (sk == NULL || (skb = skb_peek(&sk->receive_queue)) == NULL)
	{
		restore_flags(flags);
	  	if(sk && sk->debug) 
	  		printk("empty\n");
	  	return(0);
	}
  
	counted = sk->copied_seq+1;	/* Where we are at the moment */
	amount = 0;
  
	/* Do until a push or until we are out of data. */
	do 
	{
		if (before(counted, skb->h.th->seq)) 	/* Found a hole so stops here */
			break;
		sum = skb->len -(counted - skb->h.th->seq);	/* Length - header but start from where we are up to (avoid overlaps) */
		if (skb->h.th->syn)
			sum++;
		if (sum > 0) 
		{					/* Add it up, move on */
			amount += sum;
			if (skb->h.th->syn) 
				amount--;
			counted += sum;
		}
		/*
		 * Don't count urg data ... but do it in the right place!
		 * Consider: "old_data (ptr is here) URG PUSH data"
		 * The old code would stop at the first push because
		 * it counted the urg (amount==1) and then does amount--
		 * *after* the loop.  This means tcp_readable() always
		 * returned zero if any URG PUSH was in the queue, even
		 * though there was normal data available. If we subtract
		 * the urg data right here, we even get it to work for more
		 * than one URG PUSH skb without normal data.
		 * This means that select() finally works now with urg data
		 * in the queue.  Note that rlogin was never affected
		 * because it doesn't use select(); it uses two processes
		 * and a blocking read().  And the queue scan in tcp_read()
		 * was correct.  Mike <pall@rz.uni-karlsruhe.de>
		 */
		if (skb->h.th->urg)
			amount--;	/* don't count urg data */
		if (amount && skb->h.th->psh) break;
		skb = skb->next;
	}
	while(skb != (struct sk_buff *)&sk->receive_queue);

	restore_flags(flags);
	if(sk->debug)
	  	printk("got %lu bytes.\n",amount);
	return(amount);
}


/*
 *	Wait for a TCP event. Note the oddity with SEL_IN and reading. The
 *	listening socket has a receive queue of sockets to accept.
 */

static int tcp_select(struct sock *sk, int sel_type, select_table *wait)
{
	sk->inuse = 1;

	switch(sel_type) 
	{
		case SEL_IN:
			select_wait(sk->sleep, wait);
			if (skb_peek(&sk->receive_queue) != NULL) 
			{
				if ((sk->state == TCP_LISTEN && tcp_find_established(sk)) || tcp_readable(sk)) 
				{
					release_sock(sk);
					return(1);
				}
			}
			if (sk->err != 0)	/* Receiver error */
			{
				release_sock(sk);
				return(1);
			}
			if (sk->shutdown & RCV_SHUTDOWN) 
			{
				release_sock(sk);
				return(1);
			} 
			release_sock(sk);
			return(0);
		case SEL_OUT:
			select_wait(sk->sleep, wait);
			if (sk->shutdown & SEND_SHUTDOWN) 
			{
				/* FIXME: should this return an error? */
				release_sock(sk);
				return(0);
			}

			/*
			 * This is now right thanks to a small fix
			 * by Matt Dillon.
			 */
			
			if (sk->prot->wspace(sk) >= sk->mtu+128+sk->prot->max_header) 
			{
				release_sock(sk);
				/* This should cause connect to work ok. */
				if (sk->state == TCP_SYN_RECV ||
				    sk->state == TCP_SYN_SENT) return(0);
				return(1);
			}
			release_sock(sk);
			return(0);
		case SEL_EX:
			select_wait(sk->sleep,wait);
			if (sk->err || sk->urg_data) 
			{
				release_sock(sk);
				return(1);
			}
			release_sock(sk);
			return(0);
 	}

 	release_sock(sk);
 	return(0);
}


int tcp_ioctl(struct sock *sk, int cmd, unsigned long arg)
{
	int err;
	switch(cmd) 
	{

		case TIOCINQ:
#ifdef FIXME	/* FIXME: */
		case FIONREAD:
#endif
		{
			unsigned long amount;

			if (sk->state == TCP_LISTEN) 
				return(-EINVAL);

			sk->inuse = 1;
			amount = tcp_readable(sk);
			release_sock(sk);
			err=verify_area(VERIFY_WRITE,(void *)arg,
						   sizeof(unsigned long));
			if(err)
				return err;
			put_fs_long(amount,(unsigned long *)arg);
			return(0);
		}
		case SIOCATMARK:
		{
			int answ = sk->urg_data && sk->urg_seq == sk->copied_seq+1;

			err = verify_area(VERIFY_WRITE,(void *) arg,
						  sizeof(unsigned long));
			if (err)
				return err;
			put_fs_long(answ,(int *) arg);
			return(0);
		}
		case TIOCOUTQ:
		{
			unsigned long amount;

			if (sk->state == TCP_LISTEN) return(-EINVAL);
			amount = sk->prot->wspace(sk);
			err=verify_area(VERIFY_WRITE,(void *)arg,
						   sizeof(unsigned long));
			if(err)
				return err;
			put_fs_long(amount,(unsigned long *)arg);
			return(0);
		}
		default:
			return(-EINVAL);
	}
}


/*
 *	This routine computes a TCP checksum. 
 */
 
unsigned short tcp_check(struct tcphdr *th, int len,
	  unsigned long saddr, unsigned long daddr)
{     
	unsigned long sum;
   
	if (saddr == 0) saddr = ip_my_addr();

/*
 * stupid, gcc complains when I use just one __asm__ block,
 * something about too many reloads, but this is just two
 * instructions longer than what I want
 */
	__asm__("
	    addl %%ecx, %%ebx
	    adcl %%edx, %%ebx
	    adcl $0, %%ebx
	    "
	: "=b"(sum)
	: "0"(daddr), "c"(saddr), "d"((ntohs(len) << 16) + IPPROTO_TCP*256)
	: "bx", "cx", "dx" );
	__asm__("
	    movl %%ecx, %%edx
	    cld
	    cmpl $32, %%ecx
	    jb 2f
	    shrl $5, %%ecx
	    clc
1:	    lodsl
	    adcl %%eax, %%ebx
	    lodsl
	    adcl %%eax, %%ebx
	    lodsl
	    adcl %%eax, %%ebx
	    lodsl
	    adcl %%eax, %%ebx
	    lodsl
	    adcl %%eax, %%ebx
	    lodsl
	    adcl %%eax, %%ebx
	    lodsl
	    adcl %%eax, %%ebx
	    lodsl
	    adcl %%eax, %%ebx
	    loop 1b
	    adcl $0, %%ebx
	    movl %%edx, %%ecx
2:	    andl $28, %%ecx
	    je 4f
	    shrl $2, %%ecx
	    clc
3:	    lodsl
	    adcl %%eax, %%ebx
	    loop 3b
	    adcl $0, %%ebx
4:	    movl $0, %%eax
	    testw $2, %%dx
	    je 5f
	    lodsw
	    addl %%eax, %%ebx
	    adcl $0, %%ebx
	    movw $0, %%ax
5:	    test $1, %%edx
	    je 6f
	    lodsb
	    addl %%eax, %%ebx
	    adcl $0, %%ebx
6:	    movl %%ebx, %%eax
	    shrl $16, %%eax
	    addw %%ax, %%bx
	    adcw $0, %%bx
	    "
	: "=b"(sum)
	: "0"(sum), "c"(len), "S"(th)
	: "ax", "bx", "cx", "dx", "si" );

  	/* We only want the bottom 16 bits, but we never cleared the top 16. */
  
  	return((~sum) & 0xffff);
}



void tcp_send_check(struct tcphdr *th, unsigned long saddr, 
		unsigned long daddr, int len, struct sock *sk)
{
	th->check = 0;
	th->check = tcp_check(th, len, saddr, daddr);
	return;
}

static void tcp_send_skb(struct sock *sk, struct sk_buff *skb)
{
	int size;
	struct tcphdr * th = skb->h.th;

	/* length of packet (not counting length of pre-tcp headers) */
	size = skb->len - ((unsigned char *) th - skb->data);

	/* sanity check it.. */
	if (size < sizeof(struct tcphdr) || size > skb->len) 
	{
		printk("tcp_send_skb: bad skb (skb = %p, data = %p, th = %p, len = %lu)\n",
			skb, skb->data, th, skb->len);
		kfree_skb(skb, FREE_WRITE);
		return;
	}

	/* If we have queued a header size packet.. */
	if (size == sizeof(struct tcphdr)) 
	{
		/* If its got a syn or fin its notionally included in the size..*/
		if(!th->syn && !th->fin) 
		{
			printk("tcp_send_skb: attempt to queue a bogon.\n");
			kfree_skb(skb,FREE_WRITE);
			return;
		}
	}

	tcp_statistics.TcpOutSegs++;  

	skb->h.seq = ntohl(th->seq) + size - 4*th->doff;
	if (after(skb->h.seq, sk->window_seq) ||
	    (sk->retransmits && sk->timeout == TIME_WRITE) ||
	     sk->packets_out >= sk->cong_window) 
	{
		/* checksum will be supplied by tcp_write_xmit.  So
		 * we shouldn't need to set it at all.  I'm being paranoid */
		th->check = 0;
		if (skb->next != NULL) 
		{
			printk("tcp_send_partial: next != NULL\n");
			skb_unlink(skb);
		}
		skb_queue_tail(&sk->write_queue, skb);
		if (before(sk->window_seq, sk->write_queue.next->h.seq) &&
		    sk->send_head == NULL &&
		    sk->ack_backlog == 0)
			reset_timer(sk, TIME_PROBE0, sk->rto);
	} 
	else 
	{
		th->ack_seq = ntohl(sk->acked_seq);
		th->window = ntohs(tcp_select_window(sk));

		tcp_send_check(th, sk->saddr, sk->daddr, size, sk);

		sk->sent_seq = sk->write_seq;
		sk->prot->queue_xmit(sk, skb->dev, skb, 0);
	}
}

struct sk_buff * tcp_dequeue_partial(struct sock * sk)
{
	struct sk_buff * skb;
	unsigned long flags;

	save_flags(flags);
	cli();
	skb = sk->partial;
	if (skb) {
		sk->partial = NULL;
		del_timer(&sk->partial_timer);
	}
	restore_flags(flags);
	return skb;
}

static void tcp_send_partial(struct sock *sk)
{
	struct sk_buff *skb;

	if (sk == NULL)
		return;
	while ((skb = tcp_dequeue_partial(sk)) != NULL)
		tcp_send_skb(sk, skb);
}

void tcp_enqueue_partial(struct sk_buff * skb, struct sock * sk)
{
	struct sk_buff * tmp;
	unsigned long flags;

	save_flags(flags);
	cli();
	tmp = sk->partial;
	if (tmp)
		del_timer(&sk->partial_timer);
	sk->partial = skb;
	init_timer(&sk->partial_timer);
	sk->partial_timer.expires = HZ;
	sk->partial_timer.function = (void (*)(unsigned long)) tcp_send_partial;
	sk->partial_timer.data = (unsigned long) sk;
	add_timer(&sk->partial_timer);
	restore_flags(flags);
	if (tmp)
		tcp_send_skb(sk, tmp);
}


/*
 *	This routine sends an ack and also updates the window. 
 */
 
static void tcp_send_ack(unsigned long sequence, unsigned long ack,
	     struct sock *sk,
	     struct tcphdr *th, unsigned long daddr)
{
	struct sk_buff *buff;
	struct tcphdr *t1;
	struct device *dev = NULL;
	int tmp;

	if(sk->zapped)
		return;		/* We have been reset, we may not send again */
	/*
	 * We need to grab some memory, and put together an ack,
	 * and then put it into the queue to be sent.
	 */

	buff = sk->prot->wmalloc(sk, MAX_ACK_SIZE, 1, GFP_ATOMIC);
	if (buff == NULL) 
	{
		/* Force it to send an ack. */
		sk->ack_backlog++;
		if (sk->timeout != TIME_WRITE && tcp_connected(sk->state)) 
		{
			reset_timer(sk, TIME_WRITE, 10);
		}
		return;
	}

	buff->len = sizeof(struct tcphdr);
	buff->sk = sk;
	buff->localroute = sk->localroute;
	t1 =(struct tcphdr *) buff->data;

	/* Put in the IP header and routing stuff. */
	tmp = sk->prot->build_header(buff, sk->saddr, daddr, &dev,
				IPPROTO_TCP, sk->opt, MAX_ACK_SIZE,sk->ip_tos,sk->ip_ttl);
	if (tmp < 0) 
	{
  		buff->free=1;
		sk->prot->wfree(sk, buff->mem_addr, buff->mem_len);
		return;
	}
	buff->len += tmp;
	t1 =(struct tcphdr *)((char *)t1 +tmp);

	/* FIXME: */
	memcpy(t1, th, sizeof(*t1)); /* this should probably be removed */

	/*
	 *	Swap the send and the receive. 
	 */
	 
	t1->dest = th->source;
	t1->source = th->dest;
	t1->seq = ntohl(sequence);
	t1->ack = 1;
	sk->window = tcp_select_window(sk);
	t1->window = ntohs(sk->window);
	t1->res1 = 0;
	t1->res2 = 0;
	t1->rst = 0;
	t1->urg = 0;
	t1->syn = 0;
	t1->psh = 0;
	t1->fin = 0;
	if (ack == sk->acked_seq) 
	{
		sk->ack_backlog = 0;
		sk->bytes_rcv = 0;
		sk->ack_timed = 0;
		if (sk->send_head == NULL && skb_peek(&sk->write_queue) == NULL
				  && sk->timeout == TIME_WRITE) 
		{
			if(sk->keepopen) {
				reset_timer(sk,TIME_KEEPOPEN,TCP_TIMEOUT_LEN);
			} else {
				delete_timer(sk);
			}
		}
  	}
  	t1->ack_seq = ntohl(ack);
  	t1->doff = sizeof(*t1)/4;
  	tcp_send_check(t1, sk->saddr, daddr, sizeof(*t1), sk);
  	if (sk->debug)
  		 printk("\rtcp_ack: seq %lx ack %lx\n", sequence, ack);
  	tcp_statistics.TcpOutSegs++;
  	sk->prot->queue_xmit(sk, dev, buff, 1);
}


/* 
 *	This routine builds a generic TCP header. 
 */
 
static int tcp_build_header(struct tcphdr *th, struct sock *sk, int push)
{

	/* FIXME: want to get rid of this. */
	memcpy(th,(void *) &(sk->dummy_th), sizeof(*th));
	th->seq = htonl(sk->write_seq);
	th->psh =(push == 0) ? 1 : 0;
	th->doff = sizeof(*th)/4;
	th->ack = 1;
	th->fin = 0;
	sk->ack_backlog = 0;
	sk->bytes_rcv = 0;
	sk->ack_timed = 0;
	th->ack_seq = htonl(sk->acked_seq);
	sk->window = tcp_select_window(sk);
	th->window = htons(sk->window);

	return(sizeof(*th));
}

/*
 *	This routine copies from a user buffer into a socket,
 *	and starts the transmit system.
 */

static int tcp_write(struct sock *sk, unsigned char *from,
	  int len, int nonblock, unsigned flags)
{
	int copied = 0;
	int copy;
	int tmp;
	struct sk_buff *skb;
	struct sk_buff *send_tmp;
	unsigned char *buff;
	struct proto *prot;
	struct device *dev = NULL;

	sk->inuse=1;
	prot = sk->prot;
	while(len > 0) 
	{
		if (sk->err) 
		{			/* Stop on an error */
			release_sock(sk);
			if (copied) 
				return(copied);
			tmp = -sk->err;
			sk->err = 0;
			return(tmp);
		}

	/*
	 *	First thing we do is make sure that we are established. 
	 */
	
		if (sk->shutdown & SEND_SHUTDOWN) 
		{
			release_sock(sk);
			sk->err = EPIPE;
			if (copied) 
				return(copied);
			sk->err = 0;
			return(-EPIPE);
		}


	/* 
	 *	Wait for a connection to finish.
	 */
	
		while(sk->state != TCP_ESTABLISHED && sk->state != TCP_CLOSE_WAIT) 
		{
			if (sk->err) 
			{
				release_sock(sk);
				if (copied) 
					return(copied);
				tmp = -sk->err;
				sk->err = 0;
				return(tmp);
			}

			if (sk->state != TCP_SYN_SENT && sk->state != TCP_SYN_RECV) 
			{
				release_sock(sk);
				if (copied) 
					return(copied);

				if (sk->err) 
				{
					tmp = -sk->err;
					sk->err = 0;
					return(tmp);
				}

				if (sk->keepopen) 
				{
					send_sig(SIGPIPE, current, 0);
				}
				return(-EPIPE);
			}

			if (nonblock || copied) 
			{
				release_sock(sk);
				if (copied) 
					return(copied);
				return(-EAGAIN);
			}

			release_sock(sk);
			cli();
		
			if (sk->state != TCP_ESTABLISHED &&
		    		sk->state != TCP_CLOSE_WAIT && sk->err == 0) 
		    	{
				interruptible_sleep_on(sk->sleep);
				if (current->signal & ~current->blocked) 
				{
					sti();
					if (copied) 
						return(copied);
					return(-ERESTARTSYS);
				}
			}
			sk->inuse = 1;
			sti();
		}

	/*
	 * The following code can result in copy <= if sk->mss is ever
	 * decreased.  It shouldn't be.  sk->mss is min(sk->mtu, sk->max_window).
	 * sk->mtu is constant once SYN processing is finished.  I.e. we
	 * had better not get here until we've seen his SYN and at least one
	 * valid ack.  (The SYN sets sk->mtu and the ack sets sk->max_window.)
	 * But ESTABLISHED should guarantee that.  sk->max_window is by definition
	 * non-decreasing.  Note that any ioctl to set user_mss must be done
	 * before the exchange of SYN's.  If the initial ack from the other
	 * end has a window of 0, max_window and thus mss will both be 0.
	 */

	/* 
	 *	Now we need to check if we have a half built packet. 
	 */

		if ((skb = tcp_dequeue_partial(sk)) != NULL) 
		{
		        int hdrlen;

		         /* IP header + TCP header */
			hdrlen = ((unsigned long)skb->h.th - (unsigned long)skb->data)
			         + sizeof(struct tcphdr);
	
			/* Add more stuff to the end of skb->len */
			if (!(flags & MSG_OOB)) 
			{
				copy = min(sk->mss - (skb->len - hdrlen), len);
				/* FIXME: this is really a bug. */
				if (copy <= 0) 
				{
			  		printk("TCP: **bug**: \"copy\" <= 0!!\n");
			  		copy = 0;
				}
	  
				memcpy_fromfs(skb->data + skb->len, from, copy);
				skb->len += copy;
				from += copy;
				copied += copy;
				len -= copy;
				sk->write_seq += copy;
			}
			if ((skb->len - hdrlen) >= sk->mss ||
				(flags & MSG_OOB) || !sk->packets_out)
				tcp_send_skb(sk, skb);
			else
				tcp_enqueue_partial(skb, sk);
			continue;
		}

	/*
	 * We also need to worry about the window.
 	 * If window < 1/2 the maximum window we've seen from this
 	 *   host, don't use it.  This is sender side
 	 *   silly window prevention, as specified in RFC1122.
 	 *   (Note that this is different than earlier versions of
 	 *   SWS prevention, e.g. RFC813.).  What we actually do is 
	 *   use the whole MSS.  Since the results in the right
	 *   edge of the packet being outside the window, it will
	 *   be queued for later rather than sent.
	 */

		copy = sk->window_seq - sk->write_seq;
		if (copy <= 0 || copy < (sk->max_window >> 1) || copy > sk->mss)
			copy = sk->mss;
		if (copy > len)
			copy = len;

	/*
	 *	We should really check the window here also. 
	 */
	 
		send_tmp = NULL;
		if (copy < sk->mss && !(flags & MSG_OOB)) 
		{
			/*
			 *	We will release the socket incase we sleep here. 
			 */
			release_sock(sk);
			/*
			 *	NB: following must be mtu, because mss can be increased.
			 *	mss is always <= mtu 
			 */
			skb = prot->wmalloc(sk, sk->mtu + 128 + prot->max_header, 0, GFP_KERNEL);
			sk->inuse = 1;
			send_tmp = skb;
		} 
		else 
		{
			/*
			 *	We will release the socket incase we sleep here. 
			 */
			release_sock(sk);
			skb = prot->wmalloc(sk, copy + prot->max_header , 0, GFP_KERNEL);
  			sk->inuse = 1;
		}

		/*
		 *	If we didn't get any memory, we need to sleep. 
		 */

		if (skb == NULL) 
		{
			if (nonblock) 
			{
				release_sock(sk);
				if (copied) 
					return(copied);
				return(-EAGAIN);
			}

			/*
			 *	FIXME: here is another race condition. 
			 */

			tmp = sk->wmem_alloc;
			release_sock(sk);
			cli();
			/*
			 *	Again we will try to avoid it. 
			 */
			if (tmp <= sk->wmem_alloc &&
				  (sk->state == TCP_ESTABLISHED||sk->state == TCP_CLOSE_WAIT)
				&& sk->err == 0) 
			{
				interruptible_sleep_on(sk->sleep);
				if (current->signal & ~current->blocked) 
				{
					sti();
					if (copied) 
						return(copied);
					return(-ERESTARTSYS);
				}
			}
			sk->inuse = 1;
			sti();
			continue;
		}

		skb->len = 0;
		skb->sk = sk;
		skb->free = 0;
		skb->localroute = sk->localroute|(flags&MSG_DONTROUTE);
	
		buff = skb->data;
	
		/*
		 * FIXME: we need to optimize this.
		 * Perhaps some hints here would be good.
		 */
		
		tmp = prot->build_header(skb, sk->saddr, sk->daddr, &dev,
				 IPPROTO_TCP, sk->opt, skb->mem_len,sk->ip_tos,sk->ip_ttl);
		if (tmp < 0 ) 
		{
			prot->wfree(sk, skb->mem_addr, skb->mem_len);
			release_sock(sk);
			if (copied) 
				return(copied);
			return(tmp);
		}
		skb->len += tmp;
		skb->dev = dev;
		buff += tmp;
		skb->h.th =(struct tcphdr *) buff;
		tmp = tcp_build_header((struct tcphdr *)buff, sk, len-copy);
		if (tmp < 0) 
		{
			prot->wfree(sk, skb->mem_addr, skb->mem_len);
			release_sock(sk);
			if (copied) 
				return(copied);
			return(tmp);
		}

		if (flags & MSG_OOB) 
		{
			((struct tcphdr *)buff)->urg = 1;
			((struct tcphdr *)buff)->urg_ptr = ntohs(copy);
		}
		skb->len += tmp;
		memcpy_fromfs(buff+tmp, from, copy);

		from += copy;
		copied += copy;
		len -= copy;
		skb->len += copy;
		skb->free = 0;
		sk->write_seq += copy;
	
		if (send_tmp != NULL && sk->packets_out) 
		{
			tcp_enqueue_partial(send_tmp, sk);
			continue;
		}
		tcp_send_skb(sk, skb);
	}
	sk->err = 0;

/*
 *	Nagle's rule. Turn Nagle off with TCP_NODELAY for highly
 *	interactive fast network servers. It's meant to be on and
 *	it really improves the throughput though not the echo time
 *	on my slow slip link - Alan
 */

/*
 *	Avoid possible race on send_tmp - c/o Johannes Stille 
 */
 
	if(sk->partial && ((!sk->packets_out) 
     /* If not nagling we can send on the before case too.. */
	      || (sk->nonagle && before(sk->write_seq , sk->window_seq))
      	))
  		tcp_send_partial(sk);

	release_sock(sk);
	return(copied);
}


static int tcp_sendto(struct sock *sk, unsigned char *from,
	   int len, int nonblock, unsigned flags,
	   struct sockaddr_in *addr, int addr_len)
{
	if (flags & ~(MSG_OOB|MSG_DONTROUTE))
		return -EINVAL;
	if (sk->state == TCP_CLOSE)
		return -ENOTCONN;
	if (addr_len < sizeof(*addr))
		return -EINVAL;
	if (addr->sin_family && addr->sin_family != AF_INET) 
		return -EINVAL;
	if (addr->sin_port != sk->dummy_th.dest) 
		return -EISCONN;
	if (addr->sin_addr.s_addr != sk->daddr) 
		return -EISCONN;
	return tcp_write(sk, from, len, nonblock, flags);
}


static void tcp_read_wakeup(struct sock *sk)
{
	int tmp;
	struct device *dev = NULL;
	struct tcphdr *t1;
	struct sk_buff *buff;

	if (!sk->ack_backlog) 
		return;

	/*
	 * FIXME: we need to put code here to prevent this routine from
	 * being called.  Being called once in a while is ok, so only check
	 * if this is the second time in a row.
 	 */

	/*
	 * We need to grab some memory, and put together an ack,
	 * and then put it into the queue to be sent.
	 */

	buff = sk->prot->wmalloc(sk,MAX_ACK_SIZE,1, GFP_ATOMIC);
	if (buff == NULL) 
	{
		/* Try again real soon. */
		reset_timer(sk, TIME_WRITE, 10);
		return;
 	}

	buff->len = sizeof(struct tcphdr);
	buff->sk = sk;
	buff->localroute = sk->localroute;
	
	/*
	 *	Put in the IP header and routing stuff. 
	 */

	tmp = sk->prot->build_header(buff, sk->saddr, sk->daddr, &dev,
			       IPPROTO_TCP, sk->opt, MAX_ACK_SIZE,sk->ip_tos,sk->ip_ttl);
	if (tmp < 0) 
	{
  		buff->free=1;
		sk->prot->wfree(sk, buff->mem_addr, buff->mem_len);
		return;
	}

	buff->len += tmp;
	t1 =(struct tcphdr *)(buff->data +tmp);

	memcpy(t1,(void *) &sk->dummy_th, sizeof(*t1));
	t1->seq = htonl(sk->sent_seq);
	t1->ack = 1;
	t1->res1 = 0;
	t1->res2 = 0;
	t1->rst = 0;
	t1->urg = 0;
	t1->syn = 0;
	t1->psh = 0;
	sk->ack_backlog = 0;
	sk->bytes_rcv = 0;
	sk->window = tcp_select_window(sk);
	t1->window = ntohs(sk->window);
	t1->ack_seq = ntohl(sk->acked_seq);
	t1->doff = sizeof(*t1)/4;
	tcp_send_check(t1, sk->saddr, sk->daddr, sizeof(*t1), sk);
	sk->prot->queue_xmit(sk, dev, buff, 1);
	tcp_statistics.TcpOutSegs++;
}


/*
 * 	FIXME:
 * 	This routine frees used buffers.
 * 	It should consider sending an ACK to let the
 * 	other end know we now have a bigger window.
 */

static void cleanup_rbuf(struct sock *sk)
{
	unsigned long flags;
	unsigned long left;
	struct sk_buff *skb;
	unsigned long rspace;

	if(sk->debug)
	  	printk("cleaning rbuf for sk=%p\n", sk);
  
	save_flags(flags);
	cli();
  
	left = sk->prot->rspace(sk);
 
	/*
	 * We have to loop through all the buffer headers,
	 * and try to free up all the space we can.
	 */

	while((skb=skb_peek(&sk->receive_queue)) != NULL) 
	{
		if (!skb->used) 
			break;
		skb_unlink(skb);
		skb->sk = sk;
		kfree_skb(skb, FREE_READ);
	}

	restore_flags(flags);

	/*
	 * FIXME:
	 * At this point we should send an ack if the difference
	 * in the window, and the amount of space is bigger than
	 * TCP_WINDOW_DIFF.
	 */

	if(sk->debug)
		printk("sk->rspace = %lu, was %lu\n", sk->prot->rspace(sk),
  					    left);
	if ((rspace=sk->prot->rspace(sk)) != left) 
	{
		/*
		 * This area has caused the most trouble.  The current strategy
		 * is to simply do nothing if the other end has room to send at
		 * least 3 full packets, because the ack from those will auto-
		 * matically update the window.  If the other end doesn't think
		 * we have much space left, but we have room for at least 1 more
		 * complete packet than it thinks we do, we will send an ack
		 * immediately.  Otherwise we will wait up to .5 seconds in case
		 * the user reads some more.
		 */
		sk->ack_backlog++;
	/*
	 * It's unclear whether to use sk->mtu or sk->mss here.  They differ only
	 * if the other end is offering a window smaller than the agreed on MSS
	 * (called sk->mtu here).  In theory there's no connection between send
	 * and receive, and so no reason to think that they're going to send
	 * small packets.  For the moment I'm using the hack of reducing the mss
	 * only on the send side, so I'm putting mtu here.
	 */

		if (rspace > (sk->window - sk->bytes_rcv + sk->mtu)) 
		{
			/* Send an ack right now. */
			tcp_read_wakeup(sk);
		} 
		else 
		{
			/* Force it to send an ack soon. */
			int was_active = del_timer(&sk->timer);
			if (!was_active || TCP_ACK_TIME < sk->timer.expires) 
			{
				reset_timer(sk, TIME_WRITE, TCP_ACK_TIME);
			} 
			else
				add_timer(&sk->timer);
		}
	}
} 


/*
 *	Handle reading urgent data. 
 */
 
static int tcp_read_urg(struct sock * sk, int nonblock,
	     unsigned char *to, int len, unsigned flags)
{
	if (sk->urginline || !sk->urg_data || sk->urg_data == URG_READ)
		return -EINVAL;
	if (sk->err) 
	{
		int tmp = -sk->err;
		sk->err = 0;
		return tmp;
	}

	if (sk->state == TCP_CLOSE || sk->done) 
	{
		if (!sk->done) {
			sk->done = 1;
			return 0;
		}
		return -ENOTCONN;
	}

	if (sk->shutdown & RCV_SHUTDOWN) 
	{
		sk->done = 1;
		return 0;
	}
	sk->inuse = 1;
	if (sk->urg_data & URG_VALID) 
	{
		char c = sk->urg_data;
		if (!(flags & MSG_PEEK))
			sk->urg_data = URG_READ;
		put_fs_byte(c, to);
		release_sock(sk);
		return 1;
	}
	release_sock(sk);
	
	/*
	 * Fixed the recv(..., MSG_OOB) behaviour.  BSD docs and
	 * the available implementations agree in this case:
	 * this call should never block, independent of the
	 * blocking state of the socket.
	 * Mike <pall@rz.uni-karlsruhe.de>
	 */
	return -EAGAIN;
}


/*
 *	This routine copies from a sock struct into the user buffer. 
 */
 
static int tcp_read(struct sock *sk, unsigned char *to,
	int len, int nonblock, unsigned flags)
{
	struct wait_queue wait = { current, NULL };
	int copied = 0;
	unsigned long peek_seq;
	unsigned long *seq;
	unsigned long used;

	/* This error should be checked. */
	if (sk->state == TCP_LISTEN)
		return -ENOTCONN;

	/* Urgent data needs to be handled specially. */
	if (flags & MSG_OOB)
		return tcp_read_urg(sk, nonblock, to, len, flags);

	peek_seq = sk->copied_seq;
	seq = &sk->copied_seq;
	if (flags & MSG_PEEK)
		seq = &peek_seq;

	add_wait_queue(sk->sleep, &wait);
	sk->inuse = 1;
	while (len > 0) 
	{
		struct sk_buff * skb;
		unsigned long offset;
	
		/*
		 * are we at urgent data? Stop if we have read anything.
		 */
		if (copied && sk->urg_data && sk->urg_seq == 1+*seq)
			break;

		current->state = TASK_INTERRUPTIBLE;

		skb = skb_peek(&sk->receive_queue);
		do 
		{
			if (!skb)
				break;
			if (before(1+*seq, skb->h.th->seq))
				break;
			offset = 1 + *seq - skb->h.th->seq;
			if (skb->h.th->syn)
				offset--;
			if (offset < skb->len)
				goto found_ok_skb;
			if (!(flags & MSG_PEEK))
				skb->used = 1;
			skb = skb->next;
		}
		while (skb != (struct sk_buff *)&sk->receive_queue);

		if (copied)
			break;

		if (sk->err) 
		{
			copied = -sk->err;
			sk->err = 0;
			break;
		}

		if (sk->state == TCP_CLOSE) 
		{
			if (!sk->done) 
			{
				sk->done = 1;
				break;
			}
			copied = -ENOTCONN;
			break;
		}

		if (sk->shutdown & RCV_SHUTDOWN) 
		{
			sk->done = 1;
			break;
		}
			
		if (nonblock) 
		{
			copied = -EAGAIN;
			break;
		}

		cleanup_rbuf(sk);
		release_sock(sk);
		schedule();
		sk->inuse = 1;

		if (current->signal & ~current->blocked) 
		{
			copied = -ERESTARTSYS;
			break;
		}
		continue;

	found_ok_skb:
		/* Ok so how much can we use ? */
		used = skb->len - offset;
		if (len < used)
			used = len;
		/* do we have urgent data here? */
		if (sk->urg_data) 
		{
			unsigned long urg_offset = sk->urg_seq - (1 + *seq);
			if (urg_offset < used) 
			{
				if (!urg_offset) 
				{
					if (!sk->urginline) 
					{
						++*seq;
						offset++;
						used--;
					}
				}
				else
					used = urg_offset;
			}
		}
		/* Copy it */
		memcpy_tofs(to,((unsigned char *)skb->h.th) +
			skb->h.th->doff*4 + offset, used);
		copied += used;
		len -= used;
		to += used;
		*seq += used;
		if (after(sk->copied_seq+1,sk->urg_seq))
			sk->urg_data = 0;
		if (!(flags & MSG_PEEK) && (used + offset >= skb->len))
			skb->used = 1;
	}
	remove_wait_queue(sk->sleep, &wait);
	current->state = TASK_RUNNING;

	/* Clean up data we have read: This will do ACK frames */
	cleanup_rbuf(sk);
	release_sock(sk);
	return copied;
}

 
/*
 *	Shutdown the sending side of a connection.
 */

void tcp_shutdown(struct sock *sk, int how)
{
	struct sk_buff *buff;
	struct tcphdr *t1, *th;
	struct proto *prot;
	int tmp;
	struct device *dev = NULL;

	/*
	 * We need to grab some memory, and put together a FIN,
	 * and then put it into the queue to be sent.
	 * FIXME:
	 *
	 *	Tim MacKenzie(tym@dibbler.cs.monash.edu.au) 4 Dec '92.
	 *	Most of this is guesswork, so maybe it will work...
	 */

	if (!(how & SEND_SHUTDOWN)) 
		return;
	 
	/*
	 *	If we've already sent a FIN, return. 
	 */
	 
	if (sk->state == TCP_FIN_WAIT1 ||
	    sk->state == TCP_FIN_WAIT2 ||
	    sk->state == TCP_CLOSING ||
	    sk->state == TCP_LAST_ACK ||
	    sk->state == TCP_TIME_WAIT
	) 
	{
		return;
	}
	sk->inuse = 1;

	/*
	 * flag that the sender has shutdown
	 */

	sk->shutdown |= SEND_SHUTDOWN;

	/*
	 *  Clear out any half completed packets. 
	 */

	if (sk->partial)
		tcp_send_partial(sk);

	prot =(struct proto *)sk->prot;
	th =(struct tcphdr *)&sk->dummy_th;
	release_sock(sk); /* incase the malloc sleeps. */
	buff = prot->wmalloc(sk, MAX_RESET_SIZE,1 , GFP_KERNEL);
	if (buff == NULL)
		return;
	sk->inuse = 1;

	buff->sk = sk;
	buff->len = sizeof(*t1);
	buff->localroute = sk->localroute;
	t1 =(struct tcphdr *) buff->data;

	/*
	 *	Put in the IP header and routing stuff. 
	 */

	tmp = prot->build_header(buff,sk->saddr, sk->daddr, &dev,
			   IPPROTO_TCP, sk->opt,
			   sizeof(struct tcphdr),sk->ip_tos,sk->ip_ttl);
	if (tmp < 0) 
	{
  		/*
  		 *	Finish anyway, treat this as a send that got lost. 
  		 *
  		 *	Enter FIN_WAIT1 on normal shutdown, which waits for
  		 *	written data to be completely acknowledged along
  		 *	with an acknowledge to our FIN.
  		 *
  		 *	Enter FIN_WAIT2 on abnormal shutdown -- close before
  		 *	connection established.
  		 */
	  	buff->free=1;
		prot->wfree(sk,buff->mem_addr, buff->mem_len);

		if (sk->state == TCP_ESTABLISHED)
			tcp_set_state(sk,TCP_FIN_WAIT1);
		else if(sk->state == TCP_CLOSE_WAIT)
			tcp_set_state(sk,TCP_LAST_ACK);
		else
			tcp_set_state(sk,TCP_FIN_WAIT2);

		release_sock(sk);
		return;
	}

	t1 =(struct tcphdr *)((char *)t1 +tmp);
	buff->len += tmp;
	buff->dev = dev;
	memcpy(t1, th, sizeof(*t1));
	t1->seq = ntohl(sk->write_seq);
	sk->write_seq++;
	buff->h.seq = sk->write_seq;
	t1->ack = 1;
	t1->ack_seq = ntohl(sk->acked_seq);
	t1->window = ntohs(sk->window=tcp_select_window(sk));
	t1->fin = 1;
	t1->rst = 0;
	t1->doff = sizeof(*t1)/4;
	tcp_send_check(t1, sk->saddr, sk->daddr, sizeof(*t1), sk);

	/*
	 * If there is data in the write queue, the fin must be appended to
	 * the write queue.
 	 */
 	
 	if (skb_peek(&sk->write_queue) != NULL) 
 	{
  		buff->free=0;
		if (buff->next != NULL) 
		{
			printk("tcp_shutdown: next != NULL\n");
			skb_unlink(buff);
		}
		skb_queue_tail(&sk->write_queue, buff);
  	} 
  	else 
  	{
        	sk->sent_seq = sk->write_seq;
		sk->prot->queue_xmit(sk, dev, buff, 0);
	}

	if (sk->state == TCP_ESTABLISHED) 
		tcp_set_state(sk,TCP_FIN_WAIT1);
	else if (sk->state == TCP_CLOSE_WAIT)
		tcp_set_state(sk,TCP_LAST_ACK);
	else
		tcp_set_state(sk,TCP_FIN_WAIT2);

	release_sock(sk);
}


static int
tcp_recvfrom(struct sock *sk, unsigned char *to,
	     int to_len, int nonblock, unsigned flags,
	     struct sockaddr_in *addr, int *addr_len)
{
	int result;
  
	/* 
	 *	Have to check these first unlike the old code. If 
	 *	we check them after we lose data on an error
	 *	which is wrong 
	 */

	if(addr_len)
		*addr_len = sizeof(*addr);
	result=tcp_read(sk, to, to_len, nonblock, flags);

	if (result < 0) 
		return(result);
  
  	if(addr)
  	{
		addr->sin_family = AF_INET;
 		addr->sin_port = sk->dummy_th.dest;
		addr->sin_addr.s_addr = sk->daddr;
	}
	return(result);
}


/*
 *	This routine will send an RST to the other tcp. 
 */
 
static void tcp_reset(unsigned long saddr, unsigned long daddr, struct tcphdr *th,
	  struct proto *prot, struct options *opt, struct device *dev, int tos, int ttl)
{
	struct sk_buff *buff;
	struct tcphdr *t1;
	int tmp;
	struct device *ndev=NULL;
  
/*
 * We need to grab some memory, and put together an RST,
 * and then put it into the queue to be sent.
 */

	buff = prot->wmalloc(NULL, MAX_RESET_SIZE, 1, GFP_ATOMIC);
	if (buff == NULL) 
	  	return;

	buff->len = sizeof(*t1);
	buff->sk = NULL;
	buff->dev = dev;
	buff->localroute = 0;

	t1 =(struct tcphdr *) buff->data;

	/*
	 *	Put in the IP header and routing stuff. 
	 */

	tmp = prot->build_header(buff, saddr, daddr, &ndev, IPPROTO_TCP, opt,
			   sizeof(struct tcphdr),tos,ttl);
	if (tmp < 0) 
	{
  		buff->free = 1;
		prot->wfree(NULL, buff->mem_addr, buff->mem_len);
		return;
	}

	t1 =(struct tcphdr *)((char *)t1 +tmp);
	buff->len += tmp;
	memcpy(t1, th, sizeof(*t1));

	/*
	 *	Swap the send and the receive. 
	 */

	t1->dest = th->source;
	t1->source = th->dest;
	t1->rst = 1;  
	t1->window = 0;
  
	if(th->ack)
	{
		t1->ack = 0;
	  	t1->seq = th->ack_seq;
	  	t1->ack_seq = 0;
	}
	else
	{
	  	t1->ack = 1;
	  	if(!th->syn)
  			t1->ack_seq=htonl(th->seq);
  		else
  			t1->ack_seq=htonl(th->seq+1);
  		t1->seq=0;
	}

	t1->syn = 0;
	t1->urg = 0;
	t1->fin = 0;
	t1->psh = 0;
	t1->doff = sizeof(*t1)/4;
	tcp_send_check(t1, saddr, daddr, sizeof(*t1), NULL);
	prot->queue_xmit(NULL, ndev, buff, 1);
	tcp_statistics.TcpOutSegs++;
}


/*
 *	Look for tcp options. Parses everything but only knows about MSS.
 *      This routine is always called with the packet containing the SYN.
 *      However it may also be called with the ack to the SYN.  So you
 *      can't assume this is always the SYN.  It's always called after
 *      we have set up sk->mtu to our own MTU.
 */
 
static void tcp_options(struct sock *sk, struct tcphdr *th)
{
	unsigned char *ptr;
	int length=(th->doff*4)-sizeof(struct tcphdr);
	int mss_seen = 0;
    
	ptr = (unsigned char *)(th + 1);
  
	while(length>0)
	{
	  	int opcode=*ptr++;
	  	int opsize=*ptr++;
	  	switch(opcode)
	  	{
	  		case TCPOPT_EOL:
	  			return;
	  		case TCPOPT_NOP:
	  			length-=2;
	  			continue;
	  		
	  		default:
	  			if(opsize<=2)	/* Avoid silly options looping forever */
	  				return;
	  			switch(opcode)
	  			{
	  				case TCPOPT_MSS:
	  					if(opsize==4 && th->syn)
	  					{
	  						sk->mtu=min(sk->mtu,ntohs(*(unsigned short *)ptr));
							mss_seen = 1;
	  					}
	  					break;
		  				/* Add other options here as people feel the urge to implement stuff like large windows */
	  			}
	  			ptr+=opsize-2;
	  			length-=opsize;
	  	}
	}
	if (th->syn) 
	{
		if (! mss_seen)
		      sk->mtu=min(sk->mtu, 536);  /* default MSS if none sent */
	}
#ifdef CONFIG_INET_PCTCP
	sk->mss = min(sk->max_window >> 1, sk->mtu);
#else    
	sk->mss = min(sk->max_window, sk->mtu);
#endif  
}

static inline unsigned long default_mask(unsigned long dst)
{
	dst = ntohl(dst);
	if (IN_CLASSA(dst))
		return htonl(IN_CLASSA_NET);
	if (IN_CLASSB(dst))
		return htonl(IN_CLASSB_NET);
	return htonl(IN_CLASSC_NET);
}

/*
 *	Default sequence number picking algorithm.
 */

extern inline long tcp_init_seq(void)
{
	return jiffies * SEQ_TICK - seq_offset; 
}

/*
 *	This routine handles a connection request.
 *	It should make sure we haven't already responded.
 *	Because of the way BSD works, we have to send a syn/ack now.
 *	This also means it will be harder to close a socket which is
 *	listening.
 */
 
static void tcp_conn_request(struct sock *sk, struct sk_buff *skb,
		 unsigned long daddr, unsigned long saddr,
		 struct options *opt, struct device *dev, unsigned long seq)
{
	struct sk_buff *buff;
	struct tcphdr *t1;
	unsigned char *ptr;
	struct sock *newsk;
	struct tcphdr *th;
	struct device *ndev=NULL;
	int tmp;
	struct rtable *rt;
  
	th = skb->h.th;

	/* If the socket is dead, don't accept the connection. */
	if (!sk->dead) 
	{
  		sk->data_ready(sk,0);
	}
	else 
	{
		if(sk->debug)
			printk("Reset on %p: Connect on dead socket.\n",sk);
		tcp_reset(daddr, saddr, th, sk->prot, opt, dev, sk->ip_tos,sk->ip_ttl);
		tcp_statistics.TcpAttemptFails++;
		kfree_skb(skb, FREE_READ);
		return;
	}

	/*
	 * Make sure we can accept more.  This will prevent a
	 * flurry of syns from eating up all our memory.
	 */

	if (sk->ack_backlog >= sk->max_ack_backlog) 
	{
		tcp_statistics.TcpAttemptFails++;
		kfree_skb(skb, FREE_READ);
		return;
	}

	/*
	 * We need to build a new sock struct.
	 * It is sort of bad to have a socket without an inode attached
	 * to it, but the wake_up's will just wake up the listening socket,
	 * and if the listening socket is destroyed before this is taken
	 * off of the queue, this will take care of it.
	 */

	newsk = (struct sock *) kmalloc(sizeof(struct sock), GFP_ATOMIC);
	if (newsk == NULL) 
	{
		/* just ignore the syn.  It will get retransmitted. */
		tcp_statistics.TcpAttemptFails++;
		kfree_skb(skb, FREE_READ);
		return;
	}

	memcpy(newsk, sk, sizeof(*newsk));
	skb_queue_head_init(&newsk->write_queue);
	skb_queue_head_init(&newsk->receive_queue);
	newsk->send_head = NULL;
	newsk->send_tail = NULL;
	skb_queue_head_init(&newsk->back_log);
	newsk->rtt = 0;		/*TCP_CONNECT_TIME<<3*/
	newsk->rto = TCP_TIMEOUT_INIT;
	newsk->mdev = 0;
	newsk->max_window = 0;
	newsk->cong_window = 1;
	newsk->cong_count = 0;
	newsk->ssthresh = 0;
	newsk->backoff = 0;
	newsk->blog = 0;
	newsk->intr = 0;
	newsk->proc = 0;
	newsk->done = 0;
	newsk->partial = NULL;
	newsk->pair = NULL;
	newsk->wmem_alloc = 0;
	newsk->rmem_alloc = 0;
	newsk->localroute = sk->localroute;

	newsk->max_unacked = MAX_WINDOW - TCP_WINDOW_DIFF;

	newsk->err = 0;
	newsk->shutdown = 0;
	newsk->ack_backlog = 0;
	newsk->acked_seq = skb->h.th->seq+1;
	newsk->fin_seq = skb->h.th->seq;
	newsk->copied_seq = skb->h.th->seq;
	newsk->state = TCP_SYN_RECV;
	newsk->timeout = 0;
	newsk->write_seq = seq; 
	newsk->window_seq = newsk->write_seq;
	newsk->rcv_ack_seq = newsk->write_seq;
	newsk->urg_data = 0;
	newsk->retransmits = 0;
	newsk->destroy = 0;
	init_timer(&newsk->timer);
	newsk->timer.data = (unsigned long)newsk;
	newsk->timer.function = &net_timer;
	newsk->dummy_th.source = skb->h.th->dest;
	newsk->dummy_th.dest = skb->h.th->source;
	
	/*
	 *	Swap these two, they are from our point of view. 
	 */
	 
	newsk->daddr = saddr;
	newsk->saddr = daddr;

	put_sock(newsk->num,newsk);
	newsk->dummy_th.res1 = 0;
	newsk->dummy_th.doff = 6;
	newsk->dummy_th.fin = 0;
	newsk->dummy_th.syn = 0;
	newsk->dummy_th.rst = 0;	
	newsk->dummy_th.psh = 0;
	newsk->dummy_th.ack = 0;
	newsk->dummy_th.urg = 0;
	newsk->dummy_th.res2 = 0;
	newsk->acked_seq = skb->h.th->seq + 1;
	newsk->copied_seq = skb->h.th->seq;
	newsk->socket = NULL;

	/*
	 *	Grab the ttl and tos values and use them 
	 */

	newsk->ip_ttl=sk->ip_ttl;
	newsk->ip_tos=skb->ip_hdr->tos;

	/*
	 *	Use 512 or whatever user asked for 
	 */

	/*
	 * 	Note use of sk->user_mss, since user has no direct access to newsk 
	 */

	rt=ip_rt_route(saddr, NULL,NULL);
	
	if(rt!=NULL && (rt->rt_flags&RTF_WINDOW))
		newsk->window_clamp = rt->rt_window;
	else
		newsk->window_clamp = 0;
		
	if (sk->user_mss)
		newsk->mtu = sk->user_mss;
	else if(rt!=NULL && (rt->rt_flags&RTF_MSS))
		newsk->mtu = rt->rt_mss - HEADER_SIZE;
	else 
	{
#ifdef CONFIG_INET_SNARL	/* Sub Nets Are Local */
		if ((saddr ^ daddr) & default_mask(saddr))
#else
		if ((saddr ^ daddr) & dev->pa_mask)
#endif
			newsk->mtu = 576 - HEADER_SIZE;
		else
			newsk->mtu = MAX_WINDOW;
	}

	/*
	 *	But not bigger than device MTU 
	 */

	newsk->mtu = min(newsk->mtu, dev->mtu - HEADER_SIZE);

	/*
	 *	This will min with what arrived in the packet 
	 */

	tcp_options(newsk,skb->h.th);

	buff = newsk->prot->wmalloc(newsk, MAX_SYN_SIZE, 1, GFP_ATOMIC);
	if (buff == NULL) 
	{
		sk->err = -ENOMEM;
		newsk->dead = 1;
		release_sock(newsk);
		kfree_skb(skb, FREE_READ);
		tcp_statistics.TcpAttemptFails++;
		return;
	}
  
	buff->len = sizeof(struct tcphdr)+4;
	buff->sk = newsk;
	buff->localroute = newsk->localroute;

	t1 =(struct tcphdr *) buff->data;

	/*
	 *	Put in the IP header and routing stuff. 
	 */

	tmp = sk->prot->build_header(buff, newsk->saddr, newsk->daddr, &ndev,
			       IPPROTO_TCP, NULL, MAX_SYN_SIZE,sk->ip_tos,sk->ip_ttl);

	/*
	 *	Something went wrong. 
	 */

	if (tmp < 0) 
	{
		sk->err = tmp;
		buff->free=1;
		kfree_skb(buff,FREE_WRITE);
		newsk->dead = 1;
		release_sock(newsk);
		skb->sk = sk;
		kfree_skb(skb, FREE_READ);
		tcp_statistics.TcpAttemptFails++;
		return;
	}

	buff->len += tmp;
	t1 =(struct tcphdr *)((char *)t1 +tmp);
  
	memcpy(t1, skb->h.th, sizeof(*t1));
	buff->h.seq = newsk->write_seq;
	/*
	 *	Swap the send and the receive. 
	 */
	t1->dest = skb->h.th->source;
	t1->source = newsk->dummy_th.source;
	t1->seq = ntohl(newsk->write_seq++);
	t1->ack = 1;
	newsk->window = tcp_select_window(newsk);
	newsk->sent_seq = newsk->write_seq;
	t1->window = ntohs(newsk->window);
	t1->res1 = 0;
	t1->res2 = 0;
	t1->rst = 0;
	t1->urg = 0;
	t1->psh = 0;
	t1->syn = 1;
	t1->ack_seq = ntohl(skb->h.th->seq+1);
	t1->doff = sizeof(*t1)/4+1;
	ptr =(unsigned char *)(t1+1);
	ptr[0] = 2;
	ptr[1] = 4;
	ptr[2] = ((newsk->mtu) >> 8) & 0xff;
	ptr[3] =(newsk->mtu) & 0xff;

	tcp_send_check(t1, daddr, saddr, sizeof(*t1)+4, newsk);
	newsk->prot->queue_xmit(newsk, ndev, buff, 0);

	reset_timer(newsk, TIME_WRITE , TCP_TIMEOUT_INIT);
	skb->sk = newsk;

	/*
	 *	Charge the sock_buff to newsk. 
	 */
	 
	sk->rmem_alloc -= skb->mem_len;
	newsk->rmem_alloc += skb->mem_len;
	
	skb_queue_tail(&sk->receive_queue,skb);
	sk->ack_backlog++;
	release_sock(newsk);
	tcp_statistics.TcpOutSegs++;
}


static void tcp_close(struct sock *sk, int timeout)
{
  	struct sk_buff *buff;
	struct tcphdr *t1, *th;
	struct proto *prot;
	struct device *dev=NULL;
	int tmp;

	/*
	 * We need to grab some memory, and put together a FIN,	
	 * and then put it into the queue to be sent.
	 */
	sk->inuse = 1;
	sk->keepopen = 1;
	sk->shutdown = SHUTDOWN_MASK;

	if (!sk->dead) 
	  	sk->state_change(sk);

	if (timeout == 0) 
	{
		/*
		 *  We need to flush the recv. buffs.  We do this only on the
		 *  descriptor close, not protocol-sourced closes, because the
		 *  reader process may not have drained the data yet!
		 */

		if (skb_peek(&sk->receive_queue) != NULL) 
		{
			struct sk_buff *skb;
			if(sk->debug)
        			printk("Clean rcv queue\n");
  			while((skb=skb_dequeue(&sk->receive_queue))!=NULL)
  				kfree_skb(skb, FREE_READ);
			if(sk->debug)
				printk("Cleaned.\n");
		}
	}

	/*
	 *	Get rid off any half-completed packets. 
	 */
	 
	if (sk->partial) 
	{
		tcp_send_partial(sk);
	}

	switch(sk->state) 
	{
		case TCP_FIN_WAIT1:
		case TCP_FIN_WAIT2:
		case TCP_CLOSING:
			/*
			 * These states occur when we have already closed out
			 * our end.  If there is no timeout, we do not do
			 * anything.  We may still be in the middle of sending
			 * the remainder of our buffer, for example...
			 * resetting the timer would be inappropriate.
			 *
			 * XXX if retransmit count reaches limit, is tcp_close()
			 * called with timeout == 1 ? if not, we need to fix that.
			 */
			if (!timeout) {
				int timer_active;

				timer_active = del_timer(&sk->timer);
				if (timer_active)
					add_timer(&sk->timer);
				else
					reset_timer(sk, TIME_CLOSE, 4 * sk->rto);
			}
			if (timeout) 
				tcp_time_wait(sk);
			release_sock(sk);
			return;	/* break causes a double release - messy */
		case TCP_TIME_WAIT:
		case TCP_LAST_ACK:
			/*
			 * A timeout from these states terminates the TCB.
			 */
			if (timeout) 
			{
		  		tcp_set_state(sk,TCP_CLOSE);
			}
			release_sock(sk);
			return;
		case TCP_LISTEN:
			/* we need to drop any sockets which have been connected,
			   but have not yet been accepted. */
			tcp_close_pending(sk, timeout);
			tcp_set_state(sk,TCP_CLOSE);
			release_sock(sk);
			return;
		case TCP_CLOSE:
			release_sock(sk);
			return;
		case TCP_CLOSE_WAIT:
		case TCP_ESTABLISHED:
		case TCP_SYN_SENT:
		case TCP_SYN_RECV:
			prot =(struct proto *)sk->prot;
			th =(struct tcphdr *)&sk->dummy_th;
			buff = prot->wmalloc(sk, MAX_FIN_SIZE, 1, GFP_ATOMIC);
			if (buff == NULL) 
			{
				/* This will force it to try again later. */
				/* Or it would have if someone released the socket
				   first. Anyway it might work now */
				release_sock(sk);
				if (sk->state != TCP_CLOSE_WAIT)
					tcp_set_state(sk,TCP_ESTABLISHED);
				reset_timer(sk, TIME_CLOSE, 100);
				return;
			}
			buff->sk = sk;
			buff->free = 1;
			buff->len = sizeof(*t1);
			buff->localroute = sk->localroute;
			t1 =(struct tcphdr *) buff->data;
	
			/*
			 *	Put in the IP header and routing stuff. 
			 */
			tmp = prot->build_header(buff,sk->saddr, sk->daddr, &dev,
					 IPPROTO_TCP, sk->opt,
				         sizeof(struct tcphdr),sk->ip_tos,sk->ip_ttl);
			if (tmp < 0) 
			{
				sk->write_seq++;	/* Very important 8) */
				kfree_skb(buff,FREE_WRITE);

				/*
				 * Enter FIN_WAIT1 to await completion of
				 * written out data and ACK to our FIN.
				 */

				if(sk->state==TCP_ESTABLISHED)
					tcp_set_state(sk,TCP_FIN_WAIT1);
				else
					tcp_set_state(sk,TCP_FIN_WAIT2);
				reset_timer(sk, TIME_CLOSE,4*sk->rto);
				if(timeout)
					tcp_time_wait(sk);

				release_sock(sk);
				return;
			}

			t1 =(struct tcphdr *)((char *)t1 +tmp);
			buff->len += tmp;
			buff->dev = dev;
			memcpy(t1, th, sizeof(*t1));
			t1->seq = ntohl(sk->write_seq);
			sk->write_seq++;
			buff->h.seq = sk->write_seq;
			t1->ack = 1;
	
			/* 
			 *	Ack everything immediately from now on. 
			 */

			sk->delay_acks = 0;
			t1->ack_seq = ntohl(sk->acked_seq);
			t1->window = ntohs(sk->window=tcp_select_window(sk));
			t1->fin = 1;
			t1->rst = 0;
			t1->doff = sizeof(*t1)/4;
			tcp_send_check(t1, sk->saddr, sk->daddr, sizeof(*t1), sk);

			tcp_statistics.TcpOutSegs++;
	
			if (skb_peek(&sk->write_queue) == NULL) 
			{
				sk->sent_seq = sk->write_seq;
				prot->queue_xmit(sk, dev, buff, 0);
			} 
			else 
			{
				reset_timer(sk, TIME_WRITE, sk->rto);
				if (buff->next != NULL) 
				{
					printk("tcp_close: next != NULL\n");
					skb_unlink(buff);
				}
				skb_queue_tail(&sk->write_queue, buff);
			}

			/*
			 * If established (normal close), enter FIN_WAIT1.
			 * If in CLOSE_WAIT, enter LAST_ACK
			 * If in CLOSING, remain in CLOSING
			 * otherwise enter FIN_WAIT2
			 */

			if (sk->state == TCP_ESTABLISHED)
				tcp_set_state(sk,TCP_FIN_WAIT1);
			else if (sk->state == TCP_CLOSE_WAIT)
				tcp_set_state(sk,TCP_LAST_ACK);
			else if (sk->state != TCP_CLOSING)
				tcp_set_state(sk,TCP_FIN_WAIT2);
	}
	release_sock(sk);
}


/*
 * This routine takes stuff off of the write queue,
 * and puts it in the xmit queue.
 */
static void
tcp_write_xmit(struct sock *sk)
{
	struct sk_buff *skb;

	/*
	 *	The bytes will have to remain here. In time closedown will
	 *	empty the write queue and all will be happy 
	 */

	if(sk->zapped)
		return;

	while((skb = skb_peek(&sk->write_queue)) != NULL &&
		before(skb->h.seq, sk->window_seq + 1) &&
		(sk->retransmits == 0 ||
		 sk->timeout != TIME_WRITE ||
		 before(skb->h.seq, sk->rcv_ack_seq + 1))
		&& sk->packets_out < sk->cong_window) 
	{
		IS_SKB(skb);
		skb_unlink(skb);
		/* See if we really need to send the packet. */
		if (before(skb->h.seq, sk->rcv_ack_seq +1)) 
		{
			sk->retransmits = 0;
			kfree_skb(skb, FREE_WRITE);
			if (!sk->dead) 
				sk->write_space(sk);
		} 
		else
		{
			struct tcphdr *th;
			struct iphdr *iph;
			int size;
/*
 * put in the ack seq and window at this point rather than earlier,
 * in order to keep them monotonic.  We really want to avoid taking
 * back window allocations.  That's legal, but RFC1122 says it's frowned on.
 * Ack and window will in general have changed since this packet was put
 * on the write queue.
 */
			iph = (struct iphdr *)(skb->data +
					       skb->dev->hard_header_len);
			th = (struct tcphdr *)(((char *)iph) +(iph->ihl << 2));
			size = skb->len - (((unsigned char *) th) - skb->data);
			
			th->ack_seq = ntohl(sk->acked_seq);
			th->window = ntohs(tcp_select_window(sk));

			tcp_send_check(th, sk->saddr, sk->daddr, size, sk);

			sk->sent_seq = skb->h.seq;
			sk->prot->queue_xmit(sk, skb->dev, skb, skb->free);
		}
	}
}


/*
 *	This routine deals with incoming acks, but not outgoing ones.
 */

static int tcp_ack(struct sock *sk, struct tcphdr *th, unsigned long saddr, int len)
{
	unsigned long ack;
	int flag = 0;

	/* 
	 * 1 - there was data in packet as well as ack or new data is sent or 
	 *     in shutdown state
	 * 2 - data from retransmit queue was acked and removed
	 * 4 - window shrunk or data from retransmit queue was acked and removed
	 */

	if(sk->zapped)
		return(1);	/* Dead, cant ack any more so why bother */

	ack = ntohl(th->ack_seq);
	if (ntohs(th->window) > sk->max_window) 
	{
  		sk->max_window = ntohs(th->window);
#ifdef CONFIG_INET_PCTCP
		sk->mss = min(sk->max_window>>1, sk->mtu);
#else
		sk->mss = min(sk->max_window, sk->mtu);
#endif	
	}

	if (sk->retransmits && sk->timeout == TIME_KEEPOPEN)
	  	sk->retransmits = 0;

	if (after(ack, sk->sent_seq) || before(ack, sk->rcv_ack_seq)) 
	{
		if(sk->debug)
			printk("Ack ignored %lu %lu\n",ack,sk->sent_seq);
			
		/*
		 *	Keepalive processing.
		 */
		 
		if (after(ack, sk->sent_seq) || (sk->state != TCP_ESTABLISHED && sk->state != TCP_CLOSE_WAIT)) 
		{
			return(0);
		}
		if (sk->keepopen) 
		{
			if(sk->timeout==TIME_KEEPOPEN)
				reset_timer(sk, TIME_KEEPOPEN, TCP_TIMEOUT_LEN);
		}
		return(1);
	}

	if (len != th->doff*4) 
		flag |= 1;

	/* See if our window has been shrunk. */

	if (after(sk->window_seq, ack+ntohs(th->window))) 
	{
		/*
		 * We may need to move packets from the send queue
		 * to the write queue, if the window has been shrunk on us.
		 * The RFC says you are not allowed to shrink your window
		 * like this, but if the other end does, you must be able
		 * to deal with it.
		 */
		struct sk_buff *skb;
		struct sk_buff *skb2;
		struct sk_buff *wskb = NULL;
  	
		skb2 = sk->send_head;
		sk->send_head = NULL;
		sk->send_tail = NULL;
	
		flag |= 4;
	
		sk->window_seq = ack + ntohs(th->window);
		cli();
		while (skb2 != NULL) 
		{
			skb = skb2;
			skb2 = skb->link3;
			skb->link3 = NULL;
			if (after(skb->h.seq, sk->window_seq)) 
			{
				if (sk->packets_out > 0) 
					sk->packets_out--;
				/* We may need to remove this from the dev send list. */
				if (skb->next != NULL) 
				{
					skb_unlink(skb);				
				}
				/* Now add it to the write_queue. */
				if (wskb == NULL)
					skb_queue_head(&sk->write_queue,skb);
				else
					skb_append(wskb,skb);
				wskb = skb;
			} 
			else 
			{
				if (sk->send_head == NULL) 
				{
					sk->send_head = skb;
					sk->send_tail = skb;
				}
				else
				{
					sk->send_tail->link3 = skb;
					sk->send_tail = skb;
				}
				skb->link3 = NULL;
			}
		}
		sti();
	}

	if (sk->send_tail == NULL || sk->send_head == NULL) 
	{
		sk->send_head = NULL;
		sk->send_tail = NULL;
		sk->packets_out= 0;
	}

	sk->window_seq = ack + ntohs(th->window);

	/* We don't want too many packets out there. */
	if (sk->timeout == TIME_WRITE && 
		sk->cong_window < 2048 && after(ack, sk->rcv_ack_seq)) 
	{
/* 
 * This is Jacobson's slow start and congestion avoidance. 
 * SIGCOMM '88, p. 328.  Because we keep cong_window in integral
 * mss's, we can't do cwnd += 1 / cwnd.  Instead, maintain a 
 * counter and increment it once every cwnd times.  It's possible
 * that this should be done only if sk->retransmits == 0.  I'm
 * interpreting "new data is acked" as including data that has
 * been retransmitted but is just now being acked.
 */
		if (sk->cong_window < sk->ssthresh)  
		  /* 
		   *	In "safe" area, increase
		   */
			sk->cong_window++;
		else 
		{
		  /*
		   *	In dangerous area, increase slowly.  In theory this is
		   *  	sk->cong_window += 1 / sk->cong_window
		   */
			if (sk->cong_count >= sk->cong_window) 
			{
				sk->cong_window++;
				sk->cong_count = 0;
			}
			else 
				sk->cong_count++;
		}
	}

	sk->rcv_ack_seq = ack;

	/*
	 * if this ack opens up a zero window, clear backoff.  It was
	 * being used to time the probes, and is probably far higher than
	 * it needs to be for normal retransmission.
	 */

	if (sk->timeout == TIME_PROBE0) 
	{
  		if (skb_peek(&sk->write_queue) != NULL &&   /* should always be non-null */
		    ! before (sk->window_seq, sk->write_queue.next->h.seq)) 
		{
			sk->retransmits = 0;
			sk->backoff = 0;
		  /*
		   *	Recompute rto from rtt.  this eliminates any backoff.
		   */

			sk->rto = ((sk->rtt >> 2) + sk->mdev) >> 1;
			if (sk->rto > 120*HZ)
				sk->rto = 120*HZ;
			if (sk->rto < 20)	/* Was 1*HZ, then 1 - turns out we must allow about
						   .2 of a second because of BSD delayed acks - on a 100Mb/sec link
						   .2 of a second is going to need huge windows (SIGH) */
				sk->rto = 20;
		}
	}

  /* 
   *	See if we can take anything off of the retransmit queue.
   */
   
	while(sk->send_head != NULL) 
	{
		/* Check for a bug. */
		if (sk->send_head->link3 &&
		    after(sk->send_head->h.seq, sk->send_head->link3->h.seq)) 
			printk("INET: tcp.c: *** bug send_list out of order.\n");
		if (before(sk->send_head->h.seq, ack+1)) 
		{
			struct sk_buff *oskb;	
			if (sk->retransmits) 
			{	
				/*
				 *	We were retransmitting.  don't count this in RTT est 
				 */
				flag |= 2;

				/*
				 * even though we've gotten an ack, we're still
				 * retransmitting as long as we're sending from
				 * the retransmit queue.  Keeping retransmits non-zero
				 * prevents us from getting new data interspersed with
				 * retransmissions.
				 */

				if (sk->send_head->link3)
					sk->retransmits = 1;
				else
					sk->retransmits = 0;
			}
  			/*
			 * Note that we only reset backoff and rto in the
			 * rtt recomputation code.  And that doesn't happen
			 * if there were retransmissions in effect.  So the
			 * first new packet after the retransmissions is
			 * sent with the backoff still in effect.  Not until
			 * we get an ack from a non-retransmitted packet do
			 * we reset the backoff and rto.  This allows us to deal
			 * with a situation where the network delay has increased
			 * suddenly.  I.e. Karn's algorithm. (SIGCOMM '87, p5.)
			 */

			/*
			 *	We have one less packet out there. 
			 */
			 
			if (sk->packets_out > 0) 
				sk->packets_out --;
			/* 
			 *	Wake up the process, it can probably write more. 
			 */
			if (!sk->dead) 
				sk->write_space(sk);
			oskb = sk->send_head;

			if (!(flag&2)) 
			{
				long m;
	
				/*
				 *	The following amusing code comes from Jacobson's
				 *	article in SIGCOMM '88.  Note that rtt and mdev
				 *	are scaled versions of rtt and mean deviation.
				 *	This is designed to be as fast as possible 
				 *	m stands for "measurement".
				 */
	
				m = jiffies - oskb->when;  /* RTT */
				if(m<=0)
					m=1;		/* IS THIS RIGHT FOR <0 ??? */
				m -= (sk->rtt >> 3);    /* m is now error in rtt est */
				sk->rtt += m;           /* rtt = 7/8 rtt + 1/8 new */
				if (m < 0)
					m = -m;		/* m is now abs(error) */
				m -= (sk->mdev >> 2);   /* similar update on mdev */
				sk->mdev += m;	    	/* mdev = 3/4 mdev + 1/4 new */
	
				/*
				 *	Now update timeout.  Note that this removes any backoff.
				 */
			 
				sk->rto = ((sk->rtt >> 2) + sk->mdev) >> 1;
				if (sk->rto > 120*HZ)
					sk->rto = 120*HZ;
				if (sk->rto < 20)	/* Was 1*HZ - keep .2 as minimum cos of the BSD delayed acks */
					sk->rto = 20;
				sk->backoff = 0;
			}
			flag |= (2|4);
			cli();
			oskb = sk->send_head;
			IS_SKB(oskb);
			sk->send_head = oskb->link3;
			if (sk->send_head == NULL) 
			{
				sk->send_tail = NULL;
			}

		/*
		 *	We may need to remove this from the dev send list. 
		 */

			if (oskb->next)
				skb_unlink(oskb);
			sti();
			kfree_skb(oskb, FREE_WRITE); /* write. */
			if (!sk->dead) 
				sk->write_space(sk);
		}
		else
		{
			break;
		}
	}

	/*
	 * XXX someone ought to look at this too.. at the moment, if skb_peek()
	 * returns non-NULL, we complete ignore the timer stuff in the else
	 * clause.  We ought to organize the code so that else clause can
	 * (should) be executed regardless, possibly moving the PROBE timer
	 * reset over.  The skb_peek() thing should only move stuff to the
	 * write queue, NOT also manage the timer functions.
	 */

	/*
	 * Maybe we can take some stuff off of the write queue,
	 * and put it onto the xmit queue.
	 */
	if (skb_peek(&sk->write_queue) != NULL) 
	{
		if (after (sk->window_seq+1, sk->write_queue.next->h.seq) &&
		        (sk->retransmits == 0 || 
			 sk->timeout != TIME_WRITE ||
			 before(sk->write_queue.next->h.seq, sk->rcv_ack_seq + 1))
			&& sk->packets_out < sk->cong_window) 
		{
			flag |= 1;
			tcp_write_xmit(sk);
		}
		else if (before(sk->window_seq, sk->write_queue.next->h.seq) &&
 			sk->send_head == NULL &&
 			sk->ack_backlog == 0 &&
 			sk->state != TCP_TIME_WAIT) 
 		{
 	        	reset_timer(sk, TIME_PROBE0, sk->rto);
 		}		
	}
	else
	{
		/*
		 * from TIME_WAIT we stay in TIME_WAIT as long as we rx packets
		 * from TCP_CLOSE we don't do anything
		 *
		 * from anything else, if there is write data (or fin) pending,
		 * we use a TIME_WRITE timeout, else if keepalive we reset to
		 * a KEEPALIVE timeout, else we delete the timer.
		 *
		 * We do not set flag for nominal write data, otherwise we may
		 * force a state where we start to write itsy bitsy tidbits
		 * of data.
		 */

		switch(sk->state) {
		case TCP_TIME_WAIT:
			/*
			 * keep us in TIME_WAIT until we stop getting packets,
			 * reset the timeout.
			 */
			reset_timer(sk, TIME_CLOSE, TCP_TIMEWAIT_LEN);
			break;
		case TCP_CLOSE:
			/*
			 * don't touch the timer.
			 */
			break;
		default:
			/*
			 * must check send_head, write_queue, and ack_backlog
			 * to determine which timeout to use.
			 */
			if (sk->send_head || skb_peek(&sk->write_queue) != NULL || sk->ack_backlog) {
				reset_timer(sk, TIME_WRITE, sk->rto);
			} else if (sk->keepopen) {
				reset_timer(sk, TIME_KEEPOPEN, TCP_TIMEOUT_LEN);
			} else {
				delete_timer(sk);
			}
			break;
		}
#ifdef NOTDEF
		if (sk->send_head == NULL && sk->ack_backlog == 0 &&
		sk->state != TCP_TIME_WAIT && !sk->keepopen) 
		{
			if (!sk->dead)
				sk->write_space(sk);
			if (sk->keepopen) {
				reset_timer(sk, TIME_KEEPOPEN, TCP_TIMEOUT_LEN);
			} else {
				delete_timer(sk);
			}
		}
		else
		{
			if (sk->state != (unsigned char) sk->keepopen) 
			{
				reset_timer(sk, TIME_WRITE, sk->rto);
			}
			if (sk->state == TCP_TIME_WAIT) 
			{
				reset_timer(sk, TIME_CLOSE, TCP_TIMEWAIT_LEN);
			}	
		}
#endif
	}

	if (sk->packets_out == 0 && sk->partial != NULL &&
		skb_peek(&sk->write_queue) == NULL && sk->send_head == NULL) 
	{
		flag |= 1;
		tcp_send_partial(sk);
	}

	/*
	 * In the LAST_ACK case, the other end FIN'd us.  We then FIN'd them, and
	 * we are now waiting for an acknowledge to our FIN.  The other end is
	 * already in TIME_WAIT.
	 *
	 * Move to TCP_CLOSE on success.
	 */

	if (sk->state == TCP_LAST_ACK) 
	{
		if (!sk->dead)
			sk->state_change(sk);
		if (sk->rcv_ack_seq == sk->write_seq && sk->acked_seq == sk->fin_seq) 
		{
			flag |= 1;
			tcp_time_wait(sk);
			sk->shutdown = SHUTDOWN_MASK;
		}
	}

	/*
	 * Incoming ACK to a FIN we sent in the case of our initiating the close.
	 *
	 * Move to FIN_WAIT2 to await a FIN from the other end. Set
	 * SEND_SHUTDOWN but not RCV_SHUTDOWN as data can still be coming in.
	 */

	if (sk->state == TCP_FIN_WAIT1) 
	{

		if (!sk->dead) 
			sk->state_change(sk);
		if (sk->rcv_ack_seq == sk->write_seq) 
		{
			flag |= 1;
			sk->shutdown |= SEND_SHUTDOWN;
			tcp_set_state(sk, TCP_FIN_WAIT2);
		}
	}

	/*
	 *	Incoming ACK to a FIN we sent in the case of a simultaneous close.
	 *
	 *	Move to TIME_WAIT
	 */

	if (sk->state == TCP_CLOSING) 
	{

		if (!sk->dead) 
			sk->state_change(sk);
		if (sk->rcv_ack_seq == sk->write_seq) 
		{
			flag |= 1;
			tcp_time_wait(sk);
		}
	}

	/*
	 * I make no guarantees about the first clause in the following
	 * test, i.e. "(!flag) || (flag&4)".  I'm not entirely sure under
	 * what conditions "!flag" would be true.  However I think the rest
	 * of the conditions would prevent that from causing any
	 * unnecessary retransmission. 
	 *   Clearly if the first packet has expired it should be 
	 * retransmitted.  The other alternative, "flag&2 && retransmits", is
	 * harder to explain:  You have to look carefully at how and when the
	 * timer is set and with what timeout.  The most recent transmission always
	 * sets the timer.  So in general if the most recent thing has timed
	 * out, everything before it has as well.  So we want to go ahead and
	 * retransmit some more.  If we didn't explicitly test for this
	 * condition with "flag&2 && retransmits", chances are "when + rto < jiffies"
	 * would not be true.  If you look at the pattern of timing, you can
	 * show that rto is increased fast enough that the next packet would
	 * almost never be retransmitted immediately.  Then you'd end up
	 * waiting for a timeout to send each packet on the retransmission
	 * queue.  With my implementation of the Karn sampling algorithm,
	 * the timeout would double each time.  The net result is that it would
	 * take a hideous amount of time to recover from a single dropped packet.
	 * It's possible that there should also be a test for TIME_WRITE, but
	 * I think as long as "send_head != NULL" and "retransmit" is on, we've
	 * got to be in real retransmission mode.
	 *   Note that ip_do_retransmit is called with all==1.  Setting cong_window
	 * back to 1 at the timeout will cause us to send 1, then 2, etc. packets.
	 * As long as no further losses occur, this seems reasonable.
	 */
	
	if (((!flag) || (flag&4)) && sk->send_head != NULL &&
	       (((flag&2) && sk->retransmits) ||
	       (sk->send_head->when + sk->rto < jiffies))) 
	{
		ip_do_retransmit(sk, 1);
		reset_timer(sk, TIME_WRITE, sk->rto);
	}

	return(1);
}


/*
 *	This routine handles the data.  If there is room in the buffer,
 *	it will be have already been moved into it.  If there is no
 *	room, then we will just have to discard the packet.
 */

static int tcp_data(struct sk_buff *skb, struct sock *sk, 
	 unsigned long saddr, unsigned short len)
{
	struct sk_buff *skb1, *skb2;
	struct tcphdr *th;
	int dup_dumped=0;
	unsigned long new_seq;
	struct sk_buff *tail;
	unsigned long shut_seq;

	th = skb->h.th;
	skb->len = len -(th->doff*4);

	/* The bytes in the receive read/assembly queue has increased. Needed for the
	   low memory discard algorithm */
	   
	sk->bytes_rcv += skb->len;
	
	if (skb->len == 0 && !th->fin && !th->urg && !th->psh) 
	{
		/* 
		 *	Don't want to keep passing ack's back and forth. 
		 *	(someone sent us dataless, boring frame)
		 */
		if (!th->ack)
			tcp_send_ack(sk->sent_seq, sk->acked_seq,sk, th, saddr);
		kfree_skb(skb, FREE_READ);
		return(0);
	}
	
	/*
	 *	We no longer have anyone receiving data on this connection.
	 */

	if(sk->shutdown & RCV_SHUTDOWN)
	{
		new_seq= th->seq + skb->len + th->syn;	/* Right edge of _data_ part of frame */
		
		/*
		 *	This is subtle and not nice. When we shut down we can
		 *	have data in the queue and acked_seq therefore not
		 *	pointing to the last byte that will be read. Thus
		 *	the naive implementation:
		 *		after(new_seq,sk->acked_seq+1)
		 *	will cause bogus resets IFF a resend of a frame that has
		 *	been queued but not yet read after a shutdown has been done.
		 *	What we do now is a bit more complex but works as
		 *	follows. If the queue is empty copied_seq+1 is right (+1 for FIN)
		 *	if the queue has data the shutdown occurs at the right edge of
		 *	the last packet queued +1
		 *
		 *	We can't simply ack data beyond this point as it has
		 *	and will never be received by an application.
		 */
		tail=skb_peek(&sk->receive_queue);
		if(tail!=NULL)
		{
			tail=sk->receive_queue.prev;
			shut_seq=tail->h.th->seq+tail->len+1;
		}
		else
			shut_seq=sk->copied_seq+1;
		
		if(after(new_seq,shut_seq))
		{
			sk->acked_seq = new_seq + th->fin;
			if(sk->debug)
				printk("Data arrived on %p after close [Data right edge %lX, Socket shut on %lX] %d\n",
					sk, new_seq, shut_seq, sk->blog);
			tcp_reset(sk->saddr, sk->daddr, skb->h.th,
				sk->prot, NULL, skb->dev, sk->ip_tos, sk->ip_ttl);
			tcp_statistics.TcpEstabResets++;
			tcp_set_state(sk,TCP_CLOSE);
			sk->err = EPIPE;
			sk->shutdown = SHUTDOWN_MASK;
			kfree_skb(skb, FREE_READ);
			if (!sk->dead)
				sk->state_change(sk);
			return(0);
		}
	}
	/*
	 * 	Now we have to walk the chain, and figure out where this one
	 * 	goes into it.  This is set up so that the last packet we received
	 * 	will be the first one we look at, that way if everything comes
	 * 	in order, there will be no performance loss, and if they come
	 * 	out of order we will be able to fit things in nicely.
	 */

	/* 
	 *	This should start at the last one, and then go around forwards.
	 */

	if (skb_peek(&sk->receive_queue) == NULL) 	/* Empty queue is easy case */
	{
		skb_queue_head(&sk->receive_queue,skb);
		skb1= NULL;
	} 
	else
	{
		for(skb1=sk->receive_queue.prev; ; skb1 = skb1->prev) 
		{
			if(sk->debug)
			{
				printk("skb1=%p :", skb1);
				printk("skb1->h.th->seq = %ld: ", skb1->h.th->seq);
				printk("skb->h.th->seq = %ld\n",skb->h.th->seq);
				printk("copied_seq = %ld acked_seq = %ld\n", sk->copied_seq,
						sk->acked_seq);
			}
			
			/*
			 *	Optimisation: Duplicate frame or extension of previous frame from
			 *	same sequence point (lost ack case).
			 *	The frame contains duplicate data or replaces a previous frame
			 *	discard the previous frame (safe as sk->inuse is set) and put
			 *	the new one in its place.
			 */
			 
			if (th->seq==skb1->h.th->seq && skb->len>= skb1->len)
			{
				skb_append(skb1,skb);
				skb_unlink(skb1);
				kfree_skb(skb1,FREE_READ);
				dup_dumped=1;
				skb1=NULL;
				break;
			}
			
			/*
			 *	Found where it fits
			 */
			 
			if (after(th->seq+1, skb1->h.th->seq))
			{
				skb_append(skb1,skb);
				break;
			}
			
			/*
			 *	See if we've hit the start. If so insert.
			 */
			if (skb1 == skb_peek(&sk->receive_queue))
			{
				skb_queue_head(&sk->receive_queue, skb);
				break;
			}
		}
  	}

	/*
	 *	Figure out what the ack value for this frame is
	 */
	 
 	th->ack_seq = th->seq + skb->len;
 	if (th->syn) 
 		th->ack_seq++;
 	if (th->fin)
 		th->ack_seq++;

	if (before(sk->acked_seq, sk->copied_seq)) 
	{
		printk("*** tcp.c:tcp_data bug acked < copied\n");
		sk->acked_seq = sk->copied_seq;
	}

	/*
	 *	Now figure out if we can ack anything.
	 */

	if ((!dup_dumped && (skb1 == NULL || skb1->acked)) || before(th->seq, sk->acked_seq+1)) 
	{
		if (before(th->seq, sk->acked_seq+1)) 
		{
			int newwindow;

			if (after(th->ack_seq, sk->acked_seq)) 
			{
				newwindow = sk->window-(th->ack_seq - sk->acked_seq);
				if (newwindow < 0)
					newwindow = 0;	
				sk->window = newwindow;
				sk->acked_seq = th->ack_seq;
			}
			skb->acked = 1;

			/* 
			 *	When we ack the fin, we turn on the RCV_SHUTDOWN flag.
			 */

			if (skb->h.th->fin) 
			{
				if (!sk->dead) 
					sk->state_change(sk);
				sk->shutdown |= RCV_SHUTDOWN;
			}
	  
			for(skb2 = skb->next;
			    skb2 != (struct sk_buff *)&sk->receive_queue;
			    skb2 = skb2->next) 
			{
				if (before(skb2->h.th->seq, sk->acked_seq+1)) 
				{
					if (after(skb2->h.th->ack_seq, sk->acked_seq))
					{
						newwindow = sk->window -
						 (skb2->h.th->ack_seq - sk->acked_seq);
						if (newwindow < 0)
							newwindow = 0;	
						sk->window = newwindow;
						sk->acked_seq = skb2->h.th->ack_seq;
					}
					skb2->acked = 1;
					/*
					 * 	When we ack the fin, we turn on
					 * 	the RCV_SHUTDOWN flag.
					 */
					if (skb2->h.th->fin) 
					{
						sk->shutdown |= RCV_SHUTDOWN;
						if (!sk->dead)
							sk->state_change(sk);
					}

					/*
					 *	Force an immediate ack.
					 */
					 
					sk->ack_backlog = sk->max_ack_backlog;
				}
				else
				{
					break;
				}
			}

			/*
			 *	This also takes care of updating the window.
			 *	This if statement needs to be simplified.
			 */
			if (!sk->delay_acks ||
			    sk->ack_backlog >= sk->max_ack_backlog || 
			    sk->bytes_rcv > sk->max_unacked || th->fin) {
	/*			tcp_send_ack(sk->sent_seq, sk->acked_seq,sk,th, saddr); */
			}
			else 
			{
				sk->ack_backlog++;
				if(sk->debug)
					printk("Ack queued.\n");
				reset_timer(sk, TIME_WRITE, TCP_ACK_TIME);
			}
		}
	}

	/*
	 *	If we've missed a packet, send an ack.
	 *	Also start a timer to send another.
	 */
	 
	if (!skb->acked) 
	{
	
	/*
	 *	This is important.  If we don't have much room left,
	 *	we need to throw out a few packets so we have a good
	 *	window.  Note that mtu is used, not mss, because mss is really
	 *	for the send side.  He could be sending us stuff as large as mtu.
	 */
		 
		while (sk->prot->rspace(sk) < sk->mtu) 
		{
			skb1 = skb_peek(&sk->receive_queue);
			if (skb1 == NULL) 
			{
				printk("INET: tcp.c:tcp_data memory leak detected.\n");
				break;
			}

			/*
			 *	Don't throw out something that has been acked. 
			 */
		 
			if (skb1->acked) 
			{
				break;
			}
		
			skb_unlink(skb1);
			kfree_skb(skb1, FREE_READ);
		}
		tcp_send_ack(sk->sent_seq, sk->acked_seq, sk, th, saddr);
		sk->ack_backlog++;
		reset_timer(sk, TIME_WRITE, TCP_ACK_TIME);
	}
	else
	{
		/* We missed a packet.  Send an ack to try to resync things. */
		tcp_send_ack(sk->sent_seq, sk->acked_seq, sk, th, saddr);
	}

	/*
	 *	Now tell the user we may have some data. 
	 */
	 
	if (!sk->dead) 
	{
        	if(sk->debug)
        		printk("Data wakeup.\n");
		sk->data_ready(sk,0);
	} 
	return(0);
}


static void tcp_check_urg(struct sock * sk, struct tcphdr * th)
{
	unsigned long ptr = ntohs(th->urg_ptr);

	if (ptr)
		ptr--;
	ptr += th->seq;

	/* ignore urgent data that we've already seen and read */
	if (after(sk->copied_seq+1, ptr))
		return;

	/* do we already have a newer (or duplicate) urgent pointer? */
	if (sk->urg_data && !after(ptr, sk->urg_seq))
		return;

	/* tell the world about our new urgent pointer */
	if (sk->proc != 0) {
		if (sk->proc > 0) {
			kill_proc(sk->proc, SIGURG, 1);
		} else {
			kill_pg(-sk->proc, SIGURG, 1);
		}
	}
	sk->urg_data = URG_NOTYET;
	sk->urg_seq = ptr;
}

static inline int tcp_urg(struct sock *sk, struct tcphdr *th,
	unsigned long saddr, unsigned long len)
{
	unsigned long ptr;

	/* check if we get a new urgent pointer */
	if (th->urg)
		tcp_check_urg(sk,th);

	/* do we wait for any urgent data? */
	if (sk->urg_data != URG_NOTYET)
		return 0;

	/* is the urgent pointer pointing into this packet? */
	ptr = sk->urg_seq - th->seq + th->doff*4;
	if (ptr >= len)
		return 0;

	/* ok, got the correct packet, update info */
	sk->urg_data = URG_VALID | *(ptr + (unsigned char *) th);
	if (!sk->dead)
		sk->data_ready(sk,0);
	return 0;
}


/*
 *  This deals with incoming fins. 'Linus at 9 O'clock' 8-) 
 *
 *  If we are ESTABLISHED, a received fin moves us to CLOSE-WAIT
 *  (and thence onto LAST-ACK and finally, CLOSE, we never enter
 *  TIME-WAIT)
 *
 *  If we are in FINWAIT-1, a received FIN indicates simultaneous
 *  close and we go into CLOSING (and later onto TIME-WAIT)
 *
 *  If we are in FINWAIT-2, a received FIN moves us to TIME-WAIT.
 *
 */
static int tcp_fin(struct sk_buff *skb, struct sock *sk, struct tcphdr *th, 
	 unsigned long saddr, struct device *dev)
{
	sk->fin_seq = th->seq + skb->len + th->syn + th->fin;

	if (!sk->dead) 
	{
		sk->state_change(sk);
	}

	switch(sk->state) 
	{
		case TCP_SYN_RECV:
		case TCP_SYN_SENT:
		case TCP_ESTABLISHED:
			/*
			 * move to CLOSE_WAIT, tcp_data() already handled
			 * sending the ack.
			 */
			reset_timer(sk, TIME_CLOSE, TCP_TIMEOUT_LEN);
			tcp_set_state(sk,TCP_CLOSE_WAIT);
			if (th->rst)
				sk->shutdown = SHUTDOWN_MASK;
			break;

		case TCP_CLOSE_WAIT:
		case TCP_CLOSING:
			/*
			 * received a retransmission of the FIN, do
			 * nothing.
			 */
			break;
		case TCP_TIME_WAIT:
			/*
			 * received a retransmission of the FIN,
			 * restart the TIME_WAIT timer.
			 */
			reset_timer(sk, TIME_CLOSE, TCP_TIMEWAIT_LEN);
			return(0);
		case TCP_FIN_WAIT1:
			/*
			 * This case occurs when a simultaneous close
			 * happens, we must ack the received FIN and
			 * enter the CLOSING state.
			 *
			 * XXX timeout not set properly
			 */

			reset_timer(sk, TIME_CLOSE, TCP_TIMEWAIT_LEN);
			tcp_set_state(sk,TCP_CLOSING);
			break;
		case TCP_FIN_WAIT2:
			/*
			 * received a FIN -- send ACK and enter TIME_WAIT
			 */
			reset_timer(sk, TIME_CLOSE, TCP_TIMEWAIT_LEN);
			sk->shutdown|=SHUTDOWN_MASK;
			tcp_set_state(sk,TCP_TIME_WAIT);
			break;
		case TCP_CLOSE:
			/*
			 * already in CLOSE
			 */
			break;
		default:
			tcp_set_state(sk,TCP_LAST_ACK);
	
			/* Start the timers. */
			reset_timer(sk, TIME_CLOSE, TCP_TIMEWAIT_LEN);
			return(0);
	}
	sk->ack_backlog++;

	return(0);
}


/* This will accept the next outstanding connection. */
static struct sock *
tcp_accept(struct sock *sk, int flags)
{
	struct sock *newsk;
	struct sk_buff *skb;
  
  /*
   * We need to make sure that this socket is listening,
   * and that it has something pending.
   */

	if (sk->state != TCP_LISTEN) 
	{
		sk->err = EINVAL;
		return(NULL); 
	}

	/* Avoid the race. */
	cli();
	sk->inuse = 1;

	while((skb = tcp_dequeue_established(sk)) == NULL) 
	{
		if (flags & O_NONBLOCK) 
		{
			sti();
			release_sock(sk);
			sk->err = EAGAIN;
			return(NULL);
		}

		release_sock(sk);
		interruptible_sleep_on(sk->sleep);
		if (current->signal & ~current->blocked) 
		{
			sti();
			sk->err = ERESTARTSYS;
			return(NULL);
		}
		sk->inuse = 1;
  	}
	sti();

	/*
	 *	Now all we need to do is return skb->sk. 
	 */

	newsk = skb->sk;

	kfree_skb(skb, FREE_READ);
	sk->ack_backlog--;
	release_sock(sk);
	return(newsk);
}


/*
 *	This will initiate an outgoing connection. 
 */
 
static int tcp_connect(struct sock *sk, struct sockaddr_in *usin, int addr_len)
{
	struct sk_buff *buff;
	struct device *dev=NULL;
	unsigned char *ptr;
	int tmp;
	int atype;
	struct tcphdr *t1;
	struct rtable *rt;

	if (sk->state != TCP_CLOSE) 
		return(-EISCONN);

	if (addr_len < 8) 
		return(-EINVAL);

	if (usin->sin_family && usin->sin_family != AF_INET) 
		return(-EAFNOSUPPORT);

  	/*
  	 *	connect() to INADDR_ANY means loopback (BSD'ism).
  	 */
  	
  	if(usin->sin_addr.s_addr==INADDR_ANY)
		usin->sin_addr.s_addr=ip_my_addr();
		  
	/*
	 *	Don't want a TCP connection going to a broadcast address 
	 */

	if ((atype=ip_chk_addr(usin->sin_addr.s_addr)) == IS_BROADCAST || atype==IS_MULTICAST) 
		return -ENETUNREACH;
  
	sk->inuse = 1;
	sk->daddr = usin->sin_addr.s_addr;
	sk->write_seq = jiffies * SEQ_TICK - seq_offset;
	sk->window_seq = sk->write_seq;
	sk->rcv_ack_seq = sk->write_seq -1;
	sk->err = 0;
	sk->dummy_th.dest = usin->sin_port;
	release_sock(sk);

	buff = sk->prot->wmalloc(sk,MAX_SYN_SIZE,0, GFP_KERNEL);
	if (buff == NULL) 
	{
		return(-ENOMEM);
	}
	sk->inuse = 1;
	buff->len = 24;
	buff->sk = sk;
	buff->free = 1;
	buff->localroute = sk->localroute;
	
	t1 = (struct tcphdr *) buff->data;

	/*
	 *	Put in the IP header and routing stuff. 
	 */
	 
	rt=ip_rt_route(sk->daddr, NULL, NULL);
	

	/*
	 *	We need to build the routing stuff from the things saved in skb. 
	 */

	tmp = sk->prot->build_header(buff, sk->saddr, sk->daddr, &dev,
					IPPROTO_TCP, NULL, MAX_SYN_SIZE,sk->ip_tos,sk->ip_ttl);
	if (tmp < 0) 
	{
		sk->prot->wfree(sk, buff->mem_addr, buff->mem_len);
		release_sock(sk);
		return(-ENETUNREACH);
	}

	buff->len += tmp;
	t1 = (struct tcphdr *)((char *)t1 +tmp);

	memcpy(t1,(void *)&(sk->dummy_th), sizeof(*t1));
	t1->seq = ntohl(sk->write_seq++);
	sk->sent_seq = sk->write_seq;
	buff->h.seq = sk->write_seq;
	t1->ack = 0;
	t1->window = 2;
	t1->res1=0;
	t1->res2=0;
	t1->rst = 0;
	t1->urg = 0;
	t1->psh = 0;
	t1->syn = 1;
	t1->urg_ptr = 0;
	t1->doff = 6;
	/* use 512 or whatever user asked for */
	
	if(rt!=NULL && (rt->rt_flags&RTF_WINDOW))
		sk->window_clamp=rt->rt_window;
	else
		sk->window_clamp=0;

	if (sk->user_mss)
		sk->mtu = sk->user_mss;
	else if(rt!=NULL && (rt->rt_flags&RTF_MTU))
		sk->mtu = rt->rt_mss;
	else 
	{
#ifdef CONFIG_INET_SNARL
		if ((sk->saddr ^ sk->daddr) & default_mask(sk->saddr))
#else
		if ((sk->saddr ^ sk->daddr) & dev->pa_mask)
#endif
			sk->mtu = 576 - HEADER_SIZE;
		else
			sk->mtu = MAX_WINDOW;
	}
	/*
	 *	but not bigger than device MTU 
	 */

	if(sk->mtu <32)
		sk->mtu = 32;	/* Sanity limit */
		
	sk->mtu = min(sk->mtu, dev->mtu - HEADER_SIZE);
	
	/*
	 *	Put in the TCP options to say MTU. 
	 */

	ptr = (unsigned char *)(t1+1);
	ptr[0] = 2;
	ptr[1] = 4;
	ptr[2] = (sk->mtu) >> 8;
	ptr[3] = (sk->mtu) & 0xff;
	tcp_send_check(t1, sk->saddr, sk->daddr,
		  sizeof(struct tcphdr) + 4, sk);

	/*
	 *	This must go first otherwise a really quick response will get reset. 
	 */

	tcp_set_state(sk,TCP_SYN_SENT);
	sk->rto = TCP_TIMEOUT_INIT;
	reset_timer(sk, TIME_WRITE, sk->rto);	/* Timer for repeating the SYN until an answer */
	sk->retransmits = TCP_RETR2 - TCP_SYN_RETRIES;

	sk->prot->queue_xmit(sk, dev, buff, 0);  
	tcp_statistics.TcpActiveOpens++;
	tcp_statistics.TcpOutSegs++;
  
	release_sock(sk);
	return(0);
}


/* This functions checks to see if the tcp header is actually acceptable. */
static int
tcp_sequence(struct sock *sk, struct tcphdr *th, short len,
	     struct options *opt, unsigned long saddr, struct device *dev)
{
	unsigned long next_seq;

	next_seq = len - 4*th->doff;
	if (th->fin)
		next_seq++;
	/* if we have a zero window, we can't have any data in the packet.. */
	if (next_seq && !sk->window)
		goto ignore_it;
	next_seq += th->seq;

	/*
	 * This isn't quite right.  sk->acked_seq could be more recent
	 * than sk->window.  This is however close enough.  We will accept
	 * slightly more packets than we should, but it should not cause
	 * problems unless someone is trying to forge packets.
	 */

	/* have we already seen all of this packet? */
	if (!after(next_seq+1, sk->acked_seq))
		goto ignore_it;
	/* or does it start beyond the window? */
	if (!before(th->seq, sk->acked_seq + sk->window + 1))
		goto ignore_it;

	/* ok, at least part of this packet would seem interesting.. */
	return 1;

ignore_it:
	if (th->rst)
		return 0;

	/*
	 *	Send a reset if we get something not ours and we are
	 *	unsynchronized. Note: We don't do anything to our end. We
	 *	are just killing the bogus remote connection then we will
	 *	connect again and it will work (with luck).
	 */
  	 
	if (sk->state==TCP_SYN_SENT || sk->state==TCP_SYN_RECV) {
		tcp_reset(sk->saddr,sk->daddr,th,sk->prot,NULL,dev, sk->ip_tos,sk->ip_ttl);
		return 1;
	}

	/* Try to resync things. */
	tcp_send_ack(sk->sent_seq, sk->acked_seq, sk, th, saddr);
	return 0;
}


#ifdef TCP_FASTPATH
/*
 *	Is the end of the queue clear of fragments as yet unmerged into the data stream
 *	Yes if
 *	a) The queue is empty
 *	b) The last frame on the queue has the acked flag set
 */

static inline int tcp_clean_end(struct sock *sk)
{
	struct sk_buff *skb=skb_peek(&sk->receive_queue);
	if(skb==NULL || sk->receive_queue.prev->acked)
		return 1;
}

#endif

int
tcp_rcv(struct sk_buff *skb, struct device *dev, struct options *opt,
	unsigned long daddr, unsigned short len,
	unsigned long saddr, int redo, struct inet_protocol * protocol)
{
	struct tcphdr *th;
	struct sock *sk;

	if (!skb) 
	{
		return(0);
	}

	if (!dev) 
	{
		return(0);
	}
  
	tcp_statistics.TcpInSegs++;
  
	if(skb->pkt_type!=PACKET_HOST)
	{
	  	kfree_skb(skb,FREE_READ);
	  	return(0);
	}
  
	th = skb->h.th;

	/*
	 *	Find the socket.
	 */

	sk = get_sock(&tcp_prot, th->dest, saddr, th->source, daddr);

	/*
	 *	If this socket has got a reset its to all intents and purposes 
  	 *	really dead 
  	 */
  	 
	if (sk!=NULL && sk->zapped)
		sk=NULL;

	if (!redo) 
	{
		if (tcp_check(th, len, saddr, daddr )) 
		{
			skb->sk = NULL;
			kfree_skb(skb,FREE_READ);
			/*
			 * We don't release the socket because it was
			 * never marked in use.
			 */
			return(0);
		}
		th->seq = ntohl(th->seq);

		/* See if we know about the socket. */
		if (sk == NULL) 
		{
			if (!th->rst)
				tcp_reset(daddr, saddr, th, &tcp_prot, opt,dev,skb->ip_hdr->tos,255);
			skb->sk = NULL;
			kfree_skb(skb, FREE_READ);
			return(0);
		}

		skb->len = len;
		skb->sk = sk;
		skb->acked = 0;
		skb->used = 0;
		skb->free = 0;
		skb->saddr = daddr;
		skb->daddr = saddr;
	
		/* We may need to add it to the backlog here. */
		cli();
		if (sk->inuse) 
		{
			skb_queue_tail(&sk->back_log, skb);
			sti();
			return(0);
		}
		sk->inuse = 1;
		sti();
	}
	else
	{
		if (!sk) 
		{
			return(0);
		}
	}


	if (!sk->prot) 
	{
		return(0);
	}


	/*
	 *	Charge the memory to the socket. 
	 */
	 
	if (sk->rmem_alloc + skb->mem_len >= sk->rcvbuf) 
	{
		skb->sk = NULL;
		kfree_skb(skb, FREE_READ);
		release_sock(sk);
		return(0);
	}

	sk->rmem_alloc += skb->mem_len;

#ifdef TCP_FASTPATH
/*
 *	Incoming data stream fastpath. 
 *
 *	We try to optimise two things.
 *	1) Spot general data arriving without funny options and skip extra checks and the switch.
 *	2) Spot the common case in raw data receive streams of a packet that has no funny options,
 *	fits exactly on the end of the current queue and may or may not have the ack bit set.
 *
 *	Case two especially is done inline in this routine so there are no long jumps causing heavy
 *	cache thrashing, no function call overhead (except for the ack sending if needed) and for
 *	speed although further optimizing here is possible.
 */
 
	/* I'm trusting gcc to optimise this sensibly... might need judicious application of a software mallet */
	if(!(sk->shutdown & RCV_SHUTDOWN) && sk->state==TCP_ESTABLISHED && !th->urg && !th->syn && !th->fin && !th->rst)
	{	
		/* Packets in order. Fits window */
		if(th->seq == sk->acked_seq+1 && sk->window && tcp_clean_end(sk))
		{
			/* Ack is harder */
			if(th->ack && !tcp_ack(sk, th, saddr, len))
			{
				kfree_skb(skb, FREE_READ);
				release_sock(sk);
				return 0;
			}
			/*
			 *	Set up variables
			 */
			skb->len -= (th->doff *4);
			sk->bytes_rcv += skb->len;
			tcp_rx_hit2++;
			if(skb->len)
			{
				skb_queue_tail(&sk->receive_queue,skb);	/* We already know where to put it */
				if(sk->window >= skb->len)
					sk->window-=skb->len;			/* We know its effect on the window */
				else
					sk->window=0;
				sk->acked_seq = th->seq+skb->len;	/* Easy */
				skb->acked=1;				/* Guaranteed true */
				if(!sk->delay_acks || sk->ack_backlog >= sk->max_ack_backlog || 
					sk->bytes_rcv > sk->max_unacked)
				{
					tcp_send_ack(sk->sent_seq, sk->acked_seq, sk, th , saddr);
				}
				else
				{
					sk->ack_backlog++;
					reset_timer(sk, TIME_WRITE, TCP_ACK_TIME);
				}
				if(!sk->dead)
					sk->data_ready(sk,0);
				release_sock(sk);
				return 0;
			}
		}
		/*
		 *	More generic case of arriving data stream in ESTABLISHED
		 */
		tcp_rx_hit1++;
		if(!tcp_sequence(sk, th, len, opt, saddr, dev))
		{
			kfree_skb(skb, FREE_READ);
			release_sock(sk);
			return 0;
		}
		if(th->ack && !tcp_ack(sk, th, saddr, len))
		{
			kfree_skb(skb, FREE_READ);
			release_sock(sk);
			return 0;
		}
		if(tcp_data(skb, sk, saddr, len))
			kfree_skb(skb, FREE_READ);
		release_sock(sk);
		return 0;
	}
	tcp_rx_miss++;
#endif	

	/*
	 *	Now deal with all cases.
	 */
	 
	switch(sk->state) 
	{
	
		/*
		 * This should close the system down if it's waiting
		 * for an ack that is never going to be sent.
		 */
		case TCP_LAST_ACK:
			if (th->rst) 
			{
				sk->zapped=1;
				sk->err = ECONNRESET;
 				tcp_set_state(sk,TCP_CLOSE);
				sk->shutdown = SHUTDOWN_MASK;
				if (!sk->dead) 
				{
					sk->state_change(sk);
				}
				kfree_skb(skb, FREE_READ);
				release_sock(sk);
				return(0);
			}

		case TCP_ESTABLISHED:
		case TCP_CLOSE_WAIT:
		case TCP_CLOSING:
		case TCP_FIN_WAIT1:
		case TCP_FIN_WAIT2:
		case TCP_TIME_WAIT:

			/*
			 * is it a good packet?
			 */

			if (!tcp_sequence(sk, th, len, opt, saddr,dev)) 
			{
				kfree_skb(skb, FREE_READ);
				release_sock(sk);
				return(0);
			}

			if (th->rst) 
			{
				tcp_statistics.TcpEstabResets++;
				sk->zapped=1;
				/* This means the thing should really be closed. */
				sk->err = ECONNRESET;
				if (sk->state == TCP_CLOSE_WAIT) 
				{
					sk->err = EPIPE;
				}
	
				/*
				 * A reset with a fin just means that
				 * the data was not all read.
				 */
				tcp_set_state(sk,TCP_CLOSE);
				sk->shutdown = SHUTDOWN_MASK;
				if (!sk->dead) 
				{
					sk->state_change(sk);
				}
				kfree_skb(skb, FREE_READ);
				release_sock(sk);
				return(0);
			}
			if (th->syn) 
			{
				long seq=sk->write_seq;
				int st=sk->state;
				tcp_statistics.TcpEstabResets++;
				sk->err = ECONNRESET;
				tcp_set_state(sk,TCP_CLOSE);
				sk->shutdown = SHUTDOWN_MASK;
				if(sk->debug)
					printk("Socket %p reset by SYN while established.\n", sk);
				if (!sk->dead) {
					sk->state_change(sk);
				}
				/*
				 *	The BSD port reuse protocol violation.
				 *	I do sometimes wonder how the *bsd people
				 *	have the nerve to talk about 'standards'.
				 *
				 *	If seq > last used on connection then
				 *	open a new connection and use 128000+seq of
				 *	old connection.
				 *
				 */
				 
				if(st==TCP_TIME_WAIT && th->seq > sk->acked_seq && sk->dead)
				{
					struct sock *psk=sk;
					/*
					 *	Find the listening socket.
					 */
					sk=get_sock(&tcp_prot, th->source, daddr, th->dest, saddr);
					if(sk && sk->state==TCP_LISTEN)
					{
						sk->inuse=1;
						tcp_conn_request(sk, skb, daddr, saddr,opt, dev,seq+128000);
						release_sock(psk);
						/* Fall through in case people are
						   also using the piggy backed SYN + data 
						   protocol violation */
					}
					else
					{
						tcp_reset(daddr, saddr,  th, psk->prot, opt,dev, psk->ip_tos,psk->ip_ttl);
						release_sock(psk);
						kfree_skb(skb, FREE_READ);
						return 0;
					}			
				}
				else
				{
					tcp_reset(daddr, saddr,  th, sk->prot, opt,dev, sk->ip_tos,sk->ip_ttl);
					kfree_skb(skb, FREE_READ);
					release_sock(sk);
					return(0);
				}
			}	
			if (th->ack && !tcp_ack(sk, th, saddr, len)) {
				kfree_skb(skb, FREE_READ);
				release_sock(sk);
				return(0);
			}
	
			if (tcp_urg(sk, th, saddr, len)) {
				kfree_skb(skb, FREE_READ);
				release_sock(sk);
				return(0);
			}

	
			if (tcp_data(skb, sk, saddr, len)) {
				kfree_skb(skb, FREE_READ);
				release_sock(sk);
				return(0);
			}	

			if (th->fin && tcp_fin(skb, sk, th, saddr, dev)) {
				kfree_skb(skb, FREE_READ);
				release_sock(sk);
				return(0);
			}
	
			release_sock(sk);
			return(0);


		case TCP_CLOSE:
			if (sk->dead || sk->daddr) {
				kfree_skb(skb, FREE_READ);
					release_sock(sk);
				return(0);
			}
	
			if (!th->rst) {
				if (!th->ack)
					th->ack_seq = 0;
				if(sk->debug) printk("Reset on closed socket %d.\n",sk->blog);
				tcp_reset(daddr, saddr, th, sk->prot, opt,dev,sk->ip_tos,sk->ip_ttl);
			}
			kfree_skb(skb, FREE_READ);
			release_sock(sk);
				return(0);
	
		case TCP_LISTEN:
			if (th->rst) {
				kfree_skb(skb, FREE_READ);
				release_sock(sk);
				return(0);
			}
			if (th->ack) {
				if(sk->debug) printk("Reset on listening socket %d.\n",sk->blog);
				tcp_reset(daddr, saddr, th, sk->prot, opt,dev,sk->ip_tos,sk->ip_ttl);
				kfree_skb(skb, FREE_READ);
				release_sock(sk);
				return(0);
			}
	
			if (th->syn) 
			{
				/*
				 * Now we just put the whole thing including
				 * the header and saddr, and protocol pointer
				 * into the buffer.  We can't respond until the
				 * user tells us to accept the connection.
				 */
				tcp_conn_request(sk, skb, daddr, saddr, opt, dev, tcp_init_seq());
				release_sock(sk);
				return(0);
			}

			kfree_skb(skb, FREE_READ);
			release_sock(sk);
			return(0);

		case TCP_SYN_RECV:
			if (th->syn) {
				/* Probably a retransmitted syn */
				kfree_skb(skb, FREE_READ);
				release_sock(sk);
				return(0);
			}
	
	
		default:
			if (!tcp_sequence(sk, th, len, opt, saddr,dev)) 
			{
				kfree_skb(skb, FREE_READ);
				release_sock(sk);
				return(0);
			}
	
		case TCP_SYN_SENT:
			if (th->rst) 
			{
				tcp_statistics.TcpAttemptFails++;
				sk->err = ECONNREFUSED;
				tcp_set_state(sk,TCP_CLOSE);
				sk->shutdown = SHUTDOWN_MASK;
				sk->zapped = 1;
				if (!sk->dead) 
				{
					sk->state_change(sk);
				}
				kfree_skb(skb, FREE_READ);
				release_sock(sk);
				return(0);
			}
			if (!th->ack) 
			{
				if (th->syn) 
				{
					/* Crossed SYN's are fine - but talking to
					   yourself is right out... */
					if(sk->saddr==saddr && sk->daddr==daddr &&
						sk->dummy_th.source==th->source &&
						sk->dummy_th.dest==th->dest)
					{
						tcp_statistics.TcpAttemptFails++;
						sk->err = ECONNREFUSED;
						tcp_set_state(sk,TCP_CLOSE);
						sk->shutdown = SHUTDOWN_MASK;
						sk->zapped = 1;
						if (!sk->dead) 
						{
							sk->state_change(sk);
						}
						kfree_skb(skb, FREE_READ);
						release_sock(sk);
						return(0);
					}
					tcp_set_state(sk,TCP_SYN_RECV);
				}
				kfree_skb(skb, FREE_READ);
				release_sock(sk);
				return(0);
			}
	
			switch(sk->state) 
			{
				case TCP_SYN_SENT:
					if (!tcp_ack(sk, th, saddr, len)) 
					{
						tcp_statistics.TcpAttemptFails++;
						tcp_reset(daddr, saddr, th,
							sk->prot, opt,dev,sk->ip_tos,sk->ip_ttl);
						kfree_skb(skb, FREE_READ);
							release_sock(sk);
						return(0);
					}
	
					/*
					 * If the syn bit is also set, switch to
					 * tcp_syn_recv, and then to established.
					 */
					if (!th->syn) 
					{
						kfree_skb(skb, FREE_READ);
						release_sock(sk);
						return(0);
					}
	
					/* Ack the syn and fall through. */
					sk->acked_seq = th->seq+1;
					sk->fin_seq = th->seq;
					tcp_send_ack(sk->sent_seq, th->seq+1,
						sk, th, sk->daddr);
		
				case TCP_SYN_RECV:
					if (!tcp_ack(sk, th, saddr, len)) 
					{
						tcp_statistics.TcpAttemptFails++;
						tcp_reset(daddr, saddr, th,
							sk->prot, opt, dev,sk->ip_tos,sk->ip_ttl);
						kfree_skb(skb, FREE_READ);
						release_sock(sk);
						return(0);
					}
	
					tcp_set_state(sk,TCP_ESTABLISHED);
	
					/*
					 * 	Now we need to finish filling out
					 * 	some of the tcp header.
					 * 
					 *	We need to check for mtu info. 
					 */
					tcp_options(sk, th);
					sk->dummy_th.dest = th->source;
					sk->copied_seq = sk->acked_seq-1;
					if (!sk->dead) 
					{
						sk->state_change(sk);
					}
	
					/*
					 * We've already processed his first
					 * ack.  In just about all cases that
					 * will have set max_window.  This is
					 * to protect us against the possibility
					 * that the initial window he sent was 0.
					 * This must occur after tcp_options, which
					 * sets sk->mtu.
					 */
					if (sk->max_window == 0) 
					{
						sk->max_window = 32;
						sk->mss = min(sk->max_window, sk->mtu);
					}

					/*
					 * Now process the rest like we were
					 * already in the established state.
					 */
					if (th->urg) 
					{
						if (tcp_urg(sk, th, saddr, len)) 
						{ 
							kfree_skb(skb, FREE_READ);
							release_sock(sk);
							return(0);
						}
					}
					if (tcp_data(skb, sk, saddr, len))
						kfree_skb(skb, FREE_READ);

					if (th->fin)
						tcp_fin(skb, sk, th, saddr, dev);
					release_sock(sk);
					return(0);
			}
	
			if (th->urg) 
			{
				if (tcp_urg(sk, th, saddr, len)) 
				{
					kfree_skb(skb, FREE_READ);
					release_sock(sk);
					return(0);
				}
			}
			if (tcp_data(skb, sk, saddr, len)) 
			{
				kfree_skb(skb, FREE_READ);
				release_sock(sk);
				return(0);
			}
	
			if (!th->fin) 
			{
				release_sock(sk);
				return(0);
			}
			tcp_fin(skb, sk, th, saddr, dev);
			release_sock(sk);
			return(0);
	}
}


/*
 * This routine sends a packet with an out of date sequence
 * number. It assumes the other end will try to ack it.
 */

static void tcp_write_wakeup(struct sock *sk)
{
	struct sk_buff *buff;
	struct tcphdr *t1;
	struct device *dev=NULL;
	int tmp;

	if (sk->zapped)
		return;	/* After a valid reset we can send no more */

	/*
	 * Write data can still be transmitted/retransmitted in the
	 * following states.  If any other state is encountered, return.
	 */

	if (sk->state != TCP_ESTABLISHED && 
	    sk->state != TCP_CLOSE_WAIT &&
	    sk->state != TCP_FIN_WAIT1 && 
	    sk->state != TCP_LAST_ACK &&
	    sk->state != TCP_CLOSING
	) {
		return;
	}

	buff = sk->prot->wmalloc(sk,MAX_ACK_SIZE,1, GFP_ATOMIC);
	if (buff == NULL) 
		return;

	buff->len = sizeof(struct tcphdr);
	buff->free = 1;
	buff->sk = sk;
	buff->localroute = sk->localroute;

	t1 = (struct tcphdr *) buff->data;

	/* Put in the IP header and routing stuff. */
	tmp = sk->prot->build_header(buff, sk->saddr, sk->daddr, &dev,
				IPPROTO_TCP, sk->opt, MAX_ACK_SIZE,sk->ip_tos,sk->ip_ttl);
	if (tmp < 0) 
	{
		sk->prot->wfree(sk, buff->mem_addr, buff->mem_len);
		return;
	}

	buff->len += tmp;
	t1 = (struct tcphdr *)((char *)t1 +tmp);

	memcpy(t1,(void *) &sk->dummy_th, sizeof(*t1));

	/*
	 * Use a previous sequence.
	 * This should cause the other end to send an ack.
	 */
	t1->seq = htonl(sk->sent_seq-1);
	t1->ack = 1; 
	t1->res1= 0;
	t1->res2= 0;
	t1->rst = 0;
	t1->urg = 0;
	t1->psh = 0;
	t1->fin = 0;
	t1->syn = 0;
	t1->ack_seq = ntohl(sk->acked_seq);
	t1->window = ntohs(tcp_select_window(sk));
	t1->doff = sizeof(*t1)/4;
	tcp_send_check(t1, sk->saddr, sk->daddr, sizeof(*t1), sk);

	 /*	Send it and free it.
   	  *	This will prevent the timer from automatically being restarted.
	  */
	sk->prot->queue_xmit(sk, dev, buff, 1);
	tcp_statistics.TcpOutSegs++;
}

void
tcp_send_probe0(struct sock *sk)
{
	if (sk->zapped)
		return;		/* After a valid reset we can send no more */

	tcp_write_wakeup(sk);

	sk->backoff++;
	sk->rto = min(sk->rto << 1, 120*HZ);
	reset_timer (sk, TIME_PROBE0, sk->rto);
	sk->retransmits++;
	sk->prot->retransmits ++;
}

/*
 *	Socket option code for TCP. 
 */
  
int tcp_setsockopt(struct sock *sk, int level, int optname, char *optval, int optlen)
{
	int val,err;

	if(level!=SOL_TCP)
		return ip_setsockopt(sk,level,optname,optval,optlen);

  	if (optval == NULL) 
  		return(-EINVAL);

  	err=verify_area(VERIFY_READ, optval, sizeof(int));
  	if(err)
  		return err;
  	
  	val = get_fs_long((unsigned long *)optval);

	switch(optname)
	{
		case TCP_MAXSEG:
/*
 * values greater than interface MTU won't take effect.  however at
 * the point when this call is done we typically don't yet know
 * which interface is going to be used
 */
	  		if(val<1||val>MAX_WINDOW)
				return -EINVAL;
			sk->user_mss=val;
			return 0;
		case TCP_NODELAY:
			sk->nonagle=(val==0)?0:1;
			return 0;
		default:
			return(-ENOPROTOOPT);
	}
}

int tcp_getsockopt(struct sock *sk, int level, int optname, char *optval, int *optlen)
{
	int val,err;

	if(level!=SOL_TCP)
		return ip_getsockopt(sk,level,optname,optval,optlen);
			
	switch(optname)
	{
		case TCP_MAXSEG:
			val=sk->user_mss;
			break;
		case TCP_NODELAY:
			val=sk->nonagle;
			break;
		default:
			return(-ENOPROTOOPT);
	}
	err=verify_area(VERIFY_WRITE, optlen, sizeof(int));
	if(err)
  		return err;
  	put_fs_long(sizeof(int),(unsigned long *) optlen);

  	err=verify_area(VERIFY_WRITE, optval, sizeof(int));
  	if(err)
  		return err;
  	put_fs_long(val,(unsigned long *)optval);

  	return(0);
}	


struct proto tcp_prot = {
	sock_wmalloc,
	sock_rmalloc,
	sock_wfree,
	sock_rfree,
	sock_rspace,
	sock_wspace,
	tcp_close,
	tcp_read,
	tcp_write,
	tcp_sendto,
	tcp_recvfrom,
	ip_build_header,
	tcp_connect,
	tcp_accept,
	ip_queue_xmit,
	tcp_retransmit,
	tcp_write_wakeup,
	tcp_read_wakeup,
	tcp_rcv,
	tcp_select,
	tcp_ioctl,
	NULL,
	tcp_shutdown,
	tcp_setsockopt,
	tcp_getsockopt,
	128,
	0,
	{NULL,},
	"TCP"
};
