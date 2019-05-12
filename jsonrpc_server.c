// file : jsonrpc_server.c
// auth : lagula
// date : 2012-6-7
// desc : implement jsonrpc.Server
//

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdarg.h>
#include <sys/un.h>
#include <errno.h>
#include <signal.h>
#include <sys/epoll.h>
#include <assert.h>
#include "jsonrpc_server.h"
#include "log.h"
#include "block.h"
#include "socket.h"
#include "jsonrpc_utils.h"

extern int errno;

#define LISTEN_BACKLOG 5

#define EPOLL_SIZE 1024
#define EPOLL_MAX_EVENT 64
#define EPOLL_TIMEOUT 50    // 0.05 second



static bool sg_b_abort = false;


static int process_write( int fd, block_t *p_res );
static void remove_request_references( jsonrpc_server_t *p_this,
                                       jsonrpc_request_t *p_request );

void handle_signal( int signum )
{
    if ( signum == SIGINT || signum == SIGTERM )
        sg_b_abort = true;
}

static int jsonrpc_request_init( jsonrpc_request_t *p_request )
{
    p_request->i_sockfd = -1;
    memset( p_request->psz_ip, 0, sizeof( p_request->psz_ip ) );
    p_request->p_req = block_Alloc( 8192 );
    p_request->p_res = block_Alloc( 8192 );
    if ( !p_request->p_req || !p_request->p_res )
    {
        log_Err( "no memory" );
        return -1;
    }
    p_request->i_state = CONN_CLOSED;
    p_request->psz_protocol = NULL;
    p_request->ppsz_notify_service = NULL;
    p_request->i_notify_service = 0;
    p_request->p_next = NULL;
    return 0;
}

static jsonrpc_request_t *jsonrpc_request_create()
{
    jsonrpc_request_t *p_request = malloc( sizeof(jsonrpc_request_t) );
    if ( !p_request )
    {
        log_Err( "no memory" );
        return NULL;
    }
    if ( jsonrpc_request_init( p_request ) < 0 )
    {
        log_Err( "jsonrpc_request_init failed" );
        return NULL;
    }
    return p_request;
}

static void jsonrpc_request_destroy( jsonrpc_request_t *p_request )
{
    block_Release( p_request->p_req );
    block_Release( p_request->p_res );

    free( p_request->psz_protocol );
    for ( int i = 0; i < p_request->i_notify_service; i++ )
        free( p_request->ppsz_notify_service[i] );
    free( p_request->ppsz_notify_service );
}

// notify_dispatch functions can use this to send notify
int jsonrpc_request_sendResponse( jsonrpc_request_t *p_request,
                                  block_t *p_block )
{
    block_t *p_res = p_request->p_res;
    if ( p_res->i_buffer + p_block->i_buffer >= p_res->i_maxlen )
    {
        p_res = block_Realloc( p_res, p_block->i_buffer );
        if ( !p_res )
        {
            log_Err( "no memory" );
            return -1;
        }
    }
    memcpy( p_res->p_buffer + p_res->i_buffer, p_block->p_buffer,
            p_block->i_buffer );
    p_res->i_buffer += p_block->i_buffer;
    return process_write( p_request->i_sockfd, p_res );
}


static int __register_function( jsonrpc_server_t *p_this,
                                const char *psz_method,
                                void *pf )
{
    if ( !p_this->b_initialized )
    {
        log_Err( "__register_function is called before initialization" );
        return -1;
    }

    hashmap_key_t key;
    key.u.psz_string = psz_method;
    key.type = 'c';
    hashmap_put( p_this->hashmap, key, pf );
    return 0;
}

static int register_function( jsonrpc_server_t *p_this, const char *psz_method,
                              pf_rpc_callback_t pf )
{
    return __register_function( p_this, psz_method, pf );
}

static int register_member_function( jsonrpc_server_t *p_this,
                                     const char *psz_method,
                                     pf_rpc_member_callback_t pmf )
{
    return __register_function( p_this, psz_method, pmf );
}

