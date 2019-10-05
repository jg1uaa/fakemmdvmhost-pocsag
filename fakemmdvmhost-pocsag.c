// SPDX-License-Identifier: Unlicense

#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>
#include <time.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>

#define RXPORT		3800
#define TXPORT		4800
#define IPADDR		"127.0.0.1"
#define KEEPALIVE_DELAY	5	/* seconds */

static int trxsocket;

static int create_socket(struct sockaddr_in *addr)
{
	int err;

	/* use one socket for tx and rx */
	err = socket(AF_INET, SOCK_DGRAM, 0);
	if (err < 0) {
		fprintf(stderr, "socket (rx) %s\n", strerror(errno));
		goto fin0;
	}
	trxsocket = err;

	err = bind(trxsocket, (struct sockaddr *)addr, sizeof(*addr));
	if (err < 0) {
		fprintf(stderr, "bind (rx) %s\n", strerror(errno));
		goto fin1;
	}

	goto fin0;
fin1:
	close(trxsocket);
fin0:
	return err;
}
		
static void destroy_socket(void)
{
	close(trxsocket);
}

static int packet_receive(struct sockaddr_in *addr, unsigned char *buf, int bufsize)
{
	int err;
	ssize_t ssz;
	socklen_t fromlen;
	struct sockaddr_in fromaddr;

	/* emulate MMDVMHost's idle message */
	buf[0] = 0;
	err = sendto(trxsocket, buf, 1, 0,
		     (struct sockaddr *)addr, sizeof(*addr));
	if (err < 0) {
		fprintf(stderr, "sendto (tx) %s\n", strerror(errno));
		goto fin0;
	}

	/* get message from DAPNETGateway */
	fromlen = sizeof(fromaddr);
	ssz = recvfrom(trxsocket, buf, bufsize, MSG_DONTWAIT,
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

	/* check sender, header */
	if (addr->sin_addr.s_addr != fromaddr.sin_addr.s_addr ||
	    addr->sin_port != fromaddr.sin_port || memcmp(buf, "POCSAG", 6))
		err = 0;

fin0:
	return err;
}

static void packet_process(unsigned char *buf, int len)
{
	int i, ric, func;
	time_t ts;
	struct tm *tm;
	char d[64];

	time(&ts);
	tm = localtime(&ts);
	strftime(d, sizeof(d), "%Y-%m-%d %H:%M:%S %Z", tm);

	/* see CPOCSAGNetwork::write() POCSAGNetwork.cpp */
	ric = (buf[6] << 16) | (buf[7] << 8) | buf[8];
	func = buf[9];

	printf("%s %08d %02x ", d, ric, func);

	for (i = 10; i < len; i++) {
		putchar(isprint(buf[i]) ? buf[i] : '.');
	}

	printf("\n");
}

int main(int argc, char *argv[])
{
	int err;
	struct sockaddr_in addr;
	unsigned char buf[2048];
	
	memset(&addr, 0, sizeof(addr));
	inet_aton(IPADDR, &addr.sin_addr);
	addr.sin_family = AF_INET;

	addr.sin_port = htons(RXPORT);
	err = create_socket(&addr);
	if (err < 0)
		goto fin0;

	addr.sin_port = htons(TXPORT);
	while (1) {
		err = packet_receive(&addr, buf, sizeof(buf));
		if (err > 0)
			packet_process(buf, err);
	}

	destroy_socket();
fin0:
	return 0;
}
