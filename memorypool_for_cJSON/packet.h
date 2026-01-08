#pragma once
#include <cstdint>


#pragma pack(push, 1)
struct SensorRawPacket {
    uint8_t  deviceId;   // 1 字节
    uint16_t rawTemp;    // 2 字节 (大端)
    uint16_t rawHumi;    // 2 字节 (大端)
    uint8_t  statusCode; // 1 字节
    uint16_t checksum;   // 2 字节 (大端)
};
#pragma pack(pop)