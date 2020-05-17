/*
 * Copyright (C) 2020 Michael Brown <mbrown@fensystems.co.uk>.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 */

#define _GNU_SOURCE
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <utime.h>
#include <errno.h>
#include <fcntl.h>
#include <dirent.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/xattr.h>
#include <selinux/selinux.h>
#include <dlfcn.h>

/** Environment variable name */
#define PHPTURD "PHPTURD"

/** Enable debugging */
#ifndef DEBUG
#define DEBUG 0
#endif

/* Mode for implicitly created directories
 *
 * There is no guaranteed safe way to determine the permissions for a
 * directory created without the application's knowledge.  We could
 * potentially copy the mode bits from the corresponding readonly
 * directory, but we are unlikely to be able to set the owner and
 * group of the created directory to match.  We cannot simply rely on
 * the umask since the application may suppose that sensitive
 * directory permissions have been set in advance, so the umask may
 * not reflect the required permissions for this directory.
 *
 * We choose a relatively paranoid mode 0750 for implicitly created
 * directories.
 */
#define MKDIR_MODE ( S_IRWXU | S_IRGRP | S_IXGRP )

/* Error return values */
typedef char * char_ptr;
typedef DIR * DIR_ptr;
typedef FILE * FILE_ptr;
#define char_ptr_error_return NULL
#define int_error_return -1
#define ssize_t_error_return -1
#define DIR_ptr_error_return NULL
#define FILE_ptr_error_return NULL

/* Original library functions */
static int ( * orig_access ) ( const char *path, int mode );
static int ( * orig_mkdir ) ( const char *path, mode_t mode );

/**
 * Check if canonicalised path starts with a given prefix directory
 *
 * @v path		Path
 * @v prefix		Path prefix
 * @v len		Length of path prefix
 * @ret startswith	Path starts with the path prefix
 */
static inline int path_starts_with ( const char *path, const char *prefix,
				     size_t prefix_len ) {

	return ( ( strncmp ( path, prefix, prefix_len ) == 0 ) &&
		 ( ( path[prefix_len] == '/' ) ||
		   ( path[prefix_len] == '\0' ) ) );
}

/**
 * Convert to a canonical absolute path (ignoring symlinks)
 *
 * @v path		Path
 * @v func		Wrapped function name (for debugging)
 * @ret canonical	Canonical path, or NULL on error
 *
 * The caller is repsonsible for calling free() on the returned path.
 */
static char * canonical_path ( const char *path, const char *func ) {
	size_t cwd_len;
	size_t path_len;
	char *cwd;
	char *result;
	const char *in;
	char *out;
	char c;

	/* Get current working directory */
	if ( path[0] != '/' ) {
		cwd = getcwd ( NULL, 0 );
		if ( ! cwd )
			goto err_getcwd;
		cwd_len = strlen ( cwd );
	} else {
		cwd = NULL;
		cwd_len = 0;
	}

	/* Allocate result path */
	path_len = strlen ( path );
	result = malloc ( cwd_len + 1 /* '/' */ + path_len + 1 /* NUL */ );
	if ( ! result )
		goto err_result;

	/* Construct result path */
	if ( cwd ) {
		memcpy ( result, cwd, cwd_len );
		result[cwd_len] = '/';
		memcpy ( ( result + cwd_len + 1 ), path,
			 ( path_len + 1 /* NUL */ ) );
	} else {
		memcpy ( result, path, ( path_len + 1 ) );
	}

	/* Canonicalise result */
	in = out = result;
	while ( 1 ) {

		/* Dump debug information */
		if ( DEBUG >= 2 ) {
			c = *out;
			*out = '\0';
			fprintf ( stderr, PHPTURD " [%s] %s -> %s ",
				  func, path, result );
			*out = c;
			fprintf ( stderr, "%s\n", in );
		}

		/* Copy path (in place) */
		c = *out++ = *in++;
		if ( c == '\0' )
			break;
		if ( c != '/' )
			continue;

		/* We have a "/".  Remove all consecutive "/" */
		while ( *in == '/' )
			in++;
		if ( *in != '.' )
			continue;
		in++;

		/* We have a "/." */
		if ( ( *in == '/' ) || ( *in == '\0' ) ) {
			/* "/./" or "/.\0" - strip it out */
			out--;
			continue;
		}
		if ( *in != '.' ) {
			/* Filename starting with a dot - continue */
			in--;
			continue;
		}
		in++;

		/* We have a "/.." */
		if ( ( *in == '/') || ( *in == '\0' ) ) {
			/* "/../" or "/..\0" - go up a level */
			out--;
			while ( out > result ) {
				out--;
				if ( *out == '/' )
					break;
			}
		}
	}

	/* Free current working directory */
	free ( cwd );

	return result;

 err_result:
	free ( cwd );
 err_getcwd:
	return NULL;
}

