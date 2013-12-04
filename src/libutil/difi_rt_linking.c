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

  Kernel mode run time linking support
*/
#include "libutil/difi_ddk_helpers.h"
#include <Aux_klib.h>

static LONG aux_klib_initialized = 0;

void  difi_ddk_helper_initialize()
{
   // TODO: this must be run with interrupts enabled.
   if(InterlockedCompareExchange(&aux_klib_initialized, 1, 0) == 0)
   {
        difi_dbg_print("Calling AuxKlibInitialize\n");
        AuxKlibInitialize();
   }
}


int difi_get_module_info(const char* module_name, void** base, long* size)
{
    NTSTATUS  status;
    ULONG     modules_size;
    AUX_MODULE_EXTENDED_INFO*  modules;
    AUX_MODULE_EXTENDED_INFO*  module;
    ULONG     number_of_modules;
    int       name_length;
    char      module_name_to_search[AUX_KLIB_MODULE_PATH_LEN];
    
    if (module_name == NULL)
    {
        return -1;
    }
    
    if (aux_klib_initialized == 0)
    {
        difi_dbg_print("Must call difi_ddk_helper_initialize from your driver entry point!");
        return -1;
    }
    
    *base = NULL;
    *size = 0;
    name_length = strlen(module_name);
    strcpy(module_name_to_search, module_name);
    strcat(module_name_to_search, ".sys");
    name_length = strlen(module_name_to_search);
    
    /* Get the required array size. */
    status = AuxKlibQueryModuleInformation(&modules_size,
                                           sizeof(AUX_MODULE_EXTENDED_INFO),
                                           NULL);

    if (!NT_SUCCESS(status) || modules_size == 0)
    {
        difi_dbg_print("AuxKlibQueryModuleInformation failed: %x\n", status);
        return status;
    }

    number_of_modules = modules_size / sizeof(AUX_MODULE_EXTENDED_INFO);
    difi_dbg_print("Number of modules: %d, module to search: %s\n", number_of_modules, module_name_to_search);

    modules = 
        (AUX_MODULE_EXTENDED_INFO*) ExAllocatePoolWithTag(
                                          PagedPool,
                                          modules_size,
                                          '3LxF');
    if (modules == NULL)
    {
        status = STATUS_INSUFFICIENT_RESOURCES;
        return status;
    }

    RtlZeroMemory(modules, modules_size);

    status = AuxKlibQueryModuleInformation(&modules_size,
                                           sizeof(AUX_MODULE_EXTENDED_INFO),
                                           modules);
    if (!NT_SUCCESS(status))
    {
        ExFreePool(modules);
        return status;
    }

    for (module = modules; module < modules + number_of_modules; module++)
    {
        if (_strnicmp((const char*)module_name_to_search, 
                      (const char*)(&module->FullPathName[0] + module->FileNameOffset),
                     name_length) == 0)
        {    
            *base = module->BasicInfo.ImageBase;
            *size = (long)module->ImageSize;
            break;
        }

        if (_strnicmp((const char*)module_name, 
                (const char*)(&module->FullPathName[0] + module->FileNameOffset),
                strlen(module_name)) == 0)
        {    
            *base = module->BasicInfo.ImageBase;
            *size = (long)module->ImageSize;
            break;
        }
    }

    ExFreePool(modules);
    difi_dbg_print("Module base: %x\n", *base);
    return 0;
}

void* difi_get_module_base(const char* module_name)
{
    void* base;
    long  size;

    if (difi_get_module_info(module_name, &base, &size) == 0)
        return base;

    return NULL;
}


void* difi_get_function_address(void* module_base, const char* function_name)
{
  PIMAGE_EXPORT_DIRECTORY image_export_dir;
  ULONG* addr_of_funcs;
  ULONG* addr_of_names;
  SHORT* addr_of_ordinals;
  unsigned    i;
  char*  export_name;
  
  image_export_dir = AuxKlibGetImageExportDirectory(module_base);
  if (image_export_dir == NULL)
  {
    difi_dbg_print("No exports in the image (looking for %s)\n", function_name);
    return NULL;
  }
  addr_of_funcs    = (ULONG*)((ULONG)module_base + image_export_dir->AddressOfFunctions);
  addr_of_names    = (ULONG*)((ULONG)module_base + image_export_dir->AddressOfNames);
  addr_of_ordinals = (SHORT*)((ULONG)module_base + image_export_dir->AddressOfNameOrdinals);

  difi_dbg_print("Looking for %s, export dir: %x\n", function_name, image_export_dir);

  for(i = 0; i < image_export_dir->NumberOfNames; i++)
  {
    ULONG ord = addr_of_ordinals[i];

    export_name = ((char*)module_base) + addr_of_names[i];
    difi_dbg_print("  Ord: %d name: %s addr: %x\n", ord, export_name, addr_of_funcs[ord]);
    if (_stricmp((const char*)export_name, (const char*)function_name) == 0)
    {
        return ((char*)module_base) + addr_of_funcs[ord];
    }
  }
  return NULL;    
}

void* difi_get_module_export(const char *module_name, const char *func_name)
{
    void *base, *func_addr;

    if ( module_name == NULL || func_name == NULL )
    {
        difi_dbg_print("get_module_export: Invalid input parameter.  Module name - %s, Function name - %s\n",
            module_name, func_name);
        return NULL;
    }

    base = difi_get_module_base(module_name);
    if ( base == NULL )
    {
        difi_dbg_print("Unable to get module base for %s\n", module_name);
        return NULL;
    }

    func_addr = difi_get_function_address(base, func_name);
    if ( func_addr == NULL )
        difi_dbg_print("get_module_export: Unable to get function address!\n");

    return func_addr;
}

