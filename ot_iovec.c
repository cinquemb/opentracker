/* This software was written by Dirk Engling <erdgeist@erdgeist.org>
   It is considered beerware. Prost. Skol. Cheers or whatever. */

/* System */
#include <sys/types.h>
#include <sys/mman.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/uio.h>

/* Libowfat */

/* Opentracker */
#include "ot_iovec.h"

void *iovec_increase( int *iovec_entries, struct iovec **iovector, size_t new_alloc ) {
  void *new_ptr = realloc( *iovector, 1 + *iovec_entries * sizeof( struct iovec ) );
  if( !new_ptr )
    return NULL;
  *iovector = new_ptr;
  new_ptr = mmap( NULL, new_alloc, PROT_READ | PROT_WRITE, MAP_ANON | MAP_PRIVATE, -1, 0 );
  if( !new_ptr )
    return NULL;
  ((*iovector)[*iovec_entries]).iov_base = new_ptr;
  ((*iovector)[*iovec_entries]).iov_len  = new_alloc;  
  ++*iovec_entries;
  return new_ptr;
}

void iovec_free( int *iovec_entries, struct iovec **iovector ) {
  int i;
  for( i=0; i<*iovec_entries; ++i )
    munmap( ((*iovector)[i]).iov_base, ((*iovector)[i]).iov_len );
  *iovec_entries = 0;
}

void  iovec_fixlast( int *iovec_entries, struct iovec **iovector, size_t new_alloc ) {
  int page_size = getpagesize();
  size_t old_alloc, old_pages, new_pages;

  if( !*iovec_entries ) return;

  old_alloc = ((*iovector)[ *iovec_entries - 1 ]).iov_len;
  old_pages = 1 + old_alloc / page_size;
  new_pages = 1 + new_alloc / page_size;

  if( old_pages != new_pages ) {
    munmap( ((char*)((*iovector)[ *iovec_entries - 1 ]).iov_base ) +  new_pages * page_size,
            old_alloc - new_pages * page_size );
  }
  ((*iovector)[*iovec_entries - 1 ]).iov_len = new_alloc;
}

size_t iovec_length( int *iovec_entries, struct iovec **iovector ) {
  size_t length = 0;
  int i;
  for( i=0; i<*iovec_entries; ++i )
    length += ((*iovector)[i]).iov_len;
  return length;
}