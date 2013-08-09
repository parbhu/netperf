/* Test specific definitions for netperf's tipc socket tests. */

#ifndef TIPC_H
#define TIPC_H

#define NETSERVER_TIPC_DEFAULT 6118


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
extern void sockaddr_from_id(struct tipc_portid portid, struct sockaddr_tipc *sa);
extern void sockaddr_from_type_inst(unsigned int type, unsigned int instance, struct sockaddr_tipc *sa);
extern void get_portid(SOCKET s, struct sockaddr_tipc *sa, struct tipc_portid *portid);

#endif
