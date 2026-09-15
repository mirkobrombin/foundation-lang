#!/bin/sh
# Runs an installed SDK away from the machine that built it.
set -eu

root=$1
version=$2
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

for tool in foundationc foundationc-selfhost; do
    test "$("$root/bin/$tool" version)" = "foundationc $version"
done

initialize='{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"processId":null,"rootUri":null,"capabilities":{}}}'
shutdown='{"jsonrpc":"2.0","id":2,"method":"shutdown"}'
exit_notification='{"jsonrpc":"2.0","method":"exit"}'
for message in "$initialize" "$shutdown" "$exit_notification"; do
    printf 'Content-Length: %d\r\n\r\n%s' "${#message}" "$message"
done | "$root/bin/foundation-ls" > "$work/server.out"
grep -q "\"serverInfo\":{\"name\":\"foundation-ls\",\"version\":\"$version\"}" "$work/server.out"

cd "$work"
printf 'fn main() i32 {\n    print("hello from foundation")\n    0\n}\n' > hello.fn
for backend in llvm c; do
    "$root/bin/foundationc" build hello.fn -o "hello-$backend" --backend "$backend"
    test "$("./hello-$backend")" = "hello from foundation"
done
