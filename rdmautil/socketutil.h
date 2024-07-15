/*
    CopyRight (c) Luo Yongping
*/

#ifndef __SOCKET_UTIL__
#define __SOCKET_UTIL__

#include <netinet/in.h>
#include <arpa/inet.h>
#include <vector>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <string>
#include <cassert>
#include <memory>

namespace SocketUtil {

enum SockType {CLIENT = 0, SERVER};

class Socket {
public:
    static const int MTU = 256;
    
    Socket(SockType t) : socktype(t) {
        listen_sock = -1;
    }

    Socket(const Socket &) = delete;
    Socket(Socket &&) = delete;
    Socket & operator=(const Socket &) = delete;
    Socket & operator=(Socket &&) = delete;

    ~Socket();

    static std::unique_ptr<Socket> make_socket(SockType s, int port, std::string ipaddr = "");

    int NewChannel(int port);
    
    inline int GetFirst() {
        return commu_sock[0];
    }

    inline size_t Send(void * buf, int len) {
        // without slicing
        assert(len <= MTU);
        return send(commu_sock[0], buf, len, 0);
    }

    inline size_t Recv(void * buf, int len) {
        assert(len <= MTU);
        return read(commu_sock[0], buf, len);
    }

private:

    SockType socktype;
    int listen_sock;
    std::vector<int> commu_sock;
};

}

#endif // __SOCKET_UTIL__