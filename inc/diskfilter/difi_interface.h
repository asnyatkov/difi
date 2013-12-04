#pragma once
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

  Public driver interface

*/
#define DIFI_IOCTL_CODE_BASE  0x800L

#define IOCTL_DIFI_TRACK_DISK     \
    (ULONG)CTL_CODE(FILE_DEVICE_UNKNOWN, DIFI_IOCTL_CODE_BASE + 0, \
                METHOD_BUFFERED, FILE_WRITE_ACCESS)

#define IOCTL_DIFI_STOP_TRACKING_DISCARD     \
    (ULONG)CTL_CODE(FILE_DEVICE_UNKNOWN, DIFI_IOCTL_CODE_BASE + 1, \
                METHOD_BUFFERED, FILE_WRITE_ACCESS)

#define IOCTL_DIFI_STOP_TRACKING_MERGE     \
    (ULONG)CTL_CODE(FILE_DEVICE_UNKNOWN, DIFI_IOCTL_CODE_BASE + 2, \
                METHOD_BUFFERED, FILE_WRITE_ACCESS)

#define IOCTL_DIFI_ADD_STORAGE     \
    (ULONG)CTL_CODE(FILE_DEVICE_UNKNOWN, DIFI_IOCTL_CODE_BASE + 3, \
                METHOD_BUFFERED, FILE_WRITE_ACCESS)

#define IOCTL_DIFI_INITIALIZE     \
    (ULONG)CTL_CODE(FILE_DEVICE_UNKNOWN, DIFI_IOCTL_CODE_BASE + 4, \
                METHOD_BUFFERED, FILE_WRITE_ACCESS)

#define IOCTL_DIFI_GET_STATS     \
    (ULONG)CTL_CODE(FILE_DEVICE_UNKNOWN, DIFI_IOCTL_CODE_BASE + 5, \
                METHOD_BUFFERED, FILE_READ_ACCESS)

#define IOCTL_DIFI_TEST_STORAGE     \
    (ULONG)CTL_CODE(FILE_DEVICE_UNKNOWN, DIFI_IOCTL_CODE_BASE + 6, \
                METHOD_BUFFERED, FILE_WRITE_ACCESS)

#define IOCTL_DIFI_GET_INFO     \
    (ULONG)CTL_CODE(FILE_DEVICE_UNKNOWN, DIFI_IOCTL_CODE_BASE + 7, \
                METHOD_BUFFERED, FILE_READ_ACCESS)

#ifndef MAX_PATH
#define MAX_PATH 260
#endif


#define DIFI_DISKF_DEVICE_NAME      L"\\Device\\DifiDiskFilter"
#define DOS_DIFI_DISKF_DEVICE_NAME  L"\\??\\DifiDiskFilter"

#define DIFI_DISKF_CONTROL_DEVICE   L"DifiCtrlDrv"
#define DIFI_DISKF_FILT_CONTROL_DEVICE_NAME L"\\Device\\DifiCtrlDrv"
#define DOS_DIFI_DISKF_CONTROL_DEVICE_NAME  L"\\??\\DifiCtrlDrv"

struct ioctl_difi_extent {
    ULONGLONG start_lba;
    ULONG     length_in_sectors;
};

/*
   Input data for IOCTL_DIFI_ADD_STORAGE. Passes array of storage extents,
   each extent is represented by the starting LBA and length in sectors
*/

struct ioctl_difi_storage_info
{
    unsigned      size;                     /* Total size of this structure, in bytes */

    WCHAR         file_name[MAX_PATH];      /* Filename of this storage block */
    ULONG         extent_count;             /* Count of extents */
    ULONGLONG     total_size;
    ULONG         cluster_size;             /**/
    ULONG         sector_size;              /**/

    struct ioctl_difi_extent extents[1];
};

struct ioctl_difi_stats
{
    unsigned int        hash_size;
    unsigned long long  write_hits;
    unsigned long long  read_hits;
    unsigned long long  per_irql_writes[4];  // Counting 0-2, index 3 will contain all > 2
    unsigned long long  per_irql_reads[4];   // Counting 0-2, index 3 will contain all > 2
};

struct ioctl_difi_disk_initialize
{
    unsigned    size;                           /* Total size of this structure */
    int         low_storage_space_percentage;   /* If available space falls below this 
                                                   persentage, need_more_storage_event will
                                                   be triggered
                                                 */
    HANDLE      need_more_storage_event;

    struct  ioctl_difi_storage_info initial_storage;
};

struct ioctl_difi_track_disk
{
    BOOLEAN simulate;
};

struct ioctl_difi_track_disk_result
{
    int later;
};

struct ioctl_difi_diskf_info
{
    unsigned size;
    unsigned is_tracking;
    unsigned remapped_blocks;
    unsigned total_blocks;
    unsigned free_blocks;
    unsigned num_storage_infos;
};

