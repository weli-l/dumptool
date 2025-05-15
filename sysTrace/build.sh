#!/bin/bash
mkdir -p build

cd protos

protoc --c_out=. systrace.proto
protoc --cpp_out=. systrace.proto 
protoc --python_out=. systrace.proto

cd ../build

cmake ..
make -j 50
