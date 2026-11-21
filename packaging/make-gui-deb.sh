#!/bin/bash
# 构建自带 Qt 6 运行库的 GUI deb（ARCHITECTURE.md §9.1）
#
# 用法：bash packaging/make-gui-deb.sh
# 依赖：cmake/ninja、已配置好的 build/linux-gui、dpkg-deb、dpkg-shlibdeps、
#       qmake6（用于定位构建时使用的 Qt 库与插件；Qt 官方安装包里叫 qmake，
#       可用 RH_QMAKE 显式指定）
#
# 环境变量：BUILD_DIR 覆盖构建树，WORK 覆盖临时目录，RH_QMAKE 指定 qmake。
#
# 布局：
#   /usr/bin/rom-hamster-gui
#   /usr/bin/qt.conf
#   /usr/lib/rom-hamster/qt6/            Qt 6 库（不与发行版 Qt 混放）
#   /usr/lib/rom-hamster/qt6/plugins/    平台/图片/TLS 插件
#   /usr/share/{applications,icons,metainfo}/
# 核心库与出厂数据由 rom-hamster / rom-hamster-data 包提供，本包只 Depends 它们。
set -eu

repo=$(cd "$(dirname "$0")/.." && pwd)
build_dir=${BUILD_DIR:-$repo/build/linux-gui}
work=${WORK:-/tmp/rom-hamster-gui-deb}
stage="$work/root"
dist="$repo/dist"

version=$(sed -n '/^project(/,/^)/p' "$repo/CMakeLists.txt" |
    sed -n 's/^ *VERSION *\([0-9][0-9.]*\).*/\1/p')
[ -n "$version" ] || { echo "无法从 CMakeLists.txt 解析版本号" >&2; exit 1; }
arch=$(dpkg --print-architecture)

qmake=${RH_QMAKE:-}
if [ -z "$qmake" ]; then
    qmake=$(command -v qmake6 || command -v qmake || true)
fi
[ -n "$qmake" ] || {
    echo "找不到 qmake6/qmake。装上 Qt 6 开发工具，或用 RH_QMAKE 指定。" >&2
    exit 1
}

qt_libs=$("$qmake" -query QT_INSTALL_LIBS)
qt_plugins=$("$qmake" -query QT_INSTALL_PLUGINS)
[ -d "$qt_libs" ] || { echo "$qmake 找不到 Qt 库目录" >&2; exit 1; }

echo "==> 安装 GUI 到 $stage"
rm -rf "$work"
mkdir -p "$stage/usr"
cmake --install "$build_dir" --prefix "$stage/usr" --strip

# CLI、核心库、出厂数据、man page 属于 rom-hamster(-data)，不要重复打包
rm -f "$stage/usr/bin/rom-hamster"
rm -rf "$stage/usr/lib/x86_64-linux-gnu/rom-hamster" \
       "$stage/usr/lib/rom-hamster" \
       "$stage/usr/share/rom-hamster" "$stage/usr/share/man" \
       "$stage/etc" "$stage/usr/etc"
find "$stage/usr" -name .gitkeep -delete

qt_dir="$stage/usr/lib/rom-hamster/qt6"
mkdir -p "$qt_dir/plugins"

echo "==> 复制 Qt 插件"
plugin_list="platforms/libqxcb.so
platforms/libqwayland-generic.so
platforms/libqwayland-egl.so
platforms/libqoffscreen.so
platforms/libqminimal.so
imageformats/libqjpeg.so
imageformats/libqgif.so
imageformats/libqico.so
tls/libqopensslbackend.so
tls/libqcertonlybackend.so
xcbglintegrations/libqxcb-glx-integration.so
platforminputcontexts/libcomposeplatforminputcontextplugin.so"
elfs="$stage/usr/bin/rom-hamster-gui"
printf '%s\n' "$plugin_list" | while IFS= read -r p; do
    if [ -f "$qt_plugins/$p" ]; then
        mkdir -p "$qt_dir/plugins/$(dirname "$p")"
        cp -L "$qt_plugins/$p" "$qt_dir/plugins/$p"
    fi
