#include <iostream>
#include <cstdint>
#include <ctime>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <vector>

#include "tcp_header.hpp"
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>

using tcp_header::Header;

//server details
const static int PORT = 8086;
const static in_addr_t HOST = inet_addr("200.133.238.213");

//TCP parameters
int CWND = 1024; //Congestion Window Size
static int MSS = 1024;  //Maximum Segment Size
static int RTO = 50; //Retransmission Timeout in milliseconds
int max_package_size = MSS + sizeof(Header); //Maximum package size
int SSTHRESH = 15360; //Initial Slow Start Threshold

enum class CCState {
    SLOW_START,
    CONGESTION_AVOIDANCE
};

static CCState cc_state = CCState::SLOW_START;

//Server address structure
struct sockaddr_in server_addr{
    .sin_family = AF_INET,
    .sin_port = htons(PORT),
    .sin_addr = {HOST},
    .sin_zero = {0}
};

static bool set_timeout(int sock) {
    timeval timeout{};
    timeout.tv_sec = RTO / 1000;
    timeout.tv_usec = (RTO % 1000) * 1000;
    if (setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0) {
        std::cerr << "Error setting socket timeout" << std::endl;
        return false;
    }
    return true;
}

static void on_ack_success(){
    if(cc_state == CCState::SLOW_START){
        CWND += MSS;
        if(CWND >= SSTHRESH){
            cc_state = CCState::CONGESTION_AVOIDANCE;
        }   
    } else {
        CWND += (MSS * MSS) / std::max(CWND, 1);
    }
}

static void on_timeout(){
    SSTHRESH = std::max(CWND / 2, MSS);
    CWND = MSS;
    cc_state = CCState::SLOW_START;
}


static bool TCP_handshake(int sock, uint16_t& next_seq, uint16_t& server_next_seq){
    try{
        //HANDSHAKE
        //generate random initial sequence number
        std::srand((unsigned)std::time(nullptr));
        uint16_t client_isn = static_cast<uint16_t>(1000 + (std::rand() % 10000));

        //Send SYN
        Header syn{
            .seq_number = htons(client_isn),
            .ack_number = htons(0),
            .rwnd = htons(0),
            .data_flags = htons(tcp_header::pack_data_flags(0, tcp_header::FLAG_SYN))
        };

        if(!tcp_header::send_header(sock, syn, server_addr)){
            std::cerr << "Error sending SYN" << std::endl;
            return false;
        }
        std::cout << "SYN sent with ISN: " << client_isn << std::endl;

        //Recv SYN-ACK
        Header syn_ack{};
        sockaddr_in recv_addr{};
        if(!tcp_header::recv_header(sock, syn_ack, recv_addr)){
            std::cerr << "Error receiving SYN-ACK" << std::endl;
            return false;
        }

        uint16_t syn_ack_flags = tcp_header::unpack_flags(ntohs(syn_ack.data_flags));
        
        bool valid_syn_ack = ((syn_ack_flags & tcp_header::FLAG_SYN) != 0) && 
                             ((syn_ack_flags & tcp_header::FLAG_ACK) != 0) &&
                             (ntohs(syn_ack.ack_number) == static_cast<uint16_t>(client_isn + 1));

        if(!valid_syn_ack){
            std::cerr << "Invalid SYN-ACK received" << std::endl;
            return false;
        }
        std::cout << "SYN-ACK received with ISN: " << ntohs(syn_ack.seq_number) << std::endl;

        //final ACK
        uint16_t server_isn = ntohs(syn_ack.seq_number);

        Header ack{
            .seq_number = htons(static_cast<uint16_t>(client_isn + 1)),
            .ack_number = htons(static_cast<uint16_t>(server_isn + 1)),
            .rwnd = htons(0),
            .data_flags = htons(tcp_header::pack_data_flags(0, tcp_header::FLAG_ACK))
        };

        if(!tcp_header::send_header(sock, ack, server_addr)){
            std::cerr << "Error sending ACK" << std::endl;
            return false;
        }
        std::cout << "ACK sent, connection established" << std::endl;
        std::cout << "TCP Connection established with server at " << inet_ntoa(server_addr.sin_addr) << ":" << ntohs(server_addr.sin_port) << std::endl;

        next_seq = static_cast<uint16_t>(client_isn + 1);
        server_next_seq = static_cast<uint16_t>(server_isn + 1);

        return true;

    } catch(const std::exception& e) {
        std::cerr << "Failed to connect to server: " << e.what() << std::endl;
        return 0;
    }
}

