#ifndef SOCK_UTIL_H
#define SOCK_UTIL_H

#include <sys/socket.h>
#include <netinet/in.h>
#include <vector>               
#include <cstdint>

// Function to set timeout for socket operations
void set_socket_timeout(int socket_fd, int send_timeout, int receive_timeout);

// Function to check if two sockaddr_in structures are equal
bool is_sockaddr_equal(const struct sockaddr_in *addr1, const struct sockaddr_in *addr2);

// Function to check if adress is in the list of known peers
bool is_known_peer(const std::vector<struct sockaddr_in>& peer_addresses, const struct sockaddr_in& address);

#endif