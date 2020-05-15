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
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/xattr.h>
#include <selinux/selinux.h>
#include <dlfcn.h>

/** Environment variable name */
#define PHPTURD "PHPTURD"

/** Enable debugging */
#define DEBUG 0

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
 * @ret canonical	Canonical path, or NULL on error
 *
 * The caller is repsonsible for calling free() on the returned path.
 */
static char * canonical_path ( const char *path ) {
	size_t cwd_len;
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
	result = malloc ( cwd_len + 1 /* possible '/' */ + strlen ( path )
			  + 1 /* NUL */ );
	if ( ! result )
		goto err_result;

	/* Construct result path */
	if ( cwd ) {
		strcpy ( result, cwd );
		result[cwd_len] = '/';
		strcpy ( ( result + cwd_len + 1 ), path );
	} else {
		strcpy ( result, path );
	}

	/* Canonicalise result */
	in = out = result;
	while ( 1 ) {

		/* Dump debug information */
		if ( DEBUG >= 2 ) {
			c = *out;
			*out = '\0';
			fprintf ( stderr, PHPTURD " %s => %s ", path, result );
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
 * @ret turdpath	Turdified path
 *
 * The caller is repsonsible for calling free() on the returned path.
 */
static char * turdify_path ( const char *path ) {
	const char *turd;
	const char *readonly;
	const char *writable;
	const char *suffix;
	char *abspath;
	char *result;
	size_t readonly_len;
	size_t writable_len;
	size_t max_len;

	/* Check for and parse PHPTURD environment variable */
	turd = getenv ( PHPTURD );
	if ( ! turd ) {
		result = strdup ( path );
		goto no_turd;
	}
	readonly = turd;
	writable = strchr ( readonly, ':' );
	if ( ! writable ) {
		fprintf ( stderr, PHPTURD " malformed: %s\n", turd );
		result = strdup ( path );
		goto err_malformed;
	}
	readonly_len = ( writable - readonly );
	writable++;
	writable_len = strlen ( writable );

	/* Convert to an absolute path */
	abspath = canonical_path ( path );
	if ( ! abspath ) {
		fprintf ( stderr, PHPTURD " could not canonicalise \"%s\"\n",
			  path );
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
		goto no_prefix;
	}

	/* Calculate maximum result path length */
	max_len = readonly_len;
	if ( max_len < writable_len )
		max_len = writable_len;
	max_len += strlen ( suffix );

	/* Allocate result path */
	result = malloc ( max_len + 1 /* NUL */ );
	if ( ! result )
		goto err_result;

	/* Construct readonly path variant */
	strcpy ( result, readonly );
	strcpy ( ( result + readonly_len ), suffix );

	/* Construct writable path if readonly path does not exist */
	if ( access ( result, F_OK ) != 0 ) {
		strcpy ( result, writable );
		strcpy ( ( result + writable_len ), suffix );
	}

	/* Dump debug information */
	if ( DEBUG >= 1 ) {
		fprintf ( stderr, PHPTURD " %s => %s => %s\n",
			  path, abspath, result );
	}

 err_result:
 no_prefix:
	free ( abspath );
 err_canonical:
 err_malformed:
 no_turd:
	return result;
}

/**
 * Turdify a library call taking a single path parameter
 *
 * @v fund		Library function
 * @v path		Path
 * @v ...		Remaining arguments
 */
#define turdwrap1( func, path, ... ) do {				\
	static typeof ( func ) * orig_ ## func = NULL;			\
	char *turdpath;							\
	long ret;							\
									\
	/* Get original library function */				\
	if ( ! orig_ ## func )						\
		orig_ ## func = dlsym ( RTLD_NEXT, #func );		\
									\
	/* Turdify path */						\
	turdpath = turdify_path ( path );				\
	if ( ! turdpath )						\
		return -1;						\
									\
	/* Call original library function */				\
	ret = orig_ ## func ( __VA_ARGS__ );				\
									\
	/* Free turdified path */					\
	free ( turdpath );						\
									\
	/* Return value from original library function */		\
	return ret;							\
	} while ( 0 )

/*
 *
 * Library function wrappers
 *
 */

int __lxstat ( int ver, const char *path, struct stat *buf ) {
	turdwrap1 ( __lxstat, path, ver, turdpath, buf );
}

int __xstat ( int ver, const char *path, struct stat *buf ) {
	turdwrap1 ( __xstat, path, ver, turdpath, buf );
}

int chdir ( const char *path ) {
	turdwrap1 ( chdir, path, turdpath );
}

int chmod ( const char *path, mode_t mode ) {
	turdwrap1 ( chmod, path, turdpath, mode );
}

int chown ( const char *path, uid_t owner, gid_t group ) {
	turdwrap1 ( chown, path, turdpath, owner, group );
}

int getfilecon ( const char *path, security_context_t *con ) {
	turdwrap1 ( getfilecon, path, turdpath, con );
}

ssize_t getxattr ( const char *path, const char *name, void *value,
		   size_t size ) {
	turdwrap1 ( getxattr, path, turdpath, name, value, size );
}

int lchown ( const char *path, uid_t owner, gid_t group ) {
	turdwrap1 ( lchown, path, turdpath, owner, group );
}

int lgetfilecon ( const char *path, security_context_t *con ) {
	turdwrap1 ( lgetfilecon, path, turdpath, con );
}

ssize_t lgetxattr ( const char *path, const char *name, void *value,
		    size_t size ) {
	turdwrap1 ( lgetxattr, path, turdpath, name, value, size );
}

int lstat ( const char *path, struct stat *statbuf ) {
	turdwrap1 ( lstat, path, turdpath, statbuf );
}

int open ( const char *path, int flags, mode_t mode ) {
	turdwrap1 ( open, path, turdpath, flags, mode );
}

ssize_t readlink ( const char *path, char *buf, size_t bufsiz ) {
	turdwrap1 ( readlink, path, turdpath, buf, bufsiz );
}

int stat ( const char *path, struct stat *statbuf ) {
	turdwrap1 ( stat, path, turdpath, statbuf );
}
