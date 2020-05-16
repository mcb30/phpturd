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
#include <string.h>
#include <unistd.h>
#include <utime.h>
#include <errno.h>
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

/* Error return values */
typedef FILE * FILE_ptr;
#define int_error_return -1
#define ssize_t_error_return -1
#define FILE_ptr_error_return NULL

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
 * Convert to a turdified path
 *
 * @v path		Path
 * @v func		Wrapped function name (for debugging)
 * @ret turdpath	Turdified path
 *
 * The caller is repsonsible for calling free() on the returned path.
 */
static char * turdify_path ( const char *path, const char *func ) {
	static int ( * orig_access ) ( const char *path, int mode );
	const char *turd;
	const char *readonly;
	const char *writable;
	const char *suffix;
	char *abspath;
	char *result;
	size_t readonly_len;
	size_t writable_len;
	size_t suffix_len;
	size_t max_len;

	/* Get original library functions */
	if ( ! orig_access ) {
		orig_access = dlsym ( RTLD_NEXT, "access" );
		if ( ! orig_access ) {
			result = NULL;
			errno = ENOSYS;
			goto err_dlsym;
		}
	}

	/* Check for and parse PHPTURD environment variable */
	turd = getenv ( PHPTURD );
	if ( ! turd ) {
		result = strdup ( path );
		goto no_turd;
	}
	readonly = turd;
	writable = strchr ( readonly, ':' );
	if ( ! writable ) {
		fprintf ( stderr, PHPTURD " [%s] malformed: %s\n", func, turd );
		result = strdup ( path );
		goto err_malformed;
	}
	readonly_len = ( writable - readonly );
	writable++;
	writable_len = strlen ( writable );

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
		result = strdup ( path );
		if ( DEBUG >= 1 ) {
			fprintf ( stderr, PHPTURD " [%s] %s [unmodified]\n",
				  func, path );
		}
		goto no_prefix;
	}

	/* Calculate maximum result path length */
	max_len = readonly_len;
	if ( max_len < writable_len )
		max_len = writable_len;
	suffix_len = strlen ( suffix );
	max_len += suffix_len;

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
		memcpy ( result, writable, writable_len );
		memcpy ( ( result + writable_len ), suffix,
			 ( suffix_len + 1 /* NUL */ ) );
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
 err_malformed:
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
 * @v ...		Turdified call arguments
 */
#define turdwrap1( rtype, func, path, ... ) do {			\
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
	turdpath = turdify_path ( path, #func );			\
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
	/* Free turdified path */					\
	free ( turdpath );						\
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
 * @v path2		Path two
 * @v ...		Turdified call arguments
 */
#define turdwrap2( rtype, func, path1, path2, ... ) do {		\
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
	turdpath1 = turdify_path ( path1, #func );			\
	if ( ! turdpath1 ) {						\
		ret = rtype ## _error_return;				\
		goto err_turdpath1;					\
	}								\
									\
	/* Turdify path two */						\
	turdpath2 = turdify_path ( path2, #func );			\
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
	/* Free turdified path two */					\
	free ( turdpath2 );						\
									\
	err_turdpath1:							\
									\
	/* Free turdified path one */					\
	free ( turdpath1 );						\
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
	turdwrap1 ( int, __lxstat, path, ver, turdpath, buf );
}

int __xstat ( int ver, const char *path, struct stat *buf ) {
	turdwrap1 ( int, __xstat, path, ver, turdpath, buf );
}

int access ( const char *path, int mode ) {
	turdwrap1 ( int, access, path, turdpath, mode );
}

int chdir ( const char *path ) {
	turdwrap1 ( int, chdir, path, turdpath );
}

int chmod ( const char *path, mode_t mode ) {
	turdwrap1 ( int, chmod, path, turdpath, mode );
}

int chown ( const char *path, uid_t owner, gid_t group ) {
	turdwrap1 ( int, chown, path, turdpath, owner, group );
}

int creat ( const char *path, mode_t mode ) {
	turdwrap1 ( int, creat, path, turdpath, mode );
}

FILE * fopen ( const char *path, const char *mode ) {
	turdwrap1 ( FILE_ptr, fopen, path, turdpath, mode );
}

int getfilecon ( const char *path, security_context_t *con ) {
	turdwrap1 ( int, getfilecon, path, turdpath, con );
}

ssize_t getxattr ( const char *path, const char *name, void *value,
		   size_t size ) {
	turdwrap1 ( ssize_t, getxattr, path, turdpath, name, value, size );
}

int lchown ( const char *path, uid_t owner, gid_t group ) {
	turdwrap1 ( int, lchown, path, turdpath, owner, group );
}

int lgetfilecon ( const char *path, security_context_t *con ) {
	turdwrap1 ( int, lgetfilecon, path, turdpath, con );
}

ssize_t lgetxattr ( const char *path, const char *name, void *value,
		    size_t size ) {
	turdwrap1 ( ssize_t, lgetxattr, path, turdpath, name, value, size );
}

int link ( const char *path1, const char *path2 ) {
	turdwrap2 ( int, link, path1, path2, turdpath1, turdpath2 );
}

ssize_t listxattr ( const char *path, char *list, size_t size ) {
	turdwrap1 ( ssize_t, listxattr, path, turdpath, list, size );
}

ssize_t llistxattr ( const char *path, char *list, size_t size ) {
	turdwrap1 ( ssize_t, llistxattr, path, turdpath, list, size );
}

int lremovexattr ( const char *path, const char *name ) {
	turdwrap1 ( int, lremovexattr, path, turdpath, name );
}

int lsetxattr ( const char *path, const char *name, const void *value,
		size_t size, int flags ) {
	turdwrap1 ( int, lsetxattr, path, turdpath, name, value, size, flags );
}

int lstat ( const char *path, struct stat *statbuf ) {
	turdwrap1 ( int, lstat, path, turdpath, statbuf );
}

int mkdir ( const char *path, mode_t mode ) {
	turdwrap1 ( int, mkdir, path, turdpath, mode );
}

int open ( const char *path, int flags, mode_t mode ) {
	turdwrap1 ( int, open, path, turdpath, flags, mode );
}

ssize_t readlink ( const char *path, char *buf, size_t bufsiz ) {
	turdwrap1 ( ssize_t, readlink, path, turdpath, buf, bufsiz );
}

int removexattr ( const char *path, const char *name ) {
	turdwrap1 ( int, removexattr, path, turdpath, name );
}

int rename ( const char *path1, const char *path2 ) {
	turdwrap2 ( int, rename, path1, path2, turdpath1, turdpath2 );
}

int rmdir ( const char *path ) {
	turdwrap1 ( int, rmdir, path, turdpath );
}

int setxattr ( const char *path, const char *name, const void *value,
	       size_t size, int flags ) {
	turdwrap1 ( int, setxattr, path, turdpath, name, value, size, flags );
}

int stat ( const char *path, struct stat *statbuf ) {
	turdwrap1 ( int, stat, path, turdpath, statbuf );
}

int symlink ( const char *path1, const char *path2 ) {
	turdwrap2 ( int, symlink, path1, path2, turdpath1, turdpath2 );
}

int truncate ( const char *path, off_t length ) {
	turdwrap1 ( int, truncate, path, turdpath, length );
}

int unlink ( const char * path ) {
	turdwrap1 ( int, unlink, path, turdpath );
}

int utime ( const char *path, const struct utimbuf *times ) {
	turdwrap1 ( int, utime, path, turdpath, times );
}

int utimes ( const char *path, const struct timeval times[2] ) {
	turdwrap1 ( int, utimes, path, turdpath, times );
}
