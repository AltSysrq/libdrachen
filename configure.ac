AC_PREREQ([2.50])
AC_INIT([libdrachen], [0.0.1], [jasonlingle@yahoo.com])
AM_INIT_AUTOMAKE
AC_CONFIG_MACRO_DIR([m4])
LT_PREREQ([1.5])
LT_INIT([dlopen])
AC_CONFIG_SRCDIR([src/drachen.c])
AC_CONFIG_HEADERS([config.h])
AC_CONFIG_FILES([Makefile src/Makefile])
AM_MAINTAINER_MODE

AC_PROG_CC

AC_CHECK_HEADERS([inttypes.h stdlib.h])

AC_CHECK_FUNCS([memset strerror])

dnl Don't need AC_FUNC_MALLOC, because we don't call it with 0
dnl Don't need AC_PROG_CXX, nothing is in C++
dnl Don't need AC_PROG_RANLIB, included by LT and LT complains if we do

AC_TYPE_SIZE_T
AC_TYPE_UINT32_T
AC_TYPE_UINT16_T
AC_C_INLINE

AC_OUTPUT