static int register_class_object( jsonrpc_server_t *p_this,
                                  const char *psz_class, void *p_obj )
{
    if ( !p_this->b_initialized )
    {
        log_Err( "register_calss_object is called before initialization" );
        return -1;
    }

    hashmap_key_t key;
    key.u.psz_string = psz_class;
    key.type = 'c';
    hashmap_put( p_this->classmap, key, p_obj );
    return 0;
}

// NOTE: can just call it once
static int register_notify_services( jsonrpc_server_t *p_this,
                                     const char **ppsz_notify_service,
                                     int i_notify_service )
{
    p_this->ppsz_supportedNotifyService =
        malloc( sizeof(char*) * i_notify_service );
    if ( !p_this->ppsz_supportedNotifyService )
    {
        log_Err( "no memory" );
        return JSONRPC_ERR_NOMEM;
    }
    for ( int i = 0; i < i_notify_service; i++ )
    {
        p_this->ppsz_supportedNotifyService[i] = strdup(ppsz_notify_service[i]);
        if ( !p_this->ppsz_supportedNotifyService[i] )
        {
            log_Err( "no memory" );
            return JSONRPC_ERR_NOMEM;
        }

        // add NULL terminator of request list in notifyServiceMap
        hashmap_key_t key;
        key.type = 'c';
        key.u.psz_string = p_this->ppsz_supportedNotifyService[i];
        hashmap_put( p_this->notifyServiceMap, key, NULL );
    }
    p_this->i_supportedNotifyService = i_notify_service;
    return 0;
}

static bool __json_request_IsComplete( jsonrpc_server_t *p_server,
                                       block_t *p_req, size_t *pi_len )
{
    return json_request_IsComplete( p_req, pi_len );
}

static void process_read( jsonrpc_server_t *p_server,
                          jsonrpc_request_t *p_request )
{
    int fd = p_request->i_sockfd;
    block_t *p_req = p_request->p_req;

    int i_read;

    while ( true )
    {
        if ( p_req->i_buffer + 4096 >= p_req->i_maxlen )
        {
            p_req = block_Realloc( p_req, 4096 );
            if ( !p_req )
            {
                log_Err( "no memory %s %d", __FILE__, __LINE__ );
                abort();
            }
        }

        i_read = recv( fd, p_req->p_buffer + p_req->i_buffer, 4096, 0 );
        if ( i_read < 0 )
        {
            if ( errno == EAGAIN )
                break;
            else
            {
                log_Err( "read failed (%s), close connection",
                         strerror( errno ) );
                break;
            }
        }
        else if ( i_read == 0 )
        {
            log_Dbg( "peer closed connection while read" );
            // generate EPIPE and EPOLLHUG
            shutdown( fd, SHUT_RDWR );
            char c = 0;
            send( fd, &c, 1, 0 );
            break;
        }
        else
        {
            p_req->i_buffer += i_read;
            // drop notify request except handshake
            if ( p_request->psz_protocol
                 && !strcasecmp( p_request->psz_protocol, "notify" )
                 && p_request->i_state == CONN_HANDSHAKED )
            {
                log_Warn( "drop notify request, notify should not "
                          "send request" );
                p_req->i_buffer = 0;
            }
        }
    }
}

