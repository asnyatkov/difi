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

#include <map>
#include "difi-lib/DeviceSupport.h"
#include "difi-lib/DriverSupport.h"
#include "difi-lib/DiskSupport.h"
#include "difi-lib/DifiInterface.h"
#include "diskfilter/difi_interface.h"

typedef std::map<std::wstring, DifiDeviceInfo> DeviceMap_t;
LPTSTR enumerator = REGSTR_KEY_PCIENUM;

BOOL dryRun = FALSE;
BOOL disable = FALSE;
BOOL print = TRUE;
BOOL saveState = FALSE;
BOOL restoreState = FALSE;
BOOL reEnable = FALSE;
BOOL restoreOnReboot = FALSE;
BOOL printDiskStats = FALSE;
BOOL allocStorage = FALSE;
BOOL initStorage = FALSE;
BOOL simulate = FALSE;
BOOL trackDisk = FALSE;
BOOL flushStorage = FALSE;

DWORD reEnableDelaySec = 10;
DWORD allocStorageGb = 3;
DeviceMap_t allPciDevices;
TCHAR programPath[MAX_PATH];

void PrintDeviceInfo(__in HDEVINFO hDevInfo, __in SP_DEVINFO_DATA* deviceInfoData);
void MayBeDisableDevice(HDEVINFO hDevInfo, SP_DEVINFO_DATA* deviceInfoData);
void ReEnableDevice(HDEVINFO hDevInfo, SP_DEVINFO_DATA* deviceInfoData);
void CollectDeviceData(HDEVINFO hDevInfo,  SP_DEVINFO_DATA* deviceInfoData);
void PersistDeviceInfo();
void RestoreDeviceInfo(const LPTSTR fileName);
void AddRestoreRegKey() ;
void AllocStorage();
void InitStorage();

std::wstring GetFullExePath()
{
    wchar_t buf[MAX_PATH];
    
    if (!GetModuleFileName(GetModuleHandle(NULL), buf, MAX_PATH))
    {
        return L"";         // FIXME
    }
    return std::wstring(buf);
}

std::wstring Registrize(const std::wstring& path)
{
    std::wstring res = path;

    for(size_t i=0; i<path.length(); i++)
    {
        if (res[i] == '\\') {
            // Not sure, the doc says to replace all slashes with doubles, but nobody does that
            //res.insert(i, L"\\");
            i += 2;
        }
    }
    return std::wstring(L"\"") + res + L"\"";
}

BOOL GetProgramPath(PTSTR szDriverPath)
{
    PTSTR pszSlash;

    if (!GetModuleFileName(GetModuleHandle(NULL), szDriverPath, MAX_PATH))
        return FALSE;

    pszSlash=_tcsrchr(szDriverPath, _T('\\'));

    if (pszSlash)
        pszSlash[1]=_T('\0');
    else
        return FALSE;

    return TRUE;
}

std::wstring GetStateSaveFilename()
{
    return std::wstring(programPath) + L"difi-device-status.json";
}

std::wstring GetFullDriverPath(const LPTSTR driverName)
{
    return std::wstring(programPath) + driverName;
}

void PrintUsage()
{
    const char* helpText = 
        "Usage: difi-cli <options>\n"
        "  --print                Print out device information\n"
        "  --disable              Disable some devices\n"
        "  --dry-run              Dry run\n"
        "  --save                 Save device status to a text file\n"
        "  --restore              Restore devices status previously saved by --save\n"
        "  --reenable             Re-enable devices\n"
        "  --delay-sec <seconds>  \n"
        "  --alloc-storage <N GB> Allocate N gigabytes of disk storage for tracking\n"
        "  --print-disk-stats     Print Difi driver stats (if tracking)\n"
        "  --init-storage         Init storage for Difi\n"
        "  --track-disk           Start disk tracking\n"
        "  --simulate             Simulate disk tracking (works only with --track-disk)\n"
        "  --flush-storage        Try to flush disk storage using SHADOW VOLUME COPY"
    ;

};

