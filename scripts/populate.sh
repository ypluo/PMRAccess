#!/bin/bash

BUILD_DIR=/home/lyp/pmraccess/build
DATA_DIR=/data/nvme0

rm -r ${DATA_DIR}/leveldb
rm -r ${DATA_DIR}/cuckoodb

cp ${BUILD_DIR}/../workload/dataset.dat .

${BUILD_DIR}/server -f sync -d leveldb 2>>${BUILD_DIR}/server.log &
sleep 1
${BUILD_DIR}/benchmark -f sync -c 8 2>>${BUILD_DIR}/client.log
ps aux | grep ${BUILD_DIR}/server | awk '/grep/{next}{print $2}' | xargs kill -9

sleep 1

${BUILD_DIR}/server -f sync -d cuckoodb 2>>${BUILD_DIR}/server.log &
sleep 1
${BUILD_DIR}/benchmark -f sync -c 8 2>>${BUILD_DIR}/client.log
ps aux | grep ${BUILD_DIR}/server | awk '/grep/{next}{print $2}' | xargs kill -9