static void process_requests( jsonrpc_server_t *p_server,
                              jsonrpc_request_t *p_request )
{
    int fd = p_request->i_sockfd;
    block_t *p_req = p_request->p_req;
    block_t *p_res = p_request->p_res;

    if ( p_server->pf_get_request )
        p_server->pf_get_request( p_server, p_request );

    // NOTE: the request string should contain '\0' at end, it means
    // client should send it.
    size_t i_len = 0;
    while ( p_server->pf_request_IsComplete( p_server, p_req, &i_len ) )
    {
        // if the response of the previous request has not been send
        // completely (sendbuf full), this request may arrive. In this
        // case, drop this request and suggest user to enlarge sendbuf
        // or faster client recv.
        if ( p_res->i_buffer != 0 )
        {
            log_Warn( "drop rpc request, the response of previous "
                      "request has not been send completely. "
                      "Should enlarge sendbuf or speed up client "
                      "recv." );

            memmove( p_req->p_buffer, p_req->p_buffer + i_len,
                     p_req->i_buffer - i_len );
            p_req->i_buffer -= i_len;
            continue;
        }

        int ret;
        if ( p_request->i_state == CONN_CONNECTED )
        {
            ret = p_server->pf_handle_handshake( p_server, p_request );
            if ( ret < 0 )
            {
                log_Dbg( "handshake failed, close conntion" );
                p_res->i_buffer = 0;
                p_request->i_state = CONN_CLOSED;
                shutdown( fd, SHUT_RDWR );
                // generate EPIPE and EPOLLHUP
                char c = 0;
                send( fd, &c, 1, 0 );
            }
            else
            {
                process_write( fd, p_res );
                p_request->i_state = CONN_HANDSHAKED;
                log_Dbg( "handshake succeed (fd:%d)", fd );
            }
        }
        else if ( p_request->i_state == CONN_HANDSHAKED )
        {
            assert( p_res->i_buffer == 0 );
            p_server->pf_handle_request( p_server,
                                         p_request->p_req,
                                         p_request->p_res );
            // write response on both success and error condations
            process_write( fd, p_res );
        }

        // next request
        memmove( p_req->p_buffer, p_req->p_buffer + i_len,
                 p_req->i_buffer - i_len );
        p_req->i_buffer -= i_len;
    }
}

static int process_write( int fd, block_t *p_res )
{
    int i_remain = p_res->i_buffer;
    uint8_t *ptr = p_res->p_buffer;
    int i_ret = 0;
    int i_send = 0;
    while ( i_remain > 0 )
    {
        i_send = send( fd, ptr, i_remain, 0 );
        if ( i_send < 0 )
        {
            i_ret = -1;
            if ( errno == EAGAIN )
                break;
            else
            {
                log_Err( "write failed (%s)", strerror(errno) );
                break;
            }
        }
        else
        {
            ptr += i_send;
            i_remain -= i_send;
        }
    }

    memmove( p_res->p_buffer,
             p_res->p_buffer + p_res->i_buffer - i_remain,
             i_remain );
    p_res->i_buffer = i_remain;

    return i_ret;
}


