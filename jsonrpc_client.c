// file : jsonrpc_client.c
// auth : lagula
// date : 2012-6-7
// desc : implement jsonrpc.serverProxy
//

#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <errno.h>
#include <sys/un.h>
#include <assert.h>
#include <unistd.h>
#include "jsonrpc_client.h"
#include "socket.h"
#include "log.h"
#include "block.h"
#include "jsonrpc_utils.h"

#define SOCKET_TIMEOUT  15000000        // 15 second

extern int errno;


static void jsonrpc_client_exit( jsonrpc_client_t *p_this );
static int handshake( jsonrpc_client_t *p_this, const char *psz_protocol,
                      const char **ppsz_notifyService, int i_notifyService );


static int connect_tcp_socket( jsonrpc_client_t *p_this, const char *psz_name,
                               int i_port )
{
    if ( ( p_this->sock = socket( AF_INET, SOCK_STREAM, 0 ) ) < 0 )
    {
        log_Err( "open network socket failed (%s)", strerror( errno ) );
        return -1;
    }

    // set connect timeout as 15 seconds
    if ( socket_settimeout( p_this->sock, SOCKET_TIMEOUT ) < 0 )
    {
        log_Err( "set sock timeout failed %s", strerror( errno ) );
        return -1;
    }

    if ( socket_connect( p_this->sock, psz_name, i_port ) < 0 )
    {
        log_Err( "connect %s:%d failed (%s)", psz_name, i_port,
                 strerror( errno ) );
        return -1;
    }

    return 0;
}

static int connect_unix_socket( jsonrpc_client_t *p_this,
                                const char *psz_conn_file )
{
    struct sockaddr_un conn_addr;

    if ( strlen(psz_conn_file) >= sizeof( conn_addr.sun_path ) )
    {
        log_Err( "connect unix socket failed, the file name is too long" );
        return -1;
    }

    if ( ( p_this->sock = socket( AF_UNIX, SOCK_STREAM, 0 ) ) < 0 )
    {
        log_Err( "open unix socket failed (%s)", strerror( errno ) );
        return -1;
    }

    // set connect timeout as 15 seconds
    if ( socket_settimeout( p_this->sock, SOCKET_TIMEOUT ) < 0 )
    {
        log_Err( "set sock timeout failed %s", strerror( errno ) );
        return -1;
    }

    conn_addr.sun_family = AF_UNIX;
    strcpy( (char*)conn_addr.sun_path, psz_conn_file );
    if ( connect( p_this->sock, &conn_addr, sizeof( conn_addr ) ) < 0 )
    {
        log_Err( "connect %s failed (%s)", psz_conn_file, strerror(errno) );
        return -1;
    }

    return 0;
}

static int jsonrpc_client_reinit( jsonrpc_client_t *p_this )
{
    int i_ret = -1;
    assert( p_this->b_error );

    close( p_this->sock );

    if ( p_this->i_sock_type == AF_UNIX || p_this->i_sock_type == PF_UNIX )
    {
        i_ret = connect_unix_socket( p_this, p_this->psz_unix_conn_file );
    }
    else if ( p_this->i_sock_type == AF_INET || p_this->i_sock_type == PF_INET )
    {
        i_ret = connect_tcp_socket( p_this, p_this->psz_tcp_name,
                                    p_this->i_tcp_port );
    }

    p_this->b_error = (i_ret == -1) ? true : false;
    if ( p_this->b_error )
        return -1;

    if ( handshake( p_this, p_this->psz_protocol,
                    (const char **)p_this->ppsz_notifyService,
                    p_this->i_notifyService ) < 0 )
        return -1;

    if ( p_this->pf_on_reconnected )
        p_this->pf_on_reconnected( p_this );

    return 0;
}

