CXX := g++
CXXFLAGS := -O2 -std=c++20 -Wall -Wextra -pedantic

TARGETS := server client
SRCS_SERVER := server.cpp
SRCS_CLIENT := client.cpp
HDRS := tcp_header.hpp

.PHONY: all clean run-server run-client

all: $(TARGETS)

server: $(SRCS_SERVER) $(HDRS)
	$(CXX) $(CXXFLAGS) -o $@ $(SRCS_SERVER)

client: $(SRCS_CLIENT) $(HDRS)
	$(CXX) $(CXXFLAGS) -o $@ $(SRCS_CLIENT)

clean:
	rm -f $(TARGETS)

# Helpers (optional)
run-server: server
	./server 9000 0.0 0.0

run-client: client
	./client 127.0.0.1 9000 arquivo_teste.bin