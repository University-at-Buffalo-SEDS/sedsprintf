//
// Created by Rylan Meilutis on 10/1/25.
//

#ifndef SEDSPRINTF_SERIALIZE_H
#define SEDSPRINTF_SERIALIZE_H
#include <cstdint>
#include "struct_setup.h"


typedef struct
{
    uint8_t * buffer;
    size_t size;
} serialized_buffer_t;

size_t get_packet_size(const telemetry_packet_t * packet);

serialized_buffer_t create_serialized_buffer(uint8_t buff[], size_t size);

void serialize_packet(const telemetry_packet_t * packet, const serialized_buffer_t * buffer);

telemetry_packet_t deserialize_packet(const serialized_buffer_t * serialized_buffer);
#endif //SEDSPRINTF_SERIALIZE_H