static int read_response( jsonrpc_client_t *p_this, block_t *p_block )
{
    int i_read;
    int fd = p_this->sock;

    while ( true )
    {
        if ( p_block->i_buffer + 4096 >= p_block->i_maxlen )
        {
            p_block = block_Realloc( p_block, 4096 );
            if ( !p_block )
            {
                log_Err( "no memory %s %d", __FILE__, __LINE__ );
                return -1;
            }
        }

        i_read = recv( fd, p_block->p_buffer + p_block->i_buffer, 4096, 0 );
        if ( i_read < 0 )
        {
            if ( errno == EAGAIN )
            {
                log_Warn( "jsonrpc client recv timeout" );
                return -1;
            }
            else if ( errno == EINTR )
                continue;
            else
            {
                log_Err( "jsonrpc client recv failed (%s), close connection",
                         strerror( errno ) );
                p_this->b_error = true;
                return -1;
            }
        }
        else if ( i_read == 0 )
        {
            log_Warn( "peer closed connection while read" );
            p_this->b_error = true;
            return -1;
        }
        else
        {
            p_block->i_buffer += i_read;
            // NOTE: the request string should contain '\0' at end, it means
            // client should send it.
            size_t i_len;
            if ( json_request_IsComplete( p_block, &i_len ) )
            {
                if ( i_len < p_block->i_buffer )
                    log_Warn( "jsonrpc_call recved more data than one response "
                              "%s", (char*)(p_block->p_buffer + i_len) );
                break;
            }
            else
                log_Warn( "jsonrpc_call recved request is incomplete" );
        }
    }

    return 0;
}

struct json_object *jsonrpc_call( jsonrpc_client_t *p_this,
                                  const char *psz_method,
                                  struct json_object *p_params )
{
    struct json_object *p_req, *p_res;
    char err[256];

    if ( p_this->b_error )
    {
        if ( jsonrpc_client_reinit( p_this ) < 0 )
        {
            p_res = json_object_new_object();
            sprintf( err, "jsonrpc_client_reinit failed (%s)", strerror(errno));
            json_object_object_add( p_res, "error",
                                    json_object_new_string( err ) );
            return p_res;
        }
    }

    p_req = json_object_new_object();
    p_res = json_object_new_object();
    json_object_object_add( p_req, "jsonrpc", json_object_new_string("2.0") );
    json_object_object_add( p_req, "method",
                            json_object_new_string( psz_method ) );
    // p_params is NULL means calling "method" with no parameter, give "params"
    // an empty list when this happen.
    if ( !p_params )
        p_params = json_object_new_array();
    json_object_object_add( p_req, "params", p_params );
    const char *psz_req = json_object_to_json_string( p_req );

    int i_send;

    while ( true )
    {
        i_send = socket_sendall( p_this->sock, (uint8_t*)psz_req,
                                 strlen(psz_req) + 1 );
        if ( i_send < strlen(psz_req) + 1 )
        {
            if ( errno == EAGAIN )
            {
                // socket send buffer is full
                log_Warn( "jsonrpc client send request timeout, try again" );
                psz_req = psz_req + i_send;
                continue;
            }
            else if ( errno == EINTR )
                continue;
            else
            {
                p_this->b_error = true;
                snprintf( err, sizeof(err), "jsonrpc send failed (%s)",
                          strerror(errno) );
                json_object_object_add( p_res, "error",
                                        json_object_new_string( err ) );
                json_object_put( p_req );
                return p_res;
            }
        }
        else
        {
            break;
        }
    }


    block_t *p_block = block_Alloc( 4096 );
    if ( read_response( p_this, p_block ) < 0 )
    {
        if ( errno == 0 )
            sprintf( err, "jsonrpc recv failed, peer closed connection" );
        else
        {
            snprintf( err, sizeof(err) - 1, "jsonrpc recv failed (%s)",
                      strerror(errno) );
            if ( errno == EAGAIN )
                sprintf( err, "timeout while receiving" );
        }
        json_object_object_add( p_res, "error",
                                json_object_new_string( err ) );
        block_Release( p_block );
        json_object_put( p_req );
        return p_res;
    }

