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
#include <ntifs.h>
#include <stddef.h>
#include "disk_filter.h"


#define MAX_DEV_EXTENSIONS 16

static struct filter_dev_ext_info filter_device_extensions[MAX_DEV_EXTENSIONS];
static unsigned current_dev_ext = 0;


NTSTATUS create_filter_control_device(PDRIVER_OBJECT driver_obj, 
                                      DEVICE_OBJECT** out_control_dev_obj);

/***************************************************************************
   Hashtable support routines
*/
void* diskf_malloc(unsigned c) 
{ 
    return ExAllocatePoolWithTag(NonPagedPool | POOL_RAISE_IF_ALLOCATION_FAILURE, 
        c, 0xAABBCCDD);
}

void diskf_free(void* p) 
{ 
    ExFreePool(p);
}

/***************************************************************************
   Exports
*/

NTSTATUS difi_start_tracking_device(struct filter_device_extension *dev_ext, BOOLEAN simulate)
{
    dev_ext->simulate = simulate;
    
    /* Do it */
    DbgPrint("TRACKING device %u. Simulate: %d\n", dev_ext->dev_index, simulate);
    dev_ext->track_this = TRUE;

    return STATUS_SUCCESS;
}

NTSTATUS difi_stop_tracking_device_discard(struct filter_device_extension *dev_ext)
{
    /* Do it */
    DbgPrint("STOP TRACKING device %u.\n", dev_ext->dev_index);
    dev_ext->track_this = FALSE;
    disk_tracker_reset(dev_ext->remapper);

    return STATUS_SUCCESS;
}


NTSTATUS difi_start_tracking_device_by_name(const WCHAR* dev_name, BOOLEAN simulate);

int difi_start_tracking_device_by_index(unsigned dev_index, int simulate)
{
    if(dev_index >= current_dev_ext)
        return STATUS_INVALID_PARAMETER;
    
    if (filter_device_extensions[dev_index].dev_ext == NULL)
        return STATUS_INVALID_PARAMETER;
    
    return difi_start_tracking_device(
        filter_device_extensions[dev_index].dev_ext, (BOOLEAN)simulate);
    
}

int difi_stop_tracking_device_by_index_discard(unsigned dev_index)
{
    if(dev_index >= current_dev_ext)
        return STATUS_INVALID_PARAMETER;
    
    if (filter_device_extensions[dev_index].dev_ext == NULL)
        return STATUS_INVALID_PARAMETER;
    
    return difi_stop_tracking_device_discard(
        filter_device_extensions[dev_index].dev_ext);
    
}

NTSTATUS difi_driver_send_to_next_driver(PDEVICE_OBJECT dev_obj, PIRP irp)
{
    NTSTATUS status;
    struct filter_device_extension *dev_ext;
    PIO_STACK_LOCATION stack = IoGetCurrentIrpStackLocation(irp);

    dev_ext = (struct filter_device_extension *)dev_obj->DeviceExtension;
    if (dev_ext == NULL )
    {
        DbgPrint("difi_driver_send_to_next_driver: Device extension is null!\n");
        return STATUS_UNSUCCESSFUL;
    }

    stack;
    ASSERT(dev_ext->common_data.dev_type == DEVICE_TYPE_FILTER);

    /* We're not going to do anything with the IRP, 
       so skip the current stack location 
     */
    IoSkipCurrentIrpStackLocation(irp);
    status = IoCallDriver(dev_ext->target_device_obj, irp);
    return status;
}

NTSTATUS difi_driver_create_close(PDEVICE_OBJECT dev_obj, PIRP irp)
{
    struct common_device_data* dev_data = 
        (struct common_device_data*)dev_obj->DeviceExtension;
    if (dev_data->dev_type == DEVICE_TYPE_FILTER)
        return difi_driver_send_to_next_driver(dev_obj, irp);
    
    irp->IoStatus.Status = STATUS_SUCCESS;
    irp->IoStatus.Information = 0;
    IoCompleteRequest( irp, IO_NO_INCREMENT );
    return STATUS_SUCCESS;
}




VOID difi_driver_unload(__in PDRIVER_OBJECT drv_obj)
{
    PDEVICE_OBJECT dev_obj = drv_obj->DeviceObject;        
    
    if ( dev_obj )
        IoDeleteDevice(dev_obj); 
}


