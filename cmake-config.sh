#!/bin/bash
rm -rf build
mkdir build
cd build || exit 1

if [ -z "${LLVM_CONFIG}" ];
then
	export LLVM_CONFIG=llvm-config
fi

if [ -z "${CMAKE_BUILD_TYPE}" ];
then
    CMAKE_BUILD_TYPE=RelWithDebInfo
fi

cmake -DCMAKE_BUILD_TYPE="$CMAKE_BUILD_TYPE" \
	  ..