#pragma once

#include <WS2tcpip.h>
#include <WinSock2.h>

#include <cstdint>
#include <mutex>
#include <string>

class UDPSender final {
    uint16_t const port;
    SOCKET sock = INVALID_SOCKET;

  public:
    UDPSender(uint16_t port) : port(port) {
        // WSAStartup()/WSACleanup() pairs are reference counted
        WSAData data;
        int err = WSAStartup(MAKEWORD(2, 2), &data);
        if (err)
            return;

        sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    }

    ~UDPSender() {
        if (sock != INVALID_SOCKET)
            closesocket(sock);

        WSACleanup();
    }

    void SendMsg(std::string message) {
        sockaddr_in addr;
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        InetPtonA(AF_INET, "127.0.0.1", &addr.sin_addr);
        sendto(sock, message.c_str(), static_cast<int>(message.size()), 0,
               (SOCKADDR *)&addr, sizeof(addr));
    }
};
