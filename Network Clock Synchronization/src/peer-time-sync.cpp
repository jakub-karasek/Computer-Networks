#include <bits/stdc++.h>
#include <unistd.h>
#include <iostream>
#include <cstdlib>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <chrono>
#include <cstdint>
#include <endian.h>
#include <sys/time.h>
#include <netdb.h>

#include "socket_utility.h"
#include "messages.h"


using namespace std;

#define INVALID_PORT 0
#define INVALID_ADDRESS 0xFFFFFFFF
#define BUFFER_SIZE 65535

static volatile sig_atomic_t finish = 0;

/* Termination signal handling. */
static void catch_int(int sig) {
    (void)sig; 
    finish = 1;
}

void install_signal_handler(int signal, void (*handler)(int), int flags) {
    struct sigaction action;
    sigset_t block_mask;

    sigemptyset(&block_mask);
    action.sa_handler = handler;
    action.sa_mask = block_mask;
    action.sa_flags = flags;

    if (sigaction(signal, &action, NULL) < 0) {
        cerr << "ERROR sigaction failed" << endl;
        exit(EXIT_FAILURE);
    }
}

struct program_parameters {
    uint32_t b_value; // local bound address
    uint16_t p_value; // port number for binding
    uint32_t a_value; // IP address for sending HELLO if provided
    uint16_t r_value; // port number of the remote peer if provided
};

program_parameters parse_parameters(int argc, char* argv[]) {
    program_parameters params;
    // Ustaw wartości domyślne
    params.b_value = INADDR_ANY;  
    params.p_value = 0;             
    params.a_value = INVALID_ADDRESS;
    params.r_value = INVALID_PORT;

    int opt;
    while ((opt = getopt(argc, argv, "b:p:a:r:")) != -1) {
        switch(opt) {
            case 'b': {
                struct addrinfo hints{}, *res;
                hints.ai_family   = AF_INET;
                hints.ai_socktype = SOCK_DGRAM;
                if (getaddrinfo(optarg, nullptr, &hints, &res) != 0) {
                    cerr << "ERROR cannot resolve bind address: " << optarg << endl;
                    exit(EXIT_FAILURE);
                }
                // weź pierwszy wynik i zapamiętaj sieciowy porządek
                auto sa = reinterpret_cast<sockaddr_in*>(res->ai_addr);
                params.b_value = sa->sin_addr.s_addr;
                freeaddrinfo(res);
                break;
            }
            case 'p': {
                errno = 0;
                char* end;
                unsigned long val = strtoul(optarg, &end, 10);
                if (errno != 0 || *end != '\0' || val > UINT16_MAX) {
                    cerr << "ERROR Invalid port value: " << optarg << endl;
                    exit(EXIT_FAILURE);
                }
                params.p_value = static_cast<uint16_t>(val);
                break;
            }
            case 'a': {
                struct addrinfo hints {}, *res;
                hints.ai_family = AF_INET;
                hints.ai_socktype = SOCK_DGRAM;
                if (getaddrinfo(optarg, nullptr, &hints, &res) != 0) {
                    cerr << "ERROR cannot resolve host: " << optarg << endl;
                    exit(EXIT_FAILURE);
                }
                // weź pierwszy wynik
                auto sa = reinterpret_cast<sockaddr_in*>(res->ai_addr);
                params.a_value = sa->sin_addr.s_addr;  // sieciowy porządek
                freeaddrinfo(res);
                break;
            }
            case 'r': {
                errno = 0;
                char* end;
                unsigned long val = strtoul(optarg, &end, 10);
                if (errno || *end || val < 1 || val > UINT16_MAX) {
                    cerr << "ERROR Invalid peer port: " << optarg << endl;
                    exit(EXIT_FAILURE);
                }
                params.r_value = static_cast<uint16_t>(val);
                break;
            }
            default:
                cerr << "ERROR Usage: " << argv[0] 
                     << " [-b bind_addr] [-p port] [-a peer_addr] [-r peer_port]" 
                     << endl;
                exit(EXIT_FAILURE);
        }
    }

    // Check if both a_value and r_value are provided
    if ((params.a_value == INVALID_ADDRESS) != (params.r_value == INVALID_PORT)) {
        cerr << "ERROR both -a and -r must be specified together" << endl;
        exit(EXIT_FAILURE);
    }

    return params;
}

