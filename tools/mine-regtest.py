#!/usr/bin/env python3
"""Mine a block of a chosen proof-of-work profile on a regtest chain.

Proves docs/profile-1.md end to end: the buffer layouts in that document
produce blocks a Bitcoin Knots node accepts. The proof of work itself comes
from build/ic-grind, which uses this repository's library, so what is being
tested is what is published rather than a second implementation of it.

Bring a node up with the fork scheduled, then run this:

    bitcoind -regtest -datadir=DIR -testactivationheight=blake2b@8 -daemon
    bitcoin-cli -regtest -datadir=DIR generatetodescriptor 7 "raw(51)#8lvh9jxk"
    tools/mine-regtest.py --datadir DIR --profile 1

Profiles 0 to 3 all work; the node only reserves the top two bits of m_flags.
"""
import argparse, hashlib, json, struct, subprocess, sys, os

def sha256d(b):
    return hashlib.sha256(hashlib.sha256(b).digest()).digest()

def varint(n):
    if n < 0xfd:
        return bytes([n])
    if n <= 0xffff:
        return b'\xfd' + struct.pack('<H', n)
    return b'\xfe' + struct.pack('<I', n)

def script_num_push(n):
    """CScript() << n, which is what BIP34 compares the scriptSig against.
    Heights 1 to 16 are a single OP_N opcode, not a pushdata."""
    if n == 0:
        return b'\x00'
    if 1 <= n <= 16:
        return bytes([0x50 + n])
    b = b''
    v = n
    while v:
        b += bytes([v & 0xff])
        v >>= 8
    if b[-1] & 0x80:
        b += b'\x00'
    return bytes([len(b)]) + b

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--datadir", required=True)
    ap.add_argument("--profile", type=int, default=1, choices=(0, 1, 2, 3))
    ap.add_argument("--cli", default="bitcoin-cli")
    ap.add_argument("--grind", default=os.path.join(os.path.dirname(__file__),
                                                    "..", "build", "ic-grind"))
    ap.add_argument("--rpcargs", default="", help="extra bitcoin-cli arguments")
    a = ap.parse_args()

    cli = [a.cli, "-regtest", "-datadir=" + a.datadir] + a.rpcargs.split()

    def rpc(*args):
        r = subprocess.run(cli + list(args), capture_output=True, text=True)
        if r.returncode:
            raise SystemExit("bitcoin-cli failed: " + r.stderr.strip())
        return r.stdout.strip()

    tpl = json.loads(rpc("getblocktemplate", '{"rules":["segwit","blake2b"]}'))
    height = tpl["height"]

    # Coinbase: BIP34 height, the payout, and the witness commitment.
    script_sig = script_num_push(height) + b'\x00'
    vin = (b'\x01' + b'\x00' * 32 + b'\xff\xff\xff\xff'
           + varint(len(script_sig)) + script_sig + b'\xff\xff\xff\xff')
    pay = struct.pack('<Q', tpl["coinbasevalue"]) + varint(1) + b'\x51'
    commit_script = bytes.fromhex(tpl["default_witness_commitment"])
    commit = struct.pack('<Q', 0) + varint(len(commit_script)) + commit_script
    vout = b'\x02' + pay + commit

    base = struct.pack('<i', 2) + vin + vout + struct.pack('<I', 0)
    witness = b'\x01\x20' + b'\x00' * 32
    full = (struct.pack('<i', 2) + b'\x00\x01' + vin + vout + witness
            + struct.pack('<I', 0))
    merkle = sha256d(base)                      # one transaction, so the txid

    # The 164 byte v2 header. m_flags carries the profile in its low two bits.
    hdr = b''.join([
        struct.pack('<I', tpl["version"]),
        bytes.fromhex(tpl["previousblockhash"])[::-1],
        merkle,
        struct.pack('<I', tpl["curtime"]),
        struct.pack('<I', int(tpl["bits"], 16)),
        struct.pack('<I', 0),                   # nNonce, ground below
        struct.pack('<I', 0),                   # m_nonce2, ground below
        struct.pack('<I', 0),                   # m_nonce3
        b'\x00' * 16,                           # m_extranonce
        struct.pack('<I', 0),                   # m_time_offset
        struct.pack('<H', 1),                   # m_txcount
        bytes([a.profile]),                     # m_flags
        bytes([0]),                             # m_xor_key_mask_clear_bits
        b'\x00' * 16,                           # m_xor_key
        struct.pack('<I', height),              # m_height
        b'\x00' * 32,                           # m_mm_rhs
    ])
    assert len(hdr) == 164, len(hdr)

    g = subprocess.run([a.grind, hdr.hex(), tpl["target"], "20000000"],
                       capture_output=True, text=True)
    sys.stderr.write(g.stderr)
    if g.returncode:
        raise SystemExit("ic-grind found no nonce")
    solved = bytes.fromhex(g.stdout.strip())

    block = solved + varint(1) + full
    out = rpc("submitblock", block.hex())
    if out:
        raise SystemExit("submitblock rejected: " + out)
    print("height %d accepted, profile %d" % (height, a.profile))

if __name__ == "__main__":
    main()
