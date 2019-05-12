// file : main.c
// auth : lagula
// date : 2012-6-7
// desc : implement json rpc, jsonrpc.Server, jsonrpc.ServerProxy.
//

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <assert.h>
#include <pthread.h>
#include "jsonrpc_server.h"
#include "jsonrpc_client.h"
#include "log.h"

struct person
{
    char name[16];
    char phones[3][16];
};

struct house
{
    char addr[128];
    struct person a;
};

void hello( struct json_object *p_params, struct json_object *p_response )
{
    log_Dbg( "hello() entered" );
    struct json_object *p_obj, *p_ret;

    p_obj = json_object_array_get_idx( p_params, 0 );
    const char *psz_val = json_object_get_string( p_obj );
    const char *psz_return = psz_val;
    p_ret = json_object_new_string( psz_return );

    json_object_object_add( p_response, "result", p_ret );
    log_Dbg( "hello() exited" );
}

void getHouse( struct json_object *p_params, struct json_object *p_response )
{
    log_Dbg( "getHouse() entered" );
    struct json_object *p_ret;
    p_ret = json_object_new_array();

    json_object_array_add( p_ret, json_object_new_string("ShenLan streat #1"));
    struct json_object *p_dict, *p_person;
    p_dict = json_object_new_object();
    p_person = json_object_new_array();
    json_object_object_add( p_dict, "person", p_person );
    json_object_array_add( p_person, json_object_new_string( "Jason" ) );
    json_object_array_add( p_person, json_object_new_string( "15912344321" ) );
    json_object_array_add( p_ret, p_dict );

    json_object_object_add( p_response, "result", p_ret );
    log_Dbg( "getHouse() exited" );
}

void hello_notify( void *p_object,
                   struct json_object *p_params,
                   struct json_object *p_response )
{
    log_Dbg( "hello_notify() entered" );
    jsonrpc_server_t *p_server = (jsonrpc_server_t *)p_object;
    struct json_object *p_obj, *p_ret;

    p_obj = json_object_array_get_idx( p_params, 0 );
    const char *psz_val = json_object_get_string( p_obj );
    const char *psz_return = psz_val;
    p_ret = json_object_new_string( psz_return );

    json_object_object_add( p_response, "result", p_ret );

    // notify 1
    struct json_object *p_notify = json_object_new_object();
    json_object_object_add( p_notify, "type",
                            json_object_new_string("hello_received") );
    json_object_object_add( p_notify, "message",
                            json_object_new_string( "notify1" ) );
    p_server->pf_notify_dispatch( p_server, "hello_received", p_notify );

    // notify 2
    json_object_object_del( p_notify, "message" );
    json_object_object_add( p_notify, "message",
                            json_object_new_string( "notify2" ) );
    p_server->pf_notify_dispatch( p_server, "hello_received", p_notify );
    json_object_put( p_notify );

    log_Dbg( "hello_notify() exited" );
}



int test_local()
{
    jsonrpc_server_t server;
    jsonrpc_server_init( &server );
    if ( jsonrpc_server_addListener( &server, AF_UNIX, "/tmp/unixsock" ) < 0 )
    {
        log_Err( "add listener failed" );
        server.pf_exit( &server );
        return -1;
    }

    server.pf_register_function( &server, "hello", hello );
    server.pf_register_function( &server, "getHouse", getHouse );
    //server.pf_serve( &server );

    char* request1 = "{\"method\": \"hello\", \"params\": [\"This is a test\"]}";
    block_t *p_req = block_Alloc(4096);
    memcpy( p_req->p_buffer, request1, strlen(request1) + 1 );
    p_req->i_buffer = strlen( request1 ) + 1;
    block_t *p_res = block_Alloc(4096);
    server.pf_handle_request( &server, p_req, p_res );

    char *psz_response = (char *)p_res->p_buffer;
    printf( "hello return : %s\n", psz_response );

    p_req->i_buffer = 0;
    p_res->i_buffer = 0;

    char* request2 = "{\"method\" : \"getHouse\", \"params\" : []}";
    memcpy( p_req->p_buffer, request2, strlen(request2) + 1 );
    p_req->i_buffer = strlen( request2 ) + 1;
    server.pf_handle_request( &server, p_req, p_res );
    psz_response = (char *)p_res->p_buffer;
    printf( "getHouse return : %s\n", psz_response );

    p_req->i_buffer = 0;
    p_res->i_buffer = 0;

    block_Release( p_req );
    block_Release( p_res );

    server.pf_exit( &server );
    return 0;
}


