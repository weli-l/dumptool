#pragma once
#include <fstream>
#include <mutex>
#include <atomic>
#include "mspti.h"
#include <vector>
#include <thread>
#include <condition_variable>
// #include <glog/logging.h>
#include <string.h>

class MSPTIHcclFileWriter {
private:
    std::ofstream file;
    std::mutex buffermtx;
    std::mutex bufferMarkerMtx;
    std::mutex threadmtx;
    std::atomic<bool> opened; 
    std::unique_ptr<std::vector<msptiActivityMarker>> markerActivityBuffer;
    std::thread writerThread;
    std::condition_variable cv;
    std::atomic<bool> stop;

public:
    MSPTIHcclFileWriter(const std::string& filename) {
        // obtain environment variable LOCAL_RANK
        // to determine the rank of the process
        // and append it to the filename
        const char* savePath = std::getenv("METRIC_PATH");
        if (savePath == nullptr) {
            savePath = "/var/log";
        }
        std::string savePathStr = savePath;
        if (!savePathStr.empty() && savePathStr.back() != '/') {
            savePathStr += "/";
        }
        std::string saveFilename = savePathStr + filename;
        std::string filenameWithRank = saveFilename;
        this->markerActivityBuffer = std::make_unique<std::vector<msptiActivityMarker>>();

        const char* localRankCStr = std::getenv("RANK");
        if (localRankCStr == nullptr) {
            localRankCStr = "-1";
        }
        std::string localRank = localRankCStr; // Now safe to construct std::string
        auto rank = std::stoi(localRank);    
        if (saveFilename.length() >= 4 && saveFilename.substr(saveFilename.length() - 4) == ".csv") {
            std::string baseName = saveFilename.substr(0, saveFilename.length() - 4);
            filenameWithRank = baseName + "." + std::to_string(rank) + ".csv";
        } else {
            filenameWithRank = saveFilename + "." + std::to_string(rank);
        }
        std::cout << "Filename: " << filenameWithRank << std::endl;

        // if file does not exists
        // create it and write header
        if (this->fileExists(filenameWithRank)) {
            this->file.open(filenameWithRank, std::ios::out | std::ios::app);
            this->opened.store(true);
        } else {
            this->file.open(filenameWithRank, std::ios::out | std::ios::app);
            this->opened.store(true);
            this->file << "kind,mode,timestamp,id,process_id&device_id,thread_id,name" << std::endl;
        }
        
        this->stop.store(false);
        this->run();
    }

    void stopWriter() {
        if (this->file.is_open()) {
            {
                std::unique_lock<std::mutex> lock(this->threadmtx);
                // clean up the thread
                this->stop.store(true);
            }
            this->cv.notify_all();
            this->hcclActivityFormatToCSV();
            if (this->writerThread.joinable()){
                this->writerThread.join();
            }
            // write the remaining buffer
            std::cout << "Closing file" << std::endl;
            this->file.close();
            this->opened.store(false);
        }
    }

    ~MSPTIHcclFileWriter() {
        this->stopWriter();
    }

    bool fileExists(const std::string& fp) {
        std::ifstream file(fp.c_str());
        return file.good() && file.is_open();
    }


    void bufferMarkerActivity(msptiActivityMarker* activity) {
        std::lock_guard<std::mutex> lock(this->bufferMarkerMtx);
        this->markerActivityBuffer->push_back(*activity);
    }

    void run() {
        // a thread to periodically flush
        // the buffer to the file
        // watch the conditional variable for signal
        this->writerThread = std::thread([this](){
            while (!this->stop.load()) {
                std::unique_lock<std::mutex> lock(this->threadmtx);
                if (this->cv.wait_for(lock, std::chrono::seconds(5)) == std::cv_status::timeout){
                    this->hcclActivityFormatToCSV();
                } else if (this->stop.load()) {
                    break;
                };
            }
        });

    }
    
    void replaceCommasWithExclamation(const char* input, char* output) {
        for (int i = 0; input[i] != '\0'; i++) {
            if (input[i] == ',') {
                output[i] = '!';
            } else {
                output[i] = input[i];
            }
        }
        output[strlen(input)] = '\0';
    }

    void hcclActivityFormatToCSV() {
        std::lock_guard<std::mutex> lock(this->buffermtx);
        if (this->file.is_open()) {
            // enumerate the buffer and write to file
            for (auto activity : *this->markerActivityBuffer) {
                char result[strlen(activity.name) + 1];
                this->replaceCommasWithExclamation(activity.name, result);
                if (activity.sourceKind == MSPTI_ACTIVITY_SOURCE_KIND_HOST) {
                    this->file << activity.kind << "," << activity.sourceKind << "," << activity.timestamp << "," << \
                    activity.id << "," << activity.objectId.pt.processId << "," << activity.objectId.pt.threadId << "," << \
                    result << std::endl;
                }else if(activity.sourceKind == MSPTI_ACTIVITY_SOURCE_KIND_DEVICE) {
                    this->file << activity.kind << "," << activity.sourceKind << "," << activity.timestamp << "," << \
                    activity.id << "," << activity.objectId.ds.deviceId << "," << activity.objectId.ds.streamId << "," << \
                    result << std::endl;
                }
            }
            this->markerActivityBuffer->clear();
        } else {
            std::cout << "File is not open" << std::endl;
        }
    }
};