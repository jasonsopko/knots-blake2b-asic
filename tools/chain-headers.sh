#!/bin/sh
# Emit "<blockhash> <header-hex>" for every block since the BLAKE2b fork, which
# is what ic-verify-block reads in batch mode:
#
#   tools/chain-headers.sh | build/ic-verify-block -
#
# Wants a Knots node with the REST interface on. No credentials needed.
set -e
REST=${REST:-http://127.0.0.1:8332/rest}
FORK=${FORK:-961640}

exec python3 -c '
import json, signal, sys, urllib.request
# Let a downstream head(1) close the pipe without a traceback.
signal.signal(signal.SIGPIPE, signal.SIG_DFL)
rest, fork = sys.argv[1], int(sys.argv[2])
def get(p, raw=False):
    with urllib.request.urlopen(rest + p, timeout=120) as r:
        return r.read().decode() if raw else json.load(r)
tip = get("/chaininfo.json")["blocks"]
h = fork
while h <= tip:
    bh = get("/blockhashbyheight/%d.json" % h)["blockhash"]
    n = min(2000, tip - h + 1)
    hexes = get("/headers/%d/%s.hex" % (n, bh), raw=True).strip()
    meta  = get("/headers/%d/%s.json" % (n, bh))
    for i, m in enumerate(meta):
        print(m["hash"], hexes[i*328:(i+1)*328])
    h += n
' "$REST" "$FORK"
