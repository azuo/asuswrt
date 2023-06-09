/*      $NetBSD: libkern.h,v 1.23 1998/07/31 23:44:41 perry Exp $       */

/*-
 * Copyright (c) 1992, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *      This product includes software developed by the University of
 *      California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *      @(#)libkern.h   8.2 (Berkeley) 8/5/94
 */

#include <sys/types.h>

#ifndef LIBKERN_INLINE
#define LIBKERN_INLINE  static __inline
#define LIBKERN_BODY
#endif

LIBKERN_INLINE int imax __P((int, int)) __attribute__ ((unused));
LIBKERN_INLINE int imin __P((int, int)) __attribute__ ((unused));
LIBKERN_INLINE u_int max __P((u_int, u_int)) __attribute__ ((unused));
LIBKERN_INLINE u_int min __P((u_int, u_int)) __attribute__ ((unused));
LIBKERN_INLINE long lmax __P((long, long)) __attribute__ ((unused));
LIBKERN_INLINE long lmin __P((long, long)) __attribute__ ((unused));
LIBKERN_INLINE u_long ulmax __P((u_long, u_long)) __attribute__ ((unused));
LIBKERN_INLINE u_long ulmin __P((u_long, u_long)) __attribute__ ((unused));
LIBKERN_INLINE int abs __P((int)) __attribute__ ((unused));

#ifdef LIBKERN_BODY
LIBKERN_INLINE int
imax(a, b)
        int a, b;
{
        return (a > b ? a : b);
}
LIBKERN_INLINE int
imin(a, b)
        int a, b;
{
        return (a < b ? a : b);
}
LIBKERN_INLINE long
lmax(a, b)
        long a, b;
{
        return (a > b ? a : b);
}
LIBKERN_INLINE long
lmin(a, b)
        long a, b;
{
        return (a < b ? a : b);
}
LIBKERN_INLINE u_int
max(a, b)
        u_int a, b;
{
        return (a > b ? a : b);
}
LIBKERN_INLINE u_int
min(a, b)
        u_int a, b;
{
        return (a < b ? a : b);
}
LIBKERN_INLINE u_long
ulmax(a, b)
        u_long a, b;
{
        return (a > b ? a : b);
}
LIBKERN_INLINE u_long
ulmin(a, b)
        u_long a, b;
{
        return (a < b ? a : b);
}

LIBKERN_INLINE int
abs(j)
        int j;
{
        return(j < 0 ? -j : j);
}
#endif

#ifdef NDEBUG                                           /* tradition! */
#define assert(e)       ((void)0)
#else
#ifdef __STDC__
#define assert(e)       ((e) ? (void)0 :                                    \
                            __assert("", __FILE__, __LINE__, #e))
#else
#define assert(e)       ((e) ? (void)0 :                                    \
                            __assert("", __FILE__, __LINE__, "e"))
#endif
#endif

#ifndef DIAGNOSTIC
#define KASSERT(e)      ((void)0)
#else
#ifdef __STDC__
#define KASSERT(e)      ((e) ? (void)0 :                                    \
                            __assert("diagnostic ", __FILE__, __LINE__, #e))
#else
#define KASSERT(e)      ((e) ? (void)0 :                                    \
                            __assert("diagnostic ", __FILE__, __LINE__, "e"))
#endif
#endif

#ifndef DEBUG
#define KDASSERT(e)     ((void)0)
#else
#ifdef __STDC__
#define KDASSERT(e)     ((e) ? (void)0 :                                    \
                            __assert("debugging ", __FILE__, __LINE__, #e))
#else
#define KDASSERT(e)     ((e) ? (void)0 :                                    \
                            __assert("debugging ", __FILE__, __LINE__, "e"))
#endif
#endif

#define offsetof(type, member)  ((size_t)(&((type *)0)->member))

/* Prototypes for non-quad routines. */
void     __assert __P((const char *, const char *, int, const char *))
            __attribute__((__noreturn__));
int      bcmp __P((const void *, const void *, size_t));
void     bzero __P((void *, size_t));
int      ffs __P((int));
void    *memchr __P((const void *, int, size_t));
void    *memcpy __P((void *, const void *, size_t));
void    *memmove __P((void *, const void *, size_t));
void    *memset __P((void *, int, size_t));
int      pmatch __P((const char *, const char *, const char **));
u_long   random __P((void));
int      scanc __P((u_int, const u_char *, const u_char *, int));
int      skpc __P((int, size_t, u_char *));
char    *strcat __P((char *, const char *));
char    *strchr __P((const char *, int));
int      strcmp __P((const char *, const char *));
char    *strcpy __P((char *, const char *));
size_t   strlen __P((const char *));
int      strncmp __P((const char *, const char *, size_t));
char    *strncpy __P((char *, const char *, size_t));
char    *strrchr __P((const char *, int));
int      strncasecmp __P((const char *, const char *, size_t));