void ParseCommandLine(int argc, wchar_t* argv[])
{
    // TODO: quickie, should use C++ command line parsing library
    for(int i = 1; i < argc; i++) {
        if (wcscmp(argv[i], L"--print") == 0) {
            print = TRUE;
            disable = FALSE;
        } else if (wcscmp(argv[i], L"--disable") == 0) {
            disable = TRUE;
            print = FALSE;
        } else if (wcscmp(argv[i], L"--dry-run") == 0) {
            dryRun = TRUE;
        } else if (wcscmp(argv[i], L"--save") == 0) {
            saveState = TRUE;
        } else if (wcscmp(argv[i], L"--restore") == 0) {
            restoreState = TRUE;
        } else if (wcscmp(argv[i], L"--reenable") == 0) {
            reEnable = TRUE;
        } else if (wcscmp(argv[i], L"--delay-sec") == 0) {
            ++i;
            if (i == argc)
                break;
            reEnableDelaySec = _wtoi(argv[i]);
        } else if (wcscmp(argv[i], L"--print-disk-stats") == 0) {
            printDiskStats = TRUE;
        } else if (wcscmp(argv[i], L"--alloc-storage") == 0) {
            allocStorage = TRUE;
            ++i;
            if (i == argc) {
                printf("--alloc-storage expects size in Gb\n");
                exit(1);
            }
            allocStorageGb = _wtoi(argv[i]);
            if (allocStorageGb < 2) {
                printf("You should allocate at least 2Gb of storage space\n");
                exit(1);
            }
        } else if (wcscmp(argv[i], L"--init-storage") == 0) {
            initStorage = TRUE;
        } else if (wcscmp(argv[i], L"--simulate") == 0) {
            simulate = TRUE;
        } else if (wcscmp(argv[i], L"--track-disk") == 0) {
            trackDisk = TRUE;
        } else if (wcscmp(argv[i], L"--flush-storage") == 0) {
            flushStorage = TRUE;
        }
    }
}

int _tmain(int argc, _TCHAR* argv[])
{
    ParseCommandLine(argc, argv);
    GetProgramPath(programPath);
    wprintf(L"Program path: %s\n", programPath);
    
    if (flushStorage) {
        printf("Flushing storage\n");
        FlushStorage(0);
    }

    if (printDiskStats) {
        DifiInterface df;

        df.PrintDiskTrackingStats(L"");
        return 0;
    }
    

    if (allocStorage) {
        AllocStorage();
        return 0;
    }

    if (initStorage) {
        DifiInterface df;

        if (df.InitStorage() < 0)
            printf("Failed to init difi storage\n");
        else
            printf("Successfully initialized difi storage\n");
        return 0;
    }

    if (trackDisk) {
        DifiInterface difi;

        difi.TrackDisk(L"", (BOOL)simulate);
        printf("Started disk tracker\n");
        return 0;
    }

    if (print) {
        // Enumerate through all devices in Set.
        wprintf(L"PCI devices:\n======================\n");
        EnumerateDevices(PrintDeviceInfo, enumerator);
    }

    if (restoreState) {
        RestoreDeviceInfo(L"");
        EnumerateDevices(ReEnableDevice, enumerator);    
        return 0;
    }

    if (disable) {
        EnumerateDevices(MayBeDisableDevice, enumerator);    
    }

    if (saveState) {
        if(allPciDevices.empty()) {
            EnumerateDevices(CollectDeviceData, enumerator);
        }
        PersistDeviceInfo();
        if (restoreOnReboot) {
            AddRestoreRegKey();
        }
    }

    if (reEnable) {
        wprintf(L"PCI devices status:\n======================\n");
        EnumerateDevices(PrintDeviceInfo, enumerator);

        if(allPciDevices.empty()) {
            wprintf(L"Nothing to re-enable\n");
            return 0;
        }

        DWORD delay = reEnableDelaySec;
        while(delay > 0) {
            wprintf(L"Sleeping for %d secs\r", delay);
            Sleep(1000);
            --delay;
        }
        wprintf(L"\n\n");
        EnumerateDevices(ReEnableDevice, enumerator);
    }


    return 0;
}

