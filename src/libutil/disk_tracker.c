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
#include "libcrt/baselib.h"
#include "libcrt/hashtable.h"
#include "libutil/disk_tracker.h"

struct disk_tracker
{
    struct remap_storage* head;
    struct remap_storage* current;
    struct disk_extent*   current_extent;

    void* (*alloc_fn)(unsigned size); 
    void  (*free_fn) (void* mem);

    struct hashtable* blocks_map;
    unsigned          current_block;
    unsigned          free_blocks;
    unsigned          total_blocks;
};

static unsigned int diskf_hash(void* k)
{
    return good_hash_func(k, sizeof(unsigned long long), 0);
}

static int diskf_key_equal(void* a, void* b)
{
    return (int)!(*(unsigned long long*)a - *(unsigned long long*)b);
}



disk_remap_t disk_tracker_init(void* (*alloc_fn)(unsigned size), 
                               void (*free_fn)(void* mem), 
                               struct remap_storage* initial_storage)
{
    struct disk_tracker* tracker = (struct disk_tracker*)alloc_fn(sizeof(*tracker));
    if (tracker == NULL) {
        difi_dbg_print("failed to allocate new disk tracker\n");
        return NULL;
    }

    memset(tracker, 0, sizeof(*tracker));
    tracker->alloc_fn = alloc_fn;
    tracker->free_fn = free_fn;
    
    tracker->blocks_map = create_hashtable(1000, diskf_hash, diskf_key_equal, 
                                           alloc_fn, free_fn);
    if (tracker->blocks_map == NULL) {
        difi_dbg_print("failed to allocate hashtable\n");
        free_fn(tracker);
        return NULL;
    }
    
    tracker->head = tracker->current = initial_storage;
    tracker->free_blocks = tracker->total_blocks = initial_storage->number_of_blocks;
    tracker->current_extent = &initial_storage->extents[0];

    return tracker;
}

int disk_tracker_reset(disk_remap_t remap)
{
    struct disk_tracker* tracker = (struct disk_tracker*)remap;
    if (tracker == NULL) {
        difi_dbg_print("invalid argument\n");
        return DISK_TRACKER_INV_ARGUMENT;
    }

    if (tracker->blocks_map) {
        /* Destroy hashtable, free all values */
        hashtable_destroy(tracker->blocks_map, 1);
        tracker->blocks_map = NULL;
    }

    tracker->blocks_map = create_hashtable(1000, diskf_hash, diskf_key_equal, 
                                           tracker->alloc_fn, tracker->free_fn);
    if (tracker->blocks_map == NULL) {
        difi_dbg_print("failed to allocate hashtable\n");
        return DISK_TRACKER_NO_MEMORY;
    }

    tracker->current = tracker->head;
    tracker->free_blocks = tracker->total_blocks;
    tracker->current_extent = &tracker->current->extents[0];

    return DISK_TRACKER_OK;
}

int disk_tracker_destroy(disk_remap_t* remap)
{
    struct disk_tracker* tracker = (struct disk_tracker*)*remap;
    void  (*free_fn) (void* mem);

    if (tracker == NULL) {
        difi_dbg_print("invalid argument\n");
        return DISK_TRACKER_INV_ARGUMENT;
    }

    if (tracker->blocks_map) {
        /* Destroy hashtable, free all values */
        hashtable_destroy(tracker->blocks_map, 1);
        tracker->blocks_map = NULL;
    }
    free_fn = tracker->free_fn;
    free_fn(tracker);
    *remap = NULL;
    return DISK_TRACKER_OK;
}


int disk_tracker_add_storage(disk_remap_t remap, struct remap_storage* storage)
{
    struct disk_tracker* tracker = (struct disk_tracker*)remap;
    struct remap_storage* cur = NULL;

    if (tracker == NULL) {
        difi_dbg_print("invalid argument\n");
        return DISK_TRACKER_INV_ARGUMENT;
    }
    cur = tracker->head;
    while(cur->next)
        cur = cur->next;
    cur->next = storage;
    tracker->free_blocks  += storage->number_of_blocks;
    tracker->total_blocks += storage->number_of_blocks;

    return DISK_TRACKER_OK;
}

int disk_tracker_get_storage(disk_remap_t remap, struct remap_storage** storage)
{
    struct disk_tracker* tracker = (struct disk_tracker*)remap;
    if (tracker == NULL) {
        difi_dbg_print("invalid argument\n");
        return DISK_TRACKER_INV_ARGUMENT;
    }
    *storage = tracker->head;
    return 0;
}

