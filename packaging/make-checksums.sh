#!/bin/sh
# 为 dist/ 里的发布产物生成 SHA-256 清单（ARCHITECTURE.md §9.1：不做签名，公布校验和）
# 用法：sh packaging/make-checksums.sh
set -eu

repo=$(cd "$(dirname "$0")/.." && pwd)
dist="$repo/dist"
sums="$dist/SHA256SUMS"

if [ ! -d "$dist" ]; then
    echo "dist/ 不存在，先跑对应的打包脚本" >&2
    exit 1
fi

cd "$dist"
files=$(ls -1 | grep -E '\.(deb|zip|tar\.gz|tar\.xz|exe|AppImage)$' || true)
if [ -z "$files" ]; then
    echo "dist/ 里没有可校验的产物" >&2
    exit 1
fi

printf '%s\n' "$files" | sort | xargs sha256sum >"$sums"
echo "==> $sums"
cat "$sums"