/**
 * Attempt to create intermediate directories, ignoring failures
 *
 * @v path		Canonicalised absolute path
 * @v top		Top directory (known to already exist)
 * @v end		End of path
 *
 * Allow for attempts to create files within the writable directory in
 * subdirectories that currently exist only in the readonly directory,
 * by transparently creating the corresponding subdirectories as
 * needed.  Optimise for the common case that the subdirectories
 * already exist and no action is required.
 */
static void create_intermediate_dirs ( const char *path, const char *top,
				       char *end ) {
	char tmp;

	/* Find parent directory separator, allowing for the fact that
	 * the initial path may end with a '/'.
	 */
	end--;
	while ( 1 ) {
		end--;
		if ( end <= top )
			goto finished;
		if ( *end == '/' )
			break;
	}

	/* Temporarily truncate path */
	tmp = *end;
	*end = '\0';

	/* Do nothing if parent directory already exists */
	if ( orig_access ( path, F_OK ) == 0 )
		goto exists;

	/* Recursively ensure that parent directories exist */
	create_intermediate_dirs ( path, top, end );

	/* Create this directory */
	if ( orig_mkdir ( path, MKDIR_MODE ) != 0 ) {
		if ( DEBUG >= 1 ) {
			fprintf ( stderr, PHPTURD " could not create %s: %s\n",
				  path, strerror ( errno ) );
		}
		/* Ignore failure; minimise surprise by letting errors
		 * be reported by the original library call that
		 * subsequently tries to access the file within the
		 * nonexistent directory.
		 */
	}

 exists:
	/* Restore truncated path */
	*end = tmp;
 finished:
	return;
}

/**
 * Convert to a turdified path
 *
 * @v path		Path
 * @v mkdirs		Create intermediate directories if needed
 * @v func		Wrapped function name (for debugging)
 * @ret turdpath	Turdified path
 *
 * The caller is repsonsible for calling free() on the returned path
 * if and only if the pointer value differs from the original path.
 */
