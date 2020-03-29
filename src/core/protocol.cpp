#include "protocol.hpp"

#include "base/log.hpp"
#include "base/serialization.hpp"
#include "core/core.hpp"

/*
 * Some functions, that are implemented and later used inside templates are shown as not used.
 * So, warnings for unused functions are just disabled for this file.
 */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"

namespace lk
{

namespace
{

template<typename M, typename... Args>
base::Bytes prepareMessage(Args&&... args)
{
    LOG_TRACE << "Serializing " << enumToString(M::getHandledMessageType());
    base::SerializationOArchive oa;
    oa.serialize(M::getHandledMessageType());
    (oa.serialize(std::forward<Args>(args)), ...);
    return std::move(oa).getBytes();
}


class Dummy {};

template<typename C, typename F, typename... O>
bool runHandleImpl([[maybe_unused]] lk::MessageType mt, base::SerializationIArchive& ia, const C& ctx, lk::Protocol& protocol)
{
    if constexpr(std::is_same<F, Dummy>::value) {
        return false;
    }
    else {
        if (F::getHandledMessageType() == mt) {
            auto message = F::deserialize(ia);
            message.handle(ctx, protocol);
            return true;
        }
        else {
            return runHandleImpl<C, O...>(mt, ia, ctx, protocol);
        }
    }
}


template<typename C, typename... Args>
bool runHandle(lk::MessageType mt, base::SerializationIArchive& ia, const C& ctx, lk::Protocol& protocol)
{
    if(runHandleImpl<C, Args..., Dummy>(mt, ia, ctx, protocol)) {
        protocol.getState().last_processed = mt;
        return true;
    }
    else {
        return false;
    }
}


template<typename C>
class MessageProcessor
{
  public:
    //===============
    explicit MessageProcessor(const C& context)
        : _ctx{context}
    {}

    /*
     * Decode and act according to received data.
     * Throws if invalid message, or there is an error during handling.
     */
    void process(const base::Bytes& raw_message, lk::Protocol& protocol)
    {
        using namespace lk;

        base::SerializationIArchive ia(raw_message);
        auto type = ia.deserialize<lk::MessageType>();

        const auto& waiting_for = protocol.getState().message_we_are_waiting_for;
        if(waiting_for != MessageType::NOT_AVAILABLE && waiting_for != type) {
            // not the message we expected
            // TODO: decrease rating
            return;
        }

        if (runHandle<lk::Protocol::Context,
                      AcceptedMessage,
          AcceptedResponseMessage,
          PingMessage,
          PongMessage,
          TransactionMessage,
          GetBlockMessage,
          BlockMessage,
          BlockNotFoundMessage,
          GetInfoMessage,
          InfoMessage,
          NewNodeMessage,
          CloseMessage>(type, ia, _ctx, protocol)) {
            LOG_DEBUG << "Processed " << enumToString(type) << " message";
        }
        else {
            RAISE_ERROR(base::InvalidArgument, "invalid message type");
        }
    }

