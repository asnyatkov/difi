#pragma once
///////////////////////////////////////////////////////////////////////////////
//
//	(C) Copyright 2003 Hollis Technology Solutions
//	All Rights Reserved
//
//	Hollis Technology Solutions
//	94 Dow Road
//	Hollis, NH 03049
//	info@hollistech.com
//
//! \file filetranslation.h
//
///////////////////////////////////////////////////////////////////////////////

//
/// call initFileTranslation once before calling any other translation functions
///
/// filename is the file of interest.
///
/// verify requests that each call to getNextTranslation validate the translation.
///   If set, not all files can be opened.
///
/// initFileTranslation returns an opaque pointer value that is a token that
///  must be used on all calls to getNextTranslation and closeTranslation. 
///  The pointer remains valid until a call to closeTranslation is made.
///
///  If initFileTranslation returns NULL GetLastError can be used to determine
///  what went wrong.
//
PVOID initFileTranslation(
    WCHAR * filename, 
    BOOL verify);
//
/// resetTranslation must be called before starting
/// a translation interation using getNextTranslation.
/// Note that calls to resetTranslation and getLBAandLengthByOffset
/// will result in the next call to getNextTranslation starting from
/// the beginning of the file.
//
void resetTranslation(
    PVOID translationToken);

void getVolumeData(PVOID translationToken, NTFS_VOLUME_DATA_BUFFER& volumeData);

//
/// use this to iteratively fetch the disk extents for the file
/// returns a win32 error code.
/// There are three normal errors returned:
///
/// NO_ERROR - iterationm is complete and the returned data
///  is valid.
///
/// ERROR_HANDLE_EOF - iterationm is complete and the returned
///  data is NOT valid.
///
/// ERROR_MORE_DATA - iterationm is incomplete and 
///  the returned data is valid 
///
/// fileOffset returns the byte offset in the file of this translation
///
/// startSector returns the physical disk logical block address that corresponds
///  to fileOffset.
///
/// nSectors returns the number of contiguous sectors that constitute this disk extent.
//
DWORD getNextTranslation(
        PVOID translationToken,
        LONGLONG& fileOffset,
        LONGLONG& startSector,
        LONGLONG& nSectors);
//
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
//    
BOOL getLBAandLengthByOffset(
    PVOID translationToken,
    LONGLONG fileOffset,
    LONGLONG recordLength,
    LONGLONG& startSectorLBA,
    LONGLONG& runLength);
//
/// diagnostic interface to debug print to stdout
/// the raw virtual to logical cluster mapping
//
BOOL printAllClusters(
    PVOID translationToken);
//
/// end operations on this file.
//
void closeTranslation(
    PVOID translationToken);
