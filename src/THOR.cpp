// Copyright 2025 Rishit Sharma
// Licensed under the Apache License, Version 2.0

# include "THOR.h"
#include <cstring>

    std::vector<uint8_t> THOR::Serialize(const Packet& packet) 
    {
        std::vector<uint8_t> buffer;
        buffer.reserve(sizeof(Header) + packet.payload.size());
        const uint8_t* headerPtr    = reinterpret_cast<const uint8_t*>(&packet.header);
        buffer.insert(buffer.end(), headerPtr, headerPtr + sizeof(Header));
        buffer.insert(buffer.end(), packet.payload.begin(), packet.payload.end());

        return buffer;
    }
    
    std::vector<uint8_t> THOR::SerializeHeader(const Header& header) 
    {
        std::vector<uint8_t> buffer;
        buffer.reserve(sizeof(Header));
        const uint8_t* headerPtr = reinterpret_cast<const uint8_t*>(&header);
        buffer.insert(buffer.end(), headerPtr, headerPtr + sizeof(Header));
        
        return buffer;
    }
    
    bool THOR::Deserialize(const std::vector<uint8_t>& data, Packet& outPacket) 
    {
        if (data.size() < sizeof(Header)) {
            return false; // Error: Data too short to be a valid packet
        }
        std::memcpy(&outPacket.header, data.data(), sizeof(Header));
        size_t payloadSize = data.size() - sizeof(Header);
        if (payloadSize > 0) {
            outPacket.payload.resize(payloadSize);
            std::memcpy(outPacket.payload.data(), data.data() + sizeof(Header), payloadSize);
        } else {
            outPacket.payload.clear();
        }

        return true;
    }
    
    bool THOR::DeserializeHeader(const std::vector<uint8_t>& data, Header& outheader) 
    {
        if (data.size() < sizeof(Header)) {
            return false; // Error: Data too short to be a valid packet
        }
        std::memcpy(&outheader , data.data(), sizeof(Header));
        return true;
    }
    
    std::vector<uint8_t> THOR::CreateHello(uint32_t DestId ,uint32_t SenderId, uint32_t OriginId, uint32_t Sequence)
    {
        Header header = {};

        header.senderId = SenderId;
        header.destinationId = DestId;
        header.originId = OriginId;
        header.nextHopId = BROADCAST_ID; // 0xFFFFFFFF
        header.sequence = Sequence;
        header.type = THORPacketType::HELLO;
        header.flagsAndTTL.ttl = 1;
        header.flagsAndTTL.visited = 0;
        header.flagsAndTTL.myInternet = 0;
        header.flagsAndTTL.intneighbour = 0;
        return SerializeHeader(header);
    }
    
    std::vector<uint8_t> THOR::CreateACK(uint32_t DestId, uint32_t SenderId,uint32_t OriginId,uint32_t NextHopId,uint32_t Sequence, bool myinternet, bool intneighbour)
    {
        Header header = {};
        header.senderId = SenderId;//My ID
        header.destinationId = DestId;
        header.originId = OriginId;
        header.nextHopId = NextHopId;//header.senderId in the HELLO Packet
        header.sequence = Sequence;//HELLO sequence + 1
        header.flagsAndTTL.ttl = 1;
        header.flagsAndTTL.visited = 0;
        header.type = THORPacketType::ACK;
        header.flagsAndTTL.intneighbour = intneighbour ? 1 : 0;
        header.flagsAndTTL.myInternet = myinternet ? 1 : 0;
        return SerializeHeader(header); 
    }
    
    bool THOR::HandleHello(const std::vector<uint8_t>& data, Header& outheader)
    {
        return DeserializeHeader(data, outheader);
    }
    
    bool THOR::HandleAck(const std::vector<uint8_t>& data, Header& outheader)
    {
        return DeserializeHeader(data,outheader);
    }
    
    std::vector<uint8_t> THOR::SendPacket(uint32_t DestId, uint32_t SenderId, uint32_t OriginId, uint32_t Sequence, const std::vector<uint8_t>& payload)
    {
        Header header = {};
        Packet packet = {};
        
        // 1. Setup Basic Info
        header.senderId = SenderId;
        header.destinationId = DestId;
        header.originId = OriginId;
        header.sequence = Sequence;
        header.flagsAndTTL.ttl = 15;
        header.type = THORPacketType::DATA;
        header.nextHopId = 0; // Default to 0
        header.flagsAndTTL.visited = 0;
    
        // 2. Routing Decision
        uint32_t bestHop = GetBestNextHop(); //
    
        if (bestHop != 0) {
            // --- PATH FOUND ---
            neighborTable[bestHop].isVisited = true;
            
            // Update the HEADER with the route
            header.nextHopId = bestHop; 
            header.flagsAndTTL.visited = 1;
            
            // Assign to packet NOW (after updates)
            packet.header = header; 
            packet.payload = payload;
            
            return Serialize(packet); // Send immediately
        } 
        else {
            // --- NO PATH (Store and Forward) ---
            // Even if we queue it, we should save the header state
            packet.header = header;
            packet.payload = payload;
    
            if (packetQueue.size() < 50) {
                packetQueue.push_back(packet);
            }
            return {}; // Return empty -> Stored for later.
        }
    }
    
    // Returns a Packet if we need to forward it, or an empty vector if dropped/queued/delivered.
    std::vector<uint8_t> THOR::HandleData(const std::vector<uint8_t>& data, Packet& outPacket, uint32_t MyNodeId)
    {
        if (!Deserialize(data, outPacket)) return {};

        if (outPacket.header.flagsAndTTL.ttl <= 1) {
            return {};
        }

        // 3. Am I the destination?
        if(outPacket.header.destinationId == MyNodeId)
        {
            return {};
        }
        // 4. Decrement TTL
        outPacket.header.flagsAndTTL.ttl -= 1;

        // 5. Select Best Hop (Internet -> Indirect -> Explore)
        uint32_t bestHop = GetBestNextHop();

        if (bestHop != 0) {
            neighborTable[bestHop].isVisited = true;
            // 6. Forward Accordingly
            outPacket.header.nextHopId = bestHop;
            outPacket.header.flagsAndTTL.visited = 1; // Mark path as used
            return Serialize(outPacket); // Return bytes to send immediately
        } 
        else {
            // 7. No neighbors -> Fail Gracefully (Store in Queue)
            // Check if queue is full to prevent memory leaks
            if (packetQueue.size() < 50) {
                packetQueue.push_back(outPacket);
            }
            return {}; // Return empty -> Stored for later.
        }
    }
    
    void THOR::NeighborStore(uint32_t nodeId, int rssi, bool hasInternetDirect, bool hasInternetIndirect, bool isVisited)
    {
        NeighborInfo& info = neighborTable[nodeId];
        info.lastSeen = std::time(nullptr);
        info.rssi = rssi;
        info.hasInternetDirect = hasInternetDirect;
        info.hasInternetIndirect = hasInternetIndirect;
        info.isVisited = isVisited;
    }
    
    void THOR::RemoveOld()
    {
        // Remove neighbors we haven't heard from in 30 seconds
        time_t now = std::time(nullptr);
        auto it = neighborTable.begin();

        while (it != neighborTable.end()) {
            NeighborInfo& info = it->second; // Standard syntax (No warnings)

            if (std::difftime(now, info.lastSeen) > 30.0) {
                it = neighborTable.erase(it);
            } else {
                ++it;
            }
        }
    }
    
    uint32_t THOR::GetBestNextHop() 
    {
        uint32_t bestNodeId = 0;
        int maxScore = -1; 
        if (neighborTable.empty()) return 0;
        for (auto const& entry : neighborTable) {
            
            uint32_t id = entry.first;
            const NeighborInfo& info = entry.second;

            int currentScore = 0;

            // --- PRIORITY 1: DIRECT INTERNET  ---
            if (info.hasInternetDirect) {
                currentScore = 300; 
            }
            // --- PRIORITY 2: INDIRECT INTERNET  ---
            else if (info.hasInternetIndirect) {
                currentScore = 200; 
            }
            // --- PRIORITY 3: EXPLORATION  ---
            else {
                if (info.isVisited) {
                    currentScore = 10;
                } else {
                    currentScore = 100;
                }
            }
            
            if (info.rssi > -50) {
                currentScore -= 50;
            }

            else if (info.rssi <= -50 && info.rssi >= -80) {
                currentScore += 50;
            }
            else {
                 currentScore -= 20;
            }
            if (currentScore > maxScore) {
                maxScore = currentScore;
                bestNodeId = id;
            }
        }
        return (maxScore == -1) ? 0 : bestNodeId;
    }
    
    // Returns a list of serialized packets ready to be sent via Bluetooth
    std::vector<std::vector<uint8_t>> THOR::ProcessQueue() //Android Wrapper Endpoint function
    {
        // 1. If queue is empty, nothing to do.
        if (packetQueue.empty()) {
            return {}; 
        }

        // 2. Check if we have a valid target NOW
        uint32_t bestHop = GetBestNextHop();

        // 3. If still no neighbors (result is 0), keep waiting.
        if (bestHop == 0) {
            return {};
        }

        // 4. We have a target! Prepare the batch.
        std::vector<std::vector<uint8_t>> batchToSend;

        // Mark the neighbor as "busy" for this transaction
        neighborTable[bestHop].isVisited = true;

        for (auto& packet : packetQueue) {
            // Update the routing info
            packet.header.nextHopId = bestHop;

            // Mark as visited so we don't loop back immediately
            packet.header.flagsAndTTL.visited = 1;

            // Serialize and add to batch
            batchToSend.push_back(Serialize(packet));
        }

        // 5. Clear the internal queue since we are handing them off to the wrapper
        packetQueue.clear();

        return batchToSend;
    }