void *test_unix_client( void *p_void )
{
    log_Dbg( "test_unix_client enter" );

    jsonrpc_client_t serverProxy;
    if ( jsonrpc_client_init( &serverProxy, AF_UNIX, "/tmp/unixsock_s" ) < 0 )
    {
        log_Err( "init json rpc client failed (%s)", strerror( errno ) );
        return NULL;
    }

    struct json_object *p_params, *p_response;
    p_params = json_object_new_array();
    json_object_array_add( p_params, json_object_new_string( "hello world" ) );
    p_response = serverProxy.pf_call( &serverProxy, "hello", p_params );
    // no need to free p_params, pf_call will do it.
    struct json_object *p_result;
    struct lh_table *p_table = json_object_get_object( p_response );
    if ( lh_table_lookup( p_table, "error" ) )
    {
        p_result = json_object_object_get( p_response, "error" );
        log_Err( "call hello return error: %s",
                 json_object_get_string( p_result ) );
    }
    else if ( lh_table_lookup( p_table, "result" ) )
    {
        p_result = json_object_object_get( p_response, "result" );
        log_Dbg( "call hello return: %s", json_object_get_string( p_result ) );
    }
    //json_object_put( p_params );
    json_object_put( p_response );

    p_response = serverProxy.pf_call( &serverProxy, "getHouse", NULL );
    p_table = json_object_get_object( p_response );
    if ( lh_table_lookup( p_table, "error" ) )
    {
        p_result = json_object_object_get( p_response, "error" );
        log_Err( "call getHouse return error: %s",
                 json_object_get_string( p_result ) );
    }
    else
    {
        p_result = json_object_object_get( p_response, "result" );
        struct json_object *p_obj;
        p_obj = json_object_array_get_idx( p_result, 0 );
        log_Dbg( "getHouse address %s", json_object_get_string( p_obj ) );
        p_obj = json_object_array_get_idx( p_result, 1 );
        assert( json_object_is_type( p_obj, json_type_object ) );
        struct json_object *p_tmp;
        p_tmp = json_object_object_get( p_obj, "person" );
        struct json_object *p_name, *p_phone;
        p_name = json_object_array_get_idx( p_tmp, 0 );
        log_Dbg( "getHouse person name %s", json_object_get_string( p_name ) );
        p_phone = json_object_array_get_idx( p_tmp, 1 );
        log_Dbg( "getHouse person phone %s", json_object_get_string( p_phone ));
    }
    json_object_put( p_response );

    serverProxy.pf_exit( &serverProxy );
    log_Dbg( "test_unix_client thread exit" );

    return NULL;
}

int test_UNIX()
{
    jsonrpc_server_t server;
    jsonrpc_server_init( &server );
    if ( jsonrpc_server_addListener( &server, AF_UNIX, "/tmp/unixsock_s" ) < 0 )
    {
        log_Err( "add event listener failed (%s)", strerror( errno ) );
        server.pf_exit( &server );
        return -1;
    }

    server.pf_register_function( &server, "hello", hello );
    server.pf_register_function( &server, "getHouse", getHouse );

    pthread_t pid;
    pthread_create( &pid, NULL, test_unix_client, NULL );

    server.pf_serve( &server );

    pthread_join( pid, NULL );

    server.pf_exit( &server );

    return 0;
}

