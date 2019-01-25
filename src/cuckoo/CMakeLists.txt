cmake_minimum_required (VERSION 2.6)
project (cuckoo)


set(CMAKE_CXX_FLAGS "-march=native -std=c++11 -Wall -Wno-format -Wno-deprecated-declarations -D_POSIX_C_SOURCE=200112L -O3 -DPREFETCH -I.  -pthread -DATOMIC -DNSIPHASH=1 -DEDGEBITS=19")

include_directories('./')

add_executable(lean19x1 ../crypto/siphash.hpp cuckoo.h  lean.hpp lean.cpp ../crypto/blake2b-ref.c)