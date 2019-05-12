#include "json_server.h"

void hello( struct json_object *p_params, struct json_object *p_response )
{
    printf( "hello() entered" );
    struct json_object *p_obj, *p_ret;

    p_obj = json_object_array_get_idx( p_params, 0 );
    const char *psz_val = json_object_get_string( p_obj );
    const char *psz_return = psz_val;
    p_ret = json_object_new_string( psz_return );

    json_object_object_add( p_response, "result", p_ret );
    printf( "hello() exited" );
}


int main()
{
	json_server *p_server = json_server_new();

	p_server->pf_create_connection( p_server, AF_INET, "0.0.0.0", 80 );

	p_server->type = HTTP;

	p_server->pf_register_operator( p_server, "http_test", hello );

	p_server->pf_main_loop( p_server );

	p_server->pf_exit( p_server );

	p_server->pf_destory( p_server );

}


