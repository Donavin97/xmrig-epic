# XMRig

[![Github All Releases](https://img.shields.io/github/downloads/xmrig/xmrig/total.svg)](https://github.com/xmrig/xmrig/releases)
[![GitHub release](https://img.shields.io/github/release/xmrig/xmrig/all.svg)](https://github.com/xmrig/xmrig/releases)
[![GitHub Release Date](https://img.shields.io/github/release-date/xmrig/xmrig.svg)](https://github.com/xmrig/xmrig/releases)
[![GitHub license](https://img.shields.io/github/license/xmrig/xmrig.svg)](https://github.com/xmrig/xmrig/blob/master/LICENSE)
[![GitHub stars](https://img.shields.io/github/stars/xmrig/xmrig.svg)](https://github.com/xmrig/xmrig/stargazers)
[![GitHub forks](https://img.shields.io/github/forks/xmrig/xmrig.svg)](https://github.com/xmrig/xmrig/network)

XMRig is a high performance, open source, cross platform RandomX, KawPow, CryptoNight and [GhostRider](https://github.com/xmrig/xmrig/tree/master/src/crypto/ghostrider#readme) unified CPU/GPU miner and [RandomX benchmark](https://xmrig.com/benchmark). Official binaries are available for Windows, Linux, macOS and FreeBSD.

## Mining backends
- **CPU** (x86/x64/ARMv7/ARMv8)
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
