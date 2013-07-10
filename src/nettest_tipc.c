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
#include "nettest_bsd.h"

static  int confidence_iteration;
static  char  local_cpu_method;
static  char  remote_cpu_method;

void
print_top_tipc_test_header(char test_name[]) 
{
  char *address_buf;

  address_buf = malloc(16); /* magic constant */

  if (address_buf == NULL) {
    fprintf(where,"Unable to allocate address_buf\n");
    fflush(where);
    exit(1);
  }

  /* we want to have some additional, interesting information in the
     headers. we know some of it here, but not all, so we will only
     print the test title here and will print the results titles after
     the test is finished */
  fprintf(where,"%s",test_name);

  if (iteration_max > 1) {
    fprintf(where,
            " : +/-%.3f%% @ %2d%% conf. %s",
            interval/0.02,
            confidence_level,
            result_confidence_only ? " on result only" : "");
  }
    if ((loc_nodelay > 0) || (rem_nodelay > 0)) {
    fprintf(where," : nodelay");
  }
  if ((loc_sndavoid > 0) ||
      (loc_rcvavoid > 0) ||
      (rem_sndavoid > 0) ||
      (rem_rcvavoid > 0)) {
    fprintf(where," : copy avoidance");
  }

  if (no_control) {
    fprintf(where," : no control");
  }

#ifdef WANT_HISTOGRAM
  fprintf(where," : histogram");
#endif /* WANT_HISTOGRAM */

#ifdef WANT_INTERVALS
#ifndef WANT_SPIN
  fprintf(where," : interval");
#else
  fprintf(where," : spin interval");
#endif
#endif /* WANT_INTERVALS */

#ifdef DIRTY
  fprintf(where," : dirty data");
#endif /* DIRTY */
#ifdef WANT_DEMO
  fprintf(where," : demo");
#endif
//#ifdef WANT_FIRST_BURST
  /* a little hokey perhaps, but we really only want this to be
     emitted for tests where it actually is used, which means a
     "REQUEST/RESPONSE" test. raj 2005-11-10 */
//  if (strstr(test_name,"REQUEST/RESPONSE")) {
//    fprintf(where," : first burst %d",first_burst_size);
//  }
//#endif
  if (cpu_binding_requested) {
    fprintf(where," : cpu bind");
  }
  fprintf(where,"\n");

  free(address_buf);
}

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

  char *ksink_fmt2 = "\n\
Maximum\n\
Segment\n\
Size (bytes)\n\
%6d\n";


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
  int tipc_mss = -1;  /* possibly uninitialized on printf far below */

  /* with links like fddi, one can send > 32 bits worth of bytes
     during a test... ;-) at some point, this should probably become a
     64bit integral type, but those are not entirely common
     yet... time passes, and 64 bit types do indeed become common. */
#if defined(WIN32) && _MSC_VER <= 1200
  __int64 local_bytes_sent = 0
#else
    unsigned long long local_bytes_sent = 0;
#endif

  double        bytes_sent = 0.0;

  float local_cpu_utilization;
  float local_service_demand;
  float remote_cpu_utilization;
  float remote_service_demand;

  double        thruput;

  //struct addrinfo *remote_res;
  struct sockaddr_tipc remote_addr;
  struct tipc_portid   remote_port_id;
  //struct addrinfo *local_res;

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
  /* since we are now disconnected from the code that established the */
  /* control socket, and since we want to be able to use different */
  /* protocols and such, we are passed the name of the remote host and */
  /* must turn that into the test specific addressing information. */

  /* complete_addrinfos will either succede or exit the process */
  //  complete_addrinfos(&remote_res,
  //                     &local_res,
  //                     remote_host,
  //                     SOCK_STREAM,
  //                     IPPROTO_TCP,
  //                     0);

  if ( print_headers ) {
    print_top_tipc_test_header("TIPC STREAM TEST");
  }

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
      tipc_stream_request->no_delay      =       rem_nodelay;
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
      tipc_stream_request->so_rcvavoid   =       rem_rcvavoid;
      tipc_stream_request->so_sndavoid   =       rem_sndavoid;
