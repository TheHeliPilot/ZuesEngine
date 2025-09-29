#include "../include/Engine/Network.h"
#include <enet.h>

#include "../include/Engine/EngineDefines.h"

struct Engine::Network::Impl {
    ENetHost* host = nullptr;
    ENetPeer* serverPeer = nullptr;
    ENetAddress address;
    Role role = Role::None;
};

Engine::Network::Network() {
    pImpl = new Impl();
}

Engine::Network::~Network() {
    Shutdown();
}

bool Engine::Network::Init() {
    if (enet_initialize() != 0) {
        LOG_ERROR("ENet initialization failed.");
        return false;
    }
    return true;
}

void Engine::Network::Shutdown() {
    if (pImpl->host) {
        enet_host_destroy(pImpl->host);
    }
    enet_deinitialize();
    delete pImpl;
    pImpl = nullptr;
}

bool Engine::Network::Host(const std::string& address, const uint16_t port, const size_t maxClients) const {
    enet_address_set_host(&pImpl->address, address.c_str());
    pImpl->address.port = port;

    pImpl->host = enet_host_create(&pImpl->address, maxClients, 2, 0, 0);
    if (!pImpl->host) {
        LOG_ERROR("Failed to create ENet host.");
        return false;
    }

    pImpl->role = Role::Host;
    return true;
}

bool Engine::Network::Connect(const std::string& host, const uint16_t port) const {
    pImpl->host = enet_host_create(nullptr, 1, 2, 0, 0);
    if (!pImpl->host) {
        LOG_ERROR("Failed to create ENet client host.");
        return false;
    }

    enet_address_set_host(&pImpl->address, host.c_str());
    pImpl->address.port = port;

    pImpl->serverPeer = enet_host_connect(pImpl->host, &pImpl->address, 2, 0);
    if (!pImpl->serverPeer) {
        LOG_ERROR("No available peers for connection.");
        return false;
    }

    pImpl->role = Role::Client;
    return true;
}

void Engine::Network::Disconnect() const {
    if (pImpl->role == Role::Client && pImpl->serverPeer) {
        enet_peer_disconnect(pImpl->serverPeer, 0);
    } else if (pImpl->role == Role::Host && pImpl->host) {
        for (size_t i = 0; i < pImpl->host->peerCount; i++) {
            enet_peer_disconnect(&pImpl->host->peers[i], 0);
        }
    }
}

bool Engine::Network::Send(const void* data, const size_t size, const uint8_t channel) const {
    if (!pImpl->host) return false;

    ENetPacket* packet = enet_packet_create(data, size, ENET_PACKET_FLAG_RELIABLE);
    if (pImpl->role == Role::Host) {
        for (size_t i = 0; i < pImpl->host->peerCount; i++) {
            if (pImpl->host->peers[i].state == ENET_PEER_STATE_CONNECTED) {
                enet_peer_send(&pImpl->host->peers[i], channel, packet);
            }
        }
    } else if (pImpl->role == Role::Client) {
        enet_peer_send(pImpl->serverPeer, channel, packet);
    }
    enet_host_flush(pImpl->host);
    return true;
}

bool Engine::Network::Receive(std::vector<uint8_t>& outData, uint8_t& channel) const {
    if (!pImpl->host) return false;

    ENetEvent event;
    while (enet_host_service(pImpl->host, &event, 0) > 0) {
        switch (event.type) {
            case ENET_EVENT_TYPE_CONNECT:
                LOG_INFO("Connection established.");
                break;
            case ENET_EVENT_TYPE_RECEIVE:
                channel = event.channelID;
                outData.assign(event.packet->data, event.packet->data + event.packet->dataLength);
                enet_packet_destroy(event.packet);
                return true;
            case ENET_EVENT_TYPE_DISCONNECT:
                LOG_INFO("Disconnected.");
                break;
            default:
                break;
        }
    }
    return false;
}

void Engine::Network::Update() {
    std::vector<uint8_t> data;
    if (uint8_t channel = 0; Receive(data, channel)) {
        // Handle data here
    }
}

Engine::Network::Role Engine::Network::GetRole() const {
    return pImpl->role;
}
