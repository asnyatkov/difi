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

  Communicates with Difi driver
*/
#include "stdafx.h"

#include "DifiInterface.h"
#include "diskfilter/difi_interface.h"
#include "DriverSupport.h"
#include "DiskSupport.h"
#include "WinHandle.h"

template<class T>
class MallocAutoPtr
{
public:
    MallocAutoPtr(void* ptr) : 
        m_ptr(ptr)
    {
    }

    ~MallocAutoPtr()
    {
        if( m_ptr )
            free(m_ptr);
    }

    operator void* () { return m_ptr; }

private:
    void* m_ptr;
};

DifiInterface::DifiInterface()
{
}

DifiInterface::~DifiInterface()
{
}


HANDLE DifiInterface::OpenControlDevice()
{
    HANDLE difiHandle = CreateFile(_T("\\\\.\\DifiCtrlDrv0"), 
                                       GENERIC_READ | GENERIC_WRITE, 
                                       FILE_SHARE_READ | FILE_SHARE_WRITE, 
                                       NULL, 
                                       OPEN_EXISTING, 
                                       FILE_ATTRIBUTE_NORMAL, 
                                       NULL);
        
    if ( difiHandle == INVALID_HANDLE_VALUE ) {
        _tprintf(_T("Unable to open DifiCtrlDrv0! (%x)\n"), GetLastError());
    }
    return difiHandle;
}

int  DifiInterface::CheckStorageStatus()
{
    unsigned long long size;
    int storage_token = 0;

    if(::GetStorageSize(storage_token, &size) < 0 || size == 0) {
        printf("Unable to find Difi storage, please allocate it first\n");
        return DIFI_STORAGE_NOT_ALLOCATED;
    }

    WinHandle difiHandle(OpenControlDevice()); 
    if ( !difiHandle ) {
        return DIFI_DISK_FILTER_NOT_FOUND;
    }

    ioctl_difi_diskf_info info;
    memset(&info, 0, sizeof(info));

    unsigned long bytes_ret;
    if ( DeviceIoControl(difiHandle, IOCTL_DIFI_GET_INFO, 
        (LPVOID)&info, sizeof(info),
        (LPVOID)&info, sizeof(info),
        &bytes_ret, NULL) == 0 ) {
        _tprintf(_T("Unable to get disk filter info.  Error : %d\n"), GetLastError());
        return DIFI_IOCTL_FAILED;
    }

    if (info.total_blocks == 0 || info.free_blocks == 0) {
        return DIFI_STORAGE_NOT_INITIALIZED;
    }
    if (info.total_blocks != info.free_blocks) {
        return DIFI_STORAGE_NOT_INITIALIZED;
    }
    if (info.total_blocks != info.free_blocks) {
        return DIFI_STORAGE_NOT_INITIALIZED;
    }

    return DIFI_OK;
}

int DifiInterface::PrintDiskTrackingStats(const TCHAR* diskName)
{
    diskName;   // Ignore for now
                                                       
    {
        // Verify that disk filter device is installed
        WinHandle handle(CreateFile(_T("\\\\.\\DifiDiskFilter"), 0, 0, 
                            NULL, OPEN_EXISTING, 
                            FILE_ATTRIBUTE_NORMAL, NULL));
        if ( !handle ) {
            _tprintf(_T("Unable to open DifiDiskFilter! (%x)\n"), GetLastError());
        } else {
            wprintf(L"Found disk filter\n");
        }
    }

    WinHandle difiHandle(OpenControlDevice()); 
    if ( !difiHandle ) {
        return DIFI_DISK_FILTER_NOT_FOUND;
    }
    ioctl_difi_stats stats;
    memset(&stats, 0, sizeof(stats));

    unsigned long bytes_ret;
    if ( DeviceIoControl(difiHandle, IOCTL_DIFI_GET_STATS, 
        (LPVOID)&stats, sizeof(stats),
        (LPVOID)&stats, sizeof(stats),
        &bytes_ret, NULL) == 0 )
    {
        _tprintf(_T("Unable to get disk filter stats.  Error : %d\n"), GetLastError());
        return DIFI_IOCTL_FAILED;
    }

    wprintf(L"Difi disk filter stats\n"
             L"  hash size          : %u\n"
             L"  write hits         : %llu\n"
             L"  read hits          : %llu\n"
             L"  reads at PASSIVE   : %llu\n"
             L"  reads at APC       : %llu\n"
             L"  reads at DISPATCH  : %llu\n"
             L"  reads at other     : %llu\n"
             L"  writes at PASSIVE  : %llu\n"
             L"  writes at APC      : %llu\n"
             L"  writes at DISPATCH : %llu\n"
             L"  writes at other    : %llu\n",
             stats.hash_size, stats.write_hits, stats.read_hits,
             stats.per_irql_reads[0],
             stats.per_irql_reads[1],
             stats.per_irql_reads[2],
             stats.per_irql_reads[3],
             stats.per_irql_writes[0],
             stats.per_irql_writes[1],
             stats.per_irql_writes[2],
             stats.per_irql_writes[3]
             );
    return DIFI_OK;
}

