#pragma once
#include <cstdint>
#include <vector>
#include <boost/asio.hpp>

// 간단한 헤더: [Length(2)][ID(2)][Body...]
#pragma pack(push, 1)
struct PacketHeader {
    uint16_t length; // Body 길이
    uint16_t id;     // Protocol ID
};
#pragma pack(pop)

// 최대 패킷 크기 제한 (버퍼 오버플로우 방지)
constexpr size_t MAX_PACKET_SIZE = 4096;