// file : jsonrpcPlusWs_server.c
// auth : lagula
// date : 2012-7-04
// desc : implement a jsonrpc server + websocket jsonrpc server
//

#include "jsonrpc_server.h"

static bool JsonOrWs_request_IsComplete( jsonrpc_server_t *p_server,
        block_t *p_req, size_t *pi_len );
static int handle_JsonOrWs_request( jsonrpc_server_t *p_server,
                                    block_t *p_req, block_t *p_res );

int JsonrpcPlusWs_server_init( JsonrpcPlusWs_server_t *p_server )
{
    jsonrpc_server_t *p_this = (jsonrpc_server_t *)p_server;
    jsonrpc_server_init( p_this );

    p_this->pf_request_IsComplete = JsonOrWs_request_IsComplete;
    p_this->pf_handle_request = handle_JsonOrWs_request;

    jsonrpc_server_init( &p_server->jsonBase );
    ws_jsonrpc_server_init( &p_server->wsBase );
    return 0;
}


static bool IsJsonRequest( block_t *p_req );


static bool JsonOrWs_request_IsComplete( jsonrpc_server_t *p_this,
        block_t *p_req, size_t *pi_len )
{
    JsonrpcPlusWs_server_t *p_server = (JsonrpcPlusWs_server_t *)p_this;

    if ( IsJsonRequest( p_req ) )
        return p_server->jsonBase.pf_request_IsComplete( p_this, p_req, pi_len);
    else
        // deam the request is ws request
        return p_server->wsBase.self.pf_request_IsComplete( p_this, p_req, pi_len );
}

static int handle_JsonOrWs_request( jsonrpc_server_t *p_this,
                                    block_t *p_req, block_t *p_res )
{
    JsonrpcPlusWs_server_t *p_server = (JsonrpcPlusWs_server_t *)p_this;
    if ( IsJsonRequest( p_req ) )
        return p_server->jsonBase.pf_handle_request( p_this, p_req, p_res );
    else
        // deam the request is ws request
        return p_server->wsBase.self.pf_handle_request( p_this, p_req, p_res );
}

static bool IsJsonRequest( block_t *p_req )
{
    if ( p_req->p_buffer[0] == '{' ||
         ( p_req->p_buffer[0] == '\n' && p_req->p_buffer[1] == '{' ) )
        return true;
    else
        return false;
}

/* TODO: may need this later
int IsWsRequest( block_t *p_req, bool *pb_ws )
{
    char *psz_request = (char*)p_req->p_buffer;

    if ( p_req->i_buffer < 7 )
        return -1;
    else if ( !strncmp( psz_request, "GET", 3 ) )
    {
        if ( !strstr( psz_request, "\r\n\r\n" ) )
            return -1;
        char *psz_upgrade = strcasestr( psz_request, "\r\nUpgrade:" );
        char *psz_conn = strcasestr( psz_request, "\r\nConnection:" );
        if ( !psz_upgrade || !psz_conn )
            return false;

        char *psz_start, *psz_end;
        psz_start = psz_upgrade + strlen("\r\nUpgrade:");
        while ( isspace( *psz_start ) )
            psz_start++;
        psz_end = strstr( psz_start, "\r\n" );
        if ( strncasecmp( psz_start, "websocket", 9 ) )
            return false;

        psz_start = psz_conn + strlen("\r\nConnection:");
        while ( isspace( *psz_start ) )
            psz_start++;
        psz_end = strstr( psz_start, "\r\n" );
        if ( strncasecmp( psz_start, "Upgrade", 7 ) )
            return false;

        return true;
    }
    else if ( p_req->p_buffer[1] & 0x80 )
    {
        // TODO: this is very vulnerable, may use better way later
        return true;
    }
    else
        return false;
}
*/