void *test_tcp_client( void *p_void )
{
    log_Dbg( "test_tcp_client enter");

    jsonrpc_client_t serverProxy;
    if ( jsonrpc_client_init( &serverProxy, AF_INET, "127.0.0.1", 8900 ) < 0 )
    {
        log_Err( "init json rpc client failed (%s)", strerror( errno ) );
        return NULL;
    }

    struct json_object *p_params, *p_response;
    p_params = json_object_new_array();
    json_object_array_add( p_params, json_object_new_string( "hello world" ) );
    p_response = serverProxy.pf_call( &serverProxy, "hello", p_params );
    // no need to free p_params, pf_call will do it.
    struct json_object *p_result;
    struct lh_table *p_table = json_object_get_object( p_response );
    if ( lh_table_lookup( p_table, "error" ) )
    {
        p_result = json_object_object_get( p_response, "error" );
        log_Err( "call hello return error: %s",
                 json_object_get_string( p_result ) );
    }
    else if ( lh_table_lookup( p_table, "result" ) )
    {
        p_result = json_object_object_get( p_response, "result" );
        log_Dbg( "call hello return: %s", json_object_get_string( p_result ) );
    }
    //json_object_put( p_params );
    json_object_put( p_response );

    p_response = serverProxy.pf_call( &serverProxy, "getHouse", NULL );
    p_table = json_object_get_object( p_response );
    if ( lh_table_lookup( p_table, "error" ) )
    {
        p_result = json_object_object_get( p_response, "error" );
        log_Err( "call getHouse return error: %s",
                 json_object_get_string( p_result ) );
    }
    else
    {
        p_result = json_object_object_get( p_response, "result" );
        struct json_object *p_obj;
        p_obj = json_object_array_get_idx( p_result, 0 );
        log_Dbg( "getHouse address %s", json_object_get_string( p_obj ) );
        p_obj = json_object_array_get_idx( p_result, 1 );
        assert( json_object_is_type( p_obj, json_type_object ) );
        struct json_object *p_tmp;
        p_tmp = json_object_object_get( p_obj, "person" );
        struct json_object *p_name, *p_phone;
        p_name = json_object_array_get_idx( p_tmp, 0 );
        log_Dbg( "getHouse person name %s", json_object_get_string( p_name ) );
        p_phone = json_object_array_get_idx( p_tmp, 1 );
        log_Dbg( "getHouse person phone %s", json_object_get_string( p_phone ));
    }
    json_object_put( p_response );

    serverProxy.pf_exit( &serverProxy );
    log_Dbg( "test_tcp_client thread exit" );

    return NULL;
}

int test_TCP()
{
    jsonrpc_server_t server;
    jsonrpc_server_init( &server );
    if ( jsonrpc_server_addListener( &server, AF_INET, "0.0.0.0", 8900 ) < 0 )
    {
        log_Err( "add listener failed (%s)", strerror( errno ) );
        server.pf_exit( &server );
        return -1;
    }

    server.pf_register_function( &server, "hello", hello );
    server.pf_register_function( &server, "getHouse", getHouse );

    pthread_t pid;
    pthread_create( &pid, NULL, test_tcp_client, NULL );

    server.pf_serve( &server );

    pthread_join( pid, NULL );

    server.pf_exit( &server );

    return 0;
}

int test_both()
{
    jsonrpc_server_t server;
    jsonrpc_server_init( &server );
    if ( jsonrpc_server_addListener( &server, AF_INET, "0.0.0.0", 8900 ) < 0 )
    {
        log_Err( "add listener failed (%s)", strerror( errno ) );
        server.pf_exit( &server );
        return -1;
    }
    if ( jsonrpc_server_addListener( &server, AF_UNIX, "/tmp/unixsock_s" ) < 0 )
    {
        log_Err( "add listener failed" );
        return -1;
    }

    server.pf_register_function( &server, "hello", hello );
    server.pf_register_function( &server, "getHouse", getHouse );

    pthread_t pid;
    pthread_create( &pid, NULL, test_tcp_client, NULL );
    pthread_t pid2;
    pthread_create( &pid2, NULL, test_unix_client, NULL );

    server.pf_serve( &server );

    pthread_join( pid, NULL );
    pthread_join( pid2, NULL );

    server.pf_exit( &server );
    return 0;
}

