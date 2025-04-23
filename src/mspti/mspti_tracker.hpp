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
    static std::atomic<bool> tracker_initialized;
    static MSPTITracker* tracker;

public:
    MSPTITracker() {
        std::cout << "Logging initialized from preloaded library." << std::endl;
        if (!tracker_initialized.load()) {
            tracker_initialized.store(true);
            msptiSubscribe(&subscriber, nullptr, nullptr);
            msptiActivityRegisterCallbacks(UserBufferRequest, UserBufferComplete);
            msptiActivityEnable(MSPTI_ACTIVITY_KIND_MARKER);
        } else {
            std::cout << "no tracker!!!!!!" << std::endl;
        }
    }

    ~MSPTITracker() {
        if (tracker_initialized.load()) {
            msptiActivityDisable(MSPTI_ACTIVITY_KIND_MARKER);
            msptiActivityFlushAll(1);
            finish();
            msptiUnsubscribe(subscriber);
            tracker_initialized.store(false);
        }
    }

    static MSPTITracker* getInstance() {
        if (!tracker_initialized.load()) {
            tracker = new MSPTITracker();
        }
        return tracker;
    }

    static void destroyInstance() {
        if (tracker_initialized.load()) {
            delete tracker;
            tracker = nullptr;
        }
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