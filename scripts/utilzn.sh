#!/bin/bash

if [[ $# -lt 1 ]]; then
    echo -e "Usage: ${0} [40/104/168/232/360/488/744/1000]"
    exit -1
fi

valsize=$1

clientnum="1 2 4 8 12 16 24 32 64"
# clientnum="1"
BUILD_DIR=/home/lyp/pmraccess/build
DATA_DIR=/data/nvme0

export TCMALLOC_LARGE_ALLOC_REPORT_THRESHOLD=10737418240

echo valsize
for cn in $clientnum; do
    echo $cn
    # # run server locally
    # ${BUILD_DIR}/server 2>>${BUILD_DIR}/server.log &
    
    # # run benchmark at another machine
    # sshpass -p "lyp_123456789" ssh lyp@192.168.2.2 \
    #    "${BUILD_DIR}/benchmark -c ${cn} -v ${valsize}" 2>>${BUILD_DIR}/client.log
    
    # clear files and server thread
    # rm -r ${DATA_DIR}/cuckoodb
    # ps aux | grep ${BUILD_DIR}/server | awk '/grep/{next}{print $2}' | xargs kill -9
done