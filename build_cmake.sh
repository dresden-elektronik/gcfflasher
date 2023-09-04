#!/bin/env bash
#

rm -fr build
mkdir build
pushd build

cmake --fresh ..
cmake --build .
cpack -G DEB -B debout .

popd

