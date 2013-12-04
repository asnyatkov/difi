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
  
  Disk filter driver - read and write routines
*/
#include "disk_filter.h"


NTSTATUS split_irp(PDEVICE_OBJECT dev_obj, PIRP irp);
NTSTATUS split_irp_for_remap(PDEVICE_OBJECT dev_obj, PIRP irp, 
                             struct disk_extent_remap* remap);

NTSTATUS
create_transfer_packet(PDEVICE_OBJECT     dev_obj,
                       PIRP               orig_irp,
                       PIO_STACK_LOCATION orig_stack,
                       UCHAR              operation, 
                       ULONG              offset,
                       ULONG              transfer_len, 
                       ulong64_t          disk_loc, 
                       struct transfer_packet** result_out);
NTSTATUS
difi_transfer_completion(IN PDEVICE_OBJECT dev_obj, IN PIRP irp, IN PVOID context);

static void
dump_remap(const char* op, struct disk_extent* extent, 
            struct disk_extent_remap* remap_res);


NTSTATUS difi_driver_read(PDEVICE_OBJECT dev_obj, PIRP irp)
{
    NTSTATUS                  status;
    struct filter_device_extension* dev_ext = 
        (struct filter_device_extension *)dev_obj->DeviceExtension;
    PIO_STACK_LOCATION        stack = IoGetCurrentIrpStackLocation(irp);
    struct disk_extent        extent;
    struct disk_extent_remap* remap_res;
    
    dev_ext->stats.per_irql_reads[KeGetCurrentIrql() < 3 ? KeGetCurrentIrql() : 3]++;

    /* Always forward zero-length requests to the lower driver */
    if(stack->Parameters.Read.Length == 0 ||
       !dev_ext->track_this || dev_ext->remapper == NULL) {
        IoCopyCurrentIrpStackLocationToNext(irp);
        return IoCallDriver(dev_ext->target_device_obj, irp);
    }
    
    extent.start_block = stack->Parameters.Read.ByteOffset.QuadPart / BLOCK_SIZE;
    extent.length_in_blocks = stack->Parameters.Read.Length / BLOCK_SIZE;

    disk_tracker_find_remap(dev_ext->remapper, &extent, &remap_res);

    dump_remap("Read", &extent, remap_res);

    dev_ext->stats.read_hits += remap_res->num_remapped;

    if(dev_ext->simulate) {
        /* In simulation mode, just pass to the next driver */
        diskf_free(remap_res);
        IoCopyCurrentIrpStackLocationToNext(irp);
        return IoCallDriver(dev_ext->target_device_obj, irp);
    }
    

    status = split_irp_for_remap(dev_ext->target_device_obj, irp, remap_res);
    diskf_free(remap_res);
    return status;
}

NTSTATUS difi_driver_write(PDEVICE_OBJECT dev_obj, PIRP irp)
{
    NTSTATUS                  status;
    PVOID                     data;
    struct filter_device_extension* dev_ext;
    PIO_STACK_LOCATION        stack = IoGetCurrentIrpStackLocation(irp);
    struct disk_extent        extent;
    struct disk_extent_remap* remap_res;
    
    dev_ext = (struct filter_device_extension *)dev_obj->DeviceExtension;

    dev_ext->stats.per_irql_writes[KeGetCurrentIrql() < 3 ? KeGetCurrentIrql() : 3]++;

    /* Determine the buffering mode */
    if (dev_ext->target_device_obj->Flags & DO_DIRECT_IO) {
        PMDL mdl = irp->MdlAddress;
        data = (PVOID) MmGetSystemAddressForMdlSafe(mdl, LowPagePriority);
    } else {
        ASSERT(FALSE);
        data = irp->AssociatedIrp.SystemBuffer;
    }

    dev_ext->stats.per_irql_writes[KeGetCurrentIrql() < 3 ? KeGetCurrentIrql() : 3]++;

    /* Always forward zero-length requests to the lower driver */
    if(stack->Parameters.Write.Length == 0 ||
       !dev_ext->track_this || dev_ext->remapper == NULL) {
        IoCopyCurrentIrpStackLocationToNext(irp);
        return IoCallDriver(dev_ext->target_device_obj, irp);
    }

    extent.start_block = stack->Parameters.Write.ByteOffset.QuadPart / BLOCK_SIZE;
    extent.length_in_blocks = stack->Parameters.Write.Length / BLOCK_SIZE;

    disk_tracker_remap(dev_ext->remapper, &extent, &remap_res);

    dump_remap("Write", &extent, remap_res);

    dev_ext->stats.write_hits += remap_res->num_remapped;
    
    if(dev_ext->simulate) {
        /* In simulation mode, just pass to the next driver */
        diskf_free(remap_res);
        IoCopyCurrentIrpStackLocationToNext(irp);
        return IoCallDriver(dev_ext->target_device_obj, irp);
    }

    status = split_irp_for_remap(dev_ext->target_device_obj, irp, remap_res);
    diskf_free(remap_res);
    return status;
}



