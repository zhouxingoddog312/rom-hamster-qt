#!/bin/sh
# 组装 CLI 免安装 tarball（ARCHITECTURE.md §9.1「Linux · CLI tarball」）
# 用法：sh packaging/make-tarball.sh
# 依赖：cmake、ninja、核心五库开发包；产物放到 dist/
# 环境变量：BUILD_DIR 覆盖构建树，STAGE 覆盖暂存目录
set -eu

repo=$(cd "$(dirname "$0")/.." && pwd)
build_dir=${BUILD_DIR:-$repo/build/linux-cli}
stage=${STAGE:-$repo/build/stage-tarball}
dist="$repo/dist"

version=$(sed -n '/^project(/,/^)/p' "$repo/CMakeLists.txt" |
    sed -n 's/^ *VERSION *\([0-9][0-9.]*\).*/\1/p')
if [ -z "$version" ]; then
    echo "无法从 CMakeLists.txt 解析版本号" >&2
    exit 1
fi

name="rom-hamster-$version-linux-x86_64"
top="$stage/$name"

if [ ! -f "$build_dir/CMakeCache.txt" ]; then
    echo "==> 配置构建树：$build_dir"
    cmake -S "$repo" -B "$build_dir" -G Ninja \
        -DCMAKE_BUILD_TYPE=RelWithDebInfo -DRH_GUI=OFF
fi

echo "==> 构建"
cmake --build "$build_dir" --parallel

echo "==> 安装到 $top"
rm -rf "$stage"
mkdir -p "$top"
cmake --install "$build_dir" --prefix "$top" --strip

# 便携包不带 etc/：rh-path 读的是绝对路径 /etc/rom-hamster，包内这份永远不会被读到
rm -rf "$top/etc"
# .gitkeep 只是仓库占位，不该进安装树
find "$top" -name .gitkeep -delete
install -m 644 "$repo/LICENSE" "$top/LICENSE"
install -m 644 "$repo/README.md" "$top/README.md"

echo "==> 自检（不带 LD_LIBRARY_PATH，只能靠 \$ORIGIN rpath）"
if ! (cd /tmp && env -u LD_LIBRARY_PATH "$top/bin/rom-hamster" --version >/dev/null); then
    echo "自检失败：bin/rom-hamster 无法仅靠包内库启动" >&2
    exit 1
fi
if ! ldd "$top/bin/rom-hamster" | grep -q "libromham.so.1 => $top"; then
    echo "自检失败：运行时没有命中包内 libromham" >&2
    exit 1
fi

mkdir -p "$dist"
tarball="$dist/$name.tar.gz"
rm -f "$tarball"
echo "==> 打包 $tarball"
tar -C "$stage" --sort=name --owner=0 --group=0 --numeric-owner \
    -cf - "$name" | gzip -9 -n >"$tarball"

echo "==> 产物："
ls -l "$tarball"
