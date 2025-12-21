#pragma once

#include "ZuesAPI.h"
#include <string>
#include <vector>
#include <memory>
#include <functional>

namespace Engine {

    struct NetworkPeer {
        uint32_t id;
        void* internalPeer; // ENetPeer*
    };

    struct NetworkMessage {
        NetworkPeer peer;
        std::vector<uint8_t> data;
        uint8_t channel;
    };

    enum class NetworkEventType {
        ClientConnected,
        ClientDisconnected,
        DataReceived
    };

    struct NetworkEvent {
        NetworkEventType type;
        NetworkPeer peer;
        std::vector<uint8_t> data;
        uint8_t channel;
    };

    class ZUES_API Network {
    public:
        using EventCallback = std::function<void(const NetworkEvent&)>;

        Network();
        ~Network();

        static bool InitializeSystem();
        static void ShutdownSystem();

        // Setup
        [[nodiscard]] bool StartHost(const std::string& address, uint16_t port, size_t maxClients = 32) const;
        [[nodiscard]] bool StartClient(const std::string& host, uint16_t port) const;
        void Stop() const;

        // Communication
        bool SendToAll(const void* data, size_t size, uint8_t channel = 0, bool reliable = true) const;
        bool SendToPeer(NetworkPeer peer, const void* data, size_t size, uint8_t channel = 0, bool reliable = true) const;
        bool SendToServer(const void* data, size_t size, uint8_t channel = 0, bool reliable = true) const;

        // Update loop - call every frame
        void PollEvents() const;

        // Event handling
        void SetEventCallback(EventCallback callback) const;

        // State queries
        [[nodiscard]] bool IsHost() const;
        [[nodiscard]] bool IsClient() const;
        [[nodiscard]] bool IsConnected() const;
        [[nodiscard]] std::vector<NetworkPeer> GetConnectedPeers() const;

    private:
        struct Impl;
        std::unique_ptr<Impl> pImpl;

        static bool s_Initialized;
    };

} // namespace Engine