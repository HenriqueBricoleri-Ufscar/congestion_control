#pragma once
#include <cstdint>

namespace tcp_header{
    static constexpr uint16_t FLAG_ACK = 0x1;
    static constexpr uint16_t FLAG_SYN = 0x2;
    static constexpr uint16_t FLAG_FIN = 0x4;

    static constexpr uint16_t DATA_LEN_MASK = 0x1FFF;

    #pragma pack(push, 1)
    typedef struct _header {
        uint16_t seq_number;
        uint16_t ack_number;
        uint16_t rwnd; 
        uint16_t data_flags;
    } Header;
    #pragma pack(pop)

    inline uint16_t unpack_data_len(uint16_t data_flags) {
        return static_cast<uint16_t>((data_flags >> 3) & DATA_LEN_MASK);
    }

    inline uint16_t unpack_flags(uint16_t data_flags){
        return static_cast<uint16_t>(data_flags & 0x7);
    }

    inline bool is_ack(uint16_t data_flags){
        return(data_flags & FLAG_ACK) != 0;
    }

    static inline uint16_t pack_data_flags(uint16_t data_len, uint16_t flags) {
        return static_cast<uint16_t>(((data_len & tcp_header::DATA_LEN_MASK) << 3) | (flags & 0x7));
    }

    static inline bool send_header(int sock, const Header& header, const sockaddr_in& addr){
        return sendto(sock, &header, sizeof(header), 0, (const sockaddr*)&addr, sizeof(addr)) == (ssize_t)sizeof(header); 
    }

    static inline bool recv_header(int sock, Header& header, const sockaddr_in& recv_addr){
        return recvfrom(sock, &header, sizeof(header), 0, (sockaddr*)&recv_addr, (socklen_t*)sizeof(recv_addr)) == (ssize_t)sizeof(header);
    }

}