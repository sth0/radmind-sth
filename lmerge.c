#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <netdb.h>
#include <sha.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/mkdev.h>
#include <sys/ddi.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <errno.h>
#include <strings.h>

#include "argcargv.h"
#include "lmerge.h"
#include "pathcmp.h"
#include "mkdirs.h"

int		chksum = 1;
int		verbose = 0;

    int
getnextline( struct tran *tran )
{
    int len;

    if ( fgets( tran->tline, MAXPATHLEN, tran->fs ) == NULL ) {
	tran->eof = 1;
	return( 0 );
    }

    if ( tran->line != NULL ) {
	free( tran->line );
	tran->line = NULL;
    }

    if ( ( tran->line = strdup( tran->tline ) ) == NULL ) {
	perror( tran->tline );
	return( -1 );
    }

    /* Check line length */
    len = strlen( tran->tline );
    if ( ( tran->tline[ len - 1 ] ) != '\n' ) {
	fprintf( stderr, "%s: line too long\n", tran->tline );
	return( -1 );
    }
    if ( ( tran->tac = acav_parse( tran->acav,
	    tran->tline, &(tran->targv) )  ) < 0 ) {
	fprintf( stderr, "acav_parse\n" );
	return( -1 );
    }
    return( 0 );
}

/*
 * exit codes:
 *	0  	okay	
 *	1	System error
 */

    int
