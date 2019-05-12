#include <stdlib.h>
#include <json/json.h>
#include <string.h>
#include "fcgi_stdio.h"
#include "cgi-llist.h"
#include "cgi-lib.h"
#include "log.h"

void get_server_status( Request *r, struct json_object *p_response )
{
    char * psz_server_name = fcgi_getvar( r, "serverName" );
    char psz_cmd[256]= {0};
    char psz_buf[1024]= {0};
	char psz_version[32] = {0};
	char psz_time[32] = {0};
	char *psz_tmp = NULL;
	char *psz_tmp2 = NULL;
    sprintf( psz_cmd, "sudo service sino-server %s status", psz_server_name );
    SLOG_DEBUG( "[get_server_status]%s", psz_cmd );
    FILE *fd = popen( psz_cmd, "r" );
    fread( psz_buf, 1024, 1, fd );
    fclose( fd );

	if ( strstr( psz_buf, "started" ) != NULL )
        json_object_object_add( p_response, "status", json_object_new_string("Running") );
    else if( strstr( psz_buf, "stoped" ) != NULL )
        json_object_object_add( p_response, "status", json_object_new_string("Stop") );
    else
        json_object_object_add( p_response, "status", json_object_new_string("Unknow") );
	
	memset( psz_cmd, 0, 256 );
	sprintf( psz_cmd, "sudo /sinodata/bin/%s -v", psz_server_name );

	SLOG_DEBUG( "[get_server_status]cmd %s", psz_cmd );
	
	fd = popen( psz_cmd, "r" );
	memset( psz_buf, 0, 1024 );
	fread( psz_buf, 1024, 1, fd );
	fclose( fd );

	SLOG_DEBUG( "[get_server_status]return %s", psz_buf );
	
	if ( psz_tmp = strstr( (const char *)psz_buf, "Version" ) )
	{
		psz_tmp = psz_tmp + strlen("Version") + 1;
		psz_tmp2 = strstr( (const char *)psz_tmp, " " );
		psz_tmp2[0] = '\0';
		sprintf( psz_version, "%s", psz_tmp );
		psz_tmp2[0] = ' ';
	}

	if ( psz_tmp = strstr( (const char *)psz_buf, "Ltd." ) )
	{
		psz_tmp = psz_tmp + strlen("Ltd.") + 1;
		psz_tmp2 = strstr( (const char *)psz_tmp, " " );
		psz_tmp2++;
		psz_tmp2 = strstr( (const char *)psz_tmp2, " " );
		psz_tmp2[0] = '\0';
		sprintf( psz_time, "%s", psz_tmp );
	}

	SLOG_DEBUG( "[get_server_status]version %s %s", psz_version, psz_time );

	json_object_object_add( p_response, "version", json_object_new_string(psz_version) );
	json_object_object_add( p_response, "time", json_object_new_string(psz_time) );
	
    json_object_object_add( p_response, "code", json_object_new_int(0) );
    json_object_object_add( p_response, "message", json_object_new_string("Sucess!") );
}

void get_server_config( Request *r, struct json_object *p_response )
{
    char * psz_config_file = fcgi_getvar( r, "configFile" );

	//here must do something to prevent hostile attack
	
    FILE * fd = fopen( psz_config_file, "r" );
    if ( !fd )
        goto error;

    struct json_object *p_config_value = json_object_new_array();
    char psz_buf[1024] = {0};
    char psz_tmp[1024] = {0};
    int i=0;
    while ( fgets( psz_buf, 1024, fd ) )
    {
        memset( psz_tmp, 0, 1024 );
        int j = 0;
        for ( i=0; i<strlen(psz_buf); i++ )
        {
            if ( psz_buf[i] != '\0' && psz_buf[i]!='\n' && psz_buf[i]!=' ' )
                psz_tmp[j++] = psz_buf[i];
        }
        if ( strlen(psz_tmp) > 0 )
            json_object_array_add( p_config_value, json_object_new_string( psz_tmp ) );
        memset( psz_buf, 0, 1024 );
    }

    json_object_object_add( p_response, "config", p_config_value );
    json_object_object_add( p_response, "code", json_object_new_int(0) );
    json_object_object_add( p_response, "message", json_object_new_string("Sucess!") );
    return;
error:
    json_object_object_add( p_response, "code", json_object_new_int(-1) );
    json_object_object_add( p_response, "message", json_object_new_string("No Existed!") );
}

