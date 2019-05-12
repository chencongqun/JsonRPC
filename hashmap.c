/*
  *  Date: 2015-06-16
  *  Version: 1.0
  *  Author: ccq
  *
  *  Description: A hashmap implement
  *
  *  Copyright @ 2015 SinoData. All rights reserved.
  */

#include "hashmap.h"

#ifdef __LP64__
#define HASH_BITS 61
#else
#define HASH_BITS 31
#endif
#define HASH_MODULUS (((size_t)1 << HASH_BITS) - 1)


typedef struct hashmap_entry_t
{
    hashmap_key key;
    hashmap_value value;
    struct hashmap_entry_t * next;
} hashmap_entry, *hashmap_entry_ref;

typedef struct hashmap_object_t
{
    int initialSize;                    // initial size of the hashmap
    int usedSize;                       // the size used
    int tableSize;                      // the real size of the hashmap
    int threshold;                      // if the size used bigger than the threshold, resize hashmap
    float loadFactor;                   // threshold = tableSize * loadFactor
    hashmap_entry_ref *table;

    hashmap_entry_ref (*pf_entry_create)();
    void (*pf_entry_add)( struct hashmap_object_t * p_this, int index, hashmap_entry_ref p_entry );
    void (*pf_entry_put)( struct hashmap_object_t * p_this, hashmap_entry_ref p_entry, hashmap_value value );
    void (*pf_entry_pop)( struct hashmap_object_t * p_this, hashmap_entry_ref *p_prev, hashmap_entry_ref p_entry );
    hashmap_entry_ref (*pf_entry_get)( struct hashmap_object_t * p_this, hashmap_entry_ref p_entry );

} hashmap_obj, *hashmap_ref;

uint32_t hashmap_get_string_hash(const char* key)
{
    uint32_t h = 0;

    int off = 0, i;
    int len = strlen(key);

    if (len < 16)
    {
        for (i = len ; i > 0; i--)
        {
            h = (h * 37) + key[off++];
        }
    }
    else
    {
        // only sample some characters
        int skip = len / 8;
        for (i = len ; i > 0; i -= skip, off += skip)
        {
            h = (h * 39) + key[off];
        }
    }

    return h;

}

uint32_t hashmap_get_uint32_hash( uint32_t key )
{
    key = ~key + (key << 15); // key = (key << 15) - key - 1;
    key = key ^ (key >> 12);
    key = key + (key << 2);
    key = key ^ (key >> 4);
    key = key * 2057; // key = (key + (key << 3)) + (key << 11);
    key = key ^ (key >> 16);
    return key;
}

uint32_t hashmap_get_uint64_hash( uint64_t key )
{
    key = (~key) + (key << 18); // key = (key << 18) - key - 1;
    key = key ^ (key >> 31);
    key = key * 21; // key = (key + (key << 2)) + (key << 4);
    key = key ^ (key >> 11);
    key = key + (key << 6);
    key = key ^ (key >> 22);
    return (uint32_t) key;
}

uint32_t hashmap_get_double_hash( double v )
{
    int e, sign;
    double m;
#ifdef __LP64__
    uint64_t x,y;
#else
    uint32_t x, y;
#endif

    m = frexp(v, &e);

    sign = 1;
    if (m < 0)
    {
        sign = -1;
        m = -m;
    }

    /* process 28 bits at a time;  this should work well both for binary
       and hexadecimal floating point. */
    x = 0;
    while (m)
    {
        x = ((x << 28) & HASH_MODULUS) | x >> (HASH_BITS - 28);
        m *= 268435456.0;  /* 2**28 */
        e -= 28;
        y = (uint32_t)m;  /* pull out integer part */
        m -= y;
        x += y;
        if (x >= HASH_MODULUS)
            x -= HASH_MODULUS;
    }

    /* adjust for the exponent;  first reduce it modulo _PyHASH_BITS */
    e = e >= 0 ? e % HASH_BITS : HASH_BITS-1-((-1-e) % HASH_BITS);
    x = ((x << e) & HASH_MODULUS) | x >> (HASH_BITS - e);

    x = x * sign;
    if (x == (uint32_t)-1)
        x = (uint32_t)-2;
    return (uint32_t)x;
}

uint32_t general_get_hash( hashmap_key key )
{
    uint32_t hash;
    switch ( key.type )
    {
    case 'S':
        hash = hashmap_get_string_hash( key.u.psz_string );
        break;
    case 'I':
        hash = hashmap_get_uint32_hash( key.u.i_int32 );
        break;
    case 'L':
        hash = hashmap_get_uint64_hash( key.u.i_int64 );
        break;
    case 'D':
        hash = hashmap_get_double_hash( key.u.d_double );
        break;
    default:
        hash = hashmap_get_string_hash( key.u.psz_string );
        break;
    }
    return hash;
}

