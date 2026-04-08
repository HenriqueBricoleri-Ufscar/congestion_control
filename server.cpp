#include <iostream>
#include <cstdint>
#include <vector>
#include <cstdlib>
#include <cstring>

#include <unistd.h>
#include "tcp_header.hpp"
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>

using tcp_header::Header;

//server details
const static int PORT = 8086;
const static in_addr_t HOST = inet_addr("127.0.0.1");


//TCP parameters
int CWND = 1024; //Congestion Window Size
static int MSS = 1024;  //Maximum Segment Size
static int RTO = 50; //Retransmission Timeout in milliseconds
int max_package_size = MSS + sizeof(Header); //Maximum package size
int initial_SSTHRESH = 15360; //Initial Slow Start Threshold

static constexpr int SIM_DROP = 15;

//TCP parameters
struct sockaddr_in server_addr{
    .sin_family = AF_INET,
    .sin_port = htons(PORT),
    .sin_addr = {HOST},
    .sin_zero = {0}
};

inline static bool random_drop(int pct) {
    return (std::rand() % 100) < pct;
}

static int create_socket(){
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if(sock < 0){
        std::cerr << "Error creating socket" << std::endl;
        return false;
    }

    if(bind(sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0){
        std::cerr << "Error binding socket" << std::endl;
        close(sock);
        return false;
    }

    return sock;
}

static bool recive_connection(int sock, sockaddr_in& client_addr, uint16_t& client_isn, uint16_t& server_isn){
    try{
        //Receive SYN from client
        Header syn{};
        if(!tcp_header::recv_header(sock, syn, client_addr)){
            std::cerr << "Error receiving data" << std::endl;
        }

        uint16_t syn_flags = tcp_header::unpack_flags(ntohs(syn.data_flags));
        if((syn_flags & tcp_header::FLAG_SYN) == 0){
            std::cerr << "Received packet without SYN flag, ignoring." << std::endl;
        }
        client_isn = ntohs(syn.seq_number);
        std::cout << "Received SYN from client with ISN: " << client_isn << std::endl;
        
        //Send SYN-ACK to client
        server_isn = static_cast<uint16_t>(2000 + (std::rand() % 10000));

        Header syn_ack{
            .seq_number = htons(server_isn),
            .ack_number = htons(static_cast<uint16_t>(client_isn + 1)),
            .rwnd = htons(0),
            .data_flags = htons(tcp_header::pack_data_flags(0, tcp_header::FLAG_SYN | tcp_header::FLAG_ACK))
        };

        if(!tcp_header::send_header(sock, syn_ack, client_addr)){
            std::cerr << "Error sending SYN-ACK" << std::endl;
        }
        std::cout << "Sent SYN-ACK to client with ISN: " << server_isn << std::endl;

        //Receive ACK from client
        Header ack{};
        sockaddr_in ack_client_addr{};

        if(!tcp_header::recv_header(sock, ack, ack_client_addr)){
            std::cerr << "Error receiving ACK" << std::endl; 
        }

        if(ack_client_addr.sin_addr.s_addr != client_addr.sin_addr.s_addr || ack_client_addr.sin_port != client_addr.sin_port){
            std::cerr << "Received ACK from unexpected client, ignoring." << std::endl;
        }

        uint16_t ack_flags = tcp_header::unpack_flags(ntohs(ack.data_flags));
        uint16_t ack_seq = ntohs(ack.seq_number);
        uint16_t ack_num = ntohs(ack.ack_number);

        bool valid_ack =
            (ack_flags & tcp_header::FLAG_ACK) &&
            (ack_seq == static_cast<uint16_t>(client_isn + 1)) &&
            (ack_num == static_cast<uint16_t>(server_isn + 1));

        if (!valid_ack) {
            std::cerr << "Invalid ACK received, ignoring." << std::endl;
        }
        std::cout << "Received last ACK from client." << std::endl;
        std::cout << "TCP Connection established with client " << std::endl;

        return true;
        
    } catch(const std::exception& e) {
        std::cerr << "Failed to create server: " << e.what() << std::endl;
        return false;
    }
}

static void receive_data_loop(int sock, const sockaddr_in& client_addr, uint16_t client_isn, uint16_t server_isn){
    uint16_t expected_seq = static_cast<uint16_t>(client_isn + 1);
    uint16_t server_seq = static_cast<uint16_t>(server_isn + 1);

    std::vector<char> packets(max_package_size);

    while(true){
        sockaddr_in from{};
        socklen_t from_len = sizeof(from);

        ssize_t recv_len = recvfrom(sock, packets.data(), packets.size(), 0, reinterpret_cast<sockaddr*>(&from), &from_len);
        if(recv_len < (ssize_t)sizeof(Header)){
            std::cerr << "Error receiving data or connection closed by client" << std::endl;
            continue;
        }

        if (from.sin_addr.s_addr != client_addr.sin_addr.s_addr ||
            from.sin_port != client_addr.sin_port) {
            continue;
        }

        Header header{};
        std::memcpy(&header, packets.data(), sizeof(Header));

        uint16_t flags = tcp_header::unpack_flags(ntohs(header.data_flags));
        uint16_t seq   = ntohs(header.seq_number);
        uint16_t data_len = tcp_header::unpack_data_len(ntohs(header.data_flags));

        if(flags & tcp_header::FLAG_FIN){
            std::cout << "Received FIN from client, closing connection." << std::endl;
            break;
        }

        if(data_len > MSS || ((ssize_t)sizeof(Header) + data_len) > recv_len){
            std::cout << "Data greater than MSS" << std::endl;
            continue;
        }

        if(random_drop(SIM_DROP)){
            std::cout << "DROP DATA seq =" << seq << std::endl;
            continue;
        }

        if(seq == expected_seq){
            expected_seq = static_cast<uint16_t>(expected_seq + data_len);
            std::cout << "Received data with expected seq: " << seq << ", length: " << data_len << std::endl;
        }

        Header ack{
            .seq_number = htons(server_seq),
            .ack_number = htons(expected_seq),
            .rwnd = htons(0),
            .data_flags = htons(tcp_header::pack_data_flags(0, tcp_header::FLAG_ACK))
        };

        if(random_drop(SIM_DROP)){
            std::cout << "DROP ACK for seq = " << expected_seq << std::endl;
            continue;
        }

        if(!tcp_header::send_header(sock, ack, client_addr)){
            std::cerr << "Error sending ACK" << std::endl;
        } else {
            std::cout << "Sent ACK for seq: " << expected_seq << std::endl;
        }
    }
}

int main() {
    int sock = create_socket();
    if(sock < 0){
        std::cerr << "Failed to create socket." << std::endl;
        close(sock);
        return 1;
    }

    while(true){
        sockaddr_in client_addr{};
        uint16_t client_isn = 0;
        uint16_t server_isn = 0;

        if(!recive_connection(sock, client_addr, client_isn, server_isn)){
            std::cerr << "Failed to receive connections." << std::endl;
            return 1;
        }
        std::cout << "Connection received successfully!" << std::endl;

        receive_data_loop(sock, client_addr, client_isn, server_isn);
    }
    close(sock);
    return 0;
}