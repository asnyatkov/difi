//  quickSort
//
//  This public-domain C implementation by Darel Rex Finley.
//
//  * This function assumes it is called with valid parameters.
//
//  * Example calls:
//    quickSort(&myArray[0],5); // sorts elements 0, 1, 2, 3, and 4
//    quickSort(&myArray[3],5); // sorts elements 3, 4, 5, 6, and 7

#include "libcrt/baselib.h"

#define  MAX_LEVELS  300

void quick_sort_ulong64(ulong64_t *arr, unsigned elements) {


  int i=0, L, R, swap, beg[MAX_LEVELS], end[MAX_LEVELS];
  ulong64_t piv;

  beg[0]=0; end[0]=elements;
  while (i>=0) {
    L=beg[i]; R=end[i]-1;
    if (L < R) {
      piv=arr[L];
      while (L < R) {
        while (arr[R]>=piv && L<R) R--; if (L<R) arr[L++]=arr[R];
        while (arr[L]<=piv && L<R) L++; if (L<R) arr[R--]=arr[L]; 
      }
      arr[L]=piv; beg[i+1]=L+1; end[i+1]=end[i]; end[i++]=L;
      if (end[i]-beg[i] > end[i-1] - beg[i-1]) {
        swap=beg[i]; beg[i]=beg[i-1]; beg[i-1]=swap;
        swap=end[i]; end[i]=end[i-1]; end[i-1]=swap; 
      }
    } else {
      i--; 
    }
  }
}

void quick_sort_ulong64_ptr(ulong64_t** arr, unsigned elements) {

  int i=0, L, R, swap, beg[MAX_LEVELS], end[MAX_LEVELS];
  ulong64_t* piv;

  beg[0]=0; end[0]=elements;
  while (i>=0) {
    L=beg[i]; R=end[i]-1;
    if (L < R) {
      piv=arr[L];
      while (L < R) {
        while (*arr[R]>=*piv && L<R) R--; if (L<R) arr[L++]=arr[R];
        while (*arr[L]<=*piv && L<R) L++; if (L<R) arr[R--]=arr[L]; 
      }
      arr[L]=piv; beg[i+1]=L+1; end[i+1]=end[i]; end[i++]=L;
      if (end[i]-beg[i] > end[i-1] - beg[i-1]) {
        swap=beg[i]; beg[i]=beg[i-1]; beg[i-1]=swap;
        swap=end[i]; end[i]=end[i-1]; end[i-1]=swap; 
      }
    } else {
      i--; 
    }
  }
}
