#!/bin/sh
#
# Radmind Assistant shell script (rash)
# Basic client side functions include:
#
#	trip
#	update
#	create
#	auto
#	force
#

# Command line options:
# -c  without checksums

# Config file stuff:
#
# server
# -w # -x, -y, -z
# lcreate -l -U
# -c sha1

SERVER="-h _RADMIND_HOST"
AUTHLEVEL="-w _RADMIND_AUTHLEVEL"
EDITOR=${EDITOR:-vi}
DEFAULTS="/etc/radmind.defaults"
FSDIFFROOT="."

PATH=/usr/local/bin:/usr/bin:/bin; export PATH
RETRY=10

TEMPFILES=FALSE
TMPDIR="/tmp/.ra.$$"
LTMP="${TMPDIR}/lapply.out"
FTMP="${TMPDIR}/fsdiff.out"

PREAPPLY="/var/radmind/preapply"
POSTAPPLY="/var/radmind/postapply"

Yn() {
    echo -n "$*" "[Yn] "
    read ans
    if [ -z "$ans" -o X"$ans" = Xy -o X"$ans" = XY -o X"$ans" = Xyes ]; then
	return 1
    fi
    return 0
}

usage() {
    echo "Usage:	$0 [ -ct | -h server | -w authlevel ] { trip | update | create | auto | force }" >&2
    exit 1
}

cleanup() {
    if [ "$TEMPFILES" = FALSE ]; then
	rm -fr $TMPDIR
    fi
}

