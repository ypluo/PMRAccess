##!/bin/bash

clientnum="1 2 4 8 16 24 32"
PAYLOAD=40

for cn in $clientnum; do
    echo $cn

    /home/lyp/pmraccess/build/server -f pmraccess -d cuckoodb 2>>/home/lyp/pmraccess/build/server.log &
    sleep 5 # wait for the server to recover from initial database
    
    # run benchmark at another machine
    sshpass -p "lyp_123456789" ssh lyp@192.168.2.2 \
        "/home/lyp/redisbench -f pmraccess -o 8000000 -v ${PAYLOAD} -c ${cn}" 2>>/home/lyp/pmraccess/build/client.log

    # clear files and server thread
    ps aux | grep /home/lyp/pmraccess/build/server | awk '/grep/{next}{print $2}' | xargs kill -9
    rm /data/nvme0/cuckoodb -r
    sleep 1
done