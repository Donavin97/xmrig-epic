/* XMRig
 * Copyright (c) 2019      Howard Chu  <https://github.com/hyc>
 * Copyright (c) 2018-2021 SChernykh   <https://github.com/SChernykh>
 * Copyright (c) 2016-2021 XMRig       <https://github.com/xmrig>, <support@xmrig.com>
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
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include <uv.h>


#include "base/net/stratum/EpicDaemonClient.h"
#include "3rdparty/rapidjson/document.h"
#include "3rdparty/rapidjson/error/en.h"
#include "3rdparty/rapidjson/stringbuffer.h"
#include "3rdparty/rapidjson/writer.h"
#include "base/io/json/Json.h"
#include "base/io/json/JsonRequest.h"
#include "base/io/log/Log.h"
#include "base/kernel/interfaces/IClientListener.h"
#include "base/kernel/Platform.h"
#include "base/net/dns/Dns.h"
#include "base/net/dns/DnsRecords.h"
#include "base/net/stratum/SubmitResult.h"
#include "base/net/tools/NetBuffer.h"
#include "base/tools/Chrono.h"
#include "base/tools/Cvt.h"
#include "base/tools/Timer.h"
#include "net/JobResult.h"


#include <algorithm>
#include <cassert>
#include <cstring>
#include <memory>


namespace xmrig {


Storage<EpicDaemonClient> EpicDaemonClient::m_storage;


// Approximate the epic node's solution difficulty: the node computes
// uint256_max / hash (big-endian, pow/types.rs from_proof_hash). Using only
// the top 64 bits gives uint256_max / (top64 * 2^192) = uint64_max / top64,
// with a relative error below 1/2^32 for block-range difficulties.
static uint64_t epicActualDiff(const uint8_t *hash)
{
    uint64_t top = 0;
    for (int i = 0; i < 8; ++i) {
        top = (top << 8) | hash[i];
    }

    return top ? (0xFFFFFFFFFFFFFFFFULL / top) : 0;
}


static const char *kAlgorithm    = "algorithm";
static const char *kBlockDifficulty = "block_difficulty";
static const char *kDifficulty   = "difficulty";
static const char *kEpochs       = "epochs";
static const char *kJobId        = "job_id";
static const char *kPrePow       = "pre_pow";
static const char *kHeight       = "height";


} // namespace xmrig


xmrig::EpicDaemonClient::EpicDaemonClient(int id, const char *agent, IClientListener *listener) :
    BaseClient(id, listener),
    m_agent(agent),
    m_sendBuf(1024)
{
    m_reader.setListener(this);
    m_key    = m_storage.add(this);
    m_timer  = new Timer(this);
}


xmrig::EpicDaemonClient::~EpicDaemonClient()
{
    delete m_timer;
    delete m_socket;
}


bool xmrig::EpicDaemonClient::disconnect()
{
    m_failures = -1;
    m_timer->stop();

    return close();
}


bool xmrig::EpicDaemonClient::isTLS() const
{
    return false;
}


void xmrig::EpicDaemonClient::deleteLater()
{
    if (!m_listener) {
        return;
    }

    m_listener = nullptr;

    if (!disconnect()) {
        m_storage.remove(m_key);
    }
}


int64_t xmrig::EpicDaemonClient::send(const rapidjson::Value &obj, Callback callback)
{
    assert(obj["id"] == sequence());

    m_callbacks.insert({ sequence(), std::move(callback) });

    return send(obj);
}


int64_t xmrig::EpicDaemonClient::send(const rapidjson::Value &obj)
{
    using namespace rapidjson;

    StringBuffer buffer(nullptr, 512);
    Writer<StringBuffer> writer(buffer);
    obj.Accept(writer);

    const size_t size = buffer.GetSize();
    if (size > kMaxSendBufferSize) {
        LOG_ERR("%s " RED("send failed: ") RED_BOLD("\"max send buffer size exceeded: %zu\""), tag(), size);
        close();

        return -1;
    }

    if (m_sendBuf.size() < size + 2) {
        m_sendBuf.resize(((size + 1) / 1024 + 1) * 1024);
    }

    memcpy(m_sendBuf.data(), buffer.GetString(), size);
    m_sendBuf[size]     = '\n';
    m_sendBuf[size + 1] = '\0';

    return send(size + 1);
}


int64_t xmrig::EpicDaemonClient::submit(const JobResult &result)
{
    if (m_state != ConnectedState) {
        return -1;
    }

    const auto it = m_jobIds.find(result.jobId);
    if (it == m_jobIds.end()) {
        return -1;
    }

    using namespace rapidjson;

    Document doc(kObjectType);
    auto &allocator = doc.GetAllocator();

    Value params(kObjectType);
    params.AddMember("height", result.height, allocator);
    params.AddMember("job_id", it->second, allocator);
    params.AddMember("nonce",  result.nonce, allocator);

    Value pow(kObjectType);
    {
        Value pow_dec(kArrayType);
        for (int i = 0; i < 32; i++) {
            pow_dec.PushBack(result.result()[i], allocator);
        }
        // AlgorithmParams is an externally tagged enum in the node's stratum server.
        pow.AddMember(StringRef("RandomX"), pow_dec, allocator);
    }
    params.AddMember("pow", pow, allocator);

    JsonRequest::create(doc, m_sequence, "submit", params);

    m_results[m_sequence] = SubmitResult(m_sequence, result.diff, epicActualDiff(result.result()), 0, result.backend);

    return send(doc);
}


void xmrig::EpicDaemonClient::connect()
{
    if (!m_pool.isValid()) {
        if (!isQuiet()) {
            LOG_ERR("%s " RED("connect error: ") RED_BOLD("\"%s\""), tag(), "Invalid host.");
        }

        return reconnect();
    }

    if (m_pool.coin().isValid() && m_pool.coin() != Coin::EPIC) {
        if (!isQuiet()) {
            LOG_ERR("%s " RED("connect error: ") RED_BOLD("\"%s\""), tag(), "Epic daemon mining requires \"coin\": \"EPIC\".");
        }

        return reconnect();
    }

    m_reader.reset();

    if (m_failures == -1) {
        m_failures = 0;
    }

    m_dns = Dns::resolve(m_pool.host(), this);
}


void xmrig::EpicDaemonClient::connect(const Pool &pool)
{
    setPool(pool);
    connect();
}


void xmrig::EpicDaemonClient::tick(uint64_t now)
{
    if (m_state == ConnectedState) {
        if (m_expire && now > m_expire) {
            close();
        }

        return;
    }

    if (m_state == ReconnectingState && m_expire && now > m_expire) {
        return connect();
    }

    if (m_state == ConnectingState && m_expire && now > m_expire) {
        close();
    }
}


void xmrig::EpicDaemonClient::onTimer(const Timer *)
{
    if (m_state != ConnectedState) {
        return;
    }

    getJobTemplate();
}


void xmrig::EpicDaemonClient::onResolved(const DnsRecords &records, int status, const char *error)
{
    m_dns.reset();

    assert(m_listener != nullptr);
    if (!m_listener) {
        return reconnect();
    }

    if (status < 0 && records.isEmpty()) {
        if (!isQuiet()) {
            LOG_ERR("%s " RED("DNS error: ") RED_BOLD("\"%s\""), tag(), error);
        }

        return reconnect();
    }

    const auto &record = records.get();
    m_ip = record.ip();

    setState(ConnectingState);

    auto req = new uv_connect_t;
    req->data = m_storage.ptr(m_key);

    m_socket = new uv_tcp_t;
    m_socket->data = m_storage.ptr(m_key);

    uv_tcp_init(uv_default_loop(), m_socket);
    uv_tcp_nodelay(m_socket, 1);

    if (Platform::hasKeepalive()) {
        uv_tcp_keepalive(m_socket, 1, 60);
    }

    uv_tcp_connect(req, m_socket, record.addr(m_pool.port()), onConnect);
}


void xmrig::EpicDaemonClient::handshake()
{
    login();
    getJobTemplate();

    const uint64_t interval = std::max<uint64_t>(1000, m_pool.pollInterval());
    m_timer->start(interval, interval);
}


void xmrig::EpicDaemonClient::login()
{
    using namespace rapidjson;

    Document doc(kObjectType);
    auto &allocator = doc.GetAllocator();

    Value params(kObjectType);
    params.AddMember("login", m_user.toJSON(),     allocator);
    params.AddMember("pass",  m_password.toJSON(), allocator);
    params.AddMember("agent", StringRef(m_agent),  allocator);

    JsonRequest::create(doc, m_sequence, "login", params);

    send(doc);
}


int64_t xmrig::EpicDaemonClient::getJobTemplate()
{
    using namespace rapidjson;

    Document doc(kObjectType);
    auto &allocator = doc.GetAllocator();

    Value params(kObjectType);
    params.AddMember(StringRef(kAlgorithm), StringRef("randomx"), allocator);

    JsonRequest::create(doc, m_sequence, "getjobtemplate", params);

    return send(doc);
}


bool xmrig::EpicDaemonClient::close()
{
    if (m_state == ClosingState) {
        return m_socket != nullptr;
    }

    if (m_state == UnconnectedState || m_socket == nullptr) {
        return false;
    }

    setState(ClosingState);

    if (uv_is_closing(reinterpret_cast<uv_handle_t*>(m_socket)) == 0) {
        if (Platform::hasKeepalive()) {
            uv_tcp_keepalive(m_socket, 0, 60);
        }
        uv_close(reinterpret_cast<uv_handle_t*>(m_socket), EpicDaemonClient::onClose);
    }

    return true;
}


bool xmrig::EpicDaemonClient::parseJob(const rapidjson::Value &params, int *code)
{
    auto jobError = [this, code](const char *message) {
        if (!isQuiet()) {
            LOG_ERR("%s " RED("job error: ") RED_BOLD("\"%s\""), tag(), message);
        }

        *code = 1;

        return false;
    };

    if (!params.IsObject()) {
        *code = 2;
        return false;
    }

    const char *algo = Json::getString(params, kAlgorithm);
    if (algo == nullptr) {
        algo = "randomx"; // tolerate nodes without the "algorithm" field
    }

    const bool mining = (strcmp(algo, "randomx") == 0);

    const uint64_t height = Json::getUint64(params, kHeight);
    const uint64_t jobId  = Json::getUint64(params, kJobId);

    const rapidjson::Value &prePow = Json::getValue(params, kPrePow);
    if (!prePow.IsString()) {
        return jobError("Invalid pre_pow in job template.");
    }

    // Build the 282-byte mining blob: pre_pow (274 bytes) + 8-byte big-endian
    // nonce placeholder. The worker loop patches the nonce into bytes 274..281.
    std::string blobHex(prePow.GetString());
    blobHex.append("0000000000000000");

    uint64_t diff = 0;
    const rapidjson::Value &difficulty = Json::getValue(params, kDifficulty);
    if (difficulty.IsArray()) {
        for (const auto &entry : difficulty.GetArray()) {
            if (entry.IsArray() && entry.Size() >= 2 && entry[0].IsString() && entry[1].IsUint64()) {
                if (strcmp(entry[0].GetString(), "randomx") == 0) {
                    diff = entry[1].GetUint64();
                    break;
                }
            }
        }
    }
    else if (difficulty.IsUint64()) {
        diff = difficulty.GetUint64();
    }

    // Only require RandomX difficulty when actually mining RandomX.
    // For other algorithms (progpow, cuckoo), the daemon may not provide it.
    if (mining && diff == 0) {
        return jobError("No RandomX share difficulty received from daemon.");
    }

    // The full network block difficulty for RandomX, used by "block" share
    // difficulty mode (daemon-share-diff: "block") for pure solo mining.
    uint64_t blockDiff = 0;
    const rapidjson::Value &blockDifficulty = Json::getValue(params, kBlockDifficulty);
    if (blockDifficulty.IsArray()) {
        for (const auto &entry : blockDifficulty.GetArray()) {
            if (entry.IsArray() && entry.Size() >= 2 && entry[0].IsString() && entry[1].IsUint64()) {
                if (strcmp(entry[0].GetString(), "randomx") == 0) {
                    blockDiff = entry[1].GetUint64();
                    break;
                }
            }
        }
    }

    // Optional locally-raised share difficulty. The node accepts any share at
    // or above its minimum, so this only moves the submission threshold —
    // block-finding is unaffected. The special value kShareDiffBlock (set via
    // daemon-share-diff: "block") mines directly at the network block
    // difficulty, submitting only actual block solutions.
    if (mining && m_pool.shareDiff() == Pool::kShareDiffBlock) {
        if (blockDiff == 0) {
            return jobError("No RandomX block difficulty received from daemon.");
        }

        diff = blockDiff;
    }
    else if (mining && m_pool.shareDiff() > diff) {
        diff = m_pool.shareDiff();
    }

    Job job(false, m_pool.algorithm(), String());

    if (!job.setBlob(blobHex.c_str())) {
        *code = 3;
        return false;
    }

    job.setAlgorithm("rx/epic");
    job.setHeight(height);
    job.setDiff(diff);

    // RandomX seed epoch(s) provided by the daemon.
    bool haveSeed = false;
    const rapidjson::Value &epochs = Json::getValue(params, kEpochs);
    if (epochs.IsArray()) {
        const rapidjson::Value *selected = nullptr;

        for (const auto &epoch : epochs.GetArray()) {
            if (!epoch.IsArray() || epoch.Size() < 3 || !epoch[0].IsUint64() || !epoch[1].IsUint64() ||
                !epoch[2].IsArray() || epoch[2].Size() != 32) {
                continue;
            }

            selected = &epoch;

            const uint64_t start = epoch[0].GetUint64();
            const uint64_t end   = epoch[1].GetUint64();
            if (height >= start && (end == 0 || height < end)) {
                break;
            }
        }

        if (selected) {
            uint8_t seed[Job::kMaxSeedSize];
            for (size_t i = 0; i < Job::kMaxSeedSize; ++i) {
                if (!(*selected)[2][i].IsUint() || (*selected)[2][i].GetUint() > 255) {
                    return jobError("Invalid RandomX seed epoch received from daemon.");
                }

                seed[i] = static_cast<uint8_t>((*selected)[2][i].GetUint());
            }

            if (!job.setSeedHash(Cvt::toHex(seed, Job::kMaxSeedSize))) {
                return jobError("Invalid RandomX seed hash received from daemon.");
            }

            haveSeed = true;
        }
    }

    if (mining && !haveSeed) {
        return jobError("No RandomX seed epoch received from daemon.");
    }

    if (!mining) {
        job.setEpicpause(true);
    }

    // Deterministic id so that re-polled identical templates are skipped.
    m_currentJobId = String((std::to_string(jobId) + "-" + std::to_string(height)).c_str());

    job.setId(m_currentJobId);

    m_jobIds[m_currentJobId] = jobId;
    if (m_jobIds.size() > 16) {
        for (auto it = m_jobIds.begin(); it != m_jobIds.end(); ++it) {
            if (it->first != m_currentJobId) {
                m_jobIds.erase(it);
                break;
            }
        }
    }

    if (m_job == job) {
        return true;
    }

    m_job = std::move(job);

    if (mining) {
        LOG_INFO("%s " GREEN("rx/epic job received: ") WHITE_BOLD("height %llu, job_id %llu, diff %llu"),
                 tag(), static_cast<unsigned long long>(height), static_cast<unsigned long long>(jobId),
                 static_cast<unsigned long long>(diff));
    }
    else {
        // Non-randomx algorithms (progpow, cuckoo) - silently pause
        m_listener->onJobReceived(this, m_job, params);
        return true;
    }

    m_listener->onJobReceived(this, m_job, params);

    return true;
}


void xmrig::EpicDaemonClient::onClose()
{
    delete m_socket;

    m_socket = nullptr;
    setState(UnconnectedState);

    m_timer->stop();

    reconnect();
}


void xmrig::EpicDaemonClient::parse(char *line, size_t len)
{
    m_expire = 0;

    LOG_DEBUG("[%s] received (%d bytes): \"%.*s\"", m_pool.url(), static_cast<int>(len), static_cast<int>(len), line);

    if (len < 2 || line[0] != '{') {
        if (!isQuiet()) {
            LOG_ERR("%s " RED("JSON decode failed"), tag());
        }

        return;
    }

    rapidjson::Document doc;
    if (doc.ParseInsitu(line).HasParseError()) {
        if (!isQuiet()) {
            LOG_ERR("%s " RED("JSON decode failed: ") RED_BOLD("\"%s\""), tag(), rapidjson::GetParseError_En(doc.GetParseError()));
        }

        return;
    }

    if (!doc.IsObject()) {
        return;
    }

    const auto &id     = Json::getValue(doc, "id");
    const auto &error  = Json::getValue(doc, "error");
    const char *method = Json::getString(doc, "method");

    int64_t requestId = -1;
    if (id.IsInt64()) {
        requestId = id.GetInt64();
    }
    else if (id.IsString()) {
        const char *s = id.GetString();
        char *end     = nullptr;
        const long long value = strtoll(s, &end, 10);
        if (end != s && *end == '\0') {
            requestId = value;
        }
    }

    if (requestId >= 0) {
        return parseResponse(requestId, Json::getValue(doc, "result"), error);
    }

    if (!method) {
        return;
    }

    if (error.IsObject()) {
        if (!isQuiet()) {
            LOG_ERR("%s " RED("error: ") RED_BOLD("\"%s\"") RED(", code: ") RED_BOLD("%d"),
                    tag(), Json::getString(error, "message"), Json::getInt(error, "code"));
        }

        return;
    }

    if (strcmp(method, "job") == 0) {
        int code = -1;
        parseJob(Json::getValue(doc, "params"), &code);

        return;
    }
}


void xmrig::EpicDaemonClient::parseResponse(int64_t id, const rapidjson::Value &result, const rapidjson::Value &error)
{
    if (error.IsObject()) {
        const char *message = Json::getString(error, "message");
        const int code      = Json::getInt(error, "code");

        if (handleSubmitResponse(id, message)) {
            return;
        }

        if (code == -32000) {
            if (!isQuiet()) {
                LOG_WARN("%s " YELLOW("node is syncing, waiting..."), tag());
            }

            return;
        }

        if (!isQuiet()) {
            LOG_ERR("%s " RED("error: ") RED_BOLD("\"%s\"") RED(", code: ") RED_BOLD("%d"), tag(), message, code);
        }

        return;
    }

    if (result.IsObject() && result.HasMember(kPrePow)) {
        int code = -1;
        if (!parseJob(result, &code)) {
            if (!isQuiet()) {
                LOG_ERR("%s " RED("job error code: ") RED_BOLD("%d"), tag(), code);
            }
        }

        return;
    }

    // Share accepted: "ok", block found: "blockfound - <hash>".
    if (result.IsString()) {
        const char *res = result.GetString();

        if (handleSubmitResponse(id, nullptr)) {
            if (strncmp(res, "blockfound", 10) == 0) {
                LOG_WARN("%s " GREEN_BOLD("BLOCK FOUND: ") GREEN("%s"), tag(), res);
            }
        }

        return;
    }

    handleSubmitResponse(id);
}


void xmrig::EpicDaemonClient::read(ssize_t nread, const uv_buf_t *buf)
{
    if (nread < 0) {
        if (!isQuiet()) {
            LOG_ERR("%s " RED("read error: ") RED_BOLD("\"%s\""), tag(), uv_strerror(static_cast<int>(nread)));
        }

        close();

        return;
    }

    assert(m_listener != nullptr);
    if (!m_listener) {
        return reconnect();
    }

    m_reader.parse(buf->base, static_cast<size_t>(nread));
}


void xmrig::EpicDaemonClient::reconnect()
{
    if (!m_listener) {
        m_storage.remove(m_key);

        return;
    }

    if (m_failures == -1) {
        return m_listener->onClose(this, -1);
    }

    setState(ReconnectingState);

    m_failures++;
    m_listener->onClose(this, static_cast<int>(m_failures));
}


void xmrig::EpicDaemonClient::setState(SocketState state)
{
    if (m_state == state) {
        return;
    }

    switch (state) {
    case ConnectingState:
        m_expire = Chrono::steadyMSecs() + kConnectTimeout;
        break;

    case ReconnectingState:
        m_expire = Chrono::steadyMSecs() + m_retryPause;
        break;

    case ConnectedState:
        // Signal activation so the strategy layer and the "connection" /
        // "results" console commands report this pool as active.
        m_failures = 0;
        m_listener->onLoginSuccess(this);
        break;

    default:
        break;
    }

    m_state = state;
}


bool xmrig::EpicDaemonClient::write(const uv_buf_t &buf)
{
    const int rc = uv_try_write(stream(), &buf, 1);
    if (static_cast<size_t>(rc) == buf.len) {
        return true;
    }

    if (!isQuiet()) {
        LOG_ERR("%s " RED("write error: ") RED_BOLD("\"%s\""), tag(), uv_strerror(rc));
    }

    close();

    return false;
}


int64_t xmrig::EpicDaemonClient::send(size_t size)
{
    LOG_DEBUG("[%s] send (%d bytes): \"%.*s\"", m_pool.url(), static_cast<int>(size), static_cast<int>(size) - 1, m_sendBuf.data());

    if (state() != ConnectedState || !uv_is_writable(stream())) {
        LOG_DEBUG_ERR("[%s] send failed, invalid state: %d", m_pool.url(), m_state);

        return -1;
    }

    uv_buf_t buf = uv_buf_init(m_sendBuf.data(), (unsigned int) size);

    if (!write(buf)) {
        return -1;
    }

    m_expire = Chrono::steadyMSecs() + kResponseTimeout;

    return m_sequence++;
}


void xmrig::EpicDaemonClient::onClose(uv_handle_t *handle)
{
    auto client = getClient(handle->data);
    if (!client) {
        return;
    }

    client->onClose();
}


void xmrig::EpicDaemonClient::onConnect(uv_connect_t *req, int status)
{
    auto client = getClient(req->data);
    delete req;

    if (!client) {
        return;
    }

    if (status < 0) {
        if (!client->isQuiet()) {
            LOG_ERR("%s %s " RED("connect error: ") RED_BOLD("\"%s\""), client->tag(), client->ip().data(), uv_strerror(status));
        }

        if (client->state() == ReconnectingState || client->state() == ClosingState) {
            return;
        }

        if (client->state() != ConnectingState) {
            return;
        }

        client->close();

        return;
    }

    if (client->state() == ConnectedState) {
        return;
    }

    client->setState(ConnectedState);

    uv_read_start(client->stream(), NetBuffer::onAlloc, onRead);

    client->handshake();
}


void xmrig::EpicDaemonClient::onRead(uv_stream_t *stream, ssize_t nread, const uv_buf_t *buf)
{
    auto client = getClient(stream->data);
    if (client) {
        client->read(nread, buf);
    }

    NetBuffer::release(buf);
}
