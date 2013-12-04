#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cutest/CuTest.h"
#include "libcrt/baselib.h"
#include "libutil/disk_tracker.h"

struct remap_storage* create_storage()
{
    struct remap_storage* storage =
        (struct remap_storage*)malloc(sizeof(*storage) + sizeof(storage->extents[0]));
    memset(storage, 0, sizeof(*storage));
    storage->number_of_extents = 2;
    storage->number_of_blocks = 20;

    storage->extents[0].start_block = 10;
    storage->extents[0].length_in_blocks = 10;

    // Next extent is split
    storage->extents[1].start_block = 100;
    storage->extents[1].length_in_blocks = 10;

    return storage;
}

struct remap_storage* create_storage_for_reset()
{
    struct remap_storage* storage =
        (struct remap_storage*)malloc(sizeof(*storage));
    memset(storage, 0, sizeof(*storage));
    storage->number_of_extents = 1;
    storage->number_of_blocks = 12;

    storage->extents[0].start_block = 1;
    storage->extents[0].length_in_blocks = 12;

    return storage;
}


void test_disk_tracker(CuTest* tc)
{
    disk_remap_t tracker = NULL;
    struct remap_storage* storage = create_storage();
    struct disk_extent extent;
    int status;
    struct disk_extent_remap* result = NULL;
    unsigned total_blocks, free_blocks;
    unsigned remaps_count;
    struct disk_extent_remap** remaps;

    tracker = disk_tracker_init(malloc, free, storage); 
    CuAssertTrue(tc, tracker != NULL);

    // Check that returned storage info is correct
    status = disk_tracker_get_storage_info(tracker, &total_blocks, &free_blocks);
    CuAssertIntEquals(tc, DISK_TRACKER_OK, status);
    CuAssertIntEquals(tc, 20, total_blocks);
    CuAssertIntEquals(tc, 20, free_blocks);

    // Test 1: remapping must fail because there are no free blocks
    extent.start_block = 50;
    extent.length_in_blocks = 50;
    status = disk_tracker_remap(tracker, &extent, &result);
    CuAssertIntEquals(tc, DISK_TRACKER_NO_STORAGE, status);

    // Test 2: simple remap, must return 1 interval (5 blocks left)
    extent.start_block = 50;
    extent.length_in_blocks = 5;
    status = disk_tracker_remap(tracker, &extent, &result);
    CuAssertIntEquals(tc, DISK_TRACKER_OK, status);
    CuAssertPtrNotNull(tc, result);
    CuAssertIntEquals(tc, 1, result->number_of_extents);
    CuAssertIntEquals(tc, 5, result->remapped_extents[0].length_in_blocks);
    CuAssertTrue(tc, result->remapped_extents[0].start_block == 10);
    disk_tracker_free_remap(tracker, result);
    result = NULL;

    // Test 3: lookup extent, must return previous one
    status = disk_tracker_find_remap(tracker, &extent, &result); 
    CuAssertIntEquals(tc, DISK_TRACKER_OK, status);
    CuAssertPtrNotNull(tc, result);
    CuAssertIntEquals(tc, 1, result->number_of_extents);
    CuAssertIntEquals(tc, 5, result->remapped_extents[0].length_in_blocks);
    CuAssertTrue(tc, result->remapped_extents[0].start_block == 10);
    disk_tracker_free_remap(tracker, result);
    result = NULL;
    
    // Test 3: remap for an interval contained in previous one, 
    //         must return same values
    extent.start_block = 52;
    extent.length_in_blocks = 2;
    status = disk_tracker_find_remap(tracker, &extent, &result); 
    CuAssertIntEquals(tc, DISK_TRACKER_OK, status);
    CuAssertPtrNotNull(tc, result);
    CuAssertIntEquals(tc, 1, result->number_of_extents);
    CuAssertIntEquals(tc, 2, result->remapped_extents[0].length_in_blocks);
    CuAssertTrue(tc, result->remapped_extents[0].start_block == 12);
    disk_tracker_free_remap(tracker, result);
    result = NULL;

    // Test 4: intersecting interval, must return 2 intervals
    extent.start_block = 45;
    extent.length_in_blocks = 7;
    status = disk_tracker_find_remap(tracker, &extent, &result); 
    CuAssertIntEquals(tc, DISK_TRACKER_OK, status);
    CuAssertPtrNotNull(tc, result);
    CuAssertIntEquals(tc, 2, result->number_of_extents);
    CuAssertIntEquals(tc, 5, result->remapped_extents[0].length_in_blocks);
    CuAssertIntEquals(tc, 2, result->remapped_extents[1].length_in_blocks);
    CuAssertTrue(tc, result->remapped_extents[0].start_block == 45);
    CuAssertTrue(tc, result->remapped_extents[1].start_block == 10);
    disk_tracker_free_remap(tracker, result);
    result = NULL;

    // Test 5: encompassing interval: must return 3 intervals
    extent.start_block = 47;
    extent.length_in_blocks = 10;
    status = disk_tracker_find_remap(tracker, &extent, &result); 
    CuAssertIntEquals(tc, DISK_TRACKER_OK, status);
    CuAssertPtrNotNull(tc, result);
    CuAssertIntEquals(tc, 3, result->number_of_extents);
    CuAssertIntEquals(tc, 3, result->remapped_extents[0].length_in_blocks);
    CuAssertIntEquals(tc, 5, result->remapped_extents[1].length_in_blocks);
    CuAssertIntEquals(tc, 2, result->remapped_extents[2].length_in_blocks);
    CuAssertLongLongEquals(tc, 47, result->remapped_extents[0].start_block);
    CuAssertLongLongEquals(tc, 10, result->remapped_extents[1].start_block);
    CuAssertLongLongEquals(tc, 55, result->remapped_extents[2].start_block);
    disk_tracker_free_remap(tracker, result);
    result = NULL;

    // Test 6: remap intersecting, must return newly allocated and previously allocated 
    // (3 blocks left)
    extent.start_block = 48;
    extent.length_in_blocks = 5;
    status = disk_tracker_remap(tracker, &extent, &result);
    CuAssertIntEquals(tc, DISK_TRACKER_OK, status);
    CuAssertPtrNotNull(tc, result);
    CuAssertIntEquals(tc, 2, result->number_of_extents);
    CuAssertIntEquals(tc, 2, result->remapped_extents[0].length_in_blocks);
    CuAssertIntEquals(tc, 3, result->remapped_extents[1].length_in_blocks);
    CuAssertLongLongEquals(tc, 15, result->remapped_extents[0].start_block);
    CuAssertLongLongEquals(tc, 10, result->remapped_extents[1].start_block);
    disk_tracker_free_remap(tracker, result);
    result = NULL;

    // Test 7: remap another 6 blocks. First storage extent is exhausted, so we switch to 
    // second, which starts from block #100
    extent.start_block = 60;
    extent.length_in_blocks = 6;
    status = disk_tracker_remap(tracker, &extent, &result);
    CuAssertIntEquals(tc, DISK_TRACKER_OK, status);
    CuAssertPtrNotNull(tc, result);
    CuAssertIntEquals(tc, 2, result->number_of_extents);
    CuAssertIntEquals(tc, 3, result->remapped_extents[0].length_in_blocks);
    CuAssertIntEquals(tc, 3, result->remapped_extents[1].length_in_blocks);
    CuAssertLongLongEquals(tc, 17, result->remapped_extents[0].start_block);
    CuAssertLongLongEquals(tc, 100, result->remapped_extents[1].start_block);
    disk_tracker_free_remap(tracker, result);
    result = NULL;


    // Test get all extents. We remapped 50:5, 48:5, 60:6. We should get two continuous remaps:
    // 48:7 and 60:6
    status = disk_tracker_get_all_remaps(tracker, &remaps_count, &remaps);
    CuAssertIntEquals(tc, 2, remaps_count);
    CuAssertLongLongEquals(tc, 48, remaps[0]->source_extent.start_block);
    CuAssertLongLongEquals(tc,  7, remaps[0]->source_extent.length_in_blocks);
    CuAssertLongLongEquals(tc, 60, remaps[1]->source_extent.start_block);
    CuAssertLongLongEquals(tc,  6, remaps[1]->source_extent.length_in_blocks);
    CuAssertIntEquals(tc, 2, remaps[1]->number_of_extents);
    CuAssertIntEquals(tc, 3, remaps[1]->remapped_extents[0].length_in_blocks);
    CuAssertIntEquals(tc, 3, remaps[1]->remapped_extents[1].length_in_blocks);
    CuAssertLongLongEquals(tc, 17, remaps[1]->remapped_extents[0].start_block);
    CuAssertLongLongEquals(tc, 100, remaps[1]->remapped_extents[1].start_block);
    disk_tracker_free_remap(tracker, (struct disk_extent_remap*)remaps);

    // Sanity check
    extent.start_block = 60;
    extent.length_in_blocks = 6;
    status = disk_tracker_find_remap(tracker, &extent, &result);
    CuAssertIntEquals(tc, DISK_TRACKER_OK, status);
    CuAssertPtrNotNull(tc, result);
    CuAssertIntEquals(tc, 2, result->number_of_extents);
    CuAssertIntEquals(tc, 3, result->remapped_extents[0].length_in_blocks);
    CuAssertIntEquals(tc, 3, result->remapped_extents[1].length_in_blocks);
    CuAssertLongLongEquals(tc, 17, result->remapped_extents[0].start_block);
    CuAssertLongLongEquals(tc, 100, result->remapped_extents[1].start_block);
    disk_tracker_free_remap(tracker, result);
    result = NULL;


    // Test reset
    status = disk_tracker_reset(tracker);
    CuAssertIntEquals(tc, DISK_TRACKER_OK, status);
    status = disk_tracker_get_storage_info(tracker, &total_blocks, &free_blocks);
    CuAssertIntEquals(tc, DISK_TRACKER_OK, status);
    CuAssertIntEquals(tc, 20, total_blocks);
    CuAssertIntEquals(tc, 20, free_blocks);

    // Test reset storage
    status = disk_tracker_reset_storage(tracker, create_storage_for_reset());
    CuAssertIntEquals(tc, DISK_TRACKER_OK, status);
    status = disk_tracker_get_storage_info(tracker, &total_blocks, &free_blocks);
    CuAssertIntEquals(tc, DISK_TRACKER_OK, status);
    CuAssertIntEquals(tc, 12, total_blocks);
    CuAssertIntEquals(tc, 12, free_blocks);

    disk_tracker_destroy(&tracker);
}

