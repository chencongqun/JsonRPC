// file : ws_jsonrpc_server.h
// auth : lagula
// date : 2012-7-04
// desc : implement a websocket jsonrpc server
//

#ifndef WS_JSONRPC_SERVER_H
#define WS_JSONRPC_SERVER_H

struct ws_jsonrpc_server_t
{
    struct jsonrpc_server_t self;

    struct jsonrpc_server_t base;
};

typedef struct ws_jsonrpc_server_t ws_jsonrpc_server_t;


int ws_jsonrpc_server_init( ws_jsonrpc_server_t *p_this );


#endif