static bool send_data(int sock, const std::vector<char>& data, uint16_t& seq, uint16_t& server_next_seq){
    size_t offset = 0;
    std::vector<char> packet(max_package_size);

    while(offset < data.size()){
        int seg_len = std::min(static_cast<int>(data.size() - offset), MSS);
        
        Header header{
            .seq_number = htons(seq),
            .ack_number = htons(server_next_seq),
            .rwnd = htons(0),
            .data_flags = htons(tcp_header::pack_data_flags(static_cast<uint16_t>(seg_len), 0))
        };
        std::memcpy(packet.data(), &header, sizeof(Header));
        std::memcpy(packet.data() + sizeof(Header), data.data() + offset, seg_len);

        ssize_t sent_len = sendto(sock, packet.data(), sizeof(Header) + seg_len, 0, reinterpret_cast<const sockaddr*>(&server_addr), sizeof(server_addr));
        if (sent_len < 0) {
            std::cerr << "[sendto failed] ACK = " << server_next_seq << std::endl;
            return false;
        }

        Header ack{};
        sockaddr_in from{};
        if (!tcp_header::recv_header(sock, ack, from)) {
            // timeout => perda
            on_timeout();
            std::cout << "[TIMEOUT] CWND=" << CWND << " SSTHRESH=" << SSTHRESH << std::endl;
            continue; // retransmite mesmo segmento
        }

        uint16_t ack_flags = tcp_header::unpack_flags(ntohs(ack.data_flags));
        uint16_t ack_number = ntohs(ack.ack_number);
        uint16_t expected_ack = static_cast<uint16_t>(seq + seg_len);

        if ((ack_flags & tcp_header::FLAG_ACK) && ack_number == expected_ack) {
            seq = expected_ack;
            offset += seg_len;
            on_ack_success();
            std::cout << "[ACK] bytes=" << offset << "/" << data.size()
                      << " CWND=" << CWND << " SSTHRESH=" << SSTHRESH
                      << " mode=" << (cc_state == CCState::SLOW_START ? "SS" : "CA") << "\n";
        } else {
            on_timeout();
            std::cout << "[BAD ACK] CWND=" << CWND << " SSTHRESH=" << SSTHRESH << "\n";
        }
    }

    return true;
}

static bool close_connection(int sock, const sockaddr_in& client_addr, uint16_t next_seq, uint16_t server_next_seq){
    //FIN ack
    Header fin_ack{
        .seq_number = htons(next_seq),
        .ack_number = htons(server_next_seq),
        .rwnd = htons(0),
        .data_flags = htons(tcp_header::pack_data_flags(0, tcp_header::FLAG_ACK | tcp_header::FLAG_FIN))
    };

    if(!tcp_header::send_header(sock, fin_ack, client_addr)){
        std::cerr << "Error sending FIN-ACK" << std::endl;
        return false;
    }
    std::cout << "Sent FIN, closing connection" << std::endl;
    close(sock);
    return true;
}

static int estabilish_connection(uint16_t& next_seq, uint16_t& server_next_seq){
    int sock = socket(AF_INET, SOCK_DGRAM, 0);

    if(sock < 0){
        std::cerr << "Error creating socket" << std::endl;
        return -1;
    }

    if (!set_timeout(sock)) {
        close(sock);
        return -1;
    }

    if(!TCP_handshake(sock, next_seq, server_next_seq)){
        close(sock);
        return -1;
    }

    return sock; 
}

int main() {

    uint16_t next_seq = 0;
    uint16_t server_next_seq = 0;
    
    int sock = estabilish_connection(next_seq, server_next_seq);
    if(sock < 0){
        std::cerr << "Failed to establish connection." << std::endl;
    } else {
        std::cout << "Connection established successfully!" << std::endl;

        std::string text = "Mini TCP over UDP payload ";
        std::string big;
        for (int i = 0; i < 750; ++i) big += text;

        std::vector<char> payload(big.begin(), big.end());

        std::cout << "Sending data of size: " << payload.size() << " bytes" << std::endl;

        if (!send_data(sock, payload, next_seq, server_next_seq)) {
            close(sock);
            return 1;
        }

        close_connection(sock, server_addr, next_seq, server_next_seq);
    }

    return 0;
}