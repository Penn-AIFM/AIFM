The goal of this experiment is to show bitmap test throughput under different
request skews, using a setup equivalent to fig9.

The bitmap is defined at "aifm/inc/bitmap.hpp", with implementation in
"aifm/src/bitmap.cpp" and server-side compute handlers in
"aifm/src/server_bitmap.cpp".

The "run.sh" scripts in subfolders sweep the zipf skew parameter and print
application throughput (in MOPS). The results are a set of {log.X} files.
For example, log.0 contains the throughput when zipf parameter is 0.
