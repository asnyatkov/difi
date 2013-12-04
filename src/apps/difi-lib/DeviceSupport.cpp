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
#include "stdafx.h"
#include "DeviceSupport.h"

DWORD EnumerateDevices(DeviceEnumerationCallback callback, LPTSTR rootEnumerator)
{
    SP_DEVINFO_DATA deviceInfoData;
    HDEVINFO        devInfo;

    // Create a HDEVINFO with all present devices.
    devInfo = SetupDiGetClassDevs(NULL,
        REGSTR_KEY_PCIENUM,     // Enumerator: we're looking only for PCI bus
        0,
        DIGCF_PRESENT | DIGCF_ALLCLASSES );

    if (devInfo == INVALID_HANDLE_VALUE) {
        // Insert error handling here.
        return 1;
    }

    deviceInfoData.cbSize = sizeof(SP_DEVINFO_DATA);
    for (DWORD i=0; SetupDiEnumDeviceInfo(devInfo, i, &deviceInfoData); i++) {
        callback(devInfo, &deviceInfoData);
    }

    if ( GetLastError() != NO_ERROR && GetLastError() != ERROR_NO_MORE_ITEMS ) {
        // Insert error handling here.
        return 1;
    }

    //  Cleanup
    SetupDiDestroyDeviceInfoList(devInfo);
    return 0;
}


DIFI_DEV_STATUS DisableDevice( HDEVINFO devInfo,  PSP_DEVINFO_DATA devInfoData,  BOOL disable)
{
    SP_PROPCHANGE_PARAMS propChangeParams;
    SP_DEVINSTALL_PARAMS devParams;

    propChangeParams.ClassInstallHeader.cbSize = sizeof(SP_CLASSINSTALL_HEADER);
    propChangeParams.ClassInstallHeader.InstallFunction = DIF_PROPERTYCHANGE;
    propChangeParams.StateChange = disable ? DICS_DISABLE : DICS_ENABLE;
    propChangeParams.Scope = DICS_FLAG_CONFIGSPECIFIC;
    propChangeParams.HwProfile = 0;

    if(!disable) {
        //
        // Enable-only: do both on global and config-specific profile
        // do global first and see if that succeeded in enabling the device
        // (global enable doesn't mark reboot required if device is still
        // disabled on current config whereas vice-versa isn't true)
        //
        propChangeParams.Scope = DICS_FLAG_GLOBAL;

        // Don't worry if this fails, we'll get an error when we try config-specific.
        if(SetupDiSetClassInstallParams(devInfo, devInfoData, &propChangeParams.ClassInstallHeader, sizeof(propChangeParams))) {
            SetupDiCallClassInstaller(DIF_PROPERTYCHANGE, devInfo, devInfoData);
        }

        // Re-initialize to try and enable on config-specific
        propChangeParams.ClassInstallHeader.cbSize = sizeof(SP_CLASSINSTALL_HEADER);
        propChangeParams.ClassInstallHeader.InstallFunction = DIF_PROPERTYCHANGE;
        propChangeParams.StateChange = DICS_ENABLE;
        propChangeParams.Scope = DICS_FLAG_CONFIGSPECIFIC;
        propChangeParams.HwProfile = 0;
    }

    // Operate on config-specific profile
    
    if(!SetupDiSetClassInstallParams(devInfo, devInfoData, &propChangeParams.ClassInstallHeader, sizeof(propChangeParams)) ||
       !SetupDiCallClassInstaller(DIF_PROPERTYCHANGE, devInfo, devInfoData)) {
        DWORD lastError = GetLastError();

        _tprintf(TEXT("ERROR: %x\n"), lastError);
        return DEV_FAIL;
    }

    // if device needs reboot
    devParams.cbSize = sizeof(devParams);
    if(SetupDiGetDeviceInstallParams(devInfo, devInfoData, &devParams) && (devParams.Flags & (DI_NEEDRESTART | DI_NEEDREBOOT))) {
        return DEV_NEEDS_REBOOT;
    }

    return DEV_DISABLED;
}

