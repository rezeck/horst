/* horst - olsr scanning tool
 *
 * Copyright (C) 2005-2010 Bruno Randolf (br1@einfach.org)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <errno.h>
#include <err.h>

#include "main.h"
#include "util.h"
#include "network.h"

extern struct config conf;

int srv_fd = -1;
int cli_fd = -1;
static int netmon_fd;

struct net_packet_info {
	/* general */
	int			pkt_types;	/* bitmask of packet types in this pkt */
	int			len;		/* packet length */

	/* wlan phy (from radiotap) */
	int			signal;		/* signal strength (usually dBm) */
	int			noise;		/* noise level (usually dBm) */
	int			snr;		/* signal to noise ratio */
	int			rate;		/* physical rate */
	int			phy_freq;	/* frequency (unused) */
	int			phy_flags;	/* A, B, G, shortpre */

	/* wlan mac */
	int			wlan_type;	/* frame control field */
	unsigned char		wlan_src[MAC_LEN];
	unsigned char		wlan_dst[MAC_LEN];
	unsigned char		wlan_bssid[MAC_LEN];
	char			wlan_essid[MAX_ESSID_LEN];
	u_int64_t		wlan_tsf;	/* timestamp from beacon */
	int			wlan_mode;	/* AP, STA or IBSS */
	unsigned char		wlan_channel;	/* channel from beacon, probe */
	int			wlan_wep;	/* WEP on/off */

	/* IP */
	unsigned int		ip_src;
	unsigned int		ip_dst;
	int			olsr_type;
	int			olsr_neigh;
	int			olsr_tc;
} __attribute__ ((packed));


