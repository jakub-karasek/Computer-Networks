#include "messages.h"
#include "socket_utility.h"

#include <iostream>
#include <iomanip>      
#include <cstring>       
#include <cerrno>        
#include <endian.h>
#include <vector>
#include <chrono>
#include <endian.h>
#include <arpa/inet.h>

#define INVALID_PORT 0
#define INVALID_ADDRESS 0xFFFFFFFF
#define BUFFER_SIZE 65535

using namespace std;

// Function to send a message with a specified type
void send_simple_message(char send_buffer[], const struct sockaddr_in *peer_address, int socket_fd, uint8_t message) {
    send_buffer[0] = message; // message type

    // Send the simple message to the peer
    ssize_t send_length = sendto(socket_fd, send_buffer, 1, 0,
        (struct sockaddr *)peer_address, sizeof(*peer_address));

    // Check for errors
    if (send_length < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
        cerr << "ERROR sending fail" << endl;
    }
}

// Function printing message errors in specified format
void print_message_error(const char *rec_buffer, ssize_t received_length) {
    cerr << "ERROR MSG ";
    int limit = (received_length < 10 ? received_length : 10);
    for (int i = 0; i < limit; ++i) {
        unsigned char byte = static_cast<unsigned char>(rec_buffer[i]);
        cerr << hex << setw(2) << setfill('0') << nouppercase
             << static_cast<int>(byte);
    }
    cerr << dec << endl;
}

// Function validating recieved message's length
bool validate_message_length(ssize_t received_length, uint8_t message_type) {
    bool valid = true;
    switch (message_type) {
        case 1:    // HELLO
        case 3:    // CONNECT
        case 4:    // ACK_CONNECT
        case 12:   // DELAY_REQUEST
        case 31:   // GET_TIME
            valid = (received_length == 1);
            break;
        case 2:    // HELLO_REPLY
            valid = (received_length >= 3);
            break;
        case 11:   // SYNC_START
        case 13:   // DELAY_RESPONSE
            valid = (received_length == 10);
            break;
        case 21:   // LEADER
            valid = (received_length == 2);
            break;
        default:
            valid = false;
    }
    return valid;
}

// Function to send START_SYNC message to all known peers
void send_start_sync_messages(char send_buffer[], int socket_fd, 
    const std::vector<struct sockaddr_in>& peer_addresses,
    int64_t time_offset, int synch_level,
    chrono::high_resolution_clock::time_point start_time) {
    // Prepare a START_SYNC message
    send_buffer[0] = 11; // START_SYNC message
    send_buffer[1] = synch_level; 

    // Send the START_SYNC message to all known peers
    for (const auto& peer : peer_addresses) {
    // Retrieve the current timestamp before each send to minimize the time difference
    auto timestamp = chrono::duration_cast<chrono::milliseconds>(
    chrono::high_resolution_clock::now() - start_time).count();
    int64_t network_timestamp = htobe64(timestamp - time_offset); // Convert to network byte order
    memcpy(send_buffer + 2, &network_timestamp, sizeof(network_timestamp)); // Copy timestamp to send buffer

    ssize_t send_length = sendto(socket_fd, send_buffer, sizeof(network_timestamp) + 2, 0,
    (struct sockaddr *)&peer, sizeof(peer));

    // Check for errors
    if (send_length < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
    cerr << "ERROR sending START_SYNC message failed" << endl;
    }
    }
}

// Function to check synchronization conditions
bool check_sync_conditions(const vector<struct sockaddr_in>& peer_addresses, 
    const struct sockaddr_in& sender_address, 
    uint8_t sender_synch_level,
    const struct sockaddr_in& source_address, 
    int synch_level) {
    // condition 1: sender is in the list of known peers
    bool condition1 = is_known_peer(peer_addresses, sender_address);

    // condition 2: sender's synchronization level is less than 254
    bool condition2 = sender_synch_level < 254;

    // condition 3: if sender is equal to source_address, it must have a lower synchronization level
    bool condition3 = is_sockaddr_equal(&source_address, &sender_address) && (sender_synch_level < synch_level);

    // condition 4: if sender is not equal to source_address, it must have a lower synchronization level than synch_level - 1
    bool condition4 = !is_sockaddr_equal(&source_address, &sender_address) && (sender_synch_level + 2 <= synch_level);

    return condition1 && condition2 && (condition3 || condition4);
}

