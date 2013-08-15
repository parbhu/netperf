#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>

#include "netlib.h"
#include "netsh.h"
#include "nettest_tipc.h"
#ifdef WANT_TIPC
#include <linux/tipc.h>

extern int lss_size_req;    /* requested local socket send buffer size */
extern int lsr_size_req;    /* requested local socket recv buffer size */
extern int lss_size;        /* local socket send buffer size           */
extern int lsr_size;				/* local socket recv buffer size					 */

void print_top_tipc_test_header(char test_name[], uint32_t node, uint32_t ref)
{
	printf("%s to <%d.%d.%d:%u>\n", test_name, tipc_zone(node),
	        tipc_cluster(node), tipc_node(node), ref);
}

/* create_tipc_socket - set up send/receive side tipc test sockets */
SOCKET create_tipc_socket()
{
	SOCKET sock;
	netperf_socklen_t sock_opt_len;

	sock = socket(AF_TIPC, SOCK_STREAM, 0);
	if (sock == INVALID_SOCKET) {
		fprintf(where, "netperf: create_tipc_socket: socket: errno %d errmsg %s\n",
			errno, strerror(errno));
		fflush(where);
		exit(1);
	}
	/* Setting the SO_SNDBUF doesn't currently have any effect on TIPC, but
	 * we do it anyway since the netperf configuration knobs are there
	 * already*/
	set_sock_buffer(sock, SEND_BUFFER, lss_size_req, &lss_size);
	set_sock_buffer(sock, RECV_BUFFER, lsr_size_req, &lsr_size);

#if defined(SO_PRIORITY)
	/* Same goes for SO_PRIORITY, TIPC does not honor this setting on the
	 * socket, but there's an ongoing effort to add this */
	if (local_socket_prio >= 0) {
		if (setsockopt(sock, SOL_SOCKET, SO_PRIORITY, &local_socket_prio,
									 sizeof(int)) == SOCKET_ERROR) {
			fprintf(where, "netperf: create_tipc_socket: so_priority: errno %d errmsg=%s\n",
							errno, strerror(errno));
			fflush(where);
			local_socket_prio = -2;
		}
		else {
			sock_opt_len = 4;
			getsockopt(sock, SOL_SOCKET, SO_PRIORITY, &local_socket_prio,
								 &sock_opt_len);
		}
	}
#else
	local_socket_prio = -3;
#endif
	return sock;
}

void sockaddr_from_id(uint32_t node, uint32_t ref, struct sockaddr_storage *ss)
{
	struct sockaddr_tipc *sa_tipc = (struct sockaddr_tipc*)ss;

	memset(ss, 0, sizeof(struct sockaddr_storage));
	sa_tipc->family = AF_TIPC;
	sa_tipc->addrtype = TIPC_ADDR_ID;
	sa_tipc->addr.id.node = node;
	sa_tipc->addr.id.ref = ref;
	sa_tipc->scope = TIPC_ZONE_SCOPE;
}

void sockaddr_from_type_inst(uint32_t type, uint32_t instance,
														 struct sockaddr_storage *ss)
{
	struct sockaddr_tipc *sa_tipc = (struct sockaddr_tipc*)ss;

	memset(ss, 0, sizeof(struct sockaddr_storage));
	sa_tipc->family = AF_TIPC;
	sa_tipc->addrtype = TIPC_ADDR_NAME;
	sa_tipc->addr.name.name.type = type;
	sa_tipc->addr.name.name.instance = instance;
	sa_tipc->scope = TIPC_ZONE_SCOPE;
}

/* get_portid - fetch the port ID of a bound TIPC socket */
void get_portid(SOCKET sd, uint32_t *node, uint32_t *ref)
{
	struct sockaddr_tipc sa_tipc;
	netperf_socklen_t addrlen = sizeof(struct sockaddr_tipc);

	if (getsockname(sd, (struct sockaddr*)&sa_tipc, &addrlen)) {
		fprintf(where, "netperf: get_portid: getsockname: %s\n", strerror(errno));
		fflush(where);
		exit(1);
	}
	*ref = sa_tipc.addr.id.ref;
	*node = sa_tipc.addr.id.node;
}


/* get_tipc_addrinfo - create an addrinfo based on a given TIPC sockaddr */
void get_tipc_addrinfo(struct addrinfo **addr, struct sockaddr_storage *ss)
{
	struct sockaddr_tipc *sa_tipc = (struct sockaddr_tipc*)ss;

	*addr = malloc(sizeof(struct addrinfo));
	memset(*addr, 0, sizeof(struct addrinfo));
	(*addr)->ai_family = AF_TIPC;
	(*addr)->ai_socktype = SOCK_STREAM;
	(*addr)->ai_addrlen = sizeof(struct sockaddr_tipc);
	(*addr)->ai_addr = (struct sockaddr*)sa_tipc;
}


#endif /*WANT_TIPC*/



