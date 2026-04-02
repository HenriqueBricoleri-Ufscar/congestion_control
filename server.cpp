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

int main() {
    try{

        int sckt = socket(AF_INET, SOCK_STREAM, 0);
        if(sckt < 0) {
            std::cerr << "Error creating socket" << std::endl;
            return 1;
        }
        if(bind(sckt, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
            std::cerr << "Error binding socket" << std::endl;
            close(sckt);
            return 1;
        }

        if(listen(sckt, 5) < 0) {
            std::cerr << "Error listening on socket" << std::endl;
            close(sckt);
            return 1;
        }
        std::cout << "Server is listening on port " << PORT << std::endl;

        while(true) {
            int client_sock = accept(sckt, nullptr, nullptr);
            if(client_sock < 0) {
                std::cerr << "Error accepting connection" << std::endl;
                continue;
            }
            std::cout << "Accepted a connection" << std::endl;

            send(client_sock, "Hello, Client!", 14, 0);

            close(client_sock);
        }
    } catch(const std::exception& e) {
        std::cerr << "Failed to create server: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}