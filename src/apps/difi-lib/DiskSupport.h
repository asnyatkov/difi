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

struct ioctl_difi_storage_info;

/* Progress callback for allocate_storage. Return anything but 0 to cancel */
typedef int (*alloc_progress_cb)(unsigned megabytes);

DIFILIB_API int AllocateStorage(unsigned sizeInGigabytes, alloc_progress_cb cb);
DIFILIB_API int GetStorageSize(int storageIndex, unsigned long long* size);
DIFILIB_API int GetStorageIndexes(unsigned* count, int** indexes);
DIFILIB_API int RetrieveStorageExtents(int storageIndex, ioctl_difi_storage_info** storage_info);
DIFILIB_API int VerifyStorage(int storageIndex, ioctl_difi_storage_info* storage_info, 
                                 const char* firstSector, const char* lastSector);
DIFILIB_API int FlushStorage(int storageIndex);