// Function that handles recieving HELLO messages
void handle_hello_message(
    const char    rec_buffer[], 
    ssize_t       received_length,
    char          send_buffer[],
    int           socket_fd,
    std::vector<struct sockaddr_in>& peer_addresses,
    const struct sockaddr_in& sender_address,
    socklen_t     sender_addr_length
) {
    // Break if the sender is already in the list of known peers or list is full
    if (is_known_peer(peer_addresses, sender_address) || peer_addresses.size() >= UINT16_MAX){
        print_message_error(rec_buffer, received_length); // Print error for ignored message
        return;
    }

    // Calculate the size of the HELLO_REPLY message
    size_t message_size = 1 + sizeof(uint16_t) + peer_addresses.size() * (sizeof(uint8_t) + sizeof(uint32_t) + sizeof(uint16_t));
    if (message_size > BUFFER_SIZE) {
        cerr << "ERROR HELLO_REPLY message too large" << endl;
        print_message_error(rec_buffer, received_length);
        return;
    }

    // Prepare HELLO_REPLY message
    send_buffer[0] = HELLO_REPLY_MESSAGE;
    uint16_t peer_count = htons(peer_addresses.size()); 
    memcpy(send_buffer + 1, &peer_count, sizeof(peer_count));

    size_t offset = 3; // Start after the message type and count

    for (size_t i = 0; i < peer_addresses.size(); ++i) {
        // Copy the address length to the buffer
        uint8_t peer_address_length = (uint8_t) sizeof(peer_addresses[i].sin_addr.s_addr);
        memcpy(send_buffer + offset, &peer_address_length, 1);
        offset += 1;

        // Copy the address to the buffer
        uint32_t peer_address = peer_addresses[i].sin_addr.s_addr;  // address already in network byte order
        memcpy(send_buffer + offset, &peer_address, peer_address_length);
        offset += peer_address_length;

        // Copy the port to the buffer
        uint16_t peer_port = peer_addresses[i].sin_port;    // port already in network byte order
        memcpy(send_buffer + offset, &peer_port, sizeof(peer_port));
        offset += sizeof(peer_port);
    }
    // Send the HELLO_REPLY message to the sender
    ssize_t send_length = sendto(socket_fd, send_buffer, offset, 0,
        (struct sockaddr *)&sender_address, sender_addr_length);

    // Check for errors
    if (send_length < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
        cerr << "ERROR sending HELLO_REPLY message failed" << endl;
    }

    // Add the sender address to the list of known peers
    peer_addresses.push_back(sender_address);
}

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
) {
    // only respond to HELLO_REPLY if previously sent HELLO message
    if (sender_address.sin_addr.s_addr != expected_a_value
        || ntohs(sender_address.sin_port) != expected_r_value) {
        // HELLO_REPLY received from an unknown sender
        print_message_error(rec_buffer, received_length); // Print error for ignored message
        return;
    }

    // Add the sender address to the list of known peers
    peer_addresses.push_back(sender_address);

    // Extract the peer count from the message
    uint16_t peer_count;
    memcpy(&peer_count, rec_buffer + 1, sizeof(peer_count));
    peer_count = ntohs(peer_count); // Convert to host byte order

    // Check if there is space for the new peers
    if (peer_addresses.size() + peer_count > UINT16_MAX) {
        cerr << "ERROR too many peers in HELLO_REPLY" << endl;
        print_message_error(rec_buffer, received_length); // Print error for ignored message
        return;
    }

    // Validate message length
    {
        size_t offset = 3;
        bool bad = false;
        for (uint16_t i = 0; i < peer_count; ++i) {
            if (offset + 1 > static_cast<size_t>(received_length)) { bad = true; break; }
            uint8_t len = static_cast<uint8_t>(rec_buffer[offset]);
            offset += 1;
            if (offset + len + sizeof(uint16_t)
                > static_cast<size_t>(received_length)) { bad = true; break; }
            offset += len + sizeof(uint16_t);
        }
        if (bad) {
            print_message_error(rec_buffer, received_length);
            return;
        }
    }

    size_t offset = 3; // Start after the message type and count
    for (size_t i = 0; i < peer_count; ++i) {
        // Extract the address length
        uint8_t peer_address_length;
        memcpy(&peer_address_length, rec_buffer + offset, 1);
        offset += 1;

        // Extract the address
        uint32_t peer_address;
        memcpy(&peer_address, rec_buffer + offset, peer_address_length);
        offset += peer_address_length;

        // Extract the port
        uint16_t peer_port;
        memcpy(&peer_port, rec_buffer + offset, sizeof(peer_port));
        offset += sizeof(peer_port);

        // Create a new sockaddr_in structure for the peer
        struct sockaddr_in peer_addr;
        memset(&peer_addr, 0, sizeof(peer_addr));
        peer_addr.sin_family = AF_INET;
        peer_addr.sin_addr.s_addr = peer_address; // Already in network byte order
        peer_addr.sin_port = peer_port;            // Already in network byte order

        // Send a CONNECT message to the peer
        send_simple_message(send_buffer, &peer_addr, socket_fd, CONNECT_MESSAGE);
    }
}

