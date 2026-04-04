#include <iostream>
#include <cstdint>

#include <unistd.h>
#include "tcp_header.hpp"
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>

using tcp_header::Header;

int PORT = 8080;
in_addr_t HOST = inet_addr("127.0.0.1");

struct sockaddr_in server_addr{
    .sin_family = AF_INET,
    .sin_port = htons(PORT),
    .sin_addr = {HOST},
    .sin_zero = {0}
};

bool recive_connection(){
    try{

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

        while (true){
            //Receive SYN from client
            Header syn{};
            sockaddr_in client_addr{};
            if(!tcp_header::recv_header(sock, syn, client_addr)){
                std::cerr << "Error receiving data" << std::endl;
                continue;
            }

            uint16_t syn_flags = tcp_header::unpack_flags(ntohs(syn.data_flags));
            uint16_t client_isn = ntohs(syn.seq_number);

            if((syn_flags & tcp_header::FLAG_SYN) == 0){
                std::cerr << "Received packet without SYN flag, ignoring." << std::endl;
                continue;
            }
            std::cout << "Received SYN from client with ISN: " << client_isn << std::endl;

            //Send SYN-ACK to client
            uint16_t server_isn = static_cast<uint16_t>(2000 + (std::rand() % 10000));
            Header syn_ack{
                .seq_number = htons(server_isn),
                .ack_number = htons(static_cast<uint16_t>(client_isn + 1)),
                .rwnd = htons(0),
                .data_flags = htons(tcp_header::pack_data_flags(0, tcp_header::FLAG_SYN | tcp_header::FLAG_ACK))
            };
            if(!tcp_header::send_header(sock, syn_ack, client_addr)){
                std::cerr << "Error sending SYN-ACK" << std::endl;
                continue;
            }
            std::cout << "Sent SYN-ACK to client with ISN: " << server_isn << std::endl;

            //Receive ACK from client
            Header ack{};
            sockaddr_in ack_client_addr{};
            if(!tcp_header::recv_header(sock, ack, ack_client_addr)){
                std::cerr << "Error receiving ACK" << std::endl;
                continue;  
            }

            if(ack_client_addr.sin_addr.s_addr != client_addr.sin_addr.s_addr || ack_client_addr.sin_port != client_addr.sin_port){
                std::cerr << "Received ACK from unexpected client, ignoring." << std::endl;
                continue;
            }

            uint16_t ack_flags = tcp_header::unpack_flags(ntohs(ack.data_flags));
            uint16_t ack_number = ntohs(ack.ack_number);
            uint16_t seq_number = ntohs(ack.seq_number);

            bool valid_ack = ((ack_flags & tcp_header::FLAG_ACK) != 0) && 
                             (ack_number == static_cast<uint16_t>(server_isn + 1)) && 
                             (seq_number == static_cast<uint16_t>(client_isn + 1));

            if(!valid_ack){
                std::cerr << "Invalid ACK received, ignoring." << std::endl;
                continue;
            }

            std::cout << "Received ACK from client, connection established." << std::endl;
            std::cout << "TCP Connection established with client " << std::endl;
        }

        close(sock);
        return true;
        
    } catch(const std::exception& e) {
        std::cerr << "Failed to create server: " << e.what() << std::endl;
        return false;
    }

    return true;
}


int main() {
    if(!recive_connection()){
        std::cerr << "Failed to receive connections." << std::endl;
        return 1;
    }
    std::cout << "Connection received successfully!" << std::endl;

    return 0;
}