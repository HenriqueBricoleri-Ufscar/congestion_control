#include <iostream>
#include <cstdint>
#include <ctime>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <vector>
#include <map>
#include <chrono>
#include <fstream>
#include <climits>

#include "tcp_header.hpp"
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>

using tcp_header::Header;
using Clock = std::chrono::steady_clock;
using Ms = std::chrono::milliseconds;

//=============================================
//              Plot Archive
//=============================================

static std::ofstream cwnd_log;
static Clock::time_point t0;

//=============================================
//              TCP PARAMETERS
//=============================================

// Server details
const static int PORT = 8086;
const static in_addr_t HOST = inet_addr("127.0.0.1");

// TCP parameters (mesmos nomes do segundo código)
int CWND = 1024;               // Congestion Window Size
static int MSS = 1024;          // Maximum Segment Size
static int RTO = 500;           // Retransmission Timeout in milliseconds
int max_package_size = MSS + sizeof(Header);
int SSTHRESH = 15360;           // Initial Slow Start Threshold

// CC state
enum class CCState {
    SLOW_START,
    CONGESTION_AVOIDANCE,
    FAST_RECOVERY
};
static CCState cc_state = CCState::SLOW_START;

// FR parameters
static int dup_ack_count = 0;
static size_t fr_target_seq = 0;

// Server address
struct sockaddr_in server_addr{
    .sin_family = AF_INET,
    .sin_port = htons(PORT),
    .sin_addr = {HOST},
    .sin_zero = {0}
};

struct sent_packet {
    std::vector<char> data;
    uint16_t seg_len;
    Clock::time_point deadline;
};
static std::map<uint16_t, sent_packet> send_buffer;

//====================================================
//                  AUX METHODS
//====================================================

static void log_cwnd(const char* event, uint16_t seq = 0) {
    auto ms = std::chrono::duration_cast<Ms>(Clock::now() - t0).count();
    cwnd_log << ms << "," << event << "," << CWND << "," << SSTHRESH << "," << seq << "\n";
    cwnd_log.flush();
}

static void log_packet(const char* type, uint16_t seq, uint16_t ack, uint16_t len) {

    std::cout << "[" << type << "] seq=" << seq
              << " ack=" << ack
              << " pkt_len=" << len
              << " CWND=" << CWND
              << " SSTHRESH=" << SSTHRESH
              << " state=" << (cc_state==CCState::SLOW_START?"SS":
                               cc_state==CCState::CONGESTION_AVOIDANCE?"CA":"FR")
              << std::endl;
}

static void on_timeout() {
    SSTHRESH = std::max(CWND / 2, MSS);
    CWND = MSS;
    cc_state = CCState::SLOW_START;
    dup_ack_count = 0;

    log_cwnd("TIMEOUT");
}

static void on_ack() {
    switch (cc_state) {
        case CCState::SLOW_START:
            CWND += MSS;
            if (CWND >= SSTHRESH) {
                cc_state = CCState::CONGESTION_AVOIDANCE;
            }
            log_cwnd("ACK_SS");
            break;
        case CCState::CONGESTION_AVOIDANCE:
            CWND += (MSS * MSS) / std::max(CWND, 1);
            log_cwnd("ACK_CA");
            break;
        case CCState::FAST_RECOVERY:
            CWND = SSTHRESH;
            cc_state = CCState::CONGESTION_AVOIDANCE;
            dup_ack_count = 0;
            log_cwnd("FR_EXIT");
            break;
    }
}

static bool set_timeout(int sock, int ms) {
    timeval tv{};
    tv.tv_sec = ms / 1000;
    tv.tv_usec = (ms % 1000) * 1000;
    return setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) == 0;
}

static long min_deadling_remaining(){
    //itera sobre todos os pacotes enviados e altera as deadlines, se for < 0, timeout!
    auto now = Clock::now();
    long min_rem = LONG_MAX;

    for (auto& [snum, spkt] : send_buffer){
        long rem = std::chrono::duration_cast<Ms>(spkt.deadline - now).count();
        if(rem < min_rem) min_rem = rem;
    }

    return (min_rem == LONG_MAX) ? RTO : min_rem;
}

//=====================================================
//                    TCP METHODS
//=====================================================

