#!/bin/bash

./YCSB-Gen-prefix/src/YCSB-Gen-build/ycsbc -P ./YCSB-Gen-prefix/src/YCSB-Gen/workloads/insert.spec
# initial datasets
mv dataset.dat ../workload/dataset.dat
# Load only
mv query.dat ../workload/query1.dat

# YCSB-A
./YCSB-Gen-prefix/src/YCSB-Gen-build/ycsbc -P ./YCSB-Gen-prefix/src/YCSB-Gen/workloads/workloada.spec --queryonly
mv query.dat ../workload/query2.dat

# YCSB-B
./YCSB-Gen-prefix/src/YCSB-Gen-build/ycsbc -P ./YCSB-Gen-prefix/src/YCSB-Gen/workloads/workloadb.spec --queryonly
mv query.dat ../workload/query3.dat