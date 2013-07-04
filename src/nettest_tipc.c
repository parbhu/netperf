/**************************/
/*   tipc test cases      */
/*                        */
/*   send_tipc_stream()   */
/*   recv_tipc_stream()   */
/*   send_tipc_rr()       */
/*   recv_tipc_rr()       */
/*                        */
/**************************/

#include <stdio.h>  		// printf
#include <string.h>             // memset
#include <stdlib.h>		// exit 
#include <errno.h>

#include <linux/tipc.h>         // sockadd_tipc

#include "netlib.h" 		// netperf_request_struct
#include "netsh.h"  	 	// debug
#include "nettest_tipc.h"	// tipc_XXX_XXX_struct


void 
send_tipc_stream(char remote_host[]) 
{
  printf("netperf: TIPC stream test.\n");

  struct sockaddr_tipc remote_addr;  
  SOCKET send_socket;

  struct tipc_stream_response_struct 	*tipc_stream_response;

  struct tipc_portid remote_port_id;
  tipc_stream_response   =
    (struct tipc_stream_response_struct *)netperf_response.content.test_specific_data;

  if (!no_control) {
    netperf_request.content.request_type = DO_TIPC_STREAM;
    if (debug > 1) {
      fprintf(where,
	      "netperf: send_tipc_stream: requesting TCP stream test\n");
    }

    send_request();

    /* Receive response from netserver with remote tipc port_id */
    recv_response();

    if (!netperf_response.content.serv_errno) {
      if (debug)
        fprintf(where,"remote listen done.\n");
      remote_port_id = tipc_stream_response->id;    
      /* Receive test case configurations  */
    }
    else {
      Set_errno(netperf_response.content.serv_errno);
      fprintf(where,
	      "netperf: remote error %d",
	      netperf_response.content.serv_errno);
      perror("");
      fflush(where);

      exit(1);
    }

    /* Use received port id to connect to netserver */
    memset(&remote_addr, 0, sizeof(remote_addr));
    remote_addr.family = AF_TIPC;
    remote_addr.addrtype = TIPC_ADDR_ID;
    remote_addr.addr.id = remote_port_id;
    remote_addr.scope = TIPC_ZONE_SCOPE;

    send_socket = socket (AF_TIPC, SOCK_SEQPACKET, 0);
    
    if (connect(send_socket, (struct sockaddr *)&remote_addr, sizeof(remote_addr)) != 0) {
      perror("tipc: failed to connect to tipc netserver");
      exit(1);
    }

  printf("Netperf: tipc connection established!\n");
  }
}  


void
recv_tipc_stream()
{
  FILE *fp; //printing debug info
  struct sockaddr_tipc myaddr_in, peeraddr_in;
  SOCKET s_listen,s_data;
  socklen_t len = sizeof(struct sockaddr_tipc);  

  struct tipc_stream_response_struct    *tipc_stream_response;

  tipc_stream_response   =
    (struct tipc_stream_response_struct *)netperf_response.content.test_specific_data;

  /* Confirm that netperf_request is received */
  fp = fopen("netserver_output","a");
  fprintf(fp, "netserver: TIPC stream test.\n");

  /* Create tipc socket, bind */
  memset(&myaddr_in, 0, sizeof(myaddr_in));
  myaddr_in.family = AF_TIPC;
  myaddr_in.addrtype = TIPC_ADDR_NAME;
  myaddr_in.addr.name.name.type = NETSERVER_TIPC_DEFAULT;
  myaddr_in.addr.name.name.instance = 0;
  myaddr_in.scope = TIPC_ZONE_SCOPE;  

  s_listen = socket(AF_TIPC, SOCK_SEQPACKET, 0);

  if (bind(s_listen, 
	   (struct sockaddr *)&myaddr_in, 
	   sizeof(myaddr_in)) < 0) {
    perror("Netserver: failed to bind tipc port name\n");
    exit(1);
  }

  /* Get node name with getsockname */
  memset(&myaddr_in, 0, sizeof(myaddr_in));
  if (getsockname(s_listen, (struct sockaddr*)&myaddr_in, &len) != 0) {
    perror("tipc: getsockname failed.");
    exit(1);
  }
  
  int n = myaddr_in.addr.id.node;
  unsigned int ref = myaddr_in.addr.id.ref;

  fprintf(fp, "Node: %d.%d.%d ref:%u\n", tipc_zone(n), tipc_cluster(n), tipc_node(n), ref);

  /* Send response to netperf with port_id */ 
  tipc_stream_response->id = myaddr_in.addr.id;
  send_response();

  /* Listen for connections */
  if (listen(s_listen, 5) == SOCKET_ERROR) {
    netperf_response.content.serv_errno = errno;
    close(s_listen);
    send_response();

    exit(1);
  }   

  /* Accept connection */
  if ((s_data = accept(s_listen, (struct sockaddr *)&peeraddr_in, &len)) < 0) {
    perror("tipc: fail to accept connection."); 
    fprintf(fp, "netserver: fail to accept connection.");
    exit(1);
  }
}



void 
send_tipc_rr(char remote_host[])
{
  printf("netperf: TIPC rr test.\n");
}

void
recv_tipc_rr()
{
  printf("netserver: TIPC rr test.\n");
}