#define MIN(a, b) ((a) > (b) ? (b) : (a))

NTSTATUS split_irp(PDEVICE_OBJECT dev_obj, PIRP irp)
{
    PIO_STACK_LOCATION stack = IoGetCurrentIrpStackLocation(irp);
    unsigned long i;
    NTSTATUS status;

    irp->IoStatus.Status = STATUS_SUCCESS;
    irp->IoStatus.Information = 0;
    IoMarkIrpPending(irp);

    /* DriverContext[0] will hold total number of transfer packets */
    irp->Tail.Overlay.DriverContext[0] = ULongToPtr (0);
    
    for (i = 0; i < stack->Parameters.Write.Length; i += BLOCK_SIZE)
    {
        struct transfer_packet* packet = NULL;
        ULONG transfer_len = MIN(BLOCK_SIZE, stack->Parameters.Write.Length - i);
        
        status = create_transfer_packet(dev_obj, irp, stack, stack->MajorFunction,
                                        i, transfer_len, 
                                        stack->Parameters.Write.ByteOffset.QuadPart + i,
                                        &packet);
        InterlockedIncrement((volatile LONG*)&(irp->Tail.Overlay.DriverContext[0]));    
        
        IoSetCompletionRoutine(packet->irp, difi_transfer_completion, packet, TRUE, TRUE, TRUE);
        status = IoCallDriver(dev_obj, packet->irp);
    }
    DbgPrint("Split IRP into %d packets\n", (ULONG)(irp->Tail.Overlay.DriverContext[0]));

    return status = STATUS_PENDING;
}

NTSTATUS split_irp_for_remap(PDEVICE_OBJECT dev_obj, PIRP irp, struct disk_extent_remap* remap)
{
    PIO_STACK_LOCATION stack = IoGetCurrentIrpStackLocation(irp);
    unsigned long i, offset;
    NTSTATUS status;

    irp->IoStatus.Status = STATUS_SUCCESS;
    irp->IoStatus.Information = 0;
    IoMarkIrpPending(irp);

    /* DriverContext[0] will hold total number of transfer packets */
    irp->Tail.Overlay.DriverContext[0] = ULongToPtr (0);
    
    for (i = 0, offset = 0; i < remap->number_of_extents; i++) {
        struct transfer_packet* packet = NULL;
        ULONG transfer_len = remap->remapped_extents[i].length_in_blocks * BLOCK_SIZE;

        status = create_transfer_packet(dev_obj, irp, stack, stack->MajorFunction,
                                        offset, transfer_len, 
                                        remap->remapped_extents[i].start_block * BLOCK_SIZE,
                                        &packet);
        InterlockedIncrement((volatile LONG*)&(irp->Tail.Overlay.DriverContext[0]));    
        offset += transfer_len;
                
        IoSetCompletionRoutine(packet->irp, difi_transfer_completion, packet, TRUE, TRUE, TRUE);
        status = IoCallDriver(dev_obj, packet->irp);
    }
    
    DbgPrint("Split IRP into %d packets\n", (ULONG)(irp->Tail.Overlay.DriverContext[0]));

    return status = STATUS_PENDING;
}



NTSTATUS
create_transfer_packet(PDEVICE_OBJECT     dev_obj,
                       PIRP               orig_irp,
                       PIO_STACK_LOCATION orig_stack,
                       UCHAR              operation, 
                       ULONG              offset,
                       ULONG              transfer_len,
                       ulong64_t          disk_loc, 
                       struct transfer_packet** result_out)
{
    struct transfer_packet* result = NULL;
    NTSTATUS                status = STATUS_SUCCESS;
    PIO_STACK_LOCATION      stack = NULL;
    PUCHAR                  orig_buffer = MmGetMdlVirtualAddress(orig_irp->MdlAddress);

