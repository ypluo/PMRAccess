#!/bin/bash

if [[ $# -lt 3 ]]; then
    echo -e "Usage: ${0} [sync/group/pmraccess] [cuckoodb/leveldb] [0/1]"
    exit -1
fi

fronttype=$1
dbtype=$2
latmode=$3

clientnum="1 2 4 6 8 12 16 20 24 28 32"
BUILD_DIR=/home/lyp/pmraccess/build
DATA_DIR=/data/nvme0

export TCMALLOC_LARGE_ALLOC_REPORT_THRESHOLD=10737418240
for cn in $clientnum; do
    echo $cn
    # cp -r ${DATA_DIR}/${dbtype}_0 ${DATA_DIR}/${dbtype}
    
    clear page cache
    sync; echo 1 > /proc/sys/vm/drop_caches
    # run server locally
    ${BUILD_DIR}/server -f ${fronttype} -d ${dbtype} 2>>${BUILD_DIR}/server.log &
    sleep 5 # wait for the server to recover from initial database
    
    # run benchmark at another machine
    sshpass -p "lyp_123456789" ssh lyp@192.168.2.2 \
        "${BUILD_DIR}/benchmark -f ${fronttype} -l ${latmode} -c ${cn}" 2>>${BUILD_DIR}/client.log
    
    # clear files and server thread
    ps aux | grep ${BUILD_DIR}/server | awk '/grep/{next}{print $2}' | xargs kill -9
    rm ${DATA_DIR}/${dbtype} -rf
    sleep 1
done
