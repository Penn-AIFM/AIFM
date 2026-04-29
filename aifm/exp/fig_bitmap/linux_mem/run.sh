#!/bin/bash

source ../../../shared.sh

zipf_s_arr=(1.2 1.25 1.3 1.35)

sudo pkill -9 main || true
for zip_s in ${zipf_s_arr[@]}
do
    sed "s/constexpr static double kZipfParamS.*/constexpr static double kZipfParamS = $zip_s;/g" main.cpp -i
    make clean
    make -j
    rerun_local_iokerneld
    run_program ./main 1>log.$zip_s 2>&1
done
kill_local_iokerneld || true
