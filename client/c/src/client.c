#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <grpc/grpc.h>
#include <grpc/support/log.h>
#include "generated/dumptool.pb-c.h"
#include "generated/dumptool.grpc-c.h"

#define DEFAULT_SERVER   "localhost:50051"
#define MAX_FILE_SIZE    (1024 * 1024 * 100) // 100MB限制

struct CmdArgs {
    char* server;
    char* dump_path;
    char* input_file;
    Dumptool__V1__DumpRequest__DataFormat format;
};

void print_usage(const char* prog_name) {
    printf("Usage: %s -p PATH -i FILE [OPTIONS]\n\n", prog_name);
    printf("Options:\n");
    printf("  -s ADDRESS  Server address (default: %s)\n", DEFAULT_SERVER);
    printf("  -f FORMAT   Data format (json/protobuf/binary)\n");
    printf("  -h          Show this help\n");
}

int load_payload(const char* filename, uint8_t** buf, size_t* len) {
    FILE* fp = fopen(filename, "rb");
    if (!fp) {
        perror("File open failed");
        return -1;
    }
    return 0;
}

int main(int argc, char** argv) {
    struct CmdArgs args = {0};
    return 0;
}