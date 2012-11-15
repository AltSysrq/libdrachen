/* This is a header internal to libdrachen.
 * Don't install it.
 */

#ifndef UNLOCK_H_
#define UNLOCK_H_

/* Redefines the stdio functions to the unlocking variants, where available.
 *
 * <config.h> must be included before this file.
 */

#include <stdio.h>

#ifdef HAVE_GETC_UNLOCKED
#define getc getc_unlocked
#endif
#ifdef HAVE_GETCHAR_UNLOCKED
#define getchar getchar_unlocked
#endif
#ifdef HAVE_PUTCHAR_UNLOCKED
#define putc putc_unlocked
#endif
#ifdef HAVE_CLEARERR_UNLOCKED
#define clearerr clearerr_unlocked
#endif
#ifdef HAVE_FEOF_UNLOCKED
#define feof feof_unlocked
#endif
#ifdef HAVE_FERROR_UNLOCKED
#define ferror ferror_unlocked
#endif
#ifdef HAVE_FILENO_UNLOCKED
#define fileno fileno_unlocked
#endif
#ifdef HAVE_FFLUSH_UNLOCKED
#define fflush fflush_unlocked
#endif
#ifdef HAVE_FGETC_UNLOCKED
#define fgetc fgetc_unlocked
#endif
#ifdef HAVE_FPUTC_UNLOCKED
#define fputc fputc_unlocked
#endif
#ifdef HAVE_FREAD_UNLOCKED
#define fread fread_unlocked
#endif
#ifdef HAVE_FWRITE_UNLOCKED
#define fwrite fwrite_unlocked
#endif
#ifdef HAVE_FGETS_UNLOCKED
#define fgets fgets_unlocked
#endif
#ifdef HAVE_FPUTS_UNLOCKED
#define fputs fputs_unlocked
#endif

#endif /* UNLOCK_H_ */
