#include <iostream>
#include <cstdint>
#include <vector>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <map>

#include <unistd.h>
#include "tcp_header.hpp"
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>

using tcp_header::Header;

//server details
const static int PORT = 8086;
const static in_addr_t HOST = inet_addr("0.0.0.0");


static int MSS = 1024;  //Maximum Segment Size
int max_package_size = MSS + (int)sizeof(Header); //Maximum package size

static constexpr int SIM_DROP_DATA = 10;
static constexpr int SIM_DROP_ACK = 5;

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
        return -1;
    }

    if(bind(sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0){
        std::cerr << "Error binding socket" << std::endl;
        close(sock);
        return -1;
    }

    return sock;
}

static bool receive_connection(int sock, sockaddr_in& client_addr, uint16_t& client_isn, uint16_t& server_isn){
    try{
        //Receive SYN from client
        Header syn{};
        if(!tcp_header::recv_header(sock, syn, client_addr)){
            std::cerr << "Error receiving TCP header" << std::endl;
        }

        uint16_t syn_flags = tcp_header::unpack_flags(ntohs(syn.data_flags));
        if((syn_flags & tcp_header::FLAG_SYN) == 0){
            std::cerr << "Received packet without SYN flag, ignoring." << std::endl;
            return false;
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

    //Buffer for out-of-order packets
    struct oooBuffer {
        std::vector<char> data;
        uint16_t length;
    };

    std::map<uint16_t, oooBuffer> out_of_order_buffer;

    std::vector<char> received_data_raw(max_package_size); //Buffer for received data
    ssize_t received_data_size = 0;

    auto send_ack = [&]() -> bool {
        Header ack{
            .seq_number = htons(server_seq),
            .ack_number = htons(expected_seq),
            .rwnd = htons(0),
            .data_flags = htons(tcp_header::pack_data_flags(0, tcp_header::FLAG_ACK))
        };

        if(!tcp_header::send_header(sock, ack, client_addr)){
            std::cerr << "Error sending ACK" << std::endl;
            return false;
        }

        std::cout << "Sent ACK for seq: " << expected_seq << std::endl;
        return true;
    };

    while(true){
        sockaddr_in recv{};
        socklen_t from_len = sizeof(recv);

        ssize_t recv_len = recvfrom(sock, received_data_raw.data(), received_data_raw.size(), 0, (sockaddr*)&recv, &from_len);

        if(recv_len < (ssize_t)(sizeof(Header))){
            std::cerr << "Received packet too small to contain header, ignoring." << std::endl;
            continue;
        }

        if(recv.sin_addr.s_addr != client_addr.sin_addr.s_addr || recv.sin_port != client_addr.sin_port){
            std::cerr << "Received packet from unexpected client, ignoring." << std::endl;
            continue;
        }

        Header header{};
        std::memcpy(&header, received_data_raw.data(), sizeof(Header));

        //Checking for flags and data length
        uint16_t flags = tcp_header::unpack_flags(ntohs(header.data_flags));
        uint16_t data_len = tcp_header::unpack_data_len(ntohs(header.data_flags));
        uint16_t seq_num = ntohs(header.seq_number);

        if(flags & tcp_header::FLAG_FIN){
            std::cout << "Received FIN from client, closing connection." << std::endl;
            break;
        }

        if(data_len == 0 || data_len > MSS || recv_len < (ssize_t)(sizeof(Header) + data_len)){
            std::cerr << "Received packet with invalid data length, ignoring." << std::endl;
            continue;
        }

        //sim drop
        if(random_drop(SIM_DROP_DATA)){
            std::cout << "Simulating packet drop for seq: " << seq_num << std::endl;
            continue;
        }

        //In order packet treatment
        if(seq_num == expected_seq){
            received_data_size += data_len;
            expected_seq = static_cast<uint16_t>(expected_seq + data_len);

            std::cout << "Received in-order packet with seq: " << seq_num 
                      << "| data length: " << data_len 
                      << "| next expected seq: " << expected_seq
                      << "| Total bytes received: " << received_data_size
                      << std::endl;

            //Cumulative ACK
            bool sequential_acks;
            do {
                sequential_acks = false;

                auto it = out_of_order_buffer.find(expected_seq);
                if (it != out_of_order_buffer.end()) {
                    received_data_size += it->second.length;
                    expected_seq = static_cast<uint16_t>(expected_seq + it->second.length);
                    std::cout << "Processed buffered out-of-order packet with seq: " << it->first
                              << "| data length: " << it->second.length 
                              << "| next expected seq: " << expected_seq
                              << std::endl;
                    out_of_order_buffer.erase(it);
                    sequential_acks = true; // Check for more buffered packets
                }

            } while (sequential_acks);

            if(random_drop(SIM_DROP_ACK)){
                std::cout << "Simulating ACK drop for seq: " << expected_seq << std::endl;
                continue;
            }

            send_ack();
        }
        //Packet is out of order, buffer it 
        else if(seq_num > expected_seq){
            std::cout << "Received out-of-order packet with seq: " << seq_num  
                        << "| expected seq: " << expected_seq
                        << std::endl;

            if(!out_of_order_buffer.count(seq_num)){
                std::vector<char> data(received_data_raw.begin() + sizeof(Header), received_data_raw.begin() + sizeof(Header) + data_len);
                out_of_order_buffer[seq_num] = {std::move(data), data_len};
            }

            if(random_drop(SIM_DROP_ACK)){
                std::cout << "Simulating Dup ACK drop for seq: " << expected_seq << std::endl;
                continue;
            }

            send_ack();
        }

        else {
            std::cout << "Received duplicate packet with seq: " << seq_num << std::endl;

            if(random_drop(SIM_DROP_ACK)){
                std::cout << "Simulating Dup ACK drop for seq: " << expected_seq << std::endl;
                continue;
            }

            send_ack();
        }

        if(!out_of_order_buffer.empty()){
            std::cout << "Current out-of-order buffer contents: ";
            for(const auto& [seq, buf] : out_of_order_buffer){
                std::cout << "[seq: " << seq << ", len: " << buf.length << "] ";
            }
            std::cout << std::endl;
        }
    }
}

int main() {
    std::srand(std::time(nullptr));

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

        if(!receive_connection(sock, client_addr, client_isn, server_isn)){
            std::cerr << "Failed to handshake." << std::endl;
            return 1;
        }
        std::cout << "Connection received successfully!" << std::endl;

        receive_data_loop(sock, client_addr, client_isn, server_isn);
        std::cout << "Connection closed." << std::endl;
    }
    close(sock);
    return 0;
}