unsigned total;
int progress_cb(unsigned mb)
{
    printf("\rInitialized %uMb out of %uMb", mb, total);
    return 0;
}

void AllocStorage()
{
    ioctl_difi_storage_info* info;
    
    printf("Allocating DeeFee storage\n");
    total = allocStorageGb * 1024;
    int storage = AllocateStorage(allocStorageGb, progress_cb);
    printf("\n");
    if (RetrieveStorageExtents(storage, &info) < 0) {
        printf("Unable to retrieve storage extents\n");
        return;
    }
    
    wprintf(L"Filename: %s total size: %llu extents: %d\n", 
        info->file_name,   info->total_size, info->extent_count);

    for (size_t i = 0; i<info->extent_count; i++)
        wprintf(L"  Extent #%u LBA: %llu Sectors: %u\n", 
                i,
                info->extents[i].start_lba,
                info->extents[i].length_in_sectors);
}

void AddRestoreRegKey() 
{
    HKEY hKey = 0;
    char buf[255] = {0};
    DWORD dwType = REG_SZ;
    DWORD dwBufSize = sizeof(buf);
    const wchar_t* subkey = L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\RunOnce";
    
    if( RegOpenKey(HKEY_LOCAL_MACHINE,subkey,&hKey) == ERROR_SUCCESS)
    {
        std::wstring run = Registrize(GetFullExePath()) + L" --restore";
        DWORD res = RegSetValueEx(hKey, L"DifiRestore", 0, REG_SZ, 
                      (BYTE*)run.c_str(), (run.length() + 1)* sizeof(wchar_t));
        if (res != ERROR_SUCCESS) {
            wprintf(L"Unable to set RunOnce key!\n");
        }
        RegCloseKey(hKey);
        return;
    }

    wprintf(L"Unable to open RunOnce key!\n");

}

void CollectDeviceData(HDEVINFO hDevInfo,  SP_DEVINFO_DATA* deviceInfoData)
{
    DifiDeviceInfo difiDeviceInfo;

    GetDeviceInfo(hDevInfo, deviceInfoData, &difiDeviceInfo);
    allPciDevices.insert(std::pair<std::wstring, DifiDeviceInfo>(difiDeviceInfo.physObjectName, difiDeviceInfo));
}

void PersistDeviceInfo()
{
    FILE* out = _wfopen(GetStateSaveFilename().c_str(), L"w+");
    for (DeviceMap_t::iterator it = allPciDevices.begin() ; it != allPciDevices.end(); it++ ) {
        //fwprintf(out, L"\"device\" : \"%s\", \"status\" : \"%d\"\n", it->first.c_str(), it->second.devStatus);
        fwprintf(out, L"%s %d\n", it->first.c_str(), it->second.devStatus);
    }
    fclose(out);
}

void RestoreDeviceInfo(const LPTSTR fileName)
{
    allPciDevices.clear();
    EnumerateDevices(CollectDeviceData, enumerator);

    FILE* in = _wfopen(GetStateSaveFilename().c_str(), L"r+");
    while(1) {
        wchar_t device[100]; 
        DIFI_DEV_STATUS status; 
        //int res = fwscanf(in, L"\"device\" : \"%s\", \"status\" : \"%d\"", device, &status);
        int res = fwscanf(in, L"%s %d", device, &status);
        if (res == EOF)
            break;
        if (res < 2) {
            wprintf(L"Error scanning input file! res=%d\n\n", res);
            return;
        }
        DeviceMap_t::iterator it = allPciDevices.find(device);
        if (it == allPciDevices.end()) {
            wprintf(L"Device %s not found!\n", device);
            continue;
        }
        it->second.devStatus = status;
        wprintf(L"Device [%s]: setting status to %d\n", it->second.deviceName.c_str(), status);
    }
    EnumerateDevices(ReEnableDevice, enumerator);
    fclose(in);
}


bool DeviceFilter(HDEVINFO hDevInfo, SP_DEVINFO_DATA* deviceInfoData)
{
    LPTSTR devClass = GetDeviceStringProperty(hDevInfo, deviceInfoData, SPDRP_CLASS);
    delete [] devClass;

    return true;
}

