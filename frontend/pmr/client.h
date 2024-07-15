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

class PMRClient : Client {
public:
    PMRClient(MyOption opt, int id = 1);

    void Connect();

    void SendPut(const char * key, const char * val) {
        SendWrite(key, val, PUT);
    }

    bool SendGet(const char * key, std::string * val);

    void SendUpdate(const char * key, const char * val) {
        SendWrite(key, val, UPDATE);
    } 

    bool SendDelete(const char * key);

    void SendClose();

    int GetClientID() { 
        return client_id_;
    }

    void SendAlloc();

private:
    void SendWrite(const char * key, const char * val, Operation op);

private:
    std::unique_ptr<RDMADevice> rdma_device_;
    std::unique_ptr<RDMAContext> rdma_context_;
    uint8_t * local_buf_; // local send buffer
    uint32_t * header_;   // an of local_buf_
    // private write buffer
    uint32_t chunk_offset_; 
    uint32_t buf_head_;

    int client_id_;
    std::string ip_;
    int port_;
};

} // namespace frontend