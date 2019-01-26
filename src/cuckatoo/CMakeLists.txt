cmake_minimum_required (VERSION 2.6)
project (cuckatoo)


set(CMAKE_CXX_FLAGS "-g3 -march=native -std=c++11 -Wall -Wno-format -Wno-deprecated-declarations -D_POSIX_C_SOURCE=200112L -DPREFETCH -I. -pthread -DNSIPHASH=1 -DEDGEBITS=13 -DPROOFSIZE=16 -DIDXSHIFT=3")

include_directories('./')

add_executable(lean.exe ../crypto/siphash.hpp cuckatoo.h bitmap.hpp compress.hpp graph.hpp lean.hpp lean.cpp ../crypto/blake2b-ref.c)