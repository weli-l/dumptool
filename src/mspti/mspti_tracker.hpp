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
        // if (activity->sourceKind == MSPTI_ACTIVITY_SOURCE_KIND_HOST) {
        //     std::cout << "[Marker] host kind: " << activity->kind << ", mode: " << activity->sourceKind << ", timestamp: " << activity->timestamp << ", markId: "<< activity->id << ", processId: "<< activity->objectId.pt.processId << ", threadId: "<<activity->objectId.pt.threadId <<", name: "<< activity->name<<std::endl;
        // } else if(activity->sourceKind == MSPTI_ACTIVITY_SOURCE_KIND_DEVICE) {
        //     std::cout << "[Marker] device kind: " << activity->kind << ", mode: " << activity->sourceKind << ", timestamp: " << activity->timestamp << ", markId: "<< activity->id << ", deviceId: "<< activity->objectId.ds.deviceId << ", streamId: "<<activity->objectId.ds.streamId<<", name: "<< activity->name<<std::endl;
        // }
    }
    
    static void readHcclActivity(msptiActivityHccl* activity) {
        hcclFileWriter.bufferHcclActivity(activity);
    }
    
    static void readMemcpyActivity(msptiActivityMemcpy* activity) {
        std::cout << "[Memcopy] kind: " << activity->kind <<", bytes: "<<activity->bytes << ", start: " << activity->start << ", end: " << activity->end << ", deviceId: "<< activity->deviceId << ", streamId: "<< activity->streamId << ", correlationId: "<<activity->correlationId <<", isAsync: "<< activity->isAsync << std::endl;
    }
    
    // MSPTI
    static void UserBufferRequest(uint8_t **buffer, size_t *size, size_t *maxNumRecords);

    // MSPTI
    static void UserBufferComplete(uint8_t *buffer, size_t size, size_t validSize);
};