done
elfs="$elfs $(find "$qt_dir/plugins" -type f -name '*.so*' | tr '\n' ' ')"

# 收集位于构建时 Qt 目录里的传递依赖（Qt6XcbQpa 等），非 Qt 库留给发行版
echo "==> 收集 Qt 库"
todo="$work/todo.list"
done_list="$work/done.list"
: >"$done_list"
printf '%s\n' $elfs >"$todo"
while [ -s "$todo" ]; do
    next="$work/next.list"
    : >"$next"
    while IFS= read -r elf; do
        [ -f "$elf" ] || continue
        grep -qxF "$elf" "$done_list" && continue
        printf '%s\n' "$elf" >>"$done_list"
        ldd "$elf" 2>/dev/null | sed -n 's/.*=> \(\/[^ ]*\).*/\1/p' |
            while IFS= read -r dep; do
                case "$dep" in
                "$qt_libs"/libQt6*.so.*)
                    name=$(basename "$dep")
                    if [ ! -f "$qt_dir/$name" ]; then
                        cp -L "$dep" "$qt_dir/$name"
                        printf '%s\n' "$qt_dir/$name" >>"$next"
                    fi
                    ;;
                esac
            done
    done <"$todo"
    [ -s "$next" ] || break
    cp "$next" "$todo"
done
# 复制进来的库自身也可能依赖别的 Qt 库（Qt6XcbQpa 等）：再扫几轮补齐
for _ in 1 2 3; do
    before=$(ls "$qt_dir"/libQt6*.so.* 2>/dev/null | wc -l)
    for lib in "$qt_dir"/libQt6*.so.*; do
        [ -f "$lib" ] || continue
        ldd "$lib" 2>/dev/null | sed -n 's/.*=> \(\/[^ ]*\).*/\1/p' |
            while IFS= read -r dep; do
                case "$dep" in
                "$qt_libs"/libQt6*.so.*)
                    name=$(basename "$dep")
                    [ -f "$qt_dir/$name" ] || cp -L "$dep" "$qt_dir/$name"
                    ;;
                esac
            done
    done
    after=$(ls "$qt_dir"/libQt6*.so.* 2>/dev/null | wc -l)
    [ "$before" -eq "$after" ] && break
done
strip --strip-unneeded "$qt_dir"/libQt6*.so.* 2>/dev/null || true

# qt.conf：让 Qt 只从包里找插件，不碰发行版 Qt
cat >"$stage/usr/bin/qt.conf" <<EOF
[Paths]
Prefix = /usr/lib/rom-hamster/qt6
Plugins = plugins
Libraries = .
EOF

# GUI 二进制的 rpath 是 $ORIGIN/../lib/rom-hamster，而 rom-hamster 包按 Debian
# 惯例把核心库装在 /usr/lib/<multiarch>/rom-hamster；放一个指向它的符号链接，
# 免得为了打包重新配置一套 multiarch 的构建树。
mkdir -p "$stage/usr/lib/rom-hamster"
# 用相对链接，避免 lintian 的 absolute-symlink-in-top-level-folder。
ln -sf "../x86_64-linux-gnu/rom-hamster/libromham.so.1" \
    "$stage/usr/lib/rom-hamster/libromham.so.1"

# Debian 文档：copyright + changelog，缺一不可（lintian 报 error）。
doc_dir="$stage/usr/share/doc/rom-hamster-gui"
mkdir -p "$doc_dir"
cp "$repo/LICENSE" "$doc_dir/copyright"
chmod 644 "$doc_dir/copyright"
{
    printf 'rom-hamster-gui (%s) unstable; urgency=medium\n\n' "$version"
    printf '  * Release %s.\n\n' "$version"
    printf ' -- Zhou Xin <zhouxin312@qq.com>  %s\n' "$(date -R)"
} | gzip -9n >"$doc_dir/changelog.gz"

