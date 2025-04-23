#include "mspti.h"
#include <iostream>
#include <atomic>
#include <dlfcn.h>
#include <stdlib.h>
#include <memory>
#include <mutex>
#include <sstream>
#include "json_file_writer.h"
    
constexpr size_t ALIGN_SIZE = 8;

inline uint8_t* align_buffer(uint8_t* buffer, size_t align) {
    return reinterpret_cast<uint8_t*>((reinterpret_cast<uintptr_t>(buffer) + (align - 1)) & ~(align - 1));
}


class MSPTITracker {
private:
    msptiSubscriberHandle subscriber;
    static std::mutex mtx;
    static MSPTIHcclFileWriter hcclFileWriter;
    static std::atomic<int> requestedCount;

public:
    MSPTITracker() {
    }

    ~MSPTITracker() {
    }

    msptiSubscriberHandle* getSubscriber() {
        return &subscriber;
    }

    void finish() {
        std::cout << "Finishing MSPTI Tracker" << std::endl;
        hcclFileWriter.stopWriter();
    }

    static void readActivityMarker(msptiActivityMarker* activity) {
        hcclFileWriter.bufferMarkerActivity(activity);
    }
    
    static void UserBufferRequest(uint8_t **buffer, size_t *size, size_t *maxNumRecords);

    static void UserBufferComplete(uint8_t *buffer, size_t size, size_t validSize);
};