static int serve( jsonrpc_server_t *p_this )
{
    if ( !p_this->b_initialized )
    {
        log_Err( "jsonrpc server has not been initialized" );
        return -1;
    }
    assert( p_this->tcpsock != -1 || p_this->unixsock != -1 );

    int epfd = -1;
    if ( (epfd = epoll_create( EPOLL_SIZE )) < 0 )
    {
        log_Err( "epoll create failed (%s)", strerror( errno ) );
        return -1;
    }

    int socks[2];
    socks[0] = p_this->tcpsock;
    socks[1] = p_this->unixsock;
    for ( int i = 0; i < sizeof(socks)/sizeof(socks[0]); i++ )
    {
        if ( socks[i] != -1 )
        {
            socket_setblocking( socks[i], 0 );

            struct epoll_event event;
            event.events = EPOLLIN | EPOLLET;
            event.data.fd = socks[i];
            if ( epoll_ctl( epfd, EPOLL_CTL_ADD, socks[i], &event ) < 0 )
            {
                log_Err( "epoll add listening sock failed (%s)",
                         strerror(errno) );
                return -1;
            }
        }
    }

    hashmap_key_t key;
    key.type = 'l';

    hashmap requestMap = hashmap_create(101);
    hashmap_iterator it;

    struct epoll_event events[EPOLL_MAX_EVENT];
    int i_ready;
    while ( !sg_b_abort )
    {
        i_ready = epoll_wait( epfd, events, EPOLL_MAX_EVENT, EPOLL_TIMEOUT );
        if ( i_ready < 0 )
        {
            if ( errno == EINTR )
                continue;

            log_Err( "epoll_wait failed (%s)", strerror(errno) );
            break;
        }
        else if ( i_ready > 0 )
        {
            for ( int i = 0; i < i_ready; i++ )
            {
                if ( events[i].data.fd == p_this->tcpsock ||
                     events[i].data.fd == p_this->unixsock )
                {
                    while ( true )
                    {
                        struct sockaddr_in addr;
                        socklen_t addrlen = sizeof( addr );
                        int connfd = accept( events[i].data.fd,
                                             &addr, &addrlen );
                        if ( connfd < 0 )
                        {
                            if ( errno == EAGAIN )
                                break;
                            else
                            {
                                log_Err( "accept (%s)", strerror(errno) );
                                break;
                            }
                        }

                        if ( p_this->pf_on_client_connected )
                            p_this->pf_on_client_connected( p_this, connfd );

                        socket_setblocking( connfd, 0 );
                        struct epoll_event event;
                        event.events = EPOLLIN | EPOLLOUT | EPOLLET;
                        event.data.fd = connfd;
                        if ( epoll_ctl( epfd, EPOLL_CTL_ADD,
                                        connfd, &event ) < 0)
                        {
                            log_Err( "epoll ctl failed (%s)", strerror(errno) );
                            goto error;
                        }

                        key.u.i_int32 = connfd;
                        assert( !hashmap_get( requestMap, key ) );
                        jsonrpc_request_t *p_request = jsonrpc_request_create();
                        if ( !p_request )
                        {
                            log_Err( "jsonrpc_request_create failed" );
                            goto error;
                        }
                        p_request->i_sockfd = connfd;
                        p_request->i_state = CONN_CONNECTED;
                        if ( !inet_ntop( AF_INET, &addr.sin_addr,
                                         p_request->psz_ip, 16 ) )
                        {
                            log_Err( "inet_ntop failed %s", strerror( errno ) );
                            goto error;
                        }
                        hashmap_put( requestMap, key, p_request );

                        log_Dbg( "jsonrpc server add connfd" );
                    }
                }
                else if ( events[i].events & EPOLLHUP ||
                          events[i].events & EPOLLERR )
                {
                    int i_fd = events[i].data.fd;
                    log_Dbg( "epoll pollhup enter (fd:%d)", i_fd );
                    if ( epoll_ctl( epfd, EPOLL_CTL_DEL, i_fd,
                                    &events[i] ) < 0 )
                    {
                        log_Err( "epoll del failed (%s)", strerror(errno) );
                        goto error;
                    }

                    if ( p_this->pf_on_client_closed )
                        p_this->pf_on_client_closed( p_this, i_fd );

                    close( i_fd );

                    key.u.i_int32 = i_fd;
                    jsonrpc_request_t *p_request;
                    p_request = hashmap_pop( requestMap, key, NULL );
                    // remove request references before delete it
                    remove_request_references( p_this, p_request );
                    jsonrpc_request_destroy( p_request );

                    log_Dbg( "epoll pollhup exit" );
                }
                else if ( events[i].events & EPOLLIN )
                {
                    int i_fd = events[i].data.fd;
                    log_Dbg( "epoll pollin enter (fd:%d)", i_fd );
                    key.u.i_int32 = i_fd;
                    jsonrpc_request_t *p_request;
                    p_request = hashmap_get( requestMap, key );
                    assert( i_fd == p_request->i_sockfd );
                    // buffer all data from socket recv buf
                    process_read( p_this, p_request );
                    // process requests and write response to socket send buf
                    process_requests( p_this, p_request );

                    log_Dbg( "epoll pollin exit (fd:%d)", i_fd );
                }
                else if ( events[i].events & EPOLLOUT )
                {
                    int i_fd = events[i].data.fd;
                    log_Dbg( "epoll pollout enter (fd:%d)", i_fd );
                    key.u.i_int32 = i_fd;
                    jsonrpc_request_t *p_request;
                    p_request = hashmap_get( requestMap, key );
                    process_write( i_fd, p_request->p_res );

                    log_Dbg( "epoll pollout exit (fd:%d)", i_fd );
                }
            }
        } // epoll wait
        // after epoll_wait has been processed
        if ( p_this->pf_on_processed )
            p_this->pf_on_processed( p_this );
    }

    log_Dbg( "serve() exited" );

    it = hashmap_iterate( requestMap );
    while ( hashmap_next( &it ) )
        jsonrpc_request_destroy( (jsonrpc_request_t*)it.p_val );

    hashmap_free( requestMap );
    close( epfd );

    return 0;

error:

    it = hashmap_iterate( requestMap );
    while ( hashmap_next( &it ) )
        jsonrpc_request_destroy( (jsonrpc_request_t*)it.p_val );

    hashmap_free( requestMap );
    close( epfd );

    return -1;
}

