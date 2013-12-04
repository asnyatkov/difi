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
#include "DriverSupport.h"

BOOL InstallDriver(PCTSTR pszDriverPath, PCTSTR pszDriverName);
BOOL RemoveDriver(PCTSTR pszDriverName);
BOOL StartDriver(PCTSTR pszDriverName);
BOOL StopDriver(PCTSTR pszDriverName);

BOOL InstallDriver(PCTSTR pszDriverPath, PCTSTR pszDriverName)
{
    SC_HANDLE hSCManager;
    SC_HANDLE hService;

    //Remove any previous instance of the driver
    RemoveDriver(pszDriverName);

    hSCManager=OpenSCManager(NULL, NULL, SC_MANAGER_ALL_ACCESS);

    if (hSCManager)
    {
        //Install the driver
        hService=CreateService( hSCManager,
                                pszDriverName,
                                pszDriverName,
                                SERVICE_ALL_ACCESS,
                                SERVICE_KERNEL_DRIVER,
                                SERVICE_DEMAND_START,
                                SERVICE_ERROR_NORMAL,
                                pszDriverPath,
                                NULL,
                                NULL,
                                NULL,
                                NULL,
                                NULL);

        CloseServiceHandle(hSCManager);
    
        if (hService==NULL)
            return FALSE;
    }
    else
        return FALSE;

    CloseServiceHandle(hService);

    return TRUE;
}

BOOL RemoveDriver(PCTSTR pszDriverName)
{
    SC_HANDLE hSCManager;
    SC_HANDLE hService;
    BOOL bResult;

    StopDriver(pszDriverName);

    hSCManager=OpenSCManager(NULL, NULL, SC_MANAGER_ALL_ACCESS);

    if (hSCManager)
    {
        hService=OpenService(hSCManager, pszDriverName, SERVICE_ALL_ACCESS);

        CloseServiceHandle(hSCManager);

        if (hService)
        {
            bResult=DeleteService(hService);

            CloseServiceHandle(hService);
        }
        else
            return FALSE;
    }
    else
        return FALSE;

    return bResult;
}

BOOL StartDriver(PCTSTR pszDriverName)
{
    SC_HANDLE hSCManager;
    SC_HANDLE hService;
    BOOL bResult;

    hSCManager=OpenSCManager(NULL, NULL, SC_MANAGER_ALL_ACCESS);

    if (hSCManager)
    {
        hService=OpenService(hSCManager, pszDriverName, SERVICE_ALL_ACCESS);

        CloseServiceHandle(hSCManager);

        if (hService)
        {
            bResult=StartService(hService, 0, NULL);
            if (bResult==FALSE)
            {
                if (GetLastError()==ERROR_SERVICE_ALREADY_RUNNING)
                    bResult=TRUE;
            }

            CloseServiceHandle(hService);
        }
        else
            return FALSE;
    }
    else
        return FALSE;

    return bResult;
}

BOOL StopDriver(PCTSTR pszDriverName)
{
    SC_HANDLE hSCManager;
    SC_HANDLE hService;
    SERVICE_STATUS ServiceStatus;
    BOOL bResult;

    hSCManager=OpenSCManager(NULL, NULL, SC_MANAGER_ALL_ACCESS);

    if (hSCManager)
    {
        hService=OpenService(hSCManager, pszDriverName, SERVICE_ALL_ACCESS);

        CloseServiceHandle(hSCManager);

        if (hService)
        {
            bResult=ControlService(hService, SERVICE_CONTROL_STOP, &ServiceStatus);

            CloseServiceHandle(hService);
        }
        else
            return FALSE;
    }
    else
        return FALSE;

    return bResult;
}
