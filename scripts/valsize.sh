#!/bin/bash

if [[ $# -lt 3 ]]; then
    echo -e "Usage: ${0} [sync/group/pmraccess] [cuckoodb/leveldb] [0/1]"
    exit -1
fi

fronttype=$1
dbtype=$2
latmode=$3

valsize="40 104 232 488 1000 2024"
BUILD_DIR=/home/lyp/pmraccess/build
DATA_DIR=/data/nvme0

for vs in $valsize; do
    echo $vs

    cp -r ${DATA_DIR}/${dbtype}_0 ${DATA_DIR}/${dbtype}
    sync; echo 1 > /proc/sys/vm/drop_caches

    # run server locally
    cgexec -g memory:lyptest ${BUILD_DIR}/server -f ${fronttype} -d ${dbtype} 2>>${BUILD_DIR}/server.log &
    sleep 5 # wait for the server to recover from initial database
    
    # run benchmark at another machine
    sshpass -p "lyp_123456789" ssh lyp@192.168.2.2 \
        "${BUILD_DIR}/benchmark -f ${fronttype} -l ${latmode} -c 16 -v ${vs}" 2>>${BUILD_DIR}/client.log
    
    # clear files and server thread
    rm ${DATA_DIR}/${dbtype} -r
    ps aux | grep ${BUILD_DIR}/server | awk '/grep/{next}{print $2}' | xargs kill -9
    sleep 1
done