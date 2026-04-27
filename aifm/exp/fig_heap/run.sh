#!/bin/bash

source ../../shared.sh

N_arr=(65536 262144 1048576 4194304 16777216)

sudo pkill -9 main

pushd aifm
for N in "${N_arr[@]}"; do
    sed "s/constexpr uint64_t kN .*/constexpr uint64_t kN = ${N}ULL;/g" main.cpp -i
    make clean
    make -j
    rerun_local_iokerneld
    rerun_mem_server
    run_program ./main | tee ../log.aifm.$N
done
popd

pushd local_only
make clean
make -j
for N in "${N_arr[@]}"; do
    sudo taskset -c 1 ./main $N | tee ../log.local.$N
done
popd
