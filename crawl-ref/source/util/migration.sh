#!/bin/sh

# list all registered player name and run script
for name in $(find rcs -name '*.rc' | sed -re 's|rcs/([^.]+).rc|\1|')
do
    ./util/webtiles-init-player.sh "$name"
done
