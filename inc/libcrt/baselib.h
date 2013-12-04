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
#ifndef BASELIB_H
#define BASELIB_H

/* Small portability layer for unit tests */
#ifndef DIFI_USER_MODE
    #include <ntddk.h>
    #define difi_dbg_print DbgPrint
    #define difi_assert(_exp) \
    ((!(_exp)) ? \
        (DbgPrint("%s(%d): Assertion failed\n  Expression: %s\n", __FILE__, __LINE__, #_exp),FALSE) : \
        TRUE)

#else
    #include <stdio.h>
    #include <stdlib.h>
    #define difi_dbg_print printf
    #define difi_assert assert
#endif


#include "libcrt/types.h"

void quick_sort_ulong64(ulong64_t *arr, unsigned elements);
void quick_sort_ulong64_ptr(ulong64_t** arr, unsigned elements);



#endif

