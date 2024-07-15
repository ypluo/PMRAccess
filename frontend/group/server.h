#pragma once

#include <memory>
#include <vector>
#include <cstdio>
#include <thread>

#include "ringlog.h"
#include "../cs.h"

using namespace RDMAUtil;
using namespace SocketUtil;
using namespace ringlog;

namespace frontend {

class GroupClerk {
public: 
    GroupClerk(std::unique_ptr<RDMAContext> ctx, DBType * db, RingLog * log, int id);

    ~GroupClerk() {
        fprintf(stderr,"closing a clerk\n");
    }

    static void Run(std::unique_ptr<GroupClerk> clk, std::unique_ptr<uint8_t[]> buf);

private:
    std::unique_ptr<RDMAContext> context_;
    DBType * db_;
    RingLog * log_;
    uint8_t * local_buf_;

    int clerk_id_;
};

class GroupServer : Server {
public:
    GroupServer(MyOption opt, DBType * db);
    ~GroupServer() {delete log_;}

    void Listen();

private:
    std::unique_ptr<RDMADevice> rdma_device_;
    RingLog * log_;
    DBType * db_;
    int clerk_num_;
    int port_;
};

} // namespace frontend