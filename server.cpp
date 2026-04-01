#include <iostream>
#include <cstdint>
#include <unistd.h>

#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>

int PORT = 8080;
in_addr_t HOST = inet_addr("127.0.0.1");

struct sockaddr_in server_addr{
    .sin_family = AF_INET,
    .sin_port = htons(PORT),
    .sin_addr = {HOST}
};

int main() {
    try{

        int sckt = socket(AF_INET, SOCK_DGRAM, 0);
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

    } catch(const std::exception& e) {
        std::cerr << "Failed to create server: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}