void test_hashmap()
{
    hashmap hashmap = hashmap_create(101);
    hashmap_key_t key;
    key.u.i_int32 = 1;
    key.type = 'l';
    hashmap_put( hashmap, key, "one" );
    char *psz_val = hashmap_get( hashmap, key );
    assert( !strcmp( psz_val, "one" ) );

    int value = 1;
    key.u.psz_string = "one";
    key.type = 's';
    hashmap_put( hashmap, key, &value );
    hashmap_iterator it = hashmap_iterate( hashmap );
    while ( hashmap_next( &it ) != NULL )
    {
        if ( it.key.type == 's' )
        {
            int v = *(int*)hashmap_get( hashmap, it.key );
            assert( *(int*)it.p_val == v );
            printf( "key %s --> value %d\n", it.key.u.psz_string, v );
        }
        else
        {
            char *psz_val = (char*)hashmap_get( hashmap, it.key );
            assert( !strcmp( psz_val, (char*)it.p_val ) );
            printf( "key %d --> value %s\n", it.key.u.i_int32, psz_val );
        }
    }
    assert( hashmap_get_len( hashmap ) == 2 );

    // pop ('one', 1)
    value = *(int*)hashmap_pop( hashmap, key, NULL );
    printf( "pop item %d", value );
    assert( hashmap_get_len( hashmap ) == 1 );

    key.type = 'l';
    key.u.i_int32 = 1;
    hashmap_pop( hashmap, key, NULL );
    assert( hashmap_get_len( hashmap ) == 0 );
    it = hashmap_iterate( hashmap );
    assert( hashmap_next( &it ) == NULL );

    // free value
    hashmap_put( hashmap, key, strdup("test free") );
    it = hashmap_iterate( hashmap );
    while ( hashmap_next( &it ) )
        free( it.p_val );
    hashmap_pop( hashmap, key, NULL );
    assert( hashmap_get_len( hashmap ) == 0 );

    // pop in interate loop
    for ( int j = 0; j < 100; j++ )
    {
        assert( hashmap_get_len( hashmap ) == 0 );

        for ( int i = 0; i < 100; i++ )
        {
            int *p_n = malloc( sizeof( int ) );
            *p_n = rand();
            key.type = 'l';
            key.u.i_int32 = *p_n;
            hashmap_put( hashmap, key, p_n );
        }

        char *psz_names[3] = { "item1", "item2", "item3" };
        for ( int i = 0; i < 3; i++ )
        {
            key.type = 's';
            key.u.psz_string = psz_names[i];
            hashmap_put( hashmap, key, psz_names[i] );
        }
        it = hashmap_iterate( hashmap );
        while ( hashmap_next( &it ) )
        {
            if ( it.key.type == 's' )
                if ( !strcmp( it.key.u.psz_string, "item2" ) )
                    hashmap_pop( hashmap, it.key, NULL );

        }
        it = hashmap_iterate( hashmap );
        while ( hashmap_next( &it ) )
        {
            if ( it.key.type == 'l' )
                log_Dbg( "%d", *(int *)it.p_val );
            else if ( it.key.type == 's' )
                log_Dbg( "%s", it.p_val );
        }

        it = hashmap_iterate( hashmap );
        while ( hashmap_next( &it ) )
        {
            if ( it.key.type == 'l' )
                free( it.p_val );
            hashmap_pop( hashmap, it.key, NULL );
        }
    }

    hashmap_free( hashmap );
}

void get_house_person( void *p_obj, struct json_object *p_params,
                       struct json_object *p_response )
{
    struct house *p_house = (struct house *)p_obj;
    struct json_object *p_result = json_object_new_object();
    json_object_object_add( p_result, "name",
                            json_object_new_string( p_house->a.name ) );
    struct json_object *p_phones = json_object_new_array();
    for ( int i = 0; i < 3; i++ )
        json_object_array_add( p_phones,
                               json_object_new_string( p_house->a.phones[i] ) );
    json_object_object_add( p_result, "phones", p_phones );
    json_object_object_add( p_response, "result", p_result );
}

void *test_member_function( void *p_void )
{
    log_Dbg( "test_member thread entered" );
    jsonrpc_client_t serverProxy;
    if ( jsonrpc_client_init( &serverProxy, AF_INET, "127.0.0.1", 8900 ) < 0 )
    {
        log_Err( "init json rpc client failed (%s)", strerror( errno ) );
        return NULL;
    }

    struct json_object *p_response;
    p_response = serverProxy.pf_call( &serverProxy, "House.get_house_person",
                                      NULL );
    printf( "call House.get_house_person return: %s\n",
            json_object_to_json_string( p_response ) );
    json_object_put( p_response );

    serverProxy.pf_exit( &serverProxy );
    log_Dbg( "test_member thread exited" );
    return 0;
}

int test_register_member_function()
{
    jsonrpc_server_t server;
    jsonrpc_server_init( &server );
    if ( jsonrpc_server_addListener( &server, AF_INET, "0.0.0.0", 8900 ) < 0 )
    {
        log_Err( "add listener failed (%s)", strerror( errno ) );
        server.pf_exit( &server );
        return -1;
    }

    struct house h;
    strcpy( h.a.name, "Robot" );
    strcpy( h.a.phones[0], "123" );
    strcpy( h.a.phones[1], "456" );
    strcpy( h.a.phones[2], "789" );
    server.pf_register_class_object( &server, "House", &h );
    server.pf_register_member_function( &server, "House.get_house_person",
                                        get_house_person );

    pthread_t pid;
    pthread_create( &pid, NULL, test_member_function, NULL );

    server.pf_serve( &server );

    pthread_join( pid, NULL );

    server.pf_exit( &server );
    return 0;
}

