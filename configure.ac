# Configure autoconf
AC_INIT([phpturd], [1.0.1], [mbrown@fensystems.co.uk])
AC_CONFIG_MACRO_DIRS([m4])
AC_CONFIG_SRCDIR([src/phpturd.c])
AC_CONFIG_AUX_DIR([build-aux])
AC_CONFIG_HEADERS([config.h])

# Check for programs
AC_PROG_CC
AC_PROG_CXX
AM_PROG_AR

# Configure libtool
LT_INIT([shared pic-only disable-static])

# Configure automake
AM_INIT_AUTOMAKE([foreign subdir-objects])

# Checks
AC_CHECK_HEADERS([stddef.h stdio.h stdlib.h string.h unistd.h dlfcn.h])
AC_C_INLINE
AC_TYPE_SIZE_T
AC_TYPE_MODE_T
AC_FUNC_MALLOC
AC_CHECK_FUNCS([realpath strchr strdup])

# Generate files
AC_CONFIG_FILES([Makefile src/Makefile])
AC_OUTPUT
