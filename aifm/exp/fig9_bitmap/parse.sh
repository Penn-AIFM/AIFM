#!/bin/bash

echo "backend,zipf_s,mops" > summary

for f in aifm/log.* linux_mem/log.*; do
    [ -e "$f" ] || continue
    backend=$(echo "$f" | awk -F/ '{print $1}')
    zipf_s=$(basename "$f" | awk -F. '{print $2}')
    mops=$(awk '/^mops = / { val = $3 } END { if (val != "") print val }' "$f")
    if [ -n "$mops" ]; then
        echo "${backend},${zipf_s},${mops}"
    fi
done | sort -t, -k1,1 -k2,2g >> summary

cat summary
