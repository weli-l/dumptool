#include <iostream>
#include <dlfcn.h>
#include <stdlib.h>
#include <memory>
#include <mutex>
#include "mspti.h"
#include "mspti_tracker.hpp"
#include "json_file_writer.h"
#include "atomic"

constexpr size_t KB = 1 * 1024;
constexpr size_t MB = 1 * 1024 * KB;

std::mutex MSPTITracker::mtx;
std::atomic<int> MSPTITracker::requestedCount(0);
MSPTIHcclFileWriter MSPTITracker::hcclFileWriter("hccl_activity.json");
std::atomic<bool> MSPTITracker::tracker_initialized(false);

void MSPTITracker::UserBufferRequest(uint8_t **buffer, size_t *size, size_t *maxNumRecords) {
    std::unique_lock<std::mutex> lock(mtx);
    constexpr uint32_t SIZE = (uint32_t)MB * 1;
    requestedCount.fetch_add(1);
    uint8_t *pBuffer = (uint8_t *) malloc(SIZE + ALIGN_SIZE);
    *buffer = align_buffer(pBuffer, ALIGN_SIZE);
    *size = MB * 1;
    *maxNumRecords = 0;
}

void MSPTITracker::UserBufferComplete(uint8_t *buffer, size_t size, size_t validSize) {
    if (validSize > 0) {
        msptiActivity *pRecord = NULL;
        msptiResult status = MSPTI_SUCCESS;
        do {
            std::unique_lock<std::mutex> lock(mtx);
            status = msptiActivityGetNextRecord(buffer, validSize, &pRecord);
            if (status == MSPTI_SUCCESS) {
                if (pRecord->kind == MSPTI_ACTIVITY_KIND_MARKER) {
                    msptiActivityMarker* activity = reinterpret_cast<msptiActivityMarker*>(pRecord);
                    readActivityMarker(activity);
                } 
                
            } else if (status == MSPTI_ERROR_MAX_LIMIT_REACHED) {
                break;
            }
        } while (1);
    }
    free(buffer);
}