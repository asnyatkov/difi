///////////////////////////////////////////////////////////////////////////////
//
//  (C) Copyright 2003 Hollis Technology Solutions
//  All Rights Reserved
//
//  Hollis Technology Solutions
//  94 Dow Road
//  Hollis, NH 03049
//  info@hollistech.com
//
//! @file
//
///////////////////////////////////////////////////////////////////////////////
#include "stdafx.h"
#include "fileTranslation.h"

//
// internal data structures
//

typedef struct {
    VOLUME_DISK_EXTENTS volExtents;
    DISK_EXTENT extent;
} MIRRORED_DISK_EXTENT;

enum SupportedFileSystems {
    FAT32,
    NTFS
};

typedef struct {
    HANDLE fileHandle;
    WCHAR  fullVolumeName[MAX_PATH];
    HANDLE hVolume;
    MIRRORED_DISK_EXTENT volumeExtents;
    NTFS_VOLUME_DATA_BUFFER volumeData;
    LONGLONG volStartSector;
    STARTING_VCN_INPUT_BUFFER inputVcn;
    RETRIEVAL_POINTERS_BUFFER rpBuf;
    BOOLEAN verify;
    SupportedFileSystems fileSystemType;
    DWORD rootStart;
    DWORD clusterStart;
} TRANSLATION_RECORD;


typedef void (*IterateAction)(LONGLONG vcn, 
                              LONGLONG lcn, 
                              LONGLONG clusters);

//
// internal function headers
//

//
// this reads one cluster of data from both 
// the file, using the file offset, and the physical disk, using
// the startSector, and compares them.
//
// validateTranslation returns true if the first clusters of the 
// disk and the file are identical, otherwise it returns false.
//
BOOL validateTranslation(
    TRANSLATION_RECORD * translation, 
    LONGLONG& fileOffset, 
    LONGLONG& startSector,
    LONG size = -1);
//
// a test interface to iterate across all VCN->LCN mappings
//
BOOL iterateAllClusters(
        HANDLE fileHandle, 
        IterateAction callback);

//
// implementation
//

