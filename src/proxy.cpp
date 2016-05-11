/**
 * Copyright (c) 2011-2015 libbitcoin developers (see AUTHORS)
 *
 * This file is part of libbitcoin-client.
 *
 * libbitcoin-client is free software: you can redistribute it and/or
 * modify it under the terms of the GNU Affero General Public License with
 * additional permissions to the one published by the Free Software
 * Foundation, either version 3 of the License, or (at your option)
 * any later version. For more information see LICENSE.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */
#include <bitcoin/client/proxy.hpp>

#include <chrono>
#include <cstdint>
#include <memory>
#include <bitcoin/bitcoin.hpp>
#include <bitcoin/client/dealer.hpp>
#include <bitcoin/client/define.hpp>
#include <bitcoin/client/stream.hpp>

namespace libbitcoin {
namespace client {

using std::placeholders::_1;
using namespace bc::chain;
using namespace std::chrono;
using namespace bc::wallet;

/// Due to an unfortunate historical accident, the obelisk wire format encodes
/// address hashes in reverse order.
template <typename Collection>
Collection reverse(const Collection& in)
{
    Collection out;
    std::reverse_copy(in.begin(), in.end(), out.begin());
    return out;
}

proxy::proxy(stream& out,
    unknown_handler on_unknown_command, uint32_t timeout_ms, uint8_t resends)
  : dealer(out, on_unknown_command, timeout_ms, resends)
{
}

// Fetchers.
//-----------------------------------------------------------------------------

void proxy::protocol_broadcast_transaction(error_handler on_error,
    empty_handler on_reply, const chain::transaction& tx)
{
    send_request("protocol.broadcast_transaction", tx.to_data(),
        std::move(on_error),
        std::bind(decode_empty,
            _1, std::move(on_reply)));
}

void proxy::transaction_pool_validate(error_handler on_error,
    validate_handler on_reply, const chain::transaction& tx)
{
    send_request("transaction_pool.validate", tx.to_data(),
        std::move(on_error),
        std::bind(decode_validate,
            _1, std::move(on_reply)));
}

void proxy::transaction_pool_fetch_transaction(error_handler on_error,
    transaction_handler on_reply, const hash_digest& tx_hash)
{
    const auto data = build_chunk({ tx_hash });

    send_request("transaction_pool.fetch_transaction", data,
        std::move(on_error),
        std::bind(decode_transaction,
            _1, std::move(on_reply)));
}

void proxy::blockchain_fetch_transaction(error_handler on_error,
    transaction_handler on_reply, const hash_digest& tx_hash)
{
    const auto data = build_chunk({ tx_hash });

    send_request("blockchain.fetch_transaction", data, std::move(on_error),
        std::bind(decode_transaction,
            _1, std::move(on_reply)));
}

void proxy::blockchain_fetch_last_height(error_handler on_error,
    height_handler on_reply)
{
    const data_chunk data;

    send_request("blockchain.fetch_last_height", data, std::move(on_error),
        std::bind(decode_height,
            _1, std::move(on_reply)));
}

void proxy::blockchain_fetch_block_header(error_handler on_error,
    block_header_handler on_reply, uint32_t height)
{
    const auto data = build_chunk({ to_little_endian<uint32_t>(height) });

    send_request("blockchain.fetch_block_header", data, std::move(on_error),
        std::bind(decode_block_header,
            _1, std::move(on_reply)));
}

void proxy::blockchain_fetch_block_header(error_handler on_error,
    block_header_handler on_reply, const hash_digest& block_hash)
{
    const auto data = build_chunk({ block_hash });

    send_request("blockchain.fetch_block_header", data, std::move(on_error),
        std::bind(decode_block_header,
            _1, std::move(on_reply)));
}

void proxy::blockchain_fetch_transaction_index(error_handler on_error,
    transaction_index_handler on_reply, const hash_digest& tx_hash)
{
    const auto data = build_chunk({ tx_hash });

    send_request("blockchain.fetch_transaction_index", data,
        std::move(on_error),
        std::bind(decode_transaction_index,
            _1, std::move(on_reply)));
}

void proxy::blockchain_fetch_stealth(error_handler on_error,
    stealth_handler on_reply, const binary& prefix, uint32_t from_height)
{
    if (prefix.size() > max_uint8)
    {
        on_error(error::bad_stream);
        return;
    }

    const auto data = build_chunk(
    {
        to_array(static_cast<uint8_t>(prefix.size())),
        prefix.blocks(),
        to_little_endian<uint32_t>(from_height)
    });

    send_request("blockchain.fetch_stealth", data, std::move(on_error),
        std::bind(decode_stealth,
            _1, std::move(on_reply)));
}

void proxy::blockchain_fetch_history(error_handler on_error,
    history_handler on_reply, const payment_address& address,
    uint32_t from_height)
{
    // This reversal on the wire is an idiosyncracy of the Obelisk protocol.
    // We un-reverse here to limit confusion downsteam.

    const auto data = build_chunk(
    {
        to_array(address.version()),
        reverse(address.hash()),
        to_little_endian<uint32_t>(from_height)
    });

    send_request("blockchain.fetch_history", data, std::move(on_error),
        std::bind(decode_history,
            _1, std::move(on_reply)));
}

// address.fetch_history is obsoleted in bs 3.0 (use address.fetch_history2).
void proxy::address_fetch_history(error_handler on_error,
    history_handler on_reply, const payment_address& address,
    uint32_t from_height)
{
    // This reversal on the wire is an idiosyncracy of the Obelisk protocol.
    // We un-reverse here to limit confusion downsteam.

    const auto data = build_chunk(
    {
        to_array(address.version()),
        reverse(address.hash()),
        to_little_endian<uint32_t>(from_height)
    });

    // address.fetch_history is first available in sx and deprecated in bs 2.0.
    send_request("address.fetch_history", data, std::move(on_error),
        std::bind(decode_expanded_history,
            _1, std::move(on_reply)));
}

// The difference between fetch_history and fetch_history2 is hash reversal.
void proxy::address_fetch_history2(error_handler on_error,
    history_handler on_reply, const payment_address& address,
    uint32_t from_height)
{
    const auto data = build_chunk(
    {
        to_array(address.version()),
        address.hash(),
        to_little_endian<uint32_t>(from_height)
    });

    // address.fetch_history2 is first available in bs 3.0.
    send_request("address.fetch_history2", data, std::move(on_error),
        std::bind(decode_history,
            _1, std::move(on_reply)));
}

// Subscribers.
//-----------------------------------------------------------------------------

// Ths is a simplified overload for a non-private payment address subscription.
void proxy::address_subscribe(error_handler on_error, empty_handler on_reply,
    const payment_address& address)
{
    binary prefix(short_hash_size * byte_bits, address.hash());

    // [ type:1 ] (0 = address prefix, 1 = stealth prefix)
    // [ prefix_bitsize:1 ]
    // [ prefix_blocks:...  ]
    const auto data = build_chunk(
    {
        to_array(static_cast<uint8_t>(subscribe_type::address)),
        to_array(static_cast<uint8_t>(prefix.size())),
        prefix.blocks()
    });

    send_request("address.subscribe", data, std::move(on_error),
        std::bind(decode_empty,
            _1, std::move(on_reply)));
}

// Ths overload supports only a prefix for either stealth or payment address.
void proxy::address_subscribe(error_handler on_error, empty_handler on_reply,
    subscribe_type discriminator, const binary& prefix)
{
    if (prefix.size() > max_uint8)
    {
        on_error(error::bad_stream);
        return;
    }

    const auto data = build_chunk(
    {
        to_array(static_cast<uint8_t>(discriminator)),
        to_array(static_cast<uint8_t>(prefix.size())),
        prefix.blocks()
    });

    send_request("address.subscribe", data, std::move(on_error),
        std::bind(decode_empty,
            _1, std::move(on_reply)));
}

// Response handlers.
//-----------------------------------------------------------------------------

bool proxy::decode_empty(reader& payload, empty_handler& handler)
{
    if (!payload.is_exhausted())
        return false;

    handler();
    return true;
}

bool proxy::decode_transaction(reader& payload, transaction_handler& handler)
{
    transaction tx;
    if (!tx.from_data(payload) || !payload.is_exhausted())
        return false;

    handler(tx);
    return true;
}

bool proxy::decode_height(reader& payload, height_handler& handler)
{
    const auto last_height = payload.read_4_bytes_little_endian();
    if (!payload.is_exhausted())
        return false;

    handler(last_height);
    return true;
}

bool proxy::decode_block_header(reader& payload, block_header_handler& handler)
{
    chain::header header;
    if (!header.from_data(payload, false) || !payload.is_exhausted())
        return false;

    handler(header);
    return true;
}

bool proxy::decode_transaction_index(reader& payload,
    transaction_index_handler& handler)
{
    const auto block_height = payload.read_4_bytes_little_endian();
    const auto index = payload.read_4_bytes_little_endian();
    if (!payload.is_exhausted())
        return false;

    handler(block_height, index);
    return true;
}

bool proxy::decode_validate(reader& payload, validate_handler& handler)
{
    point::indexes unconfirmed;

    while (!payload.is_exhausted())
    {
        unconfirmed.push_back(payload.read_4_bytes_little_endian());
        if (!payload)
            return false;
    }

    handler(unconfirmed);
    return true;
}

stealth::list proxy::expand(const stealth_compact::list& compact)
{
    // The sign byte of the ephmemeral key is fixed (0x02) by convention.
    static const auto sign = to_array(ephemeral_public_key_sign);

    stealth::list result;

    for (const auto& in: compact)
    {
        stealth out;
        out.ephemeral_public_key = splice(sign, in.ephemeral_public_key_hash);
        out.public_key_hash = reverse(in.public_key_hash);
        out.transaction_hash = in.transaction_hash;
        result.emplace_back(out);
    }

    return result;
}

// Address hash is reversed here and unreversed in expansion.
bool proxy::decode_stealth(reader& payload, stealth_handler& handler)
{
    chain::stealth_compact::list compact;

    while (!payload.is_exhausted())
    {
        chain::stealth_compact row;
        row.ephemeral_public_key_hash = payload.read_hash();
        row.public_key_hash = payload.read_short_hash();
        row.transaction_hash = payload.read_hash();
        compact.emplace_back(row);

        if (!payload)
            return false;
    }

    handler(expand(compact));
    return true;
}

history::list proxy::expand(const history_compact::list& compact)
{
    // Temporarily store checksum in the spend height of unspent outputs.
    static_assert(sizeof(history::spend_height) == sizeof(uint64_t),
        "spend_height must be large enough to hold a spend_checksum");

    history::list result;

    for (const auto& output: compact)
    {
        if (output.kind == point_kind::output)
        {
            history row;
            row.output = output.point;
            row.output_height = output.height;
            row.value = output.value;
            row.spend = { null_hash, max_uint32 };
            row.spend_height = output.point.checksum();
            result.emplace_back(row);
        }
    }

    for (const auto& spend: compact)
    {
        if (spend.kind == point_kind::spend)
        {
            for (auto& row: result)
            {
                if (row.spend_height == spend.previous_checksum &&
                    row.spend.hash == null_hash)
                {
                    row.spend = spend.point;
                    row.spend_height = spend.height;
                    BITCOIN_ASSERT(row.value == spend.value);
                    break;
                }
            }
        }
    }

    // Clear all remaining checksums from unspent rows.
    for (auto& row: result)
        if (row.spend.hash == null_hash)
            row.spend_height = max_uint32;

    return result;
}

// row.value || row.previous_checksum is a union, just read row.value.
bool proxy::decode_history(reader& payload, history_handler& handler)
{
    chain::history_compact::list compact;

    while (!payload.is_exhausted())
    {
        chain::history_compact row;
        row.kind = static_cast<point_kind>(payload.read_byte());
        const auto success = row.point.from_data(payload);
        row.height = payload.read_4_bytes_little_endian();
        row.value = payload.read_8_bytes_little_endian();
        compact.push_back(row);

        if (!success || !payload)
            return false;
    }

    handler(expand(compact));
    return true;
}

// This supports address.fetch_history (which is obsolete as of server v3).
bool proxy::decode_expanded_history(reader& payload, history_handler& handler)
{
    chain::history::list expanded;

    while (!payload.is_exhausted())
    {
        chain::history row;
        auto success = row.output.from_data(payload);
        row.output_height = payload.read_8_bytes_little_endian();
        row.value = payload.read_8_bytes_little_endian();

        // If there is no spend then input is null_hash/max_uint32/max_uint32.
        success &= row.spend.from_data(payload);
        row.spend_height = payload.read_8_bytes_little_endian();
        expanded.push_back(row);

        if (!success || !payload)
            return false;
    }

    handler(expanded);
    return true;
}

////void proxy::subscribe(error_handler on_error,
////    empty_handler on_reply, const address_prefix& prefix)
////{
////    // BUGBUG: assertion is not good enough here.
////    BITCOIN_ASSERT(prefix.size() <= 255);
////
////    // [ type:1 ] (0 = address prefix, 1 = stealth prefix)
////    // [ prefix_bitsize:1 ]
////    // [ prefix_blocks:...  ]
////    auto data = build_chunk({
////        to_array(static_cast<uint8_t>(subscribe_type::address)),
////        to_array(prefix.size()),
////        prefix.blocks()
////    });
////
////    send_request("address.subscribe", data, std::move(on_error),
////        std::bind(decode_empty, _1, std::move(on_reply)));
////}

/**
    See also libbitcoin-server repo subscribe_manager::post_updates() and
    subscribe_manager::post_stealth_updates().

The address result is:

    Response command = "address.update"

    [ version:1 ]
    [ hash:20 ]
    [ height:4 ]
    [ block_hash:32 ]

    struct address_subscribe_result
    {
        payment_address address;
        uint32_t height;
        hash_digest block_hash;
    };

When the subscription type is stealth, then the result is:

    Response command = "address.stealth_update"

    [ 32 bit prefix:4 ]
    [ height:4 ]
    [ block_hash:32 ]
    
    // Currently not used.
    struct stealth_subscribe_result
    {
        typedef byte_array<4> stealth_prefix_bytes;
        // Protocol will send back 4 bytes of prefix.
        // See libbitcoin-server repo subscribe_manager::post_stealth_updates()
        stealth_prefix_bytes prefix;
        uint32_t height;
        hash_digest block_hash;
    };

    Subscriptions expire after 10 minutes. Therefore messages with the command
    "address.renew" should be sent periodically to the server. The format
    is the same as for "address.subscribe, and the server will respond
    with a 4 byte error code.
*/

} // namespace client
} // namespace libbitcoin