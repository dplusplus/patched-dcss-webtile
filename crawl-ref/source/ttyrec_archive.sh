#!/bin/sh

# call as follows:
# % ./ttyrec_archive.sh ./rcs/ttyrecs/

if [ "$1" != "" -a  -d "$1" ] ; then
    for ttyrec in `find $1 -name '*.ttyrec' -print`
    do
	tar jcvf $ttyrec.bz2 $ttyrec && \
	rm $ttyrec
    done
fi