dopreapply() {
    for script in ${PREAPPLY}/*; do
	${script} "$1"
    done
}

dopostapply() {
    for script in ${POSTAPPLY}/*; do
	${script} "$1"
    done
}

# if a radmind defaults file exists, source it.
# options in the defaults file can be overridden
# with the options below.
if [ -f "${DEFAULTS}" ]; then
    . "${DEFAULTS}"
fi

while getopts %ch:ltw: opt; do
    case $opt in
    %)  PROGRESS="-%"
	;;

    c)	CHECKSUM="-csha1"
	;;

    h)	SERVER="-h $OPTARG"
    	;;

    l)  USERAUTH="-l"
	;;

    t)	TEMPFILES="TRUE"
    	;;

    w)	AUTHLEVEL="-w $OPTARG"
    	;;

    *)  usage
	;;
    esac
done
shift `expr $OPTIND - 1`

if [ $# -ne 1 ]; then
	usage
fi

cd /

if ! mkdir -m 700 ${TMPDIR} ; then
    echo "Cannot create temporary directory $TMPDIR"
    exit 1
fi

# http://www.opengroup.org/onlinepubs/009695399/basedefs/signal.h.html
# The following signals shall be supported on all implementations:
# unknown: SIGPOLL 
trap cleanup SIGABRT SIGALRM SIGBUS SIGCHLD SIGCONT SIGFPE \
	     SIGHUP SIGILL SIGINT SIGKILL SIGPIPE \
	     SIGPROF SIGQUIT SIGSEGV SIGSTOP SIGSYS SIGTERM \
	     SIGTRAP SIGTSTP SIGTTIN SIGTTOU SIGURG SIGUSR1 \
	     SIGUSR2 SIGVTALRM SIGXCPU SIGXFSZ 

case "$1" in
update)
    ktcheck ${AUTHLEVEL} ${SERVER} -n -c sha1
    case "$?" in
    0)	;;

    1)	Yn "Update command file and/or transcripts?"
	    if [ $? -eq 1 ]; then
		ktcheck ${AUTHLEVEL} ${SERVER} -c sha1
		RC=$?
		if [ $RC -ne 1 ]; then
		    echo Nothing to update
		    cleanup
		    exit $RC
		fi
	    fi
	;;

    *)	cleanup
    	exit $?
	;;
    esac

    fsdiff -A -% ${CHECKSUM} -o ${FTMP} ${FSDIFFROOT}
    if [ $? -ne 0 ]; then
	cleanup
	exit 1
    fi

    if [ ! -s ${FTMP} ]; then
	echo Nothing to apply.
	cleanup
	exit 1
    fi
    Yn "Edit difference transcript?"
    if [ $? -eq 1 ]; then
	${EDITOR} ${FTMP}
    fi
    if [ -d "${PREAPPLY}" ]; then
	Yn "Run pre-apply scripts on difference transcript?"
	if [ $? -eq 1 ]; then
	    dopreapply ${FTMP}
	fi
    fi
    Yn "Apply difference transcript?"
    if [ $? -eq 1 ]; then
	lapply ${PROGRESS} ${AUTHLEVEL} ${SERVER} ${CHECKSUM} ${FTMP}
	case "$?" in
	0)	;;

	*)	cleanup
		exit $?
		;;
	esac
    fi
    if [ -d "${POSTAPPLY}" ]; then
	Yn "Run post-apply scripts on difference transcript?"
	if [ $? -eq 1 ]; then
	    dopostapply ${FMTP}
	fi
    fi
    cleanup
    ;;

create)
    ktcheck ${AUTHLEVEL} ${SERVER} -n -c sha1
    case "$?" in
    0)	;;

    1)	Yn "Update command file and/or transcripts?"
	if [ $? -eq 1 ]; then
	    ktcheck ${AUTHLEVEL} ${SERVER} -c sha1
	    rc = $?
	    if [ $rc -ne 0 ]; then
		cleanup
		exit $rc
	    fi
	fi
	;;

    *)	cleanup
   	exit $?
    	;;
    esac
    echo -n "Enter new transcript name: "
    read TNAME
    if [ -z "${TNAME}" ]; then
	echo "No transcript name provided!"
	cleanup
	exit 1
    fi
    FTMP="${TMPDIR}/${TNAME}"
    fsdiff -C -% ${CHECKSUM} -o ${FTMP} ${FSDIFFROOT}
    if [ $? -ne 0 ]; then
	cleanup
	exit 1;
    fi
    if [ ! -s ${FTMP} ]; then
	echo Nothing to create.
	cleanup
	exit 1
    fi
    Yn "Edit transcript?"
    if [ $? -eq 1 ]; then
	${EDITOR} ${FTMP}
    fi
    Yn "Store loadset ${TNAME}?"
    if [ $? -eq 1 ]; then
	if [ -n "${USERAUTH}" ]; then
	    echo -n "username: "
	    read USERNAME
	    USERNAME="-U ${USERNAME}"
	fi
	lcreate ${PROGRESS} ${AUTHLEVEL} ${USERAUTH} ${USERNAME} \
			${CHECKSUM} ${SERVER} ${FTMP}
	if [ $? -ne 0 ]; then
	    cleanup
	    exit 1
	fi
    fi
    cleanup
    ;;

trip)
    ktcheck ${AUTHLEVEL} ${SERVER} -qn -c sha1
    case "$?" in
    0)
	;;
    1)
	echo Command file and/or transcripts are out of date.
	;;
    *)
	cleanup
	exit $?
	;;
    esac

    fsdiff -C ${CHECKSUM} -o ${FTMP} ${FSDIFFROOT}
    if [ $? -ne 0 ]; then
	cleanup
	exit 1
    fi
    if [ -s ${FTMP} ]; then
	echo Trip failure: `hostname`
	cat ${FTMP}
	cleanup
	exit 0
    fi
    ;;

auto)
    fsdiff -C ${CHECKSUM} -o ${FTMP} ${FSDIFFROOT}
    if [ $? -ne 0 ]; then
	echo Auto failure: `hostname` fsdiff
	cleanup
	exit 1
    fi
    if [ -s ${FTMP} ]; then
	echo Auto failure: `hostname` trip
	cat ${FTMP}
	cleanup
	exit 1
    fi

    # XXX - if this fails, do we loop, or just report error?
    ktcheck ${AUTHLEVEL} ${SERVER} -q -c sha1
    if [ $? -eq 1 ]; then
	while true; do
	    fsdiff -A ${CHECKSUM} -o ${FTMP} ${FSDIFFROOT}
	    if [ $? -ne 0 ]; then
		echo Auto failure: `hostname`: fsdiff
		cleanup
		exit 1
	    fi
	    if [ -s ${FTMP} ]; then
		lapply ${AUTHLEVEL} ${SERVER} -q ${CHECKSUM} \
			${FTMP} 2>&1 > ${LTMP}
		case $? in
		0)
		    echo Auto update: `hostname`
		    cat ${FTMP}
		    cleanup
		    break
		    ;;

		*)
		    if [ ${RETRY} -gt 10000 ]; then
			echo Auto failure: `hostname`
			cat ${LTMP}
			cleanup
			exit 1
		    fi
		    echo Auto failure: `hostname` retrying
		    cat ${LTMP}
		    sleep ${RETRY}
		    RETRY=${RETRY}0
		    ktcheck ${AUTHLEVEL} ${SERVER} -q -c sha1
		    ;;
		esac
	    fi
	done
    fi
    ;;

force)
    ktcheck ${AUTHLEVEL} ${SERVER} -c sha1
    case "$?" in
    0)	;;
    1)	;;

    *)	cleanup
    	exit $?
	;;
    esac

    fsdiff -A -% ${CHECKSUM} -o ${FTMP} ${FSDIFFROOT}
    if [ $? -ne 0 ]; then
	cleanup
	exit 1
    fi

    if [ ! -s ${FTMP} ]; then
	echo Nothing to apply.
	cleanup
	exit 0
    fi
    
    dopreapply ${FTMP}
    lapply ${PROGRESS} ${AUTHLEVEL} ${SERVER} ${CHECKSUM} ${FTMP}
    case "$?" in
    0)	;;

    *)	cleanup
	    exit $?
	    ;;
    esac
    dopostapply ${FTMP}
    
    cleanup
    ;;

*)
    usage
    ;;

esac

cleanup
exit 0
