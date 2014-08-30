#!/bin/sh

start () {
    echo 'server start.'
    python webserver/server.py --daemon
}

stop () {
    PID=$(pgrep -f 'webserver/server.py')
    if [ -n "$PID" ] ; then
        echo "server stopped. (PID $PID)"
        kill -KILL "$PID"
    fi
}

restart() {
    stop
    start
}

if [ $# -eq 0 ] ; then
    echo "Usage: server.sh [start|stop|restart]"
    exit
fi

case "$1" in
    restart) restart ;;
    start) start ;;
    stop) stop ;;

    *) echo "bad command: $1" ;;
esac
