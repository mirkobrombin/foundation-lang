#!/bin/sh

set -eu

script_dir=$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd)
repository_root=$(CDPATH='' cd -- "$script_dir/../../.." && pwd)
extension_root="$repository_root/tools/vscode"
version=$(sed -n 's/^[[:space:]]*"version": "\([^"]*\)",$/\1/p' "$extension_root/package.json")
test -n "$version"
output="$repository_root/build/foundation-lang-$version.vsix"
work_dir=$(mktemp -d "${TMPDIR:-/tmp}/foundation-vsix.XXXXXX")
trap 'rm -rf -- "$work_dir"' EXIT HUP INT TERM

platform=${FOUNDATION_VSCODE_PLATFORM:-}
if [ -z "$platform" ]; then
    case "$(uname -s):$(uname -m)" in
        Linux:x86_64) platform=linux-x64 ;;
        Linux:aarch64 | Linux:arm64) platform=linux-arm64 ;;
        Darwin:x86_64) platform=darwin-x64 ;;
        Darwin:arm64) platform=darwin-arm64 ;;
        *)
            printf '%s\n' "unsupported VS Code extension platform" >&2
            exit 1
            ;;
    esac
fi

case "$platform" in
    win32-*) server_name=foundation-ls.exe ;;
    *) server_name=foundation-ls ;;
esac

server=${FOUNDATION_LANGUAGE_SERVER:-}
if [ -z "$server" ]; then
    server="$repository_root/build/release/$server_name"
fi
if [ ! -f "$server" ]; then
    printf '%s\n' "foundation language server not found: $server" >&2
    exit 1
fi

mkdir -p "$repository_root/build"
mkdir -p "$work_dir/extension/bin/$platform"
mkdir -p "$work_dir/extension/src"
mkdir -p "$work_dir/extension/snippets"
mkdir -p "$work_dir/extension/syntaxes"

cp "$extension_root/vsix/Content_Types.xml" "$work_dir/[Content_Types].xml"
cp "$extension_root/vsix/extension.vsixmanifest" "$work_dir/extension.vsixmanifest"
cp "$extension_root/package.json" "$work_dir/extension/package.json"
cp "$extension_root/language-configuration.json" "$work_dir/extension/language-configuration.json"
cp "$extension_root/package-language-configuration.json" \
    "$work_dir/extension/package-language-configuration.json"
cp "$extension_root/README.md" "$work_dir/extension/README.md"
cp "$extension_root/src/extension.js" "$work_dir/extension/src/extension.js"
cp "$extension_root/src/languageClient.js" "$work_dir/extension/src/languageClient.js"
cp "$extension_root/snippets/foundation.json" "$work_dir/extension/snippets/foundation.json"
cp "$extension_root/syntaxes/foundation.tmLanguage.json" \
    "$work_dir/extension/syntaxes/foundation.tmLanguage.json"
cp "$extension_root/syntaxes/foundation-package.tmLanguage.json" \
    "$work_dir/extension/syntaxes/foundation-package.tmLanguage.json"
cp "$extension_root/syntaxes/foundation-lock.tmLanguage.json" \
    "$work_dir/extension/syntaxes/foundation-lock.tmLanguage.json"
cp "$server" "$work_dir/extension/bin/$platform/$server_name"
chmod 755 "$work_dir/extension/bin/$platform/$server_name"

if [ -e "$output" ]; then
    unlink "$output"
fi

(cd "$work_dir" && zip -q -r "$output" '[Content_Types].xml' extension.vsixmanifest extension)
printf '%s\n' "$output"
