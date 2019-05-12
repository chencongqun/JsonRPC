// file : ws_jsonrpc_server.c
// auth : lagula
// date : 2012-7-03
// desc : implement a websocket jsonrpc server,
//

#define WS_NORMAL_CLOSE        (1000)
#define WS_PROTOCOL_ERROR      (1002)
#define WS_UNACCEPT_DATATYPE   (1003)
#define WS_UNCONSISTANT_DATATYPE (1007)
#define WS_MSG_TOOBIG           (1009)
#define WS_SERVER_ERROR         (1011)

#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <assert.h>
#include <openssl/sha.h>
#include "jsonrpc_server.h"
#include "jsonrpc_utils.h"
#include "block.h"
#include "utils.h"
#include "log.h"

static const char WS_SUBPROTOCOLS[][20] = { "json", "notify" };
static const char WS_MAGICSTRING[] = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";


static bool ws_request_IsComplete( jsonrpc_server_t *p_server, block_t *p_req,
                                   size_t *pi_len );
static int ws_handle_request( jsonrpc_server_t *p_server, block_t *p_req,
                              block_t *p_res );
static int ws_handle_handshake( jsonrpc_server_t *p_server,
                                jsonrpc_request_t *p_request );
static int ws_jsonrpc_server_exit( jsonrpc_server_t *p_server );
static int ws_notify_dispatch( jsonrpc_server_t *p_this,
                               const char *psz_notify_service,
                               struct json_object *p_notify );

int ws_jsonrpc_server_init( ws_jsonrpc_server_t *p_server )
{
    jsonrpc_server_t *p_this = (jsonrpc_server_t *)p_server;
    jsonrpc_server_init( p_this );

    p_this->pf_request_IsComplete = ws_request_IsComplete;
    p_this->pf_handle_request = ws_handle_request;
    p_this->pf_handle_handshake = ws_handle_handshake;
    p_this->pf_notify_dispatch  = ws_notify_dispatch;
    p_this->pf_exit = ws_jsonrpc_server_exit;

    jsonrpc_server_init( &p_server->base );
    return 0;
}

static int ws_jsonrpc_server_exit( jsonrpc_server_t *p_this )
{
    ws_jsonrpc_server_t *p_server = (ws_jsonrpc_server_t *)p_this;
    p_server->base.pf_exit( &p_server->base );
    p_server->base.pf_exit( &p_server->self );
    return 0;
}

static bool InSupportedProtocols( const char *psz_subprotocol );
static void add_ws_response_header( block_t *p_res );
static block_t *concatenate_fragments( block_t *p_req,
                                       block_t *p_res );
static int handle_ws_payload( jsonrpc_server_t *p_server, block_t *p_req,
                              block_t *p_res );
static void close_ws_conn( block_t *p_res );
static void fail_ws_conn( block_t *p_res, uint16_t i_code, const char *psz_msg );

static int get_ws_header_len( uint8_t *p_req, int i_req )
{
    if ( i_req < 2 )
        return -1;
    int i_payload = p_req[1] & 0x7f;
    if ( i_payload == 126 )
        return 4;
    else if ( i_payload == 127 )
        return 10;
    else
        return 2;
}

static int get_ws_payload_len( uint8_t *p_req, int i_req, uint64_t *pi_len )
{
    if ( i_req < 2 )
        return -1;
    uint64_t i_payload = p_req[1] & 0x7f;
    if ( i_payload == 126 )
    {
        if ( i_req < 4 )
            return -1;
        i_payload = ntohs( *(uint16_t*)&p_req[2] );
    }
    else if ( i_payload == 127 )
    {
        if ( i_req < 10 )
            return -1;
        i_payload = ntohll( *(uint64_t*)&p_req[2] );
    }
    *pi_len = i_payload;
    return 0;
}

