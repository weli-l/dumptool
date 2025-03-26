# DumpTool 数据转储系统

## 快速开始
```bash
# 安装依赖
sudo apt install build-essential libgrpc-dev protobuf-compiler

# 生成协议代码
chmod +x scripts/compile_proto.sh
./scripts/compile_proto.sh

# 编译客户端
cd client/c && make

# 启动服务端
./scripts/start_server.sh

# 发送请求
./build/dumpclient -p "/test" -i examples/test.data