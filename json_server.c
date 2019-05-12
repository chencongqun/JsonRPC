/*
  *  Author: ccq
  *  Version: 1.0
  *  Date: 2015-06-15
  *
  *  Descript: Implement the interfaces for RPC communication by json
  *
  *  Copyright @ 2015 SinoData. All rights reserved.
  */


#include "json_server.h"


static int socket_setblocking( int sock, int i_block )
{
	int i_flags = fcntl( sock, F_GETFL, 0 );
	if ( !i_block )
		i_flags |= O_NONBLOCK;
	else
		i_flags |= ~O_NONBLOCK;
	return fcntl( sock, F_SETFL, i_flags );
}

static int open_tcp_socket( json_server *p_this, const char *psz_ip, int i_port )
{
    unsigned long u_addr;

    if ( ( p_this->tcpsock = socket( AF_INET, SOCK_STREAM, 0 ) ) < 0 )
    {
        //printf( "open network socket failed (%s)", strerror( errno ) );
        return -1;
    }

    int i_reuseaddr = 1;
    if ( setsockopt( p_this->tcpsock, SOL_SOCKET, SO_REUSEADDR,
                     &i_reuseaddr, sizeof(int) ) < 0 )
    {
        //printf( "set socket SO_REUSEADDR failed (%s)", strerror( errno ) );
        return -1;
    }

    if ( inet_pton( AF_INET, psz_ip, &u_addr ) <= 0 )
        return -1;

    struct sockaddr_in addrin;
    addrin.sin_family = AF_INET;
    addrin.sin_port = htons( i_port );
    addrin.sin_addr.s_addr = u_addr;

    if ( bind( p_this->tcpsock, (struct sockaddr* )&addrin, sizeof(addrin) ) < 0 )
        return -1;

    if ( listen( p_this->tcpsock, 5 ) < 0 )
        return -1;

    return 0;
}

static int open_unix_socket( json_server *p_this, const char *psz_file )
{

    if ( (p_this->unixsock = socket( AF_UNIX, SOCK_STREAM, 0 )) < 0 )
    {
        //printf( "open unix socket failed (%s)", strerror( errno ) );
        return -1;
    }

    // remove exited file, or else bind will fail
    unlink( psz_file );

    struct sockaddr_un addr;
    addr.sun_family = AF_UNIX;
    strncpy( addr.sun_path, psz_file, sizeof(addr.sun_path) );
    if ( bind( p_this->unixsock, (struct sockaddr *)&addr, SUN_LEN(&addr) ) < 0 )
    {
        //printf( "bind unix socket failed (%s)", strerror( errno ) );
        return -1;
    }

    if ( listen( p_this->unixsock, LISTEN_BACKLOG ) < 0 )
    {
        //printf( "listen unix socket failed (%s)", strerror( errno ) );
        return -1;
    }

    return 0;
}

static void json_request_destory( json_request * p_request )
{
	if ( p_request->p_request_block )
			block_Release( p_request->p_request_block );
	if ( p_request->p_request_block )
		block_Release( p_request->p_request_block );
	free( p_request );
}

static struct json_object * json_request_parse( block_t *p_req )
{
	int package_len = 0;
	if ( p_req->i_buffer >= MAX_REQUEST_LEN )
    {
        //log_Warn( "received request more than %d bytes, may be attacked" );
        package_len = p_req->i_buffer;
        goto error;
    }

    int i_braces = 0;
    for ( int i = 0; i < p_req->i_buffer; i++ )
    {
        if ( p_req->p_buffer[i] == '{' )
            i_braces += 1;
        else if ( p_req->p_buffer[i] == '}' )
        {
            i_braces -= 1;
            if ( i_braces == 0 && p_req->p_buffer[i + 1] == '\0' )
            {
                package_len = i + 2;
                const char *psz_json = (char*)p_req->p_buffer;
				struct json_object * p_json_data = json_tokener_parse( psz_json );
				if ( is_error(p_json_data) || !json_object_is_type(p_json_data, json_type_object) )
    			{
					if ( !is_error(p_json_data) )
        				json_object_put( p_json_data );
					goto error;
				}
				memmove( p_req->p_buffer, p_req->p_buffer + package_len, p_req->i_buffer - package_len );
   				p_req->i_buffer -= package_len;
				return p_json_data;
            }
        }
    }
	
	return NULL;
	
error:
	memmove( p_req->p_buffer, p_req->p_buffer + package_len, p_req->i_buffer - package_len );
    p_req->i_buffer -= package_len;
    return NULL;
}

