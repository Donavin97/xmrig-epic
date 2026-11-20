# XMRig

[![Github All Releases](https://img.shields.io/github/downloads/xmrig/xmrig/total.svg)](https://github.com/xmrig/xmrig/releases)
[![GitHub release](https://img.shields.io/github/release/xmrig/xmrig/all.svg)](https://github.com/xmrig/xmrig/releases)
[![GitHub Release Date](https://img.shields.io/github/release-date/xmrig/xmrig.svg)](https://github.com/xmrig/xmrig/releases)
[![GitHub license](https://img.shields.io/github/license/xmrig/xmrig.svg)](https://github.com/xmrig/xmrig/blob/master/LICENSE)
[![GitHub stars](https://img.shields.io/github/stars/xmrig/xmrig.svg)](https://github.com/xmrig/xmrig/stargazers)
[![GitHub forks](https://img.shields.io/github/forks/xmrig/xmrig.svg)](https://github.com/xmrig/xmrig/network)

XMRig is a high performance, open source, cross platform RandomX, KawPow, CryptoNight and [GhostRider](https://github.com/xmrig/xmrig/tree/master/src/crypto/ghostrider#readme) unified CPU/GPU miner and [RandomX benchmark](https://xmrig.com/benchmark). Official binaries are available for Windows, Linux, macOS and FreeBSD.

## Mining backends
- **CPU** (x86/x64/ARMv7/ARMv8/RISC-V)
- **OpenCL** for AMD GPUs.
- **CUDA** for NVIDIA GPUs via external [CUDA plugin](https://github.com/xmrig/xmrig-cuda).

## Download
* **[Binary releases](https://github.com/epicminepool/xmrig-epic/releases)**
* **[Build from source](https://xmrig.com/docs/miner/build)**

Use `git clone --recurse-submodules https://github.com/epicminepool/xmrig-epic.git` to correctly import the uint256_t submodule required for xmrig-epic.

## Usage
The preferred way to configure the miner is the [JSON config file](https://xmrig.com/docs/miner/config) as it is more flexible and human friendly. The [command line interface](https://xmrig.com/docs/miner/command-line-options) does not cover all features, such as mining profiles for different algorithms. Important options can be changed during runtime without miner restart by editing the config file or executing [API](https://xmrig.com/docs/miner/api) calls.

* **[Wizard](https://xmrig.com/wizard)** helps you create initial configuration for the miner.
* **[Workers](http://workers.xmrig.info)** helps manage your miners via HTTP API.

### PCoin (PCN) solo mining in daemon mode

This fork supports solo mining PCoin against a `pcoind` daemon (a Bitcoin Core v29.4 fork, RandomX PoW). Build with `rx/pcoin` enabled and run with the `--daemon` flag.

PCoin's RPC is Bitcoin Core style JSON-RPC on port **9443** (P2P port **9444**). Authentication is HTTP Basic, and the credentials are taken from the URL userinfo: `http://RPUSER:RPCPASSWORD@host:9443`. The credentials are stripped from the logged/stored URL and are never printed.

Example JSON pool section:

```json
{
    "pools": [
        {
            "url": "rpcuser:rpcpassword@10.210.2.46:9443",
            "user": "pc1YOURPCNBECH32ADDRESS",
            "pass": "x",
            "daemon": true,
            "daemon-poll-interval": 2000,
            "coin": "PCoin",
            "algo": "rx/pcoin"
        }
    ]
}
```

Or via command line:

```sh
xmrig -a rx/pcoin -u pc1YOURADDRESS -o rpcuser:rpcpassword@10.210.2.46:9443 --daemon
```

Notes:
* Algorithm is `rx/pcoin` (randomx/pcoin), CPU-only.
* Target parsing uses a 32-byte big-endian integer target; the full 256-bit target is verified before a block is handed to `submitblock` (the in-miner share check only compares the top 64 bits).
* The RandomX key is fixed (`PCoin/RandomX/v1`, 16 bytes ASCII) and never rotates.
* The payout address (`user`) must be a PCoin bech32 address with HRP `pc` (segwit v0 uses bech32, v1+ uses bech32m); the coinbase it pays goes to this address.
* PCoin's RPC is Bitcoin Core style at path `/` (not Monero's `/json_rpc`); the `"coin": "PCoin"` + `"daemon": true` combination selects this mode. `getblocktemplate` is sent with `{"rules":["segwit"]}` and the node answers only while it is connected to the P2P network.
* The miner assembles full blocks client-side: coinbase transaction with BIP34 height push, minimal-witness (`[32 zero bytes]`), the `default_witness_commitment` from the template appended verbatim as a 0-value OP_RETURN output, the merkle root over `[coinbase txid, template transactions]`, and the 80-byte RandomX header (`nonce` at offset 76). Assembled blocks are verified against the template's `default_witness_commitment` formula and submitted via `submitblock`.
* Solo mining PCoin is live: xmrig polls `getblocktemplate`, assembles and hashes blocks, and submits winning blocks via `submitblock`. Reward maturity is 100 blocks; solo miners find blocks infrequently (difficulty ~0.08 at the time of writing, so expect shares roughly every few minutes per thread rather than daily).
* The daemon only answers `getblocktemplate` while it is connected to the P2P network — if xmrig logs template errors, check `getconnectioncount` (and the node's `-prune`/connectivity) first.

## Mining Epic Cash

This fork mines the Epic Cash `randomx` algorithm (`rx/epic`) on CPU. Note that Epic Cash is
multi-algo (randomx / progpow / cuckoo): this miner only works during the `randomx` rounds and
idles when the pool assigns progpow or cuckoo rounds.

### Pool mining (epicmine.io or any compatible stratum)

```
xmrig --coin EPIC -o de.epicmine.io:3334 --tls -u MINING_ALIAS.WORKER_NAME -p m=pool
```

See `scripts/epic_cash_example.cmd`.

### Solo mining to a local epic node

Connect to the stratum server of your own `epic` node (enable it in `~/.epic/main/epic-server.toml`
with `enable_stratum_server = true`, default address `127.0.0.1:3416`) and keep an epic wallet
listening so the node can build block templates:

```
xmrig --coin EPIC -o 127.0.0.1:3416 --daemon
```

JSON config equivalent:

```json
{
  "pools": [
    {
      "coin": "EPIC",
      "url": "127.0.0.1:3416",
      "daemon": true
    }
  ]
}
```

See `scripts/epic_cash_solo_example.cmd`.

You can raise the share difficulty above the node's minimum (4000) with
`daemon-share-diff`. This only moves the local submission threshold — the node
still accepts any share at or above its minimum, and block-finding is unaffected
(every hash is always checked against the full network block difficulty):

```
xmrig --coin EPIC -o 127.0.0.1:3416 --daemon --daemon-share-diff=100000
```

Or mine directly at the network block difficulty with `daemon-share-diff:
"block"`. In this mode the miner submits nothing but actual block solutions
(pure solo mining — expect silence, potentially for days, until you find a
block; the hashrate display still works):

```
xmrig --coin EPIC -o 127.0.0.1:3416 --daemon --daemon-share-diff=block
```

```json
{
  "pools": [
    {
      "coin": "EPIC",
      "url": "127.0.0.1:3416",
      "daemon": true,
      "daemon-share-diff": "block"
    }
  ]
}
```

With share diff raised, the `results` table (`results` command) reports
meaningful difficulties/effort instead of thousands of diff-1 style shares.

## Donations
* Default donation 1% (1 minute in 100 minutes) can be increased via option `donate-level` or disabled in source code.
* XMR: `48edfHu7V9Z84YzzMa6fUueoELZ9ZRXq9VetWzYGzKt52XU5xvqgzYnDK9URnRoJMk1j8nLwEVsaSWJ4fhdUyZijBGUicoD`
* EPIC: `https://donate.epicmine.io/`

## Developers
* **[xmrig](https://github.com/xmrig)**
* **[sech1](https://github.com/SChernykh)**

## Contacts
* support@xmrig.com
* [reddit](https://www.reddit.com/user/XMRig/)
* [twitter](https://twitter.com/xmrig_dev)