// handshake request: "{protocol: rpc}" or
//                    "{protocol: notify, notifyServiceNames: [ xxx, xxx, ... ]}"
// handshake response: "handshake OK"
// handshake request should contain '\0' as terminator
static int handle_handshake( jsonrpc_server_t *p_server,
                             jsonrpc_request_t *p_request )
{
    block_t *p_req = p_request->p_req;
    // set psz_protocol
    struct json_object *p_obj = json_tokener_parse( (char*)p_req->p_buffer );
    if ( !json_object_object_get( p_obj, "protocol" ) )
    {
        log_Err( "handshake request format error: %s, refuse connection",
                 json_object_to_json_string( p_obj ) );
        json_object_put( p_obj );
        return -1;
    }
    const char *psz_protocol = json_object_get_string(
                                   json_object_object_get( p_obj, "protocol" ) );
    if ( strcasecmp( psz_protocol, "rpc" ) &&
         strcasecmp( psz_protocol, "notify" ) )
    {
        log_Err( "handshake protocol %s is unknown, refuse connection",
                 psz_protocol );
        json_object_put( p_obj );
        return -1;
    }

    p_request->psz_protocol = strdup( psz_protocol );
    if ( !p_request->psz_protocol )
    {
        log_Err( "no memory" );
        json_object_put( p_obj );
        return JSONRPC_ERR_NOMEM;
    }
    if ( !strcasecmp( psz_protocol, "notify" ) )
    {
        struct json_object *p_array =
            json_object_object_get( p_obj, "notifyServiceNames" );
        int i_array = json_object_array_length( p_array );
        for ( int i = 0; i < i_array; i++ )
        {
            const char *psz_tmp = json_object_get_string(
                                      json_object_array_get_idx( p_array, i ) );
            int j;
            for ( j = 0; j < p_server->i_supportedNotifyService; j++ )
                if ( !strcmp( psz_tmp,
                              p_server->ppsz_supportedNotifyService[j] ) )
                    break;
            if ( j == p_server->i_supportedNotifyService )
            {
                log_Err( "notify service %s is unknown, refuse connection",
                         psz_tmp );
                json_object_put( p_obj );
                return -1;
            }
        }
        // check succeed
        p_request->i_notify_service = i_array;
        p_request->ppsz_notify_service = malloc( sizeof(char*) * i_array );
        if ( !p_request->ppsz_notify_service )
        {
            log_Err( "no memory" );
            json_object_put( p_obj );
            return JSONRPC_ERR_NOMEM;
        }
        for ( int i = 0; i < i_array; i++ )
        {
            const char *psz_tmp = json_object_get_string(
                                      json_object_array_get_idx( p_array, i ) );
            p_request->ppsz_notify_service[i] = strdup( psz_tmp );
            if ( !p_request->ppsz_notify_service[i] )
            {
                log_Err( "no memory" );
                json_object_put( p_obj );
                return JSONRPC_ERR_NOMEM;
            }

            // put request at head of list in notifyServiceMap
            hashmap_key_t key;
            key.type = 'c';
            key.u.psz_string = psz_tmp;
            jsonrpc_request_t *p_headreq =
                hashmap_get( p_server->notifyServiceMap, key );
            p_request->p_next = p_headreq;
            hashmap_put( p_server->notifyServiceMap, key, p_request );
        }
    }
    json_object_put( p_obj );

    memcpy( p_request->p_res->p_buffer, "handshake OK\r\n", 14 );
    p_request->p_res->i_buffer = 14;
    return 0;
}