static void
ioctl_to_remap_storage(struct ioctl_difi_storage_info* info,
                       struct remap_storage** out)
{
    unsigned sz = sizeof(struct remap_storage) + 
                  sizeof(struct disk_extent) * (info->extent_count - 1);
    unsigned i;
    struct remap_storage* storage = (struct remap_storage*)
        diskf_malloc(sz);

    RtlZeroMemory(storage, sz);
    
    storage->number_of_extents = info->extent_count;
    storage->number_of_blocks  = (ulong32_t)(info->total_size / 512);    // FIXME: wrong!
    for (i = 0; i < info->extent_count; i++)
    {
        storage->extents[i].start_block      = info->extents[i].start_lba;
        storage->extents[i].length_in_blocks = info->extents[i].length_in_sectors;
    }
    *out = storage;
}

NTSTATUS send_irp_sync_completion(
    PDEVICE_OBJECT  dev_obj,
    PIRP            irp,
    PVOID           context);


NTSTATUS send_irp_sync(
    PIRP        irp,
    PKEVENT     event)
{
    PIO_STACK_LOCATION next_stack = IoGetNextIrpStackLocation(irp);
    NTSTATUS status;

    KeClearEvent (event);
    IoSetCompletionRoutine(irp, send_irp_sync_completion, event, TRUE, TRUE, TRUE);
    status = IoCallDriver(next_stack->DeviceObject, irp);
    if (status == STATUS_PENDING) {
        KeWaitForSingleObject(event, Executive, KernelMode, FALSE, NULL);
        DbgPrint("send_irp_async: status %x information: %u",
                 irp->IoStatus.Status, irp->IoStatus.Information);
        status = irp->IoStatus.Status;
    }
    return status;
}


NTSTATUS send_irp_sync_completion(
    PDEVICE_OBJECT  dev_obj,
    PIRP            irp,
    PVOID           context)
{
    dev_obj;
    irp;
    
    KeSetEvent((PKEVENT) context, IO_NO_INCREMENT, FALSE);
    return STATUS_MORE_PROCESSING_REQUIRED;
}

NTSTATUS 
difi_add_or_init_storage(struct control_device_extension* control_dev_ext,
                            struct ioctl_difi_storage_info* info,
                            BOOLEAN force_reset)
{
    struct remap_storage* remap_stor = NULL;
    
    ioctl_to_remap_storage(info, &remap_stor);
    DbgPrint("Adding storage extents: %d total number of blocks: %d "
             "sector size: %u cluster size% u",
             remap_stor->number_of_extents, remap_stor->number_of_blocks,
             info->sector_size, info->cluster_size);

    if(control_dev_ext->dev_ext->remapper == NULL) {
        DbgPrint("Difi: initializing disk tracker");
        control_dev_ext->dev_ext->remapper = 
            disk_tracker_init(diskf_malloc, diskf_free, remap_stor);
        if (control_dev_ext->dev_ext->remapper == NULL) {
            DbgPrint("difi: failed to allocate mapper");
            return STATUS_NO_MEMORY;
        }
    } else {
        if (force_reset) {
            DbgPrint("Resetting storage");
            disk_tracker_reset_storage(control_dev_ext->dev_ext->remapper, remap_stor);
        }
        else {
            DbgPrint("Adding storage");
            disk_tracker_add_storage(control_dev_ext->dev_ext->remapper, remap_stor);
        }
    }
    return STATUS_SUCCESS;
}