    // It can be null, don't assert here
    // ASSERT(orig_buffer != NULL);
    
    *result_out = NULL;
    result = ExAllocatePoolWithTag(NonPagedPool, sizeof(*result), 'pnPC');
    if (result == NULL) {
        DbgPrint("Failed to allocate transfer packet.");
        return status = STATUS_INSUFFICIENT_RESOURCES;
    }

    RtlZeroMemory(result, sizeof(*result));
    result->orig_irp = orig_irp;

    /*
     *  Allocate an Irp for the packet.
     */
    result->irp = IoAllocateIrp(dev_obj->StackSize, FALSE);
    if (result->irp == NULL) {
        DbgPrint("Failed to allocate IRP for transfer packet.");
        ExFreePool(result);
        return status = STATUS_INSUFFICIENT_RESOURCES;
    }

    result->partial_mdl = IoAllocateMdl(orig_buffer,
                                        transfer_len,
                                        FALSE,
                                        FALSE,
                                        NULL);
    if (result->partial_mdl == NULL) {
        DbgPrint("Failed to allocate MDL for transfer packet.");
        IoFreeIrp(result->irp);
        ExFreePool(result);

        return status = STATUS_INSUFFICIENT_RESOURCES;
    }

    /*
       Build partial mdl: source mdl, new mdl, buffer and length
     */
    IoBuildPartialMdl (orig_irp->MdlAddress,
                       result->partial_mdl,
                       orig_buffer + offset,
                       transfer_len);
    result->irp->MdlAddress = result->partial_mdl;
    
    stack = IoGetNextIrpStackLocation (result->irp);

    /* Set operation and copy flags */
    stack->MajorFunction = operation;
    stack->Flags = orig_stack->Flags;
    
    stack->Parameters.Write.Length = transfer_len;
    stack->Parameters.Write.ByteOffset.QuadPart = disk_loc;

    *result_out = result;
    return status;
}


NTSTATUS difi_transfer_completion(IN PDEVICE_OBJECT dev_obj, IN PIRP irp, IN PVOID context)
{
    struct transfer_packet* pkt = (struct transfer_packet*)context;

    dev_obj;

    ASSERT (PtrToUlong (pkt->orig_irp->Tail.Overlay.DriverContext[0]) > 0);
    
    if (!NT_SUCCESS (irp->IoStatus.Status)) {
      InterlockedExchange ((PLONG)&pkt->orig_irp->IoStatus.Status,
                           irp->IoStatus.Status);
    }
    
    /* Update total number of bytes tranferred */
    InterlockedExchangeAdd ((PLONG)&pkt->orig_irp->IoStatus.Information,
                            irp->IoStatus.Information);
    MmPrepareMdlForReuse(pkt->partial_mdl);
    
    if (InterlockedDecrement((volatile LONG*)&pkt->orig_irp->Tail.Overlay.DriverContext[0]) == 0) {
        /* Done with all packets, assert and complete the original irp */
        PIO_STACK_LOCATION prev_stack = IoGetCurrentIrpStackLocation(pkt->orig_irp);
        ASSERT((ULONG)pkt->orig_irp->IoStatus.Information == 
               prev_stack->Parameters.Write.Length);

        /* DDK doc says to propagate pending state */
        if (pkt->irp->PendingReturned){
            IoMarkIrpPending(pkt->orig_irp);
        }

        //IoFreeMdl(pkt->partial_mdl); /*Again, not sure - keep it here? */
        IoCompleteRequest(pkt->orig_irp, IO_DISK_INCREMENT);
    }

    /* Not sure about this: free MDL or not? */
    //IoFreeMdl(pkt->partial_mdl);
    IoFreeIrp(irp);
    ExFreePool(pkt);
    
    return STATUS_MORE_PROCESSING_REQUIRED;
}

static void
dump_remap(const char* op, struct disk_extent* extent, 
            struct disk_extent_remap* remap_res)
{
    unsigned idx;
    DbgPrint("%s: Source extent: %llu len: %lu remaped to %d extents, hits %u",
             op, 
             extent->start_block,
             extent->length_in_blocks,
             remap_res->number_of_extents,
             remap_res->num_remapped);
    
    for(idx = 0; idx < remap_res->number_of_extents; idx++) {
        DbgPrint("  start: %llu length: %u", 
                 remap_res->remapped_extents[idx].start_block,
                 remap_res->remapped_extents[idx].length_in_blocks);
    }
}