// Function that handles recieving CONNECT messages
void handle_connect_message(
    char send_buffer[],
    char rec_buffer[],
    ssize_t received_length,
    int socket_fd,
    std::vector<struct sockaddr_in>& peer_addresses,
    const struct sockaddr_in& sender_address
) {
    // Break if the sender is already in the list of known peers or list is full
    if (is_known_peer(peer_addresses, sender_address) 
        || peer_addresses.size() >= UINT16_MAX) {
        print_message_error(rec_buffer, received_length); // Print error for ignored message
        return;
    }

    // Add the sender address to the list of known peers
    peer_addresses.push_back(sender_address);

    // Send an ACK_CONNECT message to the sender
    send_simple_message(send_buffer, &sender_address, socket_fd, ACK_CONNECT_MESSAGE);
}

// Function that handles recieving ACK_CONNECT messages
void handle_ack_connect_message(
    char rec_buffer[],
    ssize_t received_length,
    std::vector<struct sockaddr_in>& peer_addresses,
    const struct sockaddr_in&        sender_address
) {
    // Break if the sender is already in the list of known peers or list is full
    if (is_known_peer(peer_addresses, sender_address)
        || peer_addresses.size() >= UINT16_MAX) {
        print_message_error(rec_buffer, received_length); // Print error for ignored message
        return;
    }

    // Add the sender address to the list of known peers
    peer_addresses.push_back(sender_address);
}

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
) {
    // Ignore the message, if already in synch phase
    if (synch_phase) {
        print_message_error(rec_buffer, received_length); // Print error for ignored message
        return;
    }

    // Save T2 timestamp
    T2_timestamp = chrono::duration_cast<chrono::milliseconds>(
        chrono::high_resolution_clock::now() - start_time).count();

    // Extract the sender's synchronization level from the message
    uint8_t sender_synch_level;
    memcpy(&sender_synch_level, rec_buffer + 1, sizeof(sender_synch_level));

    // Reset the synchronization timeout, if sender is the source
    if (is_sockaddr_equal(&sender_address, &source_address)
        && sender_synch_level == source_synch_level) {
        synch_recieve_timeout_timer = chrono::high_resolution_clock::now();
    }

    // Check synchronization conditions
    if (!check_sync_conditions(
            peer_addresses,
            sender_address,
            sender_synch_level,
            source_address,
            synch_level))
    {
        return;
    }

    // Read the T1 timestamp from the message
    memcpy(&T1_timestamp, rec_buffer + 2, sizeof(T1_timestamp));
    T1_timestamp = be64toh(T1_timestamp); // Convert to host byte order

    // Set the synch phase address address and start the synchronization phase
    synch_phase_address = sender_address;
    synch_phase = true;
    synch_phase_level = sender_synch_level;
    synch_phase_start = chrono::high_resolution_clock::now(); // Start the timer

    T3_timestamp = chrono::duration_cast<chrono::milliseconds>(
        chrono::high_resolution_clock::now() - start_time).count();

    // Send the DELAY_REQUEST message to the sender
    send_simple_message(send_buffer,
                        &sender_address,
                        socket_fd,
                        DELAY_REQUEST_MESSAGE);
}

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
) {
    // Save T4 timestamp
    int64_t timestamp = chrono::duration_cast<chrono::milliseconds>(
        chrono::high_resolution_clock::now() - start_time
    ).count();
    int64_t network_T4_timestamp = htobe64(timestamp - time_offset); // Convert to network byte order

    // Check if the sender is in the list of known peers
    if (!is_known_peer(peer_addresses, sender_address)) {
        print_message_error(rec_buffer, received_length); // Print error for ignored message
        return;
    }

    // Check if the synch level is still less than 254
    if (synch_level >= 254) {
        print_message_error(rec_buffer, received_length); // Print error for ignored message
        return;
    }

    // Prepare a DELAY_RESPONSE message
    send_buffer[0] = DELAY_RESPONSE_MESSAGE;
    send_buffer[1] = static_cast<uint8_t>(synch_level);
    memcpy(send_buffer + 2, &network_T4_timestamp, sizeof(network_T4_timestamp)); // Copy T4 timestamp

    // Send the DELAY_RESPONSE message to the sender
    ssize_t send_length = sendto(
        socket_fd,
        send_buffer,
        sizeof(network_T4_timestamp) + 2,
        0,
        reinterpret_cast<const struct sockaddr*>(&sender_address),
        sender_addr_length
    );

    // Check for errors
    if (send_length < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
        cerr << "ERROR sending DELAY_RESPONSE message" << endl;
    }
}

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
) {
    // Break if synch phase is not active or the sender isn't the synch phase address
    if (!synch_phase || !is_sockaddr_equal(&synch_phase_address, &sender_address)) {
        print_message_error(rec_buffer, received_length); // Print error for ignored message
        return;
    }

    // Extract the synchronization level from the message
    uint8_t sender_synch_level;
    memcpy(&sender_synch_level, rec_buffer + 1, sizeof(sender_synch_level));

    // Extract the T4 timestamp from the message
    memcpy(&T4_timestamp, rec_buffer + 2, sizeof(T4_timestamp));
    T4_timestamp = be64toh(T4_timestamp); // Convert to host byte order

    // Abort synchronization if the sender's synchronization level has changed
    if (synch_phase_level != sender_synch_level) {
        synch_phase = false;
        synch_phase_level = 255;
        synch_phase_address.sin_addr.s_addr = INVALID_ADDRESS;
        synch_phase_address.sin_port = INVALID_PORT;
        return;
    }

    // If the difference between T1 and T4 is greater than 5 seconds, abort the synchronization
    if (static_cast<int64_t>(T4_timestamp) - static_cast<int64_t>(T1_timestamp) > 5000) {
        synch_phase = false; // Reset the synchronization phase
        synch_level = 255;   // Reset the synchronization level
        synch_phase_address.sin_addr.s_addr = INVALID_ADDRESS;
        synch_phase_address.sin_port = INVALID_PORT;
        return;
    }

    // Calculate the time offset
    time_offset = ((T2_timestamp - T1_timestamp)
                   + (T3_timestamp - T4_timestamp)) / 2;
    source_address = sender_address;          // Set the source address
    source_synch_level = synch_phase_level;   // Set the source synchronization level
    synch_level = synch_phase_level + 1;      // Set the synchronization level
    synch_phase = false;                      // Reset the synchronization phase
    synch_phase_address.sin_addr.s_addr = INVALID_ADDRESS;
    synch_phase_address.sin_port = INVALID_PORT;
    synch_recieve_timeout_timer = chrono::high_resolution_clock::now(); // Reset the timer
}

