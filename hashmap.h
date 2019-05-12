/*
  *  Date: 2015-06-16
  *  Version: 1.0
  *  Author: ccq
  *
  *  Description: A hashmap implement
  *
  *  Copyright @ 2015 SinoData. All rights reserved.
  */

#ifndef _HASHMAP_H
#define _HASHMAP_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>


#ifndef _UINT_T
#define _UINT_T
typedef unsigned char uint8_t;
typedef unsigned short uint16_t;
typedef unsigned int uint32_t;
typedef unsigned long uint64_t;
#endif

typedef void* hashmap;
typedef void* hashmap_value;
typedef struct hashmap_key_t
{
    char type;
    union
    {
        uint32_t i_int32;
        uint64_t i_int64;
        double d_double;
        char *psz_string;
    } u;
} hashmap_key;

typedef struct hashmap_iterator_t
{
	hashmap p_map;
    hashmap_key key;
    void *p_val;
    void *p_entry;
	int index;
    struct hashmap_iterator* (*pf_next) ( struct hashmap_iterator *p_self );
}hashmap_iterator;


hashmap_key hashmap_make_key( char type, void * value );

#define COPY_KEY(dest,src) { memcpy( (void *)&dest, (const void *)&src, sizeof(hashmap_key) );\
							if( src.type=='S' ) dest.u.psz_string=strdup(src.u.psz_string); }
#define FREE_KEY(key) {if( key.type == 'S' ) free(key.u.psz_string); key.u.psz_string=NULL;}
#define MAKE_STRING_KEY(value) hashmap_make_key( 'S', (void *)value )
#define MAKE_INT32_KEY(value) hashmap_make_key( 'I', (void *)&value )
#define MAKE_INT64_KEY(value) hashmap_make_key( 'L', (void *)&value )
#define MAKE_DOUBLE_KEY(value) hashmap_make_key( 'D', (void *)&value )


/*
  * hashmap_create()
  *
  * Function - create a hashmap object
  *
  * Parameter:
  *                      i_size - size of the hashmap wanted to create
  *
  * Retrun Value:
  *                      return the hashmap created
  * 
  */
hashmap hashmap_create( int i_size );


/*
  * hashmap_length()
  *
  * Function - get the length of the hashmap
  *
  * Parameter:
  *                      p_map - the hashmap object
  *
  * Retrun Value:
  *                      return the length 
  * 
  */
int hashmap_length( hashmap p_map );


/****************************************
  * hashmap_put()
  *
  * Function - modify the element match or add a new element to the hashmap
  *
  * Parameter:
  *                      p_map - the hashmap object
  *                      key - the key of the element
  *                      value - the value of the new element
  *
  * Retrun Value:
  *                      return  the old value if exists or NULL if no match found
  * 
  ***************************************/
hashmap_value hashmap_put( hashmap p_map, hashmap_key key, hashmap_value value );



/*
  * hashmap_get()
  *
  * Function - get a element from the hashmap by key
  *
  * Parameter:
  *                      p_map - the hashmap object
  *                      key - the key of the element wanted to find
  *
  * Retrun Value:
  *                      return the value of the element got
  * 
  */
hashmap_value hashmap_get( hashmap p_map, hashmap_key key );


/*
  * hashmap_pop()
  *
  * Function - delete a element from the hashmap by key
  *
  * Parameter:
  *                      p_map - the hashmap object
  *                      key - the key of the element wanted to delete
  *
  * Retrun Value:
  *                      return the value of the element deleted, 
  *                      the owner of the hashmap_value must free it by itself
  * 
  */
hashmap_value hashmap_pop( hashmap p_map, hashmap_key key );


/*
  * hashmap_free()
  *
  * Function - free the hashmap object
  *
  * Parameter:
  *                      hashmap - the hashmap object
  *
  * Retrun Value: void
  * 
  */
void hashmap_free( hashmap p_map );

/*
 * hashmap_iterate()
 *
 * Function - Get an iterator into keys the map. Ordering is arbitrary.
 *
 * Parameter:
 *                   map - the hashmap reference.
 * Return:
 *                   An iterator reference.
 */
hashmap_iterator hashmap_iterate(hashmap map);

/*
 * hashmap_next
 * 
 * Function - Get the value at the iterator and move the iterator on to the
 *                   next key. The return value will be NULL when iteratation is complete
 * 
 * Parameter:
 *                  iterator - reference to iterator pass in address of iterator.
 * 
 * Return:
 *                  A key or NULL if no more elements.
 */
hashmap_iterator* hashmap_next(hashmap_iterator* iterator);


#endif