void test_quick_sort_simple(CuTest* tc)
{
    ulong64_t arr[] = {4, 10, 0, 1, 12};

    quick_sort_ulong64(arr, 5);

    CuAssertLongLongEquals(tc, 0, arr[0]);
    CuAssertLongLongEquals(tc, 1, arr[1]);
    CuAssertLongLongEquals(tc, 4, arr[2]);
    CuAssertLongLongEquals(tc, 10, arr[3]);
    CuAssertLongLongEquals(tc, 12, arr[4]);

}

void test_quick_sort_big_array(CuTest* tc)
{
#define NUM_ELEMS 5000
    
    int i;
    ulong64_t* arr = (ulong64_t*)malloc(NUM_ELEMS*sizeof(ulong64_t));

    for (i = 0; i < NUM_ELEMS; i++)
    {
        arr[i] = ((ulong64_t)rand()) * rand();
    }
    
    quick_sort_ulong64(arr, NUM_ELEMS);
    for (i = 1; i < NUM_ELEMS; i++)
    {
        CuAssert(tc, "Array must be sorted", arr[i] >= arr[i - 1]);
    }
    free(arr);
}



CuSuite* get_test_suite()
{
    CuSuite* suite = CuSuiteNew();

    SUITE_ADD_TEST(suite, test_quick_sort_simple);
    SUITE_ADD_TEST(suite, test_quick_sort_big_array);
    SUITE_ADD_TEST(suite, test_disk_tracker);

    return suite;
}

void run_all_cunit_tests()
{
    CuString* output = CuStringNew();
    CuSuite* test_suite = CuSuiteNew();

    CuSuiteAddSuite(test_suite, get_test_suite());
    CuSuiteRun(test_suite);
    CuSuiteSummary(test_suite, output);
    CuSuiteDetails(test_suite, output);
    printf("%s\n", output->buffer);
}

int __cdecl main()
{
    run_all_cunit_tests();

    return 1;
}