LPTSTR GetDeviceStringProperty( HDEVINFO devInfo,  PSP_DEVINFO_DATA devInfoData,  DWORD Prop)
{
    LPTSTR buffer;
    DWORD size;
    DWORD reqSize;
    DWORD dataType;
    DWORD szChars;

    size = 1024; // initial guess
    buffer = new TCHAR[(size/sizeof(TCHAR))+1];
    if(!buffer) {
        return NULL;
    }
    while(!SetupDiGetDeviceRegistryProperty(devInfo, devInfoData, Prop, &dataType, 
                                            (LPBYTE)buffer,size,&reqSize)) {
        if(GetLastError() != ERROR_INSUFFICIENT_BUFFER) {
            goto failed;
        }
        if(dataType != REG_SZ) {
            goto failed;
        }
        size = reqSize;
        delete [] buffer;
        buffer = new TCHAR[(size/sizeof(TCHAR))+1];
        if(!buffer) {
            goto failed;
        }
    }
    szChars = reqSize/sizeof(TCHAR);
    buffer[szChars] = TEXT('\0');
    return buffer;

failed:
    if(buffer) {
        delete [] buffer;
    }
    return NULL;
}

/*
  TODO: SetupDiGetDeviceProperty seems to be newer and better API, switch?
  http://msdn.microsoft.com/en-us/library/ff551963(v=vs.85).aspx
*/
LPTSTR GetBusReportedDeviceDesc( HDEVINFO devInfo,  PSP_DEVINFO_DATA devInfoData)
{
    DEVPROPTYPE ulPropertyType;
    TCHAR szDesc[1024];
    DWORD size = sizeof(szDesc);

    if (!SetupDiGetDeviceProperty (devInfo, devInfoData, 
                              &DEVPKEY_Device_BusReportedDeviceDesc,
                              &ulPropertyType, (BYTE*)szDesc, sizeof(szDesc), &size, 0)) {
        return NULL;
    }
    LPTSTR buffer = new TCHAR[(size/sizeof(TCHAR))+1];
    memcpy(buffer, szDesc, size);
    DWORD szChars = size/sizeof(TCHAR);
    buffer[szChars] = TEXT('\0');
    return buffer;
}

LPTSTR GetDeviceDescription( HDEVINFO devInfo,  PSP_DEVINFO_DATA devInfoData)
{
    LPTSTR desc;
    desc = GetDeviceStringProperty(devInfo,devInfoData,SPDRP_FRIENDLYNAME);
    if(!desc) {
        desc = GetDeviceStringProperty(devInfo,devInfoData,SPDRP_DEVICEDESC);
    }
    return desc;
}


BOOL GetDevicePowerData( HDEVINFO devInfo,  PSP_DEVINFO_DATA devInfoData, __out CM_POWER_DATA* powerData)
{
    DWORD reqSize;
    DWORD dataType;
    DWORD size = sizeof(CM_POWER_DATA);

    return SetupDiGetDeviceRegistryProperty(
            devInfo, devInfoData, SPDRP_DEVICE_POWER_DATA, 
            &dataType, (PBYTE)powerData, size, &reqSize);
}

struct DevClassifier
{
    wchar_t* guid;
    DIFI_DEV_TYPE devType;
};

static DevClassifier knownDevs[] = 
{
    { L"{4d36e968-e325-11ce-bfc1-08002be10318}",   DEV_DISPLAY }, // Display
    { L"{4d36e96a-e325-11ce-bfc1-08002be10318}",   DEV_HDD_CONTROLLER }, // Hard disk controller
    { L"{4d36e967-e325-11ce-bfc1-08002be10318}",   DEV_HDD }, // Hard drive
    { L"{4d36e978-e325-11ce-bfc1-08002be10318}",   DEV_PORT }, // Ports (serial, parallel)
    { L"{4d36e972-e325-11ce-bfc1-08002be10318}",   DEV_NETWORK }, // Network adapters
    { L"{36fc9e60-c465-11cf-8056-444553540000}",   DEV_USB }, // USB
    { L"{4d36e97d-e325-11ce-bfc1-08002be10318}",   DEV_SYSTEM}
    //{ L"*", DEV_UNKNOWN}
};


DIFI_DEV_TYPE GetDifiDeviceType(HDEVINFO devInfo, PSP_DEVINFO_DATA deviceInfoData)
{
    LPTSTR devClass = GetDeviceStringProperty(devInfo, deviceInfoData, SPDRP_CLASSGUID);
    if (devClass == NULL)
        return DEV_UNKNOWN;
    std::wstring devClassStr(devClass);
    delete [] devClass;

    for(int i = 0; i < sizeof(knownDevs)/sizeof(knownDevs[0]); ++i)
    {
        if (devClassStr == std::wstring(knownDevs[i].guid))
            return knownDevs[i].devType;
    }
    return DEV_UNKNOWN;
}


