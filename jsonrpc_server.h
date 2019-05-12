// file : jsonrpc_server.h
// auth : lagula
// date : 2012-6-7
// desc : implement jsonrpc.Server
//

#ifndef JSONRPC_SERVER_H
#define JSONRPC_SERVER_H

#include <stdbool.h>
#include <json/json.h>
#include <pthread.h>
#include "hashmap.h"
#include "socket.h"
#include "block.h"

typedef struct jsonrpc_server_t jsonrpc_server_t;
typedef struct jsonrpc_request_t jsonrpc_request_t;
typedef struct ws_jsonrpc_server_t ws_jsonrpc_server_t;
typedef struct JsonrpcPlusWs_server_t JsonrpcPlusWs_server_t;

enum conn_state
{
    CONN_CLOSED,
    CONN_CONNECTED,
    CONN_HANDSHAKED,
};

struct jsonrpc_request_t
{
    int  i_sockfd;
    char psz_ip[16];
    block_t *p_req;
    block_t *p_res;
    // process_write and notify_dispatch
    int     i_state;
    char *psz_protocol;
    char **ppsz_notify_service;
    int  i_notify_service;
    struct jsonrpc_request_t *p_next;       // used for notifyServiceMap
};







typedef void (*pf_rpc_callback_t) ( struct json_object *p_params,
                                    struct json_object *p_response );
typedef void (*pf_rpc_member_callback_t) ( void *p_classobj,
        struct json_object *p_params,
        struct json_object *p_response );

struct jsonrpc_server_t
{
    bool      b_initialized;
    hashmap   hashmap;                  // store functions
    hashmap   classmap;                 // store class object
    int       tcpsock;
    int       unixsock;
    char *psz_bind_file;

    // notify
    char **ppsz_supportedNotifyService;
    int    i_supportedNotifyService;
    hashmap   notifyServiceMap;         // key is notify service,
    // value is request list

    int (*pf_register_function) ( jsonrpc_server_t *p_this,
                                  const char *psz_method, pf_rpc_callback_t pf );
    int (*pf_register_class_object) ( jsonrpc_server_t *p_this,
                                      const char *psz_class, void *p_obj );
    int (*pf_register_member_function) ( jsonrpc_server_t *p_this,
                                         const char *psz_method, pf_rpc_member_callback_t pmf);
    int (*pf_register_notify_services) ( jsonrpc_server_t *p_this,
                                         const char **ppsz_notify_service,
                                         int i_notify_service );
    int (*pf_serve) ( jsonrpc_server_t *p_this );
    int (*pf_exit)  ( jsonrpc_server_t *p_this );
    // user can overwrite these
    bool (*pf_request_IsComplete) ( jsonrpc_server_t *p_this, block_t *p_req,
                                    size_t *pi_len );
    int  (*pf_handle_handshake) ( jsonrpc_server_t *p_this,
                                  jsonrpc_request_t *p_request );
    int  (*pf_handle_request)   ( jsonrpc_server_t *p_this,
                                  block_t *p_req, block_t *p_res );
    void (*pf_get_request)      ( jsonrpc_server_t *p_this,
                                  jsonrpc_request_t *p_request );
    void (*pf_on_client_connected) ( jsonrpc_server_t *p_this, int sockfd );
    void (*pf_on_client_closed) ( jsonrpc_server_t *p_this, int sockfd );
    void (*pf_on_processed) ( jsonrpc_server_t *p_this );
    int  (*pf_notify_dispatch) ( jsonrpc_server_t *p_this,
                                 const char *psz_notify_service,
                                 struct json_object *p_notify );
};

// websocket jsonrpc server
struct ws_jsonrpc_server_t
{
    struct jsonrpc_server_t self;

    struct jsonrpc_server_t base;
};



// server that support both tcp jsonrpc and websocket jsonrpc
struct JsonrpcPlusWs_server_t
{
    struct jsonrpc_server_t self;

    struct jsonrpc_server_t jsonBase;
    struct ws_jsonrpc_server_t wsBase;
};




// notify_dispatch functions can use this to send notify
int jsonrpc_request_sendResponse( jsonrpc_request_t *p_request,
                                  block_t *p_block );

/* only support TCP and UNIX now,
 * if use TCP, set i_sock_flag as AF_INET or PF_INET, follows ip and port.
 * if use UNIX, set i_sock_flag as AF_UNIX or PF_UNIX, follows file name.
 */
int jsonrpc_server_init( jsonrpc_server_t *p_obj );
int jsonrpc_server_addListener( jsonrpc_server_t *, int i_sock_flag, ... );

int ws_jsonrpc_server_init( ws_jsonrpc_server_t *p_this );

int JsonrpcPlusWs_server_init( JsonrpcPlusWs_server_t *p_server );

#endif

