# THOR: Two-Hop Opportunistic Routing Protocol

**THOR (Two-Hop Opportunistic Routing)** is a lightweight, delay-tolerant network (DTN) protocol designed for **Disaster Relief** scenarios where internet infrastructure has collapsed.

Unlike standard mesh libraries that rely on battery-draining flooding or static routing tables, THOR uses a custom **Gradient-Based Heuristic** to route packets specifically toward internet-connected nodes ("Data Mules"). It is built from scratch in C++17 with zero external dependencies, optimized for the bandwidth constraints of Bluetooth Low Energy (BLE).

## Key Engineering Features

### 1. Physics-Aware Routing ("The Goldilocks Zone")
Most mesh protocols greedily connect to the strongest signal (e.g., RSSI -30 dBm). This results in short hops, high latency, and excessive battery usage.
* **The Innovation:** THOR implements a **Most Forward within Radius (MFR)** heuristic.
* **The Logic:** It actively penalizes signals that are "too strong" (>-50 dBm) to force the protocol to take longer, more efficient leaps (optimal range: -50 to -80 dBm). This minimizes the total hop count to reach the destination.

### 2. Internet Gravity (Gradient Routing)
The protocol creates a virtual "potential field" where packets naturally flow toward connectivity.
* Nodes broadcast a **2-Hop Visibility Flag** (`myInternet` and `intneighbour`).
* Packets are routed based on a strict priority gradient:
    1.  **Direct Internet** (Score: 300)
    2.  **Indirect Internet** (Score: 200)
    3.  **Exploration/MFR** (Score: 100 + RSSI Bonus)

### 3. Store-and-Forward Architecture (Data Mule)
In disaster zones, a path to the destination often does not exist *yet*.
* THOR treats **Time** as a routing dimension.
* If no valid next hop is found, packets are not dropped. They are moved to an internal **Store-and-Forward Queue**.
* The node acts as a "Data Mule," physically carrying the packet until a valid neighbor appears or a rescuer walks by.

### 4. Bit-Level Efficiency
Designed for embedded constraints, the protocol avoids JSON or string-based overhead.
* **Header Size:** Fixed **22 Bytes**.
* **Serialization:** Custom raw-byte packing using `reinterpret_cast` and bit-fields for flags.
* **Memory Management:** Manual queue limit handling to prevent heap exhaustion on constrained devices.

### 5. Route Verification & Locking (Visited Logic)
To prevent loops and ensure path validity without heavy routing tables, THOR uses a **Transaction-Based Locking mechanism**.
* **The Lock**: When a node forwards a packet, it marks the next hop as visited (sets a flag to 1). This effectively "locks" that neighbor, treating them as a busy path to prevent immediate backtracking or routing loops during that specific transaction.
* **The Key**: The flag is reset to 0 only upon receiving a Final ACK from the ultimate destination.
* **The Result**: If a route hits a dead end, the path remains locked (preventing retries on a failed link). If the route succeeds, the ACK propagates back, "unlocking" the nodes and confirming the path is valid for future traffic.

## Technical Architecture

### Packet Structure
The protocol uses a tightly packed struct to maximize payload availability in standard 31-byte BLE Advertisement packets.

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
The core logic (`THOR.cpp`) manages the lifecycle of a packet through four states:
1.  **Discovery:** Periodic `HELLO` broadcasts to populate the Neighbor Table.
2.  **Scoring:** Every neighbor is re-scored dynamically based on RSSI and Flags.
3.  **Queueing:** "Dead End" detection triggers the internal queue mechanism.
4.  **Forwarding:** "Locking" a route (`visited` flag) until an ACK confirms delivery.

## Usage & Simulation
This library is platform-agnostic. The core logic handles the routing, while the wrapper (Android/ESP32) handles the physical radio.

To verify the routing logic without hardware, run the included C++ simulation:

### 1. Build the Simulation
```bash
g++ examples/simulation.cpp src/THOR.cpp -I src -o thor_test
```
### 2. Run the Test
```bash
   ./thor_test
```
## Expected Output

The simulation demonstrates a node failing to find a route, queueing the packet, discovering a neighbor, and successfully flushing the queue.

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

* src/THOR.cpp - The core protocol logic (Routing, Queueing, Serialization).

* src/THOR.h - Header definitions and packet structs.

* examples/simulation.cpp - Proof-of-Concept CLI tool to verify logic.

* docs/ - Architectural notes and planning sketches.

## Future Roadmap

* Android Integration: Wrapping the C++ core in JNI for a BluetoothLeScanner implementation.

* Priority-Based Reinforcement for Successful Paths: Instead of using visited = {0,1}, THOR v2 can assign a dynamic priority score to each neighbor:

  ```cpp
  neighborTable[id].successScore += 1
  ```
  This means:
  
	•	Nodes that previously delivered packets successfully get a boost
	•	Over time, THOR stops using bad routes naturally
	•	This evolves into a reinforcement-learning routing protocol

* Duplicate Packet Suppression:
  A key improvement needed in THOR is a mechanism to detect and drop duplicate packets. In dynamic or dense networks, the same packet may arrive multiple times due to mobility or overlapping transmission ranges. Without suppression, THOR may forward these duplicates repeatedly, causing unnecessary congestion, battery drain, and temporary routing loops. Future versions will incorporate a lightweight “recent packet history” that allows each node to recognize and discard previously seen packets, ensuring cleaner routing behavior and improved network stability.
  
## License

This project is licensed under the Apache License, Version 2.0. This ensures that any derivative works or apps built using THOR must also remain open-source, protecting the project's mission for public safety.
