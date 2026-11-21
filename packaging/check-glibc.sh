#!/bin/sh
# 检查 ELF 文件引用到的最高 GLIBC 符号版本（ARCHITECTURE.md §9.1：
# AppImage/deb 的发行基线是 Ubuntu 22.04，即 glibc 2.35）。
#
# 用法：sh packaging/check-glibc.sh [--max 2.35] FILE...
#
# 任何文件需要的 GLIBC 版本高于 --max 时退出码为 1；文件不存在或 objdump
# 不可用时退出码为 2。产物在 Ubuntu 22.04 上构建时通常直接通过，这个检查
# 用来防止以后误在更新的发行版上出包。
set -eu

max=2.35
if [ "${1:-}" = "--max" ]; then
    [ $# -ge 3 ] || { echo "用法: $0 [--max X.Y] FILE..." >&2; exit 2; }
    max=$2
    shift 2
fi
if [ $# -eq 0 ]; then
    echo "用法: $0 [--max X.Y] FILE..." >&2
    exit 2
fi
command -v objdump >/dev/null 2>&1 || {
    echo "找不到 objdump（binutils）" >&2
    exit 2
}

status=0
for file in "$@"; do
    if [ ! -f "$file" ]; then
        echo "缺少文件：$file" >&2
        exit 2
    fi
    top=$(objdump -T "$file" 2>/dev/null |
        grep -o 'GLIBC_[0-9][0-9.]*' |
        sed 's/^GLIBC_//' |
        sort -Vu | tail -n 1)
    if [ -z "$top" ]; then
        echo "SKIP $file：没有 GLIBC 符号引用（静态链接或非 x86-64 ELF？）"
        continue
    fi
    if [ "$(printf '%s\n%s\n' "$max" "$top" | sort -V | tail -n 1)" = "$max" ]; then
        echo "OK   $file：GLIBC_$top <= $max"
    else
        echo "FAIL $file：需要 GLIBC_$top，高于基线 $max" >&2
        status=1
    fi
done

exit $status
