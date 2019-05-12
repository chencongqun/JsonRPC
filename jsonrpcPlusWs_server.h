// file : jsonrpcPlusWs_server.h
// auth : lagula
// date : 2012-7-04
// desc : implement a jsonrpc server + websocket jsonrpc server
//

#ifndef JSONRPCPLUSWS_SERVER_H
#define JSONRPCPLUSWS_SERVER_H

#include "jsonrpc_server.h"

struct JsonrpcPlusWs_server_t
{
    struct jsonrpc_server_t self;

    struct jsonrpc_server_t jsonBase;
    struct ws_jsonrpc_server_t wsBase;
};

typedef struct JsonrpcPlusWs_server_t JsonrpcPlusWs_server_t;

int JsonrpcPlusWs_server_init( JsonrpcPlusWs_server_t *p_server );

#endif

