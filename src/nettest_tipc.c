/******************************/
/*    nettest_tipc.c          */
/*                            */
/*    print_top_tipc_header   */
/*    create_tipc_socket      */
/*    sockaddr_from_id        */
/*    sockaddr_from_type_inst */
/*    get_portid              */
/*    get_tipc_addrinfo       */
/*                            */
/******************************/

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
extern int lsr_size;

void
print_top_tipc_test_header(char test_name[], struct omni_tipc_portid remote_port)
{
  int n = remote_port.node;
  unsigned int ref = remote_port.ref;

  printf("%s to <%d.%d.%d:%u>\n", test_name, tipc_zone(n), tipc_cluster(n), tipc_node(n), ref);

}



/* Function creating the tipc socket and set some options
for it. Used in both send and receive side for tipc stream
and tipc request/response test cases. */
SOCKET
create_tipc_socket()
{

  SOCKET sock;
  netperf_socklen_t sock_opt_len;

  /*set up the data socket                        */
  sock = socket(AF_TIPC, SOCK_STREAM, 0);

  if (sock == INVALID_SOCKET){
    fprintf(where,
            "netperf: create_tipc_socket: socket: errno %d errmsg %s\n",
	    errno,
            strerror(errno));
    fflush(where);
    exit(1);
  }

  if (debug) {
    fprintf(where,"create_tipc_socket: socket %d obtained...\n",sock);
    fflush(where);
  }

  /* Modify the local socket size. The reason we alter the send buffer
   size here rather than when the connection is made is to take care
   of decreases in buffer size. Decreasing the window size after
   connection establishment is a TCP no-no. Also, by setting the
   buffer (window) size before the connection is established, we can
   control the TCP MSS (segment size). The MSS is never (well, should
   never be) more that 1/2 the minimum receive buffer size at each
   half of the connection.  This is why we are altering the receive
   buffer size on the sending size of a unidirectional transfer. If
   the user has not requested that the socket buffers be altered, we
   will try to find-out what their values are. If we cannot touch the
   socket buffer in any way, we will set the values to -1 to indicate
   that.  */

  /* all the oogy nitty gritty stuff moved from here into the routine
     being called below, per patches from davidm to workaround the bug
     in Linux getsockopt().  raj 2004-06-15 */
  set_sock_buffer (sock, SEND_BUFFER, lss_size_req, &lss_size);
  set_sock_buffer (sock, RECV_BUFFER, lsr_size_req, &lsr_size);

  /* In the code for tcp stream test case there is code for
     setting SO_RCV_COPYAVPID, SO_SND_COPYAVOID, TCP_NODELAY,
     SCTP_NODELAY, TCP_CORK, SO_KEEPALIVE, SO_REUSEADDR and
     TCP_CORK on the created socket. This cannot be done
     for tipc. */

#if defined(SO_PRIORITY)
  if (local_socket_prio >= 0) {
    if (setsockopt(sock,
                  SOL_SOCKET,
                  SO_PRIORITY,
                  &local_socket_prio,
                  sizeof(int)) == SOCKET_ERROR) {
      fprintf(where,
             "netperf: create_tipc_socket: so_priority: errno %d\n",
             errno);
      fflush(where);
      local_socket_prio = -2;
    }
    else {
      sock_opt_len = 4;
      getsockopt(sock,
                 SOL_SOCKET,
                 SO_PRIORITY,
                 &local_socket_prio,
                 &sock_opt_len);
    }
  }
#else
  local_socket_prio = -3;
#endif

  return sock;

}

/* Routine that fills in the addressing information of
   a sockaddr_storage given the omni_tipc_portid. */
void sockaddr_from_id(struct omni_tipc_portid portid, struct sockaddr_storage *sa)
{
  // cast sa to a sockaddr_tipc
  struct sockaddr_tipc *sa_tipc = (struct sockaddr_tipc*)sa;  

  memset(sa_tipc, 0, sizeof(struct sockaddr_tipc));

  sa_tipc->family = AF_TIPC;
  sa_tipc->addrtype = TIPC_ADDR_ID;
  sa_tipc->addr.id.node = portid.node;
  sa_tipc->addr.id.ref = portid.ref;
  sa_tipc->scope = TIPC_ZONE_SCOPE;
}


/* Routine that fills in the addressing information of
   a sockaddr_storage given the type and the instance. */
void sockaddr_from_type_inst(unsigned int type, unsigned int instance, struct sockaddr_storage *sa)
{
  // cast sa to a sockaddr_tipc
  struct sockaddr_tipc *sa_tipc = (struct sockaddr_tipc*)sa;

  memset(sa_tipc, 0, sizeof(struct sockaddr_tipc));

  sa_tipc->family = AF_TIPC;
  sa_tipc->addrtype = TIPC_ADDR_NAME;
  sa_tipc->addr.name.name.type = type;
  sa_tipc->addr.name.name.instance = instance;
  sa_tipc->scope = TIPC_ZONE_SCOPE;
}


void get_portid(SOCKET sd, struct omni_tipc_portid *portid)
{
  // cast sa to a sockaddr_tipc
  struct sockaddr_tipc sa_tipc;

  netperf_socklen_t addrlen;

  addrlen = sizeof(struct sockaddr_tipc);

  if (getsockname(sd,
    (struct sockaddr*)&sa_tipc,
    &addrlen) != 0) {
      perror("get_portid: getsockname failed.");
      exit(1);
  }

  portid->ref = sa_tipc.addr.id.ref;
  portid->node = sa_tipc.addr.id.node;
}


/* This routine fills in the addrinfo structs for the tipc test cases */
void
get_tipc_addrinfo(struct addrinfo **addr, struct sockaddr_storage *sa) {

  // cast sa to a sockaddr_tipc
  struct sockaddr_tipc *sa_tipc = (struct sockaddr_tipc*)sa;

  *addr = (struct addrinfo*)malloc(sizeof(struct addrinfo));
  memset(*addr, 0, sizeof(struct addrinfo));
  (*addr)->ai_family = AF_TIPC;
  (*addr)->ai_socktype = SOCK_STREAM;
  (*addr)->ai_addrlen = sizeof(struct sockaddr_tipc);
  (*addr)->ai_addr = (struct sockaddr *)sa_tipc;

}

#else
void print_top_tipc_test_header(char test_name[], struct omni_tipc_portid remote_port){}
SOCKET create_tipc_socket(){return 0;}
void sockaddr_from_id(struct omni_tipc_portid portid, struct sockaddr_storage *sa){}
void sockaddr_from_type_inst(unsigned int type, unsigned int instance, struct sockaddr_storage *sa){}
void get_portid(SOCKET s, struct omni_tipc_portid *portid){}
void get_tipc_addrinfo(struct addrinfo **addr, struct sockaddr_storage *sa){}


#endif
