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

#include <cstring>
#include <cstdio>
#include <algorithm>
#include <sstream>

#include "base/net/stratum/PcoinTemplate.h"
#include "3rdparty/rapidjson/document.h"
#include "base/io/json/Json.h"


namespace {

// ---------------- SHA-256 (FIPS 180-4), self-contained ----------------

struct Sha256 {
    uint32_t h[8];
    uint64_t total;
    uint8_t buf[64];
    size_t bufLen;
};

static const uint32_t kK[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
};

static inline uint32_t rotr32(uint32_t x, uint32_t n) { return (x >> n) | (x << (32 - n)); }

static void sha256Init(Sha256 &ctx)
{
    static const uint32_t h0[8] = {
        0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19
    };
    std::memcpy(ctx.h, h0, sizeof(h0));
    ctx.total = 0;
    ctx.bufLen = 0;
}

static void sha256Block(Sha256 &ctx, const uint8_t *p)
{
    uint32_t w[64];
    for (int i = 0; i < 16; ++i) {
        w[i] = (static_cast<uint32_t>(p[4 * i]) << 24) | (static_cast<uint32_t>(p[4 * i + 1]) << 16)
             | (static_cast<uint32_t>(p[4 * i + 2]) << 8) | static_cast<uint32_t>(p[4 * i + 3]);
    }
    for (int i = 16; i < 64; ++i) {
        const uint32_t s0 = rotr32(w[i - 15], 7) ^ rotr32(w[i - 15], 18) ^ (w[i - 15] >> 3);
        const uint32_t s1 = rotr32(w[i - 2], 17) ^ rotr32(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }

    uint32_t a = ctx.h[0], b = ctx.h[1], c = ctx.h[2], d = ctx.h[3];
    uint32_t e = ctx.h[4], f = ctx.h[5], g = ctx.h[6], h = ctx.h[7];

    for (int i = 0; i < 64; ++i) {
        const uint32_t S1 = rotr32(e, 6) ^ rotr32(e, 11) ^ rotr32(e, 25);
        const uint32_t ch = (e & f) ^ (~e & g);
        const uint32_t t1 = h + S1 + ch + kK[i] + w[i];
        const uint32_t S0 = rotr32(a, 2) ^ rotr32(a, 13) ^ rotr32(a, 22);
        const uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        const uint32_t t2 = S0 + maj;

        h = g; g = f; f = e; e = d + t1;
        d = c; c = b; b = a; a = t1 + t2;
    }

    ctx.h[0] += a; ctx.h[1] += b; ctx.h[2] += c; ctx.h[3] += d;
    ctx.h[4] += e; ctx.h[5] += f; ctx.h[6] += g; ctx.h[7] += h;
}

static void sha256Update(Sha256 &ctx, const uint8_t *data, size_t size)
{
    ctx.total += size;
    while (size > 0) {
        const size_t take = std::min<size_t>(size, sizeof(ctx.buf) - ctx.bufLen);
        std::memcpy(ctx.buf + ctx.bufLen, data, take);
        ctx.bufLen += take;
        data += take;
        size -= take;
        if (ctx.bufLen == sizeof(ctx.buf)) {
            sha256Block(ctx, ctx.buf);
            ctx.bufLen = 0;
        }
    }
}

static void sha256Final(Sha256 &ctx, uint8_t out[32])
{
    const uint64_t bitLen = ctx.total * 8;
    uint8_t pad = 0x80;
    sha256Update(ctx, &pad, 1);
    pad = 0;
    while (ctx.bufLen != 56) {
        sha256Update(ctx, &pad, 1);
    }
    uint8_t lenBytes[8];
    for (int i = 0; i < 8; ++i) {
        lenBytes[7 - i] = static_cast<uint8_t>(bitLen >> (8 * i));
    }
    // pad manually so ctx.total is not disturbed
    std::memcpy(ctx.buf + ctx.bufLen, lenBytes, 8);
    sha256Block(ctx, ctx.buf);
    for (int i = 0; i < 8; ++i) {
        out[4 * i] = static_cast<uint8_t>(ctx.h[i] >> 24);
        out[4 * i + 1] = static_cast<uint8_t>(ctx.h[i] >> 16);
        out[4 * i + 2] = static_cast<uint8_t>(ctx.h[i] >> 8);
        out[4 * i + 3] = static_cast<uint8_t>(ctx.h[i]);
    }
}

static void sha256(const uint8_t *data, size_t size, uint8_t out[32])
{
    Sha256 ctx{};
    sha256Init(ctx);
    sha256Update(ctx, data, size);
    sha256Final(ctx, out);
}

static void sha256d(const uint8_t *data, size_t size, uint8_t out[32])
{
    uint8_t first[32];
    sha256(data, size, first);
    sha256(first, sizeof(first), out);
}

// ---------------- hex / compactsize / uint32 helpers ----------------

static int hexVal(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static bool hexToBytes(const char *hex, std::vector<uint8_t> &out)
{
    if (!hex) {
        return false;
    }
    const size_t n = std::strlen(hex);
    if (n % 2 != 0) {
        return false;
    }
    out.resize(n / 2);
    for (size_t i = 0; i < out.size(); ++i) {
        const int hi = hexVal(hex[2 * i]);
        const int lo = hexVal(hex[2 * i + 1]);
        if (hi < 0 || lo < 0) {
            return false;
        }
        out[i] = static_cast<uint8_t>((hi << 4) | lo);
    }
    return true;
}

static std::string bytesToHex(const uint8_t *data, size_t size)
{
    static const char kHex[] = "0123456789abcdef";
    std::string out;
    out.resize(size * 2);
    for (size_t i = 0; i < size; ++i) {
        out[2 * i] = kHex[data[i] >> 4];
        out[2 * i + 1] = kHex[data[i] & 0x0f];
    }
    return out;
}

static void writeCompactSize(std::vector<uint8_t> &out, uint64_t n)
{
    if (n < 0xfd) {
        out.push_back(static_cast<uint8_t>(n));
    } else if (n <= 0xffff) {
        out.push_back(0xfd);
        out.push_back(static_cast<uint8_t>(n & 0xff));
        out.push_back(static_cast<uint8_t>((n >> 8) & 0xff));
    } else if (n <= 0xffffffffULL) {
        out.push_back(0xfe);
        for (int i = 0; i < 4; ++i) {
            out.push_back(static_cast<uint8_t>((n >> (8 * i)) & 0xff));
        }
    } else {
        out.push_back(0xff);
        for (int i = 0; i < 8; ++i) {
            out.push_back(static_cast<uint8_t>((n >> (8 * i)) & 0xff));
        }
    }
}

static void writeLE32(uint8_t *dst, int64_t offset, uint32_t v)
{
    for (int i = 0; i < 4; ++i) {
        dst[offset + i] = static_cast<uint8_t>((v >> (8 * i)) & 0xff);
    }
}

static std::vector<uint8_t> scriptNumber(uint64_t n)
{
    std::vector<uint8_t> v;
    while (n > 0) {
        v.push_back(static_cast<uint8_t>(n & 0xff));
        n >>= 8;
    }
    if (v.empty()) {
        v.push_back(0);
    }
    if (v.back() & 0x80) {
        v.push_back(0x00);
    }
    return v;
}

// Double-SHA256 merkle tree, Bitcoin rules: odd leaf count duplicates the last.
static std::vector<uint8_t> computeMerkleRoot(std::vector<std::vector<uint8_t>> &leaves)
{
    while (leaves.size() > 1) {
        if (leaves.size() % 2 != 0) {
            leaves.push_back(leaves.back());
        }
        std::vector<std::vector<uint8_t>> next;
        next.reserve(leaves.size() / 2);
        for (size_t i = 0; i < leaves.size(); i += 2) {
            uint8_t both[64];
            std::memcpy(both, leaves[i].data(), 32);
            std::memcpy(both + 32, leaves[i + 1].data(), 32);
            uint8_t parent[32];
            sha256d(both, sizeof(both), parent);
            next.emplace_back(parent, parent + 32);
        }
        leaves.swap(next);
    }
    return leaves.at(0);
}

} // namespace

namespace {

// ---------------- bech32 / bech32m decode (HRP "pc") ----------------

static const char *kBech32Charset = "qpzry9x8gf2tvdw0s3jn54khce6mua7l";
static const uint32_t kBech32Const   = 1;          // bech32 (v0)
static const uint32_t kBech32mConst  = 0x2bc830a3; // bech32m (v1+)

static int bech32CharIndex(char c)
{
    if (c >= 'A' && c <= 'Z') {
        c = static_cast<char>(c - 'A' + 'a');
    }
    const char *p = std::strchr(kBech32Charset, c);
    return p ? static_cast<int>(p - kBech32Charset) : -1;
}

static uint32_t bech32Polymod(const std::vector<uint8_t> &values)
{
    static const uint32_t gen[5] = { 0x3b6a57b2, 0x26508e6d, 0x1ea119fa, 0x3d4233dd, 0x2a1462b3 };
    uint32_t chk = 1;
    for (const uint8_t v : values) {
        const uint32_t top = chk >> 25;
        chk = (chk & 0x1ffffff) << 5 ^ v;
        for (int i = 0; i < 5; ++i) {
            if ((top >> i) & 1) {
                chk ^= gen[i];
            }
        }
    }
    return chk;
}

static std::vector<uint8_t> bech32HrpExpand(const std::string &hrp)
{
    std::vector<uint8_t> v;
    v.reserve(hrp.size() * 2 + 1);
    for (const char c : hrp) {
        v.push_back(static_cast<uint8_t>(static_cast<unsigned char>(c) >> 5));
    }
    v.push_back(0);
    for (const char c : hrp) {
        v.push_back(static_cast<uint8_t>(static_cast<unsigned char>(c) & 31));
    }
    return v;
}

static bool convertBits(const std::vector<uint8_t> &in, size_t fromBits, size_t toBits, bool pad, std::vector<uint8_t> &out)
{
    uint32_t acc = 0;
    int bits = 0;
    const uint32_t maxv = (1u << toBits) - 1;
    for (const uint8_t value : in) {
        if ((value >> fromBits) != 0) {
            return false;
        }
        acc = (acc << fromBits) | value;
        bits += static_cast<int>(fromBits);
        while (bits >= static_cast<int>(toBits)) {
            bits -= static_cast<int>(toBits);
            out.push_back(static_cast<uint8_t>((acc >> bits) & maxv));
        }
    }
    if (pad) {
        if (bits > 0) {
            out.push_back(static_cast<uint8_t>((acc << (toBits - bits)) & maxv));
        }
    } else if (bits >= static_cast<int>(fromBits) || ((acc << (toBits - bits)) & maxv) != 0) {
        return false;
    }
    return true;
}

// Returns false on any decode error. `scriptPubKeyOut` receives the witness program script.
static bool decodeBech32Address(const std::string &address, std::vector<uint8_t> &scriptPubKeyOut, std::string &error)
{
    if (address.size() < 8 || address.size() > 90) {
        error = "bech32 address length out of range";
        return false;
    }

    bool hasLower = false, hasUpper = false;
    for (const char c : address) {
        if (c >= 'a' && c <= 'z') hasLower = true;
        else if (c >= 'A' && c <= 'Z') hasUpper = true;
        else if (!(c >= '0' && c <= '9')) {
            error = "bech32 address contains invalid characters";
            return false;
        }
    }
    if (hasLower && hasUpper) {
        error = "bech32 address mixes upper and lower case";
        return false;
    }

    const size_t sep = address.find_last_of('1');
    if (sep == std::string::npos) {
        error = "bech32 address missing separator";
        return false;
    }

    std::string hrp;
    hrp.reserve(sep);
    for (size_t i = 0; i < sep; ++i) {
        char c = address[i];
        if (c >= 'A' && c <= 'Z') {
            c = static_cast<char>(c - 'A' + 'a');
        }
        hrp.push_back(c);
    }
    if (hrp != "pc") {
        error = "unsupported bech32 HRP (expected \"pc\")";
        return false;
    }

    std::vector<uint8_t> values;
    values.reserve(address.size() - sep - 1);
    for (size_t i = sep + 1; i < address.size(); ++i) {
        const int v = bech32CharIndex(address[i]);
        if (v < 0) {
            error = "bech32 address contains invalid characters";
            return false;
        }
        values.push_back(static_cast<uint8_t>(v));
    }
    if (values.size() < 6) {
        error = "bech32 address data too short";
        return false;
    }

    std::vector<uint8_t> expanded = bech32HrpExpand(hrp);
    std::vector<uint8_t> checkValues = expanded;
    checkValues.insert(checkValues.end(), values.begin(), values.end());
    const uint32_t check = bech32Polymod(checkValues);

    const uint8_t version = values[0];
    if (version > 16) {
        error = "bech32 witness version out of range";
        return false;
    }

    // v0 must use bech32, v1+ must use bech32m.
    if ((version == 0 && check != kBech32Const) || (version > 0 && check != kBech32mConst)) {
        error = (version == 0) ? "bech32 checksum mismatch" : "bech32m checksum mismatch";
        return false;
    }

    std::vector<uint8_t> programData(values.begin() + 1, values.end() - 6);
    std::vector<uint8_t> program;
    if (!convertBits(programData, 5, 8, false, program)) {
        error = "bech32 witness program has invalid padding";
        return false;
    }
    if (program.size() < 2 || program.size() > 40) {
        error = "bech32 witness program length out of range";
        return false;
    }
    if (version == 0 && program.size() != 20 && program.size() != 32) {
        error = "bech32 v0 witness program must be 20 or 32 bytes";
        return false;
    }

    scriptPubKeyOut.clear();
    if (version == 0) {
        scriptPubKeyOut.push_back(0x00); // OP_0
    } else {
        scriptPubKeyOut.push_back(static_cast<uint8_t>(0x50 + version)); // OP_1 .. OP_16
    }
    scriptPubKeyOut.push_back(static_cast<uint8_t>(program.size()));
    scriptPubKeyOut.insert(scriptPubKeyOut.end(), program.begin(), program.end());
    return true;
}

} // namespace

namespace xmrig {

namespace {

static void appendLE32(std::vector<uint8_t> &out, uint32_t v)
{
    for (int i = 0; i < 4; ++i) {
        out.push_back(static_cast<uint8_t>((v >> (8 * i)) & 0xff));
    }
}

static void appendLE64(std::vector<uint8_t> &out, uint64_t v)
{
    for (int i = 0; i < 8; ++i) {
        out.push_back(static_cast<uint8_t>((v >> (8 * i)) & 0xff));
    }
}

} // namespace

bool PcoinTemplate::decodeAddress(const std::string &address, std::vector<uint8_t> &scriptPubKey, std::string &error) const
{
    return decodeBech32Address(address, scriptPubKey, error);
}

bool PcoinTemplate::buildCoinbase(const rapidjson::Value &result, const std::string &payoutAddress, std::string &error)
{
    std::vector<uint8_t> payoutScript;
    if (!decodeAddress(payoutAddress, payoutScript, error)) {
        return false;
    }

    // Coinbase scriptSig: BIP34 height (minimal script number push),
    // optional GBT coinbaseaux flags, then a miner tag.
    std::vector<uint8_t> scriptSig;
    const std::vector<uint8_t> num = scriptNumber(m_height);
    scriptSig.push_back(static_cast<uint8_t>(num.size()));
    scriptSig.insert(scriptSig.end(), num.begin(), num.end());
    if (result.HasMember("coinbaseaux") && result["coinbaseaux"].IsObject()) {
        const char *flagsHex = Json::getString(result["coinbaseaux"], "flags");
        if (flagsHex && *flagsHex) {
            std::vector<uint8_t> flags;
            if (hexToBytes(flagsHex, flags)) {
                scriptSig.insert(scriptSig.end(), flags.begin(), flags.end());
            }
        }
    }
    const char *tag = "/PCoin/xmrig/";
    scriptSig.insert(scriptSig.end(), tag, tag + std::strlen(tag));
    if (scriptSig.size() < 2 || scriptSig.size() > 100) {
        error = "coinbase scriptSig size out of range";
        return false;
    }

    // Outputs: payout, then the witness commitment OP_RETURN when the
    // template carries one. The commitment script is used verbatim.
    std::vector<std::pair<uint64_t, std::vector<uint8_t>>> outputs;
    outputs.emplace_back(static_cast<uint64_t>(m_coinbaseValue), std::move(payoutScript));

    std::vector<uint8_t> commitment;
    bool hasCommitment = false;
    if (result.HasMember("default_witness_commitment")) {
        const char *commitmentHex = Json::getString(result, "default_witness_commitment");
        if (!commitmentHex || !hexToBytes(commitmentHex, commitment)) {
            error = "malformed default_witness_commitment";
            return false;
        }
        if (commitment.size() < 38 || commitment[0] != 0x6a || commitment[1] != 0x24
            || commitment[2] != 0xaa || commitment[3] != 0x21 || commitment[4] != 0xa9 || commitment[5] != 0xed) {
            error = "malformed default_witness_commitment";
            return false;
        }
        outputs.emplace_back(0, std::move(commitment));
        hasCommitment = true;
    }

    // Shared input serialization.
    std::vector<uint8_t> input;
    input.insert(input.end(), 32, 0x00);               // prev tx hash: all zeros
    input.insert(input.end(), { 0xff, 0xff, 0xff, 0xff }); // prev index: -1
    writeCompactSize(input, scriptSig.size());
    input.insert(input.end(), scriptSig.begin(), scriptSig.end());
    input.insert(input.end(), { 0xff, 0xff, 0xff, 0xff }); // sequence

    std::vector<uint8_t> vout;
    for (const auto &kv : outputs) {
        appendLE64(vout, kv.first);
        writeCompactSize(vout, kv.second.size());
        vout.insert(vout.end(), kv.second.begin(), kv.second.end());
    }

    // Stripped serialization (used for the txid, which the header commits to).
    m_coinbaseStripped.clear();
    appendLE32(m_coinbaseStripped, 2);
    writeCompactSize(m_coinbaseStripped, 1);
    m_coinbaseStripped.insert(m_coinbaseStripped.end(), input.begin(), input.end());
    writeCompactSize(m_coinbaseStripped, outputs.size());
    m_coinbaseStripped.insert(m_coinbaseStripped.end(), vout.begin(), vout.end());
    appendLE32(m_coinbaseStripped, 0);                  // locktime

    // Full serialization (goes into the block). The witness is a single
    // 32-byte zero stack item, which is what default_witness_commitment
    // was computed against (reserved value = 32 zero bytes).
    if (hasCommitment) {
        m_coinbaseFull.clear();
        appendLE32(m_coinbaseFull, 2);                  // version
        m_coinbaseFull.push_back(0x00);                 // witness marker
        m_coinbaseFull.push_back(0x01);                 // witness flag
        writeCompactSize(m_coinbaseFull, 1);
        m_coinbaseFull.insert(m_coinbaseFull.end(), input.begin(), input.end());
        writeCompactSize(m_coinbaseFull, outputs.size());
        m_coinbaseFull.insert(m_coinbaseFull.end(), vout.begin(), vout.end());
        writeCompactSize(m_coinbaseFull, 1);            // 1 stack item
        writeCompactSize(m_coinbaseFull, 32);           // item size
        m_coinbaseFull.insert(m_coinbaseFull.end(), 32, 0x00);
        appendLE32(m_coinbaseFull, 0);                  // locktime
    } else {
        m_coinbaseFull = m_coinbaseStripped;
    }

    return true;
}

bool PcoinTemplate::parse(const rapidjson::Value &result, const std::string &payoutAddress, std::string &error)
{
    m_txs.clear();
    m_coinbaseStripped.clear();
    m_coinbaseFull.clear();
    m_target.clear();
    m_identity.clear();
    m_valid = false;

    std::memset(m_header, 0, sizeof(m_header));

    std::vector<uint8_t> prev;
    const char *prevHex = Json::getString(result, "previousblockhash");
    if (!prevHex || !hexToBytes(prevHex, prev) || prev.size() != 32) {
        error = "missing or invalid previousblockhash";
        return false;
    }
    std::reverse(prev.begin(), prev.end());   // GBT sends display (byte-reversed) order

    std::vector<uint8_t> bits;
    const char *bitsHex = Json::getString(result, "bits");
    if (!bitsHex || !hexToBytes(bitsHex, bits) || bits.size() != 4) {
        error = "missing or invalid bits";
        return false;
    }
    std::reverse(bits.begin(), bits.end());   // GBT sends display order; header wants LE bytes

    const char *targetHex = Json::getString(result, "target");
    {
        std::vector<uint8_t> targetBytes;
        if (!targetHex || !hexToBytes(targetHex, targetBytes) || targetBytes.size() != 32) {
            error = "missing or invalid target";
            return false;
        }
        m_target = targetHex;
    }

    m_height = Json::getUint64(result, "height");
    if (m_height == 0) {
        error = "missing height";
        return false;
    }
    m_coinbaseValue = Json::getInt64(result, "coinbasevalue");

    const int64_t version = Json::getInt64(result, "version");
    const int64_t curtime = Json::getInt64(result, "curtime");
    if (curtime <= 0) {
        error = "missing curtime";
        return false;
    }

    writeLE32(m_header, 0, static_cast<uint32_t>(version));
    std::memcpy(m_header + 4, prev.data(), 32);
    writeLE32(m_header, 68, static_cast<uint32_t>(curtime));
    std::memcpy(m_header + 72, bits.data(), 4);
    // 32 zero bytes at offset 76..80: nonce (miner writes it)

    // GBT transactions: store the raw witness-serialized bytes for the block
    // and their txids (internal order) for the header merkle root.
    std::vector<std::vector<uint8_t>> txids;
    const auto txsMember = result.FindMember("transactions");
    if (txsMember != result.MemberEnd() && txsMember->value.IsArray()) {
        for (const auto &entry : txsMember->value.GetArray()) {
            const char *dataHex = Json::getString(entry, "data");
            const char *txidHex = Json::getString(entry, "txid");
            std::vector<uint8_t> raw;
            if (!dataHex || !hexToBytes(dataHex, raw) || raw.empty()) {
                error = "transaction entry missing or invalid data";
                return false;
            }
            std::vector<uint8_t> txid;
            if (!txidHex || !hexToBytes(txidHex, txid) || txid.size() != 32) {
                error = "transaction entry missing or invalid txid";
                return false;
            }
            std::reverse(txid.begin(), txid.end());
            m_txs.push_back(std::move(raw));
            txids.push_back(std::move(txid));
        }
    }

    if (!buildCoinbase(result, payoutAddress, error)) {
        return false;
    }

    std::vector<std::vector<uint8_t>> leaves;
    leaves.reserve(1 + txids.size());
    uint8_t coinbaseTxid[32];
    sha256d(m_coinbaseStripped.data(), m_coinbaseStripped.size(), coinbaseTxid);
    leaves.emplace_back(coinbaseTxid, coinbaseTxid + 32);
    for (auto &txid : txids) {
        leaves.push_back(std::move(txid));
    }
    const std::vector<uint8_t> root = computeMerkleRoot(leaves);
    std::memcpy(m_header + 36, root.data(), 32);

    // Time-independent identity for the daemon client's template dedup:
    // version|prevhash(display)|bits|height|coinbasevalue|coinbase hex|tx hexes.
    {
        std::string id;
        id.reserve(128 + (m_coinbaseStripped.size() + 1) * 2 + 64);
        id += std::to_string(version);
        id += '|';
        id += prevHex;
        id += '|';
        id += bitsHex;
        id += '|';
        id += std::to_string(m_height);
        id += '|';
        id += std::to_string(m_coinbaseValue);
        id += '|';
        id += bytesToHex(m_coinbaseStripped.data(), m_coinbaseStripped.size());
        for (const auto &tx : m_txs) {
            id += ';';
            id += bytesToHex(tx.data(), tx.size());
        }
        m_identity = std::move(id);
    }

    m_valid = true;
    return true;
}

bool PcoinTemplate::buildBlock(uint32_t nonce, std::string &hexOut) const
{
    if (!m_valid) {
        return false;
    }

    std::vector<uint8_t> block(m_header, m_header + sizeof(m_header));
    writeLE32(block.data() + 76, 0, nonce);
    writeCompactSize(block, 1 + m_txs.size());
    block.insert(block.end(), m_coinbaseFull.begin(), m_coinbaseFull.end());
    for (const auto &tx : m_txs) {
        block.insert(block.end(), tx.begin(), tx.end());
    }

    hexOut = bytesToHex(block.data(), block.size());
    return true;
}

bool PcoinTemplate::hashMeetsTarget(const uint8_t *hash32) const
{
    // GBT target is 32-byte big-endian display. The RandomX hash is the raw
    // 32 bytes the node interprets as a little-endian 256-bit integer. Compare
    // both viewed big-endian, which is equivalent.
    std::vector<uint8_t> targetBytes;
    if (!hexToBytes(m_target.c_str(), targetBytes) || targetBytes.size() != 32) {
        return false;
    }
    std::vector<uint8_t> hashBe(32);
    for (int i = 0; i < 32; ++i) {
        hashBe[i] = hash32[31 - i];
    }
    for (int i = 0; i < 32; ++i) {
        if (hashBe[i] != targetBytes[i]) {
            return hashBe[i] < targetBytes[i];
        }
    }
    return true; // hash == target: node uses <=
}

} // namespace xmrig
