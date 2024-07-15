#pragma once

#include <string>
#include <cstdint>

#include "rdmautil.h"
#include "socketutil.h"
#include "flags.h"
#include "request.h"
#include "../database/db.h"

#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

namespace frontend {
class Server {
public:
    Server() {}

    virtual void Listen() = 0;
};

class Client {
public:
    Client() {}

    virtual void Connect() = 0;

    virtual void SendPut(const char * key, const char * val) = 0;

    virtual void SendUpdate(const char * key, const char * val) = 0;

    virtual bool SendGet(const char * key, std::string * val) = 0;

    virtual bool SendDelete(const char * key) = 0;

    virtual void SendClose() = 0;

    virtual int GetClientID() = 0; 
};

const int MAX_ASYNC_SIZE   = 32 * 1024;
const uint32_t CLIENT_DONE = 0x7f7f7f7f;
const uint32_t CLERK_DONE  = 0xf7f7f7f7;
const int RING_HEADER      = sizeof(uint32_t) * 2;

inline bool folder_exist(const char *fname) {
    struct stat buffer;
    return stat(fname, &buffer) == 0 && S_ISDIR(buffer.st_mode);
}

inline void mfence() {
    asm volatile ("" : : : "memory");
}

} // namespace frontend