void set_server_config( Request *r, struct json_object *p_response )
{
	char *psz_server_name = fcgi_postvar( r, "configFile" );

	//here must do something to prevent hostile attack

	int i=0;
	char *psz_params = fcgi_postvar( r, "params" );

	FILE *fp = fopen( psz_server_name, "w+" );
	if ( !fp )
		goto error;

	while ( psz_params[i] != '\0' )
	{
		
	}

	json_object_object_add( p_response, "code", json_object_new_int(0) );
    json_object_object_add( p_response, "message", json_object_new_string("Sucess!") );
    return;
error:
    json_object_object_add( p_response, "code", json_object_new_int(-1) );
    json_object_object_add( p_response, "message", json_object_new_string("No Existed!") );
}

void update_server( Request *r, struct json_object *p_response )
{

}

void stop_start_server( Request *r, struct json_object *p_response )
{
    char *psz_server_name = fcgi_getvar( r, "serverName" );
    char *psz_opt = fcgi_getvar( r, "opt" );
    char psz_cmd[1024] = {0};
    char psz_buf[1024] = {0};


    if ( strcmp( psz_opt, "start" ) != 0 && strcmp( psz_opt, "stop" ) != 0 && strcmp( psz_opt, "restart" ) != 0 )
        goto error;
    sprintf( psz_cmd, "sudo service sino-server %s %s", psz_server_name, psz_opt );

	SLOG_DEBUG("[stop_start_server]cmd: %s", psz_cmd);

	system(psz_cmd);

	sprintf( psz_cmd, "" );
    FILE *fd = popen( psz_cmd, "r" );
    fread( psz_buf, 1024, 1, fd );
    fclose( fd );

	SLOG_DEBUG("[stop_start_server] %s", psz_buf);
	
    if ( strstr( psz_buf, "success" ) != NULL )
    {
        json_object_object_add( p_response, "code", json_object_new_int(0) );
        json_object_object_add( p_response, "message", json_object_new_string("Sucess!") );
    }
    else
    {
        json_object_object_add( p_response, "code", json_object_new_int(0) );
        json_object_object_add( p_response, "message", json_object_new_string("Sucess!") );
    }
    return;

error:
    json_object_object_add( p_response, "code", json_object_new_int(-1) );
    json_object_object_add( p_response, "message", json_object_new_string("Invaild Operator!") );

}

void system_handle( Request * r )
{
    char * psz_tag = fcgi_getvar( r, "tag" );
    struct json_object * p_response = json_object_new_object();
    if ( strcmp( psz_tag, "getStatus" ) == 0 )
    {
        get_server_status( r, p_response );
    }
    else if ( strcmp( psz_tag, "getConfig" ) == 0 )
    {
        get_server_config( r, p_response );
    }
    else if ( strcmp( psz_tag, "setConfig" ) == 0 )
    {
        set_server_config( r, p_response );
    }
    else if ( strcmp( psz_tag, "updateServer" ) == 0 )
    {
        update_server( r, p_response );
    }
    else if ( strcmp( psz_tag, "stopStart" ) == 0 )
    {
        stop_start_server( r, p_response );
    }
    else
    {
        json_object_object_add( p_response, "code", json_object_new_int(-1) );
        json_object_object_add( p_response, "message", json_object_new_string("Illegal Request!") );
    }
    const char *psz_buf = json_object_to_json_string( p_response );
    fcgi_header( r );
    fcgi_write( psz_buf, strlen(psz_buf), r );
    json_object_put( p_response );
}

int main()
{
    SLOG_DEBUG("server start!");
    FCGX_Init();
    while(1)
    {
        FCGX_Request *xRequest = (FCGX_Request *)malloc(sizeof(FCGX_Request));
        FCGX_InitRequest( xRequest, 0, 0 );
        if ( FCGX_Accept_r( xRequest ) < 0 )
            continue;
        Request request= {0};
        request.frequest = xRequest;

        request.header = 0;
        request.accept_gzip = 0;

        fcgi_request_init( &request );
        system_handle( &request );
        fcgi_request_free( &request );

        FCGX_Finish_r( request.frequest );
        free(request.frequest);
    }
    SLOG_DEBUG("server end!");
    return 0;
}


