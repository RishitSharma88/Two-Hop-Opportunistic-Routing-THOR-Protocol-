// Copyright 2025 Rishit Sharma
// Licensed under the Apache License, Version 2.0
/*
 * This simulation demonstrates:
 *  - Queueing when no neighbors exist
 *  - Discovery of direct and indirect neighbors
 *  - Internet Gravity routing
 *  - RSSI scoring & hop selection
 *  - Backtrack logic (visited bits)
 *  - Two-hop inference via delayed ACK
 *  - TTL handling
 *  - Multi-hop forwarding
 */

#include <iostream>
#include "THOR.h"

// Helper to print hex bytes
void PrintPacket(const std::vector<uint8_t>& data) {
    std::cout << "[ ";
    for(auto b : data) printf("%02X ", b);
    std::cout << "]\n";
}

// Helper to label steps cleanly
void Step(const std::string& name) {
    std::cout << "\n========== " << name << " ==========\n";
}

int main() {

    //---------------------------------------------------------------
    // Create 3 Nodes:
    //  Node A → Victim (no internet)
    //  Node B → Mule (intermediate relay, NO internet)
    //  Node C → Gateway (HAS internet)
    //---------------------------------------------------------------

    THOR nodeA;
    THOR nodeB;
    THOR nodeC;

    //---------------------------------------------------------------
    // STEP 1: Node A wants to send "Help Me" to Internet (ID 9999)
    //---------------------------------------------------------------

    Step("STEP 1: Node A creates a DATA packet but has no neighbors");

    std::string msg = "Help Me";
    std::vector<uint8_t> payload(msg.begin(), msg.end());

    auto packetBytes = nodeA.SendPacket(
        9999,   // destination (internet)
        1,      // senderId
        1,      // originId
        0,      // nextHop (unknown)
        1,      // sequence
        payload
    );

    Packet parsed;
    nodeA.Deserialize(packetBytes, parsed);

    // Try routing (should queue because no neighbors exist)
    auto firstAttempt = nodeA.HandleData(packetBytes, parsed, 1);

    if(firstAttempt.empty())
        std::cout << "Node A queued packet (no route yet)\n";
    else
        std::cout << "ERROR: Node A should not forward yet!\n";


    //---------------------------------------------------------------
    // STEP 2: Node B appears (Intermediate Relay, No Internet)
    //---------------------------------------------------------------

    Step("STEP 2: Node B appears and sends HELLO");

    // Node B → HELLO
    auto helloB = nodeB.CreateHello(0, 2, 2, 10);

    Header hdrB;
    nodeA.HandleHello(helloB, hdrB);

    // Store B as neighbor of A
    nodeA.NeighborStore(2, -65, false, false, false); // RSSI -65 → ideal range

    std::cout << "Node A discovered Node B (RSSI -65, no internet)\n";

    //---------------------------------------------------------------
    // STEP 3: Node B discovers Node C (Internet Gateway)
    //---------------------------------------------------------------

    Step("STEP 3: Node B discovers Node C with Internet");

    auto helloC = nodeC.CreateHello(0, 3, 3, 20);

    Header hdrC;
    nodeB.HandleHello(helloC, hdrC);

    // Store C as neighbor of B
    nodeB.NeighborStore(3, -72, true, false, false); // C has internet

    std::cout << "Node B discovered Node C (RSSI -72, DIRECT internet)\n";

    //---------------------------------------------------------------
    // STEP 4: Node B sends ACK to A → 2-hop learning happens here
    //---------------------------------------------------------------

    Step("STEP 4: Node B ACKs A and informs it that C exists (indirect internet)");

    auto ackFromB = nodeB.CreateACK(
        1,       // Dest: A
        2,       // Sender: B
        2,       // Origin
        1,       // NextHop (A)
        11,      // Sequence
        false,   // myInternet(B)
        true     // intNeighbour (C)
    );

    Header ackHeader;
    nodeA.HandleAck(ackFromB, ackHeader);

    // Update Node A’s neighbor table: B has INDIRECT internet
    nodeA.NeighborStore(2, -65, false, true, false);

    std::cout << "Node A learns: Node B has a neighbor with Internet.\n";


    //---------------------------------------------------------------
    // STEP 5: Node A Flushes Queue → Should Forward to B
    //---------------------------------------------------------------
    
    Step("STEP 5: Node A flushes queue. Best hop should be B (indirect internet).");

    auto batch1 = nodeA.ProcessQueue();

    if(!batch1.empty()) {
        std::cout << "Node A forwarded packet to B:\n";
        PrintPacket(batch1[0]);
    } else {
        std::cout << "ERROR: Queue did not flush!\n";
    }


    //---------------------------------------------------------------
    // STEP 6: Node B now forwards packet to C (internet)
    //---------------------------------------------------------------

    Step("STEP 6: Node B forwards to C using Internet Gravity");

    Packet pktAtB;
    nodeB.Deserialize(batch1[0], pktAtB);

    auto forwardToC = nodeB.HandleData(batch1[0], pktAtB, 2);

    if(!forwardToC.empty()) {
        std::cout << "Node B forwarded packet to Node C:\n";
        PrintPacket(forwardToC);
    } else {
        std::cout << "ERROR: B should have forwarded to internet node C!\n";
    }

    //---------------------------------------------------------------
    // STEP 7: Node C sends ACK back → resets visited bits (Backtrack logic)
    //---------------------------------------------------------------

    Step("STEP 7: Node C sends ACK → resets visited bits (success path)");

    auto ackFromC = nodeC.CreateACK(
        1,      // original sender is A
        3,      // C (gateway)
        3,
        2,      // Next hop is B
        30,     // seq
        true,   // myInternet
        false
    );

    Header hdrAckC;
    nodeB.HandleAck(ackFromC, hdrAckC);

    // Reset visited bit for B
    nodeB.NeighborStore(3, -72, true, false, false);

    std::cout << "Node B resets visited state after successful delivery.\n";


    //---------------------------------------------------------------
    // FINAL: Simulation complete
    //---------------------------------------------------------------

    Step("FINAL: THOR Simulation Complete");
    std::cout << "All routing stages successfully simulated.\n";

    return 0;
}