NTSTATUS difi_driver_ioctl(PDEVICE_OBJECT dev_obj, PIRP irp)
{
    NTSTATUS status = STATUS_INVALID_DEVICE_REQUEST;
    PIO_STACK_LOCATION irp_stack = IoGetCurrentIrpStackLocation(irp);
    unsigned long control_code = irp_stack->Parameters.DeviceIoControl.IoControlCode;
    struct control_device_extension* control_dev_ext = //control_dev_ext->dev_ext;
        (struct control_device_extension*)dev_obj->DeviceExtension;

    DbgPrint("difi_driver_ioctl: DeviceObject %X Irp %X Code: %x\n", dev_obj, irp, control_code);

    /* If this is the filter device, pass through all requests */
    if(control_dev_ext->common_data.dev_type == DEVICE_TYPE_FILTER) {
        if (control_code == IOCTL_VOLSNAP_FLUSH_AND_HOLD_WRITES ) {
            DbgPrint("!!! Caught IOCTL_VOLSNAP_FLUSH_AND_HOLD_WRITES\n");
        }
        
        return difi_driver_send_to_next_driver(dev_obj, irp);
    }
    
    ASSERT(control_dev_ext->common_data.dev_type == DEVICE_TYPE_CONTROL);
    irp->IoStatus.Information = 0;
    
    switch(control_code)
    {
        case IOCTL_DIFI_GET_STATS:
        {
            struct ioctl_difi_stats* stats = NULL;
            if (irp_stack->Parameters.DeviceIoControl.OutputBufferLength <
                sizeof(*stats)) {
                status = STATUS_BUFFER_TOO_SMALL;
                DbgPrint("buffer is too small!");
                break;
            }
            stats = (struct ioctl_difi_stats*)irp->AssociatedIrp.SystemBuffer;
            RtlCopyMemory(stats, &control_dev_ext->dev_ext->stats, sizeof(*stats));
            if(control_dev_ext->dev_ext->remapper != NULL) {
                disk_tracker_get_hash_size(control_dev_ext->dev_ext->remapper, 
                                           &stats->hash_size);
            }

            DbgPrint("Difi disk filter stats\n"
                     "  hash size          : %u\n"
                     "  write hits         : %llu\n"
                     "  read hits          : %llu\n"
                     "  reads at PASSIVE   : %llu\n"
                     "  reads at APC       : %llu\n"
                     "  reads at DISPATCH  : %llu\n"
                     "  reads at other     : %llu\n"
                     "  writes at PASSIVE  : %llu\n"
                     "  writes at APC      : %llu\n"
                     "  writes at DISPATCH : %llu\n"
                     "  writes at other    : %llu\n",
                     stats->hash_size, 
                     stats->write_hits, 
                     stats->read_hits,
                     stats->per_irql_reads[0],
                     stats->per_irql_reads[1],
                     stats->per_irql_reads[2],
                     stats->per_irql_reads[3],
                     stats->per_irql_writes[0],
                     stats->per_irql_writes[1],
                     stats->per_irql_writes[2],
                     stats->per_irql_writes[3]
                     );
            
            irp->IoStatus.Information = sizeof(*stats);
            status = STATUS_SUCCESS;
            break;
        }

        case IOCTL_DIFI_INITIALIZE:
        {
            struct ioctl_difi_storage_info* info = NULL;
            struct ioctl_difi_disk_initialize* init = NULL;
            
            DbgPrint("difi_initialize");
            if (irp_stack->Parameters.DeviceIoControl.InputBufferLength < 
                sizeof(struct ioctl_difi_disk_initialize)) {
                status = STATUS_INVALID_PARAMETER;
                break;
            }
            init = (struct ioctl_difi_disk_initialize*)irp->AssociatedIrp.SystemBuffer;
            if (init == NULL) {
                status = STATUS_INVALID_PARAMETER;
                break;
            }
            control_dev_ext->dev_ext->low_storage_percentage = 
                init->low_storage_space_percentage;
            //control_dev_ext->dev_ext->need_more_storage_event = init->need_more_storage_event;
            info = &init->initial_storage;
            control_dev_ext->dev_ext->sector_size = info->sector_size;
            control_dev_ext->dev_ext->cluster_size = info->cluster_size;
            // Initial storage can be NULL
            if (info->extent_count != 0) {
                status = difi_add_or_init_storage(control_dev_ext, info, TRUE);
            } else {
                status = STATUS_SUCCESS;
            }
            break;
        }

        
        case IOCTL_DIFI_ADD_STORAGE:
        {
            struct ioctl_difi_storage_info* info = NULL;

            DbgPrint("difi_add_storage");
            
            if (irp_stack->Parameters.DeviceIoControl.InputBufferLength <
                sizeof(struct ioctl_difi_storage_info)) {
                status = STATUS_INVALID_PARAMETER;
                DbgPrint("buffer is too small!");
                break;
            }
            info = (struct ioctl_difi_storage_info*)irp->AssociatedIrp.SystemBuffer;
            
            status = difi_add_or_init_storage(control_dev_ext, info, FALSE);
            
            break;
        }

        case IOCTL_DIFI_TRACK_DISK:
        {
            struct ioctl_difi_track_disk* track_dsk = NULL;

            if (control_dev_ext->dev_ext->remapper == NULL) {
                /* Cannot track disk without storage */
                DbgPrint("Unable to track disk, no remap storage\n");
                status = STATUS_UNSUCCESSFUL;
                break;
            }
            if (control_dev_ext->dev_ext->track_this) {
                /* Cannot track disk without storage */
                DbgPrint("Already tracking this disk\n");
                status = STATUS_UNSUCCESSFUL;
                break;
            }
            
            if (irp_stack->Parameters.DeviceIoControl.InputBufferLength != 
                sizeof(struct ioctl_difi_track_disk)) {
                status = STATUS_INVALID_PARAMETER;
                break;
            }
            
            track_dsk = (struct ioctl_difi_track_disk*)irp->AssociatedIrp.SystemBuffer;
            
            status = difi_start_tracking_device(control_dev_ext->dev_ext, 
                                                  track_dsk->simulate);
            break;
        }

        case IOCTL_DIFI_STOP_TRACKING_DISCARD:
        {
            if (!control_dev_ext->dev_ext->track_this) {
                /* Cannot track disk without storage */
                DbgPrint("Not tracking this disk\n");
                status = STATUS_UNSUCCESSFUL;
                break;
            }
            status = difi_stop_tracking_device_discard(control_dev_ext->dev_ext);
            break;
        }

        case IOCTL_DIFI_TEST_STORAGE:
        {
            struct remap_storage* storage = NULL;
            PDEVICE_OBJECT        targ_dev_obj;
            PIRP                  irp;
            PIO_STACK_LOCATION    next_stack;
            CHAR*                 sector;
            PMDL                  mdl;

            if (control_dev_ext->dev_ext->remapper == NULL) {
                /* Cannot track disk without storage */
                DbgPrint("unable to test storage, no remapper\n");
                status = STATUS_UNSUCCESSFUL;
                break;
            }
            
            if (control_dev_ext->dev_ext->track_this) {
                /* Cannot track disk without storage */
                DbgPrint("Unable to test storage while tracking disk!\n");
                status = STATUS_UNSUCCESSFUL;
                break;
            }

            disk_tracker_get_storage(control_dev_ext->dev_ext->remapper, &storage);
            if (storage == NULL) {
                /* Cannot track disk without storage */
                DbgPrint("Unable to test storage: no storage allocated!\n");
                status = STATUS_UNSUCCESSFUL;
                break;
            }

            ASSERT(storage->number_of_extents > 0);
            
           /* Storage test is semi-manual process: write some data to first and 
              last sector, then caller must open the file in hex editor and verify
              its validity
            */
            targ_dev_obj = control_dev_ext->dev_ext->target_device_obj;
            irp = IoAllocateIrp(targ_dev_obj->StackSize, FALSE);
            if (irp == NULL) {
               status = STATUS_NO_MEMORY;
               break;
            }

            sector = (CHAR*)ExAllocatePoolWithTag(NonPagedPool, BLOCK_SIZE, 0xAA);
            RtlZeroMemory(sector, BLOCK_SIZE);
            RtlCopyMemory(sector, "*    Test sector", sizeof("*    Test sector"));
            sector[BLOCK_SIZE - 1] = '*';
            mdl = IoAllocateMdl(sector, BLOCK_SIZE, FALSE, FALSE, NULL);
            MmBuildMdlForNonPagedPool(mdl);
            
            irp->Flags = 0;
            irp->RequestorMode = KernelMode;
            irp->Tail.Overlay.Thread = 0;
            irp->IoStatus.Status = STATUS_NOT_SUPPORTED;
            irp->MdlAddress = mdl;

            next_stack = IoGetNextIrpStackLocation(irp);
            next_stack->MajorFunction = IRP_MJ_WRITE;
            next_stack->MinorFunction = 0;
            next_stack->Flags = 0;
            next_stack->DeviceObject = targ_dev_obj;
            next_stack->Parameters.Write.Length = BLOCK_SIZE;
            next_stack->Parameters.Write.ByteOffset.QuadPart =
               storage->extents[0].start_block * BLOCK_SIZE;

            DbgPrint("test_storage: writing first block: len %x block: %llu offset: %llu",
                 next_stack->Parameters.Write.Length, 
                 storage->extents[0].start_block,
                 next_stack->Parameters.Write.ByteOffset.QuadPart);

            send_irp_sync(irp, &control_dev_ext->dev_ext->irp_complete_ev);

            IoReuseIrp(irp, STATUS_NOT_SUPPORTED);

            irp->Flags = 0;
            irp->RequestorMode = KernelMode;
            irp->Tail.Overlay.Thread = 0;
            irp->MdlAddress = mdl;

            next_stack = IoGetNextIrpStackLocation(irp);
            next_stack->MajorFunction = IRP_MJ_WRITE;
            next_stack->MinorFunction = 0;
            next_stack->Flags = 0;
            next_stack->DeviceObject = targ_dev_obj;
            next_stack->Parameters.Write.Length = BLOCK_SIZE;
            next_stack->Parameters.Write.ByteOffset.QuadPart =
               (storage->extents[storage->number_of_extents - 1].start_block + 
                storage->extents[storage->number_of_extents - 1].length_in_blocks - 1)* BLOCK_SIZE;
           
            DbgPrint("test_storage: writing last block: len %x block: %llu offset: %llu",
                 next_stack->Parameters.Write.Length, 
                 storage->extents[storage->number_of_extents - 1].start_block + 
                 storage->extents[storage->number_of_extents - 1].length_in_blocks - 1,
                 next_stack->Parameters.Write.ByteOffset.QuadPart);

            send_irp_sync(irp, &control_dev_ext->dev_ext->irp_complete_ev);
            
            IoFreeIrp(irp);
            
            status = STATUS_SUCCESS;
            break;
        }

        case IOCTL_DIFI_GET_INFO: 
        {
            struct ioctl_difi_diskf_info* info = NULL;
            if (irp_stack->Parameters.DeviceIoControl.OutputBufferLength <
                sizeof(*info)) {
                status = STATUS_BUFFER_TOO_SMALL;
                DbgPrint("buffer is too small!");
                break;
            }
            info = (struct ioctl_difi_diskf_info*)irp->AssociatedIrp.SystemBuffer;
            RtlZeroMemory(info, sizeof(*info));
            info->size = sizeof(*info);
            info->is_tracking = control_dev_ext->dev_ext->track_this;

            if(control_dev_ext->dev_ext->remapper != NULL) {
                disk_tracker_get_hash_size(control_dev_ext->dev_ext->remapper, 
                                           &info->remapped_blocks);
                disk_tracker_get_storage_info(control_dev_ext->dev_ext->remapper, 
                                              &info->total_blocks,
                                              &info->free_blocks);
            }

            status = STATUS_SUCCESS;
            break;
        }

        default: 
        {
            status = STATUS_INVALID_PARAMETER;
            break;
        }
    }

    
    irp->IoStatus.Status = status;
    IoCompleteRequest( irp, IO_NO_INCREMENT );
    return status;
}

