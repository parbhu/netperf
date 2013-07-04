#ifndef TIPC_H
#define TIPC_H

#define NETSERVER_TIPC_DEFAULT 6118

/* Test specific definitions for netperf's tipc socket tests. */

struct tipc_stream_request_struct {
};

struct tipc_stream_response_struct {
  struct tipc_portid id;
};

struct tipc_stream_result_struct {
};

struct tipc_rr_request_struct {
};

struct tipc_rr_response_struct {
};

struct tipc_rr_result_struct {
};

#endif