int disk_tracker_reset_storage(disk_remap_t remap, struct remap_storage* storage)
{
    struct disk_tracker* tracker = (struct disk_tracker*)remap;
    struct remap_storage* old_storage, *p;
    if (tracker == NULL) {
        difi_dbg_print("invalid argument\n");
        return DISK_TRACKER_INV_ARGUMENT;
    }

    /* Free old storage first */
    old_storage = tracker->head;
    while(old_storage != NULL)
    {
        p = old_storage->next;
        tracker->free_fn(old_storage);
        old_storage = p;
    }

    tracker->head = tracker->current = storage;
    tracker->free_blocks = tracker->total_blocks = storage->number_of_blocks;
    tracker->current_extent = &storage->extents[0];
    return 0;
}

int disk_tracker_get_storage_info(disk_remap_t remap, 
                                  unsigned* total_blocks,
                                  unsigned* free_blocks)
{
    struct disk_tracker* tracker = (struct disk_tracker*)remap;
    if (tracker == NULL) {
        difi_dbg_print("invalid argument\n");
        return DISK_TRACKER_INV_ARGUMENT;
    }
    *total_blocks = tracker->total_blocks;
    *free_blocks = tracker->free_blocks;
    return 0;
}


int disk_tracker_get_hash_size(disk_remap_t remap, unsigned* size)
{
    struct disk_tracker* tracker = (struct disk_tracker*)remap;
    if (tracker == NULL) {
        difi_dbg_print("invalid argument\n");
        return DISK_TRACKER_INV_ARGUMENT;
    }
    *size = hashtable_count(tracker->blocks_map);

    return DISK_TRACKER_OK;
}

int disk_tracker_get_all_remaps(disk_remap_t remap, 
                                /*OUT*/unsigned* remaps_count, 
                                /*OUT*/struct disk_extent_remap*** remaps)
{
    ulong64_t** keys, b;
    unsigned i, j, num_keys, source_extents_count;
    struct disk_tracker* tracker = (struct disk_tracker*)remap;
    if (tracker == NULL) {
        difi_dbg_print("invalid argument\n");
        return DISK_TRACKER_INV_ARGUMENT;
    }

    if (remaps_count == NULL) {
        difi_dbg_print("invalid argument\n");
        return DISK_TRACKER_INV_ARGUMENT;
    }

    num_keys = hashtable_count(tracker->blocks_map);
    keys = (ulong64_t**)tracker->alloc_fn(num_keys * sizeof(ulong64_t));

    hashtable_get_all_keys(tracker->blocks_map, (void**)keys);

    quick_sort_ulong64_ptr(keys, num_keys);
    
    /* First find how many source extents do we have */
    b = *keys[0];
    source_extents_count = 1;
    for (i = 1; i < num_keys; i++) {
        if (*keys[i] != b + 1) {
            source_extents_count++;
        }
        b = *keys[i];
    }

    *remaps = (struct disk_extent_remap**)tracker->alloc_fn(source_extents_count * sizeof(void*));

    for (i = 0, j = 0; i < source_extents_count; i++) {
        struct disk_extent extent;
        extent.start_block = *keys[j++];
        extent.length_in_blocks = 1;
        while (j < num_keys && *keys[j] == *keys[j - 1] + 1) {
            extent.length_in_blocks++;
            j++;
        }
        disk_tracker_find_remap(remap, &extent, &(*remaps)[i]);
    }

    *remaps_count = source_extents_count;
    return DISK_TRACKER_OK;
}

static struct remap_storage* advance_storage(struct disk_tracker* tracker)
{
    return tracker->current->next;
}

int disk_tracker_remap(disk_remap_t remap, 
                       struct disk_extent* source,
                       struct disk_extent_remap** result)
{
    struct disk_tracker* tracker = (struct disk_tracker*)remap;
    ulong64_t    b; 

    if (tracker == NULL) {
        difi_dbg_print("remap is NULL\n");
        return DISK_TRACKER_INV_ARGUMENT;
    }
    if (tracker->current == NULL) {
        difi_dbg_print("no current storage\n");
        return DISK_TRACKER_NO_STORAGE;
    }