NTSTATUS difi_driver_shutdown(PDEVICE_OBJECT dev_obj, PIRP irp)
{
    NTSTATUS status;
    struct common_device_data* dev_data = 
        (struct common_device_data*)dev_obj->DeviceExtension;
    if (dev_data->dev_type == DEVICE_TYPE_FILTER)
        return difi_driver_send_to_next_driver(dev_obj, irp);

    status = STATUS_SUCCESS;
    irp->IoStatus.Information = 0;
    irp->IoStatus.Status = status;
    
    return status;
}

NTSTATUS difi_driver_pnp(PDEVICE_OBJECT dev_obj, PIRP irp)
{
    NTSTATUS status;
    struct common_device_data* dev_data = 
        (struct common_device_data*)dev_obj->DeviceExtension;
    if (dev_data->dev_type == DEVICE_TYPE_FILTER)
        return difi_driver_send_to_next_driver(dev_obj, irp);

    status = STATUS_NOT_SUPPORTED;
    irp->IoStatus.Information = 0;
    irp->IoStatus.Status = status;
    
    return status;
}

NTSTATUS difi_driver_power(PDEVICE_OBJECT dev_obj, PIRP irp)
{
    NTSTATUS status;
    struct common_device_data* dev_data = 
        (struct common_device_data*)dev_obj->DeviceExtension;
    if (dev_data->dev_type == DEVICE_TYPE_FILTER)
        return difi_driver_send_to_next_driver(dev_obj, irp);

    status = STATUS_NOT_SUPPORTED;
    irp->IoStatus.Information = 0;
    irp->IoStatus.Status = status;
    
    return status;
}