static int handle_request( jsonrpc_server_t *p_server,
                           block_t *p_reqblock, block_t *p_resblock )
{
    const char *psz_json = (char*)p_reqblock->p_buffer;
    struct json_object *p_req, *p_response;
    struct json_object *p_params = NULL;
    char psz_err[256] = {0};
    const char *psz_response = NULL;
    pf_rpc_callback_t pf = NULL;
    pf_rpc_member_callback_t pmf = NULL;
    void *p_classobj = NULL;
    assert( p_resblock->i_buffer == 0 );

    p_response = json_object_new_object();
    json_object_object_add( p_response, "jsonrpc",
                            json_object_new_string("2.0"));

    p_req = json_tokener_parse( psz_json );
    if ( is_error(p_req) || !json_object_is_type(p_req, json_type_object) )
    {
        sprintf( psz_err, "jsonrpc server parsing parameter error" );
        goto error;
    }
    json_object_object_foreach( p_req, psz_key, val )
    {
        if ( !strcmp( psz_key, "method" ) )
        {
            const char *psz_method = json_object_get_string( val );
            hashmap_key_t key;
            key.type = 'c';
            if ( !strchr( psz_method, '.' ) )
            {
                key.u.psz_string = psz_method;
                pf = hashmap_get( p_server->hashmap, key );
                if ( !pf )
                {
                    snprintf( psz_err, sizeof(psz_err) - 1,
                              "jsonrpc_server method %s is unkown", psz_method );
                    goto error;
                }
            }
            else
            {
                char *psz_class = strndup( psz_method,
                                           strchr(psz_method, '.') - psz_method );
                key.u.psz_string = psz_class;
                p_classobj = hashmap_get( p_server->classmap, key );
                key.u.psz_string = psz_method;
                pmf = hashmap_get( p_server->hashmap, key );
                free( psz_class );
                if ( !pmf || !p_classobj )
                {
                    snprintf( psz_err, sizeof(psz_err) - 1,
                              "jsonrpc_server method %s is unkown", psz_method );
                    goto error;
                }
            }
        }
        else if ( !strcmp( psz_key, "params" ) )
            p_params = val;
    }

    if ( !pf && !pmf )
    {
        snprintf( psz_err, sizeof( psz_err ) - 1,
                  "invalid request %s, method is missing", psz_json );
        goto error;
    }
    if ( !p_params )
    {
        snprintf( psz_err, sizeof( psz_err ) - 1,
                  "invalid request %s, params is missing", psz_json );
        goto error;
    }

    if ( pf )
        pf( p_params, p_response );
    else if ( pmf )
        pmf( p_classobj, p_params, p_response );

    json_object_put( p_req );
    psz_response = json_object_to_json_string( p_response );
    if ( strlen(psz_response) + 1 >= p_resblock->i_maxlen )
    {
        p_resblock = block_Realloc( p_resblock, strlen(psz_response) + 1 );
        if ( !p_resblock )
        {
            log_Err( "no memory" );
            return JSONRPC_ERR_NOMEM;
        }
    }
    memcpy( p_resblock->p_buffer, psz_response, strlen(psz_response) + 1 );
    p_resblock->i_buffer = strlen(psz_response) + 1;
    json_object_put( p_response );
    return 0;

error:
    log_Err( psz_err );
    json_object_object_add( p_response, "error",
                            json_object_new_string( psz_err ) );
    if ( !is_error(p_req) )
        json_object_put( p_req );
    psz_response = json_object_to_json_string( p_response );
    if ( strlen(psz_response) + 1 >= p_resblock->i_maxlen )
    {
        p_resblock = block_Realloc( p_resblock, strlen(psz_response) + 1 );
        if ( !p_resblock )
        {
            log_Err( "no memory" );
            abort();
        }
    }
    memcpy( p_resblock->p_buffer, psz_response, strlen(psz_response) + 1 );
    p_resblock->i_buffer = strlen(psz_response) + 1;
    json_object_put( p_response );
    return -1;
}