static char * turdify_path ( const char *path, int mkdirs, const char *func ) {
	static int used;
	static const char *readonly;
	static const char *writable;
	static size_t readonly_len;
	static size_t writable_len;
	static size_t max_prefix_len;
	const char *turd;
	const char *suffix;
	char *abspath;
	char *result;
	size_t suffix_len;
	size_t max_len;

	/* Perform initialisation on first use */
	if ( ! used ) {

		/* Record that initialisation has been attempted */
		used = 1;

		/* Get original access() function */
		orig_access = dlsym ( RTLD_NEXT, "access" );
		if ( ! orig_access ) {
			result = NULL;
			errno = ENOSYS;
			goto err_dlsym;
		}

		/* Get original mkdir() function */
		orig_mkdir = dlsym ( RTLD_NEXT, "mkdir" );
		if ( ! orig_mkdir ) {
			result = NULL;
			errno = ENOSYS;
			goto err_dlsym;
		}

		/* Check for and parse PHPTURD environment variable */
		turd = getenv ( PHPTURD );
		if ( ! turd ) {
			fprintf ( stderr, PHPTURD " [%s] no turd found\n",
				  func );
			result = ( ( char * ) path );
			goto no_turd;
		}
		readonly = strdup ( turd );
		if ( ! readonly ) {
			result = NULL;
			goto err_strdup;
		}
		writable = strchr ( readonly, ':' );
		if ( ! writable ) {
			fprintf ( stderr, PHPTURD " [%s] malformed: %s\n",
				  func, readonly );
			result = ( ( char * ) path );
			goto err_malformed;
		}
		readonly_len = ( writable - readonly );
		writable++;
		writable_len = strlen ( writable );
		max_prefix_len = readonly_len;
		if ( max_prefix_len < writable_len )
			max_prefix_len = writable_len;
	}

	/* Bypass everything if initialisation did not find a valid PHPTURD */
	if ( ! max_prefix_len ) {
		result = ( ( char * ) path );
		goto bypass;
	}

	/* Convert to an absolute path */
	abspath = canonical_path ( path, func );
	if ( ! abspath ) {
		fprintf ( stderr, PHPTURD " [%s] could not canonicalise "
			  "\"%s\"\n", func, path );
		result = NULL;
		goto err_canonical;
	}

	/* Check if path lies within a turd directory */
	if ( path_starts_with ( abspath, readonly, readonly_len ) ) {
		suffix = &abspath[readonly_len];
	} else if ( path_starts_with ( abspath, writable, writable_len ) ) {
		suffix = &abspath[writable_len];
	} else {
		result = ( ( char * ) path );
		if ( DEBUG >= 1 ) {
			fprintf ( stderr, PHPTURD " [%s] %s [unmodified]\n",
				  func, path );
		}
		goto no_prefix;
	}

	/* Calculate maximum result path length */
	suffix_len = strlen ( suffix );
	max_len = ( max_prefix_len + suffix_len );

	/* Allocate result path */
	result = malloc ( max_len + 1 /* NUL */ );
	if ( ! result )
		goto err_result;

	/* Construct readonly path variant */
	memcpy ( result, readonly, readonly_len );
	memcpy ( ( result + readonly_len ), suffix,
		 ( suffix_len + 1 /* NUL */ ) );

	/* Construct writable path if readonly path does not exist */
	if ( orig_access ( result, F_OK ) != 0 ) {

		/* Construct writable path */
		memcpy ( result, writable, writable_len );
		memcpy ( ( result + writable_len ), suffix,
			 ( suffix_len + 1 /* NUL */ ) );

		/* Ensure that path components exist, if applicable */
		if ( mkdirs ) {
			create_intermediate_dirs ( result,
						   ( result + writable_len ),
						   ( result + writable_len +
						     suffix_len ) );
		}
	}

	/* Dump debug information */
	if ( DEBUG >= 1 ) {
		fprintf ( stderr, PHPTURD " [%s] %s => %s => %s\n",
			  func, path, abspath, result );
	}

 err_result:
 no_prefix:
	free ( abspath );
 err_canonical:
 bypass:
 err_malformed:
 err_strdup:
 no_turd:
 err_dlsym:
	return result;
}

/**
 * Turdify a library call taking a single path parameter
 *
 * @v rtype		Return type
 * @v func		Library function
 * @v path		Path
 * @v mkdirs		Create intermediate directories if needed
 * @v ...		Turdified call arguments
 */
#define turdwrap1( rtype, func, path, mkdirs, ... ) do {		\
	static typeof ( func ) * orig_ ## func = NULL;			\
	char *turdpath;							\
	rtype ret;							\
									\
	/* Get original library function */				\
	if ( ! orig_ ## func ) {					\
		orig_ ## func = dlsym ( RTLD_NEXT, #func );		\
		if ( ! orig_ ## func ) {				\
			ret = rtype ## _error_return;			\
			errno = ENOSYS;					\
			goto err_dlsym;					\
		}							\
	}								\
									\
	/* Turdify path */						\
	turdpath = turdify_path ( path, mkdirs, #func );		\
	if ( ! turdpath ) {						\
		ret = rtype ## _error_return;				\
		goto err_turdpath;					\
	}								\
									\
	/* Call original library function */				\
	ret = orig_ ## func ( __VA_ARGS__ );				\
									\
	err_turdpath:							\
									\
	/* Free turdified path, if applicable */			\
	if ( turdpath != path )						\
		free ( turdpath );					\
									\
	err_dlsym:							\
									\
	/* Return value from original library function */		\
	return ret;							\
									\
	} while ( 0 )