static json_request * json_request_create(PACKAGE_TYPE type)
{
	json_request *p_request = (json_request *)malloc(sizeof(json_request));
	if ( !p_request )
		return NULL;
	
	p_request->p_response_block = block_Alloc(8192);
	p_request->p_request_block = block_Alloc(8192);

	if ( !p_request->p_request_block || !p_request->p_response_block )
	{
		json_request_destory( p_request );
		return NULL;
	}

	if ( type == HTTP )
		p_request->pf_get_json_object = http_request_parse;
	else
		p_request->pf_get_json_object = json_request_parse;
		
	
	return p_request;
}

static int json_server_create_connection( json_server *p_this, int i_sock_flag, ... )
{
	
	va_list args;
	va_start( args, i_sock_flag );

	if ( i_sock_flag == AF_INET || i_sock_flag == PF_INET )
	{
		if ( p_this->tcpsock != 0 )
			return -1;

		const char * psz_ip = va_arg( args, const char * );
		int i_port = va_arg( args, int );
		if ( open_tcp_socket( p_this, psz_ip, i_port ) != 0 )
			return -1;
	}
	else if ( i_sock_flag == AF_UNIX || i_sock_flag == PF_UNIX )
	{
		if ( p_this->unixsock != 0 )
			return -1;

		const char *psz_file = va_arg(args, const char *);
		if ( open_unix_socket( p_this, psz_file ) != 0 )
			return -1;
	}
	
	va_end( args );
	
	return 0;
}

static int json_server_register_operator( json_server *p_this, const char *psz_method_name, pf_operator p_fn )
{
	hashmap_put( p_this->hm_operators, MAKE_STRING_KEY(psz_method_name), (hashmap_value)p_fn );
	return 0;
}