int DifiInterface::AllocateStorage(unsigned size_in_gb)
{
    int storage_token = ::AllocateStorage(size_in_gb, 0);
    if (storage_token < 0) {
        return -1;
    }
    return storage_token;
}

int DifiInterface::InitStorage()
{
    unsigned long long size = 0;
    int storage_token = 0;

    if(::GetStorageSize(storage_token, &size) < 0 || size == 0) {
        printf("Unable to find Difi storage, please allocate it first\n");
        return -1;
    }
    
    ioctl_difi_storage_info* storage_info = NULL;
    if(::RetrieveStorageExtents(storage_token, &storage_info) < 0) {
        printf("Unable to retrieve storage extents");
        return -1;
    }

    WinHandle difiHandle(OpenControlDevice()); 
    if ( !difiHandle ) {
        return DIFI_DISK_FILTER_NOT_FOUND;
    }

    unsigned long bytes_ret;
    unsigned inp_buffer_size = sizeof(ioctl_difi_disk_initialize) +
                               sizeof(ioctl_difi_extent)*(storage_info->extent_count - 1);
    ioctl_difi_disk_initialize* disk_init = (ioctl_difi_disk_initialize*)malloc(inp_buffer_size);
    disk_init->size = inp_buffer_size;
    disk_init->low_storage_space_percentage = 20;    // Be on the safe side
    disk_init->need_more_storage_event = NULL;
    memcpy(&disk_init->initial_storage, storage_info,
                sizeof(ioctl_difi_storage_info) +
                sizeof(ioctl_difi_extent)*(storage_info->extent_count - 1));
    
    wprintf(L"ioctl_difi_disk_initialize:\n"
            L"  size       : %u\n"
            L"  file name  : %s\n"
            L"  num_extents: %u\n"
            L"  total size : %llu\n",
            inp_buffer_size, 
            disk_init->initial_storage.file_name, 
            disk_init->initial_storage.extent_count, 
            disk_init->initial_storage.total_size
    );
    for(unsigned i = 0; i < disk_init->initial_storage.extent_count; i++) {
        wprintf(L"  extent #%u start_lba: %llu size %u\n", 
                i,
                disk_init->initial_storage.extents[i].start_lba,
                disk_init->initial_storage.extents[i].length_in_sectors);
    }

    if ( DeviceIoControl(difiHandle, IOCTL_DIFI_INITIALIZE, 
                         (LPVOID)disk_init, inp_buffer_size,
                         NULL, 0, 
                         &bytes_ret, NULL) == 0 )
    {
        _tprintf(_T("Unable to initialize storage.  Error : %d\n"), GetLastError());
        free(disk_init);
        free(storage_info);
        return DIFI_IOCTL_FAILED;
    }

    if ( DeviceIoControl(difiHandle, IOCTL_DIFI_TEST_STORAGE, 
                         NULL, 0,
                         NULL, 0, 
                         &bytes_ret, NULL) == 0 )
    {
        _tprintf(_T("Unable to test storage.  Error : %d\n"), GetLastError());
        free(disk_init);
        free(storage_info);
        return DIFI_IOCTL_FAILED;
    }

    char sector[512];
    memset(sector, 0, sizeof(sector));
    memcpy(sector, "*    Test sector", sizeof("*    Test sector"));
    sector[511] = '*';
    if (::VerifyStorage(storage_token, storage_info, sector, sector) < 0) {
        printf("Storage verification failed!");
    } else {
        printf("All storage verification passed\n");
    }

    free(disk_init);
    free(storage_info);
    return 0;
}

int DifiInterface::TrackDisk(const wchar_t* disk, bool simulate)
{
    WinHandle difiHandle(OpenControlDevice()); 
    if ( difiHandle == INVALID_HANDLE_VALUE ) {
        return DIFI_DISK_FILTER_NOT_FOUND;
    }

    unsigned long bytes_ret;
    ioctl_difi_track_disk track;
    track.simulate = simulate;

    if ( DeviceIoControl(difiHandle, IOCTL_DIFI_TRACK_DISK, 
                         (LPVOID)&track, sizeof(track),
                         NULL, 0, 
                         &bytes_ret, NULL) == 0 )
    {
        _tprintf(_T("Unable to track storage.  Error : %d\n"), GetLastError());
        return DIFI_IOCTL_FAILED;
    }

    return DIFI_OK;
}

