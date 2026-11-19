/* XMRig
 * Copyright (C) 2026      xmrig-pcoin fork
 *
 *   This program is free software: you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation, either version 3 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *   GNU General Public License for more details.
 */

#ifndef XMRIG_PCOINTEMPLATE_H
#define XMRIG_PCOINTEMPLATE_H


#include <cstdint>
#include <string>
#include <vector>


#include "3rdparty/rapidjson/fwd.h"





namespace xmrig {


/*
 * Assembles a PCoin (Bitcoin Core fork) block template returned by
 * getblocktemplate into a minable 80-byte RandomX PoW header plus everything
 * needed to serialize the final block for submitblock.
 *
 * Unlike Monero's cryptoNote daemon, PCoin's RPC returns no hashing blob and
 * no coinbase -- the miner is expected to:
 *   1. build the coinbase transaction (BIP34 height script, payout output,
 *      witness commitment OP_RETURN output when default_witness_commitment is
 *      present, segwit witness with the 32-byte zero reserved value that the
 *      daemon used to compute the commitment),
 *   2. build the merkle root over [coinbase txid, GBT transaction txids],
 *   3. hash the canonical 80-byte header (version, reversed display prevhash,
 *      merkle root, curtime, bits) with the fixed key "PCoin/RandomX/v1".
 *
 * All consensus-affecting details here were taken from the PCoin source
 * (src/validation.cpp ChainstateManager::GenerateCoinbaseCommitment,
 * src/consensus/merkle.cpp BlockWitnessMerkleRoot, src/crypto/pow_randomx.cpp).
 */
class PcoinTemplate
{
public:
    PcoinTemplate() = default;

    // Parse a getblocktemplate result and assemble the coinbase + header.
    // payoutAddress: the miner's bech32 payout address ("pc1..." for mainnet).
    // Returns false and fills `error` on malformed input.
    bool parse(const rapidjson::Value &result, const std::string &payoutAddress, std::string &error);

    const uint8_t *blob() const              { return m_header; }
    size_t blobSize() const                  { return sizeof(m_header); }
    uint64_t height() const                  { return m_height; }
    const std::string &target() const        { return m_target; }
    const uint8_t *merkleRoot() const        { return m_header + 36; }
    const uint8_t *previousHash() const      { return m_header + 4; }
    int64_t coinbaseValue() const            { return m_coinbaseValue; }

    // Time-independent template identity (version, prevhash, bits, height,
    // coinbasevalue, full coinbase stripped serialization and every tx).
    // GBT's curtime/mintime change on every poll, so blobs never match --
    // this key exists so the daemon client can skip re-announcing templates
    // that differ only in time.
    const std::string &identity() const              { return m_identity; }

    // Serialize the complete block for submitblock; `nonce` is written into
    // the header at offset 76. Returns the block as a hex string.
    bool buildBlock(uint32_t nonce, std::string &hexOut) const;

    // Full 256-bit target check with the same little-endian semantics the
    // node uses (UintToArith256). CpuWorker only compares the top 64 bits of
    // the hash, so this is the authoritative check before submitblock.
    bool hashMeetsTarget(const uint8_t *hash32) const;
    bool isValid() const                     { return m_valid; }

private:
    bool decodeAddress(const std::string &address, std::vector<uint8_t> &scriptPubKey, std::string &error) const;
    bool buildCoinbase(const rapidjson::Value &result, const std::string &payoutAddress, std::string &error);
    bool computeMerkle();

    uint8_t m_header[80]{};
    uint64_t m_height = 0;
    std::string m_target;                 // 32-byte big-endian target as hex (for Job::setTarget)
    int64_t m_coinbaseValue = 0;
    std::vector<uint8_t> m_coinbaseStripped;   // serialization used for txid
    std::vector<uint8_t> m_coinbaseFull;       // serialization with witness marker, for the block
    std::vector<std::vector<uint8_t>> m_txs;   // raw (witness-serialized) transactions
    std::string m_identity;            // time-independent template key (see identity())
    bool m_valid = false;
};


} // namespace xmrig


#endif /* XMRIG_PCOINTEMPLATE_H */
