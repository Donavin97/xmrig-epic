:: Example batch file for mining Epic Cash solo
::
:: Format:
::	xmrig.exe --coin EPIC -o <node address>:<stratum port> --daemon
::
:: Fields:
::	node address		The host name or IP address of your epic node. By default the
::				node's stratum API only accepts local connections, so this is
::				usually 127.0.0.1
::	stratum port		The stratum server port of your epic node, usually 3416.
::				Enable it in ~/.epic/main/epic-server.toml with:
::				    enable_stratum_server = true
::				You also need an epic wallet listening while mining
::				(epic-wallet listen) so the node can build block templates.
::	share difficulty	Optional: raise the share difficulty above the node's
::				minimum (4000) with --daemon-share-diff=N, e.g. 100000.
::				Does not affect block finding, just reduces the number
::				of shares reported to the node.
::
:: Mining solo is the best way to help the Epic Cash network be more decentralized!
:: But you will only get a payout when you find a block.
:: Epic Cash is multi-algo: blocks are mined with randomx (CPU), progpow (GPU) or
:: cuckoo. xmrig can only mine the randomx rounds and idles during the others.

cd /d "%~dp0"
xmrig.exe --coin EPIC -o 127.0.0.1:3416 --daemon
pause
