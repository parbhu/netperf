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
#include "netlib.h" 		// netperf_request_struct
#include "netsh.h"  	 	// debug
#include "nettest_tipc.h"	// tipc_XXX_XXX_struct

void 
send_tipc_stream(char remote_host[]) 
{
  printf("netperf: TIPC stream test.\n");

  struct tipc_stream_request_struct 	*tipc_stream_request;
  struct tipc_stream_response_struct 	*tipc_stream_response;
  struct tipc_stream_results_struct 	*tipc_stream_results;

  netperf_request.content.request_type = DO_TIPC_STREAM;
  if (debug > 1) {
        fprintf(where,
                "netperf: send_tcp_stream: requesting TCP stream test\n");
  }

  send_request();
}



void
recv_tipc_stream()
{
  FILE *fp;
  fp = fopen("netserver_output","a");
  fprintf(fp, "netserver: TIPC stream test.\n");
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



