#ifndef NETTEST_TIPC_H
#define NETTEST_TIPC_H

#define NETSERVER_TIPC_TYPE	6118

/* Used by netperf.c */
void send_tipc_stream(char remote_host[]);

/* Used by nettest_omni.c */
void print_top_tipc_test_header(char test_name[], uint32_t node, uint32_t ref);
SOCKET create_tipc_socket();
void sockaddr_from_id(uint32_t node, uint32_t ref, struct sockaddr_storage *sa);
void sockaddr_from_type_inst(uint32_t type, uint32_t instance, struct sockaddr_storage *sa);
void get_portid(SOCKET s, uint32_t *node, uint32_t *ref);
void get_tipc_addrinfo(struct addrinfo **addr, struct sockaddr_storage *sa);

#endif
