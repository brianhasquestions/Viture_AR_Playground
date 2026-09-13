#!/usr/bin/env bash
# Offline regression for 04_sealed_drawing: runs the encode / decode /
# locked / not-found paths on the sample frames in test/data with a fixed
# glasses hash, so matcher and crypto changes can be checked without the
# headset. Exit status is non-zero on any unexpected outcome.
set -u
cd "$(dirname "$0")/.."
make -s >/dev/null || { echo "build failed"; exit 1; }

ROOT=$(cd ../.. && pwd)
export LD_LIBRARY_PATH="$ROOT/sdk/viture_x86_64/x86_64:${LD_LIBRARY_PATH:-}"
BIN=./bin/sealed_drawing
HASH_A=aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa
HASH_B=bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
VAULT="$WORK/test.vault"
CAPS="$WORK/captures"
fail=0

expect() {   # expect <label> <pattern> <command...>
    local label=$1 pattern=$2; shift 2
    local out
    out=$("$@" 2>&1)
    if grep -qE "$pattern" <<<"$out"; then
        echo "ok   $label"
    else
        echo "FAIL $label"; echo "$out" | sed 's/^/     /'; fail=1
    fi
}

expect "encode doll_a"          'sealed\] message sealed' \
    $BIN watch --image test/data/doll_a.jpg --hash $HASH_A --vault "$VAULT" --captures "$CAPS" --message "test message"
expect "decode doll_b matches"  'unlocked\] object matched' \
    $BIN watch --image test/data/doll_b.jpg --hash $HASH_A --vault "$VAULT" --captures "$CAPS"
expect "decode doll_c matches"  'unlocked\] object matched' \
    $BIN watch --image test/data/doll_c.jpg --hash $HASH_A --vault "$VAULT" --captures "$CAPS"
expect "room is not found"      'no match\]' \
    $BIN watch --image test/data/room.jpg --hash $HASH_A --vault "$VAULT" --captures "$CAPS"
expect "drawing is not found"   'no match\]' \
    $BIN watch --image test/data/drawing.jpg --hash $HASH_A --vault "$VAULT" --captures "$CAPS"
expect "wrong glasses locked"   'locked\] object matched' \
    $BIN watch --image test/data/doll_b.jpg --hash $HASH_B --vault "$VAULT" --captures "$CAPS"
expect "list shows one record"  '1 record$' \
    $BIN list --vault "$VAULT"
expect "forget removes it"      'forgot record 0' \
    $BIN forget 0 --vault "$VAULT"
expect "decode after forget"    'no match\]' \
    $BIN watch --image test/data/doll_b.jpg --hash $HASH_A --vault "$VAULT" --captures "$CAPS"
expect "identity prints key"    'public key:' \
    $BIN identity --hash $HASH_A

exit $fail