DWORD GetDeviceCaps( HDEVINFO devInfo,  PSP_DEVINFO_DATA devInfoData)
{
    DWORD reqSize;
    DWORD dataType;
    DWORD result = 0;
    DWORD size = sizeof(DWORD);

    return  (
        devInfo, devInfoData, SPDRP_CAPABILITIES, 
            &dataType, (PBYTE)&result, size, &reqSize) ? result : 0;
}

// FIXME: Doesn't work, always returns error 0xd
DWORD GetDeviceType( HDEVINFO devInfo,  PSP_DEVINFO_DATA devInfoData)
{
    DWORD reqSize;
    DWORD dataType;
    DWORD result = 0;
    DWORD size = sizeof(DWORD);

    BOOL res = SetupDiGetDeviceRegistryProperty(
        devInfo, devInfoData, SPDRP_DEVTYPE, 
        &dataType, (PBYTE)&result, size, &reqSize);
    if(res) {
        return result;
    }
    wprintf(L" (Error %x) ", GetLastError());
    return (DWORD)-1;
}


ULONG GetDevNodeStatus(PSP_DEVINFO_DATA devInfoData)
{
    ULONG status;
    ULONG problem;
    CONFIGRET ret;

    ret = CM_Get_DevNode_Status(&status, &problem, (DEVINST)devInfoData->DevInst, 0);

    if (ret != CR_SUCCESS) {
        wprintf(L"CM_Get_DevNode_Status failed\n");
        return 0;
    }
    return status;
}

DIFI_DEV_STATUS GetDeviceInfo( HDEVINFO devInfo,  PSP_DEVINFO_DATA deviceInfoData, DifiDeviceInfo* difiDevInfo)
{
    difiDevInfo->devInst = deviceInfoData->DevInst;
    difiDevInfo->devStatus = DEV_FAIL;
    
    LPTSTR descr = GetDeviceDescription(devInfo, deviceInfoData);
    difiDevInfo->deviceName = descr;    
    delete [] descr;

    LPTSTR phys = GetDeviceStringProperty(devInfo, deviceInfoData, 
                      SPDRP_PHYSICAL_DEVICE_OBJECT_NAME);
    difiDevInfo->physObjectName = phys;
    delete [] phys;

    ULONG devStatus = GetDevNodeStatus(deviceInfoData);
    difiDevInfo->devNodeStatus = devStatus;
    difiDevInfo->devType = GetDifiDeviceType(devInfo, deviceInfoData);
    if (!(devStatus & (DN_DRIVER_LOADED | DN_STARTED | DN_DISABLEABLE))) {
        return difiDevInfo->devStatus = DEV_NOT_DISABLEABLE;
    }
    if (devStatus & (DN_HAS_PROBLEM | DN_PRIVATE_PROBLEM)) {
        return difiDevInfo->devStatus = DEV_NOT_DISABLEABLE;
    }
    return difiDevInfo->devStatus = DEV_OK;
}

void DumpPowerData(CM_POWER_DATA* powerData)
{
    TCHAR buffer[1024];

    _tcscpy(buffer, L"   Power state: ");
    switch(powerData->PD_MostRecentPowerState) {
    case PowerDeviceUnspecified:
        _tcscat_s(buffer, L"unspecified");
        break;
    case PowerDeviceD0:
        _tcscat_s(buffer, 1024, L"D0");
        break;
    case PowerDeviceD1:
        _tcscat_s(buffer, 1024, L"D1");
        break;
    case PowerDeviceD2:
        _tcscat_s(buffer, 1024, L"D2");
        break;
    case PowerDeviceD3:
        _tcscat_s(buffer, 1024, L"D3");
        break;
    }
    _tcscat_s(buffer, 1024, L" Supports: ");
    if (powerData->PD_Capabilities & PDCAP_D0_SUPPORTED)
        _tcscat_s(buffer, 1024, L"D0 ");
    if (powerData->PD_Capabilities & PDCAP_D1_SUPPORTED)
        _tcscat_s(buffer, 1024, L"D1 ");
    if (powerData->PD_Capabilities & PDCAP_D2_SUPPORTED)
        _tcscat_s(buffer, 1024, L"D2 ");
    if (powerData->PD_Capabilities & PDCAP_D3_SUPPORTED)
        _tcscat_s(buffer, 1024, L"D3 ");
    _tcscat(buffer, L"\n");

    _tprintf(buffer);
}