void test_env( char *envp[] )
{
    char *p_env = envp[0];
    int i = 0;
    while ( p_env )
    {
        printf( "env: %s\n", p_env );
        p_env = envp[i];
        i += 1;
    }
    char *p[i];
#ifdef __LP64__
    printf( "sizeof p[]: %lu\n", sizeof(p) );
#else
    printf( "sizeof p[]: %d\n", sizeof(p) );
#endif
}

void *test_notify_client( void *p_void )
{
    log_Dbg( "test_notify_client thread entered" );
    jsonrpc_client_t serverProxy;
    if ( jsonrpc_client_init( &serverProxy, AF_INET, "127.0.0.1", 8900 ) < 0 )
    {
        log_Err( "init json rpc client failed (%s)", strerror( errno ) );
        return NULL;
    }

    struct json_object *p_params;
    p_params = json_object_new_array();
    json_object_array_add( p_params, json_object_new_string( "hello world" ) );
    if ( serverProxy.pf_notify( &serverProxy, "hello", p_params ) < 0 )
    {
        log_Err( "notify hello failed" );
        return NULL;
    }

    serverProxy.pf_exit( &serverProxy );
    log_Dbg( "test_notify_client thread exit" );
    return NULL;
}

int test_notify()
{
    jsonrpc_server_t server;
    jsonrpc_server_init( &server );
    if ( jsonrpc_server_addListener( &server, AF_INET, "0.0.0.0", 8900 ) < 0 )
    {
        log_Err( "add listener failed (%s)", strerror( errno ) );
        server.pf_exit( &server );
        return -1;
    }

    server.pf_register_function( &server, "hello", hello );

    pthread_t pid;
    pthread_create( &pid, NULL, test_notify_client, NULL );

    server.pf_serve( &server );

    pthread_join( pid, NULL );

    server.pf_exit( &server );
    return 0;
}

int test_ws_server()
{
    ws_jsonrpc_server_t server;
    ws_jsonrpc_server_init( &server );
    jsonrpc_server_t *p_server = (jsonrpc_server_t *)&server;
    if ( jsonrpc_server_addListener( p_server, AF_INET, "0.0.0.0", 80 ) < 0 )
    {
        log_Err( "add listener failed (%s)", strerror( errno ) );
        p_server->pf_exit( p_server );
        return -1;
    }

    p_server->pf_register_function( p_server, "hello", hello );
    p_server->pf_serve( p_server );

    p_server->pf_exit( p_server );
    return 0;
}

void *test_JsonPlusWs_client( void *p_void )
{
    log_Dbg( "test_JsonPlusWs_client thread entered" );
    jsonrpc_client_t serverProxy;
    if ( jsonrpc_client_init( &serverProxy, AF_INET, "127.0.0.1", 80 ) < 0 )
    {
        log_Err( "init json rpc client failed (%s)", strerror( errno ) );
        return NULL;
    }

    struct json_object *p_params, *p_response, *p_result;
    p_params = json_object_new_array();
    json_object_array_add( p_params, json_object_new_string( "hello world" ) );
    p_response = serverProxy.pf_call( &serverProxy, "hello", p_params );
    if ( json_object_object_get( p_response, "error" ) )
    {
        p_result = json_object_object_get( p_response, "error" );
        log_Err( "call hello failed: %s", json_object_get_string(p_result) );
        return NULL;
    }
    else
    {
        p_result = json_object_object_get( p_response, "result" );
        log_Dbg( "call hello return: %s", json_object_to_json_string(p_result));
    }

    serverProxy.pf_exit( &serverProxy );
    log_Dbg( "test_JsonPlusWs_client thread exit" );
    return NULL;
}

