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
  
  Disk filter driver
*/
#ifndef DISK_FILTER_H
#define DISK_FILTER_H

#include <ntddk.h>
#include <Ntstrsafe.h >

#include "libcrt/hashtable.h"
#include "libutil/disk_tracker.h"
#include "diskfilter/difi_interface.h"

// FIXME: should be part of the IOCTL!
#define BLOCK_SIZE (512)


struct block_map_t
{
    unsigned long long id;          /* original block number */
    unsigned long long target;      /* target block */
};

/*
    Transfer_packet used by irp splitter. Temporary, most of the fields 
    will be removed
*/
struct transfer_packet
{
    ULONG   num_blocks;
    ULONG   transfer_length;
    unsigned long long current_offset;
    PIRP    irp;
    PIRP    orig_irp;
    PMDL    partial_mdl;
};

enum device_type {

    DEVICE_TYPE_INVALID = 0,         // Invalid Type;

    DEVICE_TYPE_FILTER,              // Device is a filter device.
    DEVICE_TYPE_CONTROL,             // Device is a control device.
    DEVICE_TYPE_MAIN
};

struct common_device_data
{
    enum device_type dev_type;
};

struct filter_device_extension
{
    struct common_device_data common_data;

    PDEVICE_OBJECT      device_obj;
    PDEVICE_OBJECT      target_device_obj;
    PDEVICE_OBJECT      phys_device_obj;
    BOOLEAN             track_this;         /* True if we track this disk */
    BOOLEAN             simulate;           /* True if we only simulate tracking */
    KEVENT              irp_complete_ev;
    disk_remap_t        remapper;
    unsigned            sector_size;
    unsigned            cluster_size;
    int                 low_storage_percentage;
    KEVENT              need_more_storage_event;
    unsigned            dev_index;          /* Internal index */
    
    struct ioctl_difi_stats stats;
};

struct control_device_extension
{
    struct common_device_data common_data;

    struct filter_device_extension* dev_ext;
};

struct main_driver_device_extension
{
    struct common_device_data common_data;
};

struct filter_dev_ext_info
{
    struct filter_device_extension* dev_ext;
};



void* diskf_malloc(unsigned c); 
void diskf_free(void* p); 


NTSTATUS difi_driver_read(PDEVICE_OBJECT dev_obj, PIRP irp);
NTSTATUS difi_driver_write(PDEVICE_OBJECT dev_obj, PIRP irp);


#define DEEFEE_DISKF_DEVIOTYPE 0xA001

#endif
