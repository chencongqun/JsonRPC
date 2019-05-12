// file : jsonrpc_utils.c
// auth : lagula
// date : 2012-6-14
// desc : tool functions for jsonrpc
//

#include <ctype.h>
#include <arpa/inet.h>
#include <string.h>
#include <json/json.h>
#include "log.h"
#include "common.h"
#include "jsonrpc_utils.h"



bool json_request_IsComplete( block_t *p_req, size_t *pi_len )
{
    if ( p_req->i_buffer >= MAX_REQUEST_LEN )
    {
        log_Warn( "received request more than %d bytes, may be attacked" );
        return true;
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
                *pi_len = i + 2;
                return true;
            }
        }
    }
    return false;
}



void jsoncpy_bool( bool *p_bool, struct json_object *p_obj )
{
    *p_bool = json_object_get_boolean( p_obj );
}

void jsoncpy_double( double *p_double, struct json_object *p_obj )
{
    *p_double = json_object_get_double( p_obj );
}

void jsoncpy_int( int *p_int, struct json_object *p_obj )
{
    *p_int = json_object_get_int( p_obj );
}

void jsoncpy_ushort( uint16_t* p_short, struct json_object *p_obj )
{
    *p_short = (uint16_t)json_object_get_int( p_obj );
}

void jsoncpy_ulong( unsigned long *p_long, struct json_object *p_obj )
{
    *p_long = (unsigned long)json_object_get_int( p_obj );
}

void jsoncpy_string( char *psz_string, int i_bytes, struct json_object *p_obj )
{
    strncpy( psz_string, json_object_get_string( p_obj ), i_bytes - 1 );
    psz_string[ i_bytes - 1 ] = '\0';
}

int jsoncpy( void* p_buf, int i_buf, struct json_object *p_obj )
{
    if ( json_object_is_type( p_obj, json_type_boolean ) )
    {
        jsoncpy_bool( (bool *)p_buf, p_obj );
    }
    else if ( json_object_is_type( p_obj, json_type_double ) )
    {
        jsoncpy_double( (double *)p_buf, p_obj );
    }
    else if ( json_object_is_type( p_obj, json_type_int ) )
    {
        jsoncpy_int( (int *)p_buf, p_obj );
    }
    else if ( json_object_is_type( p_obj, json_type_string ) )
    {
        jsoncpy_string( (char *)p_buf, i_buf, p_obj );
    }
    else
        return -1;

    return 0;
}