static bool TCP_handshake(int sock, uint16_t& next_seq, uint16_t& server_next_seq) {
    std::srand((unsigned)std::time(nullptr));
    uint16_t client_isn = static_cast<uint16_t>(1000 + (std::rand() % 10000));

    Header syn{
        .seq_number = htons(client_isn),
        .ack_number = htons(0),
        .rwnd = htons(0),
        .data_flags = htons(tcp_header::pack_data_flags(0, tcp_header::FLAG_SYN))
    };

    if (!tcp_header::send_header(sock, syn, server_addr)) {
        std::cerr << "Error sending SYN\n";
        return false;
    }
    log_packet("SYN", client_isn, 0, 0);

    Header syn_ack{};
    sockaddr_in recv_addr{};
    if (!tcp_header::recv_header(sock, syn_ack, recv_addr)) {
        std::cerr << "Error receiving SYN-ACK\n";
        return false;
    }

    uint16_t syn_ack_flags = tcp_header::unpack_flags(ntohs(syn_ack.data_flags));
    bool valid_syn_ack = ((syn_ack_flags & tcp_header::FLAG_SYN) != 0) &&
                         ((syn_ack_flags & tcp_header::FLAG_ACK) != 0) &&
                         (ntohs(syn_ack.ack_number) == static_cast<uint16_t>(client_isn + 1));

    if (!valid_syn_ack) {
        std::cerr << "Invalid SYN-ACK\n";
        return false;
    }

    uint16_t server_isn = ntohs(syn_ack.seq_number);

    Header ack{
        .seq_number = htons(static_cast<uint16_t>(client_isn + 1)),
        .ack_number = htons(static_cast<uint16_t>(server_isn + 1)),
        .rwnd = htons(0),
        .data_flags = htons(tcp_header::pack_data_flags(0, tcp_header::FLAG_ACK))
    };

    if (!tcp_header::send_header(sock, ack, server_addr)) {
        std::cerr << "Error sending ACK\n";
        return false;
    }
    log_packet("ACK", client_isn+1, server_isn+1, 0);

    next_seq = static_cast<uint16_t>(client_isn + 1);
    server_next_seq = static_cast<uint16_t>(server_isn + 1);
    return true;
}

static bool send_data(int sock, const std::vector<char>& data, uint16_t& seq, uint16_t& server_next_seq) {
    const uint16_t base_seq = seq;
    const size_t total_size = data.size();

    size_t unacked_off = 0;
    size_t next_off = 0;
    uint32_t rwnd = UINT32_MAX;

    send_buffer.clear();

    //===========================
    //      Inner Methods
    //===========================
    
    auto make_seq = [&](size_t offset) -> uint16_t { 
        return static_cast<uint16_t>(base_seq + offset); 
    };

    auto send = [&] (size_t offset) -> bool {
        int seg_size = static_cast<int>(std::min(static_cast<size_t>(MSS), total_size - offset));

        uint16_t seq_num = make_seq(offset);
        Header header {
            .seq_number = htons(seq_num),
            .ack_number = htons(server_next_seq),
            .rwnd = htons(0),
            .data_flags = htons(tcp_header::pack_data_flags(static_cast<uint16_t>(seg_size), 0))
        };

        std::vector<char> packet(sizeof(Header) + seg_size);
        std::memcpy(packet.data(), &header, sizeof(Header));
        std::memcpy(packet.data() + sizeof(Header), data.data() + offset, seg_size);

        //Buffer & packet timeout
        send_buffer[seq_num] = {std::move(packet), static_cast<uint16_t>(seg_size), {}};
        sent_packet& sp = send_buffer[seq_num];

        sp.deadline = Clock::now() + Ms(RTO);

        if(sendto(sock, send_buffer[seq_num].data.data(), send_buffer[seq_num].data.size(), 0, reinterpret_cast<const sockaddr*>(&server_addr), sizeof(server_addr)) < 0){
            std::cerr << "failed to send packet SEQ = " << seq_num << std::endl;
            return false;
        }

        return true;
    };

    //-----------------
    //  Data treat.
    //-----------------

    while(unacked_off < total_size) {
        //Inflight <= CWND
        int in_flight = static_cast<int>(next_off - unacked_off);

        uint32_t snd_wnd = std::min(static_cast<uint32_t>(CWND), rwnd);

        if(snd_wnd == 0) {
            //Buffer do receptor zerado
            set_timeout(sock, RTO);
            Header zero_ack{}; 
            sockaddr_in from{};
            if(tcp_header::recv_header(sock, zero_ack, from)) {
                rwnd = ntohs(zero_ack.rwnd);
            } else {
                on_timeout();
                next_off = unacked_off;
                send_buffer.clear();
            }
            continue;
        }

        //make payload
        while(static_cast<uint32_t>(in_flight) < snd_wnd && next_off < total_size){
            int seg = static_cast<int>(std::min((size_t)MSS, total_size - next_off));
            if (!send(next_off)) return false;

            log_packet("DATA", make_seq(next_off), server_next_seq, static_cast<uint16_t>(seg));
            
            next_off  += seg;
            in_flight  = static_cast<int>(next_off - unacked_off);
        }

        long min_rem = min_deadling_remaining();

        if(min_rem <= 0 ){
            on_timeout();
            next_off = unacked_off;
            send_buffer.clear();
            continue;
        }

        set_timeout(sock, static_cast<int>(min_rem));

        //Wait for Cumulative ACK
        Header ack_hdr {};
        sockaddr_in from{};

        if(!tcp_header::recv_header(sock, ack_hdr, from)){
            //Socket timeout
            auto now = Clock::now();
            bool expired = false;

            for(auto& [snmb, spkt] : send_buffer) 
                if(now >= spkt.deadline) {
                    expired = true;
                    break;
                }
            
            if(expired) {
                on_timeout();
                next_off = unacked_off;
                send_buffer.clear();
            }

            continue;
        }
        
        uint16_t flags = tcp_header::unpack_flags(ntohs(ack_hdr.data_flags));
        if(!(flags & tcp_header::FLAG_ACK)) continue;

        rwnd = ntohs(ack_hdr.rwnd);

        uint16_t ack_num = ntohs(ack_hdr.ack_number);
        size_t ack_off = static_cast<size_t>(static_cast<uint16_t>(ack_num - base_seq));


        //New ACK, Not dup-ACK
        if(ack_off > unacked_off && ack_off <= total_size){
            size_t bytes_acked = ack_off - unacked_off;
            
            for(auto it = send_buffer.begin(); it != send_buffer.end(); ){
                size_t seg_off = static_cast<size_t>(static_cast<uint16_t>(it->first - base_seq));
                if(seg_off + it->second.seg_len <= ack_off){
                    it = send_buffer.erase(it);
                } else {
                    ++it;
                }
            }

            unacked_off = ack_off;
            dup_ack_count = 0;
            log_packet("ACK", 0, ack_num, 0);

            if((cc_state == CCState::FAST_RECOVERY) && ack_off < fr_target_seq){
                //Partial ACK during FR
                CWND = std::max(CWND - static_cast<int>(bytes_acked), MSS) + MSS;
                log_cwnd("PARTIAL_ACK", make_seq(unacked_off));
                uint16_t retx = make_seq(unacked_off);

                if(send_buffer.count(retx)){
                    send(unacked_off);
                    log_packet("RETX(FR)", retx, server_next_seq, send_buffer[retx].seg_len);
                }
            } else {
                on_ack();
            }
        //dup-ACK
        } else if (ack_off == unacked_off){
            dup_ack_count++;
            log_packet("DUP-ACK", 0, ack_num, 0);

            //3 dup-ACK's -> FR
            if (dup_ack_count == 3 && rwnd != 0){
                int flight = static_cast<int>(next_off - unacked_off);
                
                //FR
                SSTHRESH    = std::max(flight / 2, 2 * MSS);
                CWND        = SSTHRESH + 3 * MSS;
                fr_target_seq = next_off;
                cc_state    = CCState::FAST_RECOVERY;
                log_cwnd("FR_ENTER", make_seq(unacked_off));

                uint16_t retx = make_seq(unacked_off);
                if (send_buffer.count(retx)) {
                    send(unacked_off);
                    log_packet("RETX(FAST)", retx, server_next_seq, send_buffer[retx].seg_len);
                } 
            } else if (dup_ack_count > 3 && cc_state == CCState::FAST_RECOVERY){
                CWND += MSS;
            }
        }
    }

    seq = make_seq(unacked_off);
    return true;
}