int compare_key( hashmap_key key1, hashmap_key key2 )
{
    if ( key1.type != key2.type )
        return -1;

    switch ( key1.type )
    {
    case 'S':
        return strcmp( key1.u.psz_string, key2.u.psz_string );
    case 'I':
        return (key1.u.i_int32 == key2.u.i_int32) ? 0 : 1;
    case 'L':
        return (key1.u.i_int64 == key2.u.i_int64) ? 0 : 1;
    case 'D':
        return (key1.u.d_double == key2.u.d_double) ? 0 : 1;
    default:
        return strcmp( key1.u.psz_string, key2.u.psz_string );
    }
}

void hashmap_rehash( hashmap_ref p_map )
{
    int i=0;
    hashmap_entry_ref p_entry, p_old_entry;
    int i_old_capacity = p_map->tableSize;
    hashmap_entry_ref *p_old_table = p_map->table;
    int i_new_capacity = i_old_capacity * 2 + 1;
    hashmap_entry_ref *p_new_table = malloc( sizeof(hashmap_entry_ref) * i_new_capacity );
    if( p_new_table == NULL )
    {
        //log no memory
        return;
    }

    p_map->threshold = (int)( i_new_capacity * p_map->loadFactor);
    p_map->table = p_new_table;
    p_map->tableSize = i_new_capacity;

    for( i = 0; i < p_map->tableSize; i++ )
    {
        p_map->table[i] = NULL;
    }

    for( i = i_old_capacity; i > 0; i-- )
    {
        for( p_old_entry = p_old_table[i]; p_old_entry; p_old_entry=p_old_entry->next )
        {
            hashmap_entry_ref p_entry = p_old_entry;
            int hash = general_get_hash( p_entry->key );
            int index = ( hash & 0x7FFFFFFF ) % i_new_capacity;
            p_entry->next = p_new_table[index];
            p_new_table[index] = p_entry;
        }
    }

    free(p_old_table);
}


static hashmap_entry_ref hashmap_entry_create()
{
    hashmap_entry_ref p_entry = malloc( sizeof( hashmap_entry ) );
    return p_entry;
}

static void hashmap_entry_add( hashmap_ref p_this, int index, hashmap_entry_ref p_entry )
{
    p_entry->next = p_this->table[index];
    p_this->table[index] = p_entry;
}

static void hashmap_entry_put( hashmap_ref p_this, hashmap_entry_ref p_entry, hashmap_value value )
{
    p_entry->value = value;
}

static void hashmap_entry_pop( hashmap_ref p_this, hashmap_entry_ref *p_prev, hashmap_entry_ref p_entry )
{
    *p_prev = p_entry;
}

static hashmap_entry_ref hashmap_entry_get( hashmap_ref p_this, hashmap_entry_ref p_entry  )
{
    return p_entry;
}

hashmap hashmap_create( int i_size )
{
    hashmap_ref p_map = (hashmap_ref)malloc(sizeof(hashmap_obj));
    if ( !p_map )
        return NULL;

    memset( p_map, 0, sizeof(hashmap_obj) );
    p_map->initialSize = i_size > 101 ? i_size : 101;
    p_map->tableSize = p_map->initialSize;
    p_map->loadFactor = 0.75f;
    p_map->threshold = (int)(p_map->tableSize * p_map->loadFactor);

    p_map->table = ( hashmap_entry_ref * )malloc( sizeof(hashmap_entry_ref) * p_map->tableSize );
    if ( !p_map->table )
    {
        free(p_map);
        return NULL;
    }
    memset( p_map->table, 0, sizeof(hashmap_entry_ref)*p_map->tableSize );

    p_map->pf_entry_create = hashmap_entry_create;
    p_map->pf_entry_add = hashmap_entry_add;
    p_map->pf_entry_put = hashmap_entry_put;
    p_map->pf_entry_pop = hashmap_entry_pop;
    p_map->pf_entry_get = hashmap_entry_get;

    return (hashmap)p_map;
}

void hashmap_free( hashmap p_map )
{
    hashmap_ref p_hashmap = (hashmap_ref) p_map;
    int i = 0;
    hashmap_entry_ref p_entry = NULL;
    for ( i=0; i<p_hashmap->tableSize; i++ )
    {
        for ( p_entry = p_hashmap->table[i]; p_entry; )
        {
            // free hashmap_entry and hashmap_key,  hashmap_value must free by the owner itself
            hashmap_entry_ref p_tmp = p_entry;
            p_entry = p_tmp->next;
            FREE_KEY( p_tmp->key );
            free( p_tmp );
        }
    }
    free( p_hashmap->table );
    free( p_hashmap );
}

int hashmap_length( hashmap p_map )
{
    hashmap_ref p_hashmap = (hashmap_ref) p_map;
    return p_hashmap->usedSize;
}