/**
 * Turdify a library call taking two path parameters
 *
 * @v rtype		Return type
 * @v func		Library function
 * @v path1		Path one
 * @v mkdirs1		Create path one intermediate directories if needed
 * @v path2		Path two
 * @v mkdirs2		Create path two intermediate directories if needed
 * @v ...		Turdified call arguments
 */
#define turdwrap2( rtype, func, path1, mkdirs1, path2, mkdirs2,		\
		   ... ) do {						\
	static typeof ( func ) * orig_ ## func = NULL;			\
	char *turdpath1;						\
	char *turdpath2;						\
	rtype ret;							\
									\
	/* Get original library function */				\
	if ( ! orig_ ## func ) {					\
		orig_ ## func = dlsym ( RTLD_NEXT, #func );		\
		if ( ! orig_ ## func ) {				\
			ret = rtype ## _error_return;			\
			errno = ENOSYS;					\
			goto err_dlsym;					\
		}							\
	}								\
									\
	/* Turdify path one */						\
	turdpath1 = turdify_path ( path1, mkdirs1, #func );		\
	if ( ! turdpath1 ) {						\
		ret = rtype ## _error_return;				\
		goto err_turdpath1;					\
	}								\
									\
	/* Turdify path two */						\
	turdpath2 = turdify_path ( path2, mkdirs2, #func );		\
	if ( ! turdpath2 ) {						\
		ret = rtype ## _error_return;				\
		goto err_turdpath2;					\
	}								\
									\
	/* Call original library function */				\
	ret = orig_ ## func ( __VA_ARGS__ );				\
									\
	err_turdpath2:							\
									\
	/* Free turdified path two, if applicable */			\
	if ( turdpath2 != path2 )					\
		free ( turdpath2 );					\
									\
	err_turdpath1:							\
									\
	/* Free turdified path one, if applicable */			\
	if ( turdpath1 != path1 )					\
		free ( turdpath1 );					\
									\
	err_dlsym:							\
									\
	/* Return value from original library function */		\
	return ret;							\
									\
	} while ( 0 )

/*
 *
 * Library function wrappers
 *
 */

int __lxstat ( int ver, const char *path, struct stat *buf ) {
	turdwrap1 ( int, __lxstat, path, 0, ver, turdpath, buf );
}

int __xstat ( int ver, const char *path, struct stat *buf ) {
	turdwrap1 ( int, __xstat, path, 0, ver, turdpath, buf );
}

int access ( const char *path, int mode ) {
	turdwrap1 ( int, access, path, 0, turdpath, mode );
}

int chdir ( const char *path ) {
	turdwrap1 ( int, chdir, path, 0, turdpath );
}

int chmod ( const char *path, mode_t mode ) {
	turdwrap1 ( int, chmod, path, 0, turdpath, mode );
}

int chown ( const char *path, uid_t owner, gid_t group ) {
	turdwrap1 ( int, chown, path, 0, turdpath, owner, group );
}

int creat ( const char *path, mode_t mode ) {
	turdwrap1 ( int, creat, path, 1, turdpath, mode );
}

FILE * fopen ( const char *path, const char *mode ) {
	turdwrap1 ( FILE_ptr, fopen, path,
		    ( ( mode[0] == 'w' ) | ( mode[0] == 'a' ) ),
		    turdpath, mode );
}

int getfilecon ( const char *path, security_context_t *con ) {
	turdwrap1 ( int, getfilecon, path, 0, turdpath, con );
}

ssize_t getxattr ( const char *path, const char *name, void *value,
		   size_t size ) {
	turdwrap1 ( ssize_t, getxattr, path, 0, turdpath, name, value, size );
}

int lchown ( const char *path, uid_t owner, gid_t group ) {
	turdwrap1 ( int, lchown, path, 0, turdpath, owner, group );
}

int lgetfilecon ( const char *path, security_context_t *con ) {
	turdwrap1 ( int, lgetfilecon, path, 0, turdpath, con );
}

ssize_t lgetxattr ( const char *path, const char *name, void *value,
		    size_t size ) {
	turdwrap1 ( ssize_t, lgetxattr, path, 0, turdpath, name, value, size );
}

int link ( const char *path1, const char *path2 ) {
	turdwrap2 ( int, link, path1, 0, path2, 1, turdpath1, turdpath2 );
}

ssize_t listxattr ( const char *path, char *list, size_t size ) {
	turdwrap1 ( ssize_t, listxattr, path, 0, turdpath, list, size );
}

ssize_t llistxattr ( const char *path, char *list, size_t size ) {
	turdwrap1 ( ssize_t, llistxattr, path, 0, turdpath, list, size );
}

int lremovexattr ( const char *path, const char *name ) {
	turdwrap1 ( int, lremovexattr, path, 0, turdpath, name );
}

int lsetxattr ( const char *path, const char *name, const void *value,
		size_t size, int flags ) {
	turdwrap1 ( int, lsetxattr, path, 0, turdpath, name, value, size,
		    flags );
}

int lstat ( const char *path, struct stat *statbuf ) {
	turdwrap1 ( int, lstat, path, 0, turdpath, statbuf );
}

int mkdir ( const char *path, mode_t mode ) {
	turdwrap1 ( int, mkdir, path, 1, turdpath, mode );
}

int mkostemp ( char *path, int flags ) {
	turdwrap1 ( int, mkostemp, path, 1, turdpath, flags );
}

int mkostemps ( char *path, int suffixlen, int flags ) {
	turdwrap1 ( int, mkostemps, path, 1, turdpath, suffixlen, flags );
}

int mkstemp ( char *path ) {
	turdwrap1 ( int, mkstemp, path, 1, turdpath );
}

int mkstemps ( char *path, int suffixlen ) {
	turdwrap1 ( int, mkstemps, path, 1, turdpath, suffixlen );
}

char * mktemp ( char *path ) {
	turdwrap1 ( char_ptr, mktemp, path, 1, turdpath );
}

int open ( const char *path, int flags, ... ) {
	int creat = ( flags & O_CREAT );
	mode_t mode;
	va_list ap;

	va_start ( ap, flags );
	if ( creat ) {
		mode = va_arg ( ap, mode_t );
	} else {
		mode = 0;
	}
	va_end ( ap );

	turdwrap1 ( int, open, path, creat, turdpath, flags, mode );
}

DIR * opendir ( const char *path ) {
	turdwrap1 ( DIR_ptr, opendir, path, 0, turdpath );
}

ssize_t readlink ( const char *path, char *buf, size_t bufsiz ) {
	turdwrap1 ( ssize_t, readlink, path, 0, turdpath, buf, bufsiz );
}

int removexattr ( const char *path, const char *name ) {
	turdwrap1 ( int, removexattr, path, 0, turdpath, name );
}

int rename ( const char *path1, const char *path2 ) {
	turdwrap2 ( int, rename, path1, 0, path2, 1, turdpath1, turdpath2 );
}

int rmdir ( const char *path ) {
	turdwrap1 ( int, rmdir, path, 0, turdpath );
}

int setxattr ( const char *path, const char *name, const void *value,
	       size_t size, int flags ) {
	turdwrap1 ( int, setxattr, path, 0, turdpath, name, value, size,
		    flags );
}

int stat ( const char *path, struct stat *statbuf ) {
	turdwrap1 ( int, stat, path, 0, turdpath, statbuf );
}

int symlink ( const char *path1, const char *path2 ) {
	turdwrap2 ( int, symlink, path1, 0, path2, 1, turdpath1, turdpath2 );
}

int truncate ( const char *path, off_t length ) {
	turdwrap1 ( int, truncate, path, 0, turdpath, length );
}

int unlink ( const char * path ) {
	turdwrap1 ( int, unlink, path, 0, turdpath );
}

int utime ( const char *path, const struct utimbuf *times ) {
	turdwrap1 ( int, utime, path, 0, turdpath, times );
}

int utimes ( const char *path, const struct timeval times[2] ) {
	turdwrap1 ( int, utimes, path, 0, turdpath, times );
}
