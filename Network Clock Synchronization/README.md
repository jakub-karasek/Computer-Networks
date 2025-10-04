## Network Clock Synchronization

Implement a program for synchronizing clocks in a peer-to-peer model.
The clock synchronization network consists of equal nodes. Each node communicates with other nodes to synchronize its clock, taking into account the packet travel time in the network.

### Operation of the clock synchronization network includes the following activities:

* joining the network by contacting another node,
* selecting a leader to which other nodes synchronize their clocks,
* synchronizing clocks while accounting for packet transmission delays.

Each node maintains its natural clock, which is the number of milliseconds since its startup.

The **synchronization level** of a node is:

* `255` when the node is not synchronized with any node,
* `0` when the node is the source of synchronization for other nodes (it is the leader),
* `1` when the node is synchronized with the leader,
* `2` when the node is synchronized with a node that is synchronized with the leader, and so on — up to a maximum value of `254`.

Each node stores its synchronization level.
The initial synchronization level of a node is `255`.
A node remembers which other node it is synchronized with.

Nodes communicate using **UDP** and **IPv4 addressing**.

---

## Node Program Parameters

The program implementing the node functionality accepts the following command-line parameters:

* `-b bind_address` – IP address on which the node listens (optional; by default, it listens on all addresses of the host),
* `-p port` – port on which the node listens; integer in the range 0–65535 (0 means any available port, optional; default is 0),
* `-a peer_address` – IP address or hostname of another node to contact (optional),
* `-r peer_port` – port of another node to contact; integer in the range 1–65535 (optional).

Parameters may appear in any order. Parameters `-a` and `-r` must both be provided.
If a parameter appears multiple times, the program’s behavior should be reasonable.

---

## Messages Exchanged Between Nodes

The following fields may appear in messages exchanged between nodes:

* `message` – 1 octet, message type,
* `count` – 2 octets, number of known nodes,
* `peer_address_length` – 1 octet, number of octets in `peer_address`,
* `peer_address` – `peer_address_length` octets, the IP address of the node (as in the IP header),
* `peer_port` – 2 octets, port number on which the node listens,
* `timestamp` – 8 octets, clock value,
* `synchronized` – 1 octet, synchronization level of the node.

Values in multi-octet binary fields are stored in **network byte order**.

Nodes send messages from the port on which they listen.
Nodes obtain the sender’s IP address and port number from the network layer.
The IP address and port number uniquely identify a node.

---

## Joining the Network

A node may join the network in two ways:

1. If started **without** parameters `-a` and `-r`, it listens for messages and waits for a new participant.
2. If started **with** parameters `-a` and `-r`, it sends a **HELLO** message to the specified node.

Messages exchanged during this stage:

* `HELLO` – `message = 1`,
* `HELLO_REPLY` – `message = 2`, `count` = number of records containing information about nodes known to the responding node, followed by these records (each containing `peer_address_length`, `peer_address`, `peer_port`),
* `CONNECT` – `message = 3`,
* `ACK_CONNECT` – `message = 4`.

The **HELLO** message informs another node of the desire to establish communication.

The **HELLO_REPLY** message is the response to a HELLO.
It informs the new node about other active nodes in the network.
The sender and receiver of this message are not included in the transmitted list.
The new node then sends a **CONNECT** message to each known node in order to establish communication.

The **CONNECT** message indicates an attempt to establish communication.
The node receiving CONNECT adds the sender to its list of known nodes.

The **ACK_CONNECT** message is a response to CONNECT.
It confirms that communication has been established.
A node adds to its list any nodes that acknowledge connection.

Nodes that have exchanged **HELLO** and **HELLO_REPLY** messages are considered connected and do not exchange **CONNECT** or **ACK_CONNECT**.

---

## Time Synchronization

The clock synchronization process involves exchanging three messages:

* `SYNC_START` – `message = 11`, `synchronized`, `timestamp`,
* `DELAY_REQUEST` – `message = 12`,
* `DELAY_RESPONSE` – `message = 13`, `synchronized`, `timestamp`.

The **SYNC_START** message is sent periodically by all nodes whose synchronization level is less than 254, to all nodes they know.
This message contains the synchronization level and the current clock value **T1** of the sender.

A node attempts synchronization and responds to SYNC_START only if:

* the sender is known,
* the sender’s synchronization level is less than 254,
* the sender’s synchronization level is less than the receiver’s synchronization level if the receiver is synchronized with the sender,
* the sender’s synchronization level is at least two less than the receiver’s synchronization level if the receiver is **not** synchronized with the sender.

