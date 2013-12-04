/*
  Copyright (c) 2010-2013 Alex Snyatkov

  Permission is hereby granted, free of charge, to any person obtaining a copy
  of this software and associated documentation files (the "Software"), to deal
  in the Software without restriction, including without limitation the rights
  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
  copies of the Software, and to permit persons to whom the Software is
  furnished to do so, subject to the following conditions:

  The above copyright notice and this permission notice shall be included in
  all copies or substantial portions of the Software.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
  THE SOFTWARE.
*/
#ifndef DISK_TRACKER_H
#define DISK_TRACKER_H

#include "libcrt/types.h"

typedef void*              disk_remap_t;

#define DISK_TRACKER_OK           (0)
#define DISK_TRACKER_NO_MEMORY    (-1)
#define DISK_TRACKER_NO_STORAGE   (-2)
#define DISK_TRACKER_INV_ARGUMENT (-3)

struct disk_extent
{
    ulong64_t start_block;
    ulong32_t length_in_blocks;
};

struct disk_extent_remap
{
    struct disk_extent source_extent;
    ulong32_t          number_of_extents;
    ulong32_t          num_remapped;     /* How many remapped blocks found */
    struct disk_extent remapped_extents[1];
};

struct remap_storage
{
    void*                 custom_info;
    struct remap_storage* next;
    ulong32_t             number_of_blocks;  /* Total number of blocks in all extents */
    ulong32_t             number_of_extents; /* Number of extents in the following array*/
    struct disk_extent    extents[1];
};


disk_remap_t disk_tracker_init(void* (*alloc_fn)(unsigned size), 
                               void (*free_fn)(void* mem), 
                               struct remap_storage* initial_storage);

int disk_tracker_reset(disk_remap_t remap);

int disk_tracker_destroy(disk_remap_t* remap);

int disk_tracker_add_storage(disk_remap_t remap, struct remap_storage* storage);

int disk_tracker_reset_storage(disk_remap_t remap, struct remap_storage* storage);

int disk_tracker_get_storage(disk_remap_t remap, struct remap_storage** storage);

int disk_tracker_get_storage_info(disk_remap_t remap, 
                                  unsigned* total_blocks,
                                  unsigned* free_blocks);

int disk_tracker_remap(disk_remap_t remap, 
                       struct disk_extent* source,
                       struct disk_extent_remap** result);

int disk_tracker_find_remap(disk_remap_t remap, 
                            struct disk_extent* source, 
                            struct disk_extent_remap** result);

int disk_tracker_get_all_remaps(disk_remap_t remap, 
                                /*OUT*/unsigned* remaps_count, 
                                /*OUT*/struct disk_extent_remap*** remaps);

/* Get internal hash table size */
int disk_tracker_get_hash_size(disk_remap_t remap, unsigned* size);


struct disk_extent* disk_tracker_find_remap_for_block(disk_remap_t remap,
                                                      ulong64_t source_block);

void disk_tracker_free_remap(disk_remap_t remap,  struct disk_extent_remap* extent_remap);

void disk_tracker_free_extent(disk_remap_t remap, struct disk_extent* extent);

struct remap_storage* disk_tracker_alloc_remap_storage(disk_remap_t remap, unsigned num_extents);

void disk_tracker_free_remap_storage(disk_remap_t remap, struct remap_storage* storage);


#endif 