static bool close_connection(int sock, const sockaddr_in& client_addr, uint16_t next_seq, uint16_t server_next_seq) {
    Header fin_ack{
        .seq_number = htons(next_seq),
        .ack_number = htons(server_next_seq),
        .rwnd = htons(0),
        .data_flags = htons(tcp_header::pack_data_flags(0, tcp_header::FLAG_ACK | tcp_header::FLAG_FIN))
    };

    if(!tcp_header::send_header(sock, fin_ack, client_addr)) {
        std::cerr << "Error sending FIN-ACK\n";
        return false;
    }
    log_packet("FIN", next_seq, server_next_seq, 0);
    close(sock);
    return true;
}

static int estabilish_connection(uint16_t& next_seq, uint16_t& server_next_seq) {
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) return -1;
    if (!set_timeout(sock, RTO)) { close(sock); return -1; }
    if (!TCP_handshake(sock, next_seq, server_next_seq)) { close(sock); return -1; }
    return sock;
}

int main() {
    t0 = Clock::now();

    uint16_t next_seq = 0;
    uint16_t server_next_seq = 0;

    int sock = estabilish_connection(next_seq, server_next_seq);
    if (sock < 0) {
        std::cerr << "Failed to establish connection.\n";
        return 1;
    }

    std::string text = "Mini TCP over UDP payload ";
    std::string big;
    for (int i = 0; i < 1000; ++i) big += text;

    std::vector<char> payload(big.begin(), big.end());

    cwnd_log.open("data/cwnd_log_" + std::to_string(payload.size()) + "B.csv");
    cwnd_log << "time_ms,event,cwnd,ssthresh,seq\n";

    std::cout << "Sending " << payload.size() << " Bytes" << std::endl;

    if (!send_data(sock, payload, next_seq, server_next_seq)) {
        close(sock);
        return 1;
    }

    close_connection(sock, server_addr, next_seq, server_next_seq);
    return 0;
}