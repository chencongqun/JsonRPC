#include <string.h>
#include <math.h>
#include <stdlib.h>
#include "block.h"

block_t * block_Alloc( size_t i_size )
{
    block_t *p_block = malloc( sizeof(block_t) );
    if ( !p_block )
        return NULL;

    size_t i_max = ceil( i_size * 1.0 / 4096 ) * 4096;
    p_block->p_buffer = malloc( i_max );
    if ( !p_block->p_buffer )
    {
        free( p_block );
        return NULL;
    }
    p_block->i_buffer = 0;
    p_block->i_maxlen = i_max;
    return p_block;
}

block_t *block_Realloc( block_t *p_block, size_t i_addsize )
{
    size_t i_max = ceil( i_addsize * 1.0 / 4096 ) * 4096;
    p_block->p_buffer = realloc( p_block->p_buffer,
                                 p_block->i_maxlen + i_max );
    if ( !p_block->p_buffer )
    {
        block_Release( p_block );
        return NULL;
    }
    p_block->i_maxlen += i_max;
    return p_block;
}

void block_Release( block_t *p_block )
{
    free( p_block->p_buffer );
    free( p_block );
}

block_t *block_Append( block_t *p_block, uint8_t *p_buf, size_t i_buf )
{
    if ( p_block->i_buffer + i_buf >= p_block->i_maxlen )
    {
        //if ( p_block->i_buffer > 65535 )
        //    log_Warn( "block append found i_buffer big than 65535" );

        p_block = block_Realloc( p_block, i_buf );
        if ( !p_block )
        {
            //log_Err( "no memory" );
            return NULL;
        }
    }
    memcpy( p_block->p_buffer + p_block->i_buffer, p_buf, i_buf );
    p_block->i_buffer += i_buf;
    return p_block;
}

