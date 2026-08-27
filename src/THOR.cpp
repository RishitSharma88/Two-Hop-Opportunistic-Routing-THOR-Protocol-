// Copyright 2025 Rishit Sharma
// Licensed under the Apache License, Version 2.0

#include "THOR.h"
#include <cstring>
#include <fstream>
#include <iostream>
#include <algorithm>
#include <cctype>

    static std::string trimString(const std::string& s) {
        auto start = s.find_first_not_of(" \t\r\n");
        if (start == std::string::npos) return "";
        auto end = s.find_last_not_of(" \t\r\n");
        return s.substr(start, end - start + 1);
    }

    bool THOR::ParseRoleConfig(const std::string &filename)
    {
        std::ifstream infile(filename);
        if (!infile.is_open()) {
            infile.open("config.ini");
        }

        if (!infile.is_open()) {
            std::cerr << "[THOR] Warning: Could not open config file: " << filename << std::endl;
            isInitiator = false;
            return isInitiator;
        }

        std::string line;
        std::string currentSection = "";

        while (std::getline(infile, line)) {
            // Remove comments starting with ';' or '#'
            size_t commentPos = line.find_first_of(";#");
            if (commentPos != std::string::npos) {
                line = line.substr(0, commentPos);
            }

            std::string trimmed = trimString(line);
            if (trimmed.empty()) continue;

            // Section header e.g. [Role]
            if (trimmed.front() == '[' && trimmed.back() == ']') {
                currentSection = trimString(trimmed.substr(1, trimmed.size() - 2));
                continue;
            }

            // Key = Value e.g. Curren_Role = Initiator
            size_t eqPos = trimmed.find('=');
            if (eqPos != std::string::npos) {
                std::string key = trimString(trimmed.substr(0, eqPos));
                std::string val = trimString(trimmed.substr(eqPos + 1));

                std::string keyLower = key;
                std::transform(keyLower.begin(), keyLower.end(), keyLower.begin(), ::tolower);

                if (keyLower == "curren_role" || keyLower == "current_role" || keyLower == "role") {
                    std::string valLower = val;
                    std::transform(valLower.begin(), valLower.end(), valLower.begin(), ::tolower);

                    if (valLower == "initiator") {
                        isInitiator = true;
                        currentRoleStr = "Initiator";
                        std::cout << "[THOR] Config Parser: Parsed role 'Initiator' from config. isInitiator set to true." << std::endl;
                        return isInitiator;
                    } else if (valLower == "acceptor") {
                        isInitiator = false;
                        currentRoleStr = "Acceptor";
                        std::cout << "[THOR] Config Parser: Parsed role 'Acceptor' from config. isInitiator set to false." << std::endl;
                        return isInitiator;
                    }
                }
            }
        }

        return isInitiator;
    }

    void THOR::InitConfig()
    {
        // 1) Read Role from Config.ini and build struct object
        ParseRoleConfig("Config.ini");

        // 0 = Android, 1 = ESP32
        uint16_t mtuCap = 0;

        if (cfg.deviceType == 1) {
            // ESP32
            mtuCap = 185;
        } else {
            // Android (default)
            mtuCap = 247; // safe upper bound for cross-device reliability
        }

        if (cfg.attMtu == 0) {
            cfg.attMtu = mtuCap;
        }

        // 2) Clamp MTU
        cfg.attMtu = std::min<uint16_t>(cfg.attMtu, mtuCap);

        // 3) Compute max payload
        if (cfg.attMtu <= (attOverhead + headerSize)) {
            cfg.maxPayload = 0;
            cfg.fragPayloadSize = 0;
            return;
        }

        cfg.maxPayload = cfg.attMtu - attOverhead - headerSize;

        // 4) Fragment payload size
        cfg.fragPayloadSize = cfg.maxPayload;

        // 5) Safety cap
        if (cfg.maxFragments == 0) cfg.maxFragments = 32;

        // 6) Compute TempId from internet state
        cfg.TempId = CreateTempId();
        header.senderId = cfg.TempId;
        
        header.sequence = 1; // Reset sequence number
    }
    uint32_t THOR::CreateTempId() //Ensure BLE device wrapper gives 0 as LSB of the PermNodeId always (Use Error checks)
    {
        if(cfg.myInternet)
        {
            return cfg.PermId|1;
        }
        else
        {
            return cfg.PermId & ~1u ;
        }
    }
    
    std::array<int32_t, DEST_ARRAY_SIZE> THOR::GetDestID()
    {
        std::array<int32_t, DEST_ARRAY_SIZE> destArray{};
        destArray.fill(-1);

        size_t count = 0;
        while (count < DEST_ARRAY_SIZE) {
            auto popper = destIdQueue.pop();
            if (!popper) break;
            destArray[count] = static_cast<int32_t>(*popper);
            count++;
        }
        return destArray;
    }

    bool THOR::PushDestId(uint32_t destId)
    {
        auto pusher = destIdQueue.push();
        if (!pusher) return false;
        *pusher = destId;
        return true;
    }
        
    uint32_t THOR::ParseTempId() //Return back to the permanent Id stored in the BLE device
    {
        return cfg.TempId & ~1u;
    }
    
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
    
    bool THOR::set_transaction()
    {
        return transaction;
    }
    uint32_t THOR::mysequence(uint32_t &seq)
    {
        //check for roles and respond with appropriate sequence based on the role
        return seq;
    }
    std::vector<uint8_t> THOR::CreateHello(uint32_t DestId)
    // InitHello
    {
        if(header.senderId == header.originId)
        {header.sequence = 1;}
        header.destinationId = DestId;
        header.originId = header.senderId;
        header.nextHopId = BROADCAST_ID; // 0xFFFFFFFF
        header.sequence = mysequence(header.sequence);
        header.type = THORPacketType::HELLO;
        header.flagsAndTTL.setTTL(1);
        header.flagsAndTTL.setVisited(false);
        header.flagsAndTTL.setIntNeighbour(false);
        return SerializeHeader(header);
    }
    
    std::vector<uint8_t> THOR::CreateACK(uint32_t DestId, uint32_t SenderId,uint32_t OriginId,uint32_t NextHopId,uint32_t Sequence, bool intneighbour,bool successACK)
    {
        header.senderId = SenderId;//My ID
        header.destinationId = DestId;
        header.originId = OriginId;
        header.nextHopId = NextHopId;//header.senderId in the HELLO Packet
        header.sequence = Sequence;//HELLO sequence + 1
        header.flagsAndTTL.setTTL(1);
        header.flagsAndTTL.setVisited(false);
        header.type = THORPacketType::ACK;
        header.flagsAndTTL.setIntNeighbour(cfg.intneighbour);
        return SerializeHeader(header); 
    }
    
    bool THOR::HandleHello(const std::vector<uint8_t>& data)//Endpoint call
    // DiscoverHello
    {
        if(!transaction)
        {
            transaction = true;
            bool result = DeserializeHeader(data, header);
            auto &n = neighborTable[header.senderId];
            n.lastSeen = std::time(nullptr);
            n.lock = true;
            outheader.DestId = header.destinationId;
            outheader.OriginId = header.originId;
            outheader.sequence = header.sequence;
            outheader.SenderId = header.senderId;
            outheaderFields.push(outheader);
            return result;
        }
        else
        {return false;}
    }
    //Process hello packets to find neighbors
    std::vector<uint8_t> THOR::ACK()//Endpoint call
    {
        outheader = outheaderFields.front();
        outheaderFields.pop();
        if(cfg.TempId == outheader.DestId)
        {
            return CreateACK(outheader.DestId,cfg.TempId,outheader.OriginId,outheader.SenderId,outheader.sequence,cfg.intneighbour,1);
        }
        else
        {
            return CreateACK(outheader.DestId,cfg.TempId,outheader.OriginId,outheader.SenderId,outheader.sequence,cfg.intneighbour,0);
        }
    }
    
    void THOR::ProcessACK(Header outheader)
    
    {
        auto &n = neighborTable[outheader.senderId];
        if(outheader.type == THORPacketType::ACK && outheader.flagsAndTTL.getMoreData()==1)
        {
            n.lock = false;
            transaction = false;
        }
        else
        {
            n.lastSeen = std::time(nullptr);
            n.hasInternetDirect = outheader.senderId & 1 ? true : false;
            OutHeader oh;
            oh.DestId = outheader.destinationId;
            oh.OriginId = outheader.originId;
            oh.sequence = outheader.sequence;
            oh.SenderId = outheader.senderId;
            if(oh.sequence%2!=0)
            {
                
            }
            //have to put sequencing logic for hasdirectinternet to check which node is it - primary/secondary
            outheaderFields.push(oh);
        }
    }
    
    //store ACKs in the queue and pop elements for single threaded function HandleAck
    bool THOR::HandleAck(const std::vector<uint8_t>& data)//Endpoint call
    // AckReceive
    {
        Header outheader;
        bool result =  DeserializeHeader(data,outheader);
        ProcessACK(outheader);
        return result;
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
        header.flagsAndTTL.setTTL(15);
        header.type = THORPacketType::DATA;
        header.nextHopId = 0; // Default to 0
        header.flagsAndTTL.setVisited(false);
    
        // 2. Routing Decision
        uint32_t bestHop = GetBestNextHop();
    
        if (bestHop != 0) {
            // --- PATH FOUND ---
            neighborTable[bestHop].isVisited = true;
            
            // Update the HEADER with the route
            header.nextHopId = bestHop; 
            header.flagsAndTTL.setVisited(true);
            packet.header = header; 
            packet.payload = payload;
            
            return Serialize(packet); // Send immediately
        } 
        else {
            // --- NO PATH (Store and Forward) ---
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


        if (outPacket.header.flagsAndTTL.getTTL() <= 1) {
            return {};
        }

        // 3. Am I the destination?
        if(outPacket.header.destinationId == MyNodeId)
        {
            return {};
        }
        // 4. Decrement TTL
        outPacket.header.flagsAndTTL.setTTL(outPacket.header.flagsAndTTL.getTTL()-1);

        // 5. Select Best Hop (Internet -> Indirect -> Explore)
        uint32_t bestHop = GetBestNextHop();

        if (bestHop != 0) {
            neighborTable[bestHop].isVisited = true;
            // 6. Forward Accordingly
            outPacket.header.nextHopId = bestHop;
            outPacket.header.flagsAndTTL.setVisited(true); // Mark path as used
            return Serialize(outPacket); // Return bytes to send immediately
        } 
        else {
            // 7. No neighbors -> Fail (Store in Queue)
            // Check if queue is full to prevent memory leaks
            if (packetQueue.size() < 50) {
                packetQueue.push_back(outPacket);
            }
            return {}; // Return empty -> Stored for later.
        }
    }
    
    void THOR::RemoveOld()
    {
        // Remove neighbors we haven't heard from in 30 seconds
        time_t now = std::time(nullptr);
        auto it = neighborTable.begin();

        while (it != neighborTable.end()) {
            NeighborInfo& info = it->second;

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
        for (auto const& entry : neighborTable) 
        {
            if (!entry.second.lock)
            {
            
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
        }
        return (maxScore == -1) ? 0 : bestNodeId;
    }
    
    uint8_t THOR::ismoredata()
    {
        if(cfg.fragPayloadSize > 0)
        {
            return 1;
        }
        return 0;
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
            packet.header.flagsAndTTL.setVisited(true);

            // Serialize and add to batch
            batchToSend.push_back(Serialize(packet));
        }

        // 5. Clear the internal queue since we are handing them off to the wrapper
        packetQueue.clear();

        return batchToSend;
    }
