#ifndef _HTTP_PARSER_H
#define _HTTP_PARSER_H

#include <stdio.h>
#include <stdlib.h>
#include <json/json.h>
#include "block.h"




typedef struct http_package_t
{
	char method[16];
	char request_url[256];
	char http_version[32];
	
	char filename[256];
	char post_data[1024];
}http_package;

struct json_object * http_request_parse( block_t *p_request_block );

#endif

