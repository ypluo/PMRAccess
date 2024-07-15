#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include <cstdio>

#include "../cs.h"

using namespace RDMAUtil;
using namespace SocketUtil;

namespace frontend {

class PMemClient : Client {
public:
    PMemClient(MyOption opt, int id = 1);

    ~PMemClient() {
        delete [] local_buf_;
    }

    void Connect();

    void SendPut(const char * key, const char * val) {
        SendWrite(key, val, PUT);
    }

    void SendUpdate(const char * key, const char * val) {
        SendWrite(key, val, UPDATE);
    } 

    bool SendGet(const char * key, std::string * val);

    bool SendDelete(const char * key);

    void SendClose();

    int GetClientID() { 
        return client_id_;
    }

private:
    void SendWrite(const char * key, const char * val, Operation op);

private:
    uint8_t * local_buf_;
    std::unique_ptr<RDMADevice> rdma_device_;
    std::unique_ptr<RDMAContext> rdma_context_;

    int client_id_;
    uint64_t buf_head_;
    std::string ip_;
    int port_;
};

} // namespace frontend