hashmap_value hashmap_put( hashmap p_map, hashmap_key key, hashmap_value value )
{
    if ( !p_map )
        return NULL;

    hashmap_ref p_hashmap = (hashmap_ref) p_map;
    uint32_t hash = general_get_hash( key );
    int index = ( hash & 0x7FFFFFFF )% p_hashmap->tableSize;
    int i=0;
    hashmap_entry_ref p_entry = NULL;
    hashmap_entry_ref *table = p_hashmap->table;

    for ( p_entry=table[index]; p_entry; p_entry=p_entry->next )
    {
        if ( compare_key( p_entry->key, key ) == 0 )
        {
            hashmap_value p_old_value = p_entry->value;
            p_hashmap->pf_entry_put( p_hashmap, p_entry, value );
            return p_old_value;
        }
    }

    p_entry = p_hashmap->pf_entry_create();
    COPY_KEY(p_entry->key, key);
    p_entry->value = value;
    p_hashmap->pf_entry_add( p_hashmap, index, p_entry );
    p_hashmap->usedSize++;

    if ( p_hashmap->usedSize > p_hashmap->tableSize )
        hashmap_rehash( p_hashmap );

    return NULL;
}

hashmap_value hashmap_get( hashmap p_map, hashmap_key key )
{
    if ( !p_map )
        return NULL;
    hashmap_ref p_hashmap = (hashmap_ref)p_map;
    int hash = general_get_hash( key );
    int index = ( hash & 0x7FFFFFFF ) % p_hashmap->tableSize;
    hashmap_entry_ref p_entry = NULL;

    for ( p_entry = p_hashmap->table[index]; p_entry; p_entry = p_entry->next )
    {
        if ( compare_key( p_entry->key, key ) == 0 )
        {
            p_entry = p_hashmap->pf_entry_get( p_hashmap, p_entry );
            return p_entry->value;
        }
    }

    return NULL;
}

hashmap_value hashmap_pop( hashmap p_map, hashmap_key key )
{
    if ( !p_map )
        return NULL;
    hashmap_ref p_hashmap = (hashmap_ref)p_map;
    hashmap_entry_ref *table = p_hashmap->table;
    int hash = general_get_hash( key );
    int index = ( hash & 0x7FFFFFFF ) % p_hashmap->tableSize;
    hashmap_entry_ref p_entry = NULL, p_prev = NULL;

    for ( p_entry = table[index]; p_entry; p_prev=p_entry,p_entry = p_entry->next )
    {
        if ( compare_key( p_entry->key, key ) == 0 )
        {
            hashmap_value p_value = p_entry->value;
            if ( p_prev )
                p_hashmap->pf_entry_pop( p_hashmap, &p_prev->next, p_entry );
            else
                p_hashmap->pf_entry_pop( p_hashmap, &table[index], p_entry );
            p_hashmap->usedSize--;
            FREE_KEY( p_entry->key )
            free( p_entry );
            return p_value;
        }
    }

    return NULL;
}

hashmap_iterator hashmap_iterate(hashmap m)
{
	int i;
    hashmap_ref map = (hashmap_ref) m;
    hashmap_iterator it;
    memset( &it.key, 0, sizeof( it.key ) );
    it.p_val = NULL;
    it.p_entry = NULL;
	it.index = 0;
	it.p_map = m;
	
    for(i = 0; i < map->tableSize ; ++i)
    {
        if(map->table[i])
        {
            it.p_entry = map->table[i];
			it.index = i;
            break;
        }
    }
    return it;
}

hashmap_iterator* hashmap_next(hashmap_iterator* iterator)
{
	int i;
    hashmap_entry_ref p_entry  = ( hashmap_entry_ref ) iterator->p_entry;

    if ( !p_entry )
        return NULL;

    hashmap_ref p_map = ( hashmap_ref ) iterator->p_map;
    iterator->key = p_entry->key;
    iterator->p_val = p_entry->value;

    if( p_entry->next )
    {
        p_entry = p_entry->next;
    }
    else
    {
        int i_start = iterator->index + 1;
        p_entry = NULL;
        for(i = i_start; i < p_map->tableSize ; ++i)
        {
            if( p_map->table[i] )
            {
                p_entry = p_map->table[i];
                break;
            }
        }
    }

    iterator->p_entry= p_entry;
	
    return iterator;
}


hashmap_key hashmap_make_key( char type, void * value )
{
    hashmap_key key;

    switch( type )
    {
    case 'S':
        key.type = 'S';
        key.u.psz_string = (char *)value;
        break;
    case 'I':
        key.type = 'I';
        key.u.i_int32 = *((uint32_t*) value);
        break;
    case 'L':
        key.type = 'L';
        key.u.i_int64 = *((uint64_t *)value);
        break;
    case 'D':
        key.type = 'D';
        key.u.d_double = *((double*)value);
        break;
    default:
        break;
    }

    return key;
}