    if (tracker->free_blocks < source->length_in_blocks) {
        /* TODO: Dumb algo, wasting storage, need to use leftover from the current block */
        if (tracker->current->next == NULL) {
            difi_dbg_print("no more storage\n");
            
            return DISK_TRACKER_NO_STORAGE;
        }
        tracker->current = tracker->current->next;
    }
    for (b = source->start_block; b < source->start_block + source->length_in_blocks; b++)
    {
        ulong64_t* block = (ulong64_t*)hashtable_search(tracker->blocks_map, &b);
        if (block == NULL) {
            ulong64_t* key;
            block = (ulong64_t*)tracker->alloc_fn(sizeof(ulong64_t));
            *block = tracker->current_extent->start_block + tracker->current_block++;
            tracker->free_blocks--;
            if (tracker->current_block == tracker->current_extent->length_in_blocks) {
                tracker->current_extent++;
                tracker->current_block = 0;
            }

            key = (ulong64_t*)tracker->alloc_fn(sizeof(*key));
            *key = b;
            hashtable_insert(tracker->blocks_map, key, block);
        }
    }
    return disk_tracker_find_remap(remap, source, result);
}

int disk_tracker_find_remap(disk_remap_t remap, 
                            struct disk_extent* source,
                            struct disk_extent_remap** result_out)
{
    struct disk_tracker* tracker = (struct disk_tracker*)remap;
    ulong64_t*   blocks = NULL;
    ulong64_t    b; 
    unsigned     i = 0;
    unsigned     num_remapped = 0;
    unsigned     num_intervals = 0;
    struct disk_extent_remap* result = NULL;
    struct disk_extent*       cur_extent = NULL;

    if (tracker == NULL) {
        difi_dbg_print("remap is NULL\n");
        return DISK_TRACKER_INV_ARGUMENT;
    }
    
    blocks = (ulong64_t*)tracker->alloc_fn(source->length_in_blocks * sizeof(ulong64_t));
    if (blocks == NULL) {
        difi_dbg_print("out of memory\n");
        return DISK_TRACKER_NO_MEMORY;
    }
    
    for (b = source->start_block; b < source->start_block + source->length_in_blocks; b++, i++)
    {
        ulong64_t* block = (ulong64_t*)hashtable_search(tracker->blocks_map, &b);
        /* If not found,use the original block */
        if (block == NULL) {
            blocks[i] = b;
        } else {
            blocks[i] = *block;
            num_remapped++;
        }
    }
    
    /* Convert array of blocks to array of intervals */

    /* First find how many intervals do we have */
    b = blocks[0];
    num_intervals = 1;
    for (i = 1; i < source->length_in_blocks; i++) {
        if (blocks[i] != b + 1) {
            num_intervals++;
        }
        b = blocks[i];
    }
    
    /* Allocate return struct */
    i = sizeof(struct disk_extent_remap) + sizeof(struct disk_extent)*(num_intervals - 1);
    result = (struct disk_extent_remap*)tracker->alloc_fn(i);
    memset(result, 0, i);
    result->number_of_extents = num_intervals;
    result->num_remapped = num_remapped;
    result->source_extent = *source;

    cur_extent = &result->remapped_extents[0];
    cur_extent->length_in_blocks = 1;
    cur_extent->start_block = blocks[0];
    for (i = 1; i < source->length_in_blocks; i++) {
        while(blocks[i] == blocks[i - 1] + 1 && i < source->length_in_blocks) {
            cur_extent->length_in_blocks++;
            i++;
        }
        if (i == source->length_in_blocks)
            break;
        cur_extent++;
        cur_extent->length_in_blocks = 1;
        cur_extent->start_block = blocks[i];
    }
    tracker->free_fn(blocks);
    *result_out = result;
    return DISK_TRACKER_OK;
}

void disk_tracker_free_remap(disk_remap_t remap,  struct disk_extent_remap* extent_remap)
{
    struct disk_tracker* tracker = (struct disk_tracker*)remap;

    if (tracker == NULL) {
        difi_dbg_print("remap is NULL\n");
        return;
    }

    tracker->free_fn(extent_remap);
}

void disk_tracker_free_extent(disk_remap_t remap, struct disk_extent* extent)
{
    struct disk_tracker* tracker = (struct disk_tracker*)remap;
    if (tracker == NULL) {
        difi_dbg_print("remap is NULL\n");
        return;
    }

    tracker->free_fn(extent);
}

struct remap_storage* disk_tracker_alloc_remap_storage(disk_remap_t remap, unsigned num_extents)
{
    struct disk_tracker*  tracker = (struct disk_tracker*)remap;
    unsigned              sz;
    struct remap_storage* storage;

    if (tracker == NULL) {
        difi_dbg_print("remap is NULL\n");
        return NULL;
    }
    sz = sizeof(struct remap_storage) + sizeof(struct disk_extent) * (num_extents - 1);
    storage = (struct remap_storage*)tracker->alloc_fn(sz);
    if (storage != NULL) {
        memset(storage, 0, sz);
    }

    return storage;
}
