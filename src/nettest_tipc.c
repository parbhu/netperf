/**************************/
/*   nettest_tipc.c       */
/*                        */
/*   scan_tipc_args()     */
/*                        */
/*   send_tipc_stream()   */
/*   recv_tipc_stream()   */
/*   send_tipc_rr()       */
/*   recv_tipc_rr()       */
/*                        */
/**************************/

#include <stdio.h> 
#include <string.h> 
#include <stdlib.h>
#include <errno.h>
#include <linux/tipc.h> 
#include <unistd.h>

#include "netlib.h"
#include "netsh.h" 
#include "nettest_tipc.h"

/* Following extern was defined in nettest_bsd.c */
extern int first_burst_size; 

extern int rss_size_req;    /* requested remote socket send buffer size */
extern int rsr_size_req;    /* requested remote socket recv buffer size */
extern int rss_size;             /* initial remote socket send buffer size */
extern int rsr_size;             /* initial remote socket recv buffer size */
extern int lss_size_req;    /* requested local socket send buffer size */
extern int lsr_size_req;    /* requested local socket recv buffer size */
extern int lss_size;             /* local  socket send buffer size       */
extern int lsr_size;             /* local  socket recv buffer size       */
extern int rsp_size;         /* response size                        */

extern int send_size;            /* how big are individual sends         */
extern int recv_size;            /* how big are individual receives      */

uint32_t direction;     	/* which way flows the data? */
static int req_size = 100;      /* request size                         */
char test_uuid[38];
int	legacy;
char	*output_selection_spec;
int	implicit_direction;

static  int confidence_iteration;
static  char  local_cpu_method;
static  char  remote_cpu_method;

char tipc_usage[] = "\n\
Usage: netperf [global options] -- [test options] \n\
\n\
TIPC Sockets Test Options:\n\
    -b number         Send number requests at start of TIPC_RR tests\n\
    -h                Display this text\n\
    -m bytes          Set the send size (TIPC_STREAM)\n\
    -M bytes          Set the recv size (TIPC_STREAM)\n\
    -r req,[rsp]      Set request/response sizes (TIPC_RR)\n\
    -s send[,recv]    Set local socket send/recv buffer sizes\n\
    -S send[,recv]    Set remote socket send/recv buffer sizes\n\
    -o [file]         Generate CSV output optionally based on file\n\
                      Use filename of '?' to get the list of choices\n\
    -O [file]         Generate classic-style output based on file\n\
                      Use filename of '?' to get the list of choices\n\
    -u uuid           Use the supplied string as the UUID for this test.\n\
\n\
For those options taking two parms, at least one must be specified;\n\
specifying one value without a comma will set both parms to that\n\
value, specifying a value with a leading comma will set just the second\n\
parm, a value with a trailing comma will set just the first. To set\n\
each parm to unique values, specify both and separate them with a\n\
comma.\n";



void
print_top_tipc_test_header(char test_name[], struct tipc_portid remote_port) 
{
  int n = remote_port.node;
  unsigned int ref = remote_port.ref;

  printf("%s to <%d.%d.%d:%u>\n", test_name, tipc_zone(n), tipc_cluster(n), tipc_node(n), ref);

}



/* Function creating the tipc socket and set some options
for it. Used in both send and receive side for tipc stream
test case */
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


/* This routine implements the TIPC unidirectional data transfer test */
/* (a.k.a. stream). It receives its */
/* parameters via global variables from the shell and writes its */
/* output to the standard output. */