///
/// call initFileTranslation once before calling any other translation functions
/// returns a translation token which must be used for all calls to the other
/// apis.
///
PVOID initFileTranslation(WCHAR * filename,
                          BOOL verify)
{
    TRANSLATION_RECORD * record = NULL; 
    BOOL result = true;

    do {

        record = new TRANSLATION_RECORD;
        if (!record) {
            result = false;
            break;
        }

        record->verify = verify;
        record->fileHandle = INVALID_HANDLE_VALUE;
        
        DWORD Attributes = GetFileAttributes(filename);
        
        if  (Attributes == INVALID_FILE_ATTRIBUTES) {
            result = false;
            break;
        }
        
        if (Attributes & (FILE_ATTRIBUTE_COMPRESSED|FILE_ATTRIBUTE_ENCRYPTED)) {
            SetLastError(ERROR_INVALID_PARAMETER);
            result = false;
            break;
        }
        
        record->fileHandle = CreateFile(filename,
            verify ? GENERIC_READ : FILE_READ_ATTRIBUTES,
            FILE_SHARE_READ|FILE_SHARE_WRITE,
            0,
            OPEN_EXISTING,
            FILE_FLAG_NO_BUFFERING, // use FILE_FLAG_NO_BUFFERING
            0);
        
        if (record->fileHandle == INVALID_HANDLE_VALUE) {
            result = false;
            break;
        }
        //
        // get volume information
        //
        WCHAR volumeName[MAX_PATH];        
        result = GetVolumePathName(filename, volumeName, sizeof(volumeName));
        
        if (!result) {
            break;
        }
        printf("Volume name: %s\n", volumeName);

        //
        // use the crappy-ass win32 GetDiskFreeSpace to query the volume information
        //
        DWORD SectorsPerCluster;
        DWORD NumberOfFreeClusters;
        DWORD TotalNumberOfClusters;
        result = GetDiskFreeSpace(volumeName, 
                    &SectorsPerCluster,
                    &record->volumeData.BytesPerSector,
                    &NumberOfFreeClusters,
                    &TotalNumberOfClusters);

        if (!result) {
            break;
        }

        record->volumeData.BytesPerCluster = record->volumeData.BytesPerSector * SectorsPerCluster;
        record->volumeData.NumberSectors.QuadPart = TotalNumberOfClusters;
        record->volumeData.NumberSectors.QuadPart *= SectorsPerCluster;
        printf("Bytes per sector: %u sectors per cluster: %u num sectors: %llu\n",
               record->volumeData.BytesPerSector, SectorsPerCluster, 
               record->volumeData.NumberSectors.QuadPart);

        WCHAR fileSystemName[MAX_PATH];
        DWORD maxNameLength;
        DWORD fileSystemFlags;
        result =  GetVolumeInformation(volumeName,
                        NULL, 0,    // don't care about volume name
                        NULL,
                        &maxNameLength,
                        &fileSystemFlags,
                        fileSystemName,
                        sizeof(fileSystemName));
        if (!result) {
            break;
        }

        if (0 == _wcsicmp(fileSystemName, L"FAT32")) {
            record->fileSystemType = FAT32;
        } else if (0 == _wcsicmp(fileSystemName, L"NTFS")) {
            record->fileSystemType = NTFS;
        } else {
            result = FALSE;
            SetLastError(ERROR_NOT_SUPPORTED);
            break;
        }

        WCHAR * lastSlash = wcschr(volumeName, '\\');
        
        if (lastSlash) {
            *lastSlash = '\0';
        }
        
        _snwprintf(record->fullVolumeName, MAX_PATH, L"\\\\.\\%s", volumeName);
        
        record->hVolume = CreateFile(record->fullVolumeName,
            GENERIC_READ,
            FILE_SHARE_READ | FILE_SHARE_WRITE,
            NULL, OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL, NULL);
        
        if (record->hVolume == INVALID_HANDLE_VALUE) {
            printf("Unable to open volume, %x", GetLastError());
            result = false;
            break;
        }
        
        DWORD dwBytesReturned = 0;
        
        result = DeviceIoControl(record->hVolume,
            IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS,
            NULL, 0, 
            &record->volumeExtents,
            sizeof(record->volumeExtents),
            &dwBytesReturned,
            NULL);
        
        if (!result) {

            if (GetLastError() != ERROR_MORE_DATA) {
                printf("Unable to get volume extents, %x", GetLastError());
                result = false;
                break;
            }
        }

        if (record->fileSystemType == FAT32) {
            //
            // setup FAT32 offsets
            //
            result = FALSE;
            SetLastError(ERROR_NOT_SUPPORTED);
            // May be later
            //result = getFat32Offsets(record, record->hVolume);

        } else {

            record->clusterStart = 0;
            record->rootStart = 0;
        }

        // CloseHandle(hVolume);

        if (!result) {
            break;
        }

        LONGLONG volumeLength= record->volumeData.NumberSectors.QuadPart * record->volumeData.BytesPerSector;

        if (record->volumeExtents.volExtents.NumberOfDiskExtents > 1) {

            if ((record->volumeExtents.volExtents.NumberOfDiskExtents > 2) ||
                (record->volumeExtents.volExtents.Extents[0].StartingOffset.QuadPart !=
                 record->volumeExtents.volExtents.Extents[1].StartingOffset.QuadPart) ||
                (record->volumeExtents.volExtents.Extents[0].ExtentLength.QuadPart !=
                record->volumeExtents.volExtents.Extents[1].ExtentLength.QuadPart) ||
                (record->volumeExtents.volExtents.Extents[0].ExtentLength.QuadPart <
                 volumeLength) ||
                 (record->volumeExtents.volExtents.Extents[1].ExtentLength.QuadPart <
                 volumeLength)) {
                //
                // don't support striped volumes!
                // NB: don't support compressed volumes either!
                //
                SetLastError(ERROR_NOT_SUPPORTED);
                result = false;
                break;
            }
        }
        
        record->volStartSector = record->volumeExtents.volExtents.Extents[0].StartingOffset.QuadPart /
            record->volumeData.BytesPerSector;
        
        memset(&record->inputVcn, 0, sizeof(STARTING_VCN_INPUT_BUFFER));
        
        memset(&record->rpBuf, 0, sizeof(RETRIEVAL_POINTERS_BUFFER));
        
    } while(0);
    
    if (!result) {

        if (record) {

            if (record->fileHandle != INVALID_HANDLE_VALUE) {

                CloseHandle(record->fileHandle);
                record->fileHandle = INVALID_HANDLE_VALUE;
            }

            if (record->hVolume != INVALID_HANDLE_VALUE) {

                CloseHandle(record->hVolume);
                record->hVolume = INVALID_HANDLE_VALUE;

            }
            delete record;
            record = NULL;
        }

    } 
    return record;
}



