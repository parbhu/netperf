#ifndef TIPC_H
#define TIPC_H

#define NETSERVER_TIPC_DEFAULT 6118

/* Test specific definitions for netperf's tipc socket tests. */


/* Following routines are defined in nettest_tipc.c */

/* Used in netsh.c */
extern void scan_tipc_args(int argc, char *argv[]);

/* Used in netperf.c */
extern void send_tipc_stream(char remote_host[]);
extern void send_tipc_rr(char remote_host[]);

/* Used in netserver.c */
extern void recv_tipc_stream();
extern void recv_tipc_rr();

/* Used in nettest_omni.c */
extern void print_top_tipc_test_header(char test_name[], struct tipc_portid remote_port);
extern SOCKET create_tipc_socket();

#endif
