/*
    CopyRight (c) Luo Yongping
*/

#include "socketutil.h"

namespace SocketUtil {

Socket::~Socket() {
    if(socktype == SERVER) {
        // closing the connected socket
        for(auto i : commu_sock)
            close(i);
        // closing the listening socket
        shutdown(listen_sock, SHUT_RDWR);
    } else {
        // closing the connected socket
        close(commu_sock[0]);
    }
}

int Socket::NewChannel(int port) {
    assert(socktype == SERVER);

    // accept a new connect
    int new_commu_sock;
    if (listen(listen_sock, 3) < 0) {
        perror("listen");
        exit(EXIT_FAILURE);
    }

    // accept a connection and create a new socket for communication
    struct sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port);
    int addrlen = sizeof(address);
    if ((new_commu_sock = accept(listen_sock, (struct sockaddr*)&address, (socklen_t*)&addrlen)) < 0) {
        perror("accept");
        exit(EXIT_FAILURE);
    }

    commu_sock.push_back(new_commu_sock);

    return new_commu_sock;
}

std::unique_ptr<Socket> Socket::make_socket(SockType s, int port, std::string ipaddr) {
    auto new_socket = std::make_unique<Socket>(s);
    
    // Creating a socket
    int init_sock = -1;
    if ((init_sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("socket failed");
        exit(EXIT_FAILURE);
    }

    struct sockaddr_in address;
    if(new_socket->socktype == SERVER) {
        new_socket->listen_sock = init_sock;

        int opt = 1;
        if (setsockopt(new_socket->listen_sock, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt))) {
            perror("setsockopt");
            exit(EXIT_FAILURE);
        }

        address.sin_family = AF_INET;
        address.sin_addr.s_addr = INADDR_ANY;
        address.sin_port = htons(port);
        if (bind(new_socket->listen_sock, (struct sockaddr*)&address, sizeof(sockaddr_in)) < 0) {
            perror("bind failed");
            exit(EXIT_FAILURE);
        }
        
        if (listen(new_socket->listen_sock, 3) < 0) {
            perror("listen");
            exit(EXIT_FAILURE);
        }

        // accept a connection and create a new socket for communication
        int addrlen = sizeof(address);
        int new_commu_sock;
        if ((new_commu_sock = accept(new_socket->listen_sock, (struct sockaddr*)&address, (socklen_t*)&addrlen)) < 0) {
            perror("accept");
            exit(EXIT_FAILURE);
        }
        new_socket->commu_sock.push_back(new_commu_sock);
        
    } else {
        assert(ipaddr.length() > 0);

        new_socket->commu_sock.push_back(init_sock);

        address.sin_family = AF_INET;
        address.sin_port = htons(port);
        // Convert IPv4 and IPv6 addresses from text to binary form
        if (inet_pton(AF_INET, ipaddr.c_str(), &address.sin_addr) <= 0) {
            perror("IP Address inet_pton");
            exit(EXIT_FAILURE);
        }

        // connect to a server
        if (connect(new_socket->commu_sock[0], (struct sockaddr*)&address, sizeof(address)) < 0) {
            perror("connect");
            exit(EXIT_FAILURE);
        }
    }
    
    return std::move(new_socket);
}

}