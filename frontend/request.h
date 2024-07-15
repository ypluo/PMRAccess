#pragma once

#include <cstdint>

enum Operation {PUT, GET, UPDATE, DELETE, CLOSE, ALLOC};
enum RequestStatus {OK, NOTFOUND, ERROR};

struct Request {
    uint32_t op : 8;
    uint32_t key_size : 24;
    uint32_t val_size;
    char keyvalue[0];

    inline uint32_t Length() {
        return sizeof(Request) + key_size + val_size;
    }
};

struct RequestReply {
    RequestStatus status;
    uint32_t val_size;
    char value[0];
};

const int MAX_REQUEST = 4096;