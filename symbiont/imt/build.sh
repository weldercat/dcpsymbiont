#!/bin/bash

CXXFLAGS=" -Wall -g3 -DXDEBUG -DDEBUG -I. -O1 -Wno-overloaded-virtual -fno-exceptions \
	-fPIC -DHAVE_GCC_FORMAT_CHECK -DHAVE_BLOCK_RETURN \
	-I/usr/local/include/yate -I../../yate-svn/libs/ysig "

LXXFLAGS=" -rdynamic -shared \
	-Wl,--unresolved-symbols=ignore-in-shared-libs \
	-Wl,--retain-symbols-file,/dev/null -lyatesig -lyate \
	-L/usr/local/lib "

#g++ $CXXFLAGS -o analog.o -c analog.cpp
#g++ $LXXFLAGS -o analog.yate analog.o
g++ -c -o imt_line.o $CXXFLAGS imt_line.cpp
g++ -c -o imt.o $CXXFLAGS imt.cpp
g++ -o imt.yate imt.o imt_line.o $LXXFLAGS
#g++  -o analog.yate $CXXFLAGS analog.cpp $LXXFLAGS


