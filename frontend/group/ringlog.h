#pragma once

#include <string>
#include <cstring>
#include <unistd.h>
#include <fcntl.h>
#include <cassert>
#include <sys/stat.h>
#include <atomic>

namespace ringlog {

// preallocate a piece of space to reduce updating file metadata
const uint64_t PREALLOCATE_SIZE = 4UL * 1024 * 1024 * 1024;

class RingLog {
private:
    int fd_;
    bool sync_;
    std::atomic<uint64_t> offset_;

public:
    RingLog(const std::string & directory, const std::string & dbname, bool sync) {
        std::string dir = directory + "/" + dbname;
        if (access(dir.c_str(), F_OK) != 0) {
            mkdir(dir.c_str(), 0755);
        }
        std::string filename = dir + "/" + "grouplog.dat";
        fd_ = open(filename.c_str(), O_RDWR | O_CREAT, 0645);
        int ret = ftruncate(fd_, PREALLOCATE_SIZE);
        assert(fd_ > 2 && ret == 0); // check if the file is opened successfully

        lseek(fd_ , 0 , SEEK_SET);
        sync_ = sync;
        offset_.store(0);
    }

    ~RingLog() {
        close(fd_);
    }

    void Put(std::string & key, std::string & val) {
        char buffer[8 * 1024];
        *((int *)(buffer)) = key.size();
        *((int *)(buffer + 4)) = val.size();
        memcpy(buffer + 8, key.c_str(), key.size());
        memcpy(buffer + 8 + key.size(), val.c_str(), val.size());
        int len = 8 + key.size() + val.size();

        uint64_t pos = offset_.load(std::memory_order_relaxed);
        uint64_t next_pos = (pos < PREALLOCATE_SIZE ? pos + len : len);
        while(offset_.compare_exchange_weak(pos, next_pos, std::memory_order_acq_rel) == false) {
            next_pos = (pos < PREALLOCATE_SIZE ? pos + len : len);
        }

        auto ret = pwrite(fd_, buffer, len, pos);
        if(sync_) fdatasync(fd_);
    }

    void PutBatch(char * buffer, int len) {
        uint64_t pos = offset_.load(std::memory_order_relaxed);
        uint64_t next_pos = (pos < PREALLOCATE_SIZE ? pos + len : len);
        while(offset_.compare_exchange_weak(pos, next_pos, std::memory_order_acq_rel) == false) {
            next_pos = (pos < PREALLOCATE_SIZE ? pos + len : len);
        }

        auto ret = pwrite(fd_, buffer, len, pos);
        if(sync_) fdatasync(fd_);
    }
};

} // namespace logfile