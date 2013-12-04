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

  ------------------------------------------------------------------------------
  
  Helper routines to get and dump various device information
  using obscure and poorly documented Setup API. A lot of information and source
  snippets is lifted from devcon utility

*/

#include <string>
#include "difi-lib.h"

enum DIFI_DEV_STATUS
{
    DEV_OK,
    DEV_FAIL,
    DEV_NEEDS_REBOOT,
    DEV_DISABLED,
    DEV_NOT_DISABLEABLE
};

enum DIFI_DEV_TYPE
{
    DEV_UNKNOWN,            // Unknown or we don't care
    DEV_DISPLAY,
    DEV_HDD,
    DEV_HDD_CONTROLLER,
    DEV_PORT,
    DEV_NETWORK,
    DEV_USB,
    DEV_SYSTEM
};

enum DIFI_DEV_SUBTYPE
{
    DEV_UNKNOWN_SUBTYPE,    // Unknown or we don't care
    DEV_COM_PORT,
    DEV_ETHERNET_CARD                                 
};

struct DifiDeviceInfo
{
    DWORD              devInst;
    DIFI_DEV_TYPE      devType;
    DIFI_DEV_SUBTYPE   devSubType;
    ULONG              devNodeStatus;
    std::wstring       deviceName;
    std::wstring       physObjectName;
    DIFI_DEV_STATUS    devStatus;           
};

DIFILIB_API DIFI_DEV_STATUS GetDeviceInfo( HDEVINFO devInfo,  PSP_DEVINFO_DATA devInfoData,  DifiDeviceInfo* difiDevInfo);
DIFI_DEV_TYPE GetDeefeeDeviceType( HDEVINFO devInfo,  PSP_DEVINFO_DATA devInfoData);

DIFILIB_API LPTSTR GetDeviceDescription( HDEVINFO devInfo,  PSP_DEVINFO_DATA devInfoData);
DIFILIB_API LPTSTR GetDeviceStringProperty( HDEVINFO devInfo,  PSP_DEVINFO_DATA devInfoData,  DWORD Prop);
DIFILIB_API LPTSTR GetBusReportedDeviceDesc( HDEVINFO hDevInfo,  PSP_DEVINFO_DATA devInfoData);
DIFILIB_API BOOL   GetDevicePowerData( HDEVINFO devInfo, PSP_DEVINFO_DATA devInfoData, __out CM_POWER_DATA* powerData);
DIFILIB_API void   DumpPowerData(CM_POWER_DATA* powerData);
DIFILIB_API DWORD  GetDeviceCaps(HDEVINFO devInfo,  PSP_DEVINFO_DATA devInfoData);
DIFILIB_API DWORD  GetDeviceType(HDEVINFO devInfo,  PSP_DEVINFO_DATA devInfoData);

DIFILIB_API void   DumpDeviceCaps(DWORD caps);
DIFILIB_API ULONG  GetDevNodeStatus(PSP_DEVINFO_DATA devInfoData);
DIFILIB_API void   DumpDevnodeStatus(ULONG status);
DIFILIB_API DIFI_DEV_STATUS DisableDevice( HDEVINFO devInfo,  PSP_DEVINFO_DATA devInfoData,  BOOL disable);

typedef void (*DeviceEnumerationCallback)( HDEVINFO hDevInfo, SP_DEVINFO_DATA* deviceInfoData);

DIFILIB_API DWORD EnumerateDevices(DeviceEnumerationCallback callback, LPTSTR rootEnumerator);