    struct json_object *p_tmp = json_tokener_parse( (char*)p_block->p_buffer );
    //log_Dbg( "jsonrpc_call got result: %s", json_object_to_json_string(p_tmp) );
    if ( is_error( p_tmp ) )
    {
        snprintf( err, sizeof(err) - 1,
                  "jsonrpc parse response failed, response: %s",
                  (char*)p_block->p_buffer );
        json_object_object_add( p_res, "error",
                                json_object_new_string( err ) );
        block_Release( p_block );
        json_object_put( p_req );
        return p_res;
    }

    json_object_put( p_req );
    json_object_put( p_res );
    block_Release( p_block );
    p_res = p_tmp;
    log_Dbg( "jsonrpc client jsonrpc_call exit normally" );
    return p_res;
}

// jsonrpc_notify must use short connection
int jsonrpc_notify( jsonrpc_client_t *p_this, const char *psz_method,
                    struct json_object *p_params )
{
    if ( p_this->b_error )
    {
        log_Err( "connection is not made, you should init first" );
        return -1;
    }

    struct json_object *p_req = json_object_new_object();
    if ( !p_req )
    {
        log_Err( "no memory" );
        return -1;
    }
    json_object_object_add( p_req, "jsonrpc", json_object_new_string("2.0") );
    json_object_object_add( p_req, "method",
                            json_object_new_string( psz_method ) );
    if ( !p_params )
        p_params = json_object_new_array();
    json_object_object_add( p_req, "params", p_params );
    const char *psz_req = json_object_to_json_string( p_req );

    int i_send;
    i_send = socket_sendall( p_this->sock, (uint8_t*)psz_req,
                             strlen(psz_req) + 1 );
    if ( i_send < strlen(psz_req) + 1 )
    {
        if ( errno == EAGAIN )
            log_Err( "json_notify sendall timeout" );
        else
            log_Err( "json_notify sendall failed (%s)", strerror(errno) );
        return -1;
    }
    // should use close, don't use shutdown, shutdonw will discard any data
    // waiting to be send. close will try to complete transmiting data in
    // socket buffer. Although the close in pf_exit will cause EBADF.

    //shutdown( p_this->sock, SHUT_RDWR );
    close( p_this->sock );

    json_object_put( p_req );
    return 0;
}

// p_this->sock is block mode in default,
// timeout is in microseconds,
// timeout 0 means never timeout,
// caller need to remove the returned object.
static struct json_object *get_notify( jsonrpc_client_t *p_this,
                                       bool b_block, int i_timeout )
{
    block_t *p_buf = p_this->p_buf;

    if ( p_this->b_error )
    {
        if ( jsonrpc_client_reinit( p_this ) < 0 )
            return NULL;
    }

    bool b_old_block = socket_getblocking( p_this->sock );
    if ( b_block != b_old_block )
    {
        if ( socket_setblocking( p_this->sock, b_block ) < 0 )
        {
            log_Err( "get_notify set blocking failed" );
            return NULL;
        }
    }

    int i_old_timeout;
    socket_gettimeout( p_this->sock, &i_old_timeout, NULL );
    if ( i_old_timeout != i_timeout )
    {
        if ( socket_settimeout( p_this->sock, i_timeout ) < 0 )
        {
            log_Err( "get_notify set timeout failed" );
            // roll back
            if ( b_old_block != b_block )
                socket_setblocking( p_this->sock, b_old_block );
            return NULL;
        }
    }

    struct json_object *p_obj = NULL;
    int i_read;
    while ( p_buf->i_buffer < 5 ||
            p_buf->i_buffer < 5 + ntohl(*(uint32_t*)(p_buf->p_buffer + 1)) )
    {
        if ( p_buf->i_buffer + 4096 >= p_buf->i_maxlen )
        {
            p_buf = block_Realloc( p_buf, 4096 );
            if ( !p_buf )
            {
                log_Err( "no memory" );
                goto error;
            }
        }
        i_read = recv( p_this->sock, p_buf->p_buffer + p_buf->i_buffer,
                       4096, 0 );
        if ( i_read < 0 )
        {
            if ( errno == EAGAIN )
                break;
            else if ( errno == EINTR )
                continue;
            else
            {
                log_Err( "get_notify recv failed (%s)", strerror( errno ) );
                p_this->b_error = true;
                break;
            }
        }
        else if ( i_read == 0 )
        {
            log_Err( "get_notify received unexpected EOF" );
            p_this->b_error = true;
            break;
        }
        else
        {
            p_buf->i_buffer += i_read;
        }
    } //~ while
    if ( p_buf->i_buffer > 5 &&
         p_buf->i_buffer >= 5 + ntohl( *(uint32_t*)(&p_buf->p_buffer[1]) ) )
    {
        assert( p_buf->p_buffer[0] == '$' );
        uint32_t i_len = ntohl( *(uint32_t*)(&p_buf->p_buffer[1]) );
        p_obj = json_tokener_parse( (char *)(p_buf->p_buffer + 5) );
        if ( is_error( p_obj ) )
        {
            log_Err( "parse json string %s failed",
                     (char*)(p_buf->p_buffer + 5) );
            goto error;
        }
        log_Dbg( "get_notify got a notify %s", (char*)(p_buf->p_buffer + 5));
        memmove( p_buf->p_buffer, p_buf->p_buffer + 5 + i_len,
                 p_buf->i_buffer - 5 - i_len );
        p_buf->i_buffer -= (5 + i_len);
    }

    // roll back
    if ( b_old_block != b_block )
        socket_setblocking( p_this->sock, b_old_block );
    if ( i_old_timeout != i_timeout )
        socket_settimeout( p_this->sock, i_old_timeout );
    return p_obj;

error:
    if ( b_old_block != b_block )
        socket_setblocking( p_this->sock, b_old_block );
    if ( i_old_timeout != i_timeout )
        socket_settimeout( p_this->sock, i_old_timeout );
    return NULL;
}

