# THOR: Two-Hop Opportunistic Routing

THOR is a routing protocol for delay-tolerant networks over Bluetooth Low Energy (BLE). It is meant for disaster relief situations where cellular and internet infrastructure is down, and it lets phones and small embedded devices pass messages to each other.

This repository holds two versions of the design:

| Version | Status | Where to find it |
|---|---|---|
| **V1** | Released | [V1 (Released)](#v1-released) |
| **V2** | Under development, not released | [V2 (In Development)](#v2-in-development) |

V1 is the code in this repository today. V2 is the redesign I am building. Everything under the V2 heading describes the planned design, and only part of it is implemented. See [Architecture Status](#architecture-status) for what is done.

---

# V1 (Released)

V1 is a C++17 library with no external dependencies. It routes packets toward nodes that have internet access, and it stores packets when no next hop is available.

## How V1 Works

### 1. Next-Hop Selection (Most Forward within Radius)
Always picking the strongest signal (for example RSSI -30 dBm) tends to produce short hops, so a packet needs more hops to get anywhere. V1 uses a Most Forward within Radius (MFR) heuristic instead.
* It lowers the score of signals stronger than -50 dBm so that longer hops are preferred.
* The preferred range is -50 to -80 dBm.
* The goal is to reduce the number of hops to the destination.

### 2. Routing Toward Internet-Connected Nodes
Each node broadcasts two flags, `myInternet` (this node has internet) and `intneighbour` (one of its neighbours has internet). That gives every node two-hop visibility of where internet access is. Neighbours are ranked by a fixed priority:
1. **Direct internet** (score 300)
2. **Indirect internet**, meaning a neighbour has internet (score 200)
3. **Exploration** (score 100 plus an RSSI bonus)

### 3. Store-and-Forward Queue
In a disaster area a path to the destination often does not exist yet.
* If no valid next hop is found, the packet goes into a store-and-forward queue instead of being dropped.
* The node holds the packet until a valid neighbour appears or a mobile device comes within range, so the node acts as a data mule.

### 4. Packet Format
V1 uses a fixed binary header instead of JSON or strings.
* **Header size:** fixed at 22 bytes.
* **Serialization:** raw byte packing with `reinterpret_cast`, and bit-fields for the flags.
* **Memory:** the queue has a fixed size limit, which keeps heap usage bounded on small devices.

### 5. Route Locking (Visited Logic)
V1 avoids routing loops without a routing table by using a transaction-based lock.
* **Lock:** when a node forwards a packet, it sets the `visited` flag to 1 for the chosen next hop. That neighbour is treated as busy, which prevents backtracking and loops during that transaction.
* **Unlock:** the flag goes back to 0 only when a Final ACK arrives from the destination.
* **Result:** if a route hits a dead end, the path stays locked, so the same failed link is not retried. If delivery succeeds, the ACK travels back, unlocks the nodes, and confirms the path.

## V1 Technical Details

### Packet Structure
The header is packed tightly so more of a standard 31-byte BLE advertisement is left for payload.

```cpp
enum class THORPacketType : uint8_t {
    HELLO = 1,
    ACK   = 2,
    DATA  = 3
};

struct flags{
    unsigned char ttl : 5;          // time to live
    unsigned char intneighbour : 1; // 1 = neighbour has internet, 0 = no internet neighbour.
    unsigned char visited : 1;      // 1 = visited, 0 = not visited
    unsigned char myInternet : 1;   //
};

struct Header {
    THORPacketType type;       // 1 byte

    flags  flagsAndTTL;        // 1 byte (Renamed from 'ttl')
    uint32_t destinationId;    // 4 bytes
    uint32_t senderId;         // 4 bytes
    uint32_t originId;         // 4 bytes
    uint32_t nextHopId;        // 4 bytes
    uint32_t sequence;         // 4 bytes
}; // total : 22 bytes
```

### State Machine
The core logic in `THOR.cpp` moves a packet through four stages:
1.  **Discovery:** periodic `HELLO` broadcasts fill the neighbour table.
2.  **Scoring:** each neighbour is re-scored from its RSSI and flags.
3.  **Queueing:** when no next hop is found (a dead end), the packet goes into the queue.
4.  **Forwarding:** the route stays locked (`visited` flag) until an ACK confirms delivery.

## Usage & Simulation
The library does not depend on a platform. The core logic does the routing, and a wrapper (Android or ESP32) drives the radio.

To check the routing logic without hardware, build and run the included C++ simulation.

### 1. Build the Simulation
```bash
g++ examples/simulation.cpp src/THOR.cpp -I src -o thor_test
```
### 2. Run the Test
```bash
   ./thor_test
```
## Expected Output

The simulation shows a node failing to find a route, queueing the packet, discovering a neighbour, and then flushing the queue.

```bash
========== STEP 1: Node A creates a DATA packet but has no neighbors ==========
Node A queued packet (no route yet)

========== STEP 2: Node B appears and sends HELLO ==========
Node A discovered Node B (RSSI -65, no internet)

========== STEP 3: Node B discovers Node C with Internet ==========
Node B discovered Node C (RSSI -72, DIRECT internet)

========== STEP 4: Node B ACKs A and informs it that C exists (indirect internet) ==========
Node A learns: Node B has a neighbor with Internet.

========== STEP 5: Node A flushes queue. Best hop should be B (indirect internet). ==========
Node A forwarded packet to B:
[ 03 4E 0F 27 00 00 01 00 00 00 01 00 00 00 02 00 00 00 01 00 00 00 48 65 6C 70 20 4D 65 ]

========== STEP 6: Node B forwards to C using Internet Gravity ==========
Node B forwarded packet to Node C:
[ 03 4D 0F 27 00 00 01 00 00 00 01 00 00 00 03 00 00 00 01 00 00 00 48 65 6C 70 20 4D 65 ]

========== STEP 7: Node C sends ACK → resets visited bits (success path) ==========
Node B resets visited state after successful delivery.

========== FINAL: THOR Simulation Complete ==========
All routing stages successfully simulated.
```

## Project Structure

* src/THOR.cpp - the core protocol logic (routing, queueing, serialization).

* src/THOR.h - header definitions and packet structs.

* examples/simulation.cpp - a command-line simulation for checking the logic.

* docs/ - architecture notes and planning sketches.

---

# V2 (In Development)

V2 is a redesign of the protocol. It is not released and is not in this repository's released code. The design below is what I am building toward, and parts of it are still unspecified or unimplemented (see [Architecture Status](#architecture-status)).

V1 sends packets toward internet-connected nodes and holds them when it has no next hop. V2 keeps the neighbour table and store-and-forward behaviour, and adds a discovery procedure controlled by sequence numbers, per-transaction locks with timers, a state machine with Initiator and Acceptor roles, retransmission with failure tracking, fragmentation, and three routing modes that depend on neighbour density.

## V2 Overview

V2 is a transaction-based protocol for BLE. It is meant for areas where cellular and internet service is unavailable, unreliable, or interrupted. Instead of flooding messages across the network, a node discovers its neighbours locally, picks a next hop using its neighbour table, and sets up a temporary path for each transfer. Its forwarding behaviour changes depending on how many neighbours a node can see.

## V2 Features

### A. Greedy Opportunistic Routing
- Discovers one-hop neighbours using HELLO messages.
- Keeps a neighbour table with each neighbour's RSSI, connectivity information, visit history, and failure count.
- Chooses forwarding candidates using rules based on the neighbour table.
- Forwards along a single path, going deeper node by node, instead of sending packets to every node.
- Ranks candidates by connection quality and whether they have internet access.

### B. Odd/Even Sequence-Based Discovery
- Uses the parity of the sequence number to control how far route discovery goes and when it stops.
- **Odd sequence numbers:** the current node may continue discovery to the next node.
- **Even sequence numbers:** discovery stops here, and the receiving node returns the sequence response to the node before it.
- The returning even sequence tells the previous node that this discovery branch has ended.
- The intent is to limit discovery and signal the return without flooding the network.

Parity is used to tell "continue" from "stop," so no separate discovery-control field is needed. How the sequence number increments, how branches are identified, and how duplicate responses are handled are not yet specified.

### C. Transaction-Based Route Locking
- Groups each forwarding attempt into a temporary transaction.
- Uses Initiator and Acceptor roles to set up the route.
- Intermediate nodes take a temporary lock while a transaction is active.
- The lock keeps other forwarding attempts from interfering with an established path.
- Locks are released in an explicit disconnect and cleanup phase.

### D. Lease Timers and Heartbeats
- Each participating node has a timer that expires its lock.
- Lease extensions depend on how far the transaction has progressed and the estimated hop distance.
- Downstream nodes send heartbeats backwards along the established path.
- Heartbeats extend the lock so it does not expire during longer transactions.
- If a transaction stalls, the timeout releases its locks.

### E. Role-Based Finite State Machine

V2 keeps transaction roles separate from protocol states.

**Roles:**
- `Initiator`: starts a transaction.
- `Acceptor`: receives and takes part in an incoming transaction.

**States:**
1. `InitHello`: the Initiator begins discovery.
2. `DiscoverHello`: the Acceptor processes incoming discovery.
3. `AckReceive`: handles acknowledgement progression.
4. `WaitForConfirm`: waits for a confirmation, a heartbeat, or another transaction event.
5. `Disconnecting`: sends the final acknowledgement and releases the path.
6. `Disconnected`: resets transaction state so the node can start the next one.

State transitions are driven by events. Timeouts, exhausted retries, received acknowledgements, and transaction completion cause transitions, so there is no separate state for each failure case.

### F. Reliability and Retransmission
- Uses acknowledgements and timers to track whether packets arrived.
- Detects missing acknowledgements and starts retransmission.
- Records each neighbour's transmissions as success or failure.
- Repeated failures increase that neighbour's failure count.
- Later routing decisions can rank that neighbour lower or try another path.
- The count can change over time, so one failed transmission does not exclude a node for good.

### G. MTU-Aware Fragmentation
- Uses a custom 22-byte binary packet header.
- Takes the BLE ATT MTU and transport overhead into account.
- Calculates the maximum payload and fragmentation limits from the device configuration.
- Fragments payloads that exceed the available capacity and reassembles them at the receiver.
- Intended to work between Android devices and ESP32-based nodes.

### H. Routing Modes Based on Neighbour Density

V2 picks its routing behaviour based on how many neighbours a node can see and what forwarding options are available.

**Dense Mode**
- Runs the normal greedy routing algorithm.
- Uses neighbour discovery and next-hop selection from the neighbour table.
- Uses transaction locking, heartbeats with lease extension, and failure-count-based forwarding.
- Can choose among several available forwarding candidates.

**Sparse Mode**
- Relaxes the normal forwarding restrictions.
- Favours store-and-forward: the node keeps the message until a forwarding opportunity appears.
- Lets mobile devices carry messages between areas (data mules).
- Holds messages until a suitable neighbour is in range.

**Emergency Mode**
- Activates when a node has very few or no neighbours.
- Prefers handing the message off quickly over picking the best next hop.
- Lets an emergency message go to any available neighbour without waiting for the best candidate.
- Marks the packet with an emergency flag.
- Each node along the delivery chain keeps using emergency forwarding for that message, overriding the normal routing preferences for that transaction.

### I. Static and Mobile Nodes
- Supports mobile Android devices together with stationary ESP32-based relay nodes.
- ESP32 devices can be placed at locations such as streetlights, shelters, hospitals, tunnels, and disaster-prone areas.
- Stationary relays store messages and offer forwarding opportunities.
- Passing mobile devices can exchange messages with these relays.
- Combining mobile devices with fixed relays gives messages more nodes to reach and more places to be stored.

### J. Library Structure and Configuration
- Written as a C++17 protocol library.
- Used on Android through JNI and Kotlin, and on ESP32 as embedded code.
- Keeps protocol definitions, compile-time constants, runtime configuration, and transaction state separate.
- Reads platform-specific parameters from an INI file.
- Uses fixed-size data structures and avoids dynamic allocation where possible.

## V2 Summary

V2 discovers neighbours locally, chooses a next hop greedily, limits discovery with sequence parity, sets up a temporary locked path for each transaction, extends leases with heartbeats, tracks neighbour failures, and switches between dense, sparse, and emergency modes by neighbour count. It tries to avoid flooding while still getting messages through networks where connections come and go.

It is meant to work across Android phones and ESP32 boards, with optional fixed relays. It does not aim for the best possible routes. It aims to deliver messages over BLE when connectivity, available devices, and infrastructure cannot be assumed.

---

## Future Roadmap

* Android Integration: wrap the C++ core in JNI for a BluetoothLeScanner implementation.

* Priority-Based Reinforcement for Successful Paths: instead of using visited = {0,1}, a later version can give each neighbour a score that rises on success:

  ```cpp
  neighborTable[id].successScore += 1
  ```
  This means:

	•	Nodes that previously delivered packets successfully get a boost

	•	Over time, routes that keep failing get used less

* Duplicate Packet Suppression:
  A mechanism to detect and drop duplicate packets is still needed. In dynamic or dense networks, the same packet may arrive more than once because of mobility or overlapping transmission ranges. Without suppression, THOR may forward these duplicates repeatedly, causing extra congestion, battery drain, and temporary routing loops. A planned fix is a short "recent packet history" so each node can recognize and discard packets it has already seen.

## Architecture Status

This protocol is currently in active development. The core deterministic routing engine, MTU clamping, and memory-safe state machine are fully implemented and stable for testing. 

### Implementation State

**Core Engine (Stable)**
- [x] Scoring Engine (-50dBm optimization)
- [x] Custom 22-byte bit-packed header serialization
- [x] Memory-bounded Store-and-Forward Queue
- [x] MTU Clamping and hardware constraints

**Pending Infrastructure**
- [ ] State Machine architecture for Initiator and Acceptor states (Partially Completed)
- [ ] 3 Different Modes of packet delivery based on the neighbour traffic density (In Progress)
- [ ] Payload Fragmentation (`fragmentedHeader` structs defined; queue integration pending)
- [ ] Transaction timeout mechanisms for dropped ACKs
- [ ] Duplicate Packet Suppression
      

## NOTE:

THOR v1 assumes homogeneous architecture and compiler across all nodes.
Wire format is not guaranteed to be portable across heterogeneous systems.
  
## License

This project is licensed under the Apache License, Version 2.0. This ensures that any derivative works or apps built using THOR must also remain open-source, protecting the project's mission for public safety.