static bool ws_request_IsComplete( jsonrpc_server_t *p_server, block_t *p_req,
                                   size_t *pi_len )
{
    if ( p_req->i_buffer >= MAX_REQUEST_LEN )
    {
        log_Err( "ws request is big than %d", MAX_REQUEST_LEN );
        return true;
    }

    char *psz_request = (char*)p_req->p_buffer;

    if ( p_req->i_buffer < 7 )
        return false;
    else if ( !strncmp( psz_request, "GET", 3 ) )
    {
        if ( !strstr( psz_request, "\r\n\r\n" ) )
            return false;
        else
            *pi_len = (int)(strstr(psz_request, "\r\n\r\n") - psz_request) + 4;
    }
    else if ( p_req->p_buffer[1] & 0x80 )
    {
        // it's ws client data
        uint8_t *p_ptr = p_req->p_buffer;
        int i_ptr = p_req->i_buffer;

        if ( !(p_req->p_buffer[0] & 0x80) )     // unfinished
        {
            // fragments
            // point p_ptr to the last fin frame
            uint64_t i_payload;
            int      i_header;
            while ( !(p_ptr[0] & 0x80) )
            {
                i_header = get_ws_header_len( p_ptr, i_ptr );
                if ( i_header < 0 )
                    return false;
                if ( get_ws_payload_len( p_ptr, i_ptr, &i_payload ) < 0 )
                    return false;
                if ( i_header + 4 + i_payload > i_ptr )
                    return false;
                p_ptr += i_header + 4 + i_payload;
                i_ptr -= i_header + 4 + i_payload;
            }
            *pi_len = p_req->i_buffer - i_ptr;
        }
        else
        {
            // finished data
            uint64_t i_payload;
            int i_header = get_ws_header_len( p_ptr, i_ptr );
            if ( i_header < 0 )
                return false;
            if ( get_ws_payload_len( p_ptr, i_ptr, &i_payload ) < 0 )
                return false;
            if ( i_header + 4 + i_payload > i_ptr )
                return false;
            *pi_len = i_header + 4 + i_payload;
        }
    }

    return true;
}

