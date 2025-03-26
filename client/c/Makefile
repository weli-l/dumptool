CC := gcc
CFLAGS := -Wall -O2
PROTO_PATH := ../../proto
GEN_DIR := generated
SRC_DIR := src
BUILD_DIR := ../../build

CLIENT_TARGET := $(BUILD_DIR)/dumpclient

all: $(CLIENT_TARGET)

$(GEN_DIR)/dumptool.pb-c.c: $(PROTO_PATH)/dumptool.proto
	protoc --proto_path=$(PROTO_PATH) --c_out=$(GEN_DIR) $<
	protoc --plugin=protoc-gen-grpc-c=`which grpc-c-generator` \
        --grpc-c_out=$(GEN_DIR) $(PROTO_PATH)/dumptool.proto

$(CLIENT_TARGET): $(SRC_DIR)/client.c $(GEN_DIR)/dumptool.pb-c.c
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -I$(GEN_DIR) -I/usr/local/include $^ \
        -o $@ -lgrpc -lgrpc-c -lprotobuf-c -lpthread

clean:
	rm -rf $(CLIENT_TARGET) $(GEN_DIR)/*

.PHONY: all clean