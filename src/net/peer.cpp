#include "peer.hpp"

#include "base/log.hpp"

namespace net
{

Peer::Peer(std::unique_ptr<Connection> connection) : _id{Peer::getNextId()}, _connection{std::move(connection)}
{}


std::size_t Peer::getNextId() const
{
    static std::atomic<std::size_t> next_id{0};
    return next_id++;
}


std::size_t Peer::getId() const noexcept
{
    return _id;
}


bool Peer::isActive() const noexcept
{
    return !(isClosed());
}


bool Peer::isClosed() const noexcept
{
    return !_connection || _connection->isClosed();
}


void Peer::refreshLastSeen()
{
    _last_seen = base::Time::now();
}


base::Time Peer::getLastSeen() const noexcept
{
    return _last_seen;
}


void Peer::receive(Peer::ReceiveHandler handler)
{
    // TODO: add shared_from_this capturing in lambda
    _connection->receive(2, [this, handler = std::move(handler)](const base::Bytes& data) {
        refreshLastSeen();
        auto length = base::fromBytes<std::uint16_t>(data);
        LOG_DEBUG << "Received length = " << length;
        _connection->receive(length, [this, handler = std::move(handler)](const base::Bytes& data) {
            refreshLastSeen();
            handler(data);
        });
    });
}


void Peer::send(base::Bytes data)
{
    base::Bytes b;
    b.append(base::toBytes(static_cast<std::uint16_t >(data.size())));
    b.append(data);
    _connection->send(std::move(b));
}


void Peer::close()
{
    ASSERT(!_connection->isClosed());
    if(!_connection->isClosed()) {
        _connection->close();
    }
}


bool operator<(const Peer& a, const Peer& b)
{
    return a.getId() < b.getId();
}


bool operator>(const Peer& a, const Peer& b)
{
    return a.getId() > b.getId();
}


bool operator<=(const Peer& a, const Peer& b)
{
    return a.getId() <= b.getId();
}


bool operator>=(const Peer& a, const Peer& b)
{
    return a.getId() >= b.getId();
}


bool operator==(const Peer& a, const Peer& b)
{
    return a.getId() == b.getId();
}


bool operator!=(const Peer& a, const Peer& b)
{
    return a.getId() != b.getId();
}


void Peers::add(std::shared_ptr<Peer> peer)
{
    ASSERT(peer);
    std::lock_guard lk(_state_mutex);
    _peers.push_front(std::move(peer));
    ++_size;
}


std::size_t Peers::size() const noexcept
{
    std::shared_lock lk(_state_mutex);
    return _size;
}


void Peers::forEach(std::function<void(Peer&)> f)
{
    std::lock_guard lk(_state_mutex);
    for(auto& peer: _peers) {
        f(*peer);
    }
}


void Peers::removeClosed()
{
    std::lock_guard lk(_state_mutex);
    _peers.remove_if([](const auto& peer) {
        return peer->isClosed();
    });
}

} // namespace net