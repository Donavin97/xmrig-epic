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

#ifndef XMRIG_EPICDAEMONCLIENT_H
#define XMRIG_EPICDAEMONCLIENT_H


#include <map>


#include "base/kernel/interfaces/IDnsListener.h"
#include "base/kernel/interfaces/ILineListener.h"
#include "base/kernel/interfaces/ITimerListener.h"
#include "base/net/stratum/BaseClient.h"
#include "base/net/tools/LineReader.h"
#include "base/net/tools/Storage.h"
#include "base/tools/Object.h"


using uv_buf_t      = struct uv_buf_t;
using uv_connect_t  = struct uv_connect_s;
using uv_handle_t   = struct uv_handle_s;
using uv_stream_t   = struct uv_stream_s;
using uv_tcp_t      = struct uv_tcp_s;


namespace xmrig {


class DnsRequest;
class IClientListener;
class JobResult;
class Timer;


/**
 * @brief Stratum client for a local (or remote) Epic Cash node stratum server.
 *
 * The Epic node's built-in stratum server (default port 3416) speaks a
 * Grin-style, newline-delimited JSON-RPC protocol over a raw TCP socket,
 * which is different from the Monero-style stratum used by Epic pools:
 *
 *   - "getjobtemplate" (poll) / "job" (push): jobs carry a 274-byte hex
 *     "pre_pow" header prefix, the share "difficulty" map, RandomX seed
 *     "epochs" and the designated "algorithm" (randomx/cuckoo/progpow).
 *   - "submit": {"height": u64, "job_id": u64, "nonce": u64,
 *     "pow": [32 decimal hash bytes]}.
 *
 * The mining blob is built as pre_pow (274 bytes) + 8-byte big-endian
 * nonce placeholder (282 bytes total), which is exactly the layout the
 * epic worker loop in CpuWorker uses.
 */
class EpicDaemonClient : public BaseClient, public IDnsListener, public ILineListener, public ITimerListener
{
public:
    XMRIG_DISABLE_COPY_MOVE_DEFAULT(EpicDaemonClient)

    constexpr static uint64_t kConnectTimeout   = 20 * 1000;
    constexpr static uint64_t kResponseTimeout  = 20 * 1000;
    constexpr static size_t kMaxSendBufferSize  = 1024 * 16;
    constexpr static size_t kEpicBlobSize       = 282;   // 274-byte pre_pow + 8-byte nonce

    EpicDaemonClient(int id, const char *agent, IClientListener *listener);
    ~EpicDaemonClient() override;

protected:
    bool disconnect() override;
    bool isTLS() const override;
    int64_t send(const rapidjson::Value &obj, Callback callback) override;
    int64_t send(const rapidjson::Value &obj) override;
    int64_t submit(const JobResult &result) override;
    void connect() override;
    void connect(const Pool &pool) override;
    void deleteLater() override;
    void tick(uint64_t now) override;

    void onResolved(const DnsRecords &records, int status, const char *error) override;
    void onTimer(const Timer *timer) override;

    inline bool hasExtension(Extension) const noexcept override    { return false; }
    inline const char *mode() const override                       { return "daemon"; }
    inline const char *tlsFingerprint() const override             { return nullptr; }
    inline const char *tlsVersion() const override                 { return nullptr; }
    inline void onLine(char *line, size_t size) override           { parse(line, size); }

private:
    bool close();
    bool parseJob(const rapidjson::Value &params, int *code);
    bool write(const uv_buf_t &buf);
    int64_t getJobTemplate();
    int64_t send(size_t size);
    void handshake();
    void login();
    void onClose();
    void parse(char *line, size_t len);
    void parseResponse(int64_t id, const rapidjson::Value &result, const rapidjson::Value &error);
    void read(ssize_t nread, const uv_buf_t *buf);
    void reconnect();
    void resolve(const String &host);
    void setState(SocketState state);

    inline uv_stream_t *stream() const { return reinterpret_cast<uv_stream_t *>(m_socket); }
    inline SocketState state() const    { return m_state; }

    static void onClose(uv_handle_t *handle);
    static void onConnect(uv_connect_t *req, int status);
    static void onRead(uv_stream_t *stream, ssize_t nread, const uv_buf_t *buf);

    static inline EpicDaemonClient *getClient(void *data) { return m_storage.get(data); }

    const char *m_agent;
    LineReader m_reader;
    std::map<String, uint64_t> m_jobIds; // local job id -> server job_id
    std::shared_ptr<DnsRequest> m_dns;
    std::vector<char> m_sendBuf;
    String m_currentJobId;
    uint64_t m_blockDiff = 0;            // network block difficulty for randomx (informational)
    uint64_t m_expire    = 0;
    uintptr_t m_key      = 0;
    uv_tcp_t *m_socket   = nullptr;
    Timer *m_timer       = nullptr;

    static Storage<EpicDaemonClient> m_storage;
};


} /* namespace xmrig */


#endif // XMRIG_EPICDAEMONCLIENT_H
