#include <iostream>
#include <cstdint>

#include <unistd.h>
#include "tcp_header.hpp"
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>

using tcp_header::Header;

//server details
int PORT = 8080;
in_addr_t HOST = inet_addr("127.0.0.1");

//TCP parameters
int CWND = 1024; //Congestion Window Size
static int MSS = 1024;  //Maximum Segment Size
static int RTO = 50; //Retransmission Timeout in milliseconds
int max_package_size = MSS + sizeof(Header); //Maximum package size
int initial_SSTHRESH = 15360; //Initial Slow Start Threshold

//Server address structure
struct sockaddr_in server_addr{
    .sin_family = AF_INET,
    .sin_port = htons(PORT),
    .sin_addr = {HOST},
    .sin_zero = {0}
};

bool estabilish_connection(){
    try{
        int sock = socket(AF_INET, SOCK_DGRAM, 0);
        if(sock < 0){
            std::cerr << "Error creating socket" << std::endl;
            return false;
        }

        //Timeout configuration
        timeval timeout;
        timeout.tv_sec = RTO / 1000;
        timeout.tv_usec = (RTO % 1000) * 1000;

        if(setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0){
            std::cerr << "Error setting socket timeout" << std::endl;
            close(sock);
            return false;
        }

        //HANDSHAKE
        //generate random initial sequence number
        std::srand(static_cast<unsigned int>(time(nullptr)));
        uint16_t client_isn = static_cast<uint16_t>(1000 + std::rand() % 10000);

        //Send SYN
        Header syn{
            .seq_number = htons(client_isn),
            .ack_number = htons(0),
            .rwnd = htons(0),
            .data_flags = htons(tcp_header::pack_data_flags(0, tcp_header::FLAG_SYN))
        };

        if(!tcp_header::send_header(sock, syn, server_addr)){
            std::cerr << "Error sending SYN" << std::endl;
            close(sock);
            return false;
        }
        std::cout << "SYN sent with ISN: " << client_isn << std::endl;

        //Recv SYN-ACK
        Header syn_ack{};
        sockaddr_in recv_addr{};
        if(!tcp_header::recv_header(sock, syn_ack, recv_addr)){
            std::cerr << "Error receiving SYN-ACK" << std::endl;
            close(sock);
            return false;
        }

        uint16_t syn_ack_flags = tcp_header::unpack_flags(ntohs(syn_ack.data_flags));
        
        bool valid_syn_ack = ((syn_ack_flags & tcp_header::FLAG_SYN) != 0) && 
                             ((syn_ack_flags & tcp_header::FLAG_ACK) != 0) &&
                             (ntohs(syn_ack.ack_number) == static_cast<uint16_t>(client_isn + 1));

        if(!valid_syn_ack){
            std::cerr << "Invalid SYN-ACK received" << std::endl;
            close(sock);
            return false;
        }
        std::cout << "SYN-ACK received with ISN: " << ntohs(syn_ack.seq_number) << std::endl;

        //final ACK
        Header ack{
            .seq_number = htons(static_cast<uint16_t>(client_isn + 1)),
            .ack_number = htons(static_cast<uint16_t>(syn_ack.seq_number)),
            .rwnd = htons(0),
            .data_flags = htons(tcp_header::pack_data_flags(0, tcp_header::FLAG_ACK))
        };

        if(!tcp_header::send_header(sock, ack, server_addr)){
            std::cerr << "Error sending ACK" << std::endl;
            close(sock);
            return false;
        }
        std::cout << "ACK sent, connection established" << std::endl;
        std::cout << "TCP Connection established with server at " << inet_ntoa(server_addr.sin_addr) << ":" << ntohs(server_addr.sin_port) << std::endl;

        close(sock);
        return true;

    } catch(const std::exception& e) {
        std::cerr << "Failed to connect to server: " << e.what() << std::endl;
        return 0;
    }
}

int main() {
    
    if (!estabilish_connection()){
        std::cerr << "Failed to establish connection." << std::endl;
    } 
    std::cout << "Connection established successfully!" << std::endl;

    return 0;
}