int test_jsonPlusWs_server()
{
    JsonrpcPlusWs_server_t server;
    JsonrpcPlusWs_server_init( &server );
    jsonrpc_server_t *p_server = (jsonrpc_server_t*)&server;
    if ( jsonrpc_server_addListener( p_server, AF_INET, "0.0.0.0", 80 ) < 0 )
    {
        log_Err( "add listener failed (%s)", strerror( errno ) );
        p_server->pf_exit( p_server );
        return -1;
    }

    p_server->pf_register_function( p_server, "hello", hello );

    pthread_t pid;
    pthread_create( &pid, NULL, test_JsonPlusWs_client, NULL );

    p_server->pf_serve( p_server );

    pthread_join( pid, NULL );

    p_server->pf_exit( p_server );
    return 0;
}

void *test_notifyService_client( void *p_void )
{
    log_Dbg( "test_notifyService_client thread entered" );
    jsonrpc_client_t serverProxy;
    if ( jsonrpc_client_init( &serverProxy, AF_INET, "127.0.0.1", 80 ) < 0 )
    {
        log_Err( "init json rpc client failed (%s)", strerror( errno ) );
        return NULL;
    }
    jsonrpc_client_t subscriber;
    char *ppsz_notifyService[] = {"hello_received"};
    if ( jsonrpc_client_subscribe_init( &subscriber, AF_INET, "127.0.0.1", 80,
                                        ppsz_notifyService, 1 ) < 0 )
    {
        log_Err( "init json rpc client failed (%s)", strerror( errno ) );
        return NULL;
    }

    struct json_object *p_params, *p_response, *p_result;
    p_params = json_object_new_array();
    json_object_array_add( p_params, json_object_new_string( "hello world" ) );
    p_response = serverProxy.pf_call( &serverProxy,
                                      "Server.hello_notify", p_params );
    if ( json_object_object_get( p_response, "error" ) )
    {
        p_result = json_object_object_get( p_response, "error" );
        log_Err( "call hello failed: %s", json_object_get_string(p_result) );
        return NULL;
    }
    else
    {
        p_result = json_object_object_get( p_response, "result" );
        log_Dbg( "call hello return: %s", json_object_to_json_string(p_result));
    }

    struct json_object *p_notify =
        subscriber.pf_get_notify( &subscriber, true, 0 );
    p_result = json_object_object_get( p_notify, "type" );
    const char *psz_type = json_object_get_string( p_result );
    assert( !strcmp( psz_type, "hello_received" ) );
    p_result = json_object_object_get( p_notify, "message" );
    psz_type = json_object_get_string( p_result );
    assert( !strcmp( psz_type, "notify1" ) );
    json_object_put( p_notify );

    p_notify = subscriber.pf_get_notify( &subscriber, true, 0 );
    p_result = json_object_object_get( p_notify, "message" );
    psz_type = json_object_get_string( p_result );
    assert( !strcmp( psz_type, "notify2" ) );
    json_object_put( p_notify );

    serverProxy.pf_exit( &serverProxy );
    subscriber.pf_exit( &subscriber );
    log_Dbg( "test_notifyService_client thread exit" );
    return NULL;

}

int test_notifyService()
{
    jsonrpc_server_t server;
    jsonrpc_server_init( &server );
    jsonrpc_server_t *p_server = &server;
    if ( jsonrpc_server_addListener( p_server, AF_INET, "0.0.0.0", 80 ) < 0 )
    {
        log_Err( "add listener failed (%s)", strerror( errno ) );
        p_server->pf_exit( p_server );
        return -1;
    }

    const char *ppsz_notifyService[] = {"hello_received"};
    if ( p_server->pf_register_notify_services( p_server,
            ppsz_notifyService, 1 ) < 0 )
    {
        log_Err( "register notify services failed" );
        p_server->pf_exit( p_server );
        return -1;
    }

    p_server->pf_register_class_object( p_server, "Server", p_server );
    p_server->pf_register_member_function( p_server, "Server.hello_notify",
                                           hello_notify );

    pthread_t pid;
    pthread_create( &pid, NULL, test_notifyService_client, NULL );

    p_server->pf_serve( p_server );

    pthread_join( pid, NULL );

    p_server->pf_exit( p_server );
    return 0;
}


int main( int argc, char **argv, char **envp )
{
    int i_loglevel= LOG_Info;
    log_init(i_loglevel, LOGTYPE_STDERR );

    //test_local();
    //test_UNIX();
    //test_TCP();
    //test_both();
    test_hashmap();
    //test_register_member_function();
    //test_env( envp );
    //test_notify();
    //test_ws_server();
    //test_jsonPlusWs_server();
    //test_notifyService();

    return 0;
}


