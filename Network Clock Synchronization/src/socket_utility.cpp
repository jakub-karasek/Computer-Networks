#include "socket_utility.h"

#include <sys/time.h>
#include <unistd.h>
#include <cstdlib>
#include <iostream>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <vector>

using namespace std;

// Function to set timeout for socket operations
void set_socket_timeout(int socket_fd, int send_timeout, int receive_timeout) {
    struct timeval timeout;
    timeout.tv_sec = send_timeout; // seconds
    timeout.tv_usec = 0; // microseconds

    if (setsockopt(socket_fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout)) < 0) {
        cerr << "ERROR setting socket send timeout fail" << endl;
        close(socket_fd);
        exit(EXIT_FAILURE);
    }

    timeout.tv_sec = receive_timeout; // seconds
    timeout.tv_usec = 0; // microseconds

    if (setsockopt(socket_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0) {
        cerr << "ERROR setting socket receive timeout fail" << endl;
        close(socket_fd);
        exit(EXIT_FAILURE);
    }
}

// Function to check if two sockaddr_in structures are equal
bool is_sockaddr_equal(const struct sockaddr_in *addr1, const struct sockaddr_in *addr2) {
    return (addr1->sin_family == addr2->sin_family &&
            addr1->sin_port == addr2->sin_port &&
            addr1->sin_addr.s_addr == addr2->sin_addr.s_addr);
}

// Function to check if adress is in the list of known peers
bool is_known_peer(const std::vector<struct sockaddr_in>& peer_addresses, const struct sockaddr_in& address) {
    for (const auto& peer : peer_addresses) {
        if (is_sockaddr_equal(&peer, &address)) {
            return true;
        }
    }
    return false;
}

