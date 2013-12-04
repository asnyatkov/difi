#ifndef STDARG_H
#define STDARG_H

typedef char* va_list;


#if   defined(_M_IX86)

#define va_dcl va_list va_alist;

/*

 * define a macro to compute the size of a type, variable or expression,
 * rounded up to the nearest multiple of sizeof(int). This number is its
 * size as function argument (Intel architecture). Note that the macro
 * depends on sizeof(int) being a power of 2!
 */
#define __va_size(n)    ( (sizeof(n) + sizeof(int) - 1) & ~(sizeof(int) - 1) )

#define va_start(ap, last) \
    ((ap) = (va_list)&(last) + __va_size(last))

#define va_arg(ap, type) \
    (*(type *)((ap) += __va_size(type), (ap) - __va_size(type)))

#define va_end(ap)


#elif defined(_M_AMD64)

// TODO: find CRT-independent implementation

extern void __cdecl __va_start(__out va_list *, ...);

#define va_dcl          va_list va_alist;

#define va_start(ap)   ( __va_start(&ap, 0) )

#define va_arg(ap, t)   \
    ( ( sizeof(t) > sizeof(__int64) || ( sizeof(t) & (sizeof(t) - 1) ) != 0 ) \
        ? **(t **)( ( ap += sizeof(__int64) ) - sizeof(__int64) ) \
        :  *(t  *)( ( ap += sizeof(__int64) ) - sizeof(__int64) ) )
#define va_end(ap)      ( ap = (va_list)0 )


#endif

#endif
