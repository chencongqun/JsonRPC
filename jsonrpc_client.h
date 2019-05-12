// file : jsonrpc_client.h
// auth : lagula
// date : 2012-6-7
// desc : implement jsonrpc.serverProxy
//

#ifndef JSONRPC_CLIENT_H
#define JSONRPC_CLIENT_H

#include <json/json.h>
#include <stdbool.h>
#include "block.h"

typedef struct jsonrpc_client_t jsonrpc_client_t;

struct jsonrpc_client_t
{
    bool b_error;
    int i_sock_type;
    int sock;
    char *psz_protocol;

    // unix sock
    char *psz_unix_conn_file;

    // tcp sock
    char *psz_tcp_name;
    int         i_tcp_port;

    // notify
    char **ppsz_notifyService;
    int    i_notifyService;

    block_t *p_buf;             // used for cache

    struct json_object* (*pf_call) ( jsonrpc_client_t *p_this,
                                     const char *psz_mothod, struct json_object* p_params );
    int                 (*pf_notify) ( jsonrpc_client_t *p_this,
                                       const char *psz_mothod, struct json_object* p_params );
    struct json_object* (*pf_get_notify) ( jsonrpc_client_t *p_this,
                                           bool b_block, int i_timeout );
    void (*pf_exit) ( jsonrpc_client_t *p_this );
    // user can overwrite these
    void (*pf_on_reconnected) ( jsonrpc_client_t *p_this );
};

/*
 * if i_sock_flag is AF_UNIX or PF_UNIX, the following params are
 *      const char* psz_conn_file
 * else if AF_INET or PF_INET, the following params are
 *      const char* psz_server_name, int i_port
 */
int jsonrpc_client_init( jsonrpc_client_t *p_this, int i_sock_flag, ... );

/*
 * if i_sock_flag is AF_UNIX or PF_UNIX, the following params are
 *      const char* psz_conn_file,
 *      const char **ppsz_notifyService, int i_notifyService.
 * else if AF_INET or PF_INET, the following params are
 *      const char* psz_server_name, int i_port,
 *      const char **ppsz_notifyService, int i_notifyService.
 */
int jsonrpc_client_subscribe_init( jsonrpc_client_t *p_this,
                                   int i_sock_flag, ... );

#endif