#ifdef DIRTY
      tipc_stream_request->dirty_count     =       rem_dirty_count;
      tipc_stream_request->clean_count     =       rem_clean_count;
#endif /* DIRTY */
      tipc_stream_request->port            =    atoi(remote_data_port);
      //tcp_stream_request->ipfamily = af_to_nf(remote_res->ai_family);

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
         the hassle of extra messages being sent for the TCP
         tests.  */

      recv_response();

      if (!netperf_response.content.serv_errno) {
        if (debug)
          fprintf(where,"remote listen done.\n");
        remote_port_id = tipc_stream_response->id;
        rsr_size              = tipc_stream_response->recv_buf_size;
        rss_size              = tipc_stream_response->send_buf_size;
        rem_nodelay     =       tipc_stream_response->no_delay;
        remote_cpu_usage=       tipc_stream_response->measure_cpu;
        remote_cpu_rate = tipc_stream_response->cpu_rate;

        /* we have to make sure that the server port number is in
           network order */
        //set_port_number(remote_res,
        //                (short)tcp_stream_response->data_port_number);

        rem_rcvavoid    = tipc_stream_response->so_rcvavoid;
        rem_sndavoid    = tipc_stream_response->so_sndavoid;

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


    /* Use received port id to connect to the remote tipc port */
    memset(&remote_addr, 0, sizeof(remote_addr));
    remote_addr.family = AF_TIPC;
    remote_addr.addrtype = TIPC_ADDR_ID;
    remote_addr.addr.id = remote_port_id;
    remote_addr.scope = TIPC_ZONE_SCOPE;

    send_socket = socket (AF_TIPC, SOCK_STREAM, 0);

    if (send_socket == INVALID_SOCKET){
      perror("netperf: send_tipc_stream: tipc stream data socket");
      exit(1);
    }

    if (debug) {
      fprintf(where,"send_tipc_stream: send_socket obtained...\n");
    }

    // set buffer sizes and other cool stuff
    set_sock_buffer (send_socket, SEND_BUFFER, lss_size_req, &lss_size);
    set_sock_buffer (send_socket, RECV_BUFFER, lsr_size_req, &lsr_size);



/* begin: moved down */

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

#ifdef WANT_DEMO
    demo_stream_setup(lss_size,rsr_size);
#endif

/* end: moved down */




    if (connect(send_socket, (struct sockaddr *)&remote_addr, sizeof(remote_addr)) != 0) {
      perror("tipc: failed to connect to tipc netserver");
      exit(1);
    }

#ifdef WIN32
    /* this is used so the timer thread can close the socket out from */
    /* under us, which to date is the easiest/cleanest/least */
    /* Windows-specific way I can find to force the winsock calls to */
    /* return WSAEINTR with the test is over. anything that will run on */
    /* 95 and NT and is closer to what netperf expects from Unix signals */
    /* and such would be appreciated raj 1/96 */
    win_kludge_socket = send_socket;
#endif /* WIN32 */
    
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
      /* in previous revisions, we had the same code repeated throught */
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

    /* before we start, initialize a few variables */

#ifdef WANT_DEMO
    if (demo_mode) {
      demo_first_timestamp();
    }
#endif


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
	printf("sent bytes: %llu\n", local_bytes_sent);
	perror("netperf: data send error");
	printf("len was %d\n",len);
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

#ifdef WANT_DEMO
      demo_stream_interval(send_size);
#endif

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

    /* but first, if the verbosity is greater than 1, find-out what */
    /* the TCP maximum segment_size was (if possible) */
    if (verbosity > 1) {
      tipc_mss = -1;
      //get_tcp_info(send_socket,&tipc_mss);
    }

    if (shutdown(send_socket,SHUT_WR) == SOCKET_ERROR && !times_up) {
      perror("netperf: cannot shutdown tipc stream socket");
      exit(1);
    }

    /* hang a recv() off the socket to block until the remote has */
    /* brought all the data up into the application. it will do a */
    /* shutdown to cause a FIN to be sent our way. We will assume that */
    /* any exit from the recv() call is good... raj 4/93 */
 
    recv(send_socket, send_ring->buffer_ptr, send_size, 0);

    /* this call will always give us the elapsed time for the test, and */
    /* will also store-away the necessaries for cpu utilization */

    cpu_stop(local_cpu_usage,&elapsed_time);    /* was cpu being */
                                                /* measured and how */
                                                /* long did we really */
                                                /* run? */

    /* we are finished with the socket, so close it to prevent hitting */
    /* the limit on maximum open files. */

    close(send_socket);

#if defined(WANT_INTERVALS)
#ifdef WIN32
    stop_itimer();
#endif
#endif /* WANT_INTERVALS */

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
         TCP stream test, that the two numbers should be *very*
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
    /* TCP statistics, the alignments of the sends and receives */
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
    fprintf(where,
            ksink_fmt2,
            tipc_mss);
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

  FILE *fp; 						//printing debug info. Old

  unsigned int  receive_calls;
  float elapsed_time;
  double   bytes_received;

  struct ring_elt *recv_ring;

  //struct addrinfo *local_res;
  char local_name[BUFSIZ];
  char port_buffer[PORTBUFSIZE];

#ifdef DO_SELECT
  fd_set readfds;
  struct timeval timeout;
#endif /* DO_SELECT */

  struct        tipc_stream_request_struct       *tipc_stream_request;
  struct        tipc_stream_response_struct      *tipc_stream_response;
  struct        tipc_stream_results_struct       *tipc_stream_results;

  /* Confirm that netperf_request is received - for debugging */
  fp = fopen("netserver_output","a");
  fprintf(fp, "netserver: TIPC stream test.\n");

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

  /* create_data_socket expects to find some things in the global */
  /* variables, so set the globals based on the values in the request. */
  /* once the socket has been created, we will set the response values */
  /* based on the updated value of those globals. raj 7/94 */
  lss_size_req = tipc_stream_request->send_buf_size;
  lsr_size_req = tipc_stream_request->recv_buf_size;
  loc_nodelay  = tipc_stream_request->no_delay;
  loc_rcvavoid = tipc_stream_request->so_rcvavoid;
  loc_sndavoid = tipc_stream_request->so_sndavoid;

  //  set_hostname_and_port(local_name,
  //                        port_buffer,
  //                        nf_to_af(tcp_stream_request->ipfamily),
  //                        tcp_stream_request->port);

  //  local_res = complete_addrinfo(local_name,
  //                                local_name,
  //                                port_buffer,
  //                                nf_to_af(tcp_stream_request->ipfamily),
  //                                SOCK_STREAM,
  //                                IPPROTO_TCP,
  //                                0);
  //
  //  s_listen = create_data_socket(local_res);




  /* Call to set_sock_buffer to set buffer size!!! */




  /* Set up the tipc socket, bind */
  memset(&myaddr_in, 0, sizeof(myaddr_in));
  myaddr_in.family = AF_TIPC;
  myaddr_in.addrtype = TIPC_ADDR_NAME;
  myaddr_in.addr.name.name.type = NETSERVER_TIPC_DEFAULT;
  myaddr_in.addr.name.name.instance = 0;
  myaddr_in.scope = TIPC_ZONE_SCOPE;  

  s_listen = socket(AF_TIPC, SOCK_STREAM, 0);

  if (bind(s_listen, 
	   (struct sockaddr *)&myaddr_in, 
	   sizeof(myaddr_in)) < 0) {
    perror("Netserver: failed to bind tipc port name\n");
    exit(1);
  }

  /* Get node name with getsockname */
  addrlen = sizeof(struct sockaddr_tipc);
  memset(&myaddr_in, 0, sizeof(myaddr_in));
  if (getsockname(s_listen, 
		  (struct sockaddr*)&myaddr_in, 
		  &addrlen) != 0) {
    perror("tipc: getsockname failed.");
    exit(1);
  }

  /* Netperf needs port_id of the tipc socket */ 
  tipc_stream_response->id = myaddr_in.addr.id;


  if (s_listen == INVALID_SOCKET) {
    netperf_response.content.serv_errno = errno;
    send_response();
    exit(1);
  }

  // set buffer sizes and other cool stuff
  set_sock_buffer (s_listen, SEND_BUFFER, lss_size_req, &lss_size);
  set_sock_buffer (s_listen, RECV_BUFFER, lsr_size_req, &lsr_size);


#ifdef WIN32
  /* The test timer can fire during operations on the listening socket,
     so to make the start_timer below work we have to move
     it to close s_listen while we are blocked on accept. */
  win_kludge_socket2 = s_listen;
#endif

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
  /* do on the sending side. this is more for the sake of symmetry */
  /* than for the needs of say copy avoidance, but it might also be */
  /* more realistic - this way one could conceivably go with a */
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


  /* now get the port number assigned by the system  */
  //  addrlen = sizeof(myaddr_in);
  //  if (getsockname(s_listen,
  //                  (struct sockaddr *)&myaddr_in,
  //                  &addrlen) == SOCKET_ERROR){
  //    netperf_response.content.serv_errno = errno;
  //    close(s_listen);
  //    send_response();
  //
  //    exit(1);
  //  }

  /* Now myaddr_in contains the port and the internet address this is */
  /* returned to the sender also implicitly telling the sender that the */
  /* socket buffer sizing has been done. */

  //  tcp_stream_response->data_port_number =
  //    (int) ntohs(((struct sockaddr_in *)&myaddr_in)->sin_port);
  //  netperf_response.content.serv_errno   = 0;

  /* But wait, there's more. If the initiator wanted cpu measurements, */
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
  tipc_stream_response->no_delay = loc_nodelay;
  tipc_stream_response->so_rcvavoid = loc_rcvavoid;
  tipc_stream_response->so_sndavoid = loc_sndavoid;
  tipc_stream_response->receive_size = recv_size;

  send_response();

  addrlen = sizeof(peeraddr_in);

  if ((s_data = accept(s_listen, 
		       (struct sockaddr *)&peeraddr_in, 
		       &addrlen)) == INVALID_SOCKET) {
    /* Let's just punt. The remote will be given some information */
    close(s_listen);
    exit(1);
  }

#ifdef WIN32
  /* this is used so the timer thread can close the socket out from */
  /* under us, which to date is the easiest/cleanest/least */
  /* Windows-specific way I can find to force the winsock calls to */
  /* return WSAEINTR with the test is over. anything that will run on */
  /* 95 and NT and is closer to what netperf expects from Unix signals */
  /* and such would be appreciated raj 1/96 */
  win_kludge_socket = s_data;
  win_kludge_socket2 = INVALID_SOCKET;
#endif /* WIN32 */

  times_up = 0;

  start_timer(tipc_stream_request->test_length + PAD_TIME);

#ifdef KLUDGE_SOCKET_OPTIONS
  /* this is for those systems which *INCORRECTLY* fail to pass */
  /* attributes across an accept() call. Including this goes against */
  /* my better judgement :( raj 11/95 */

  kludge_socket_options(s_data);

#endif /* KLUDGE_SOCKET_OPTIONS */

  /* Now it's time to start receiving data on the connection. We will */
  /* first grab the apropriate counters and then start grabbing. */

  cpu_start(tipc_stream_request->measure_cpu);

  /* The loop will exit when the sender does a shutdown, which will */
  /* return a length of zero   */

  /* there used to be an #ifdef DIRTY call to access_buffer() here,
     but we have switched from accessing the buffer before the recv()
     call to accessing the buffer after the recv() call.  The
     accessing before was, IIRC, related to having dirty data when
     doing page-flipping copy avoidance. */

  bytes_received = 0;
  receive_calls  = 0;

  while (!times_up && (len = recv(s_data, recv_ring->buffer_ptr, recv_size, 0) )) {
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
                  tcp_stream_request->dirty_count,
                  tcp_stream_request->clean_count);
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

  if (shutdown(s_data,SHUT_WR) == SOCKET_ERROR && !times_up) {
    netperf_response.content.serv_errno = errno;
    send_response();
    exit(1);
  }

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



