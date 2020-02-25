#pragma once

#include "base/property_tree.hpp"
#include "base/crypto.hpp"
#include "base/utility.hpp"
#include "bc/block.hpp"
#include "bc/blockchain.hpp"
#include "lk/account_manager.hpp"
#include "lk/protocol.hpp"
#include "net/host.hpp"
#include "vm/host.hpp"
#include "vm/vm.hpp"

#include <shared_mutex>
#include <vm/host.hpp>

namespace lk
{

class Core
{
  public:
    //==================
    Core(const base::PropertyTree& config, const base::KeyVault& vault);

    /**
     *  @brief Stops network, does cleaning and flushing.
     *
     *  @threadsafe
     */
    ~Core() = default;
    //==================
    /**
     *  @brief Loads blockchain from disk and runs networking.
     *
     *  @async
     *  @threadsafe
     */
    void run();
    //==================
    bc::Balance getBalance(const bc::Address& address) const;
    //==================
    bool performTransaction(const bc::Transaction& tx);
    //==================
    bool tryAddBlock(const bc::Block& b);
    std::optional<bc::Block> findBlock(const base::Sha256& hash) const;
    const bc::Block& getTopBlock() const;
    //==================
    bc::Block getBlockTemplate() const;
    //==================
    base::Bytes getThisNodeAddress() const;
    //==================
  private:
    //==================
    const base::PropertyTree& _config;
    const base::KeyVault& _vault;
    //==================
    base::Observable<const bc::Block&> _event_block_added;
    base::Observable<const bc::Transaction&> _event_new_pending_transaction;
    //==================
    bool _is_account_manager_updated{false};
    AccountManager _account_manager;
    bc::Blockchain _blockchain;
    lk::Network _network;
    //==================
    bc::TransactionsSet _pending_transactions;
    mutable std::shared_mutex _pending_transactions_mutex;
    //==================
    vm::HostImplementation _host_impl;
    vm::VM _vm;
    //==================
    static const bc::Block& getGenesisBlock();
    void updateNewBlock(const bc::Block& block);
    //==================
    bool checkBlock(const bc::Block& block) const;
    bool checkTransaction(const bc::Transaction& tx) const;
    //==================
  public:
    //==================
    // notifies if new blocks are added: genesis and blocks, that are stored in DB, are not handled by this
    void subscribeToBlockAddition(decltype(_event_block_added)::CallbackType callback);

    // notifies if some transaction was added to set of pending
    void subscribeToNewPendingTransaction(decltype(_event_new_pending_transaction)::CallbackType callback);
    //==================
};

} // namespace lk
