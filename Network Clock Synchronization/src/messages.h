#ifndef MESSAGES_H
#define MESSAGES_H

#include <cstdint>
#include <sys/types.h>
#include <chrono>
#include <vector>
#include <netinet/in.h>

#define HELLO_MESSAGE 1
#define HELLO_REPLY_MESSAGE 2
#define CONNECT_MESSAGE 3
#define ACK_CONNECT_MESSAGE 4
#define SYNC_START_MESSAGE 11
#define DELAY_REQUEST_MESSAGE 12
#define DELAY_RESPONSE_MESSAGE 13
#define LEADER_MESSAGE 21
#define GET_TIME_MESSAGE 31
#define TIME_MESSAGE 32

// Function printing message errors in specified format
void print_message_error(const char *rec_buffer, ssize_t received_length);

// Function validating recieved message's length
bool validate_message_length(ssize_t received_length, uint8_t message_type);

// Function to send a message with a specified type
void send_simple_message(char send_buffer[], const struct sockaddr_in *peer_address, int socket_fd, uint8_t message);

// Function to send START_SYNC message to all known peers
void send_start_sync_messages(char send_buffer[], int socket_fd, 
    const std::vector<struct sockaddr_in>& peer_addresses,
    int64_t time_offset, int synch_level,
    std::chrono::high_resolution_clock::time_point start_time);

// Function to check synchronization conditions
bool check_sync_conditions(
    const std::vector<struct sockaddr_in>&,
    const struct sockaddr_in&,
    uint8_t,
    const struct sockaddr_in&,
    int
);

// Function that handles recieving HELLO messages
void handle_hello_message(
    const char    rec_buffer[], 
    ssize_t       received_length,
    char          send_buffer[],
    int           socket_fd,
    std::vector<struct sockaddr_in>& peer_addresses,
    const struct sockaddr_in& sender_address,
    socklen_t     sender_addr_length
);

// Function that handles recieving HELLO_REPLY messages
void handle_hello_reply_message(
    const char                      rec_buffer[],
    ssize_t                         received_length,
    char                            send_buffer[],
    int                             socket_fd,
    uint32_t                        expected_a_value,
    uint16_t                        expected_r_value,
    std::vector<struct sockaddr_in>& peer_addresses,
    const struct sockaddr_in&       sender_address
);

// Function that handles recieving CONNECT messages
void handle_connect_message(
    char                                  send_buffer[],
    char                                  rec_buffer[],
    ssize_t                               received_length,
    int                                   socket_fd,
    std::vector<struct sockaddr_in>&     peer_addresses,
    const struct sockaddr_in&            sender_address
);

// Function that handles recieving ACK_CONNECT messages
void handle_ack_connect_message(
    char rec_buffer[],
    ssize_t received_length,
    std::vector<struct sockaddr_in>& peer_addresses,
    const struct sockaddr_in&        sender_address
);

// Function that handles recieving SYNC_START messages
void handle_sync_start_message(
    const char                                           rec_buffer[],
    ssize_t                                             received_length,
    char                                                send_buffer[],
    int                                                 socket_fd,
    const std::vector<struct sockaddr_in>&             peer_addresses,
    const struct sockaddr_in&                          sender_address,
    const struct sockaddr_in&                          source_address,
    uint8_t                                             source_synch_level,
    int&                                                synch_level,
    bool&                                               synch_phase,
    uint8_t&                                            synch_phase_level,
    struct sockaddr_in&                                 synch_phase_address,
    std::chrono::high_resolution_clock::time_point&     synch_phase_start,
    std::chrono::high_resolution_clock::time_point&     synch_recieve_timeout_timer,
    const std::chrono::high_resolution_clock::time_point& start_time,
    int64_t&                                            T1_timestamp,
    int64_t&                                            T2_timestamp,
    int64_t&                                            T3_timestamp
);

// Function that handles recieving DELAY_REQUEST messages
void handle_delay_request_message(
    char                                           send_buffer[],
    char                                           rec_buffer[],
    ssize_t                                        received_length,
    int                                            socket_fd,
    const std::chrono::high_resolution_clock::time_point& start_time,
    int64_t                                        time_offset,
    int                                            synch_level,
    const struct sockaddr_in&                      sender_address,
    socklen_t                                      sender_addr_length,
    const std::vector<struct sockaddr_in>&         peer_addresses
);

// Function that handles recieving DELAY_RESPONSE messages
void handle_delay_response_message(
    const char                                     rec_buffer[],
    ssize_t                                       received_length,
    bool&                                          synch_phase,
    const struct sockaddr_in&                      sender_address,
    struct sockaddr_in&                            synch_phase_address,
    uint8_t&                                       synch_phase_level,
    int&                                           synch_level,
    int64_t&                                       T1_timestamp,
    int64_t&                                       T2_timestamp,
    int64_t&                                       T3_timestamp,
    int64_t&                                       T4_timestamp,
    int64_t&                                       time_offset,
    struct sockaddr_in&                            source_address,
    uint8_t&                                       source_synch_level,
    std::chrono::high_resolution_clock::time_point& synch_recieve_timeout_timer
);

// Function that handles recieving LEADER messages
void handle_leader_message(
    const char                                       rec_buffer[],
    ssize_t                                          received_length,
    int&                                             synch_level,
    struct sockaddr_in&                              source_address,
    uint8_t&                                         source_synch_level,
    int64_t&                                         time_offset,
    std::chrono::high_resolution_clock::time_point&  synch_send_timer
);

// Function that handles recieving GET_TIME messages
void handle_get_time_message(
    char                                         send_buffer[],
    int                                          socket_fd,
    const std::chrono::high_resolution_clock::time_point& start_time,
    int64_t                                      time_offset,
    int                                          synch_level,
    const struct sockaddr_in&                   sender_address,
    socklen_t                                    sender_addr_length
);

#endif