void MayBeDisableDevice(HDEVINFO hDevInfo,  SP_DEVINFO_DATA* deviceInfoData)
{
    DifiDeviceInfo difiDeviceInfo;

    GetDeviceInfo(hDevInfo, deviceInfoData, &difiDeviceInfo);
    _tprintf(TEXT("Device [%s] (%s): "), difiDeviceInfo.deviceName.c_str(), difiDeviceInfo.physObjectName.c_str());
    std::pair<DeviceMap_t::iterator, bool> inserted = allPciDevices.insert(
        std::pair<std::wstring, DifiDeviceInfo>(difiDeviceInfo.physObjectName, difiDeviceInfo));

    if (difiDeviceInfo.devStatus == DEV_NOT_DISABLEABLE) {
        wprintf(L" not disableable: status\n");
        return;
    }

    if (difiDeviceInfo.devType == DEV_DISPLAY || difiDeviceInfo.devType == DEV_SYSTEM ||
        difiDeviceInfo.devType == DEV_HDD || difiDeviceInfo.devType == DEV_HDD_CONTROLLER) {
        difiDeviceInfo.devStatus = DEV_NOT_DISABLEABLE;
        wprintf(L" not disableable: type\n");
        return;
    }

    DIFI_DEV_STATUS status = DEV_OK;
    if (dryRun) {
        status = DEV_DISABLED;
    } else {
        status = DisableDevice(hDevInfo, deviceInfoData, TRUE);
    }
    if(status == DEV_NEEDS_REBOOT) {
        // Needs restart, disable didn't work. Re-enable back
        wprintf(L" needs restart, re-enabling");
        DisableDevice(hDevInfo, deviceInfoData, FALSE);
    } 
    inserted.first->second.devStatus = status;
    if (status == DEV_DISABLED) {
        wprintf(L" disabled\n");
    } else if (status == DEV_FAIL) {
        wprintf(L" failed\n");
    }
}

void ReEnableDevice(HDEVINFO hDevInfo, SP_DEVINFO_DATA* deviceInfoData)
{
    LPTSTR phys = GetDeviceStringProperty(hDevInfo, deviceInfoData, 
                      SPDRP_PHYSICAL_DEVICE_OBJECT_NAME);
    DeviceMap_t::iterator dev = allPciDevices.find(phys);
    delete [] phys;

    if (dev == allPciDevices.end()) {
        wprintf(L"device not found?\n");
        return;
    }

    _tprintf(TEXT("Device [%s]: "), dev->second.deviceName.c_str());
    DIFI_DEV_STATUS status = DEV_OK;
    if (!dryRun && dev->second.devStatus == DEV_DISABLED) {
        status = DisableDevice(hDevInfo, deviceInfoData, FALSE);
        dev->second.devStatus = DEV_OK;
        if (status == DEV_NEEDS_REBOOT)
            wprintf(L" re-enabled but NEEDS REBOOT\n");
        else if (status == DEV_FAIL)
            wprintf(L" failed\n");
        else 
            wprintf(L" re-enabled\n");
    } else {
        wprintf(L" no op\n");
    }
}

void PrintDeviceInfo(HDEVINFO hDevInfo, SP_DEVINFO_DATA* deviceInfoData)
{
    DifiDeviceInfo difiDevInfo;

    GetDeviceInfo(hDevInfo, deviceInfoData, &difiDevInfo);
    LPTSTR busReportedDesc = GetBusReportedDeviceDesc(hDevInfo, deviceInfoData);

    _tprintf(TEXT("Device: [%s] %s\n   Friendly: %s Type: %04x\n"), 
             difiDevInfo.deviceName.c_str(), difiDevInfo.physObjectName.c_str(), 
             busReportedDesc, difiDevInfo.devType);
    delete [] busReportedDesc;
        
    CM_POWER_DATA powerData;
    powerData.PD_Size = sizeof(powerData);
    GetDevicePowerData(hDevInfo, deviceInfoData, &powerData);
    DumpPowerData(&powerData);

    DumpDevnodeStatus(difiDevInfo.devNodeStatus);
}