NTSTATUS difi_driver_add_device(PDRIVER_OBJECT driver_obj, 
                                   PDEVICE_OBJECT phys_dev_obj)
{
    NTSTATUS         status;
    PDEVICE_OBJECT   dev_obj = NULL;
    PDEVICE_OBJECT   control_dev_obj = NULL;
    struct filter_device_extension *dev_ext = NULL;
    struct control_device_extension* control_dev_ext = NULL;

    DbgPrint("diskf_driver_add_device: : %#p\n", phys_dev_obj);

    status = IoCreateDevice( driver_obj, 
                             sizeof(struct filter_device_extension), 
                             NULL, 
                             FILE_DEVICE_DISK,
                             FILE_DEVICE_SECURE_OPEN, 
                             FALSE, &dev_obj );
    if ( status != STATUS_SUCCESS )
    {
        DbgPrint("Create device failed - %x\n", status);
        return status;
    }

    dev_obj->Flags |= DO_DIRECT_IO;
    
    dev_ext = dev_obj->DeviceExtension;

    RtlZeroMemory(dev_ext, sizeof(struct filter_device_extension));

    dev_ext->common_data.dev_type = DEVICE_TYPE_FILTER;
    dev_ext->phys_device_obj = phys_dev_obj;
    dev_ext->target_device_obj = IoAttachDeviceToDeviceStack(dev_obj, phys_dev_obj);
    if ( dev_ext->target_device_obj == NULL )
    {
        DbgPrint("Unable to attach device to device stack!\n");
        IoDeleteDevice(dev_obj);
        return STATUS_UNSUCCESSFUL;
    }

    dev_ext->device_obj = dev_obj;

    KeInitializeEvent(&dev_ext->irp_complete_ev, NotificationEvent, FALSE);

    dev_obj->Flags &= ~DO_DEVICE_INITIALIZING;
    
    /*
     * MSDN says to create separate control device for the filter.
     */
    status = create_filter_control_device(driver_obj, 
                                          &control_dev_obj);
    if (!NT_SUCCESS(status)) 
    {   
        DbgPrint("Unable to create device control object!\n");
        IoDetachDevice(dev_ext->target_device_obj);
        IoDeleteDevice(dev_obj);
        return status;
    }

    control_dev_ext = (struct control_device_extension*)control_dev_obj->DeviceExtension;
    RtlZeroMemory(control_dev_ext, sizeof(struct control_device_extension));
    control_dev_ext->common_data.dev_type = DEVICE_TYPE_CONTROL;
    control_dev_ext->dev_ext = dev_ext;

    /* Save filter device extension in global array for easy access later */
    dev_ext->dev_index = current_dev_ext++;
    filter_device_extensions[dev_ext->dev_index].dev_ext = dev_ext;

    control_dev_obj->Flags &= ~DO_DEVICE_INITIALIZING;

    return STATUS_SUCCESS;
}

