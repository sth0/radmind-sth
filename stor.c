#include <sys/types.h>
#include <sys/stat.h>
#ifdef __APPLE__
#include <sys/paths.h>
#include <sys/attr.h>
#endif __APPLE__
#include <sys/uio.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>

#include <snet.h>
#include <sha.h>

#include "connect.h"
#include "cksum.h"
#include "applefile.h"
#include "base64.h"
#include "code.h"

extern struct timeval	timeout;
extern struct as_header as_header;
extern struct attrlist  alist;
extern int		verbose;
extern int		quiet;
extern int		dodots;
extern int		cksum;
extern int		linenum;
extern void            (*logger)( char * );

    int
n_stor_file( SNET *sn, char *filename, char *transcript )
{
    struct timeval      tv;
    char                *line;

    if ( snet_writef( sn,
                "STOR FILE %s %s\r\n", transcript, filename ) == NULL ) {
            perror( "snet_writef" );
            return( -1 );
    }

    if ( verbose ) {
        printf( ">>> STOR FILE %s %s\n", transcript, filename );
    }

    tv.tv_sec = 120;
    tv.tv_usec = 0;
    if (( line = snet_getline_multi( sn, logger, &tv )) == NULL ) {
        perror( "snet_getline_multi" );
        return( -1 );
    }
    if ( *line != '3' ) {
        fprintf( stderr, "%s\n", line );
        return( -1 );
    }

    if ( snet_writef( sn, "0\r\n.\r\n" ) == NULL ) {
        perror( "snet_writef" );
        return( -1 );
    }
    if ( verbose ) fputs( ">>> 0\n\n>>> .\n", stdout );

    tv.tv_sec = 120;
    tv.tv_usec = 0;
    if (( line = snet_getline_multi( sn, logger, &tv )) == NULL ) {
        perror( "snet_getline_multi" );
        return( -1 );
    }
    if ( *line != '2' ) {
        fprintf( stderr, "%s\n", line );
        return( -1 );
    }

    if ( !quiet && !verbose ) {
        printf( "%s: stored as zero length file\n", decode( filename ));
    }
    return( 0 );
}

    int 
stor_file( int fd, SNET *sn, char *filename, char *cksumval, char *transcript,
    char *filetype, size_t size )
{
    struct stat         st;
    struct timeval      tv;
    unsigned char       buf[ 8192 ];
    ssize_t             rr;
    char                *line;
    unsigned char       md[ SHA_DIGEST_LENGTH ];
    unsigned char       mde[ SZ_BASE64_E( sizeof( md )) ];
    SHA_CTX             sha_ctx;

    if ( cksum ) {
        if ( strcmp( cksumval, "-" ) == 0 ) {
            return( -3 );
        }
        SHA1_Init( &sha_ctx );
    }

    /* STOR "TRANSCRIPT" <transcript-name>  "\r\n" */
    if ( filename == NULL ) {
        filename = transcript;
        if ( snet_writef( sn,
                "STOR TRANSCRIPT %s\r\n", transcript ) == NULL ) {
            perror( "snet_writef" );
            return( -1 );
        }
        if ( verbose ) {
            printf( ">>> STOR TRANSCRIPT %s\n", transcript );
        }
    } else {  /* STOR "FILE" <transcript-name> <path> "\r\n" */
        if ( snet_writef( sn,
                "STOR FILE %s %s\r\n", transcript, filename ) == NULL ) {
            perror( "snet_writef" );
            return( -1 );
        }
        if ( verbose ) {
            printf( ">>> STOR FILE %s %s\n", transcript, filename );
        }
    }

    tv = timeout;
    if (( line = snet_getline_multi( sn, logger, &tv )) == NULL ) {
        perror( "snet_getline_multi" );
        return( -1 );
    }
    if ( *line != '3' ) {
        fprintf( stderr, "%s\n", line );
        return( -1 );
    }

    switch( *filetype ) {
    case 'a':
        if ( stor_applefile( fd, sn, decode( filename ), &sha_ctx, size )
		!= 0 ) {
            return( -1 );
        }
        break;
    case 'f':
	if ( fstat( fd, &st) < 0 ) {
	    perror( filename );
	    return( -1 );
	}

	if ( size != 0 ) {
	    if ( st.st_size != size ) {
		fprintf( stderr,
		    "%s: size in transcript does not match size of file\n",
		    decode( filename ));
		return( -1 );
	    }
	}
        if ( snet_writef( sn, "%d\r\n", (int)st.st_size ) == NULL ) {
            perror( "snet_writef" );
            return( -1 );
        }
        if ( verbose ) printf( ">>> %d\n", (int)st.st_size );

        while (( rr = read( fd, buf, sizeof( buf ))) > 0 ) {
            tv = timeout;
            if ( snet_write( sn, buf, (int)rr, &tv ) != rr ) {
                perror( "snet_write" );
                return( -1 );
	    }
            if ( dodots ) { putc( '.', stdout ); fflush( stdout ); }
	    if ( cksum ) {
		SHA1_Update( &sha_ctx, buf, (size_t)rr );
            }
        }
	if ( rr < 0 ) {
	    perror( filename );
	    return( -1 );
	}
        break;
    default:
        fprintf( stderr, "%c: unknown file type\n", *filetype );
        return( -1 );
    }

    /* cksum data sent */
    if ( cksum ) {
        SHA1_Final( md, &sha_ctx );
        base64_e( md, sizeof( md ), mde );
        if ( strcmp( cksumval, mde ) != 0 ) {
            return( -2 );
        }
    }

    if ( snet_writef( sn, ".\r\n" ) == NULL ) {
        perror( "snet_writef" );
        return( -1 );
    }
    if ( verbose ) fputs( "\n>>> .\n", stdout );

    tv = timeout;
    if (( line = snet_getline_multi( sn, logger, &tv )) == NULL ) {
        perror( "snet_getline_multi" );
        return( -1 );
    }
    if ( *line != '2' ) {
        fprintf( stderr, "%s\n", line );
        return( -1 );
    }

    if ( !quiet && !verbose ) printf( "%s: stored\n", decode( filename ));
    return( 0 );
}

