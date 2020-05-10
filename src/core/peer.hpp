#pragma once

#include "base/time.hpp"
#include "core/address.hpp"
#include "core/block.hpp"
#include "net/session.hpp"

#include <forward_list>
#include <memory>

namespace lk
{

class Core;
class Host;


class PeerBase
{
  public:
    //===========================
    struct Info
    {
        net::Endpoint endpoint;
        lk::Address address;

        static Info deserialize(base::SerializationIArchive& ia);
        void serialize(base::SerializationOArchive& oa) const;
    };
    //===========================
    virtual bool isClosed() const = 0;
    //===========================
    virtual void send(const base::Bytes& data, net::Connection::SendHandler on_send = {}) = 0;
    virtual void send(base::Bytes&& data, net::Connection::SendHandler on_send = {}) = 0;

    virtual const lk::Address& getAddress() const noexcept = 0;
    virtual Info getInfo() const = 0;
    virtual base::Time getLastSeen() const = 0;
    virtual net::Endpoint getEndpoint() const = 0;
    virtual net::Endpoint getPublicEndpoint() const = 0;
    //===========================
    virtual bool tryAddToPool() = 0;
    //===========================
};


class PeerPoolBase
{
  public:
    virtual bool tryAddPeer(std::shared_ptr<PeerBase> peer) = 0;
    virtual void removePeer(std::shared_ptr<PeerBase> peer) = 0;
    virtual void removePeer(PeerBase* peer) = 0;

    virtual void forEachPeer(std::function<void(const PeerBase&)> f) const = 0;
    virtual void forEachPeer(std::function<void(PeerBase&)> f) = 0;

    virtual void broadcast(const base::Bytes& bytes) = 0;

    virtual std::vector<PeerBase::Info> allPeersInfo() const = 0;
};


/*
 * Protocol doesn't manage states of session or peer.
 * It just prepares, sends and handles messages.
 */
class ProtocolBase : public net::Session::Handler
{
  public:
    // event-processing functions
    void onReceive(const base::Bytes& bytes) override = 0;
    void onClose() override = 0;

    // functions, initiated by caller code
    virtual void sendTransaction(const lk::Transaction& tx) = 0;
    virtual void sendBlock(const lk::Block& block) = 0;
    virtual void sendSessionEnd(std::function<void()> on_send) = 0; // TODO: set close reason
};


class Peer : public PeerBase, public std::enable_shared_from_this<Peer>
{
  public:
    //================
    enum class State
    {
        JUST_ESTABLISHED,
        REQUESTED_BLOCKS,
        SYNCHRONISED
    };
    //================
    static std::shared_ptr<Peer> accepted(std::unique_ptr<net::Session> session, lk::Host& host, lk::Core& core);
    static std::shared_ptr<Peer> connected(std::unique_ptr<net::Session> session, lk::Host& host, lk::Core& core);
    //================
    bool tryAddToPool() override;
    //================
    base::Time getLastSeen() const override;
    net::Endpoint getEndpoint() const override;
    net::Endpoint getPublicEndpoint() const override;
    void setServerEndpoint(net::Endpoint endpoint);

    void setProtocol(std::shared_ptr<lk::ProtocolBase> protocol);
    void start();
    //================
    const lk::Address& getAddress() const noexcept override;
    void setAddress(lk::Address address);
    //================
    void setState(State new_state);
    State getState() const noexcept;

    Info getInfo() const override;
    bool isClosed() const;
    //================
    void addSyncBlock(lk::Block block);
    void applySyncs();
    const std::forward_list<lk::Block>& getSyncBlocks() const noexcept;
    //================
    void send(const base::Bytes& data, net::Connection::SendHandler on_send = {}) override;
    void send(base::Bytes&& data, net::Connection::SendHandler on_send = {}) override;

    void send(const lk::Block& block);
    void send(const lk::Transaction& tx);
    //================
  private:
    //================
    Peer(std::unique_ptr<net::Session> session, lk::PeerPoolBase& pool, lk::Core& core);
    //================
    std::unique_ptr<net::Session> _session;
    //================
    State _state{ State::JUST_ESTABLISHED };
    std::optional<net::Endpoint> _endpoint_for_incoming_connections;
    lk::Address _address;
    //================
    std::forward_list<lk::Block> _sync_blocks;
    //================
    bool _is_attached_to_pool{ false };
    lk::PeerPoolBase& _pool;
    lk::Core& _core;
    std::shared_ptr<lk::ProtocolBase> _protocol;
    //================
    void rejectedByPool();
    //================
};

}