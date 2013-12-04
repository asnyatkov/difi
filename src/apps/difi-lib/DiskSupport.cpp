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
#include "stdafx.h"
#include <vector>

#include "fileTranslation.h"
#include "diskfilter/difi_interface.h"
#include "DiskSupport.h"

#define ONE_GB (1024ULL*1024ULL*1024ULL)
#define ONE_MB (1024ULL*1024ULL)

wchar_t difiStorageDir[MAX_PATH] = 
    //L"c:\\temp\\"; 
    L"c:\\Difi\\"; 

static char buf[4096] = {0};

static WCHAR* GetStorageFilename(int token, WCHAR* fileName, size_t bufLen)
{
    _snwprintf(fileName, bufLen, L"%s%s%03d%s",  difiStorageDir, L"difi", token, L".sys");
    return fileName;
}

static int MakeDifiDataDir()
{
    _wmkdir(difiStorageDir);
    SetFileAttributes(difiStorageDir, FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM);
    return 0;
}

static void FillStorage(HANDLE h, LONGLONG totalSize, alloc_progress_cb cb)
{
    unsigned mb;
    for(long long i = ONE_MB, mb = 0; i < totalSize; i += ONE_MB, mb++) {
        LARGE_INTEGER sz;
        sz.QuadPart = LONGLONG(i) - 512;
        SetFilePointerEx(h, sz, NULL, FILE_BEGIN);
        
        memcpy(buf, "* 1Mb fill", sizeof("* 1Mb fill"));
        buf[511] = '*';
        DWORD b;
        WriteFile(h, buf, 512, &b, NULL);

        cb(mb);
    }
}

int AllocateStorage(unsigned sizeInGigabytes, alloc_progress_cb cb)
{
    MakeDifiDataDir();
    
    WCHAR fileName[MAX_PATH];

    HANDLE h = CreateFile(GetStorageFilename(0, fileName, MAX_PATH), 
                          GENERIC_WRITE, 0, NULL, OPEN_ALWAYS, //CREATE_NEW, 
                          FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM | FILE_ATTRIBUTE_ARCHIVE, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        printf("Failed to create storage file: %x\n", GetLastError());
        return -1;
    }

#if 1
    // Add visual verification to the file    
    memcpy(buf, "* Fist sector", sizeof("* Fist sector"));
    buf[511] = '*';
    memcpy(buf+512, "* Second sector", sizeof("* Second sector"));
    buf[1023] = '*';
    DWORD b;
    WriteFile(h, buf, 1024, &b, NULL);
#endif

    /* Here's bad part: if I only call SetEndOfFile, without writing 
     * any data at the very end of the file, driver data verification always fails.
     * So the space is allocated, extents are right, but driver's writes go somewhere
     * else. Weird. The only way to fix it is to actually write a blocks, but it
     * takes very long time to finish.
     */
    LARGE_INTEGER sz;
    sz.QuadPart = LONGLONG(sizeInGigabytes) * ONE_GB;
    
    SetFilePointerEx(h, sz, NULL, FILE_BEGIN);
    // Reserve space
    SetEndOfFile(h);
    
    FillStorage(h, sz.QuadPart, cb);

#if 1
    sz.QuadPart = LONGLONG(sizeInGigabytes) * ONE_GB - 1024;
    SetFilePointerEx(h, sz, NULL, FILE_BEGIN);
    memcpy(buf, "* Before last sector", sizeof("* Before last sector"));
    buf[511] = '*';
    memcpy(buf+512, "* Last sector", sizeof("* Last sector"));
    buf[1023] = '*';
    WriteFile(h, buf, 1024, &b, NULL);
#endif

    CloseHandle(h);
    printf("Done allocating storage\n");

    return 0;
}

int GetStorageSize(int storageIndex, unsigned long long* size)
{
    WCHAR fileName[MAX_PATH];

    HANDLE h = CreateFile(GetStorageFilename(storageIndex, fileName, MAX_PATH), 
                          GENERIC_WRITE, 0, NULL, OPEN_ALWAYS, //CREATE_NEW, 
                          FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM | FILE_ATTRIBUTE_ARCHIVE, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        *size = 0;
        return -1;
    }
    LARGE_INTEGER fileSize;
    if (!GetFileSizeEx(h, &fileSize)) {
        printf("GetFileSizeEx failed, %x\n", GetLastError());
        CloseHandle(h);
        return -1;
    }
    *size = fileSize.QuadPart;
    CloseHandle(h);
    return 0;
}

