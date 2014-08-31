#!/bin/sh

for CRAWL_DIR in ../../*
do
    RCDIR="${CRAWL_DIR}/source/rcs"
    INPROGRESSDIR="${RCDIR}/running"
    TTYRECDIR="${RCDIR}/ttyrecs"
    DEFAULT_RC="${CRAWL_DIR}/settings/init.txt"
    PLAYERNAME="$1"

    mkdir -p "${RCDIR}/${PLAYERNAME}"
    mkdir -p "${RCDIR}/${PLAYERNAME}.macro"
    if [ $(basename "${CRAWL_DIR}") != "crawl-ref" -a -e "rcs/${PLAYERNAME}.macro/macro.txt" ] ; then
	cp "rcs/${PLAYERNAME}.macro/macro.txt" "${RCDIR}/${PLAYERNAME}.macro/"
    fi
    if [ ! -d "${INPROGRESSDIR}" ] ; then
        mkdir -p "${INPROGRESSDIR}"
    fi
    if [ ! -d "${TTYRECDIR}/${PLAYERNAME}" ] ; then
        mkdir -p "${TTYRECDIR}/${PLAYERNAME}"
    fi
    if [ ! -f "${RCDIR}/${PLAYERNAME}.rc" ]; then
        cp "${DEFAULT_RC}" "${RCDIR}/${PLAYERNAME}.rc"
    fi
done