// Function that handles recieving LEADER messages
void handle_leader_message(
    const char                                       rec_buffer[],
    ssize_t                                          received_length,
    int&                                             synch_level,
    struct sockaddr_in&                              source_address,
    uint8_t&                                         source_synch_level,
    int64_t&                                         time_offset,
    std::chrono::high_resolution_clock::time_point&  synch_send_timer
) {
    // read synchronisation value
    uint8_t synch_value;
    memcpy(&synch_value, rec_buffer + 1, sizeof(synch_value));

    if (synch_value == 0) {
        // Set the synchronization level to 0 and abort the current synchronization
        synch_level = 0;
        source_address.sin_addr.s_addr = INVALID_ADDRESS;
        source_address.sin_port = INVALID_PORT;
        source_synch_level = 0; 
        time_offset = 0;
        // Wait 2 seconds before sending START_SYNC
        synch_send_timer = chrono::high_resolution_clock::now() + chrono::seconds(3);  
    } else if (synch_value == 255 && synch_level == 0) {
        synch_level = 255;
    } else {
        print_message_error(rec_buffer, received_length);
    }
}

// Function that handles recieving GET_TIME messages
void handle_get_time_message(
    char                                         send_buffer[],
    int                                          socket_fd,
    const std::chrono::high_resolution_clock::time_point& start_time,
    int64_t                                      time_offset,
    int                                          synch_level,
    const struct sockaddr_in&                   sender_address,
    socklen_t                                    sender_addr_length
) {
    int64_t timestamp = chrono::duration_cast<chrono::milliseconds>(
        chrono::high_resolution_clock::now() - start_time).count();
    send_buffer[0] = TIME_MESSAGE; 
    send_buffer[1] = synch_level; 

    int64_t network_timestamp = htobe64(timestamp - time_offset); // Convert to network byte order
    memcpy(send_buffer + 2, &network_timestamp, sizeof(network_timestamp)); // Copy timestamp to send buffer

    // Send the TIME message to the sender
    ssize_t send_length = sendto(
        socket_fd,
        send_buffer,
        sizeof(network_timestamp) + 2,
        0,
        reinterpret_cast<const struct sockaddr*>(&sender_address),
        sender_addr_length
    );

    // Check for errors
    if (send_length < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
        cerr << "ERROR sending TIME message failed" << endl;
    }
}