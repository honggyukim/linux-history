/*
 * INET		An implementation of the TCP/IP protocol suite for the LINUX
 *		operating system.  INET is implemented using the  BSD Socket
 *		interface as the means of communication with the user level.
 *
 *		Definitions for the IP module.
 *
 * Version:	@(#)ip.h	1.0.2	05/07/93
 *
 * Authors:	Ross Biro, <bir7@leland.Stanford.Edu>
 *		Fred N. van Kempen, <waltje@uWalt.NL.Mugnet.ORG>
 *		Alan Cox, <gw4pts@gw4pts.ampr.org>
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 */
#ifndef _IP_H
#define _IP_H


#include <linux/ip.h>
#include <linux/config.h>

#ifndef _SNMP_H
#include "snmp.h"
#endif

#include "sock.h"	/* struct sock */

/* IP flags. */
#define IP_CE		0x8000		/* Flag: "Congestion"		*/
#define IP_DF		0x4000		/* Flag: "Don't Fragment"	*/
#define IP_MF		0x2000		/* Flag: "More Fragments"	*/
#define IP_OFFSET	0x1FFF		/* "Fragment Offset" part	*/

#define IP_FRAG_TIME	(30 * HZ)		/* fragment lifetime	*/

#ifdef CONFIG_IP_MULTICAST
extern void		ip_mc_dropsocket(struct sock *);
extern void		ip_mc_dropdevice(struct device *dev);
extern int		ip_mc_procinfo(char *, char **, off_t, int);
#define MULTICAST(x)	(IN_MULTICAST(htonl(x)))
#endif
 

/* Describe an IP fragment. */
struct ipfrag {
  int		offset;		/* offset of fragment in IP datagram	*/
  int		end;		/* last byte of data in datagram	*/
  int		len;		/* length of this fragment		*/
  struct sk_buff *skb;			/* complete received fragment		*/
  unsigned char		*ptr;		/* pointer into real fragment data	*/
  struct ipfrag		*next;		/* linked list pointers			*/
  struct ipfrag		*prev;
};

/* Describe an entry in the "incomplete datagrams" queue. */
struct ipq	 {
  unsigned char		*mac;		/* pointer to MAC header		*/
  struct iphdr	*iph;		/* pointer to IP header			*/
  int		len;		/* total length of original datagram	*/
  short			ihlen;		/* length of the IP header		*/
  short 	maclen;		/* length of the MAC header		*/
  struct timer_list timer;	/* when will this queue expire?		*/
  struct ipfrag		*fragments;	/* linked list of received fragments	*/
  struct ipq	*next;		/* linked list pointers			*/
  struct ipq	*prev;
  struct device *dev;		/* Device - for icmp replies */
};


extern int		backoff(int n);

extern void		ip_print(const struct iphdr *ip);
extern int		ip_ioctl(struct sock *sk, int cmd,
				 unsigned long arg);
extern void		ip_route_check(unsigned long daddr);
extern int		ip_build_header(struct sk_buff *skb,
					unsigned long saddr,
					unsigned long daddr,
					struct device **dev, int type,
					struct options *opt, int len,
					int tos,int ttl);
extern unsigned short	ip_compute_csum(unsigned char * buff, int len);
extern int		ip_rcv(struct sk_buff *skb, struct device *dev,
			       struct packet_type *pt);
extern void		ip_send_check(struct iphdr *ip);
extern int		ip_id_count;			  
extern void		ip_queue_xmit(struct sock *sk,
				      struct device *dev, struct sk_buff *skb,
				      int free);
extern int 		ip_setsockopt(struct sock *sk, int level, int optname, char *optval, int optlen);
extern int 		ip_getsockopt(struct sock *sk, int level, int optname, char *optval, int *optlen);
extern void		ip_init(void);

extern struct ip_mib	ip_statistics;
#endif	/* _IP_H */
