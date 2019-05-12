// file : jsonrpc_utils.h
// auth : lagula
// date : 2012-6-14
// desc : tool functions for jsonrpc
//

#ifndef JSONRPC_UTILS_H
#define JSONRPC_UTILS_H


#include <stdbool.h>
#include "block.h"

#define MAX_REQUEST_LEN 100000000   // 100 M

#define JSONRPC_ERR_NOMEM (-100)

bool json_request_IsComplete( block_t *p_req, size_t *pi_len );

void jsoncpy_bool( bool *p_bool, struct json_object *p_obj );
void jsoncpy_double( double *p_double, struct json_object *p_obj );
void jsoncpy_int( int *p_int, struct json_object *p_obj );
void jsoncpy_ushort( uint16_t* p_short, struct json_object *p_obj );
void jsoncpy_ulong( unsigned long *p_long, struct json_object *p_obj );
void jsoncpy_string( char *psz_string, int i_bytes, struct json_object *p_obj );
int  jsoncpy( void* p_buf, int i_buf, struct json_object *p_obj );




#endif

