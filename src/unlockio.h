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

#ifdef HAVE_GETC
#define getc getc_unlocked
#endif
#ifdef HAVE_GETCHAR
#define getchar getchar_unlocked
#endif
#ifdef HAVE_PUTCHAR
#define putc putc_unlocked
#endif
#ifdef HAVE_CLEARERR
#define clearerr clearerr_unlocked
#endif
#ifdef HAVE_FEOF
#define feof feof_unlocked
#endif
#ifdef HAVE_FERROR
#define ferror ferror_unlocked
#endif
#ifdef HAVE_FILENO
#define fileno fileno_unlocked
#endif
#ifdef HAVE_FFLUSH
#define fflush fflush_unlocked
#endif
#ifdef HAVE_FGETC
#define fgetc fgetc_unlocked
#endif
#ifdef HAVE_FPUTC
#define fputc fputc_unlocked
#endif
#ifdef HAVE_FREAD
#define fread fread_unlocked
#endif
#ifdef HAVE_FWRITE
#define fwrite fwrite_unlocked
#endif
#ifdef HAVE_FGETS
#define fgets fgets_unlocked
#endif
#ifdef HAVE_FPUTS
#define fputs fputs_unlocked
#endif

#endif /* UNLOCK_H_ */