static int handshake( jsonrpc_client_t *p_this, const char *psz_protocol,
                      const char **ppsz_notifyService, int i_notifyService )
{
    struct json_object *p_proto = json_object_new_object();
    json_object_object_add( p_proto, "protocol",
                            json_object_new_string( psz_protocol ) );
    if ( !strcasecmp( psz_protocol, "notify" ) )
    {
        struct json_object *p_array = json_object_new_array();
        for ( int i = 0; i < i_notifyService; i++ )
            json_object_array_add( p_array,
                                   json_object_new_string( ppsz_notifyService[i] ) );
        json_object_object_add( p_proto, "notifyServiceNames", p_array );
    }
    const char *psz_data = json_object_to_json_string( p_proto );
    size_t i_data = strlen( psz_data ) + 1;
    if ( socket_sendall( p_this->sock,
                         (const uint8_t*)psz_data, i_data ) < i_data )
    {
        json_object_put(p_proto);
        log_Err( "handshake send failed (%s)", strerror( errno ) );
        return -1;
    }
    json_object_put(p_proto);

    bool b_ok = false;
    char buf[1024];
    int i_buf = 0;
    int i_read;
    while ( true )
    {
        i_read = recv( p_this->sock, buf + i_buf, 1024, 0 );
        if ( i_read == 0 )
            break;
        else if ( i_read < 0 )
        {
            if ( errno == EINTR )
                continue;

            log_Err( "handshake recv failed (%s)", strerror( errno ) );
            return -1;
        }
        i_buf += i_read;
        if ( !strncmp( buf, "handshake OK", 12 ) )
        {
            b_ok = true;
            break;
        }
    }
    return b_ok ? 0 : -1;
}

