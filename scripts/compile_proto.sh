#!/bin/bash

# 创建目标目录
mkdir -p client/c/generated server/python/generated

# C 客户端协议生成
protoc --proto_path=proto \
    --c_out=client/c/generated \
    --grpc-c_out=client/c/generated \
    proto/dumptool.proto

# Python 服务端协议生成
python -m grpc_tools.protoc \
    -Iproto \
    --python_out=server/python/generated \
    --grpc_python_out=server/python/generated \
    proto/dumptool.proto

echo "Protocol files generated successfully"