static int ws_handle_handshake( jsonrpc_server_t *p_server,
                                jsonrpc_request_t *p_request )
{
    block_t *p_res = p_request->p_res;
    const char *psz_request = (char*)p_request->p_req->p_buffer;

    if ( strncmp( psz_request, "GET", 3 ) )
    {
        log_Warn( "ws handshake request format error, GET not found" );
        return -1;
    }

    bool b_isNotifyProtocol = false;
    char *psz_subprotocol = NULL;
    uint8_t *p_key_buf = NULL;
    char *psz_reskey = NULL;

    // TODO: check Host and Origin
    char *psz_start, *psz_end;
    char **ppsz_fields = NULL;
    int i_fields;

    psz_start = strcasestr( psz_request, "Sec-WebSocket-Protocol:" );
    if ( psz_start )
    {
        psz_start += strlen("Sec-WebSocket-Protocol:");
        while ( isspace( *psz_start ) )
            psz_start++;
        psz_end = strstr( psz_start, "\r\n" );
        char *psz_protocols = strndup( psz_start,
                                       (size_t)(psz_end - psz_start) );
        if ( !psz_protocols )
        {
            log_Err( "no memory" );
            return JSONRPC_ERR_NOMEM;
        }
        split( psz_protocols, ", ", 0, &ppsz_fields, &i_fields, true );
        free( psz_protocols );
        int i;
        for ( i = 0; i < i_fields; i++ )
        {
            if ( InSupportedProtocols( ppsz_fields[i] ) )
            {
                psz_subprotocol = strdup( ppsz_fields[i] );
                if ( !psz_subprotocol )
                {
                    log_Err( "no memory" );
                    return JSONRPC_ERR_NOMEM;
                }
                if ( !strcasecmp( psz_subprotocol, "notify" ) )
                    b_isNotifyProtocol = true;
                break;
            }
        }
        if ( i == i_fields )
        {
            int i_subpro_len = sizeof(WS_SUBPROTOCOLS) / \
                               sizeof(WS_SUBPROTOCOLS[0]);
            psz_subprotocol = malloc( sizeof(WS_SUBPROTOCOLS) + \
                                      2 * i_subpro_len );
            if ( !psz_subprotocol )
            {
                log_Err( "no memory" );
                return JSONRPC_ERR_NOMEM;
            }
            psz_subprotocol[0] = '\0';
            for ( int j = 0; j < i_subpro_len - 1; j++ )
            {
                strcat( psz_subprotocol, WS_SUBPROTOCOLS[j] );
                strcat( psz_subprotocol, ", " );
            }
            strcat( psz_subprotocol, WS_SUBPROTOCOLS[ i_subpro_len - 1 ] );
        }
        for ( int i = 0; i < i_fields; i++ )
            free( ppsz_fields[i] );
        free( ppsz_fields );
    }
    else
    {
        psz_subprotocol = strdup( WS_SUBPROTOCOLS[0] );
        if ( !psz_subprotocol )
        {
            log_Err( "no memory" );
            return JSONRPC_ERR_NOMEM;
        }
    }

    if ( b_isNotifyProtocol )
    {
        // get notify service names from uri in status line, the status line
        // is in the form:
        // GET /notify1+notify2?xxxx HTTP/1.1
        psz_start = strstr( psz_request, "\r\n" );
        psz_start = strrchr( psz_start, '/' );
        psz_start = strrchr( psz_start, '/' );
        if ( !psz_start )
            psz_start = strchr( psz_request, ' ' );
        psz_start++;
        psz_end = strchr( psz_start, '?' );
        if ( !psz_end )
            psz_end = strchr( psz_start, ' ' );
        char *psz_notifies = strndup( psz_start,
                                      (size_t)(psz_end - psz_start) );
        if ( !psz_notifies )
        {
            log_Err( "no memory" );
            return JSONRPC_ERR_NOMEM;
        }
        split( psz_notifies, "+", 0, &ppsz_fields, &i_fields, true );
        free( psz_notifies );
        // check notify services
        for ( int i = 0; i < i_fields; i++ )
        {
            int j;
            for ( j = 0; j < p_server->i_supportedNotifyService; j++ )
                if ( !strcmp( ppsz_fields[i],
                              p_server->ppsz_supportedNotifyService[j] ) )
                    break;
            if ( j == p_server->i_supportedNotifyService )
            {
                log_Err( "notify service %s is unknown, refuse ws connection",
                         ppsz_fields[i] );
                for ( int k = 0; k < i_fields; k++ )
                    free( ppsz_fields[k] );
                free( ppsz_fields );
                return -1;
            }
        }
        // add to notifyServiceMap
        p_request->ppsz_notify_service = ppsz_fields;
        p_request->i_notify_service = i_fields;
        for ( int i = 0; i < i_fields; i++ )
        {
            hashmap_key_t key;
            key.type = 'c';
            key.u.psz_string = ppsz_fields[i];
            jsonrpc_request_t *p_headreq =
                hashmap_get( p_server->notifyServiceMap, key );
            p_request->p_next = p_headreq;
            hashmap_put( p_server->notifyServiceMap, key, p_request );
        }
    }

    psz_start = strcasestr( psz_request, "Sec-WebSocket-Version:" );
    if ( !psz_start )
    {
        fail_ws_conn( p_res, WS_PROTOCOL_ERROR, "missing version" );
        return -1;
    }
    else
    {
        psz_start += strlen("Sec-WebSocket-Version:");
        while ( isspace( *psz_start ) )
            psz_start++;
        if ( strncmp( psz_start, "13", 2 ) )
        {
            fail_ws_conn( p_res, WS_PROTOCOL_ERROR,
                          "support only version 13" );
            return -1;
        }
    }

    psz_start = strcasestr( psz_request, "Sec-WebSocket-Key:" );
    if ( !psz_start )
    {
        fail_ws_conn( p_res, WS_PROTOCOL_ERROR, "missing key" );
        return -1;
    }
    else
    {
        psz_start += strlen("Sec-WebSocket-Key:");
        while ( isspace( *psz_start ) )
            psz_start++;
        psz_end = strstr( psz_start, "\r\n" );
        int i_key = (size_t)(psz_end - psz_start);
        p_key_buf = malloc( i_key + strlen(WS_MAGICSTRING) );
        if ( !p_key_buf )
        {
            log_Err( "no memory" );
            return JSONRPC_ERR_NOMEM;
        }
        memcpy( p_key_buf, psz_start, i_key );
        memcpy( p_key_buf + i_key, WS_MAGICSTRING, strlen(WS_MAGICSTRING));
        int i_buf = i_key + strlen(WS_MAGICSTRING);

        uint8_t psz_sha1[20];
        SHA1( p_key_buf, i_buf, psz_sha1 );
        psz_reskey = b64_encode( psz_sha1, 20 );
        if ( !psz_reskey )
        {
            log_Err( "no memory" );
            return JSONRPC_ERR_NOMEM;
        }
        free( p_key_buf );
    }

    char psz_result[4096] = {0};
    snprintf( psz_result, sizeof(psz_result) - 1,
              "HTTP/1.1 101 Switching Protocols\r\n"
              "Upgrade: websocket\r\n"
              "Connection: Upgrade\r\n"
              "Sec-WebSocket-Accept: %s\r\n"
              "Sec-WebSocket-Protocol: %s\r\n"
              "\r\n",
              psz_reskey, psz_subprotocol
            );
    memcpy( p_res->p_buffer, psz_result, strlen(psz_result) );
    p_res->i_buffer = strlen( psz_result );
    free( psz_subprotocol );
    free( psz_reskey );
    return 0;
}