int RetrieveStorageExtents(int storageIndex, ioctl_difi_storage_info** storageInfo)
{
    WCHAR fileName[MAX_PATH];
    *storageInfo = NULL;

    if (storageIndex < 0) {
        return storageIndex;
    }
    
    PVOID token = initFileTranslation(GetStorageFilename(storageIndex, fileName, MAX_PATH), TRUE);
    if (token == NULL) {
        return -1;
    }
    resetTranslation(token);

    DWORD status = ERROR_MORE_DATA;
    LONGLONG fileOffset = 0;
    LONGLONG totalSectors = 0;
    std::vector<ioctl_difi_extent> extents;
    NTFS_VOLUME_DATA_BUFFER volumeData;

    getVolumeData(token, volumeData);
    
    do {
        LONGLONG startSector;
        LONGLONG nSectors;

        status = getNextTranslation(token, fileOffset, startSector, nSectors);
        
        switch (status) {
        case ERROR_HANDLE_EOF:
            break;

        case NO_ERROR:
        case ERROR_MORE_DATA:
        {
            printf("File offset %llx LBA: %lld Sectors: %lld\n",
                   fileOffset, startSector, nSectors);
            ioctl_difi_extent extent;
            extent.start_lba         = (ULONGLONG)startSector;
            extent.length_in_sectors = (ULONG)nSectors;
            extents.push_back(extent);
            totalSectors += nSectors;
            break;
        }

        default:
            //PrintError(error, "getNextTranslation");
            break;
        }

    } while (status == ERROR_MORE_DATA);

    closeTranslation(token);
    
    
    *storageInfo = (ioctl_difi_storage_info*)malloc(sizeof(ioctl_difi_storage_info) + 
                                                       sizeof(ioctl_difi_extent)*extents.size());
    memset(*storageInfo, 0, sizeof(**storageInfo));
    (*storageInfo)->size = sizeof(ioctl_difi_storage_info) + 
                            sizeof(ioctl_difi_extent)*extents.size();
    (*storageInfo)->cluster_size = volumeData.BytesPerCluster;
    (*storageInfo)->sector_size  = volumeData.BytesPerSector;
    (*storageInfo)->extent_count = extents.size();
    (*storageInfo)->total_size = totalSectors * 512;  // FIXME: implement get_sec_size!
    wcscpy_s((*storageInfo)->file_name, MAX_PATH, fileName);
    for (size_t i = 0; i < extents.size(); i++) {
        (*storageInfo)->extents[i] = extents[i];
    }

    return 0;
}

int VerifyStorage(int storageIndex, ioctl_difi_storage_info* storageInfo, 
                   const char* firstSector, const char* lastSector)
{
    WCHAR fileName[MAX_PATH];
    int result = 0;

    HANDLE h = CreateFile(GetStorageFilename(storageIndex, fileName, MAX_PATH), 
                          GENERIC_WRITE | GENERIC_READ, 0, NULL, OPEN_ALWAYS, //CREATE_NEW, 
                          FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM | FILE_ATTRIBUTE_ARCHIVE |
                          FILE_FLAG_NO_BUFFERING | FILE_FLAG_WRITE_THROUGH, 
                          NULL);
    if (h == INVALID_HANDLE_VALUE) {
        return -1;
    }
    
    DWORD b;
    if (!ReadFile(h, buf, 512, &b, NULL)) {
        printf("Reading first sector failed!\n");
        result = -1;
    } else if (memcmp(buf, firstSector, 512) != 0) {
        printf("First verification sector failed!\n");
        result = -1;
    }
    
    LARGE_INTEGER sz;
    sz.QuadPart = storageInfo->total_size - 512;
    
    SetFilePointerEx(h, sz, NULL, FILE_BEGIN);
    if (!ReadFile(h, buf, 512, &b, NULL)) {
        printf("Reading last sector failed!\n");
        result = -1;
    } else if (memcmp(buf, lastSector, 512) != 0) {
        printf("Last verification sector failed!\n");
        result = -1;
    }    
    CloseHandle(h);

    return result;
}

// I have to copy these here because it's defined in DDK header which is not 
// compatible with user mode
#define VOLSNAPCONTROLTYPE                              0x00000053 // 'S'
#define IOCTL_VOLSNAP_FLUSH_AND_HOLD_WRITES             CTL_CODE(VOLSNAPCONTROLTYPE, 0, METHOD_BUFFERED, FILE_READ_ACCESS | FILE_WRITE_ACCESS) 

int FlushStorage(int storageIndex)
{
    WCHAR fileName[MAX_PATH];
    WCHAR volumeName[MAX_PATH];        
    WCHAR fullVolumeName[MAX_PATH];        

    GetStorageFilename(storageIndex, fileName, MAX_PATH);

    BOOL result = GetVolumePathName(fileName, volumeName, sizeof(volumeName));
        
    if (!result) {
        printf("Unable to get volume pathname\n");
        return -1;
    }
    WCHAR * lastSlash = wcschr(volumeName, '\\');
    if (lastSlash) {
        *lastSlash = '\0';
    }
    _snwprintf_s(fullVolumeName, MAX_PATH, L"\\\\.\\%s", L"FileSystem\\Ntfs");//volumeName);
    wprintf(L"Volume name: %s full name: %s\n", volumeName, fullVolumeName);
        
    HANDLE volume = CreateFile(fullVolumeName,
        GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        NULL, OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL, NULL);
        
    if (volume == INVALID_HANDLE_VALUE) {
        printf("Unable to open volume: %x\n", GetLastError());
        return -1;
    }

    DWORD dwBytesReturned = 0;
        
    result = DeviceIoControl(volume,
        IOCTL_VOLSNAP_FLUSH_AND_HOLD_WRITES,
        NULL, 0, 
        NULL, 0,
        &dwBytesReturned,
        NULL);

    if (!result) {
        printf("Unable to send IOCTL_VOLSNAP_FLUSH_AND_HOLD_WRITES, %x\n", GetLastError());
        return -1;
    } else {
        printf("Successfully flushed volume!");
    }

    CloseHandle(volume);
    return 0;
}