A node continues synchronization only with the first node that meets the above conditions.

If a node does not receive a SYNC_START message from the node it is synchronized with for 20–30 seconds,
or receives such a message with a `synchronized` value greater than or equal to its own level,
it resets its synchronization level to 255.

If a node receives SYNC_START and continues synchronization, it records its clock value **T2** upon reception,
sends a **DELAY_REQUEST**, and records its clock value **T3** upon sending it.

The node that started synchronization, upon receiving DELAY_REQUEST, sends a **DELAY_RESPONSE** containing its synchronization level and clock value **T4** at the time it received DELAY_REQUEST.

The synchronized node computes:

```
offset = (T2 - T1 + T3 - T4) / 2
```

From that moment, the synchronized node’s synchronization level is **one greater** than the `synchronized` value received in SYNC_START and DELAY_RESPONSE (these must match).

Nodes should send SYNC_START to all known nodes every **5 to 10 seconds**.
If a node does not receive DELAY_REQUEST or DELAY_RESPONSE within **5 to 10 seconds**,
it aborts synchronization and ignores messages received after that time.

If the node sending T1 and T4 values has its clock synchronized with another node, it sends the **synchronized time** values.

---

## Leader Election

To start synchronization, at least one node must become a leader.
This is done via the **LEADER** message:

* `LEADER` – `message = 21`, `synchronized`.

If `synchronized = 0`, the node becomes a leader (sets its synchronization level to 0) and, after two seconds, begins sending synchronization messages to other known nodes.

If `synchronized = 255` and the node **is** a leader, it ceases to be one (sets level to 255) and stops sending synchronization messages, but completes already started exchanges.

Other values in the `synchronized` field are invalid.
A `LEADER` message with `synchronized = 255` received by a non-leader node is also invalid.

A node should respond to every **LEADER** message from the very start of its operation, without verifying the sender.

---

## Providing Current Time

Each node should provide information about its current time using the following messages:

* `GET_TIME` – `message = 31`,
* `TIME` – `message = 32`, `synchronized`, `timestamp`.

`GET_TIME` requests the current time.
`TIME` is the response and contains the synchronization level and the node’s natural clock value if it is unsynchronized,
or the corrected clock value (adjusted by subtracting the offset) if synchronized.

A node should respond to every **GET_TIME** message from the start, without verifying the sender.

---

## Error Handling

The program should carefully validate parameters and print detailed error messages to **standard error**, exiting with code 1.

System or library call errors should be reported to standard error as well.
If the error prevents further operation, the program must exit with code 1.
Errors that do not prevent operation (e.g., invalid or ignored messages) should not stop the program.
A message should also be treated as invalid if its sender is unknown (except for the exceptions above),
if it is unexpected in the current communication state,
or if it contains an invalid or unexpected field value.

Error messages printed to standard error should begin with `ERROR`.
Messages about invalid packets should begin with `ERROR MSG`, followed by a space and up to the first 10 bytes of the message in hexadecimal, e.g.:

```
ERROR MSG 7a12c534
```

---

## Additional Notes

The size of the `count` field in the HELLO_REPLY message limits the number of nodes to 65,535.
This should be treated as an upper limit — messages exceeding this limit should be ignored.
If a HELLO_REPLY message is too large to be sent, it should be dropped and treated like any other ignored message.

A HELLO_REPLY message should be considered invalid if:

* it does not contain exactly `count` records,
* any `peer_address_length` or `peer_port` field is invalid,
* the sender or receiver appears in the node list.

---

## Solution

The solution should be implemented in **C or C++** using **socket interfaces**.
No external libraries for network communication may be used.
A single-threaded solution is expected, but communication with one node must not block communication with others.
The program must compile and run in the computer lab.

Programs must follow good programming practices.
The absence of obvious requirements (e.g., not formatting disks, checking system call return values) does not mean that noncompliant programs will be accepted.
Program code will also be evaluated.
The protocol will be tested rigorously.

The submitted solution must be an archive containing all files necessary to build the program.
Do not include binaries or unnecessary files.
The archive must be created using **zip**, **rar**, **7z**, or **tar + gzip**,
with an extension `.zip`, `.rar`, `.7z`, or `.tgz`.

After extraction, all files should be in a single directory (no subdirectories).
The archive must include a **makefile** or **Makefile**.
Running `make` should produce an executable named **peer-time-sync**.
Running `make clean` should remove all build artifacts.
