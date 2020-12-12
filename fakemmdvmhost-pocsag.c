// SPDX-License-Identifier: Unlicense

#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>
#include <time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>

#define RXPORT		"3800"
#define TXPORT		"4800"
#define IPADDR		"127.0.0.1"
#define KEEPALIVE_DELAY	5	/* seconds */

static int acquire_addrinfo(char *hostname, char *servname, struct addrinfo **addr)
{
	int err;
	struct addrinfo hints;

	memset(&hints, 0, sizeof(hints));
	hints.ai_socktype = SOCK_DGRAM;
	hints.ai_flags = AI_NUMERICHOST | AI_NUMERICSERV;
	err = getaddrinfo(hostname, servname, &hints, addr);
	if (err)
		fprintf(stderr, "getaddrinfo %s\n", gai_strerror(err));

	return err;
}

static void release_addrinfo(struct addrinfo *addr)
{
	freeaddrinfo(addr);
}

static int create_socket(struct addrinfo *addr)
{
	int err, s;

	/* use one socket for tx and rx */
	err = socket(addr->ai_family, addr->ai_socktype, addr->ai_protocol);
	if (err < 0) {
		fprintf(stderr, "socket %s\n", strerror(errno));
		goto fin0;
	}
	s = err;

	err = bind(s, addr->ai_addr, addr->ai_addrlen);
	if (err < 0) {
		fprintf(stderr, "bind %s\n", strerror(errno));
		goto fin1;
	}

	goto fin0;
fin1:
	close(s);
fin0:
	return (err < 0) ? err : s;
}
		
static void destroy_socket(int s)
{
	close(s);
}

static int packet_receive(int s, struct addrinfo *addr, unsigned char *buf, int bufsize)
{
	int err, invalid_port, invalid_address;
	ssize_t ssz;
	socklen_t fromlen;
	struct sockaddr_storage fromaddr;
	struct sockaddr_in *a4, *fa4;
	struct sockaddr_in6 *a6, *fa6;

	/* emulate MMDVMHost's idle message */
	buf[0] = 0;
	err = sendto(s, buf, 1, 0, addr->ai_addr, addr->ai_addrlen);
	if (err < 0) {
		fprintf(stderr, "sendto %s\n", strerror(errno));
		goto fin0;
	}

	/* get message from DAPNETGateway */
	fromlen = sizeof(fromaddr);
	ssz = recvfrom(s, buf, bufsize, MSG_DONTWAIT,
		       (struct sockaddr *)&fromaddr, &fromlen);
	err = ssz;
	if (err < 0) {
		/* no message, wait and retry */
		if (errno == EAGAIN) {
			sleep(KEEPALIVE_DELAY);
			err = 0;
		}
		goto fin0;
	}

	// ugly
	switch (fromaddr.ss_family) {
	case AF_INET:
		a4 = (struct sockaddr_in *)addr->ai_addr;
		fa4 = (struct sockaddr_in *)&fromaddr;
		invalid_address = (a4->sin_addr.s_addr != fa4->sin_addr.s_addr);
		invalid_port = (a4->sin_port != fa4->sin_port);
		break;
	case AF_INET6:
		a6 = (struct sockaddr_in6 *)addr->ai_addr;
		fa6 = (struct sockaddr_in6 *)&fromaddr;
		invalid_address = memcmp(a6->sin6_addr.s6_addr,
					 fa6->sin6_addr.s6_addr,
					 sizeof(a6->sin6_addr.s6_addr));
		invalid_port = (a6->sin6_port != fa6->sin6_port);
		break;
	default:
		invalid_address = invalid_port = 1;
		break;
	}

	/* check sender, header */
	if (invalid_address || invalid_port || memcmp(buf, "POCSAG", 6))
		err = 0;

fin0:
	return err;
}

static void decode_rot1(unsigned char *buf, int len)
{
	int i;

	for (i = 0; i < len; i++)
		buf[i]--;
}

static void display(unsigned char *buf, int len)
{
	int i;

	for (i = 0; i < len; i++)
		putchar(isprint(buf[i]) ? buf[i] : '.');
}

static void packet_process(unsigned char *buf, int len)
{
	int ric, func, rot1, pos;
	time_t ts;
	struct tm *tm;
	char d[64];

	time(&ts);
	tm = localtime(&ts);
	strftime(d, sizeof(d), "%Y-%m-%d %H:%M:%S %Z", tm);

	/* see CPOCSAGNetwork::write() POCSAGNetwork.cpp of DAPNETGateway */
	ric = (buf[6] << 16) | (buf[7] << 8) | buf[8];
	func = buf[9];

	printf("%s %08d %02x ", d, ric, func);

	/* see SkyperProtocol.java of DAPNET-Core */
	switch (ric) {
	case 4512:
		// buf[10]: fixed value ('1'), buf[12]: fixed value (0x2a)
		printf("(%d) ", buf[11] - 0x1f);
		rot1 = 1;
		pos = 13;
		break;
	case 4520:
		printf("(%d-%d) ", buf[10] - 0x1f, buf[11] - 0x20);
		rot1 = 1;
		pos = 12;
		break;
	default:
		rot1 = 0;
		pos = 10;
		break;
	}

	if (rot1)
		decode_rot1(&buf[pos], len - pos);

	display(&buf[pos], len - pos);
	printf("\n");
}

int main(int argc, char *argv[])
{
	int err, s;
	struct addrinfo *txaddr, *rxaddr;
	unsigned char buf[2048];

	err = acquire_addrinfo((argc < 2) ? IPADDR : argv[1], TXPORT, &txaddr);
	if (err)
		goto fin0;

	err = acquire_addrinfo((argc < 2) ? IPADDR : argv[1], RXPORT, &rxaddr);
	if (err)
		goto fin1;

	err = create_socket(rxaddr);
	if (err < 0)
		goto fin2;
	s = err;

	while (1) {
		err = packet_receive(s, txaddr, buf, sizeof(buf));
		if (err > 0)
			packet_process(buf, err);
	}

	destroy_socket(s);
fin2:
	release_addrinfo(rxaddr);
fin1:
	release_addrinfo(txaddr);
fin0:
	return 0;
}
