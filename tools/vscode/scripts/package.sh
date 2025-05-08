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

mkdir -p "$repository_root/build"
mkdir -p "$work_dir/extension/src"
mkdir -p "$work_dir/extension/snippets"
mkdir -p "$work_dir/extension/syntaxes"

cp "$extension_root/vsix/Content_Types.xml" "$work_dir/[Content_Types].xml"
cp "$extension_root/vsix/extension.vsixmanifest" "$work_dir/extension.vsixmanifest"
cp "$extension_root/package.json" "$work_dir/extension/package.json"
cp "$extension_root/language-configuration.json" "$work_dir/extension/language-configuration.json"
cp "$extension_root/README.md" "$work_dir/extension/README.md"
cp "$extension_root/src/completions.js" "$work_dir/extension/src/completions.js"
cp "$extension_root/src/extension.js" "$work_dir/extension/src/extension.js"
cp "$extension_root/snippets/foundation.json" "$work_dir/extension/snippets/foundation.json"
cp "$extension_root/syntaxes/foundation.tmLanguage.json" \
    "$work_dir/extension/syntaxes/foundation.tmLanguage.json"

if [ -e "$output" ]; then
    unlink "$output"
fi

(cd "$work_dir" && zip -q -r "$output" '[Content_Types].xml' extension.vsixmanifest extension)
printf '%s\n' "$output"
