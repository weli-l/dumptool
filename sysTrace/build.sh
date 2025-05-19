#!/bin/bash

sudo dnf remove -y libunwind libunwind-devel 2>/dev/null || true
mkdir -p build

cd protos
protoc --c_out=. systrace.proto
protoc --cpp_out=. systrace.proto
protoc --python_out=. systrace.proto
cd ..

mkdir -p libunwind_build
cd libunwind_build
git clone https://github.com/libunwind/libunwind.git
cd libunwind
autoreconf -i
./configure --prefix=/usr/local
make -j $(nproc) 
sudo make install
cp -r include /usr/local
cd ../..

cd build
cmake ..
make -j $(nproc)