static int ws_handle_request( jsonrpc_server_t *p_this,
                              block_t *p_req, block_t *p_res )
{
    ws_jsonrpc_server_t *p_server = (ws_jsonrpc_server_t *)p_this;
    assert( p_res->i_buffer == 0 );
    assert( p_res->i_maxlen >= 4096 );

    if ( p_req->p_buffer[1] & 0x80 )
    {
        // client mark bit

        if ( p_req->i_buffer >= MAX_REQUEST_LEN )
        {
            fail_ws_conn( p_res, WS_MSG_TOOBIG, "msg is too big" );
            return -1;
        }

        if ( !(p_req->p_buffer[0] & 0x80) )
        {
            // fragment
            block_t *p_json_req = concatenate_fragments( p_req, p_res );
            if ( !p_json_req )
            {
                fail_ws_conn( p_res, WS_SERVER_ERROR, "server error, "
                              "concatenate_fragments failed" );
                return -1;
            }
            // call base handle_request
            int ret = p_server->base.pf_handle_request( p_this, p_json_req,
                      p_res );
            add_ws_response_header( p_res );
            block_Release( p_json_req );
            return ret;
        }

        assert( p_req->p_buffer[0] & 0x80 ); // fin
        int i_opcode = p_req->p_buffer[0] & 0x0f;
        // fragments are handled previously
        assert( i_opcode != 0x0 );
        if ( i_opcode == 0x8 )
        {
            // close ws connection request
            close_ws_conn( p_res );
            return 0;
        }
        else if ( i_opcode == 0x2 )
        {
            // binary data type
            fail_ws_conn( p_res, WS_UNACCEPT_DATATYPE,
                          "unaccept binary data" );
            return -1;
        }
        else if ( i_opcode == 0x1 )
        {
            return handle_ws_payload( p_this, p_req, p_res );
        }
        else
        {
            fail_ws_conn( p_res, WS_UNACCEPT_DATATYPE,
                          "unaccept ping pong or other data type" );
            return -1;
        }
    }
    else
    {
        log_Err( "received unkonwn ws request" );
        return -1;
    }
}

static int ws_notify_dispatch( jsonrpc_server_t *p_this,
                               const char *psz_notify_service,
                               struct json_object *p_notify )
{
    const char *psz_notify = json_object_to_json_string( p_notify );
    uint32_t i_notify = strlen( psz_notify ) + 1;
    int i_header = i_notify <= 125 ? 2 : \
                   (i_notify <= 65535 ? 4 : 10);

