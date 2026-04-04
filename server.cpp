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