void
net_init_server_socket(char* rport)
{
	struct sockaddr_in sock_in;
	int reuse = 1;

	printf("using server port %s\n", rport);

	sock_in.sin_family = AF_INET;
	sock_in.sin_addr.s_addr = htonl(INADDR_ANY);
	sock_in.sin_port = htons(atoi(rport));

	if ((srv_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
		err(1, "socket");
	}

	if (setsockopt(srv_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
		err(1, "setsockopt SO_REUSEADDR");
	}

	if (bind(srv_fd, (struct sockaddr*)&sock_in, sizeof(sock_in)) < 0) {
		err(1, "bind");
	}

	if (listen(srv_fd, 0) < 0) {
		err(1, "listen");
	}
}


int
net_send_packet(struct packet_info *pkt)
{
	int ret;
	struct net_packet_info np;

	np.pkt_types	= htole32(pkt->pkt_types);
	np.len		= htole32(pkt->len);
	np.signal	= htole32(pkt->signal);
	np.noise	= htole32(pkt->noise);
	np.snr		= htole32(pkt->snr);
	np.rate		= htole32(pkt->rate);
	np.phy_freq	= htole32(pkt->phy_freq);
	np.phy_flags	= htole32(pkt->phy_flags);
	np.wlan_type	= htole32(pkt->wlan_type);
	np.wlan_tsf	= htole64(pkt->wlan_tsf);
	np.wlan_mode	= htole32(pkt->wlan_mode);
	np.wlan_channel = pkt->wlan_channel;
	np.wlan_wep	= htole32(pkt->wlan_wep);
	np.ip_src	= htole32(pkt->ip_src);
	np.ip_dst	= htole32(pkt->ip_dst);
	np.olsr_type	= htole32(pkt->olsr_type);
	np.olsr_neigh	= htole32(pkt->olsr_neigh);
	np.olsr_tc	= htole32(pkt->olsr_tc);
	memcpy(np.wlan_src, pkt->wlan_src, MAC_LEN);
	memcpy(np.wlan_dst, pkt->wlan_dst, MAC_LEN);
	memcpy(np.wlan_bssid, pkt->wlan_bssid, MAC_LEN);
	memcpy(np.wlan_essid, pkt->wlan_essid, MAX_ESSID_LEN);

	ret = write(cli_fd, &np, sizeof(np));
	if (ret == -1) {
		if (errno == EPIPE) {
			printf("client has closed\n");
			close(cli_fd);
			cli_fd = -1;
		}
		else {
			perror("write");
		}
	}
	return 0;
}


/*
 * return 0 - error
 *	  1 - ok
 */
int
net_receive_packet(unsigned char *buffer, int len, struct packet_info *pkt)
{
	struct net_packet_info *np;

	if (len < sizeof(struct net_packet_info)) {
		return 0;
	}

	np = (struct net_packet_info *)buffer;

	if (np->rate == 0) {
		return 0;
	}

	pkt->pkt_types	= le32toh(np->pkt_types);
	pkt->len	= le32toh(np->len);
	pkt->signal	= le32toh(np->signal);
	pkt->noise	= le32toh(np->noise);
	pkt->snr	= le32toh(np->snr);
	pkt->rate	= le32toh(np->rate);
	pkt->phy_freq	= le32toh(np->phy_freq);
	pkt->phy_flags	= le32toh(np->phy_flags);
	pkt->wlan_type	= le32toh(np->wlan_type);
	pkt->wlan_tsf	= le64toh(np->wlan_tsf);
	pkt->wlan_mode	= le32toh(np->wlan_mode);
	pkt->wlan_channel = np->wlan_channel;
	pkt->wlan_wep	= le32toh(np->wlan_wep);
	pkt->ip_src	= le32toh(np->ip_src);
	pkt->ip_dst	= le32toh(np->ip_dst);
	pkt->olsr_type	= le32toh(np->olsr_type);
	pkt->olsr_neigh	= le32toh(np->olsr_neigh);
	pkt->olsr_tc	= le32toh(np->olsr_tc);
	memcpy(pkt->wlan_src, np->wlan_src, MAC_LEN);
	memcpy(pkt->wlan_dst, np->wlan_dst, MAC_LEN);
	memcpy(pkt->wlan_bssid, np->wlan_bssid, MAC_LEN);
	memcpy(pkt->wlan_essid, np->wlan_essid, MAX_ESSID_LEN);

	return 1;
}


int net_handle_server_conn( void )
{
	struct sockaddr_in cin;
	socklen_t cinlen;

	if (cli_fd != -1) {
		printf("can only handle one client\n");
		return -1;
	}

	cli_fd = accept(srv_fd, (struct sockaddr*)&cin, &cinlen);

	printf("horst: accepting client\n");

	//read(cli_fd,line,sizeof(line));
	return 0;
}


int
net_open_client_socket(char* serveraddr, char* rport)
{
	struct addrinfo saddr;
	struct addrinfo *result, *rp;
	int ret;

	printf("connecting to server %s port %s\n", serveraddr, rport);

	/* Obtain address(es) matching host/port */
	memset(&saddr, 0, sizeof(struct addrinfo));
	saddr.ai_family = AF_INET;
	saddr.ai_socktype = SOCK_STREAM;
	saddr.ai_flags = 0;
	saddr.ai_protocol = 0;

	ret = getaddrinfo(serveraddr, rport, &saddr, &result);
	if (ret != 0) {
		fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(ret));
		exit(EXIT_FAILURE);
	}

	/* getaddrinfo() returns a list of address structures.
	   Try each address until we successfully connect. */
	for (rp = result; rp != NULL; rp = rp->ai_next) {
		netmon_fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
		if (netmon_fd == -1) {
			continue;
		}

		if (connect(netmon_fd, rp->ai_addr, rp->ai_addrlen) != -1) {
			break; /* Success */
		}

		close(netmon_fd);
	}

	if (rp == NULL) {
		/* No address succeeded */
		freeaddrinfo(result);
		err(1, "could not connect");
	}

	freeaddrinfo(result);

	printf("connected\n");
	return netmon_fd;
}


void
net_finish(void) {
	if (srv_fd != -1) {
		close(srv_fd);
	}
	if (cli_fd != -1) {
		close(cli_fd);
	}
	if (netmon_fd) {
		close(netmon_fd);
	}
}