  private:
    const C& _ctx;
};


std::vector<lk::PeerBase::Info> allPeersInfoExcept(lk::Host& host, const lk::Address& address)
{
    auto ret = host.allConnectedPeersInfo();
    ret.erase(std::find_if(ret.begin(), ret.end(), [address](const auto& cand) {
        return cand.address == address;
    }));
    return ret;
}


} // namespace

//============================================

constexpr lk::MessageType CannotAcceptMessage::getHandledMessageType()
{
    return lk::MessageType::CANNOT_ACCEPT;
}


void CannotAcceptMessage::serialize(base::SerializationOArchive& oa, CannotAcceptMessage::RefusionReason why_not_accepted)
{
    oa.serialize(getHandledMessageType());
    oa.serialize(why_not_accepted);
}


CannotAcceptMessage CannotAcceptMessage::deserialize(base::SerializationIArchive& ia)
{
    auto why_not_accepted = ia.deserialize<RefusionReason>();
    auto peers_info = ia.deserialize<std::vector<lk::PeerBase::Info>>();
    return CannotAcceptMessage(why_not_accepted, std::move(peers_info));
}


void CannotAcceptMessage::handle(const lk::Protocol::Context& ctx, lk::Protocol&)
{
    ctx.pool->removePeer(ctx.peer);

    for(const auto& peer : _peers_info) {
        ctx.host->checkOutPeer(peer.endpoint);
    }
}


CannotAcceptMessage::CannotAcceptMessage(CannotAcceptMessage::RefusionReason why_not_accepted, std::vector<lk::PeerBase::Info> peers_info)
  : _why_not_accepted{why_not_accepted}, _peers_info{std::move(peers_info)}
{}

//============================================

constexpr lk::MessageType AcceptedMessage::getHandledMessageType()
{
    return lk::MessageType::ACCEPTED;
}


void AcceptedMessage::serialize(base::SerializationOArchive& oa,
                                 const lk::Block& block,
                                 const lk::Address& address,
                                 std::uint16_t public_port,
                                 const std::vector<lk::Peer::Info>& known_peers)
{
    oa.serialize(getHandledMessageType());
    oa.serialize(block);
    oa.serialize(address);
    oa.serialize(public_port);
    oa.serialize(known_peers);
}


AcceptedMessage AcceptedMessage::deserialize(base::SerializationIArchive& ia)
{
    auto top_block = ia.deserialize<lk::Block>();
    auto address = ia.deserialize<lk::Address>();
    auto public_port = ia.deserialize<std::uint16_t>();
    auto known_peers = ia.deserialize<std::vector<lk::Peer::Info>>();
    return AcceptedMessage(std::move(top_block), std::move(address), public_port, std::move(known_peers));
}


void AcceptedMessage::handle(const lk::Protocol::Context& ctx, lk::Protocol&)
{
    auto& peer = *ctx.peer;
    auto& host = *ctx.host;

    const auto& ours_top_block = ctx.core->getTopBlock();

    peer.send(prepareMessage<AcceptedResponseMessage>(ctx.core->getTopBlock(),
                                                      ctx.core->getThisNodeAddress(),
                                                      ctx.peer->getPublicEndpoint().getPort(),
                                                      allPeersInfoExcept(host, peer.getAddress())));

    if (_public_port) {
        auto public_ep = peer.getEndpoint();
        public_ep.setPort(_public_port);
        peer.setServerEndpoint(public_ep);
    }

    for (const auto& peer_info : _known_peers) {
        host.checkOutPeer(peer_info.endpoint);
    }

    if (_theirs_top_block == ours_top_block) {
        peer.setState(lk::Peer::State::SYNCHRONISED);
        return; // nothing changes, because top blocks are equal
    }
    else {
        if (ours_top_block.getDepth() > _theirs_top_block.getDepth()) {
            peer.setState(lk::Peer::State::SYNCHRONISED);
            // do nothing, because we are ahead of this peer and we don't need to sync: this node might sync
            return;
        }
        else {
            if (ctx.core->getTopBlock().getDepth() + 1 == _theirs_top_block.getDepth()) {
                ctx.core->tryAddBlock(_theirs_top_block);
                peer.setState(lk::Peer::State::SYNCHRONISED);
            }
            else {
                base::SerializationOArchive oa;
                GetBlockMessage::serialize(oa, _theirs_top_block.getPrevBlockHash());
                peer.send(std::move(oa).getBytes());
                peer.setState(lk::Peer::State::REQUESTED_BLOCKS);
                peer.addSyncBlock(std::move(_theirs_top_block));
            }
        }
    }
}


AcceptedMessage::AcceptedMessage(lk::Block&& top_block,
                                   lk::Address address,
                                   std::uint16_t public_port,
                                   std::vector<lk::Peer::Info>&& known_peers)
  : _theirs_top_block{ std::move(top_block) }
  , _address{ std::move(address) }
  , _public_port{ public_port }
  , _known_peers{ std::move(known_peers) }
{}

//============================================

constexpr lk::MessageType AcceptedResponseMessage::getHandledMessageType()
{
    return lk::MessageType::ACCEPTED_RESPONSE;
}


void AcceptedResponseMessage::serialize(base::SerializationOArchive& oa,
                                const lk::Block& block,
                                const lk::Address& address,
                                std::uint16_t public_port,
                                const std::vector<lk::Peer::Info>& known_peers)
{
    oa.serialize(getHandledMessageType());
    oa.serialize(block);
    oa.serialize(address);
    oa.serialize(public_port);
    oa.serialize(known_peers);
}


AcceptedResponseMessage AcceptedResponseMessage::deserialize(base::SerializationIArchive& ia)
{
    auto top_block = ia.deserialize<lk::Block>();
    auto address = ia.deserialize<lk::Address>();
    auto public_port = ia.deserialize<std::uint16_t>();
    auto known_peers = ia.deserialize<std::vector<lk::Peer::Info>>();
    return AcceptedResponseMessage(std::move(top_block), std::move(address), public_port, std::move(known_peers));
}


void AcceptedResponseMessage::handle(const lk::Protocol::Context& ctx, lk::Protocol&)
{
    auto& peer = *ctx.peer;
    auto& host = *ctx.host;

    const auto& ours_top_block = ctx.core->getTopBlock();

    if (_public_port) {
        auto public_ep = peer.getEndpoint();
        public_ep.setPort(_public_port);
        peer.setServerEndpoint(public_ep);
    }

    for (const auto& peer_info : _known_peers) {
        host.checkOutPeer(peer_info.endpoint);
    }

    if (_theirs_top_block == ours_top_block) {
        peer.setState(lk::Peer::State::SYNCHRONISED);
        return; // nothing changes, because top blocks are equal
    }
    else {
        if (ours_top_block.getDepth() > _theirs_top_block.getDepth()) {
            peer.setState(lk::Peer::State::SYNCHRONISED);
            // do nothing, because we are ahead of this peer and we don't need to sync: this node might sync
            return;
        }
        else {
            if (ctx.core->getTopBlock().getDepth() + 1 == _theirs_top_block.getDepth()) {
                ctx.core->tryAddBlock(_theirs_top_block);
                peer.setState(lk::Peer::State::SYNCHRONISED);
            }
            else {
                base::SerializationOArchive oa;
                GetBlockMessage::serialize(oa, _theirs_top_block.getPrevBlockHash());
                peer.send(std::move(oa).getBytes());
                peer.setState(lk::Peer::State::REQUESTED_BLOCKS);
                peer.addSyncBlock(std::move(_theirs_top_block));
            }
        }
    }
}


AcceptedResponseMessage::AcceptedResponseMessage(lk::Block&& top_block,
                                 lk::Address address,
                                 std::uint16_t public_port,
                                 std::vector<lk::Peer::Info>&& known_peers)
  : _theirs_top_block{ std::move(top_block) }
  , _address{ std::move(address) }
  , _public_port{ public_port }
  , _known_peers{ std::move(known_peers) }
{}

//============================================

constexpr lk::MessageType PingMessage::getHandledMessageType()
{
    return lk::MessageType::PING;
}


void PingMessage::serialize(base::SerializationOArchive& oa)
{
    oa.serialize(lk::MessageType::PING);
}


PingMessage PingMessage::deserialize(base::SerializationIArchive&)
{
    return {};
}


void PingMessage::handle(const lk::Protocol::Context&, lk::Protocol&) {}


//============================================

constexpr lk::MessageType PongMessage::getHandledMessageType()
{
    return lk::MessageType::PONG;
}


void PongMessage::serialize(base::SerializationOArchive& oa)
{
    oa.serialize(lk::MessageType::PONG);
}


PongMessage PongMessage::deserialize(base::SerializationIArchive&)
{
    return {};
}


void PongMessage::handle(const lk::Protocol::Context&, lk::Protocol&) {}

//============================================

constexpr lk::MessageType LookupMessage::getHandledMessageType()
{
    return lk::MessageType::LOOKUP;
}


void LookupMessage::serialize(base::SerializationOArchive& oa, const lk::Address& address, std::uint8_t selection_size)
{
    oa.serialize(lk::MessageType::LOOKUP);
    oa.serialize(address);
    oa.serialize(selection_size);
}


LookupMessage LookupMessage::deserialize(base::SerializationIArchive& ia)
{
}


void LookupMessage::handle(const lk::Protocol::Context& ctx, lk::Protocol&)
{
    ctx.peer->send(prepareMessage<LookupResponseMessage>(ctx.pool->lookup(_address, _selection_size)));
}


LookupMessage::LookupMessage(lk::Address address, std::uint8_t selection_size)
    : _address{std::move(address)}, _selection_size{selection_size}
{}

//============================================

constexpr lk::MessageType LookupResponseMessage::getHandledMessageType()
{
    return lk::MessageType::LOOKUP;
}


void LookupResponseMessage::serialize(base::SerializationOArchive& oa, const std::vector<lk::PeerBase::Info>& peers_info)
{
    oa.serialize(lk::MessageType::LOOKUP_RESPONSE);
    oa.serialize(peers_info);
}


LookupResponseMessage LookupResponseMessage::deserialize(base::SerializationIArchive& ia)
{
}


void LookupResponseMessage::handle(const lk::Protocol::Context& ctx, lk::Protocol& protocol)
{
    // either we continue to ask for closest nodes or just connect to them
    // TODO: a peer table, where we ask for LOOKUP, and collect their responds + change the very beginning of communication:
    // now it is not necessary to do a HANDSHAKE if we just want to ask for LOOKUP
}


LookupResponseMessage::LookupResponseMessage(std::vector<lk::PeerBase::Info> peers_info)
  : _peers_info{std::move(peers_info)}
{}

//============================================

constexpr lk::MessageType TransactionMessage::getHandledMessageType()
{
    return lk::MessageType::TRANSACTION;
}


void TransactionMessage::serialize(base::SerializationOArchive& oa, const lk::Transaction& tx)
{
    oa.serialize(lk::MessageType::TRANSACTION);
    oa.serialize(tx);
}


TransactionMessage TransactionMessage::deserialize(base::SerializationIArchive& ia)
{
    auto tx = lk::Transaction::deserialize(ia);
    return { std::move(tx) };
}


void TransactionMessage::handle(const lk::Protocol::Context& ctx, lk::Protocol&)
{
    ctx.core->addPendingTransaction(_tx);
}


TransactionMessage::TransactionMessage(const lk::Transaction& tx)
  : _tx{ std::move(tx) }
{}

//============================================

constexpr lk::MessageType GetBlockMessage::getHandledMessageType()
{
    return lk::MessageType::GET_BLOCK;
}


void GetBlockMessage::serialize(base::SerializationOArchive& oa, const base::Sha256& block_hash)
{
    oa.serialize(lk::MessageType::GET_BLOCK);
    oa.serialize(block_hash);
}


GetBlockMessage GetBlockMessage::deserialize(base::SerializationIArchive& ia)
{
    auto block_hash = base::Sha256::deserialize(ia);
    return { std::move(block_hash) };
}


void GetBlockMessage::handle(const lk::Protocol::Context& ctx, lk::Protocol&)
{
    LOG_DEBUG << "Received GET_BLOCK on " << _block_hash;
    auto block = ctx.core->findBlock(_block_hash);
    if (block) {
        ctx.peer->send(prepareMessage<BlockMessage>(*block));
    }
    else {
        ctx.peer->send(prepareMessage<BlockNotFoundMessage>(_block_hash));
    }
}


GetBlockMessage::GetBlockMessage(base::Sha256 block_hash)
  : _block_hash{ std::move(block_hash) }
{}

//============================================

constexpr lk::MessageType BlockMessage::getHandledMessageType()
{
    return lk::MessageType::BLOCK;
}


void BlockMessage::serialize(base::SerializationOArchive& oa, const lk::Block& block)
{
    oa.serialize(lk::MessageType::BLOCK);
    oa.serialize(block);
}


BlockMessage BlockMessage::deserialize(base::SerializationIArchive& ia)
{
    auto block = lk::Block::deserialize(ia);
    return { std::move(block) };
}


void BlockMessage::handle(const lk::Protocol::Context& ctx, lk::Protocol&)
{
    auto& peer = *ctx.peer;
    if (peer.getState() == lk::Peer::State::SYNCHRONISED) {
        // we're synchronised already

        if(ctx.core->tryAddBlock(_block)) {
            // block added, all is OK
        }
        else {
            // in this case we are missing some blocks
        }
    }
    else {
        // we are in synchronization process
        lk::BlockDepth block_depth = _block.getDepth();
        peer.addSyncBlock(std::move(_block));

        if (block_depth == ctx.core->getTopBlock().getDepth() + 1) {
            peer.applySyncs();
        }
        else {
            ctx.peer->send(prepareMessage<GetBlockMessage>(peer.getSyncBlocks().front().getPrevBlockHash()));
        }
    }
}


BlockMessage::BlockMessage(lk::Block block)
  : _block{ std::move(block) }
{}

//============================================

constexpr lk::MessageType BlockNotFoundMessage::getHandledMessageType()
{
    return lk::MessageType::BLOCK_NOT_FOUND;
}


void BlockNotFoundMessage::serialize(base::SerializationOArchive& oa, const base::Sha256& block_hash)
{
    oa.serialize(lk::MessageType::BLOCK_NOT_FOUND);
    oa.serialize(block_hash);
}


BlockNotFoundMessage BlockNotFoundMessage::deserialize(base::SerializationIArchive& ia)
{
    auto block_hash = base::Sha256::deserialize(ia);
    return { std::move(block_hash) };
}


void BlockNotFoundMessage::handle(const lk::Protocol::Context&, lk::Protocol&)
{
    LOG_DEBUG << "Block not found " << _block_hash;
}


BlockNotFoundMessage::BlockNotFoundMessage(base::Sha256 block_hash)
  : _block_hash{ std::move(block_hash) }
{}

//============================================

constexpr lk::MessageType GetInfoMessage::getHandledMessageType()
{
    return lk::MessageType::GET_INFO;
}


void GetInfoMessage::serialize(base::SerializationOArchive& oa)
{
    oa.serialize(lk::MessageType::GET_INFO);
}


GetInfoMessage GetInfoMessage::deserialize(base::SerializationIArchive&)
{
    return {};
}


void GetInfoMessage::handle(const lk::Protocol::Context& ctx, lk::Protocol&)
{
    auto& host = *ctx.host;
    ctx.peer->send(prepareMessage<InfoMessage>(ctx.core->getTopBlock(), allPeersInfoExcept(host, ctx.peer->getAddress())));
}

//============================================

constexpr lk::MessageType InfoMessage::getHandledMessageType()
{
    return lk::MessageType::INFO;
}


void InfoMessage::serialize(base::SerializationOArchive& oa,
                            const base::Sha256& top_block_hash,
                            const std::vector<net::Endpoint>& available_peers)
{
    oa.serialize(lk::MessageType::INFO);
    oa.serialize(top_block_hash);
    oa.serialize(available_peers);
}


InfoMessage InfoMessage::deserialize(base::SerializationIArchive& ia)
{
    auto top_block_hash = ia.deserialize<base::Bytes>();
    auto available_peers = ia.deserialize<std::vector<net::Endpoint>>();
    return { std::move(top_block_hash), std::move(available_peers) };
}


void InfoMessage::handle(const lk::Protocol::Context&, lk::Protocol&) {}


InfoMessage::InfoMessage(base::Sha256&& top_block_hash, std::vector<net::Endpoint>&& available_peers)
  : _top_block_hash{ std::move(top_block_hash) }
  , _available_peers{ std::move(available_peers) }
{}

//============================================

constexpr lk::MessageType NewNodeMessage::getHandledMessageType()
{
    return lk::MessageType::INFO;
}


void NewNodeMessage::serialize(base::SerializationOArchive& oa,
                               const net::Endpoint& new_node_endpoint,
                               const lk::Address& address)
{
    oa.serialize(lk::MessageType::NEW_NODE);
    oa.serialize(new_node_endpoint);
    oa.serialize(address);
}


NewNodeMessage NewNodeMessage::deserialize(base::SerializationIArchive& ia)
{
    auto ep = net::Endpoint::deserialize(ia);
    auto address = ia.deserialize<lk::Address>();
    return { std::move(ep), std::move(address) };
}


void NewNodeMessage::handle(const lk::Protocol::Context& ctx, lk::Protocol&)
{
    auto& host = *ctx.host;
    host.checkOutPeer(_new_node_endpoint);
    host.broadcast(prepareMessage<NewNodeMessage>(_new_node_endpoint));
}


NewNodeMessage::NewNodeMessage(net::Endpoint&& new_node_endpoint, lk::Address&& address)
  : _new_node_endpoint{ std::move(new_node_endpoint) }
  , _address{ std::move(address) }
{}

//============================================

constexpr lk::MessageType CloseMessage::getHandledMessageType()
{
    return lk::MessageType::CLOSE;
}


void CloseMessage::serialize(base::SerializationOArchive& oa)
{
    oa.serialize(getHandledMessageType());
}


CloseMessage CloseMessage::deserialize(base::SerializationIArchive&)
{
    return {};
}


void CloseMessage::handle(const lk::Protocol::Context&, lk::Protocol&)
{
}


CloseMessage::CloseMessage()
{}

//============================================

Protocol::State& Protocol::getState() noexcept
{
    return _state;
}


const Protocol::State& Protocol::getState() const noexcept
{
    return _state;
}


Protocol Protocol::peerConnected(Protocol::Context context)
{
    Protocol ret{ std::move(context) };
    ret.startOnConnectedPeer();
    return ret;
}


Protocol Protocol::peerAccepted(Protocol::Context context)
{
    Protocol ret{ std::move(context) };
    ret.startOnAcceptedPeer();
    return ret;
}


Protocol::Protocol(Protocol::Context context)
  : _ctx{ context }
{}


void Protocol::startOnAcceptedPeer()
{
    // TODO: _ctx.pool->schedule(_peer.close); schedule disconnection on timeout
    // now does nothing, since we wait for connected peer to send us something (HANDSHAKE message)
    if(_ctx.peer->tryAddToPool()) {
        _ctx.peer->send(prepareMessage<AcceptedMessage>(_ctx.core->getTopBlock(),
                                                         _ctx.core->getThisNodeAddress(),
                                                         _ctx.peer->getPublicEndpoint().getPort(),
                                                         allPeersInfoExcept(*_ctx.host, _ctx.peer->getAddress())));
    }
    else
    {
        _ctx.peer->send(prepareMessage<CannotAcceptMessage>(CannotAcceptMessage::RefusionReason::BUCKET_IS_FULL,
          _ctx.host->allConnectedPeersInfo()));
        // and close peer properly
    }
}


void Protocol::startOnConnectedPeer()
{
    /*
     * we connected to a node, so now we are waiting for:
     * 1) success response ---> handshake message
     * 2) failure response ---> cannot accept message
     * 3) for timeout
     */
}


void Protocol::onReceive(const base::Bytes& bytes)
{
    MessageProcessor processor{_ctx};
    processor.process(bytes, *this);
}


void Protocol::onClose() {}


void Protocol::sendBlock(const lk::Block& block)
{
    _ctx.peer->send(prepareMessage<BlockMessage>(block));
}


void Protocol::sendTransaction(const lk::Transaction& tx)
{
    _ctx.peer->send(prepareMessage<TransactionMessage>(tx));
}


void Protocol::sendSessionEnd(std::function<void()> on_send)
{
    _ctx.peer->send(prepareMessage<CloseMessage>(), std::move(on_send));
}

} // namespace lk

#pragma GCC diagnostic pop