# qt.conf 不是可执行程序，GUI 主程序也不单独提供手册页：按已知误报覆盖。
override_dir="$stage/usr/share/lintian/overrides"
mkdir -p "$override_dir"
cat >"$override_dir/rom-hamster-gui" <<'EOF'
rom-hamster-gui: no-manual-page [usr/bin/qt.conf]
rom-hamster-gui: no-manual-page [usr/bin/rom-hamster-gui]
EOF

echo "==> 计算依赖"
mkdir -p "$work/debian"
cat >"$work/debian/control" <<EOF
Source: rom-hamster-gui
Section: utils
Priority: optional
Maintainer: Zhou Xin <zhouxin312@qq.com>
Standards-Version: 4.6.2
Homepage: https://gitee.com/goddog312/rom-hamster-qt

Package: rom-hamster-gui
Architecture: any
Depends: \${shlibs:Depends}
Description: ROM library manager (Qt 6 GUI)
EOF
shlibs=""
if command -v dpkg-shlibdeps >/dev/null 2>&1; then
    # 核心库来自 rom-hamster 包，不在本包内；放到临时目录让 shlibdeps 能找到，
    # 用 --ignore-missing-info 避免它要求一个只对内部库适用的 shlibs 条目。
    core_stub="$work/corelib"
    mkdir -p "$core_stub"
    cp -L "$build_dir"/libromham.so.1* "$core_stub/" 2>/dev/null || true
    # 用 find 生成 -e 列表，避免依赖 shell 通配符展开
    elf_args=$(find "$stage/usr/bin" "$qt_dir" -type f \
        \( -name 'rom-hamster-gui' -o -name '*.so*' \) -printf '-e%p\n')
    shlibs=$(cd "$work" && dpkg-shlibdeps -O --ignore-missing-info -l"$core_stub" \
        $elf_args 2>/dev/null | sed -n 's/^shlibs:Depends=//p' || true)
    if [ -n "$shlibs" ]; then
        # 包内自带的 Qt 库不需要发行版 Qt 包；shlibdeps 仍会把它们当外部依赖，
        # 这里按包名过滤掉（libqt6*/qt6-base-private-abi）。
        shlibs=$(printf '%s\n' "$shlibs" | tr ',' '\n' |
            sed 's/^ *//;s/ *$//' |
            grep -v -E '^libqt6|^qt6-' |
            grep -v '^$' | paste -sd, - | sed 's/,/, /g')
        [ -n "$shlibs" ] || shlibs="libc6, libstdc++6, libgcc-s1"
    fi
fi
[ -n "$shlibs" ] || shlibs="libc6, libstdc++6, libgcc-s1"

installed_size=$(du -sk "$stage" | cut -f1)
mkdir -p "$stage/DEBIAN"
cat >"$stage/DEBIAN/control" <<EOF
Package: rom-hamster-gui
Version: $version
Section: utils
Priority: optional
Architecture: $arch
Installed-Size: $installed_size
Depends: rom-hamster (= $version), $shlibs
Maintainer: Zhou Xin <zhouxin312@qq.com>
Homepage: https://gitee.com/goddog312/rom-hamster-qt
Description: ROM library manager (Qt 6 GUI)
 Qt 6 desktop front end for rom-hamster. Bundles the Qt 6 runtime and
 plugins under /usr/lib/rom-hamster/qt6 so it does not depend on the
 distribution's Qt packages.
EOF

mkdir -p "$dist"
deb="$dist/rom-hamster-gui_${version}_${arch}.deb"
echo "==> 打包 $deb"
dpkg-deb --root-owner-group -Zxz --build "$stage" "$deb"

echo "==> 自检"
ldd "$stage/usr/bin/rom-hamster-gui" | grep -q "libQt6Core.so.6 => $stage" ||
    { echo "自检失败：GUI 没有链接到包内 Qt6Core" >&2; exit 1; }
if (cd /tmp && env -u LD_LIBRARY_PATH "$stage/usr/bin/rom-hamster-gui" --version >/dev/null 2>&1); then
    echo "    --version OK"
else
    echo "    注意：直接运行需要核心库（rom-hamster 包）；依赖已写入 control" >&2
fi

echo "==> 产物："
ls -l "$deb"