    hashmap_key_t key;
    key.type = 'c';
    key.u.psz_string = psz_notify_service;
    jsonrpc_request_t *p_head = hashmap_get( p_this->notifyServiceMap, key );
    if ( !p_head )
    {
        log_Err( "can not find notify service %s in notifyServiceMap",
                 psz_notify_service );
        return -1;
    }
    // send msg for all requests in request list
    while ( p_head != NULL )
    {
        block_t *p_block = block_Alloc( i_header + i_notify );
        if ( !p_block )
        {
            log_Err( "no memory" );
            return -1;
        }
        uint8_t *p_ptr = p_block->p_buffer;
        p_ptr[0] = 0x81;        // fin and text type
        p_ptr[1] = (i_header == 2) ? i_notify : \
                   ((i_header == 4) ? 126 : 127);
        if ( i_header == 4 )
            *(uint16_t*)(p_ptr + 2) = htons( (uint16_t)i_notify );
        else if ( i_header == 10 )
            *(uint64_t*)(p_ptr + 2) = htonll( (uint64_t)i_notify );
        memcpy( p_ptr + i_header, psz_notify, i_notify );
        p_block->i_buffer += (i_header + i_notify);
        jsonrpc_request_sendResponse( p_head, p_block );
        block_Release( p_block );

        p_head = p_head->p_next;
    }
    return 0;
}

// TODO: shutdown in close_ws_conn?

static bool InSupportedProtocols( const char *psz_subprotocol )
{
    int i_pro_len = sizeof(WS_SUBPROTOCOLS) / sizeof(WS_SUBPROTOCOLS[0]);
    for ( int i = 0; i < i_pro_len; i++ )
    {
        if ( !strcmp( psz_subprotocol, WS_SUBPROTOCOLS[i] ) )
            return true;
    }
    return false;
}

static void fail_ws_conn( block_t *p_res, uint16_t i_code, const char *psz_msg )
{
    assert( p_res->i_buffer == 0 );
    assert( strlen(psz_msg) <= 123 );
    uint8_t i_payload = 2 + strlen( psz_msg );
    p_res->p_buffer[0] = 0x88;        // fin & close
    p_res->p_buffer[1] = i_payload;   // mask bit is 0
    p_res->p_buffer[2] = ( i_code >> 8 ) & 0xff;
    p_res->p_buffer[3] = ( i_code      ) & 0xff;
    memcpy( p_res->p_buffer + 4, psz_msg, strlen(psz_msg) );
    p_res->i_buffer = 2 + i_payload;
    log_Err( "close ws connection on error %d (%s)", i_code, psz_msg );
}

static void close_ws_conn( block_t *p_res )
{
    assert( p_res->i_buffer == 0 );
    uint16_t i_code = WS_NORMAL_CLOSE;
    p_res->p_buffer[0] = 0x88;        // fin & close
    p_res->p_buffer[1] = 2;   // mask bit is 0, payload len is 2
    p_res->p_buffer[2] = ( i_code >> 8 ) & 0xff;
    p_res->p_buffer[3] = ( i_code      ) & 0xff;
    p_res->i_buffer = 4;
    log_Dbg( "close ws connection on normal, send return close msg" );
}

static int handle_ws_payload( jsonrpc_server_t *p_this, block_t *p_req,
                              block_t *p_res )
{
    ws_jsonrpc_server_t *p_server = (ws_jsonrpc_server_t *)p_this;
    uint64_t i_payload;
    int i_header = get_ws_header_len( p_req->p_buffer, p_req->i_buffer );
    int i_ret = get_ws_payload_len( p_req->p_buffer, p_req->i_buffer,
                                    &i_payload );
    assert( i_header != -1 );
    assert( i_ret != -1 );

    uint8_t p_mark_key[4];
    for ( int i = 0; i < 4; i++ )
        p_mark_key[i] = p_req->p_buffer[ 2 + i ];

    block_t *p_json_req = block_Alloc( i_payload );
    if ( !p_json_req )
    {
        log_Err( "no memory" );
        return JSONRPC_ERR_NOMEM;
    }
    uint8_t *p_payload = p_req->p_buffer + i_header + 4;
    for ( int i = 0; i < i_payload; i++ )
        p_json_req->p_buffer[i] = p_payload[i] ^ p_mark_key[ i % 4 ];
    p_json_req->i_buffer = i_payload;

    // call base handle_request
    int ret = p_server->base.pf_handle_request( p_this, p_json_req, p_res );
    block_Release( p_json_req );
    add_ws_response_header( p_res );
    return ret;
}

