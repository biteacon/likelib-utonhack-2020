#include "core.hpp"

#include "base/log.hpp"
#include "vm/error.hpp"
#include "vm/tools.hpp"

#include <algorithm>

namespace lk
{

Core::Core(const base::PropertyTree& config, const base::KeyVault& key_vault)
  : _config{ config }
  , _vault{ key_vault }
  , _this_node_address{ _vault.getKey().toPublicKey() }
  , _blockchain{ _config }
  , _host{ _config, 0xFFFF, *this }
  , _vm{ std::move(vm::load()) }
{
    [[maybe_unused]] bool result = _blockchain.tryAddBlock(getGenesisBlock());
    ASSERT(result);
    _state_manager.updateFromGenesis(getGenesisBlock());

    _blockchain.load();
    for (lk::BlockDepth d = 1; d <= _blockchain.getTopBlock().getDepth(); ++d) {
        auto block = *_blockchain.findBlock(*_blockchain.findBlockHashByDepth(d));
        for (const auto& tx : block.getTransactions()) {
            tryPerformTransaction(tx, block);
        }
    }

    subscribeToBlockAddition(
      [this](const base::Sha256& block_hash, const lk::Block& block) { _host.broadcast(block_hash, block); });
    subscribeToNewPendingTransaction([this](const lk::Transaction& tx) { _host.broadcast(tx); });
}


const lk::Block& Core::getGenesisBlock()
{
    static lk::Block genesis = [] {
        auto timestamp = base::Time(1583789617);

        lk::Block ret{ 0, base::Sha256(base::Bytes(32)), timestamp, lk::Address::null(), {} };
        lk::Address from{ lk::Address::null() };
        lk::Address to{ "49cfqVfB1gTGw5XZSu6nZDrntLr1" };
        auto amount = lk::Balance{ "0xffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff" };
        std::uint64_t fee{ 0 };

        ret.addTransaction({ from, to, amount, fee, timestamp, base::Bytes{} });
        return ret;
    }();
    return genesis;
}


void Core::run()
{
    _host.run();
}


TransactionStatus Core::addPendingTransaction(const lk::Transaction& tx)
{
    auto transaction_hash = tx.hashOfTransaction();
    auto transaction_cost = tx.getAmount() + tx.getFee();

    if (!tx.checkSign()) {
        LOG_DEBUG << "Failed signature verification";
        TransactionStatus status{
            TransactionStatus::StatusCode::BadSign, TransactionStatus::ActionType::None, tx.getFee(), ""
        };
        addTransactionOutput(transaction_hash, status);
        return status;
    }

    if (_blockchain.findTransaction(transaction_hash)) {
        auto output_opt = getTransactionOutput(transaction_hash);
        if (output_opt) {
            return *output_opt;
        }
        TransactionStatus status{
            TransactionStatus::StatusCode::Failed, TransactionStatus::ActionType::None, tx.getFee(), ""
        };
        addTransactionOutput(transaction_hash, status);
        return status;
    }

    std::map<lk::Address, lk::Balance> current_pending_balance;
    {
        std::shared_lock lk(_pending_transactions_mutex);
        if (_pending_transactions.find(tx)) {
            TransactionStatus status{
                TransactionStatus::StatusCode::Pending, TransactionStatus::ActionType::None, tx.getFee(), ""
            };
            addTransactionOutput(transaction_hash, status);
            return status;
        }

        current_pending_balance = lk::calcCost(_pending_transactions);
    }

    const auto& pending_from_account_balance = current_pending_balance.find(tx.getFrom());
    if ((pending_from_account_balance != current_pending_balance.end()) && (_state_manager.hasAccount(tx.getFrom()))) {
        auto current_account_balance = _state_manager.getAccount(tx.getFrom()).getBalance();
        if (pending_from_account_balance->second + transaction_cost < current_account_balance) {
            TransactionStatus status{
                TransactionStatus::StatusCode::NotEnoughBalance, TransactionStatus::ActionType::None, 0, ""
            };
            addTransactionOutput(transaction_hash, status);
            return status;
        }
    }

    if (!_state_manager.checkTransaction(tx)) {
        TransactionStatus status{
            TransactionStatus::StatusCode::NotEnoughBalance, TransactionStatus::ActionType::None, 0, ""
        };
        addTransactionOutput(transaction_hash, status);
        return status;
    }

    LOG_DEBUG << "Adding tx to pending:" << transaction_hash;
    {
        std::unique_lock lk(_pending_transactions_mutex);
        _pending_transactions.add(tx);
    }

    _event_new_pending_transaction.notify(tx);
    TransactionStatus status{
        TransactionStatus::StatusCode::Pending, TransactionStatus::ActionType::None, tx.getFee(), ""
    };
    addTransactionOutput(transaction_hash, status);
    return status;
}


std::optional<TransactionStatus> Core::getTransactionOutput(const base::Sha256& tx)
{
    std::shared_lock lk(_tx_outputs_mutex);
    if (auto it = _tx_outputs.find(tx); it != _tx_outputs.end()) {
        return it->second;
    }
    else {
        return TransactionStatus{ TransactionStatus::StatusCode::Failed, TransactionStatus::ActionType::None, 0, "" };
    }
}


void Core::addTransactionOutput(const base::Sha256& tx, const TransactionStatus& status)
{
    std::unique_lock lk(_tx_outputs_mutex);
    if (auto it = _tx_outputs.find(tx); it != _tx_outputs.end()) {
        it->second = status;
    }
    else {
        _tx_outputs.insert({ tx, status });
    }
}


bool Core::tryAddBlock(const lk::Block& b)
{
    std::unique_lock lk{ _blockchain_mutex };

    if (checkBlock(b) && _blockchain.tryAddBlock(b)) {
        {
            std::shared_lock lk(_pending_transactions_mutex);
            _pending_transactions.remove(b.getTransactions());
        }

        LOG_DEBUG << "Applying transactions from block #" << b.getDepth();

        applyBlockTransactions(b);
        lk.unlock();
        auto block_hash = base::Sha256::compute(base::toBytes(b));
        _event_block_added.notify(std::move(block_hash), b);
        return true;
    }
    else {
        return false;
    }
}


std::optional<lk::Block> Core::findBlock(const base::Sha256& hash) const
{
    return _blockchain.findBlock(hash);
}


std::optional<base::Sha256> Core::findBlockHash(const lk::BlockDepth& depth) const
{
    return _blockchain.findBlockHashByDepth(depth);
}


std::optional<lk::Transaction> Core::findTransaction(const base::Sha256& hash) const
{
    return _blockchain.findTransaction(hash);
}


bool Core::checkBlock(const lk::Block& block) const
{
    const auto& top_block = _blockchain.getTopBlock();

    if (top_block.getTimestamp() >= block.getTimestamp()) {
        return false;
    }
    if (block.getTransactions().size() == 0 ||
        block.getTransactions().size() > base::config::BC_MAX_TRANSACTIONS_IN_BLOCK) {
        return false;
    }
    if (_blockchain.findBlock(base::Sha256::compute(base::toBytes(block)))) {
        return false;
    }

    auto block_balance = lk::calcCost(block.getTransactions());
    for (const auto& tx : block.getTransactions()) {
        if (_state_manager.hasAccount(tx.getFrom())) {
            auto current_account_balance = _state_manager.getAccount(tx.getFrom()).getBalance();
            auto block_account_cost = block_balance.find(tx.getFrom());
            ASSERT(block_account_cost != block_balance.end());
            if (block_account_cost->second > current_account_balance) {
                return false;
            }
        }
        else {
            return false;
        }
    }
    return true;
}


std::pair<lk::Block, lk::Complexity> Core::getMiningData() const
{
    std::unique_lock lk{ _blockchain_mutex };

    const auto& p = _blockchain.getTopBlockAndComplexity();
    const auto& top_block = p.first;
    auto& complexity = p.second;

    lk::BlockDepth depth = top_block.getDepth() + 1;
    auto prev_hash = base::Sha256::compute(base::toBytes(top_block));

    TransactionsSet pending;
    {
        std::shared_lock lk(_pending_transactions_mutex);
        pending = _pending_transactions;
    }

    if (pending.size() > base::config::BC_MAX_TRANSACTIONS_IN_BLOCK) {
        pending.selectBestByFee(base::config::BC_MAX_TRANSACTIONS_IN_BLOCK);
    }

    return { lk::Block{ depth, prev_hash, base::Time::now(), getThisNodeAddress(), std::move(pending) },
             std::move(complexity) };
}


lk::AccountInfo Core::getAccountInfo(const lk::Address& address) const
{
    if (_state_manager.hasAccount(address)) {
        auto info = _state_manager.getAccount(address).toInfo();
        info.address = address;
        return info;
    }
    return AccountInfo{ AccountType::CLIENT, address, {}, {}, {} };
}


lk::Block Core::getTopBlock() const
{
    return _blockchain.getTopBlock();
}


base::Sha256 Core::getTopBlockHash() const
{
    return _blockchain.getTopBlockHash();
}


const lk::Address& Core::getThisNodeAddress() const noexcept
{
    return _this_node_address;
}


void Core::applyBlockTransactions(const lk::Block& block)
{
    static constexpr lk::Balance EMISSION_VALUE{ base::config::BC_EMISSION_VALUE };
    _state_manager.getAccount(block.getCoinbase()).addBalance(EMISSION_VALUE);

    for (const auto& tx : block.getTransactions()) {
        tryPerformTransaction(tx, block);
    }
}


void Core::tryPerformTransaction(const lk::Transaction& tx, const lk::Block& block_where_tx)
{
    auto transaction_hash = tx.hashOfTransaction();
    LOG_DEBUG << "Performing transactions with hash " << transaction_hash;
    _state_manager.getAccount(tx.getFrom()).addTransactionHash(transaction_hash);

    auto tx_manager{ _state_manager.createCopy() };

    if (tx.getTo() == lk::Address::null()) {
        try {
            tx_manager.getAccount(tx.getFrom()).subBalance(tx.getFee());

            auto contract_data_hash = base::Sha256::compute(tx.getData());
            lk::Address contract_address = tx_manager.createContractAccount(tx.getFrom(), contract_data_hash);

            if (!tx_manager.tryTransferMoney(tx.getFrom(), contract_address, tx.getAmount())) {
                TransactionStatus status(TransactionStatus::StatusCode::NotEnoughBalance,
                                         TransactionStatus::ActionType::ContractCreation,
                                         tx.getFee(),
                                         {});
                addTransactionOutput(transaction_hash, status);
                return;
            }

            auto eval_result = callInitContractVm(tx_manager, block_where_tx, tx, contract_address, tx.getData());

            if (eval_result.status_code == evmc_status_code::EVMC_SUCCESS) {
                auto runtime_code = vm::copy(eval_result.output_data, eval_result.output_size);
                tx_manager.getAccount(contract_address).setRuntimeCode(runtime_code);
                LOG_DEBUG << "Deployed contract to address "
                          << base::base58Encode(contract_address.getBytes().toBytes());
                TransactionStatus status(TransactionStatus::StatusCode::Success,
                                         TransactionStatus::ActionType::ContractCreation,
                                         eval_result.gas_left,
                                         base::base58Encode(contract_address.getBytes()));

                addTransactionOutput(transaction_hash, status);
                tx_manager.getAccount(block_where_tx.getCoinbase()).addBalance(tx.getFee() - eval_result.gas_left);
                tx_manager.getAccount(tx.getFrom()).addBalance(eval_result.gas_left);

                _state_manager.applyChanges(std::move(tx_manager));

                return;
            }
            else if (eval_result.status_code == evmc_status_code::EVMC_REVERT) {
                TransactionStatus status(TransactionStatus::StatusCode::Revert,
                                         TransactionStatus::ActionType::ContractCreation,
                                         eval_result.gas_left,
                                         "");

                addTransactionOutput(transaction_hash, status);
                _state_manager.getAccount(tx.getFrom()).subBalance(eval_result.gas_left);
                _state_manager.getAccount(block_where_tx.getCoinbase()).addBalance(tx.getFee() - eval_result.gas_left);
                return;
            }
            else {
                TransactionStatus status(TransactionStatus::StatusCode::BadQueryForm,
                                         TransactionStatus::ActionType::ContractCreation,
                                         eval_result.gas_left,
                                         "");
                addTransactionOutput(transaction_hash, status);
                _state_manager.getAccount(tx.getFrom()).subBalance(eval_result.gas_left);
                _state_manager.getAccount(block_where_tx.getCoinbase()).addBalance(tx.getFee() - eval_result.gas_left);
                return;
            }
            ASSERT(false);
        }
        catch (const base::Error&) {
            TransactionStatus status(
              TransactionStatus::StatusCode::Failed, TransactionStatus::ActionType::ContractCreation, tx.getFee(), "");
            addTransactionOutput(transaction_hash, status);
            return;
        }
        ASSERT(false);
    }
    else {
        lk::AccountState to_account_recovery{ _state_manager.getAccount(tx.getTo()) };

        tx_manager.getAccount(tx.getFrom()).subBalance(tx.getFee());

        if (tx_manager.getAccount(tx.getTo()).getType() == AccountType::CONTRACT) {
            try {

                if (tx.getData().isEmpty()) {
                    TransactionStatus status(TransactionStatus::StatusCode::BadQueryForm,
                                             TransactionStatus::ActionType::ContractCall,
                                             tx.getFee(),
                                             "");
                    addTransactionOutput(transaction_hash, status);
                    return;
                }

                if (tx.getAmount() > 0 && !tx_manager.tryTransferMoney(tx.getFrom(), tx.getTo(), tx.getAmount())) {
                    TransactionStatus status(TransactionStatus::StatusCode::NotEnoughBalance,
                                             TransactionStatus::ActionType::ContractCall,
                                             tx.getFee(),
                                             {});
                    addTransactionOutput(transaction_hash, status);
                    return;
                }

                auto code = tx_manager.getAccount(tx.getTo()).getRuntimeCode();
                auto eval_result = callContractVm(tx_manager, block_where_tx, tx, code, tx.getData());


                if (eval_result.status_code == evmc_status_code::EVMC_SUCCESS) {
                    auto output_data = vm::copy(eval_result.output_data, eval_result.output_size);

                    TransactionStatus status(TransactionStatus::StatusCode::Success,
                                             TransactionStatus::ActionType::ContractCall,
                                             eval_result.gas_left,
                                             base::base64Encode(output_data));

                    addTransactionOutput(transaction_hash, status);
                    tx_manager.getAccount(block_where_tx.getCoinbase()).addBalance(tx.getFee() - eval_result.gas_left);
                    tx_manager.getAccount(tx.getFrom()).addBalance(eval_result.gas_left);
                    _state_manager.applyChanges(std::move(tx_manager));
                    return;
                }
                else if (eval_result.status_code == evmc_status_code::EVMC_REVERT) {
                    TransactionStatus status(TransactionStatus::StatusCode::Revert,
                                             TransactionStatus::ActionType::ContractCall,
                                             eval_result.gas_left,
                                             "");

                    addTransactionOutput(transaction_hash, status);
                    _state_manager.getAccount(tx.getFrom()).subBalance(eval_result.gas_left);
                    _state_manager.getAccount(block_where_tx.getCoinbase())
                      .addBalance(tx.getFee() - eval_result.gas_left);
                    return;
                }
                else {
                    TransactionStatus status(TransactionStatus::StatusCode::BadQueryForm,
                                             TransactionStatus::ActionType::ContractCall,
                                             eval_result.gas_left,
                                             "");
                    addTransactionOutput(transaction_hash, status);
                    _state_manager.getAccount(tx.getFrom()).subBalance(eval_result.gas_left);
                    _state_manager.getAccount(block_where_tx.getCoinbase())
                      .addBalance(tx.getFee() - eval_result.gas_left);
                    return;
                }
            }
            catch (const base::Error&) {
                TransactionStatus status(
                  TransactionStatus::StatusCode::Failed, TransactionStatus::ActionType::ContractCall, tx.getFee(), "");
                addTransactionOutput(transaction_hash, status);
                return;
            }
        }
        else {
            try {
                if (!tx_manager.tryTransferMoney(tx.getFrom(), tx.getTo(), tx.getAmount())) {
                    TransactionStatus status(TransactionStatus::StatusCode::NotEnoughBalance,
                                             TransactionStatus::ActionType::Transfer,
                                             tx.getFee(),
                                             {});
                    addTransactionOutput(transaction_hash, status);
                    return;
                }
                TransactionStatus status(
                  TransactionStatus::StatusCode::Success, TransactionStatus::ActionType::Transfer, 0, {});

                addTransactionOutput(transaction_hash, status);
                tx_manager.getAccount(block_where_tx.getCoinbase()).addBalance(tx.getFee());
                _state_manager.applyChanges(std::move(tx_manager));
                return;
            }
            catch (const base::Error&) {
                TransactionStatus status(
                  TransactionStatus::StatusCode::Failed, TransactionStatus::ActionType::Transfer, tx.getFee(), "");
                addTransactionOutput(transaction_hash, status);
                return;
            }
        }
        ASSERT(false);
    }
    ASSERT(false);
}


evmc::result Core::callInitContractVm(StateManager& state_manager,
                                      const lk::Block& associated_block,
                                      const lk::Transaction& tx,
                                      const lk::Address& contract_address,
                                      const base::Bytes& code)
{
    evmc_message message{};
    message.kind = evmc_call_kind::EVMC_CALL;
    message.flags = 0;
    message.depth = 0;
    message.gas = tx.getFee();
    message.sender = vm::toEthAddress(tx.getFrom());
    message.destination = vm::toEthAddress(contract_address);
    message.value = vm::toEvmcUint256(tx.getAmount());
    message.create2_salt = evmc_bytes32();
    return callVm(state_manager, associated_block, tx, message, code);
}


evmc::result Core::callContractVm(StateManager& state_manager,
                                  const lk::Block& associated_block,
                                  const lk::Transaction& tx,
                                  const base::Bytes& code,
                                  const base::Bytes& message_data)
{
    evmc_message message{};
    message.kind = evmc_call_kind::EVMC_CALL;
    message.flags = 0;
    message.depth = 0;
    message.gas = tx.getFee();
    message.sender = vm::toEthAddress(tx.getFrom());
    message.destination = vm::toEthAddress(tx.getTo());
    message.value = vm::toEvmcUint256(tx.getAmount());
    message.input_data = message_data.getData();
    message.input_size = message_data.size();
    return callVm(state_manager, associated_block, tx, message, code);
}


evmc::result Core::callVm(StateManager& state_manager,
                          const lk::Block& associated_block,
                          const lk::Transaction& associated_tx,
                          const evmc_message& message,
                          const base::Bytes& code)
{
    EthHost _eth_host{ *this, state_manager, associated_block, associated_tx };
    return _vm.execute(_eth_host, evmc_revision::EVMC_ISTANBUL, message, code.getData(), code.size());
}


void Core::subscribeToBlockAddition(decltype(Core::_event_block_added)::CallbackType callback)
{
    _event_block_added.subscribe(std::move(callback));
}


void Core::subscribeToNewPendingTransaction(decltype(Core::_event_new_pending_transaction)::CallbackType callback)
{
    _event_new_pending_transaction.subscribe(std::move(callback));
}


EthHost::EthHost(lk::Core& core,
                 lk::StateManager& state_manager,
                 const lk::Block& associated_block,
                 const lk::Transaction& associated_tx)
  : _core{ core }
  , _state_manager{ state_manager }
  , _associated_block{ associated_block }
  , _associated_tx{ associated_tx }
{}


bool EthHost::account_exists(const evmc::address& addr) const noexcept
{
    LOG_DEBUG << "Core::account_exists";
    try {
        auto address = vm::toNativeAddress(addr);
        LOG_DEBUG << "Core::account_exists by address " << base::base58Encode(address.getBytes().toBytes());
        return _state_manager.hasAccount(address);
    }
    catch (...) { // cannot pass exceptions since noexcept
        return false;
    }
}


evmc::bytes32 EthHost::get_storage(const evmc::address& addr, const evmc::bytes32& ethKey) const noexcept
{

    LOG_DEBUG << "Core::get_storage";
    try {
        auto address = vm::toNativeAddress(addr);
        LOG_DEBUG << "Core::get_storage from address " << base::base58Encode(address.getBytes().toBytes());
        base::Bytes key(ethKey.bytes, 32);
        if (_state_manager.hasAccount(address)) {
            auto storage_value = _state_manager.getAccount(address).getStorageValue(base::Sha256(key)).data;
            return vm::toEvmcBytes32(storage_value);
        }
        return {};
    }
    catch (...) { // cannot pass exceptions since noexcept
        return {};
    }
}


evmc_storage_status EthHost::set_storage(const evmc::address& addr,
                                         const evmc::bytes32& ekey,
                                         const evmc::bytes32& evalue) noexcept
{
    LOG_DEBUG << "Core::set_storage";
    try {
        static const base::Bytes NULL_VALUE(32);
        auto address = vm::toNativeAddress(addr);
        LOG_DEBUG << "Core::set_storage to address " << base::base58Encode(address.getBytes().toBytes());
        auto key = base::Sha256(base::Bytes(ekey.bytes, 32));
        base::Bytes new_value(evalue.bytes, 32);

        auto& account_state = _state_manager.getAccount(address);

        if (!account_state.checkStorageValue(key)) {
            if (new_value == NULL_VALUE) {
                return evmc_storage_status::EVMC_STORAGE_UNCHANGED;
            }
            else {
                account_state.setStorageValue(key, new_value);
                return evmc_storage_status::EVMC_STORAGE_ADDED;
            }
        }
        else {
            auto old_storage_data = account_state.getStorageValue(key);
            const auto& old_value = old_storage_data.data;

            account_state.setStorageValue(key, new_value);
            if (old_value == new_value) {
                return evmc_storage_status::EVMC_STORAGE_UNCHANGED;
            }
            else if (new_value == NULL_VALUE) {
                return evmc_storage_status::EVMC_STORAGE_DELETED;
            }
            else {
                return evmc_storage_status::EVMC_STORAGE_MODIFIED;
            }
        }
    }
    catch (...) { // cannot pass exceptions since noexcept
        return {};
    }
}


evmc::uint256be EthHost::get_balance(const evmc::address& addr) const noexcept
{
    LOG_DEBUG << "Core::get_balance";
    try {
        auto address = vm::toNativeAddress(addr);
        LOG_DEBUG << "Core::get_balance to address " << base::base58Encode(address.getBytes().toBytes());
        if (_state_manager.hasAccount(address)) {
            auto balance = _state_manager.getAccount(address).getBalance();
            return vm::toEvmcUint256(balance);
        }
        return {};
    }
    catch (...) { // cannot pass exceptions since noexcept
        return {};
    }
}


size_t EthHost::get_code_size(const evmc::address& addr) const noexcept
{
    LOG_DEBUG << "Core::get_code_size";
    try {
        auto address = vm::toNativeAddress(addr);
        LOG_DEBUG << "Core::get_code_size to address " << base::base58Encode(address.getBytes().toBytes());

        if (_state_manager.hasAccount(address)) {
            auto account = _state_manager.getAccount(address);
            const auto& code = _state_manager.getAccount(address).getRuntimeCode();
            return code.size();
        }
        return 0;
    }
    catch (...) { // cannot pass exceptions since noexcept
        return 0;
    }
}


evmc::bytes32 EthHost::get_code_hash(const evmc::address& addr) const noexcept
{
    LOG_DEBUG << "Core::get_code_hash";
    try {
        auto address = vm::toNativeAddress(addr);
        LOG_DEBUG << "Core::get_code_hash to address " << base::base58Encode(address.getBytes().toBytes());
        auto account_code_hash = _state_manager.getAccount(address).getCodeHash();
        return vm::toEvmcBytes32(account_code_hash.getBytes());
    }
    catch (...) { // cannot pass exceptions since noexcept
        return {};
    }
}


size_t EthHost::copy_code(const evmc::address& addr, size_t code_offset, uint8_t* buffer_data, size_t buffer_size) const
  noexcept
{
    LOG_DEBUG << "Core::copy_code";
    try {
        auto address = vm::toNativeAddress(addr);
        LOG_DEBUG << "Core::copy_code to address " << base::base58Encode(address.getBytes().toBytes());
        if (auto code = _state_manager.getAccount(address).getRuntimeCode(); code.isEmpty()) {
            return 0;
        }
        else {
            std::size_t bytes_to_copy = std::min(buffer_size, code.size() - code_offset);
            std::copy_n(code.getData() + code_offset, bytes_to_copy, buffer_data);
            return bytes_to_copy;
        }
    }
    catch (...) { // cannot pass exceptions since noexcept
        return 0;
    }
}


void EthHost::selfdestruct(const evmc::address& eaddr, const evmc::address& ebeneficiary) noexcept
{
    LOG_DEBUG << "Core::selfdestruct";
    try {
        auto address = vm::toNativeAddress(eaddr);
        LOG_DEBUG << "Core::selfdestruct to address " << base::base58Encode(address.getBytes().toBytes());
        auto account = _state_manager.getAccount(address);

        auto beneficiary_address = vm::toNativeAddress(ebeneficiary);
        auto beneficiary_account = _state_manager.getAccount(beneficiary_address);

        _state_manager.tryTransferMoney(address, beneficiary_address, account.getBalance());
        _state_manager.deleteAccount(address);
    }
    catch (...) { // cannot pass exceptions since noexcept
        return;
    }
}


evmc::result EthHost::call(const evmc_message& msg) noexcept
{
    LOG_DEBUG << "Core::call";
    try {
        lk::Address to = vm::toNativeAddress(msg.destination);
        LOG_DEBUG << "Core::call to address " << base::base58Encode(to.getBytes().toBytes());
        if (_state_manager.hasAccount(to) && _state_manager.getAccount(to).getType() == lk::AccountType::CONTRACT) {
            const auto& code = _state_manager.getAccount(to).getRuntimeCode();
            return _core.callVm(_state_manager, _associated_block, _associated_tx, msg, code);
        }
        else {
            lk::Address from = vm::toNativeAddress(msg.sender);
            _state_manager.tryTransferMoney(from, to, vm::toBalance(msg.value));
            evmc::result result{ evmc_status_code::EVMC_SUCCESS, msg.gas, nullptr, 0 };
            return result;
        }
    }
    catch (...) { // cannot pass exceptions since noexcept
        return evmc::result{ evmc_status_code::EVMC_FAILURE, msg.gas, nullptr, 0 };
    }
}


evmc_tx_context EthHost::get_tx_context() const noexcept
{
    LOG_DEBUG << "Core::get_tx_context";
    try {
        evmc_tx_context ret;
        std::fill(std::begin(ret.tx_gas_price.bytes), std::end(ret.tx_gas_price.bytes), 0);
        ret.tx_origin = vm::toEthAddress(_associated_tx.getFrom());
        ret.block_number = _associated_block.getDepth();
        ret.block_timestamp = _associated_block.getTimestamp().getSeconds();
        ret.block_coinbase = vm::toEthAddress(_associated_block.getCoinbase());
        // ret.block_gas_limit
        std::fill(std::begin(ret.block_difficulty.bytes), std::end(ret.block_difficulty.bytes), 0);
        ret.block_difficulty.bytes[2] = 0x28;

        return ret;
    }
    catch (...) { // cannot pass exceptions since noexcept
        return {};
    }
}


evmc::bytes32 EthHost::get_block_hash(int64_t block_number) const noexcept
{
    LOG_DEBUG << "Core::get_block_hash";
    try {
        auto hash = _core.findBlockHash(block_number);
        if (hash) {
            return vm::toEvmcBytes32(hash->getBytes());
        }
        return {};
    }
    catch (...) { // cannot pass exceptions since noexcept
        return {};
    }
}


void EthHost::emit_log(const evmc::address&, const uint8_t*, size_t, const evmc::bytes32[], size_t) noexcept
{
    LOG_DEBUG << "Core::emit_log";
    LOG_WARNING << "emit_log is denied. For more information, see docs";
}


} // namespace core
