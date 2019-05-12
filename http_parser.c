#include "http_parser.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>

struct json_object * http_request_parse( block_t *p_request_block )
{
	struct json_object * p_json_data = json_object_new_object();

	char *p_request_data = (char *)p_request_block->p_buffer;

	int file = open( "/tmp/http.txt", O_RDWR | O_CREAT | O_NONBLOCK, 0777 );

	write( file, p_request_data, strlen(p_request_data) );

	return p_json_data;
}



