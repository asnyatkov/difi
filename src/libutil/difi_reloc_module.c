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

  Credit: ManualMap - by Darawk - RealmGX.com
*/
#include "libutil/difi_ddk_helpers.h"
#include <Aux_klib.h>

int fix_relocations(void* module_base, long delta);

int difi_relocate_module(void* module_base, unsigned long module_size, void* target_addr)
{
    UNREFERENCED_PARAMETER(module_size);

    _try 
    {
        //RtlCopyMemory(target_addr, module_base, module_size);
    }
    _except (EXCEPTION_EXECUTE_HANDLER)
    {
        difi_dbg_print("Unable to copy driver to the target location!\n");
        return -1;
    }


    return fix_relocations(module_base, (DWORD_PTR)target_addr - (DWORD_PTR)module_base);
}


#define MakePtr(cast, ptr, addValue) (cast)( (DWORD_PTR)(ptr) + (DWORD_PTR)(addValue))

// Fix relocations in module. Work in progress
int fix_relocations(void* module_base, long delta)
{
   IMAGE_DOS_HEADER* dos_hdr;
   IMAGE_NT_HEADERS* nt_hdr;
   IMAGE_DATA_DIRECTORY* reloc_data_dir;
   IMAGE_BASE_RELOCATION* reloc;
   IMAGE_BASE_RELOCATION* reloc_last;
   char c;

   difi_dbg_print("Relocating module %x, delta %ld\n", module_base, delta);

   dos_hdr = (IMAGE_DOS_HEADER*)module_base;
   if(dos_hdr->e_magic != IMAGE_DOS_SIGNATURE)
   {
      // Weird
      return -1;
   }

   nt_hdr = MakePtr(IMAGE_NT_HEADERS*, module_base, dos_hdr->e_lfanew);
   if(nt_hdr->Signature != IMAGE_NT_SIGNATURE)
   {
      return -1;
   }

   // Find optional header with relocations
   reloc_data_dir = &nt_hdr->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC];
   if (reloc_data_dir->VirtualAddress == 0 || reloc_data_dir->Size == 0)
   {
      difi_dbg_print("No relocations\n");
      return 0;
   }
   reloc = (IMAGE_BASE_RELOCATION*)(((ULONG_PTR)module_base) + reloc_data_dir->VirtualAddress);
   c = *(char*)reloc;
   reloc_last = (IMAGE_BASE_RELOCATION*)(((ULONG_PTR)reloc) + reloc_data_dir->Size);
   difi_dbg_print("Reloc: %x sz: %d last: %x\n", reloc, reloc_data_dir->Size, 
              reloc_last);

#if 0
   while(reloc < reloc_last && reloc->SizeOfBlock > 0)
   {
      unsigned int   num_relocs = (reloc->SizeOfBlock - sizeof(IMAGE_BASE_RELOCATION)) / sizeof(SHORT);
      //unsigned int   i;
      //unsigned long* reloc_base = MakePtr(unsigned long*, module_base, reloc->VirtualAddress);

      //unsigned short* reloc_item = (unsigned short*)(reloc + 1);
      difi_dbg_print("Blk size %d RVA %x Num relocs: %d\n", reloc->SizeOfBlock, 
                 reloc->VirtualAddress, num_relocs);

      for(i = 0; i < num_relocs; i++)
      {       
         if ((*reloc_item >> 12) == IMAGE_REL_BASED_HIGHLOW)
         {
             difi_dbg_print("  item: %x\n", *reloc_item & 0x0FFF);
             //*MakePtr(unsigned long*, reloc_base, (*reloc_item & 0x0FFF)) += delta;
         }
         else if ((*reloc_item >> 12) == IMAGE_REL_BASED_DIR64)
         {
            // FIXME: handle 64-bit relocations
         }
         reloc_item++;
      }

      reloc = (IMAGE_BASE_RELOCATION*)reloc_item;
      break;
   }
#endif
   return 0;
}