static int _jsonrpc_client_init( jsonrpc_client_t *p_this,
                                 const char *psz_arg_proto,
                                 int i_sock_flag, va_list args )
{
    p_this->i_sock_type = i_sock_flag;
    p_this->b_error = false;
    p_this->psz_unix_conn_file = NULL;
    p_this->psz_tcp_name = NULL;
    p_this->i_tcp_port = 0;
    p_this->psz_protocol = NULL;
    p_this->ppsz_notifyService = NULL;
    p_this->i_notifyService = 0;

    p_this->pf_call = jsonrpc_call;
    p_this->pf_notify = jsonrpc_notify;
    p_this->pf_get_notify = get_notify;
    p_this->pf_exit = jsonrpc_client_exit;

    p_this->p_buf = block_Alloc( 4096 );
    if ( !p_this->p_buf )
    {
        log_Err( "no memory" );
        goto error;
    }

    if ( i_sock_flag == AF_INET || i_sock_flag == PF_INET )
    {
        const char *psz_name = (const char*)va_arg( args, const char * );
        int i_port = (int)va_arg( args, int );

        p_this->psz_tcp_name = strdup(psz_name);
        p_this->i_tcp_port = i_port;
        if ( connect_tcp_socket( p_this, psz_name, i_port ) < 0 )
            goto error;
    }
    else if ( i_sock_flag == AF_UNIX || i_sock_flag == PF_UNIX )
    {
        const char *psz_conn_file = (const char*)va_arg( args, const char * );

        p_this->psz_unix_conn_file = strdup(psz_conn_file);
        if ( connect_unix_socket( p_this, psz_conn_file ) < 0 )
            goto error;
    }
    else
    {
        log_Err( "sock flag %d is unknown", i_sock_flag );
        goto error;
    }
    // protocol
    const char *psz_protocol = "rpc";
    if ( psz_arg_proto )
    {
        if ( strcasecmp( psz_arg_proto, "rpc" ) &&
             strcasecmp( psz_arg_proto, "notify" ) )
        {
            log_Err( "protocol %s is unknown", psz_arg_proto );
            goto error;
        }
        psz_protocol = psz_arg_proto;
    }
    p_this->psz_protocol = strdup( psz_protocol );
    if ( !p_this->psz_protocol )
    {
        log_Err( "no memory" );
        goto error;
    }

    const char **ppsz_notifyService = NULL;
    int i_notifyService = 0;
    if ( !strcmp( psz_protocol, "notify" ) )
    {
        ppsz_notifyService = ( const char **)va_arg( args, const char **);
        i_notifyService = ( int )va_arg( args, int );
    }
    p_this->i_notifyService = i_notifyService;
    p_this->ppsz_notifyService = malloc( sizeof(char*) * i_notifyService );
    if ( !p_this->ppsz_notifyService )
    {
        log_Err( "no memory" );
        goto error;
    }
    for ( int i = 0; i < i_notifyService; i++ )
    {
        p_this->ppsz_notifyService[i] = strdup( ppsz_notifyService[i] );
        if ( !p_this->ppsz_notifyService[i] )
        {
            log_Err( "no memory" );
            goto error;
        }
    }

    if( handshake( p_this, psz_protocol,
                   ppsz_notifyService, i_notifyService ) < 0 )
        goto error;


    return 0;

error:
    p_this->pf_exit( p_this );
    return -1;
}

int jsonrpc_client_init( jsonrpc_client_t *p_this, int i_sock_flag, ... )
{
    va_list args;
    va_start( args, i_sock_flag );
    int i_ret = _jsonrpc_client_init( p_this, "rpc", i_sock_flag, args );
    va_end( args );
    return i_ret;
}

int jsonrpc_client_subscribe_init( jsonrpc_client_t *p_this,
                                   int i_sock_flag, ... )
{
    va_list args;
    va_start( args, i_sock_flag );
    int i_ret = _jsonrpc_client_init( p_this, "notify", i_sock_flag, args );
    va_end( args );
    return i_ret;
}

static void jsonrpc_client_exit( jsonrpc_client_t *p_this )
{
    if ( p_this->psz_unix_conn_file )
        free( p_this->psz_unix_conn_file );
    if ( p_this->psz_tcp_name )
        free( p_this->psz_tcp_name );

    free( p_this->psz_protocol );
    for ( int i = 0; i < p_this->i_notifyService; i++ )
        free( p_this->ppsz_notifyService[i] );
    free( p_this->ppsz_notifyService );

    close( p_this->sock );

    if ( p_this->p_buf)
        block_Release( p_this->p_buf );
}

