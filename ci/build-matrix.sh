#!/usr/bin/env bash
# CI 入口：在本机跑 ARCHITECTURE.md §8.4 的四组构建中的两组，并按需跑 sanitizer。
# Linux 上跑 linux-cli / linux-gui；MSYS2(MINGW64) 上跑 windows-cli / windows-gui。
# 两个平台各跑一次，合起来就是四组矩阵。
set -euo pipefail

cd "$(dirname "$0")/.."

case "$(uname -s)" in
    Linux*)       presets=(linux-cli linux-gui) ;;
    MINGW*|MSYS*) presets=(windows-cli windows-gui) ;;
    *)            echo "unsupported host: $(uname -s)" >&2; exit 1 ;;
esac

for preset in "${presets[@]}"; do
    echo "==> ${preset}"
    cmake --preset "${preset}"
    cmake --build --preset "${preset}"
    ctest --preset "${preset}"
done

if [ "$(uname -s)" = "Linux" ] && [ "${RH_SANITIZE_BUILD:-1}" = "1" ]; then
    echo "==> sanitize (address,undefined)"
    cmake -S . -B build/linux-sanitize -G Ninja \
        -DCMAKE_BUILD_TYPE=RelWithDebInfo \
        -DRH_GUI=OFF \
        -DRH_SANITIZE=address,undefined
    cmake --build build/linux-sanitize
    ctest --test-dir build/linux-sanitize --output-on-failure
fi
