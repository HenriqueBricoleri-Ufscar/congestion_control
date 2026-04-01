#include <cstdint>

#ifndef HEADER_H
#define HEADER_H

typedef struct _header {
    uint16_t seq_number;
    uint16_t ack_number;
    uint16_t rwnd; 
    uint16_t data_flags;
} Header;

inline uint16_t unpack_data_len(uint16_t data_flags);

inline uint16_t unpack_flags(uint16_t data_flags);

inline bool is_ack(uint16_t data_flags);
#endif