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

  Disk filter support routines
*/

#include "difi-lib.h"

enum DIFI_ERRORS
{
    DIFI_IOCTL_FAILED = -10,
    DIFI_DISK_FILTER_NOT_FOUND = -2,
    DIFI_GENERIC_ERROR = -1,
    DIFI_OK,
    
    DIFI_STORAGE_NOT_ALLOCATED,   /* Warnings */
    DIFI_STORAGE_NOT_INITIALIZED,
    DIFI_STORAGE_NEEDS_RESET,
    DIFI_ALREADY_TRACKING
};

class DIFILIB_API DifiInterface
{
public:
    DifiInterface();
    ~DifiInterface();

    int PrintDiskTrackingStats(const TCHAR* diskName);
    int CheckStorageStatus();
    int AllocateStorage(unsigned size_in_gb);
    int InitStorage();
    int TrackDisk(const wchar_t* disk, bool simulate);

private:
    int InstallDriver();
    int StartDriver();
    HANDLE OpenControlDevice();

};