int main(int argc, char *argv[]) {
    // Initialize time variables
    auto start_time = chrono::high_resolution_clock::now(); 
    int64_t time_offset = 0; 
    int64_t T1_timestamp, T2_timestamp, T3_timestamp, T4_timestamp;
    
    // Parse command line arguments
    program_parameters params = parse_parameters(argc, argv);

    
    install_signal_handler(SIGINT, catch_int, SA_RESTART);

    // Create a socket
    int socket_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (socket_fd < 0) {
        cerr << "ERROR creating socket failed" << endl;
        exit(EXIT_FAILURE);
    }

    // Create a socket address structure
    struct sockaddr_in server_address;
    server_address.sin_family = AF_INET; // IPv4
    server_address.sin_addr.s_addr = params.b_value; // Already in network byte order
    server_address.sin_port = htons(params.p_value);

    // Set timeout for socket operations
    set_socket_timeout(socket_fd, 5, 1); // 5 seconds for sending, 1 second for receiving
    
    // Bind the socket to the address and port
    if (bind(socket_fd, (struct sockaddr *)&server_address, sizeof(server_address)) < 0) {
        cerr << "ERROR binding socket failed" << endl;
        close(socket_fd);
        exit(EXIT_FAILURE);
    }

    // Initialize the synchronization level
    int synch_level = 255;

    // Initalize buffers for sending and receiving messages
    char rec_buffer[BUFFER_SIZE];
    char send_buffer[BUFFER_SIZE];

    // Initialize sender address
    struct sockaddr_in sender_address;
    socklen_t sender_addr_length = (socklen_t) sizeof(sender_address);

    // Initialize the vector to store known peer addresses
    vector<struct sockaddr_in> peer_addresses;

    // Initialize variables for synchronization phase
    bool synch_phase = false;
    uint8_t synch_phase_level = 0; 
    struct sockaddr_in synch_phase_address;

    // Initialize the source address
    uint8_t source_synch_level = 0; 
    struct sockaddr_in source_address;
    source_address.sin_family = AF_INET;
    source_address.sin_addr.s_addr = INVALID_ADDRESS;
    source_address.sin_port = INVALID_PORT;

    // Initialize the timer for cyclic tasks
    auto synch_phase_start = chrono::high_resolution_clock::now();
    auto synch_phase_timeout = chrono::seconds(5); 
    auto synch_send_timer = chrono::high_resolution_clock::now();
    auto synch_send_interval = chrono::seconds(5); 
    auto synch_recieve_timeout_timer = chrono::high_resolution_clock::now();
    auto synch_recieve_timeout_interval = chrono::seconds(20); 

    // Send a HELLO message if a_value, r_value is provided
    if (params.a_value != INVALID_ADDRESS && params.r_value != INVALID_PORT) {
        struct sockaddr_in peer_address;
        memset(&peer_address, 0, sizeof(peer_address));
        peer_address.sin_family = AF_INET;
        peer_address.sin_addr.s_addr = params.a_value; // Already in network byte order
        peer_address.sin_port = htons(params.r_value); // Convert to network byte order

        send_simple_message(send_buffer, &peer_address, socket_fd, HELLO_MESSAGE); // Send HELLO message
    }

    // Main loop to receive messages
    while (!finish) {
        // Send START_SYNC message every 5 seconds if synch_level is less than 254
        auto current_time = chrono::high_resolution_clock::now();
        if (synch_level < 254 && chrono::duration_cast<chrono::seconds>(current_time - synch_send_timer) >= synch_send_interval) {  
            // send START_SYNC message to all known peers     
            send_start_sync_messages(send_buffer, socket_fd, peer_addresses, time_offset, synch_level, start_time);
            synch_send_timer = current_time; // Reset the timer
        }

        // Check for timeouts for receiving messages
        current_time = chrono::high_resolution_clock::now();
        if ((synch_level < 255 && synch_level != 0) && chrono::duration_cast<chrono::seconds>
            (current_time - synch_recieve_timeout_timer) >= synch_recieve_timeout_interval) {
            // 20 seconds passed since the last message, abort the current synchronization
            synch_level = 255;
            source_address.sin_addr.s_addr = INVALID_ADDRESS;
            source_address.sin_port = INVALID_PORT;
            source_synch_level = 0; 
            time_offset = 0; 
            synch_recieve_timeout_timer = current_time; // Reset the timer
        }

        // Abort synch phase if it is taking more than 5 seconds
        if (synch_phase && chrono::duration_cast<chrono::seconds>(current_time - synch_phase_start) >= synch_phase_timeout) {
            synch_phase = false;
            synch_level = 255; 
            synch_phase_address.sin_addr.s_addr = INVALID_ADDRESS; 
            synch_phase_address.sin_port = INVALID_PORT; 
        }

        // Receive a message
        ssize_t received_length = recvfrom(socket_fd, rec_buffer, sizeof(rec_buffer), 0,
                                           (struct sockaddr *)&sender_address, &sender_addr_length);
        
        if (received_length == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            // Timeout occurred, continue to the next iteration
            continue;
        } else if (received_length < 0) {
            // Error occurred
            cerr << "ERROR recvfrom failed" << endl;
            break;
        }

        uint8_t message = rec_buffer[0];

        // Check if the message is valid
        if (!validate_message_length(received_length, message)) {
            print_message_error(rec_buffer, received_length);
            continue;
        }

        switch (message) {
            case HELLO_MESSAGE: { 
                handle_hello_message(
                    rec_buffer, received_length,
                    send_buffer, socket_fd,
                    peer_addresses,
                    sender_address, sender_addr_length
                  );
                break;
            }
            case HELLO_REPLY_MESSAGE: { 
                handle_hello_reply_message(
                    rec_buffer, received_length,
                    send_buffer, socket_fd,
                    params.a_value, params.r_value,
                    peer_addresses, sender_address
                );
                break;
            }
            case CONNECT_MESSAGE: { 
                handle_connect_message(
                    send_buffer,
                    rec_buffer, 
                    received_length,
                    socket_fd,
                    peer_addresses,
                    sender_address
                );
                break;
            }
            case ACK_CONNECT_MESSAGE: { 
                handle_ack_connect_message(
                    rec_buffer, 
                    received_length,
                    peer_addresses, 
                    sender_address);
                break;
            }
            case SYNC_START_MESSAGE: { 
                handle_sync_start_message(
                    rec_buffer, 
                    received_length,
                    send_buffer, socket_fd,
                    peer_addresses,
                    sender_address,
                    source_address,
                    source_synch_level,
                    synch_level,
                    synch_phase,
                    synch_phase_level,
                    synch_phase_address,
                    synch_phase_start,
                    synch_recieve_timeout_timer,
                    start_time,
                    T1_timestamp,
                    T2_timestamp,
                    T3_timestamp
                );
                break;
            }
            case DELAY_REQUEST_MESSAGE: { 
                handle_delay_request_message(
                    send_buffer,
                    rec_buffer,
                    received_length,
                    socket_fd,
                    start_time,
                    time_offset,
                    synch_level,
                    sender_address,
                    sender_addr_length,
                    peer_addresses
                );
                break;
            }
            case DELAY_RESPONSE_MESSAGE: { 
                handle_delay_response_message(
                    rec_buffer, 
                    received_length,
                    synch_phase,
                    sender_address,
                    synch_phase_address,
                    synch_phase_level,
                    synch_level,
                    T1_timestamp,
                    T2_timestamp,
                    T3_timestamp,
                    T4_timestamp,
                    time_offset,
                    source_address,
                    source_synch_level,
                    synch_recieve_timeout_timer
                );
                break;
            }
            case LEADER_MESSAGE: { 
                handle_leader_message(
                    rec_buffer, received_length,
                    synch_level,
                    source_address,
                    source_synch_level,
                    time_offset,
                    synch_send_timer
                );
                break;
            }
            case GET_TIME_MESSAGE: { 
                handle_get_time_message(
                    send_buffer,
                    socket_fd,
                    start_time,
                    time_offset,
                    synch_level,
                    sender_address,
                    sender_addr_length
                );
                break;
            }
            default:
                cerr << "ERROR wrong message type" << endl;
                print_message_error(rec_buffer, received_length);
        }
    }

    close(socket_fd); // Close the socket

    return 0;
}