main( int argc, char **argv )
{
    int			c, i, j, cmpval, err = 0, tcount = 0, canidate = NULL;
    int			ofd;
int	ccount = 0;
    char		*tname = NULL, *version = "1.0", *file = NULL;
    char		*tpath = NULL;
    char		npath[ 2 * MAXPATHLEN ];
    char		opath[ 2 * MAXPATHLEN ];
    struct tran		**trans = NULL;
    FILE		*ofs;

    while ( ( c = getopt( argc, argv, "Vv" ) ) != EOF ) {
	switch( c ) {
	case 'V':
	    printf( "%s\n", version );
	    exit( 0 );
	case 'v':
	    verbose = 1;
	    break;
	default:
	    err++;
	    break;
	}
    }

    if ( err || ( ( argc - optind ) < 2 ) ) {
	fprintf( stderr, "usage: lmerge [ -vV ] " );
	fprintf( stderr, "transcript1, transcript 2, ..., dest\n" );
	exit( 2 );
    }

    tcount = argc - optind - 1;
    tpath = argv[ argc - 1 ];

    /* Create array of transcripts */
    if ( ( trans = (struct tran**)malloc(
	    sizeof( struct tran* ) * ( tcount ) ) ) == NULL ) {
	perror( "malloc" );
	exit( 1 );
    }
    for ( i = optind - 1; i + 2 < argc; i++ ) {

	if ( ( trans[ i ] = (struct tran*)malloc( sizeof( struct tran ) ) )
		== NULL ) {
	    perror( "malloc" );
	    return( 1 );
	}
	trans[ i ]->num = argc - i;
	trans[ i ]->eof = 0;
	if ( ( trans[ i ]->fs = fopen( argv[ i + 1 ], "r" ) ) == NULL ) {
	    perror( argv[ i +1 ]);
	    return( 1 );
	}

	/* Get transcript name from path */
	trans[ i ]->path = argv[ i + 1 ];
	if ( ( trans[ i ]->name = strrchr( trans[ i ]->path, '/' ) ) == NULL ) {
	    trans[ i ]->name = trans[ i ]->path;
	    trans[ i ]->path = ".";
	} else {
	    *trans[ i ]->name = (char)'\0';
	    trans[ i ]->name++;
	}

	if ( ( trans[ i ]->acav = acav_alloc() ) == NULL ) {
	    fprintf( stderr, "acav_malloc\n" );
	    return( 1 );
	}
	trans[ i ]->line = NULL;
	if ( getnextline( trans[ i ] ) < 0 ) {
	    fprintf( stderr, "getnextline\n" );
	    exit( 1 );
	}
    }

    /* Get new transcript name from transcript path */
    if ( ( tname = strrchr( tpath, '/' ) ) == NULL ) {
	tname = tpath;
	tpath = ".";
    } else {
	*tname = (char)'\0';
	tname++;
    }

    /* Create file/tname dir */
    sprintf( npath, "%s/../file/%s.%d", tpath, tname, (int)getpid() );
    if ( mkdir( npath, 0777 ) != 0 ) {
	perror( npath );
	exit( 1 );
    }

    /* Create transcript/tname file */
    sprintf( opath, "%s/%s.%d", tpath, tname, (int)getpid() );
    if ( ( ofd = open( opath, O_WRONLY | O_CREAT | O_EXCL,
	    0666 ) ) < 0 ) {
	perror( opath );
	exit( 1 );
    }
    if ( ( ofs = fdopen( ofd, "w" ) ) == NULL ) {
	perror( opath );
	exit( 1 );
    }
	
    /* Merge transcripts */
    for ( i = 0; i < tcount; i++ ) {
	while ( !(trans[ i ]->eof) ) {
	    canidate = i;

	    /* Compare canidate to other transcripts */
	    for ( j = i + 1; j < tcount; j++ ) {
		if ( trans[ j ]->eof ) {
		    continue;
		}
ccount++;
		cmpval = pathcmp( trans[ canidate ]->targv[ 1 ],
		    trans[ j ]->targv[ 1 ] );
		if ( cmpval == 0 ) {

		    /* Advance lower precedence transcript */
		    if ( getnextline( trans[ j ] ) < 0 ) {
			fprintf( stderr, "getnextline\n" );
			exit( 1 );
		    }
		} else if ( cmpval > 0 ) {
		    canidate = j;
		}
	    }
	    if ( *trans[ canidate ]->targv[ 0 ] != 'f' ) {
		goto getnext;
	    }
	    /*
	    sprintf( npath, "%s/../file.%d/%s/%s", tpath, (int)getpid(),
		tname, trans[ canidate ]->targv[ 1 ] ); 
	    */

	    /* verify directory structure for link */
	    if ( ( file = strrchr( trans[ canidate ]->targv[ 1 ], '/' ) )
		    != NULL ) {
		*file = (char)'\0';
		sprintf( npath, "%s/../file/%s.%d/%s", tpath,
		    tname, (int)getpid(), trans[ canidate ]->targv[ 1 ] ); 
		if ( create_directories( npath ) != 0 ) {
		    fprintf( stderr, "create_dirs\n" );
		    exit( 1 );
		}
		*file = (char)'/';
	    } 

	    /* Link file */
	    sprintf( npath, "%s/../file/%s.%d/%s", tpath, tname,
		(int)getpid(), trans[ canidate ]->targv[ 1 ] );
	    sprintf( opath,"%s/../file/%s/%s", trans[ canidate ]->path,
		trans[ canidate ]->name, trans[ canidate ]->targv[ 1 ] );
	    if ( link( opath, npath ) != 0 ) {
		perror( npath );
		exit( 1 );
	    }
	    if ( verbose ) printf( "*** linked %s/%s\n",
		tname, trans[ canidate ]->targv[ 1 ]);
		
getnext:
	    if ( fputs( trans[ canidate ]->line, ofs ) == EOF ) {
		perror( trans[ canidate ]->line );
		exit( 1 );
	    }
	    if ( getnextline( trans[ canidate ] ) != 0 ) {
		fprintf( stderr, "getnextline\n" );
		exit( 1 );
	    }
	}
    }

    /* Rename temp transcript and file structure */
    sprintf( opath, "%s/../file/%s.%d", tpath, tname, (int)getpid() );
    sprintf( npath, "%s/../file/%s", tpath, tname );
    if ( rename( opath, npath ) != 0 ) {
	perror( npath );
	exit( 1 );
    }
    sprintf( opath, "%s/%s.%d", tpath, tname, (int)getpid() );
    sprintf( npath, "%s/%s", tpath, tname );
    if ( rename( opath, npath ) != 0 ) {
	perror( npath );
	exit ( 1 );
    }

printf( "ccount: %d\n", ccount );

    exit( 0 );
} 