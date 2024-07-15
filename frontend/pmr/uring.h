#pragma once

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cassert>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <liburing.h>
#include <mutex>
#include <queue>

namespace frontend {

const uint64_t PREALLOCATE_SIZE = 4UL * 1024 * 1024 * 1024;
static inline int get_file_size(int fd, off_t *size) {
    struct stat st;

    if (fstat(fd, &st) < 0 )
        return -1;
    if(S_ISREG(st.st_mode)) {
        *size = st.st_size;
        return 0;
    } else if (S_ISBLK(st.st_mode)) {
        unsigned long long bytes;
        if (ioctl(fd, BLKGETSIZE64, &bytes) != 0)
            return -1;

        *size = bytes;
        return 0;
    }
    return -1;
}

class IOuring {
private:
    io_uring ring_;
    int fd_;
    int inflight_;
    off_t offset_;
    int qd_;

public:
    IOuring(int fd, int queue_size) { 
        fd_ = fd;
        qd_ = queue_size;
        inflight_ = 0;
        offset_ = 0;

        int ret = io_uring_queue_init(queue_size, &ring_, 0);
        if(ret < 0) {
            fprintf(stderr, "queue_init: %s\n", strerror(-ret));
            exit(-1);
        }
    }

    ~IOuring() { 
        io_uring_queue_exit(&ring_); 
    }

    inline void Seen(io_uring_cqe* cqe) { 
        io_uring_cqe_seen(&ring_, cqe);
        inflight_ -= 1;
    }

    inline int Wait(io_uring_cqe** cqe_ptr) { 
        return io_uring_wait_cqe(&ring_, cqe_ptr);
    }

    inline int Submit() {
        return io_uring_submit(&ring_);
    }

    void Read(void * buf, int size, __u64 data) {
        auto sqe = io_uring_get_sqe(&ring_);
        io_uring_prep_read(sqe, fd_, buf, size, -1);
        io_uring_sqe_set_data64(sqe, data);
        inflight_ += 1;
    }

    void Write(void * buf, int size, __u64 data) {
        auto sqe = io_uring_get_sqe(&ring_);
        io_uring_prep_write(sqe, fd_, buf, size, offset_);
        io_uring_sqe_set_data64(sqe, data);
        inflight_ += 1;
        offset_ += size;
    }

    inline bool Full() {
        return inflight_ >= qd_;
    }

    inline bool Empty() {
        return inflight_ == 0;
    }
};

}