#!/bin/sh
# Runs an installed SDK away from the machine that built it.
set -eu

root=$1
version=$2
suffix=${3:-}
if [ "$suffix" = .exe ]; then
    root=$(cygpath -u "$root")
    TMPDIR=$(cygpath -u "$TMPDIR")
    export TMPDIR
fi
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

for tool in foundationc foundationc-selfhost; do
    actual=$("$root/bin/$tool$suffix" version)
    test "$(printf '%s' "$actual" | tr -d '\r')" = "foundationc $version"
done

initialize='{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"processId":null,"rootUri":null,"capabilities":{}}}'
shutdown='{"jsonrpc":"2.0","id":2,"method":"shutdown"}'
exit_notification='{"jsonrpc":"2.0","method":"exit"}'
for message in "$initialize" "$shutdown" "$exit_notification"; do
    printf 'Content-Length: %d\r\n\r\n%s' "${#message}" "$message"
done | "$root/bin/foundation-ls$suffix" > "$work/server.out"
grep -q "\"serverInfo\":{\"name\":\"foundation-ls\",\"version\":\"$version\"}" "$work/server.out"

cd "$work"
printf 'fn main() i32 {\n    print("hello from foundation")\n    0\n}\n' > hello.fn
for backend in llvm c; do
    "$root/bin/foundationc$suffix" build hello.fn -o "hello-$backend$suffix" --backend "$backend"
    actual=$("./hello-$backend$suffix")
    test "$(printf '%s' "$actual" | tr -d '\r')" = "hello from foundation"
done
