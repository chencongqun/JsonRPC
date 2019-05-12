#ifndef _BLOCK_H
#define _BLOCK_H

#include <stdint.h>
#include <stddef.h>

struct block_t
{
    size_t i_maxlen;
    size_t i_buffer;
    uint8_t *p_buffer;
};

typedef struct block_t block_t;

block_t *block_Alloc( size_t i_size );
block_t *block_Realloc( block_t *p_block, size_t i_addsize );
void     block_Release( block_t *p_block );
block_t *block_Append( block_t *p_block, uint8_t *p_buf, size_t i_buf );

#endif

