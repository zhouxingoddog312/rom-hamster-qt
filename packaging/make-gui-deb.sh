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

# 诊断：失败时打印行号/退出码/命令；xtrace 把每条命令打到 stderr（无缓冲），
# 即使进程被信号杀死，也能从日志尾部看到“最后执行的是哪条命令”。EXIT trap
# 标记是否走到了正常退出（被 SIGKILL 时不会触发，可据此区分信号杀）。
trap 'echo "RH_TRAP line=$LINENO exit=$? cmd=$BASH_COMMAND" >&2' ERR
trap 'echo "RH_EXIT_TRAP rc=$?" >&2' EXIT
set -x

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
# 收集阶段靠 ldd 解析传递依赖。cmake --install 后二进制的 RUNPATH 已被
# CMake 换成 INSTALL_RPATH（不含构建时 Qt 目录），若系统默认路径里没有 Qt
# （CI 用 aqt 装在非默认位置即如此），裸 ldd 会报 not found 而复制不到任何
# Qt 库。这里把构建时 Qt 库目录显式喂给 ldd；末尾自检仍用裸 ldd，验证的是
# 包内 RUNPATH 能否解析到已复制进来的库。
qt_ld_library_path="$qt_libs${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"

echo "==> 安装 GUI 到 $stage"
echo "    build_dir=$build_dir"
cmake --version 2>/dev/null | head -n1
echo "RH_DIAG disk:" >&2; df -h / /tmp "$build_dir" 2>&1 | sed 's/^/  /' >&2 || true
echo "RH_DIAG mem:" >&2; free -m 2>&1 | sed 's/^/  /' >&2 || true
rm -rf "$work"
mkdir -p "$stage/usr"
# 显式捕获 cmake 退出码：ERR trap 在子进程被信号杀死时未必触发，且输出可能
# 因管道缓冲丢失；这里绕过 set -e 单独取 rc，确保拿到真实退出码（143/137/139
# 分别对应 SIGTERM/SIGKILL/SIGSEGV，1 则是 cmake 自身报错）。
set +e
cmake --install "$build_dir" --prefix "$stage/usr" --strip
cmake_rc=$?
set -e
echo "RH_DIAG cmake_install_exit=$cmake_rc (strip=on)" >&2
if [ "$cmake_rc" -ne 0 ]; then
  echo "RH_DIAG cmake failed with --strip; retrying WITHOUT --strip to isolate:" >&2
  rm -rf "$work"
  mkdir -p "$stage/usr"
  set +e
  cmake --install "$build_dir" --prefix "$stage/usr" 2>&1 | tail -n 12
  nostrip_rc=${PIPESTATUS[0]}
  set -e
  echo "RH_DIAG cmake_install_nostrip_exit=$nostrip_rc" >&2
  exit "$cmake_rc"
fi

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
        env LD_LIBRARY_PATH="$qt_ld_library_path" ldd "$elf" 2>/dev/null |
            sed -n 's/.*=> \(\/[^ ]*\).*/\1/p' |
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
        env LD_LIBRARY_PATH="$qt_ld_library_path" ldd "$lib" 2>/dev/null |
            sed -n 's/.*=> \(\/[^ ]*\).*/\1/p' |
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
# install-qt-action 会把 LD_LIBRARY_PATH 残留为 aqt 的 Qt 库目录；ldd 搜索时
# LD_LIBRARY_PATH 优先于二进制自身的 RUNPATH，会把 Qt 依赖解析到构建机的 aqt
# 目录而非包内，造成误报（CI 上 aqt 目录仍在，本地/真实系统上则不会）。
# 这里清掉它，模拟真实安装环境：只靠包内 RUNPATH 解析，验证 deb 装到干净系
# 统上的行为。失败时把 ldd 输出打出来，便于定位是缺库还是 RUNPATH 不对。
if ! env -u LD_LIBRARY_PATH ldd "$stage/usr/bin/rom-hamster-gui" 2>&1 |
        grep -q "libQt6Core.so.6 => $stage"; then
    echo "自检失败：GUI 没有链接到包内 Qt6Core。ldd 输出（已清 LD_LIBRARY_PATH）：" >&2
    env -u LD_LIBRARY_PATH ldd "$stage/usr/bin/rom-hamster-gui" >&2 || true
    exit 1
fi
if (cd /tmp && env -u LD_LIBRARY_PATH "$stage/usr/bin/rom-hamster-gui" --version >/dev/null 2>&1); then
    echo "    --version OK"
else
    echo "    注意：直接运行需要核心库（rom-hamster 包）；依赖已写入 control" >&2
fi

echo "==> 产物："
ls -l "$deb"
