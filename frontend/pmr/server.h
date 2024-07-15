#pragma once

#include <memory>
#include <vector>
#include <cstdio>
#include <thread>
#include <mutex>
#include <queue>

#include "libcuckoo/cuckoohash_map.h"
#include "atomicbitset.h"
#include "uring.h"
#include "../cs.h"

#ifdef DMABUF
#include "dmabuf.h"
#endif

using namespace RDMAUtil;
using namespace SocketUtil;
using HashType = libcuckoo::cuckoohash_map<std::string, Meta>;

namespace frontend {

class PMRServer;

/* PMRClerk: sync on every operation, but write do not sync to disk immediately */
class PMRClerk {
public: 
    PMRClerk(std::unique_ptr<RDMAContext> ctx, PMRServer *server, int id);

    ~PMRClerk() {
        fprintf(stderr,"closing a clerk\n");
    }

    static void Run(std::unique_ptr<PMRClerk> clk);

private:
    std::unique_ptr<RDMAContext> context_;
    DBType * db_;
    uint8_t * send_buf_;
    uint8_t * write_buf_;
    uint32_t buf_head_;
    uint32_t chunk_offset_;
    PMRServer * server_;
    int clerk_id_;
};

class PMRServer : Server {
public:
    PMRServer(MyOption opt, DBType * db);

    void Listen();

    size_t AllocChunk();

    void FreeChunk(size_t);

    float PeekUsage();

public:
    std::unique_ptr<RDMADevice> rdma_device_;
    DBType * db_;
    int clerk_num_;
    int port_;
    std::string path_;
    HashType map_;

    IOuring * ring_;
    AtomicBitset bitmap_;
    
    #ifdef DMABUF
        int dmabuf_fd_;
    #else
        uint8_t * dmabuf_mem_;
    #endif
};

} // namespace frontend