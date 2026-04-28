#!/bin/bash

echo "backend,N,build_us,drain_us,total_us" > summary

for f in log.aifm.* log.local.*; do
    [ -e "$f" ] || continue
    backend=$(echo "$f" | awk -F. '{print $2}')
    grep -E '^N=[0-9]+ build_us=[0-9]+ drain_us=[0-9]+ total_us=[0-9]+' "$f" | \
        sed -E "s/^N=([0-9]+) build_us=([0-9]+) drain_us=([0-9]+) total_us=([0-9]+).*/${backend},\1,\2,\3,\4/"
done | sort -t, -k1,1 -k2,2n >> summary

cat summary
