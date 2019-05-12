/*
  *  Author: ccq
  *  Version: 1.0
  *  Date: 2015-06-15
  *
  *  Description: Implement the interfaces for RPC communication by json
  *
  *  Copyright @ 2015 SinoData. All rights reserved.
  */

#include <json/json.h>
#include "hashmap.h"
#include "block.h"
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/un.h>
#include <stdarg.h>
#include <sys/epoll.h>
#include <errno.h>
#include <stdbool.h>
#include <arpa/inet.h>
#include "http_parser.h"


#define LISTEN_BACKLOG 5

#define EPOLL_SIZE 1024
#define EPOLL_MAX_EVENT 64
#define EPOLL_TIMEOUT 50

#define MAX_REQUEST_LEN 100000000

#define JSON_SERVER_LOGDBUG(...) printf(...)
#define JSON_SERVER_LOGERROR(...) printf(...)
#define JSON_SERVER_LOGWARNING(...) printf(...)



typedef enum PACKAGE_TYPE_E
{
	TCP,
	HTTP
}PACKAGE_TYPE;

typedef void (*pf_operator)( struct json_object *p_params, struct json_object *p_response );

typedef struct json_server_t
{
	int tcpsock;
	int unixsock;
	PACKAGE_TYPE type;
	short s_exit;
	hashmap hm_operators;
	hashmap hm_requests;

	int (*pf_create_connection)( struct json_server_t *p_this, int i_sock_flag, ... );
	int (*pf_register_operator)( struct json_server_t *p_this, const char *psz_method_name, pf_operator p_fn );
	int (*pf_set_muti_worker) ( struct json_server_t *p_this, int i_num_worker );
	int (*pf_main_loop)(struct json_server_t *p_this);
	void (*pf_exit)(struct json_server_t *p_this);	
	void (*pf_destory)(struct json_server_t *p_this);	
}json_server;

typedef struct json_request_t
{
	int i_client_fd;
	char psz_ip[16];
	block_t *p_response_block;
	block_t *p_request_block;
	struct json_object * (*pf_get_json_object)( block_t *p_request_block );
}json_request;

json_server *json_server_new();




