#pragma once
#include <string>
#include <vector>
#include <memory>

namespace Engine {

    class Network {
    public:
        enum class Role { None, Host, Client };

        Network();
        ~Network();

        bool Init();
        void Shutdown();

        bool Host(const std::string& address, uint16_t port, size_t maxClients = 32) const;
        bool Connect(const std::string& host, uint16_t port) const;
        void Disconnect() const;

        bool Send(const void* data, size_t size, uint8_t channel = 0) const;
        bool Receive(std::vector<uint8_t>& outData, uint8_t& channel) const;

        void Update();

        Role GetRole() const;

    private:
        struct Impl;
        Impl* pImpl;
    };

} // namespace Engine
