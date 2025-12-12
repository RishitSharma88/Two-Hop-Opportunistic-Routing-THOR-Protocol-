// Copyright 2025 Rishit Sharma
// Licensed under the Apache License, Version 2.0
#ifndef THOR_H
#define THOR_H
#include <cstdint>
#include <vector>
#include <iostream>
#include <map>
#include <ctime>

const uint32_t BROADCAST_ID = 0xFFFFFFFF;

#pragma pack(push, 1)

enum class THORPacketType : uint8_t {
    HELLO = 1,
    ACK   = 2,
    DATA  = 3
};

struct flags{
    unsigned char ttl : 5;// time to live
    unsigned char intneighbour : 1;// 1 = neighbour has internet, 0 = no internet neighbour.
    unsigned char visited : 1;//1 = visited, 0 = not visited
    unsigned char myInternet : 1;//
};

struct Header {
    THORPacketType type;   // 1 byte

    flags  flagsAndTTL;  // 1 byte (Renamed from 'ttl')
    uint32_t destinationId;// 4 bytes
    uint32_t senderId;     // 4 bytes
    uint32_t originId;     // 4 bytes
    uint32_t nextHopId;    // 4 bytes
    uint32_t sequence;     // 4 bytes
};//total : 22 bytes
#pragma pack(pop)

struct Packet {
    Header header;
    std::vector<uint8_t> payload;
};
struct NeighborInfo {
    time_t lastSeen;          // To expire old neighbors
    int    rssi;              // Signal strength
    bool hasInternetDirect;   // Priority 1 (Bit 7 of Header)
    bool hasInternetIndirect; // Priority 2 (Bit 5 of Header)
    bool isVisited;           // Priority 3 (Bit 6 of Header) - Avoid if true
};
static_assert(sizeof(Header) == 22, "Error: Header size must be exactly 22 bytes for BLE!");

class THOR
{
public: 
    std::vector<uint8_t> Serialize(const Packet& packet);
    bool Deserialize(const std::vector<uint8_t>& data, Packet& outPacket);
    bool DeserializeHeader(const std::vector<uint8_t>& data, Header& outheader);
    std::vector<uint8_t> SerializeHeader(const Header& header);
    std::vector<uint8_t> CreateHello(uint32_t DestId ,uint32_t SenderId, uint32_t OriginId, uint32_t Sequence);
    std::vector<uint8_t> CreateACK(uint32_t DestId, uint32_t SenderId,uint32_t OriginId,uint32_t NextHopId,uint32_t Sequence, bool myinternet, bool intneighbour);
    bool HandleHello(const std::vector<uint8_t>& data, Header& outheader);
    bool HandleAck(const std::vector<uint8_t>& data, Header& outheader);
    std::vector<uint8_t> SendPacket(uint32_t DestId, uint32_t SenderId, uint32_t OriginId, uint32_t Sequence, const std::vector<uint8_t>& payload);
    std::vector<uint8_t> HandleData(const std::vector<uint8_t>& data, Packet& outPacket, uint32_t MyNodeId); 
    void NeighborStore(uint32_t nodeId, int rssi, bool hasInternetDirect, bool hasInternetIndirect, bool isVisited);
    void RemoveOld();
    uint32_t GetBestNextHop();
    std::vector<std::vector<uint8_t>> ProcessQueue(); //Android Wrapper Endpoint Function
    
private:
    std::map<uint32_t, NeighborInfo> neighborTable;
    std::vector<Packet> packetQueue;
};

#endif /* THOR_H */