#ifdef __APPLE__
    int    
stor_applefile( int dfd, SNET *sn, char *filename, SHA_CTX *sha_ctx,
    size_t size )
{
    int		    	    rfd, rc;
    size_t		    asingle_size = 0;
    char	    	    rsrc_path[ MAXPATHLEN ];
    char		    buf[ 8192 ];
    static char		    null_buf[ 32 ] = { 0 };
    struct timeval	    tv;
    struct stat		    r_stp;	    /* for rsrc fork */
    struct stat		    d_stp;	    /* for data fork */
    static struct as_entry  ae_ents[ 3 ] = {	{ ASEID_FINFO, 62, 32 },
						{ ASEID_RFORK, 94, 0 },
						{ ASEID_DFORK, 0, 0 }
					    };
    struct {
	unsigned long	fs_ssize;
	char		fs_fi[ 32 ];
    } fi_struct;

    /* must check for finder info here first */
    if ( getattrlist( filename, &alist, &fi_struct, sizeof( fi_struct ),
		FSOPT_NOFOLLOW ) != 0 ) {
	fprintf( stderr, "Non-HFS+ filesystem\n" );
	goto error1;
    }

    if ( memcmp( fi_struct.fs_fi, null_buf, sizeof( null_buf )) == 0 ) {
	goto error1;
    }

    if ( fstat( dfd, &d_stp ) != 0 ) {
	perror( filename );
	goto error1;
    }

    if ( snprintf( rsrc_path, MAXPATHLEN, "%s%s", filename, _PATH_RSRCFORKSPEC )
		> MAXPATHLEN ) {
	fprintf( stderr, "%s%s: path too long\n", filename,
		_PATH_RSRCFORKSPEC );
	goto error1;
    }

    if (( rfd = open( rsrc_path, O_RDONLY )) < 0 ) {
	perror( rsrc_path );
	goto error1;
    }

    if 	( fstat( rfd, &r_stp ) != 0 ) {
	/* if there's no rsrc fork, but there is finder info,
	 * assume zero length rsrc fork.
	 */
	if ( errno == ENOENT ) {
	    ae_ents[ AS_RFE ].ae_length = 0;
    	} else {
	    perror( rsrc_path );
	    goto error2;
	}
    } else {
    	ae_ents[ AS_RFE ].ae_length = ( int )r_stp.st_size;
    }

    ae_ents[ AS_DFE ].ae_offset = 
	( ae_ents[ AS_RFE ].ae_offset + ae_ents[ AS_RFE ].ae_length );
    ae_ents[ AS_DFE ].ae_length = ( int )d_stp.st_size;

    /* calculate total applesingle file size */
    asingle_size = ( AS_HEADERLEN + ( 3 * sizeof( struct as_entry ))
		+ FINFOLEN + ae_ents[ AS_RFE ].ae_length
		+ ae_ents[ AS_DFE ].ae_length );

    /* Check size listed in transcript */
    if ( size != 0 ) {
	if ( asingle_size != size ) {
	    fprintf( stderr,
		"%s: size in transcript does not match size of file\n",
		decode( filename ));
	    return( -1 );
	}
    }

    /* tell server how much data to expect */
    tv = timeout;
    if ( snet_writef( sn, "%d\r\n", ( int )asingle_size ) == NULL ) {
        perror( "snet_writef" );
        goto error2;
    }
    if ( verbose ) printf( ">>> %d\n", ( int )asingle_size );

    /* snet write applesingle header to server */
    tv = timeout;
    if ( snet_write( sn, ( char * )&as_header, AS_HEADERLEN, &tv ) !=
		AS_HEADERLEN  ) {
	perror( "snet_write" );
	goto error2;
    }
    if ( cksum ) {
	SHA1_Update( sha_ctx, ( char * )&as_header, AS_HEADERLEN );
    }
    if ( dodots ) { putc( '.', stdout ); fflush( stdout ); }

    /* snet write header entries */
    tv = timeout;
    if ( snet_write( sn, ( char * )&ae_ents,
		( 3 * sizeof( struct as_entry )), &tv )
		!= ( 3 * sizeof( struct as_entry ))) {
	perror( "snet_write" );
	goto error2;
    }
    if ( cksum ) {
        SHA1_Update( sha_ctx, ( char * )&ae_ents,
	    ( 3 * sizeof( struct as_entry )));
    }
    if ( dodots ) { putc( '.', stdout ); fflush( stdout ); }

    /* snet_write finder info data to server */
    tv = timeout;
    if ( snet_write( sn, fi_struct.fs_fi, sizeof( fi_struct.fs_fi ), &tv ) !=
		sizeof( fi_struct.fs_fi )) {
	perror( "snet_write" );
	goto error2;
    }
    if ( cksum ) {
        SHA1_Update( sha_ctx, fi_struct.fs_fi, sizeof( fi_struct.fs_fi ));
    }
    if ( dodots ) { putc( '.', stdout ); fflush( stdout ); }

    /* snet_write rsrc fork data to server */
    if ( rfd >= 0 ) {
	while (( rc = read( rfd, buf, sizeof( buf ))) > 0 ) {
	    tv = timeout;
	    if ( snet_write( sn, buf, rc, &tv ) != rc ) {
		perror( "snet_write" );
		goto error2;
	    }
	    if ( cksum ) {
		SHA1_Update( sha_ctx, buf, (size_t)rc );
	    } 
	    if ( dodots ) { putc( '.', stdout ); fflush( stdout ); }
	}
	if ( close( rfd ) < 0 ) {
	    perror( "close rfd" );
	    exit( 1 );
	}
    }

    /* snet_write data fork to server */
    while (( rc = read( dfd, buf, sizeof( buf ))) > 0 ) {
	tv = timeout;
	if ( snet_write( sn, buf, rc, &tv ) != rc ) {
	    perror( "snet_write" );
	    goto error2;
	}
	if ( cksum ) {
	    SHA1_Update( sha_ctx, buf, (size_t)rc );
	}
    	if ( dodots ) { putc( '.', stdout ); fflush( stdout ); }
    }
    /* dfd is closed in main() of lcreate.c */

    return( 0 ); 

error1:
    return( -1 );
error2:
    if ( close( rfd ) < 0 ) {
	perror( rsrc_path );
	exit( 1 );
    }
    return( -1 );
}
#else !__APPLE__

#include <sys/types.h>
#include <stdio.h>


#include "connect.h"

    int
stor_applefile( int dfd, SNET *sn, char *filename, SHA_CTX *sha_ctx,
    size_t size )
{
    return( -1 );
}
#endif __APPLE__
