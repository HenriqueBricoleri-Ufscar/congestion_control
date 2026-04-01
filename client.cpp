#include <iostream>
#include <cstdint>

#include <unistd.h>
#include "tcp_header.hpp"
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>

int PORT = 8080;
in_addr_t HOST = inet_addr("127.0.0.1");

struct sockaddr_in server_addr{
    .sin_family = AF_INET,
    .sin_port = htons(PORT),
    .sin_addr = {HOST},
    .sin_zero = {0}
};

int main() {
    try{
        int sckt = socket(AF_INET, SOCK_STREAM, 0);
        if(sckt < 0) {
            std::cerr << "Error creating socket" << std::endl;
            return 1;
        }
        if(connect(sckt, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
            std::cerr << "Error connecting to server" << std::endl;
            close(sckt);
            return 1;
        }
        std::cout << "Connected to server at " << inet_ntoa(server_addr.sin_addr) << ":" << PORT << std::endl;

        send(sckt, "Hello, Server!", 14, 0);
        std::cout << "Sent message to server" << std::endl;

        recv(sckt, nullptr, 0, 0); // Placeholder for receiving data

        close(sckt);

    } catch(const std::exception& e) {
        std::cerr << "Failed to connect to server: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}