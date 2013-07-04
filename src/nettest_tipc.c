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
  struct        tipc_stream_results_struct       *tipc_stream_result;

  tipc_stream_request  =
    (struct tipc_stream_request_struct *)netperf_request.content.test_specific_data;
  tipc_stream_response =
    (struct tipc_stream_response_struct *)netperf_response.content.test_specific_data;
  tipc_stream_result   =
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
  //complete_addrinfos(&remote_res,
  //                   &local_res,
  //                   remote_host,
  //                   SOCK_STREAM,
  //                   IPPROTO_TCP,
  //                   0);

  //if ( print_headers ) {
  //  print_top_test_header("TCP STREAM TEST",local_res,remote_res);
  //}

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

    /*set up the data socket                        */
    //send_socket = create_data_socket(local_res);

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

    /* If the user has requested cpu utilization measurements, we must */
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

#ifdef WANT_DEMO
    demo_stream_setup(lss_size,rsr_size);
#endif

    /* Use received port id to connect to the remote tipc port */
    memset(&remote_addr, 0, sizeof(remote_addr));
    remote_addr.family = AF_TIPC;
    remote_addr.addrtype = TIPC_ADDR_ID;
    remote_addr.addr.id = remote_port_id;
    remote_addr.scope = TIPC_ZONE_SCOPE;

    send_socket = socket (AF_TIPC, SOCK_SEQPACKET, 0);

    if (send_socket == INVALID_SOCKET){
      perror("netperf: send_tipc_stream: tipc stream data socket");
      exit(1);
    }

    if (debug) {
      fprintf(where,"send_tipc_stream: send_socket obtained...\n");
    }

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

    printf("Waiting for receive.\n");
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

      printf("Waiting for second receive\n");
      recv_response();
      if (!netperf_response.content.serv_errno) {
        if (debug)
          fprintf(where,
                  "remote reporting results for %.2f seconds\n",
                  tipc_stream_result->elapsed_time);
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

      bytes_sent        = ntohd(tipc_stream_result->bytes_received);
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

        remote_cpu_utilization  = tipc_stream_result->cpu_util;
        remote_service_demand   = calc_service_demand(bytes_sent,
                                                      0.0,
                                                      remote_cpu_utilization,
                                                      tipc_stream_result->num_cpus);
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
    remote_cpu_method = format_cpu_method(tipc_stream_result->cpu_method);

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
            bytes_sent / (double)tipc_stream_result->recv_calls,
            tipc_stream_result->recv_calls);
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