static int json_server_set_muti_worker ( json_server *p_this, int i_num_worker )
{
	

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
                //log_Err( "write failed (%s)", strerror(errno) );
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

static int process_read( int fd, block_t *p_req )
{
    int i_read;
	
    while ( true )
    {
        if ( p_req->i_buffer + 4096 >= p_req->i_maxlen )
        {
            p_req = block_Realloc( p_req, 4096 );
            if ( !p_req )
            {
                //log_Err( "no memory %s %d", __FILE__, __LINE__ );
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
                //log_Err( "read failed (%s), close connection",strerror( errno ) );
                break;
            }
        }
        else if ( i_read == 0 )
        {
            //log_Dbg( "peer closed connection while read" );
            // generate EPIPE and EPOLLHUG
            shutdown( fd, SHUT_RDWR );
            char c = 0;
            send( fd, &c, 1, 0 );
            break;
        }
        else
        {
            p_req->i_buffer += i_read;
        }
    }
	return 0;
}

static int process_request( json_server *p_this, json_request *p_request )
{
	//transform string to json
	struct json_object * p_request_data = p_request->pf_get_json_object( p_request->p_request_block );

	//handler request , improve it later
	if ( !p_request_data )
		return 0;

	struct json_object * p_params;
	pf_operator p_fn = NULL;
	struct json_object * p_response = json_object_new_object();

	json_object_object_foreach( p_request_data, psz_key, val )
    {
		if ( !strcmp( psz_key, "method" ) )
        {
            const char *psz_method = json_object_get_string( val );
			p_fn = (pf_operator)hashmap_get( p_this->hm_operators, MAKE_STRING_KEY(psz_method) );
			if ( !p_fn )
				goto end;
		}
		else if ( !strcmp( psz_key, "params" ) )
			p_params = val;
	}

	if ( p_fn )
		p_fn( p_params, p_response );

	const char * psz_response = json_object_to_json_string( p_response );
	block_t * p_resblock = p_request->p_response_block;
    if ( strlen(psz_response) + 1 >= p_resblock->i_maxlen )
    {
        p_resblock = block_Realloc( p_resblock, strlen(psz_response) + 1 );
        if ( !p_resblock )
            goto end;
    }
    memcpy( p_resblock->p_buffer, psz_response, strlen(psz_response) + 1 );
    p_resblock->i_buffer = strlen(psz_response) + 1;
end:
	json_object_put( p_request_data );
	json_object_put( p_response );
	return 0;
}



static int json_server_main_loop( json_server *p_this )
{
	struct epoll_event events[EPOLL_MAX_EVENT];
	int socks[2], i, i_ready, ret=-1;
	struct sockaddr_in client_addr;
	socklen_t addrlen = sizeof(client_addr);
	hashmap_iterator iterator; 
	int epfd = epoll_create(EPOLL_SIZE);
	if ( epfd < 0 )
		return -1;
	socks[0] = p_this->tcpsock;
	socks[1] = p_this->unixsock;
	for ( i=0; i<sizeof(socks)/sizeof(socks[0]); i++ )
	{
		if ( socks[i] > 0 )
		{
			socket_setblocking( socks[i], 0 );
			struct epoll_event event;
			event.events = EPOLLIN | EPOLLET;
			event.data.fd = socks[i];
			if ( epoll_ctl( epfd, EPOLL_CTL_ADD, socks[i], &event ) < 0 )
				return -1;
		}
	}

	while( !p_this->s_exit )
	{
		i_ready = epoll_wait( epfd, events, EPOLL_MAX_EVENT, EPOLL_TIMEOUT );
		if ( i_ready < 0 )
		{
			if ( errno == EINTR )
				continue;
			break;
		}
		else if ( i_ready > 0 )
		{
			for ( i=0; i<i_ready; i++ )
			{
				if ( events[i].data.fd == socks[0] || events[i].data.fd == socks[1] )
				{
					while ( true )
					{ 
						int client_fd = accept( events[i].data.fd, (struct sockaddr *)&client_addr, &addrlen );
						if ( client_fd < 0 )
							break;

						socket_setblocking( client_fd, 0 );
						struct epoll_event event;
						event.events = EPOLLIN | EPOLLET;
						event.data.fd = client_fd;
						if ( epoll_ctl( epfd, EPOLL_CTL_ADD, client_fd, &event ) < 0 )
							goto end;

						json_request *p_request = json_request_create(p_this->type);
						if ( !p_request )
							goto end;
						p_request->i_client_fd = client_fd;
						if ( !inet_ntop( AF_INET, &client_addr.sin_addr, p_request->psz_ip, 16 ) )
							goto end;
						hashmap_put( p_this->hm_requests, MAKE_INT32_KEY(client_fd), (hashmap_value)p_request );
					}
				}
				else if ( events[i].events & EPOLLIN )
				{
					int client_fd = events[i].data.fd;
					json_request *p_request = hashmap_get( p_this->hm_requests, MAKE_INT32_KEY( client_fd ) );
					// read data, http post may be out of memory, optimize later
					process_read( client_fd, p_request->p_request_block );
					process_request( p_this, p_request );
					process_write( client_fd, p_request->p_response_block );
				}
				else if ( events[i].events & EPOLLOUT )
				{
					int client_fd = events[i].data.fd;
                    json_request *p_request = hashmap_get( p_this->hm_requests, MAKE_INT32_KEY(client_fd) );
                    process_write( client_fd, p_request->p_response_block );
				}
				else if ( events[i].events & EPOLLHUP || events[i].events & EPOLLERR )
				{
					int client_fd = events[i].data.fd;
					if ( epoll_ctl( epfd, EPOLL_CTL_DEL, client_fd, &events[i] ) < 0 )
						goto end;
					close(client_fd);
					json_request * p_request = hashmap_pop( p_this->hm_requests, MAKE_INT32_KEY(client_fd) );
					if ( p_request )
						json_request_destory( p_request );
				}
			}
		}
	}

	ret = 0;
end:
	iterator = hashmap_iterate( p_this->hm_requests );
	while ( hashmap_next( &iterator ) )
		json_request_destory( (json_request *)iterator.p_val );
	close( epfd );
	return ret;
}

static void json_server_exit( json_server *p_this )
{
	p_this->s_exit = 1;
}

static void json_server_destory( json_server *p_this )
{
    if ( p_this->hm_operators )
			hashmap_free( p_this->hm_operators );
	if ( p_this->hm_requests )
		hashmap_free( p_this->hm_requests );
    free(p_this);
}

json_server *json_server_new()
{
    json_server *p_server = (json_server *)malloc( sizeof(json_server) );
	if ( !p_server )
		return NULL;
	
    memset( p_server, 0, sizeof( json_server ) );
	
    p_server->hm_operators = hashmap_create(101);
	p_server->hm_requests = hashmap_create(101);
	if ( p_server->hm_operators == NULL || p_server->hm_requests == NULL )
	{
		if ( p_server->hm_operators )
			hashmap_free( p_server->hm_operators );
		if ( p_server->hm_requests )
			hashmap_free( p_server->hm_requests );
		free( p_server );
		return NULL;
	}
	
    p_server->pf_register_operator = json_server_register_operator;
	p_server->pf_create_connection = json_server_create_connection;
    p_server->pf_set_muti_worker = json_server_set_muti_worker;
    p_server->pf_main_loop = json_server_main_loop;
	p_server->pf_exit = json_server_exit;
    p_server->pf_destory = json_server_destory;

    return p_server;
	
}