///
/// reset the translation state to its original
/// intialized state.
///
void resetTranslation(
    PVOID translationToken)
{
    TRANSLATION_RECORD * translation = 
        reinterpret_cast<TRANSLATION_RECORD *>(translationToken);

    translation->inputVcn.StartingVcn.QuadPart = 0;
}

void getVolumeData(PVOID translationToken, NTFS_VOLUME_DATA_BUFFER& volumeData)
{
    TRANSLATION_RECORD * translation = 
        reinterpret_cast<TRANSLATION_RECORD *>(translationToken);
    volumeData = translation->volumeData;
}

///
/// use this to iteratively fetch the disk extents for the file
///
DWORD getNextTranslation(
        PVOID translationToken,
        LONGLONG& fileOffset,
        LONGLONG& startSector,
        LONGLONG& nSectors)
{
    printf("getNextTranslation\n");

    TRANSLATION_RECORD * translation = 
        reinterpret_cast<TRANSLATION_RECORD *>(translationToken);

    DWORD dwBytesReturned;

    BOOL result = DeviceIoControl(translation->fileHandle,
            FSCTL_GET_RETRIEVAL_POINTERS,
            &translation->inputVcn,
            sizeof(STARTING_VCN_INPUT_BUFFER),
            &translation->rpBuf,
            sizeof(RETRIEVAL_POINTERS_BUFFER),
            &dwBytesReturned,
            NULL);

    DWORD  error = GetLastError();

    switch (error) {
    default:
        break;
        
    case ERROR_HANDLE_EOF:
        //
        // UNDOCUMENTED: this indicates, when parsing the extents
        // one record at a time, that there are no more records.
        // Data returned is invalid.
        //
        break;
        
    case ERROR_MORE_DATA:
        translation->inputVcn.StartingVcn = translation->rpBuf.Extents[0].NextVcn;
        //
        // fall through
        //
    case NO_ERROR:
        //
        // this has to be scaled by the cluster factor and offset by
        // the volume extent starting offset, and everything normalized
        // to sectors.
        //
        LONGLONG lengthInClusters = 
            translation->rpBuf.Extents[0].NextVcn.QuadPart - translation->rpBuf.StartingVcn.QuadPart;

        VOLUME_LOGICAL_OFFSET logicalOffset;
        struct {
            VOLUME_PHYSICAL_OFFSETS physical;
            VOLUME_PHYSICAL_OFFSET  plex2;
        } outputBuffer;

        logicalOffset.LogicalOffset = translation->rpBuf.Extents[0].Lcn.QuadPart * translation->volumeData.BytesPerCluster;
        
        result = DeviceIoControl(translation->hVolume,
            IOCTL_VOLUME_LOGICAL_TO_PHYSICAL,
            &logicalOffset,
            sizeof(VOLUME_LOGICAL_OFFSET),
            &outputBuffer,
            sizeof(outputBuffer),
            &dwBytesReturned,
            NULL);

        if (!result) {
            error = GetLastError();
            break;
        }

        startSector = outputBuffer.physical.PhysicalOffset[0].Offset / translation->volumeData.BytesPerSector;

        startSector += translation->clusterStart;
        
        nSectors = (lengthInClusters * translation->volumeData.BytesPerCluster) /
            translation->volumeData.BytesPerSector;
        
        fileOffset = translation->rpBuf.StartingVcn.QuadPart * translation->volumeData.BytesPerCluster; 
        
        if (translation->verify) {

            printf("validateTranslation\n");
            // Validate first cluster and last sector
            BOOL result = validateTranslation(translation, fileOffset, startSector);
            if (!result) {
                error = ERROR_INVALID_DATA;
            }

            LONGLONG lastSecFileOffset = fileOffset + (nSectors - 1) * translation->volumeData.BytesPerSector; 
            LONGLONG lastSector = startSector + nSectors - 1;
            result = validateTranslation(translation, lastSecFileOffset, lastSector, 
                                         translation->volumeData.BytesPerSector);
            if (!result) {
                error = ERROR_INVALID_DATA;
            }        
        }
        break;
    }
    return error;
}
//
// this reads one cluster of data from both 
// the file, using the file offset, and the physical disk, using
// the startSector, and compares them.
//
BOOL validateTranslation(
    TRANSLATION_RECORD * translation, 
    LONGLONG& fileOffset, 
    LONGLONG& startSector,
    LONG size)
{
    printf("Verify at offset %llu sector %llu\n", fileOffset, startSector);
    
    WCHAR physicalDisk[MAX_PATH];
    _snwprintf(physicalDisk, MAX_PATH, L"\\\\.\\PhysicalDrive%d", 
        translation->volumeExtents.volExtents.Extents[0].DiskNumber);

    HANDLE hDisk = CreateFile(physicalDisk,
        GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        NULL, OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL, NULL);

    if (hDisk == INVALID_HANDLE_VALUE) {
        printf("Unable to open disk %d\n", GetLastError());
        return false;
    }

    if (size == -1) {
        size = translation->volumeData.BytesPerCluster;
    }
    
    LARGE_INTEGER offset;
    BOOL result = true;
    DWORD bytesRead;

    char * physicalBuffer = new char[size];
    char * fileBuffer = new char[size];

    if (physicalBuffer == NULL) {
        result = false;
    }

    if (fileBuffer == NULL) {
        result = false;
    }

    do {

        if (!result) {
            break;
        }

        offset.QuadPart = startSector * translation->volumeData.BytesPerSector;

        result = SetFilePointerEx(hDisk, offset, NULL, FILE_BEGIN);

        if (!result) {
            break;
        }

        result = ReadFile(hDisk, physicalBuffer, 
            size, 
            &bytesRead, NULL);

        if (!result) {
            break;
        }

        offset.QuadPart = fileOffset;

        result = SetFilePointerEx(translation->fileHandle, offset, NULL, FILE_BEGIN);

        if (!result) {
            break;
        }

        result = ReadFile(translation->fileHandle, fileBuffer, 
            size, 
            &bytesRead, NULL);

        if (!result) {
            break;
        }

        if (memcmp(physicalBuffer, fileBuffer, size) != 0) {
            result = false;
        }
        printf("Compares ok\n");
    } while(0);

    if (physicalBuffer) {

        delete[] physicalBuffer;
    }

    if (fileBuffer) {

        delete[] fileBuffer;
    }

    CloseHandle(hDisk);

    return result;
}
///
/// pass in a file offset (byte offset in file),
/// and the length in bytes of the data at that offset,
/// and get back the physical LBA (sector offset) and
/// byte length of this on disk run.
///
/// returns true if the operation succeeded, false otherwise.
/// if runLength < recordLength, this record is split across more
/// than one physical run. runLength should be added to fileOffset,
/// and subtracted from recordLength, and getLBAandLengthByOffset
/// should be called again to get the next LBA.
/// RecordLength mod BytesPerSector must be zero!
///    
BOOL getLBAandLengthByOffset(
    PVOID translationToken,
    LONGLONG fileOffset,
    LONGLONG recordLength,
    LONGLONG& startSectorLBA,
    LONGLONG& runLength)
{
    TRANSLATION_RECORD * translation = 
        reinterpret_cast<TRANSLATION_RECORD *>(translationToken);

    resetTranslation(translationToken);

    BOOL retval = true;
    BOOL foundRun = FALSE;
    LONGLONG currentRunOffset = 0;
    LONGLONG nextRunOffset = 0;
    LONGLONG startSector;
    LONGLONG nSectors;

    while (!foundRun) {

        DWORD error = getNextTranslation(translationToken,
                    currentRunOffset, startSector,
                    nSectors);

        switch (error) {
        case ERROR_HANDLE_EOF:
            //
            // done, no data.
            //
            retval = FALSE;
            break;

        case NO_ERROR:
        case ERROR_MORE_DATA:
            {
                //
                // compute the record LBA and length
                //
                LONGLONG newRunOffset = currentRunOffset + (nSectors * translation->volumeData.BytesPerSector);
                if (newRunOffset <= nextRunOffset) {
                    //
                    // fileOffset was bogus!
                    //
                    retval = FALSE;
                    SetLastError(ERROR_INVALID_PARAMETER);
                    break;
                }
                nextRunOffset = newRunOffset;

                if ((fileOffset >= currentRunOffset) &&
                    (fileOffset < nextRunOffset)) {
                        //
                        // we found the start of this run;
                        //
                        foundRun = true;
                        //
                        // offset in bytes of record start in this run
                        //
                        LONGLONG recordOffset = fileOffset - currentRunOffset;
                        LONGLONG sectorOffset = recordOffset/translation->volumeData.BytesPerSector;

                        startSectorLBA = startSector + sectorOffset;
                        nSectors -= sectorOffset;
                        runLength = nSectors * translation->volumeData.BytesPerSector;
                        if (runLength > recordLength) {
                            runLength = recordLength;
                        }

                        if (translation->verify) {
                            retval = validateTranslation(translation, fileOffset, startSectorLBA); 
                        }
                    }
            }
            break;

        default:
            //
            // done, failed.
            //
            retval = FALSE;
            break;
        }

        if (!retval) {
            break;
        }
        currentRunOffset = nextRunOffset;
    }                   

    return retval;
}


