#!/bin/bash
# 构建 GUI AppImage（ARCHITECTURE.md §9.1「Linux · AppImage」）
#
# 用法：bash packaging/make-appimage.sh
# 依赖：已配置好的 build/linux-gui、linuxdeploy、linuxdeploy-plugin-qt、
#       appimagetool（三者需在 PATH，或用环境变量 RH_LINUXDEPLOY /
#       RH_LINUXDEPLOY_QT / RH_APPIMAGETOOL 指定）
#
# 单一 GUI 入口；CLI 的免安装形态是 CLI tarball。AppImage 内的
# rom-hamster.AppImage --cli … 是应急入口，转发给同目录的 rom-hamster。
set -eu

repo=$(cd "$(dirname "$0")/.." && pwd)
build_dir=${BUILD_DIR:-$repo/build/linux-gui}
work=${WORK:-/tmp/rom-hamster-appimage}
dist="$repo/dist"

version=$(sed -n '/^project(/,/^)/p' "$repo/CMakeLists.txt" |
    sed -n 's/^ *VERSION *\([0-9][0-9.]*\).*/\1/p')
[ -n "$version" ] || { echo "无法从 CMakeLists.txt 解析版本号" >&2; exit 1; }

resolve_tool() {
    name=$1
    env_value=$2
    if [ -n "$env_value" ]; then
        echo "$env_value"
        return 0
    fi
    if command -v "$name" >/dev/null 2>&1; then
        command -v "$name"
        return 0
    fi
    return 1
}

linuxdeploy=$(resolve_tool linuxdeploy "${RH_LINUXDEPLOY:-}") || {
    echo "找不到 linuxdeploy。从 linuxdeploy 发布页下载后放进 PATH，或设置 RH_LINUXDEPLOY。" >&2
    exit 1
}
plugin_qt=$(resolve_tool linuxdeploy-plugin-qt "${RH_LINUXDEPLOY_QT:-}") || {
    echo "找不到 linuxdeploy-plugin-qt。设置 RH_LINUXDEPLOY_QT 或放进 PATH。" >&2
    exit 1
}
appimagetool=$(resolve_tool appimagetool "${RH_APPIMAGETOOL:-}") || {
    echo "找不到 appimagetool。设置 RH_APPIMAGETOOL 或放进 PATH。" >&2
    exit 1
}

appdir="$work/AppDir"
echo "==> 安装到 $appdir"
rm -rf "$work"
mkdir -p "$appdir/usr"
cmake --install "$build_dir" --prefix "$appdir/usr" --strip

# 便携形态不带 etc/ 与 .gitkeep（rh-path 读绝对路径 /etc/rom-hamster）
rm -rf "$appdir/usr/etc"
find "$appdir/usr" -name .gitkeep -delete

# linuxdeploy 要求 desktop 文件与图标位于 AppDir 根
cp "$repo/data/io.github.goddog312.RomHamster.desktop" "$appdir/rom-hamster.desktop"
cp "$repo/resources/icons/rom-hamster-256.png" "$appdir/rom-hamster.png"

export APPIMAGE_EXTRACT_AND_RUN=1
export ARCH=x86_64
# linuxdeploy 按固定名字（linuxdeploy-plugin-qt / appimagetool）在 PATH 里找伙伴，
# 而它们通常带 -x86_64.AppImage 后缀，这里建无后缀链接。
mkdir -p "$work/bin"
ln -sf "$linuxdeploy" "$work/bin/linuxdeploy"
ln -sf "$plugin_qt" "$work/bin/linuxdeploy-plugin-qt"
ln -sf "$appimagetool" "$work/bin/appimagetool"
export PATH="$work/bin:$PATH"

# plugin-qt 需要知道 qmake；系统里是 qmake6
if ! command -v qmake >/dev/null 2>&1 && command -v qmake6 >/dev/null 2>&1; then
    mkdir -p "$work/bin"
    ln -sf "$(command -v qmake6)" "$work/bin/qmake"
    export PATH="$work/bin:$PATH"
fi
export LINUXDEPLOY_PLUGIN_QT="$plugin_qt"

echo "==> linuxdeploy"
(cd "$work" && "$linuxdeploy" --appdir "$appdir" \
    --desktop-file "$appdir/rom-hamster.desktop" \
    --icon-file "$appdir/rom-hamster.png" \
    --executable "$appdir/usr/bin/rom-hamster-gui" \
    --plugin qt)

# appimagetool 默认会去 GitHub 下载 type2 runtime；网络受限时用本地副本。
runtime=${RH_APPIMAGE_RUNTIME:-}
if [ -z "$runtime" ] && [ -f "$(dirname "$appimagetool")/runtime-x86_64" ]; then
    runtime="$(dirname "$appimagetool")/runtime-x86_64"
fi

echo "==> appimagetool"
# appimagetool 的 GOption 在非 ASCII 路径上会解析失败（本仓库路径含中文），
# 因此 runtime 与输出都放 ASCII 的临时目录，最后再拷到 dist/。
runtime_copy="$work/runtime-x86_64"
if [ -n "$runtime" ]; then
    cp "$runtime" "$runtime_copy"
    "$appimagetool" --runtime-file "$runtime_copy" "$appdir" "$work/$version.AppImage"
else
    "$appimagetool" "$appdir" "$work/$version.AppImage"
fi
built="$work/$version.AppImage"
[ -f "$built" ] || { echo "appimagetool 没有产出 AppImage" >&2; exit 1; }

mkdir -p "$dist"
out="$dist/rom-hamster-$version-linux-x86_64.AppImage"
cp "$built" "$out"
chmod +x "$out"

echo "==> 自检"
if (cd /tmp && APPIMAGE_EXTRACT_AND_RUN=1 "$out" --version >/dev/null 2>&1); then
    echo "    --version OK"
else
    echo "自检失败：AppImage 无法运行 --version" >&2
    exit 1
fi

echo "==> 产物："
ls -l "$out"