NTSTATUS create_filter_control_device(PDRIVER_OBJECT driver_obj, 
                                      DEVICE_OBJECT** out_control_dev_obj)
{
    WCHAR           name_buf[51];
    WCHAR           sym_name_buf[51];
    UNICODE_STRING  dev_name;
    UNICODE_STRING  dos_dev_name;
    NTSTATUS        status;
    static unsigned dev_num = 0;

    name_buf[0] = L'\0';
    dev_name.Length = 0;
    dev_name.MaximumLength = sizeof(name_buf);
    dev_name.Buffer = name_buf;
    
    status = RtlUnicodeStringPrintf(
            &dev_name,
            L"%s%d",
            DIFI_DISKF_FILT_CONTROL_DEVICE_NAME,
            dev_num);
    if (!NT_SUCCESS(status)) {
        // Weird
        return status;
    }

    status = IoCreateDevice( driver_obj,
                             sizeof(struct control_device_extension),
                             &dev_name,
                             FILE_DEVICE_UNKNOWN,
                             FILE_DEVICE_SECURE_OPEN,
                             FALSE,
                             out_control_dev_obj );
    if (!NT_SUCCESS(status)) 
    {
        DbgPrint("CreateDevice -  IoCreateDevice() failed with "
            "status %#010x\n",
            status);
        return status;
    }
    (*out_control_dev_obj)->Flags |= DO_BUFFERED_IO;

    sym_name_buf[0] = L'\0';
    dos_dev_name.Length = 0;
    dos_dev_name.MaximumLength = sizeof(sym_name_buf);
    dos_dev_name.Buffer = sym_name_buf;

    status = RtlUnicodeStringPrintf(
            &dos_dev_name,
            L"%s%d",
            DOS_DIFI_DISKF_CONTROL_DEVICE_NAME,
            dev_num);
    if (!NT_SUCCESS(status)) {
        // Weird
        IoDeleteDevice(*out_control_dev_obj); 
        DbgPrint("RtlUnicodeStringPrintf - %x\n", status);
        return status;
    }

    status = IoCreateSymbolicLink(&dos_dev_name, &dev_name);
    if ( status != STATUS_SUCCESS ) {
        IoDeleteDevice(*out_control_dev_obj); 
        DbgPrint("Create symbolic link failed - %x\n", status);
        return status;
    }
    
    dev_num++;

    return status;
}