void 
send_tipc_stream(char remote_host[]) 
{

  char *tput_title = "\
Recv   Send    Send                          \n\
Socket Socket  Message  Elapsed              \n\
Size   Size    Size     Time     Throughput  \n\
bytes  bytes   bytes    secs.    %s/sec  \n\n";

  char *tput_fmt_0 =
    "%7.2f %s\n";

  char *tput_fmt_1 =
    "%6d %6d %6d    %-6.2f   %7.2f   %s\n";

  char *cpu_title = "\
Recv   Send    Send                          Utilization       Service Demand\n\
Socket Socket  Message  Elapsed              Send     Recv     Send    Recv\n\
Size   Size    Size     Time     Throughput  local    remote   local   remote\n\
bytes  bytes   bytes    secs.    %-8.8s/s  %% %c      %% %c      us/KB   us/KB\n\n";

  char *cpu_fmt_0 =
    "%6.3f %c %s\n";

  char *cpu_fmt_1 =
    "%6d %6d %6d    %-6.2f     %7.2f   %-6.2f   %-6.2f   %-6.3f  %-6.3f %s\n";

  char *ksink_fmt = "\n\
Alignment      Offset         %-8.8s %-8.8s    Sends   %-8.8s Recvs\n\
Local  Remote  Local  Remote  Xfered   Per                 Per\n\
Send   Recv    Send   Recv             Send (avg)          Recv (avg)\n\
%5d   %5d  %5d   %5d %6.4g  %6.2f    %6d   %6.2f %6d\n";


  float                 elapsed_time;

  /* what we want is to have a buffer space that is at least one */
  /* send-size greater than our send window. this will insure that we */
  /* are never trying to re-use a buffer that may still be in the hands */
  /* of the transport. This buffer will be malloc'd after we have found */
  /* the size of the local senc socket buffer. We will want to deal */
  /* with alignment and offset concerns as well. */

  struct ring_elt *send_ring;

  int len;
  unsigned int nummessages = 0;
  SOCKET send_socket;
  int bytes_remaining;

  /* with links like fddi, one can send > 32 bits worth of bytes
     during a test... ;-) at some point, this should probably become a
     64bit integral type, but those are not entirely common
     yet... time passes, and 64 bit types do indeed become common. */

  unsigned long long local_bytes_sent = 0;

  double        bytes_sent = 0.0;

  float local_cpu_utilization;
  float local_service_demand;
  float remote_cpu_utilization;
  float remote_service_demand;

  double        thruput;

  struct sockaddr_tipc remote_addr;
  struct tipc_portid   remote_port_id;

  struct        tipc_stream_request_struct       *tipc_stream_request;
  struct        tipc_stream_response_struct      *tipc_stream_response;
  struct        tipc_stream_results_struct       *tipc_stream_results;

  tipc_stream_request  =
    (struct tipc_stream_request_struct *)netperf_request.content.test_specific_data;
  tipc_stream_response =
    (struct tipc_stream_response_struct *)netperf_response.content.test_specific_data;
  tipc_stream_results   =
    (struct tipc_stream_results_struct *)netperf_response.content.test_specific_data;

#ifdef WANT_HISTOGRAM
  if (verbosity > 1) {
    time_hist = HIST_new();
  }
#endif /* WANT_HISTOGRAM */

  send_ring = NULL;
  confidence_iteration = 1;
  init_stat();

  /* we have a great-big while loop which controls the number of times */
  /* we run a particular test. this is for the calculation of a */
  /* confidence interval (I really should have stayed awake during */
  /* probstats :). If the user did not request confidence measurement */
  /* (no confidence is the default) then we will only go though the */
  /* loop once. the confidence stuff originates from the folks at IBM */

  while (((confidence < 0) && (confidence_iteration < iteration_max)) ||
         (confidence_iteration <= iteration_min)) {

    /* initialize a few counters. we have to remember that we might be */
    /* going through the loop more than once. */

    nummessages    =    0;
    bytes_sent     =    0.0;
    times_up       =    0;

    /* calibrate the cpu(s). We will perform this task within the tests */
    /* themselves. If the user has specified the cpu rate, then */
    /* calibrate_local_cpu will return rather quickly as it will have */
    /* nothing to do. If local_cpu_rate is zero, then we will go through */
    /* all the "normal" calibration stuff and return the rate back. */

    if (local_cpu_usage) {
      local_cpu_rate = calibrate_local_cpu(local_cpu_rate);
    }

    if (!no_control) {
      /* Tell the remote end to do a listen. The server alters the
         socket paramters on the other side at this point, hence the
         reason for all the values being passed in the setup
         message. If the user did not specify any of the parameters,
         they will be passed as 0, which will indicate to the remote
         that no changes beyond the system's default should be
         used. Alignment is the exception, it will default to 1, which
         will be no alignment alterations. */

      netperf_request.content.request_type = DO_TIPC_STREAM;
      tipc_stream_request->send_buf_size =       rss_size_req;
      tipc_stream_request->recv_buf_size =       rsr_size_req;
      tipc_stream_request->receive_size  =       recv_size;
      tipc_stream_request->recv_alignment        =       remote_recv_align;
      tipc_stream_request->recv_offset   =       remote_recv_offset;
      tipc_stream_request->measure_cpu   =       remote_cpu_usage;
      tipc_stream_request->cpu_rate      =       remote_cpu_rate;
      if (test_time) {
        tipc_stream_request->test_length =       test_time;
      }
      else {
        tipc_stream_request->test_length =       test_bytes;
      }
#ifdef DIRTY
      tipc_stream_request->dirty_count     =       rem_dirty_count;
      tipc_stream_request->clean_count     =       rem_clean_count;
#endif /* DIRTY */
      tipc_stream_request->port            =    atoi(remote_data_port);

      if (debug > 1) {
        fprintf(where,
		"netperf: send_tipc_stream: requesting TIPC stream test\n");
      }

      send_request();

      /* The response from the remote will contain all of the relevant
         socket parameters for this test type. We will put them back
         into the variables here so they can be displayed if desired.
         The remote will have calibrated CPU if necessary, and will
         have done all the needed set-up we will have calibrated the
         cpu locally before sending the request, and will grab the
         counter value right after the connect returns. The remote
         will grab the counter right after the accept call. This saves
	 the hassle of extra messages being sent for the TIPC
         tests.  */

      recv_response();

      if (!netperf_response.content.serv_errno) {
        if (debug)
          fprintf(where,"remote listen done.\n");
	// Get the id of the netserver tipc socket
        remote_port_id  = tipc_stream_response->id; 
        rsr_size        = tipc_stream_response->recv_buf_size;
        rss_size        = tipc_stream_response->send_buf_size;
        remote_cpu_usage= tipc_stream_response->measure_cpu;
        remote_cpu_rate = tipc_stream_response->cpu_rate;
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
    }

    if ( print_headers ) {
      print_top_tipc_test_header("TIPC STREAM TEST", remote_port_id);
    }

    memset(&remote_addr, 0, sizeof(remote_addr));
    remote_addr.family = AF_TIPC;
    remote_addr.addrtype = TIPC_ADDR_ID;
    remote_addr.addr.id = remote_port_id;
    remote_addr.scope = TIPC_ZONE_SCOPE;

    send_socket = create_tipc_socket();

    /* at this point, we have either retrieved the socket buffer sizes, */
    /* or have tried to set them, so now, we may want to set the send */
    /* size based on that (because the user either did not use a -m */
    /* option, or used one with an argument of 0). If the socket buffer */
    /* size is not available, we will set the send size to 4KB - no */
    /* particular reason, just arbitrary... */
    if (send_size == 0) {
      if (lss_size > 0) {
        send_size = lss_size;
      }
      else {
        send_size = 4096;
      }
    }

    /* set-up the data buffer ring with the requested alignment and offset. */
    /* note also that we have allocated a quantity */
    /* of memory that is at least one send-size greater than our socket */
    /* buffer size. We want to be sure that there are at least two */
    /* buffers allocated - this can be a bit of a problem when the */
    /* send_size is bigger than the socket size, so we must check... the */
    /* user may have wanted to explicitly set the "width" of our send */
    /* buffers, we should respect that wish... */
    if (send_width == 0) {
      send_width = (lss_size/send_size) + 1;
      if (send_width == 1) send_width++;
    }

    if (send_ring == NULL) {
      /* only allocate the send ring once. this is a networking test, */
      /* not a memory allocation test. this way, we do not need a */
      /* deallocate_buffer_ring() routine, and I don't feel like */
      /* writing one anyway :) raj 11/94 */
      send_ring = allocate_buffer_ring(send_width,
                                       send_size,
                                       local_send_align,
                                       local_send_offset);
    }

    if (connect(send_socket, (struct sockaddr *)&remote_addr, sizeof(remote_addr)) != 0) {
      perror("tipc: failed to connect to tipc netserver");
      exit(1);
    }

    /* Data Socket set-up is finished. If there were problems, either */
    /* the connect would have failed, or the previous response would */
    /* have indicated a problem. I failed to see the value of the */
    /* extra  message after the accept on the remote. If it failed, */
    /* we'll see it here. If it didn't, we might as well start pumping */
    /* data. */

    /* Set-up the test end conditions. For a stream test, they can be */
    /* either time or byte-count based. */

    if (test_time) {
      /* The user wanted to end the test after a period of time. */
      times_up = 0;
      bytes_remaining = 0;
      /* in previous revisions, we had the same code repeated through */
      /* all the test suites. this was unnecessary, and meant more */
      /* work for me when I wanted to switch to POSIX signals, so I */
      /* have abstracted this out into a routine in netlib.c. if you */
      /* are experiencing signal problems, you might want to look */
      /* there. raj 11/94 */
      start_timer(test_time);
    }
    else {
      /* The tester wanted to send a number of bytes. */
      bytes_remaining = test_bytes;
      times_up = 1;
    }

    /* The cpu_start routine will grab the current time and possibly */
    /* value of the idle counter for later use in measuring cpu */
    /* utilization and/or service demand and thruput. */

    cpu_start(local_cpu_usage);

    /* we only start the interval timer if we are using the
       timer-timed intervals rather than the sit and spin ones. raj
       2006-02-06 */
#if defined(WANT_INTERVALS)
    INTERVALS_INIT();
#endif /* WANT_INTERVALS */

    /* We use an "OR" to control test execution. When the test is */
    /* controlled by time, the byte count check will always return false. */
    /* When the test is controlled by byte count, the time test will */
    /* always return false. When the test is finished, the whole */
    /* expression will go false and we will stop sending data. */

    while ((!times_up) || (bytes_remaining > 0)) {

#ifdef DIRTY
      access_buffer(send_ring->buffer_ptr,
		    send_size,
		    loc_dirty_count,
		    loc_clean_count);
#endif /* DIRTY */

#ifdef WANT_HISTOGRAM
      if (verbosity > 1) {
	/* timestamp just before we go into send and then again just
	   after we come out raj 8/94 */
	/* but lets only do this if there is going to be a histogram
	   displayed */
	HIST_timestamp(&time_one);
      }
#endif /* WANT_HISTOGRAM */
      if((len=send(send_socket,
		   send_ring->buffer_ptr,
		   send_size,
		   0)) != send_size) {
	if ((len >=0) || SOCKET_EINTR(len)) {
	  /* the test was interrupted, must be the end of test */
	  break;
	}
	perror("netperf: data send error");
	exit(1);
      }

      local_bytes_sent += send_size;
#ifdef WANT_HISTOGRAM
      if (verbosity > 1) {
	/* timestamp the exit from the send call and update the histogram */
	HIST_timestamp(&time_two);
	HIST_add(time_hist,delta_micro(&time_one,&time_two));
      }
#endif /* WANT_HISTOGRAM */

#if defined(WANT_INTERVALS)
      INTERVALS_WAIT();
#endif /* WANT_INTERVALS */

      /* now we want to move our pointer to the next position in the */
      /* data buffer...we may also want to wrap back to the "beginning" */
      /* of the bufferspace, so we will mod the number of messages sent */
      /* by the send width, and use that to calculate the offset to add */
      /* to the base pointer. */
      nummessages++;
      send_ring = send_ring->next;
      if (bytes_remaining) {
	bytes_remaining -= send_size;
      }

    }
    
    /* The test is over. Flush the buffers to the remote end. We do a */
    /* graceful release to insure that all data has been taken by the */
    /* remote. */
    /* TIPC does not acknowledge connection shutdowns the same way TCP does,
       so we cannot do the SHUT_WR+recv() hack here*/

    if (shutdown(send_socket,SHUT_RDWR) == SOCKET_ERROR && !times_up) {
      perror("netperf: cannot shutdown tipc stream socket");
      exit(1);
    }

    /* this call will always give us the elapsed time for the test, and */
    /* will also store-away the necessaries for cpu utilization */

    cpu_stop(local_cpu_usage,&elapsed_time);    /* was cpu being */
                                                /* measured and how */
                                                /* long did we really */
                                                /* run? */

    /* we are finished with the socket, so close it to prevent hitting */
    /* the limit on maximum open files. */

    close(send_socket);

    if (!no_control) {
      /* Get the statistics from the remote end. The remote will have
         calculated service demand and all those interesting
         things. If it wasn't supposed to care, it will return obvious
         values. */

      recv_response();
      if (!netperf_response.content.serv_errno) {
        if (debug)
          fprintf(where,
                  "remote reporting results for %.2f seconds\n",
                  tipc_stream_results->elapsed_time);
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

      /* We now calculate what our thruput was for the test. In the
         future, we may want to include a calculation of the thruput
         measured by the remote, but it should be the case that for a
         TIPC stream test, that the two numbers should be *very*
         close... We calculate bytes_sent regardless of the way the
         test length was controlled.  If it was time, we needed to,
         and if it was by bytes, the user may have specified a number
         of bytes that wasn't a multiple of the send_size, so we
         really didn't send what he asked for ;-) */

      bytes_sent        = ntohd(tipc_stream_results->bytes_received);
    }
    else {
      bytes_sent = (double)local_bytes_sent;
    }

    thruput     = calc_thruput(bytes_sent);

    if (local_cpu_usage || remote_cpu_usage) {
      /* We must now do a little math for service demand and cpu */
      /* utilization for the system(s) */
      /* Of course, some of the information might be bogus because */
      /* there was no idle counter in the kernel(s). We need to make */
      /* a note of this for the user's benefit...*/
      if (local_cpu_usage) {

        local_cpu_utilization   = calc_cpu_util(0.0);
        local_service_demand    = calc_service_demand(bytes_sent,
                                                      0.0,
                                                      0.0,
                                                      0);
      }
      else {
        local_cpu_utilization   = (float) -1.0;
        local_service_demand    = (float) -1.0;
      }

      if (remote_cpu_usage) {

        remote_cpu_utilization  = tipc_stream_results->cpu_util;
        remote_service_demand   = calc_service_demand(bytes_sent,
                                                      0.0,
                                                      remote_cpu_utilization,
                                                      tipc_stream_results->num_cpus);
      }
      else {
        remote_cpu_utilization = (float) -1.0;
        remote_service_demand  = (float) -1.0;
      }
    }
    else {
      /* we were not measuring cpu, for the confidence stuff, we */
      /* should make it -1.0 */
      local_cpu_utilization     = (float) -1.0;
      local_service_demand      = (float) -1.0;
      remote_cpu_utilization = (float) -1.0;
      remote_service_demand  = (float) -1.0;
    }

    /* at this point, we want to calculate the confidence information. */
    /* if debugging is on, calculate_confidence will print-out the */
    /* parameters we pass it */

    calculate_confidence(confidence_iteration,
                         elapsed_time,
                         thruput,
                         local_cpu_utilization,
                         remote_cpu_utilization,
                         local_service_demand,
                         remote_service_demand);


    confidence_iteration++;
  }

  /* at this point, we have finished making all the runs that we */
  /* will be making. so, we should extract what the calcuated values */
  /* are for all the confidence stuff. we could make the values */
  /* global, but that seemed a little messy, and it did not seem worth */
  /* all the mucking with header files. so, we create a routine much */
  /* like calcualte_confidence, which just returns the mean values. */
  /* raj 11/94 */

  retrieve_confident_values(&elapsed_time,
                            &thruput,
                            &local_cpu_utilization,
                            &remote_cpu_utilization,
                            &local_service_demand,
                            &remote_service_demand);

  /* We are now ready to print all the information. If the user */
  /* has specified zero-level verbosity, we will just print the */
  /* local service demand, or the remote service demand. If the */
  /* user has requested verbosity level 1, he will get the basic */
  /* "streamperf" numbers. If the user has specified a verbosity */
  /* of greater than 1, we will display a veritable plethora of */
  /* background information from outside of this block as it it */
  /* not cpu_measurement specific...  */

  if (confidence < 0) {
    /* we did not hit confidence, but were we asked to look for it? */
    if (iteration_max > 1) {
      display_confidence();
    }
  }

  if (local_cpu_usage || remote_cpu_usage) {
    local_cpu_method = format_cpu_method(cpu_method);
    remote_cpu_method = format_cpu_method(tipc_stream_results->cpu_method);

    switch (verbosity) {
    case 0:
      if (local_cpu_usage) {
        fprintf(where,
                cpu_fmt_0,
                local_service_demand,
                local_cpu_method,
                ((print_headers) ||
                 (result_brand == NULL)) ? "" : result_brand);
      }
      else {
        fprintf(where,
                cpu_fmt_0,
                remote_service_demand,
                remote_cpu_method,
                ((print_headers) ||
                 (result_brand == NULL)) ? "" : result_brand);
      }
      break;
    case 1:
    case 2:
      if (print_headers) {
	fprintf(where,
                cpu_title,
                format_units(),
                local_cpu_method,
                remote_cpu_method);
      }

      fprintf(where,
              cpu_fmt_1,                /* the format string */
              rsr_size,                 /* remote recvbuf size */
              lss_size,                 /* local sendbuf size */
              send_size,                /* how large were the sends */
              elapsed_time,             /* how long was the test */
              thruput,                  /* what was the xfer rate */
              local_cpu_utilization,    /* local cpu */
              remote_cpu_utilization,   /* remote cpu */
              local_service_demand,     /* local service demand */
              remote_service_demand,    /* remote service demand */
              ((print_headers) ||
               (result_brand == NULL)) ? "" : result_brand);
      break;
    }
  }
  else {
    /* The tester did not wish to measure service demand. */

    switch (verbosity) {
    case 0:
      fprintf(where,
              tput_fmt_0,
              thruput,
              ((print_headers) ||
               (result_brand == NULL)) ? "" : result_brand);
      break;
    case 1:
    case 2:
      if (print_headers) {
	fprintf(where,tput_title,format_units());
      }
      fprintf(where,
              tput_fmt_1,               /* the format string */
              rsr_size,                 /* remote recvbuf size */
              lss_size,                 /* local sendbuf size */
              send_size,                /* how large were the sends */
              elapsed_time,             /* how long did it take */
              thruput,                  /* how fast did it go */
              ((print_headers) ||
               (result_brand == NULL)) ? "" : result_brand);
      break;
    }
  }

  /* it would be a good thing to include information about some of the */
  /* other parameters that may have been set for this test, but at the */
  /* moment, I do not wish to figure-out all the  formatting, so I will */
  /* just put this comment here to help remind me that it is something */
  /* that should be done at a later time. */

  if (verbosity > 1) {
    /* The user wanted to know it all, so we will give it to him. */
    /* This information will include as much as we can find about */
    /* TIPC statistics, the alignments of the sends and receives */
    /* and all that sort of rot... */

    /* this stuff needs to be worked-out in the presence of confidence */
    /* intervals and multiple iterations of the test... raj 11/94 */

    fprintf(where,
            ksink_fmt,
            "Bytes",
            "Bytes",
            "Bytes",
            local_send_align,
            remote_recv_align,
            local_send_offset,
            remote_recv_offset,
            bytes_sent,
            bytes_sent / (double)nummessages,
            nummessages,
            bytes_sent / (double)tipc_stream_results->recv_calls,
            tipc_stream_results->recv_calls);
    fflush(where);
#ifdef WANT_HISTOGRAM
    fprintf(where,"\n\nHistogram of time spent in send() call.\n");
    fflush(where);
    HIST_report(time_hist);
#endif /* WANT_HISTOGRAM */
  }

}  


void
recv_tipc_stream()
{

  struct sockaddr_tipc myaddr_in, peeraddr_in;
  SOCKET s_listen,s_data;
  netperf_socklen_t addrlen;
  int   len;

  unsigned int  receive_calls;
  float elapsed_time;
  double   bytes_received;
  struct ring_elt *recv_ring;
  char port_buffer[PORTBUFSIZE];

#ifdef DO_SELECT
  fd_set readfds;
  struct timeval timeout;
#endif /* DO_SELECT */

  struct        tipc_stream_request_struct       *tipc_stream_request;
  struct        tipc_stream_response_struct      *tipc_stream_response;
  struct        tipc_stream_results_struct       *tipc_stream_results;

#ifdef DO_SELECT
  FD_ZERO(&readfds);
  timeout.tv_sec = 1;
  timeout.tv_usec = 0;
#endif /* DO_SELECT */

  tipc_stream_request  =
    (struct tipc_stream_request_struct *)netperf_request.content.test_specific_data;
  tipc_stream_response =
    (struct tipc_stream_response_struct *)netperf_response.content.test_specific_data;
  tipc_stream_results   =
    (struct tipc_stream_results_struct *)netperf_response.content.test_specific_data;

  if (debug) {
    fprintf(where,"netserver: recv_tipc_stream: entered...\n");
    fflush(where);
  }

  /* We want to set-up the listen socket with all the desired */
  /* parameters and then let the initiator know that all is ready. If */
  /* socket size defaults are to be used, then the initiator will have */
  /* sent us 0's. If the socket sizes cannot be changed, then we will */
  /* send-back what they are. If that information cannot be determined, */
  /* then we send-back -1's for the sizes. If things go wrong for any */
  /* reason, we will drop back ten yards and punt. */

  /* If anything goes wrong, we want the remote to know about it. It */
  /* would be best if the error that the remote reports to the user is */
  /* the actual error we encountered, rather than some bogus unexpected */
  /* response type message. */

  if (debug) {
    fprintf(where,"recv_tipc_stream: setting the response type...\n");
    fflush(where);
  }

  netperf_response.content.response_type = TIPC_STREAM_RESPONSE;

  if (debug) {
    fprintf(where,"recv_tipc_stream: the response type is set...\n");
    fflush(where);
  }

  /* We now alter the message_ptr variable to be at the desired */
  /* alignment with the desired offset. */

  if (debug) {
    fprintf(where,"recv_tipc_stream: requested alignment of %d\n",
            tipc_stream_request->recv_alignment);
    fflush(where);
  }

  /* create_tipc_socket expects to find some things in the global */
  /* variables, so set the globals based on the values in the request. */
  /* once the socket has been created, we will set the response values */
  /* based on the updated value of those globals. */
  lss_size_req = tipc_stream_request->send_buf_size;
  lsr_size_req = tipc_stream_request->recv_buf_size;

  memset(&myaddr_in, 0, sizeof(myaddr_in));
  myaddr_in.family = AF_TIPC;
  myaddr_in.addrtype = TIPC_ADDR_NAME;
  myaddr_in.addr.name.name.type = NETSERVER_TIPC_DEFAULT;
  myaddr_in.addr.name.name.instance = 0;
  myaddr_in.scope = TIPC_ZONE_SCOPE;

  s_listen = create_tipc_socket();

  if (bind(s_listen, 
	   (struct sockaddr *)&myaddr_in, 
	   sizeof(myaddr_in)) < 0) {
    perror("Netserver: failed to bind tipc port name\n");
    exit(1);
  }

  /* what sort of sizes did we end-up with? */
  if (tipc_stream_request->receive_size == 0) {
    if (lsr_size > 0) {
      recv_size = lsr_size;
    }
    else {
      recv_size = 4096;
    }
  }
  else {
    recv_size = tipc_stream_request->receive_size;
  }

  /* we want to set-up our recv_ring in a manner analagous to what we */
  /* do on the sending side - this way one could conceivably go with a */
  /* double-buffering scheme when taking the data an putting it into */
  /* the filesystem or something like that. raj 7/94 */

  if (recv_width == 0) {
    recv_width = (lsr_size/recv_size) + 1;
    if (recv_width == 1) recv_width++;
  }

  recv_ring = allocate_buffer_ring(recv_width,
                                   recv_size,
                                   tipc_stream_request->recv_alignment,
                                   tipc_stream_request->recv_offset);

  if (debug) {
    fprintf(where,"recv_tipc_stream: receive alignment and offset set...\n");
    fflush(where);
  }

  /* Now, let's set-up the socket to listen for connections */
  if (listen(s_listen, 5) == SOCKET_ERROR) {
    netperf_response.content.serv_errno = errno;
    close(s_listen);
    send_response();

    exit(1);
  }

  /* If the initiator wanted cpu measurements, */
  /* then we must call the calibrate routine, which will return the max */
  /* rate back to the initiator. If the CPU was not to be measured, or */
  /* something went wrong with the calibration, we will return a -1 to */
  /* the initiator. */

  tipc_stream_response->cpu_rate = (float)0.0;   /* assume no cpu */
  if (tipc_stream_request->measure_cpu) {
    tipc_stream_response->measure_cpu = 1;
    tipc_stream_response->cpu_rate =
      calibrate_local_cpu(tipc_stream_request->cpu_rate);
  }
  else {
    tipc_stream_response->measure_cpu = 0;
  }

  /* before we send the response back to the initiator, pull some of */
  /* the socket parms from the globals */
  tipc_stream_response->send_buf_size = lss_size;
  tipc_stream_response->recv_buf_size = lsr_size;
  tipc_stream_response->receive_size = recv_size;

  /* Netperf will need the port id of s_listen to be able to connect */
  /* to netserver. This information is given by getsockname. */
  addrlen = sizeof(struct sockaddr_tipc);
  memset(&myaddr_in, 0, sizeof(myaddr_in));
  if (getsockname(s_listen, 
		  (struct sockaddr*)&myaddr_in, 
		  &addrlen) != 0) {
    perror("tipc: getsockname failed.");
    exit(1);
  }
  tipc_stream_response->id = myaddr_in.addr.id;


  send_response();

  addrlen = sizeof(peeraddr_in);

  if ((s_data = accept(s_listen, 
		       (struct sockaddr *)&peeraddr_in, 
		       &addrlen)) == INVALID_SOCKET) {
    /* Let's just punt. The remote will be given some information */
    close(s_listen);
    exit(1);
  }

  times_up = 0;

  start_timer(tipc_stream_request->test_length + PAD_TIME);

#ifdef KLUDGE_SOCKET_OPTIONS
  /* this is for those systems which *INCORRECTLY* fail to pass */
  /* attributes across an accept() call. Including this goes against */
  /* my better judgement :( raj 11/95 */

  //kludge_socket_options(s_data);

#endif /* KLUDGE_SOCKET_OPTIONS */

  /* Now it's time to start receiving data on the connection. We will */
  /* first grab the apropriate counters and then start grabbing. */

  cpu_start(tipc_stream_request->measure_cpu);

  /* The loop will exit when the sender does a shutdown */

  bytes_received = 0;
  receive_calls  = 0;

  while (!times_up && ((len = recv(s_data, recv_ring->buffer_ptr, recv_size, 0)) != 0)) {
    if (len == SOCKET_ERROR) {
      if (times_up)
	break;
      netperf_response.content.serv_errno = errno;
      send_response();
      exit(1);
    }
    bytes_received += len;
    receive_calls++;
#ifdef DIRTY
    /* we access the buffer after the recv() call now, rather than before */
    access_buffer(recv_ring->buffer_ptr,
                  recv_size,
                  tipc_stream_request->dirty_count,
                  tipc_stream_request->clean_count);
#endif /* DIRTY */


    /* move to the next buffer in the recv_ring */
    recv_ring = recv_ring->next;

#ifdef PAUSE
    sleep(1);
#endif /* PAUSE */

#ifdef DO_SELECT
    FD_SET(s_data,&readfds);
    select(s_data+1,&readfds,NULL,NULL,&timeout);
#endif /* DO_SELECT */
  }

  /* perform a shutdown to signal the sender that */
  /* we have received all the data sent. raj 4/93 */

  if (shutdown(s_data,SHUT_RDWR) == SOCKET_ERROR && !times_up) {
    netperf_response.content.serv_errno = errno;
    send_response();
    exit(1);
  }

  stop_timer();
  cpu_stop(tipc_stream_request->measure_cpu,&elapsed_time);

  /* send the results to the sender                     */

  if (debug) {
    fprintf(where,
            "recv_tipc_stream: got %g bytes\n",
            bytes_received);
    fprintf(where,
            "recv_tipc_stream: got %d recvs\n",
            receive_calls);
    fflush(where);
  }

  tipc_stream_results->bytes_received    = htond(bytes_received);
  tipc_stream_results->elapsed_time      = elapsed_time;
  tipc_stream_results->recv_calls        = receive_calls;

  tipc_stream_results->cpu_method = cpu_method;
  tipc_stream_results->num_cpus   = lib_num_loc_cpus;

  if (tipc_stream_request->measure_cpu) {
    tipc_stream_results->cpu_util        = calc_cpu_util(0.0);
  };

  if (debug) {
    fprintf(where,
            "recv_tipc_stream: test complete, sending results.\n");
    fprintf(where,
            "                 bytes_received %g receive_calls %d\n",
            bytes_received,
            receive_calls);
    fprintf(where,
            "                 len %d\n",
            len);
    fflush(where);
  }

  send_response();

  /* we are now done with the sockets */
  close(s_data);
  close(s_listen);

}









 /* this routine implements the sending (netperf) side of the TIPC_RR */
 /* test. */
void 
send_tipc_rr(char remote_host[])
{

  char *tput_title = "\
Local /Remote\n\
Socket Size   Request  Resp.   Elapsed  Trans.\n\
Send   Recv   Size     Size    Time     Rate         \n\
bytes  Bytes  bytes    bytes   secs.    per sec   \n\n";

  char *tput_title_band = "\
Local /Remote\n\
Socket Size   Request  Resp.   Elapsed  \n\
Send   Recv   Size     Size    Time     Throughput \n\
bytes  Bytes  bytes    bytes   secs.    %s/sec   \n\n";

  char *tput_fmt_0 =
    "%7.2f %s\n";

  char *tput_fmt_1_line_1 = "\
%-6d %-6d %-6d   %-6d  %-6.2f   %7.2f   %s\n";
  char *tput_fmt_1_line_2 = "\
%-6d %-6d\n";

  char *cpu_title = "\
Local /Remote\n\
Socket Size   Request Resp.  Elapsed Trans.   CPU    CPU    S.dem   S.dem\n\
Send   Recv   Size    Size   Time    Rate     local  remote local   remote\n\
bytes  bytes  bytes   bytes  secs.   per sec  %% %c    %% %c    us/Tr   us/Tr\n\n";

  char *cpu_title_tput = "\
Local /Remote\n\
Socket Size   Request Resp.  Elapsed Tput     CPU    CPU    S.dem   S.dem\n\
Send   Recv   Size    Size   Time    %-8.8s local  remote local   remote\n\
bytes  bytes  bytes   bytes  secs.   per sec  %% %c    %% %c    us/Tr   us/Tr\n\n";

  char *cpu_title_latency = "\
Local /Remote\n\
Socket Size   Request Resp.  Elapsed Latency  CPU    CPU    S.dem   S.dem\n\
Send   Recv   Size    Size   Time    usecs    local  remote local   remote\n\
bytes  bytes  bytes   bytes  secs.   per tran %% %c    %% %c    us/Tr   us/Tr\n\n";

  char *cpu_fmt_0 =
    "%6.3f %c %s\n";

  char *cpu_fmt_1_line_1 = "\
%-6d %-6d %-6d  %-6d %-6.2f  %-6.2f  %-6.2f %-6.2f %-6.3f  %-6.3f %s\n";

  char *cpu_fmt_1_line_2 = "\
%-6d %-6d\n";

  char *ksink_fmt = "\
Alignment      Offset         RoundTrip  Trans    Throughput\n\
Local  Remote  Local  Remote  Latency    Rate     %-8.8s/s\n\
Send   Recv    Send   Recv    usec/Tran  per sec  Outbound   Inbound\n\
%5d  %5d   %5d  %5d   %-6.3f   %-6.3f %-6.3f    %-6.3f\n";


  int                   timed_out = 0;
  float                 elapsed_time;

  int   	len;
  char  	*temp_message_ptr;
  int   	nummessages;
  SOCKET        send_socket;
  int   	trans_remaining;
  double        bytes_xferd;

  struct 	ring_elt *send_ring;
  struct 	ring_elt *recv_ring;

  int   	rsp_bytes_left;
  int   	rsp_bytes_recvd;

  float 	local_cpu_utilization;
  float	 	local_service_demand;
  float 	remote_cpu_utilization;
  float 	remote_service_demand;
  double        thruput;

  //struct addrinfo *local_res;
  //struct addrinfo *remote_res;

  struct sockaddr_tipc remote_addr;
  struct tipc_portid   remote_port_id;

  struct        tipc_rr_request_struct   *tipc_rr_request;
  struct        tipc_rr_response_struct  *tipc_rr_response;
  struct        tipc_rr_results_struct   *tipc_rr_result;

#ifdef WANT_FIRST_BURST
#define REQUEST_CWND_INITIAL 2
  int requests_outstanding = 0;
  int request_cwnd = REQUEST_CWND_INITIAL;  /* we ass-u-me that having
                                               three requests
                                               outstanding at the
                                               beginning of the test
                                               is ok with TIPC stacks
                                               of interest. the first
                                               two will come from our
                                               first_burst loop, and
                                               the third from our
                                               regularly scheduled
                                               send */
#endif

  tipc_rr_request =
    (struct tipc_rr_request_struct *)netperf_request.content.test_specific_data;
  tipc_rr_response=
    (struct tipc_rr_response_struct *)netperf_response.content.test_specific_data;
  tipc_rr_result =
    (struct tipc_rr_results_struct *)netperf_response.content.test_specific_data;

#ifdef WANT_HISTOGRAM
  if (verbosity > 1) {
    time_hist = HIST_new();
  }
#endif /* WANT_HISTOGRAM */

  /* initialize a few counters */

  send_ring = NULL;
  recv_ring = NULL;
  confidence_iteration = 1;
  init_stat();

  /* we have a great-big while loop which controls the number of times */
  /* we run a particular test. this is for the calculation of a */
  /* confidence interval (I really should have stayed awake during */
  /* probstats :). If the user did not request confidence measurement */
  /* (no confidence is the default) then we will only go though the */
  /* loop once. the confidence stuff originates from the folks at IBM */

  while (((confidence < 0) && (confidence_iteration < iteration_max)) ||
         (confidence_iteration <= iteration_min)) {

    /* initialize a few counters. we have to remember that we might be */
    /* going through the loop more than once. */

    nummessages     = 0;
    bytes_xferd     = 0.0;
    times_up        = 0;
    timed_out       = 0;
    trans_remaining = 0;

#ifdef WANT_FIRST_BURST
    /* we have to remember to reset the number of transactions
       outstanding and the "congestion window for each new
       iteration. raj 2006-01-31 */
    requests_outstanding = 0;
    request_cwnd = REQUEST_CWND_INITIAL;
#endif


    /* set-up the data buffers with the requested alignment and offset. */
    /* since this is a request/response test, default the send_width and */
    /* recv_width to 1 and not two raj 7/94 */

    if (send_width == 0) send_width = 1;
    if (recv_width == 0) recv_width = 1;

    if (send_ring == NULL) {
      send_ring = allocate_buffer_ring(send_width,
                                       req_size,
                                       local_send_align,
                                       local_send_offset);
    }

    if (recv_ring == NULL) {
      recv_ring = allocate_buffer_ring(recv_width,
                                       rsp_size,
                                       local_recv_align,
                                       local_recv_offset);
    }

    /* If the user has requested cpu utilization measurements, we must */
    /* calibrate the cpu(s). We will perform this task within the tests */
    /* themselves. If the user has specified the cpu rate, then */
    /* calibrate_local_cpu will return rather quickly as it will have */
    /* nothing to do. If local_cpu_rate is zero, then we will go through */
    /* all the "normal" calibration stuff and return the rate back.*/

    if (local_cpu_usage) {
      local_cpu_rate = calibrate_local_cpu(local_cpu_rate);
    }

    if (!no_control) {
      /* Tell the remote end to do a listen. The server alters the
         socket paramters on the other side at this point, hence the
         reason for all the values being passed in the setup
         message. If the user did not specify any of the parameters,
         they will be passed as 0, which will indicate to the remote
         that no changes beyond the system's default should be
         used. Alignment is the exception, it will default to 8, which
         will be no alignment alterations. */

      netperf_request.content.request_type      =       DO_TIPC_RR;
      tipc_rr_request->recv_buf_size     =       rsr_size_req;
      tipc_rr_request->send_buf_size     =       rss_size_req;
      tipc_rr_request->recv_alignment    =       remote_recv_align;
      tipc_rr_request->recv_offset       =       remote_recv_offset;
      tipc_rr_request->send_alignment    =       remote_send_align;
      tipc_rr_request->send_offset       =       remote_send_offset;
      tipc_rr_request->request_size      =       req_size;
      tipc_rr_request->response_size     =       rsp_size;
      tipc_rr_request->measure_cpu       =       remote_cpu_usage;
      tipc_rr_request->cpu_rate          =       remote_cpu_rate;
      if (test_time) {
        tipc_rr_request->test_length     =       test_time;
      }
      else {
        tipc_rr_request->test_length     =       test_trans * -1;
      }

      if (debug > 1) {
        fprintf(where,"netperf: send_tipc_rr: requesting TIPC rr test\n");
      }

      send_request();

      /* The response from the remote will contain all of the relevant
         socket parameters for this test type. We will put them back
         into the variables here so they can be displayed if desired.
         The remote will have calibrated CPU if necessary, and will
         have done all the needed set-up we will have calibrated the
         cpu locally before sending the request, and will grab the
         counter value right after the connect returns. The remote
         will grab the counter right after the accept call. This saves
         the hassle of extra messages being sent for the TCP
         tests.  */

      recv_response();

      if (!netperf_response.content.serv_errno) {
        if (debug)
          fprintf(where,"remote listen done.\n");
        // Get the id of the netserver tipc socket
        remote_port_id  = tipc_rr_response->id;
        rsr_size          = tipc_rr_response->recv_buf_size;
        rss_size          = tipc_rr_response->send_buf_size;
        remote_cpu_usage  = tipc_rr_response->measure_cpu;
        remote_cpu_rate   = tipc_rr_response->cpu_rate;
        /* make sure that port numbers are in network order */
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
    }

    if ( print_headers ) {
      print_top_tipc_test_header("TIPC REQUEST/RESPONSE TEST", remote_port_id);
    }

    memset(&remote_addr, 0, sizeof(remote_addr));
    remote_addr.family = AF_TIPC;
    remote_addr.addrtype = TIPC_ADDR_ID;
    remote_addr.addr.id = remote_port_id;
    remote_addr.scope = TIPC_ZONE_SCOPE;

    send_socket = create_tipc_socket();

    /*Connect up to the remote port on the data socket  */
    if (connect(send_socket, 
		(struct sockaddr *)&remote_addr, 
		sizeof(remote_addr)) == INVALID_SOCKET){
      perror("netperf: data socket connect failed");

      exit(1);
    }

    /* Data Socket set-up is finished. If there were problems, either the */
    /* connect would have failed, or the previous response would have */
    /* indicated a problem. I failed to see the value of the extra */
    /* message after the accept on the remote. If it failed, we'll see it */
    /* here. If it didn't, we might as well start pumping data. */

    /* Set-up the test end conditions. For a request/response test, they */
    /* can be either time or transaction based. */

    if (test_time) {
      /* The user wanted to end the test after a period of time. */
      times_up = 0;
      trans_remaining = 0;
      start_timer(test_time);
    }
    else {
      /* The tester wanted to send a number of bytes. */
      trans_remaining = test_bytes;
      times_up = 1;
    }

    /* The cpu_start routine will grab the current time and possibly */
    /* value of the idle counter for later use in measuring cpu */
    /* utilization and/or service demand and thruput. */

    cpu_start(local_cpu_usage);

#ifdef WANT_INTERVALS
    INTERVALS_INIT();
#endif /* WANT_INTERVALS */

    /* We use an "OR" to control test execution. When the test is */
    /* controlled by time, the byte count check will always return false. */
    /* When the test is controlled by byte count, the time test will */
    /* always return false. When the test is finished, the whole */
    /* expression will go false and we will stop sending data. I think I */
    /* just arbitrarily decrement trans_remaining for the timed test, but */
    /* will not do that just yet... One other question is whether or not */
    /* the send buffer and the receive buffer should be the same buffer. */

    while ((!times_up) || (trans_remaining > 0)) {
      /* send the request. we assume that if we use a blocking socket, */
      /* the request will be sent at one shot. */

#ifdef WANT_FIRST_BURST
      /* we can inject no more than request_cwnd, which will grow with
         time, and no more than first_burst_size.  we don't use <= to
         account for the "regularly scheduled" send call.  of course
         that makes it more a "max_outstanding_ than a
         "first_burst_size" but for now we won't fix the names. also,
         I suspect the extra check against < first_burst_size is
         redundant since later I expect to make sure that request_cwnd
         can never get larger than first_burst_size, but just at the
         moment I'm feeling like a belt and suspenders kind of
         programmer. raj 2006-01-30 */
      while ((first_burst_size > 0) &&
             (requests_outstanding < request_cwnd) &&
             (requests_outstanding < first_burst_size)) {
        if (debug) {
          fprintf(where,
                  "injecting, req_outstndng %d req_cwnd %d burst %d\n",
                  requests_outstanding,
                  request_cwnd,
                  first_burst_size);
        }
        if ((len = send(send_socket,
                        send_ring->buffer_ptr,
                        req_size,
                        0)) != req_size) {
          /* we should never hit the end of the test in the first burst */
          perror("send_tipc_rr: initial burst data send error");
          exit(-1);
        }
        requests_outstanding += 1;
      }

#endif /* WANT_FIRST_BURST */

#ifdef WANT_HISTOGRAM
      if (verbosity > 1) {
        /* timestamp just before our call to send, and then again just
           after the receive raj 8/94 */
        /* but only if we are actually going to display one. raj
           2007-02-07 */

        HIST_timestamp(&time_one);
      }
#endif /* WANT_HISTOGRAM */

      if ((len = send(send_socket,
                      send_ring->buffer_ptr,
                      req_size,
                      0)) != req_size) {
        if (SOCKET_EINTR(len) || (errno == 0)) {
          /* we hit the end of a */
          /* timed test. */
          timed_out = 1;
          break;
        }
        perror("send_tipc_rr: data send error");
        exit(1);
      }
      send_ring = send_ring->next;

#ifdef WANT_FIRST_BURST
      requests_outstanding += 1;
#endif

      /* receive the response */
      rsp_bytes_left = rsp_size;
      temp_message_ptr  = recv_ring->buffer_ptr;
      while(rsp_bytes_left > 0) {
        if((rsp_bytes_recvd=recv(send_socket,
                                 temp_message_ptr,
                                 rsp_bytes_left,
                                 0)) == SOCKET_ERROR || rsp_bytes_recvd == 0) {
                if ( SOCKET_EINTR(rsp_bytes_recvd) ) {
                    /* We hit the end of a timed test. */
                        timed_out = 1;
                        break;
                }
          perror("send_tipc_rr: data recv error");
          exit(1);
        }
        rsp_bytes_left -= rsp_bytes_recvd;
        temp_message_ptr  += rsp_bytes_recvd;
      }
      recv_ring = recv_ring->next;

#ifdef WANT_FIRST_BURST
      /* so, since we've gotten a response back, update the
         bookkeeping accordingly.  there is one less request
         outstanding and we can put one more out there than before. */
      requests_outstanding -= 1;
      if (request_cwnd < first_burst_size) {
        request_cwnd += 1;
        if (debug) {
          fprintf(where,
                  "incr req_cwnd to %d first_burst %d reqs_outstndng %d\n",
                  request_cwnd,
                  first_burst_size,
                  requests_outstanding);
        }
      }
#endif
      if (timed_out) {
        /* we may have been in a nested while loop - we need */
        /* another call to break. */
        break;
      }

#ifdef WANT_HISTOGRAM
      if (verbosity > 1) {
        HIST_timestamp(&time_two);
        HIST_add(time_hist,delta_micro(&time_one,&time_two));
      }
#endif /* WANT_HISTOGRAM */

#ifdef WANT_INTERVALS
      INTERVALS_WAIT();
#endif /* WANT_INTERVALS */

      nummessages++;
      if (trans_remaining) {
        trans_remaining--;
      }

      if (debug > 3) {
        if ((nummessages % 100) == 0) {
          fprintf(where,
                  "Transaction %d completed\n",
                  nummessages);
          fflush(where);
        }
      }
    }
    
    /* At this point we used to call shutdown on the data socket to be
       sure all the data was delivered, but this was not germane in a
       request/response test, and it was causing the tests to "hang"
       when they were being controlled by time. So, I have replaced
       this shutdown call with a call to close that can be found later
       in the procedure. */

    /* this call will always give us the elapsed time for the test,
       and will also store-away the necessaries for cpu utilization */

    cpu_stop(local_cpu_usage,&elapsed_time);    /* was cpu being */
                                                /* measured? how long */
                                                /* did we really run? */

    if (!no_control) {
      /* Get the statistics from the remote end. The remote will have
         calculated CPU utilization. If it wasn't supposed to care, it
         will return obvious values. */

      recv_response();
      if (!netperf_response.content.serv_errno) {
        if (debug)
          fprintf(where,"remote results obtained\n");
      }
      else {
        Set_errno(netperf_response.content.serv_errno);
        fprintf(where,"netperf: remote error %d",
                netperf_response.content.serv_errno);
        perror("");
        fflush(where);
        exit(1);
      }
    }

    confidence_iteration++;
  }

}




 /* this routine implements the receive (netserver) side of a TIPC_RR */
 /* test */
void
recv_tipc_rr()
{

  struct ring_elt 	*send_ring;
  struct ring_elt 	*recv_ring;

  //struct addrinfo 	*local_res;
  char 			local_name[BUFSIZ];
  char 			port_buffer[PORTBUFSIZE];

  //struct        	sockaddr_storage        myaddr_in,
  //peeraddr_in;
  struct sockaddr_tipc	myaddr_in, peeraddr_in;
  SOCKET        	s_listen,s_data;
  netperf_socklen_t     addrlen;
  char  		*temp_message_ptr;
  int   		trans_received;
  int   		trans_remaining;
  int   		bytes_sent;
  int   		request_bytes_recvd;
  int   		request_bytes_remaining;
  int   		timed_out = 0;
  int   		sock_closed = 0;
  float 		elapsed_time;

  struct        tipc_rr_request_struct   *tipc_rr_request;
  struct        tipc_rr_response_struct  *tipc_rr_response;
  struct        tipc_rr_results_struct   *tipc_rr_results;

  tipc_rr_request =
    (struct tipc_rr_request_struct *)netperf_request.content.test_specific_data;
  tipc_rr_response =
    (struct tipc_rr_response_struct *)netperf_response.content.test_specific_data;
  tipc_rr_results =
    (struct tipc_rr_results_struct *)netperf_response.content.test_specific_data;

  if (debug) {
    fprintf(where,"netserver: recv_tipc_rr: entered...\n");
    fflush(where);
  }

  /* We want to set-up the listen socket with all the desired */
  /* parameters and then let the initiator know that all is ready. If */
  /* socket size defaults are to be used, then the initiator will have */
  /* sent us 0's. If the socket sizes cannot be changed, then we will */
  /* send-back what they are. If that information cannot be determined, */
  /* then we send-back -1's for the sizes. If things go wrong for any */
  /* reason, we will drop back ten yards and punt. */

  /* If anything goes wrong, we want the remote to know about it. It */
  /* would be best if the error that the remote reports to the user is */
  /* the actual error we encountered, rather than some bogus unexpected */
  /* response type message. */

  if (debug) {
    fprintf(where,"recv_tipc_rr: setting the response type...\n");
    fflush(where);
  }

  netperf_response.content.response_type = TIPC_RR_RESPONSE;

  if (debug) {
    fprintf(where,"recv_tipc_rr: the response type is set...\n");
    fflush(where);
  }

  /* allocate the recv and send rings with the requested alignments */
  /* and offsets. raj 7/94 */
  if (debug) {
    fprintf(where,"recv_tipc_rr: requested recv alignment of %d offset %d\n",
            tipc_rr_request->recv_alignment,
            tipc_rr_request->recv_offset);
    fprintf(where,"recv_tipc_rr: requested send alignment of %d offset %d\n",
            tipc_rr_request->send_alignment,
            tipc_rr_request->send_offset);
    fflush(where);
  }

  /* at some point, these need to come to us from the remote system */
  if (send_width == 0) send_width = 1;
  if (recv_width == 0) recv_width = 1;

  send_ring = allocate_buffer_ring(send_width,
                                   tipc_rr_request->response_size,
                                   tipc_rr_request->send_alignment,
                                   tipc_rr_request->send_offset);

  recv_ring = allocate_buffer_ring(recv_width,
                                   tipc_rr_request->request_size,
                                   tipc_rr_request->recv_alignment,
                                   tipc_rr_request->recv_offset);


  /* Grab a socket to listen on, and then listen on it. */

  if (debug) {
    fprintf(where,"recv_tipc_rr: grabbing a socket...\n");
    fflush(where);
  }

  /* create_tipc_socket expects to find some things in the global */
  /* variables, so set the globals based on the values in the request. */
  /* once the socket has been created, we will set the response values */
  /* based on the updated value of those globals. raj 7/94 */
  lss_size_req = tipc_rr_request->send_buf_size;
  lsr_size_req = tipc_rr_request->recv_buf_size;

  memset(&myaddr_in, 0, sizeof(myaddr_in));
  myaddr_in.family = AF_TIPC;
  myaddr_in.addrtype = TIPC_ADDR_NAME;
  myaddr_in.addr.name.name.type = NETSERVER_TIPC_DEFAULT;
  myaddr_in.addr.name.name.instance = 0;
  myaddr_in.scope = TIPC_ZONE_SCOPE;

  s_listen = create_tipc_socket();

  if (bind(s_listen,
           (struct sockaddr *)&myaddr_in,
           sizeof(myaddr_in)) < 0) {
    perror("Netserver: failed to bind tipc port name\n");
    exit(1);
  }

  if (s_listen == INVALID_SOCKET) {
    netperf_response.content.serv_errno = errno;
    send_response();

    exit(1);
  }

  /* Now, let's set-up the socket to listen for connections */
  if (listen(s_listen, 5) == SOCKET_ERROR) {
    netperf_response.content.serv_errno = errno;
    close(s_listen);
    send_response();

    exit(1);
  }

  /* Netperf will need the port id of s_listen to be able to connect */
  /* to netserver. This information is given by getsockname. */
  addrlen = sizeof(struct sockaddr_tipc);
  memset(&myaddr_in, 0, sizeof(myaddr_in));
  if (getsockname(s_listen,
                  (struct sockaddr*)&myaddr_in,
                  &addrlen) != 0) {
    perror("tipc: getsockname failed.");
    exit(1);
  }
  tipc_rr_response->id = myaddr_in.addr.id;

  /* If the initiator wanted cpu measurements, */
  /* then we must call the calibrate routine, which will return the max */
  /* rate back to the initiator. If the CPU was not to be measured, or */
  /* something went wrong with the calibration, we will return a 0.0 to */
  /* the initiator. */

  tipc_rr_response->cpu_rate = (float)0.0;       /* assume no cpu */
  tipc_rr_response->measure_cpu = 0;

  if (tipc_rr_request->measure_cpu) {
    tipc_rr_response->measure_cpu = 1;
    tipc_rr_response->cpu_rate = calibrate_local_cpu(tipc_rr_request->cpu_rate);
  }


  /* before we send the response back to the initiator, pull some of */
  /* the socket parms from the globals */
  tipc_rr_response->send_buf_size = lss_size;
  tipc_rr_response->recv_buf_size = lsr_size;
  tipc_rr_response->test_length = tipc_rr_request->test_length;
  send_response();

  addrlen = sizeof(peeraddr_in);

  if ((s_data = accept(s_listen,
                       (struct sockaddr *)&peeraddr_in,
                       &addrlen)) == INVALID_SOCKET) {
    /* Let's just punt. The remote will be given some information */
    close(s_listen);
    exit(1);
  }

#ifdef KLUDGE_SOCKET_OPTIONS
  /* this is for those systems which *INCORRECTLY* fail to pass */
  /* attributes across an accept() call. Including this goes against */
  /* my better judgement :( raj 11/95 */

  kludge_socket_options(s_data);

#endif /* KLUDGE_SOCKET_OPTIONS */

  if (debug) {
    fprintf(where,"recv_tipc_rr: accept completes on the data connection.\n");
    fflush(where);
  }

  /* Now it's time to start receiving data on the connection. We will */
  /* first grab the apropriate counters and then start grabbing. */

  cpu_start(tipc_rr_request->measure_cpu);

  /* The loop will exit when we hit the end of the test time, or when */
  /* we have exchanged the requested number of transactions. */

  if (tipc_rr_request->test_length > 0) {
    times_up = 0;
    trans_remaining = 0;
    start_timer(tipc_rr_request->test_length + PAD_TIME);
  }
  else {
    times_up = 1;
    trans_remaining = tipc_rr_request->test_length * -1;
  }

  trans_received = 0;

  while ((!times_up) || (trans_remaining > 0)) {
    temp_message_ptr = recv_ring->buffer_ptr;
    request_bytes_remaining     = tipc_rr_request->request_size;
    while(request_bytes_remaining > 0) {
      if((request_bytes_recvd=recv(s_data,
                                   temp_message_ptr,
                                   request_bytes_remaining,
                                   0)) == SOCKET_ERROR) {
        if (SOCKET_EINTR(request_bytes_recvd))
        {
          timed_out = 1;
          break;
        }

        netperf_response.content.serv_errno = errno;
        send_response();
        exit(1);
      }
      else if( request_bytes_recvd == 0 ) {
        if (debug) {
          fprintf(where,"zero is my hero\n");
          fflush(where);
        }
        sock_closed = 1;
        break;
      }
      else {
        request_bytes_remaining -= request_bytes_recvd;
        temp_message_ptr  += request_bytes_recvd;
      }
    }

    recv_ring = recv_ring->next;

    if ((timed_out) || (sock_closed)) {
      /* we hit the end of the test based on time - or the socket
         closed on us along the way.  bail out of here now... */
      if (debug) {
        fprintf(where,"yo5\n");
        fflush(where);
      }
      break;
    }

    /* Now, send the response to the remote */
    if((bytes_sent=send(s_data,
                        send_ring->buffer_ptr,
                        tipc_rr_request->response_size,
                        0)) == SOCKET_ERROR) {
      if (SOCKET_EINTR(bytes_sent)) {
        /* the test timer has popped */
        timed_out = 1;
        fprintf(where,"yo6\n");
        fflush(where);
        break;
      }
      netperf_response.content.serv_errno = 992;
      send_response();
      exit(1);
    }

    send_ring = send_ring->next;

    trans_received++;
    if (trans_remaining) {
      trans_remaining--;
    }
  }

  /* The loop now exits due to timeout or transaction count being */
  /* reached */

  cpu_stop(tipc_rr_request->measure_cpu,&elapsed_time);

  stop_timer();

  if (timed_out) {
    /* we ended the test by time, which was at least 2 seconds */
    /* longer than we wanted to run. so, we want to subtract */
    /* PAD_TIME from the elapsed_time. */
    elapsed_time -= PAD_TIME;
  }

  /* send the results to the sender                     */

  if (debug) {
    fprintf(where,
            "recv_tipc_rr: got %d transactions\n",
            trans_received);
    fflush(where);
  }

  tipc_rr_results->bytes_received = (trans_received *
                                    (tipc_rr_request->request_size +
                                     tipc_rr_request->response_size));
  tipc_rr_results->trans_received = trans_received;
  tipc_rr_results->elapsed_time   = elapsed_time;
  tipc_rr_results->cpu_method     = cpu_method;
  tipc_rr_results->num_cpus       = lib_num_loc_cpus;
  if (tipc_rr_request->measure_cpu) {
    tipc_rr_results->cpu_util    = calc_cpu_util(elapsed_time);
  }

  if (debug) {
    fprintf(where,
            "recv_tipc_rr: test complete, sending results.\n");
    fflush(where);
  }

  /* we are now done with the sockets */
  close(s_data);
  close(s_listen);

  send_response();



}




void
print_tipc_usage()
{

  fwrite(tipc_usage, sizeof(char), strlen(tipc_usage), stdout);
  exit(1);

}




void
scan_tipc_args(int argc, char *argv[])
{

#define TIPC_ARGS "h:b:m:M:oO:r:s:S:u"

  extern char   *optarg;          /* pointer to option string   */

  int           c;
  int           have_uuid = 0;

  char arg1[BUFSIZ];  /* argument holders          */
  char arg2[BUFSIZ];

  if (debug) {
    int i;
    printf("%s called with the following argument vector\n",
           __FUNCTION__);
    for (i = 0; i< argc; i++) {
      printf("%s ",argv[i]);
    }
    printf("\n");
  }
  /* Go through all the command line arguments and break them */
  /* out. For those options that take two parms, specifying only */
  /* the first will set both to that value. Specifying only the */
  /* second will leave the first untouched. To change only the */
  /* first, use the form "first," (see the routine break_args.. */

  while ((c= getopt(argc, argv, TIPC_ARGS)) != EOF) {
    switch (c) {
    case '?':
    case 'h':
      print_tipc_usage();
      exit(1);
    case 'b':
#ifdef WANT_FIRST_BURST
      first_burst_size = atoi(optarg);
#else /* WANT_FIRST_BURST */
      printf("Initial request burst functionality not compiled-in!\n");
#endif /* WANT_FIRST_BURST */
      break;
    case 'm':
      /* set the send size */
      send_size = convert(optarg);
      break;
    case 'M':
      /* set the recv size */
      recv_size = convert(optarg);
      break;
    case 'o':
      netperf_output_mode = CSV;
      legacy = 0;
      /* obliterate any previous file name */
      if (output_selection_spec) {
        free(output_selection_spec);
        output_selection_spec = NULL;
      }
      if (output_selection_spec) {
        free(output_selection_spec);
        output_selection_spec = NULL;
      }
      if (argv[optind] && ((unsigned char)argv[optind][0] != '-')) {
        /* we assume that what follows is the name of a file with the
           list of desired output values. */
        output_selection_spec = strdup(argv[optind]);
        optind++;
        /* special case - if the file name is "?" then we will emit a
           list of the available outputs */
        if (strcmp(output_selection_spec,"?") == 0) {
          dump_netperf_output_choices(stdout,1);
          exit(1);
        }
      }
      break;
    case 'O':
      netperf_output_mode = HUMAN;
      legacy = 0;
      /* obliterate any previous file name */
      if (output_selection_spec) {
        free(output_selection_spec);
        output_selection_spec = NULL;
      }
      if (argv[optind] && ((unsigned char)argv[optind][0] != '-')) {
        /* we assume that what follows is the name of a file with the
           list of desired output values */
        output_selection_spec = strdup(argv[optind]);
        optind++;
        if (strcmp(output_selection_spec,"?") == 0) {
          dump_netperf_output_choices(stdout,0);
          exit(1);
        }
      }
      break;
    case 'r':
      /* set the request/response sizes. setting request/response
         sizes implicitly sets direction to XMIT and RECV */
      if (implicit_direction) {
        direction |= NETPERF_XMIT;
        direction |= NETPERF_RECV;
      }
      break_args(optarg,arg1,arg2);
      if (arg1[0])
        req_size = convert(arg1);
      if (arg2[0])
        rsp_size = convert(arg2);
      break;
    case 's':
      /* set local socket sizes */
      break_args(optarg,arg1,arg2);
      if (arg1[0])
        lss_size_req = convert(arg1);
      if (arg2[0])
        lsr_size_req = convert(arg2);
      break;
    case 'S':
      /* set remote socket sizes */
      break_args(optarg,arg1,arg2);
      if (arg1[0])
        rss_size_req = convert(arg1);
      if (arg2[0])
        rsr_size_req = convert(arg2);
      break;
    case 'u':
      /* use the supplied string as the UUID for this test. at some
         point we may want to sanity check the string we are given but
         for now we won't worry about it */
      strncpy(test_uuid,optarg,sizeof(test_uuid));
      /* strncpy may leave us with a string without a null at the end */
      test_uuid[sizeof(test_uuid) - 1] = 0;
      have_uuid = 1;
      break;
    }
  }
}


