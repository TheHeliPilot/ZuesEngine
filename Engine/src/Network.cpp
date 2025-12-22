#include "../include/Engine/Network.h"

// Define ENET_IMPLEMENTATION to include the ENet implementation directly
// This is required because ENet is a single-header library
#define ENET_IMPLEMENTATION
#include <enet.h>

#include <unordered_map>
#include <sstream>
#include "../include/Engine/EngineDefines.h"

namespace Engine {

bool Network::s_Initialized = false;

struct Network::Impl {
    ENetHost* host = nullptr;
    ENetPeer* serverPeer = nullptr;
    ENetAddress address = {};

    enum class Role { None, Host, Client };
    Role role = Role::None;

    EventCallback eventCallback = nullptr;

    // Track peers with unique IDs
    std::unordered_map<ENetPeer*, uint32_t> peerToId;
    std::unordered_map<uint32_t, ENetPeer*> idToPeer;
    uint32_t nextPeerId = 1;

    uint32_t GetOrCreatePeerId(ENetPeer* peer) {
        if (const auto it = peerToId.find(peer); it != peerToId.end()) {
            return it->second;
        }

        const uint32_t id = nextPeerId++;
        peerToId[peer] = id;
        idToPeer[id] = peer;
        return id;
    }

    void RemovePeer(ENetPeer* peer) {
        if (const auto it = peerToId.find(peer); it != peerToId.end()) {
            idToPeer.erase(it->second);
            peerToId.erase(it);
        }
    }

    ENetPeer* GetPeerById(const uint32_t id) {
        const auto it = idToPeer.find(id);
        return (it != idToPeer.end()) ? it->second : nullptr;
    }
};

Network::Network() : pImpl(std::make_unique<Impl>()) {
}

Network::~Network() {
    Stop();
}

bool Network::InitializeSystem() {
    if (s_Initialized) {
        LOG_WARN("Network system already initialized.");
        return true;
    }

    if (enet_initialize() != 0) {
        LOG_ERROR("ENet initialization failed.");
        return false;
    }

    s_Initialized = true;
    LOG_INFO("Network system initialized.");
    return true;
}

void Network::ShutdownSystem() {
    if (!s_Initialized) {
        return;
    }

    enet_deinitialize();
    s_Initialized = false;
    LOG_INFO("Network system shut down.");
}

bool Network::StartHost(const std::string& address, const uint16_t port, const size_t maxClients) const {
    if (!s_Initialized) {
        LOG_ERROR("Network system not initialized. Call InitializeSystem() first.");
        return false;
    }

    if (pImpl->host) {
        LOG_ERROR("Already hosting or connected. Call Stop() first.");
        return false;
    }

    enet_address_set_host(&pImpl->address, address.c_str());
    pImpl->address.port = port;

    // 2 channels: 0 = reliable, 1 = unreliable
    pImpl->host = enet_host_create(&pImpl->address, maxClients, 2, 0, 0);
    if (!pImpl->host) {
        LOG_ERROR("Failed to create ENet host.");
        return false;
    }

    pImpl->role = Impl::Role::Host;

    std::ostringstream oss;
    oss << "Started hosting on " << address << ":" << port << " (max " << maxClients << " clients)";
    LOG_INFO(oss.str());

    return true;
}

bool Network::StartClient(const std::string& host, const uint16_t port) const {
    if (!s_Initialized) {
        LOG_ERROR("Network system not initialized. Call InitializeSystem() first.");
        return false;
    }

    if (pImpl->host) {
        LOG_ERROR("Already hosting or connected. Call Stop() first.");
        return false;
    }

    // Client only needs 1 peer connection
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
        enet_host_destroy(pImpl->host);
        pImpl->host = nullptr;
        return false;
    }

    pImpl->role = Impl::Role::Client;

    std::ostringstream oss;
    oss << "Connecting to " << host << ":" << port << "...";
    LOG_INFO(oss.str());

    return true;
}

void Network::Stop() const {
    if (!pImpl->host) {
        return;
    }

    // Disconnect all peers gracefully
    if (pImpl->role == Impl::Role::Client && pImpl->serverPeer) {
        enet_peer_disconnect(pImpl->serverPeer, 0);
    } else if (pImpl->role == Impl::Role::Host) {
        for (size_t i = 0; i < pImpl->host->peerCount; i++) {
            if (pImpl->host->peers[i].state == ENET_PEER_STATE_CONNECTED) {
                enet_peer_disconnect(&pImpl->host->peers[i], 0);
            }
        }
    }

    // Allow disconnection events to be sent
    ENetEvent event;
    while (enet_host_service(pImpl->host, &event, 100) > 0) {
        if (event.type == ENET_EVENT_TYPE_RECEIVE) {
            enet_packet_destroy(event.packet);
        }
    }

    enet_host_destroy(pImpl->host);
    pImpl->host = nullptr;
    pImpl->serverPeer = nullptr;
    pImpl->role = Impl::Role::None;

    pImpl->peerToId.clear();
    pImpl->idToPeer.clear();
    pImpl->nextPeerId = 1;

    LOG_INFO("Network stopped.");
}