static void remove_request_references( jsonrpc_server_t *p_this,
                                       jsonrpc_request_t *p_request )
{
    // remove request from request list in notifyServiceMap
    for ( int i = 0; i < p_request->i_notify_service; i++ )
    {
        hashmap_key_t key;
        key.type = 'c';
        key.u.psz_string = p_request->ppsz_notify_service[i];
        jsonrpc_request_t *p_head = hashmap_get( p_this->notifyServiceMap, key);
        jsonrpc_request_t *p_tmp = p_head, *p_prev = NULL;
        while ( p_tmp != NULL )
        {
            if ( p_tmp == p_request )
            {
                if ( p_prev )
                    p_prev->p_next = p_tmp->p_next;
                else
                    p_head = p_head->p_next;
                break;
            }
            p_prev = p_tmp;
            p_tmp = p_tmp->p_next;
        }
        hashmap_put( p_this->notifyServiceMap, key, p_head );
    }
}

static int notify_dispatch( jsonrpc_server_t *p_this,
                            const char *psz_notify_service,
                            struct json_object *p_notify )
{
    const char *psz_notify = json_object_to_json_string( p_notify );
    uint32_t i_notify = strlen( psz_notify ) + 1;

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
        assert( p_head->i_state == CONN_HANDSHAKED );
        if ( p_head->p_res->i_buffer + 5 + i_notify >= p_head->p_res->i_maxlen )
        {
            p_head->p_res = block_Realloc( p_head->p_res, i_notify );
            if ( !p_head->p_res )
            {
                log_Err( "no memory" );
                return JSONRPC_ERR_NOMEM;
            }
        }
        block_t *p_res = p_head->p_res;
        p_res->p_buffer[ p_res->i_buffer ] = '$';
        uint32_t i_len = htonl( i_notify );
        memcpy( p_res->p_buffer + p_res->i_buffer + 1, &i_len, 4 );
        p_res->i_buffer += 5;
        memcpy( p_res->p_buffer + p_res->i_buffer,
                psz_notify, i_notify );
        p_res->i_buffer += i_notify;
        // if process_write return -1, EPOLLOUT or EPOLLHUP will process
        // the unfinished task, depends on errno
        process_write( p_head->i_sockfd, p_res );

        p_head = p_head->p_next;

        if ( p_res->i_buffer == 0 )
            log_Dbg( "dispatched a notify: %s", psz_notify );
    }
    return 0;
}

static int jsonrpc_server_exit( jsonrpc_server_t *p_this )
{
    log_Dbg( "jsonrpc server exit" );

    if ( p_this->psz_bind_file )
    {
        unlink( p_this->psz_bind_file );
        free( p_this->psz_bind_file );
    }

    for ( int i = 0; i < p_this->i_supportedNotifyService; i++ )
        free( p_this->ppsz_supportedNotifyService[i] );
    free( p_this->ppsz_supportedNotifyService );

    if ( p_this->tcpsock != -1 )
        close( p_this->tcpsock );
    if ( p_this->unixsock != -1 )
        close( p_this->unixsock );

    hashmap_free( p_this->hashmap );
    hashmap_free( p_this->classmap );
    hashmap_free( p_this->notifyServiceMap );

    p_this->b_initialized = false;
    return 0;
}

static int open_tcp_socket( jsonrpc_server_t *p_this, const char *psz_name,
                            int i_port )
{

    if ( ( p_this->tcpsock = socket( AF_INET, SOCK_STREAM, 0 ) ) < 0 )
    {
        log_Err( "open network socket failed (%s)", strerror( errno ) );
        return -1;
    }

    int i_reuseaddr = 1;
    if ( setsockopt( p_this->tcpsock, SOL_SOCKET, SO_REUSEADDR,
                     &i_reuseaddr, sizeof(int) ) < 0 )
    {
        log_Err( "set socket SO_REUSEADDR failed (%s)", strerror( errno ) );
        return -1;
    }

    if ( socket_listen( p_this->tcpsock, psz_name, i_port ) < 0 )
    {
        log_Err( "socket_listen on %s:%d failed (%s)", psz_name, i_port,
                 strerror( errno ) );
        return -1;
    }

    return 0;
}

