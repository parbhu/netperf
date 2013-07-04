#ifndef TIPC_H
#define TIPC_H

#define NETSERVER_TIPC_DEFAULT 6118

/* Test specific definitions for netperf's tipc socket tests. */

struct tipc_stream_request_struct {
  int send_buf_size;
  int recv_buf_size;    /* how big does the client want it - the */
                        /* receive socket buffer that is */
  int receive_size;     /* how many bytes do we want to receive at one */
                        /* time? */
  int   recv_alignment; /* what is the alignment of the receive */
                        /* buffer? */
  int   recv_offset;    /* and at what offset from that alignment? */
  int   no_delay;       /* do we disable the nagle algorithm for send */
                        /* coalescing? */
  int   measure_cpu;    /* does the client want server cpu utilization */
                        /* measured? */
  float cpu_rate;       /* do we know how fast the cpu is already? */
  int   test_length;    /* how long is the test?                */
  int   so_rcvavoid;    /* do we want the remote to avoid copies on */
                        /* receives? */
  int   so_sndavoid;    /* do we want the remote to avoid send copies? */
  int   dirty_count;    /* how many integers in the receive buffer */
                        /* should be made dirty before calling recv? */
  int   clean_count;    /* how many integers should be read from the */
                        /* recv buffer before calling recv? */
  int   port;           /* the port to which the recv side should bind
                           to allow netperf to run through those evil
                           firewall things */
  //int   ipfamily;       /* the address family of ipaddress */

};

struct tipc_stream_response_struct {
  struct tipc_portid id;
  int   recv_buf_size;    /* how big does the client want it      */
  //int   receive_size;
  int   no_delay;
  int   measure_cpu;      /* does the client want server cpu      */
  //int   test_length;    /* how long is the test?                */
  int   send_buf_size;
  //int   data_port_number;     /* connect to me here   */
  float cpu_rate;               /* could we measure     */
  int   so_rcvavoid;      /* could the remote avoid receive copies? */
  int   so_sndavoid;      /* could the remote avoid send copies? */
};

struct tipc_stream_results_struct {
  double         bytes_received;
  unsigned int   recv_calls;
  float          elapsed_time;  /* how long the test ran */
  float          cpu_util;      /* -1 if not measured */
  //float          serv_dem;      /* -1 if not measured */
  int            cpu_method;    /* how was cpu util measured? */
  int            num_cpus;      /* how many CPUs had the remote? */
  //int            recv_buf_size; /* how large was it at the end? */
  //int            send_buf_size; /* how large was it at the end? */
};

struct tipc_rr_request_struct {
};

struct tipc_rr_response_struct {
};

struct tipc_rr_result_struct {
};

#endif