bool Network::SendToAll(const void* data, const size_t size, const uint8_t channel, const bool reliable) const {
    if (!pImpl->host || pImpl->role != Impl::Role::Host) {
        LOG_ERROR("Can only send to all clients when hosting.");
        return false;
    }

    ENetPacket* packet = enet_packet_create(
        data,
        size,
        reliable ? ENET_PACKET_FLAG_RELIABLE : ENET_PACKET_FLAG_UNSEQUENCED
    );

    bool sentToAny = false;
    for (size_t i = 0; i < pImpl->host->peerCount; i++) {
        if (pImpl->host->peers[i].state == ENET_PEER_STATE_CONNECTED) {
            enet_peer_send(&pImpl->host->peers[i], channel, packet);
            sentToAny = true;
        }
    }

    if (sentToAny) {
        enet_host_flush(pImpl->host);
    } else {
        enet_packet_destroy(packet);
    }

    return sentToAny;
}

bool Network::SendToPeer(const NetworkPeer peer, const void* data, const size_t size, const uint8_t channel, const bool reliable) const {
    if (!pImpl->host || pImpl->role != Impl::Role::Host) {
        LOG_ERROR("Can only send to specific peer when hosting.");
        return false;
    }

    ENetPeer* enetPeer = pImpl->GetPeerById(peer.id);
    if (!enetPeer || enetPeer->state != ENET_PEER_STATE_CONNECTED) {
        std::ostringstream oss;
        oss << "Peer " << peer.id << " not found or not connected.";
        LOG_ERROR(oss.str());
        return false;
    }

    ENetPacket* packet = enet_packet_create(
        data,
        size,
        reliable ? ENET_PACKET_FLAG_RELIABLE : ENET_PACKET_FLAG_UNSEQUENCED
    );

    enet_peer_send(enetPeer, channel, packet);
    enet_host_flush(pImpl->host);
    return true;
}

bool Network::SendToServer(const void* data, const size_t size, const uint8_t channel, const bool reliable) const {
    if (!pImpl->host || pImpl->role != Impl::Role::Client) {
        LOG_ERROR("Can only send to server when connected as client.");
        return false;
    }

    if (!pImpl->serverPeer || pImpl->serverPeer->state != ENET_PEER_STATE_CONNECTED) {
        LOG_ERROR("Not connected to server.");
        return false;
    }

    ENetPacket* packet = enet_packet_create(
        data,
        size,
        reliable ? ENET_PACKET_FLAG_RELIABLE : ENET_PACKET_FLAG_UNSEQUENCED
    );

    enet_peer_send(pImpl->serverPeer, channel, packet);
    enet_host_flush(pImpl->host);
    return true;
}

void Network::PollEvents() const {
    if (!pImpl->host) {
        return;
    }

    ENetEvent event;
    while (enet_host_service(pImpl->host, &event, 0) > 0) {
        NetworkEvent netEvent = {};

        switch (event.type) {
            case ENET_EVENT_TYPE_CONNECT: {
                uint32_t peerId = pImpl->GetOrCreatePeerId(event.peer);
                netEvent.type = NetworkEventType::ClientConnected;
                netEvent.peer.id = peerId;
                netEvent.peer.internalPeer = event.peer;

                std::ostringstream oss;
                oss << "Client " << peerId << " connected.";
                LOG_INFO(oss.str());

                if (pImpl->eventCallback) {
                    pImpl->eventCallback(netEvent);
                }
                break;
            }

            case ENET_EVENT_TYPE_RECEIVE: {
                uint32_t peerId = pImpl->GetOrCreatePeerId(event.peer);
                netEvent.type = NetworkEventType::DataReceived;
                netEvent.peer.id = peerId;
                netEvent.peer.internalPeer = event.peer;
                netEvent.channel = event.channelID;
                netEvent.data.assign(
                    event.packet->data,
                    event.packet->data + event.packet->dataLength
                );

                if (pImpl->eventCallback) {
                    pImpl->eventCallback(netEvent);
                }

                enet_packet_destroy(event.packet);
                break;
            }

            case ENET_EVENT_TYPE_DISCONNECT: {
                uint32_t peerId = pImpl->GetOrCreatePeerId(event.peer);
                netEvent.type = NetworkEventType::ClientDisconnected;
                netEvent.peer.id = peerId;
                netEvent.peer.internalPeer = event.peer;

                std::ostringstream oss;
                oss << "Client " << peerId << " disconnected.";
                LOG_INFO(oss.str());

                if (pImpl->eventCallback) {
                    pImpl->eventCallback(netEvent);
                }

                pImpl->RemovePeer(event.peer);
                event.peer->data = nullptr;
                break;
            }

            default:
                break;
        }
    }
}

void Network::SetEventCallback(EventCallback callback) const {
    pImpl->eventCallback = std::move(callback);
}

bool Network::IsHost() const {
    return pImpl->role == Impl::Role::Host;
}

bool Network::IsClient() const {
    return pImpl->role == Impl::Role::Client;
}

bool Network::IsConnected() const {
    if (pImpl->role == Impl::Role::Client) {
        return pImpl->serverPeer && pImpl->serverPeer->state == ENET_PEER_STATE_CONNECTED;
    }
    return pImpl->host != nullptr;
}

std::vector<NetworkPeer> Network::GetConnectedPeers() const {
    std::vector<NetworkPeer> peers;

    if (!pImpl->host) {
        return peers;
    }

    for (const auto& [id, peer] : pImpl->idToPeer) {
        if (peer->state == ENET_PEER_STATE_CONNECTED) {
            NetworkPeer netPeer;
            netPeer.id = id;
            netPeer.internalPeer = peer;
            peers.push_back(netPeer);
        }
    }

    return peers;
}

} // namespace Engine