static int open_unix_socket( jsonrpc_server_t *p_this, const char *psz_file )
{

    if ( (p_this->unixsock = socket( AF_UNIX, SOCK_STREAM, 0 )) < 0 )
    {
        log_Err( "open unix socket failed (%s)", strerror( errno ) );
        return -1;
    }

    // remove exited file, or else bind will fail
    unlink( psz_file );

    struct sockaddr_un addr;
    addr.sun_family = AF_UNIX;
    strncpy( addr.sun_path, psz_file, sizeof(addr.sun_path) );
    if ( bind( p_this->unixsock, &addr, SUN_LEN(&addr) ) < 0 )
    {
        log_Err( "bind unix socket failed (%s)", strerror( errno ) );
        return -1;
    }

    if ( listen( p_this->unixsock, LISTEN_BACKLOG ) < 0 )
    {
        log_Err( "listen unix socket failed (%s)", strerror( errno ) );
        return -1;
    }

    return 0;
}

int jsonrpc_server_init( jsonrpc_server_t *p_this )
{
    p_this->b_initialized = false;
    p_this->hashmap = NULL;
    p_this->classmap = NULL;
    p_this->tcpsock = -1;
    p_this->unixsock = -1;
    p_this->psz_bind_file = NULL;
    p_this->ppsz_supportedNotifyService = NULL;
    p_this->i_supportedNotifyService = 0;
    p_this->notifyServiceMap = NULL;

    p_this->hashmap = hashmap_create(101);
    p_this->classmap = hashmap_create(101);
    p_this->notifyServiceMap = hashmap_create(101);

    p_this->pf_register_function = register_function;
    p_this->pf_register_member_function = register_member_function;
    p_this->pf_register_class_object = register_class_object;
    p_this->pf_register_notify_services = register_notify_services;
    p_this->pf_serve = serve;
    p_this->pf_exit = jsonrpc_server_exit;
    // user specific
    p_this->pf_request_IsComplete = __json_request_IsComplete;
    p_this->pf_handle_handshake = handle_handshake;
    p_this->pf_handle_request = handle_request;
    p_this->pf_get_request = NULL;
    p_this->pf_on_client_connected = NULL;
    p_this->pf_on_client_closed = NULL;
    p_this->pf_on_processed = NULL;
    p_this->pf_notify_dispatch = notify_dispatch;

    signal( SIGINT, handle_signal );
    signal( SIGTERM, handle_signal );
    signal( SIGPIPE, SIG_IGN );

    // all sockets are set non-block, so there's no need to set timeout.
    p_this->b_initialized = true;
    return 0;
}

int jsonrpc_server_addListener( jsonrpc_server_t *p_this, int i_sock_flag, ... )
{
    if ( !p_this->b_initialized )
    {
        log_Err( "jsonrpc server object has not been initialized" );
        return -1;
    }

    va_list args;
    va_start( args, i_sock_flag );

    if ( i_sock_flag == AF_INET || i_sock_flag == PF_INET )
    {
        if ( p_this->tcpsock != -1 )
        {
            log_Err( "jsonrpc server addListener failed: "
                     "tcp socket already exists" );
            return -1;
        }
        const char *psz_name = (const char*)va_arg( args, const char * );
        int i_port = (int)va_arg( args, int );
        if ( open_tcp_socket( p_this, psz_name, i_port ) < 0 )
            return -1;
    }
    else if ( i_sock_flag == AF_UNIX || i_sock_flag == PF_UNIX )
    {
        if ( p_this->unixsock != -1 )
        {
            log_Err( "jsonrpc server addListener failed: "
                     "unix socket already exists" );
            return -1;
        }
        const char *psz_file = (const char*)va_arg( args, const char * );
        p_this->psz_bind_file = strdup( psz_file );
        if ( open_unix_socket( p_this, psz_file ) < 0 )
            return -1;
    }
    va_end( args );

    return 0;
}