void DumpDeviceCaps(DWORD caps)
{
    TCHAR buffer[1024];

    _tcscpy(buffer, L" Caps: ");
    if (caps & CM_DEVCAP_LOCKSUPPORTED)
        _tcscat_s(buffer, 1024, L"Lockable ");
    if (caps & CM_DEVCAP_REMOVABLE)
        _tcscat_s(buffer, 1024, L"Removable ");
    if (caps & CM_DEVCAP_EJECTSUPPORTED)    
        _tcscat_s(buffer, 1024, L"Ejectable ");
    if (caps & CM_DEVCAP_DOCKDEVICE)        
        _tcscat_s(buffer, 1024, L"Dockable ");
    if (caps & CM_DEVCAP_UNIQUEID)          
        _tcscat_s(buffer, 1024, L"UniqueId ");
    if (caps & CM_DEVCAP_SILENTINSTALL)     
        _tcscat_s(buffer, 1024, L"SilentInstall ");
    if (caps & CM_DEVCAP_RAWDEVICEOK)       
        _tcscat_s(buffer, 1024, L"RawOk ");
    if (caps & CM_DEVCAP_SURPRISEREMOVALOK) 
        _tcscat_s(buffer, 1024, L"Suprise ");
    if (caps & CM_DEVCAP_HARDWAREDISABLED)  
        _tcscat_s(buffer, 1024, L"HwDisabled ");
    if (caps & CM_DEVCAP_NONDYNAMIC)        
        _tcscat_s(buffer, 1024, L"NonDynamic ");

    _tcscat_s(buffer, 1024, L"\n");
    _tprintf(buffer);
}

/*
Status guide:

#define DN_ENUM_LOADED     (0x00000004) // Has Register_Enumerator
#define DN_NEED_TO_ENUM    (0x00000020) // May need reenumeration
#define DN_NOT_FIRST_TIME  (0x00000040) // Has received a config
#define DN_HARDWARE_ENUM   (0x00000080) // Enum generates hardware ID
#define DN_LIAR            (0x00000100) // Lied about can reconfig once
#define DN_HAS_MARK        (0x00000200) // Not CM_Create_DevInst lately
#define DN_PRIVATE_PROBLEM (0x00008000) // Has a private problem
#define DN_MF_PARENT       (0x00010000) // Multi function parent
#define DN_MF_CHILD        (0x00020000) // Multi function child
#define DN_WILL_BE_REMOVED (0x00040000) // DevInst is being removed
#define DN_NOT_FIRST_TIMEE  0x00080000  // S: Has received a config enumerate
#define DN_STOP_FREE_RES    0x00100000  // S: When child is stopped, free resources
#define DN_REBAL_CANDIDATE  0x00200000  // S: Don't skip during rebalance
#define DN_BAD_PARTIAL      0x00400000  // S: This devnode's log_confs do not have same resources
#define DN_NT_ENUMERATOR    0x00800000  // S: This devnode's is an NT enumerator
#define DN_NT_DRIVER        0x01000000  // S: This devnode's is an NT driver
#define DN_FILTERED        (0x00000800) // Is filtered
#define DN_MOVED           (0x00001000) // Has been moved

*/

void DumpDevnodeStatus(ULONG status)
{
    TCHAR buffer[1024];

    _tcscpy(buffer, L"   Status: ");

    if (status & DN_DRIVER_LOADED)
        _tcscat_s(buffer, 1024, L"DriverLoaded ");
    if (status & DN_STARTED)      
        _tcscat_s(buffer, 1024, L"Started ");
    if (status & DN_MANUAL)       
        _tcscat_s(buffer, 1024, L"Manual ");
    if (status & DN_HAS_PROBLEM)  
        _tcscat_s(buffer, 1024, L"HasProblem ");
    if (status & DN_PRIVATE_PROBLEM)  
        _tcscat_s(buffer, 1024, L"PrivateProblem ");
    if (status & DN_DISABLEABLE)  
        _tcscat_s(buffer, 1024, L"Disableable ");
    if (status & DN_REMOVABLE)    
        _tcscat_s(buffer, 1024, L"Removable ");

    _tcscat_s(buffer, 1024, L"\n");
    _tprintf(buffer);
}