NTSTATUS DriverEntry(__in PDRIVER_OBJECT driver_obj, __in PUNICODE_STRING  registry_path)
{
    PDRIVER_DISPATCH *dispatch_fn;
    unsigned long   index;
    UNICODE_STRING  dev_name;
    UNICODE_STRING  dos_dev_name;
    PDEVICE_OBJECT  dev_object = NULL;
    NTSTATUS status;
    struct main_driver_device_extension* dev_ext;

    UNREFERENCED_PARAMETER(registry_path);

    RtlInitUnicodeString(&dev_name, DIFI_DISKF_DEVICE_NAME );

    status = IoCreateDevice( driver_obj, sizeof(struct main_driver_device_extension), 
                             &dev_name, FILE_DEVICE_UNKNOWN,
                             FILE_DEVICE_SECURE_OPEN, FALSE, &dev_object );
    if ( status != STATUS_SUCCESS )
    {
        DbgPrint("Create device failed - %x\n", status);
        return status;
    }

    RtlInitUnicodeString(&dos_dev_name, DOS_DIFI_DISKF_DEVICE_NAME);

    status = IoCreateSymbolicLink(&dos_dev_name, &dev_name);
    if ( status != STATUS_SUCCESS ) {
        IoDeleteDevice(dev_object); 
        DbgPrint("Create symbolic link failed - %x\n", status);
        return status;
    }

    dev_ext = (struct main_driver_device_extension*)dev_object->DeviceExtension;
    RtlZeroMemory(dev_ext, sizeof(*dev_ext));
    dev_ext->common_data.dev_type = DEVICE_TYPE_MAIN;

    RtlZeroMemory(&filter_device_extensions[0], sizeof(filter_device_extensions));
    
    for (index=0, dispatch_fn = driver_obj->MajorFunction; 
         index <= IRP_MJ_MAXIMUM_FUNCTION; 
         index++, dispatch_fn++ )
    {
        *dispatch_fn = difi_driver_send_to_next_driver;
    }

    driver_obj->MajorFunction[IRP_MJ_CREATE]   = difi_driver_create_close;
    driver_obj->MajorFunction[IRP_MJ_CLOSE]    = difi_driver_create_close;
    driver_obj->MajorFunction[IRP_MJ_CLEANUP]  = difi_driver_create_close;
    driver_obj->MajorFunction[IRP_MJ_READ]     = difi_driver_read;
    driver_obj->MajorFunction[IRP_MJ_WRITE]    = difi_driver_write;
    driver_obj->MajorFunction[IRP_MJ_DEVICE_CONTROL] = difi_driver_ioctl;
    driver_obj->MajorFunction[IRP_MJ_SHUTDOWN] = difi_driver_shutdown;
    driver_obj->MajorFunction[IRP_MJ_PNP] = difi_driver_pnp;
    driver_obj->MajorFunction[IRP_MJ_POWER] = difi_driver_power;
    
    driver_obj->DriverExtension->AddDevice = difi_driver_add_device;  
    driver_obj->DriverUnload = difi_driver_unload;

    return STATUS_SUCCESS;
}
