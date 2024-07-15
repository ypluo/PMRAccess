##!/bin/bash

PAYLOAD="40 104 232 488 1000 2024 4072"
cn=1

for size in $PAYLOAD; do
    echo $size

    /home/lyp/pmraccess/build/server -f pmraccess -d cuckoodb 2>>/home/lyp/pmraccess/build/server.log &
    sleep 5 # wait for the server to recover from initial database
    
    # run benchmark at another machine
    sshpass -p "lyp_123456789" ssh lyp@192.168.2.2 \
        "/home/lyp/redisbench -f pmraccess -o 8000000 -v ${size} -c ${cn} -l 1" 2>>/home/lyp/pmraccess/build/client.log

    # clear files and server thread
    ps aux | grep /home/lyp/pmraccess/build/server | awk '/grep/{next}{print $2}' | xargs kill -9
    rm /data/nvme0/cuckoodb -r
    sleep 2
done