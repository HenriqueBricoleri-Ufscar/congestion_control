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


struct sockaddr_in server_addr{
    .sin_family = AF_INET,
    .sin_port = htons(PORT),
    .sin_addr = {HOST},
    .sin_zero = {0}
};

 bool estabilish_connection(){
    try{

        int sckt = socket(AF_INET, SOCK_STREAM, 0);
        if(sckt < 0) {
            std::cerr << "Error creating socket" << std::endl;
            return 0;
        }
        if(connect(sckt, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
            std::cerr << "Error connecting to server" << std::endl;
            close(sckt);
            return 0;
        }
        std::cout << "Connected to server at " << inet_ntoa(server_addr.sin_addr) << ":" << PORT << std::endl;

        send(sckt, "Hello, Server!", 14, 0);
        std::cout << "Sent message to server" << std::endl;

        char response[1024];
        recv(sckt, response, sizeof(response), 0);
        std::cout << "Received response from server: " << response << std::endl;
        
        return 1;

    } catch(const std::exception& e) {
        std::cerr << "Failed to connect to server: " << e.what() << std::endl;
        return 0;
    }
}

int main() {
    
    if (estabilish_connection()){
        std::cout << "Connection established successfully!" << std::endl;
    } else {
        std::cerr << "Failed to establish connection." << std::endl;
    }

    return 0;
}