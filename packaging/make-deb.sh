#!/bin/sh
# 构建 Linux deb（M1 骨架：rom-hamster + rom-hamster-data）
# 用法：sh packaging/make-deb.sh
# 依赖：dpkg-dev、debhelper、cmake、ninja、核心五库开发包
set -eu

repo=$(cd "$(dirname "$0")/.." && pwd)
work=${WORK:-/tmp/rom-hamster-deb}
dist="$repo/dist"

# 与旧 GTK 版同名的包会互相覆盖：同名 + 同版本，装过就卸不掉（依赖互锁）
if dpkg -s rom-hamster-gui 2>/dev/null | grep -q 'rom-hamster-gtk'; then
    echo "警告：系统里装的是旧 GTK 版 rom-hamster 包（Homepage 为 rom-hamster-gtk）。" >&2
    echo "      请先 sudo dpkg --purge rom-hamster-gui，再安装本次产物。" >&2
fi

echo "==> 准备构建树：$work"
rm -rf "$work"
mkdir -p "$work"

tar -C "$repo" \
    --exclude=./.git \
    --exclude=./build \
    --exclude=./dist \
    --exclude='./20[0-9][0-9]-[0-9][0-9]-[0-9][0-9]' \
    -cf - . | tar -x -C "$work"

cp -r "$repo/packaging/debian" "$work/debian"
# Windows 文件系统会给所有文件带执行位，debhelper 会把 .install 当可执行配置
find "$work/debian" -type f -exec chmod a-x {} +
chmod +x "$work/debian/rules"

echo "==> dpkg-buildpackage"
(cd "$work" && dpkg-buildpackage -us -uc -b)

mkdir -p "$dist"
cp "$work"/../rom-hamster_*.deb "$work"/../rom-hamster-data_*.deb "$dist"/
echo "==> 产物："
ls -1 "$dist"
