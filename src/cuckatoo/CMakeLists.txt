cmake_minimum_required (VERSION 2.6)
project (cuckatoo)


set(CMAKE_CXX_FLAGS "-g3 -march=native -std=c++11 -Wall -Wno-format -Wno-deprecated-declarations -D_POSIX_C_SOURCE=200112L -DPREFETCH -I. -pthread -DNSIPHASH=1 -DEDGEBITS=12 -DIDXSHIFT=3")

# set(CMAKE_CXX_FLAGS "-O3 -mno-avx2 -march=native -std=c++11 -Wall -Wno-format -Wno-deprecated-declarations -D_POSIX_C_SOURCE=200112L -DPREFETCH -I. -DATOMIC -pthread -DNSIPHASH=1 -DEDGEBITS=19 -DXBITS=2")

include_directories('./')


add_executable(lean.exe ../crypto/siphash.hpp cuckatoo.h bitmap.hpp compress.hpp graph.hpp lean.hpp lean.cpp ../crypto/blake2b-ref.c)

add_executable(mean.exe ../crypto/siphash.hpp mean.hpp mean.cpp cuckatoo.h  bitmap.hpp graph.hpp ../threads/barrier.hpp ../crypto/blake2b-ref.c)