void printClusterMap(LONGLONG vcn, LONGLONG lcn, LONGLONG clusters)
{
    printf("VCN: %I64d LCN: %I64d Clusters: %I64d\n",
        vcn, lcn, clusters);
}
///
/// print all clusters for the current translation object.
///
BOOL printAllClusters(
    PVOID translationToken)
{
    TRANSLATION_RECORD * translation = 
        reinterpret_cast<TRANSLATION_RECORD *>(translationToken);

    return iterateAllClusters(translation->fileHandle, printClusterMap);
}
//
// iterate across the VCN -> LCN mapping,
// calling the supplied callback function for each iteration
//
BOOL iterateAllClusters(
        HANDLE fileHandle, 
        IterateAction callback)
{
    STARTING_VCN_INPUT_BUFFER inputVcn;
    RETRIEVAL_POINTERS_BUFFER rpBuf;

    inputVcn.StartingVcn.QuadPart = 0; // start at the beginning

    DWORD error = NO_ERROR;
    BOOL result = false;

    do {

        DWORD dwBytesReturned;

        result = DeviceIoControl(fileHandle,
            FSCTL_GET_RETRIEVAL_POINTERS,
            &inputVcn,
            sizeof(STARTING_VCN_INPUT_BUFFER),
            &rpBuf,
            sizeof(RETRIEVAL_POINTERS_BUFFER),
            &dwBytesReturned,
            NULL);

        error = GetLastError();

        switch (error) {
            case ERROR_HANDLE_EOF:
                //
                // done, no data.
                //
                result = true;
                break;
            
            case ERROR_MORE_DATA:
                //
                // more to do, valid data
                //
                inputVcn.StartingVcn = rpBuf.Extents[0].NextVcn;
                //
                // fall through
                //
            case NO_ERROR:
                //
                // done, valid data
                //
                callback(rpBuf.StartingVcn.QuadPart, rpBuf.Extents[0].Lcn.QuadPart,
                    rpBuf.Extents[0].NextVcn.QuadPart - rpBuf.StartingVcn.QuadPart);   
                result = true;
                break;

            default:
                //
                // user error?
                //
                break;
        }

    } while (error == ERROR_MORE_DATA);

    return result;
}

///
/// end operations on this file.
///
void closeTranslation(PVOID translationToken)
{
    TRANSLATION_RECORD * translation = 
        reinterpret_cast<TRANSLATION_RECORD *>(translationToken);

    CloseHandle(translation->fileHandle);

    CloseHandle(translation->hVolume);

    delete translation;
}