static block_t *concatenate_fragments( block_t *p_req, block_t *p_res )
{
    uint8_t *p_ptr = p_req->p_buffer;
    int i_ptr = p_req->i_buffer;
    int i_opcode = -1;

    block_t *p_block = block_Alloc( i_ptr );
    if ( !p_block )
    {
        log_Err( "no memory" );
        return NULL;
    }
    assert( p_block->i_buffer == 0 );

    uint64_t i_payload;
    int      i_header;
    uint8_t p_mark_key[4];

    while ( !(p_ptr[0] & 0x80) && i_ptr > 0 )
    {
        // fragments
        if ( i_opcode == -1 )
        {
            i_opcode = p_ptr[0] & 0x0f;
            if ( i_opcode == 0x0 )
                fail_ws_conn( p_res, WS_UNCONSISTANT_DATATYPE,
                              "first fragment opcode is 0x0" );
        }
        else
        {
            if ( (p_ptr[0] & 0x0f) != 0x0 )
                fail_ws_conn( p_res, WS_UNCONSISTANT_DATATYPE,
                              "following fragment opcode is not 0x0" );
        }

        i_header = get_ws_header_len( p_ptr, i_ptr );
        int i_ret = get_ws_payload_len( p_ptr, i_ptr, &i_payload );
        assert( i_header != -1 && i_ret != -1 );

        for ( int i = 0; i < 4; i++ )
            p_mark_key[i] = p_ptr[ i_header + i ];
        for ( int i = 0; i < i_payload; i++ )
            p_block->p_buffer[ p_block->i_buffer + i ] =
                p_ptr[ i_header + 4 + i ] ^ p_mark_key[ i % 4 ];
        p_block->i_buffer += i_payload;

        p_ptr += i_header + 4 + i_payload;
        i_ptr -= i_header + 4 + i_payload;
    }

    return p_block;
}

static void add_ws_response_header( block_t *p_res )
{
    int i_header;
    if ( p_res->i_buffer <= 125 )
        i_header = 2;
    else if ( p_res->i_buffer <= 65535 )
        i_header = 4;
    else
        i_header = 10;
    uint64_t i_payload = p_res->i_buffer;

    if ( p_res->i_buffer + i_header >= p_res->i_maxlen )
    {
        p_res = block_Realloc( p_res, 4096 );
        if ( !p_res )
        {
            log_Err( "no memory" );
            return;
        }
    }
    memmove( p_res->p_buffer + i_header, p_res->p_buffer, p_res->i_buffer );
    p_res->p_buffer[0] = 0x81;      // fin and text type
    p_res->p_buffer[1] = (i_header == 2) ? i_payload : \
                         ((i_header == 4) ? 126 : 127);
    if ( i_header == 4 )
    {
        p_res->p_buffer[2] = ( i_payload >> 8 ) & 0xff;
        p_res->p_buffer[3] = ( i_payload      ) & 0xff;
    }
    else if ( i_header == 10 )
    {
        p_res->p_buffer[2] = ( i_payload >> 56 ) & 0xff;
        p_res->p_buffer[3] = ( i_payload >> 48 ) & 0xff;
        p_res->p_buffer[4] = ( i_payload >> 40 ) & 0xff;
        p_res->p_buffer[5] = ( i_payload >> 32 ) & 0xff;
        p_res->p_buffer[6] = ( i_payload >> 24 ) & 0xff;
        p_res->p_buffer[7] = ( i_payload >> 16 ) & 0xff;
        p_res->p_buffer[8] = ( i_payload >> 8  ) & 0xff;
        p_res->p_buffer[9] = ( i_payload       ) & 0xff;
    }
    p_res->i_buffer += i_header;
}

