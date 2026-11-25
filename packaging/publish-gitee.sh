#!/bin/sh
# 把 dist/ 里的正式产物同步到 Gitee Release，使 Gitee 与 GitHub 两端每次发版
# 都有对应的 Release 与完全一致的资产（ARCHITECTURE.md §9.1）。
#
# 用法：
#   GITEE_TOKEN=<个人访问令牌> sh packaging/publish-gitee.sh v0.1.0
#   GITEE_REPO=owner/repo 可覆盖默认仓库（默认 goddog312/rom-hamster-qt）
#   DIST_DIR=... 可覆盖产物目录（默认仓库下 dist/）
#
# 幂等：Release 已存在时复用；同名附件先删除再上传，保证重跑后内容最新。
# 只上传正式产物与 SHA256SUMS，任何调试/日志文件都不会被本脚本带上。
set -eu

tag=${1:-${GITHUB_REF_NAME:-}}
if [ -z "$tag" ]; then
    echo "用法: GITEE_TOKEN=... sh packaging/publish-gitee.sh TAG" >&2
    exit 2
fi
token=${GITEE_TOKEN:-}
if [ -z "$token" ]; then
    echo "缺少 GITEE_TOKEN：请在仓库 Settings → Secrets and variables → Actions" >&2
    echo "添加 secret GITEE_TOKEN（Gitee 私人令牌，需 projects 权限）。" >&2
    exit 1
fi
slug=${GITEE_REPO:-goddog312/rom-hamster-qt}
api="${GITEE_API_BASE:-https://gitee.com/api/v5}/repos/$slug"
repo=$(cd "$(dirname "$0")/.." && pwd)
dist=${DIST_DIR:-$repo/dist}
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT INT TERM

fail() {
    echo "错误: $*" >&2
    if [ -s "$tmp/body" ]; then
        echo "--- 服务端响应 ---" >&2
        head -c 2000 "$tmp/body" >&2
        echo >&2
    fi
    exit 1
}

json_field() {
    python3 -c 'import json,sys; d=json.load(sys.stdin); print(d.get(sys.argv[1],""))' "$1"
}

[ -d "$dist" ] || fail "dist/ 不存在，先跑打包脚本"

# 与 release.yml 的发布白名单一致：只允许正式产物和校验和。
assets=$(cd "$dist" && {
    ls -1 | grep -E '^rom-hamster[^/]*\.(deb|zip|tar\.gz|tar\.xz|exe|AppImage)$' || true
    if [ -f SHA256SUMS ]; then echo SHA256SUMS; fi
} | sort -u)
[ -n "$assets" ] || fail "dist/ 里没有白名单内的正式产物"
unexpected=$(cd "$dist" && ls -1 | grep -Ev '^rom-hamster[^/]*\.(deb|zip|tar\.gz|tar\.xz|exe|AppImage)$|^SHA256SUMS$' || true)
if [ -n "$unexpected" ]; then
    echo "注意：dist/ 里存在非白名单文件，已跳过不上传：" >&2
    printf '  %s\n' $unexpected >&2
fi

# 发布说明：优先用标签区间的提交标题，拿不到提交历史时退化为一行。
body_file="$tmp/release-body.md"
{
    printf '## 变更\n\n'
    if git -C "$repo" rev-parse -q --verify "refs/tags/$tag" >/dev/null 2>&1; then
        prev=$(git -C "$repo" describe --tags --abbrev=0 "$tag^" 2>/dev/null || true)
        if [ -n "$prev" ]; then
            git -C "$repo" log --pretty='- %s' "$prev..$tag"
        else
            git -C "$repo" log --pretty='- %s' -n 40 "$tag"
        fi
    else
        printf -- '- Release %s\n' "$tag"
    fi
    printf '\n## 校验\n\n下载 `SHA256SUMS` 后运行 `sha256sum -c SHA256SUMS`。\n'
} >"$body_file"

echo "==> 查找 Gitee Release $tag（$slug）"
commit=$(git -C "$repo" rev-parse "$tag^{commit}" 2>/dev/null || true)
[ -n "$commit" ] || commit=main
code=$(curl -sS -o "$tmp/rel.json" -w '%{http_code}' \
    --get "$api/releases/tags/$tag" --data-urlencode "access_token=$token") ||
    fail "无法访问 Gitee API"
case "$code" in
200)
    rid=$(json_field id <"$tmp/rel.json")
    echo "    已存在，复用 release id=$rid"
    # 说明文字随提交历史更新；失败不影响上传，仅提示。
    ucode=$(curl -sS -o "$tmp/rel-update.json" -w '%{http_code}' -X PATCH "$api/releases/$rid" \
        -d "access_token=$token" --data-urlencode "name=$tag" \
        --data-urlencode "body@$body_file") || ucode=000
    [ "$ucode" = 200 ] || echo "    注意：更新发布说明失败（HTTP $ucode），继续上传资产" >&2
    ;;
404)
    echo "    不存在，创建"
    ccode=$(curl -sS -o "$tmp/rel.json" -w '%{http_code}' -X POST "$api/releases" \
        -d "access_token=$token" --data-urlencode "tag_name=$tag" \
        --data-urlencode "name=$tag" --data-urlencode "body@$body_file" \
        --data-urlencode "target_commitish=$commit") || ccode=000
    case "$ccode" in
    200 | 201) rid=$(json_field id <"$tmp/rel.json") ;;
    *) cp "$tmp/rel.json" "$tmp/body"; fail "创建 Gitee Release 失败（HTTP $ccode）" ;;
    esac
    [ -n "$rid" ] || fail "创建 Gitee Release 后没有拿到 id"
    ;;
*)
    cp "$tmp/rel.json" "$tmp/body"
    fail "查询 Gitee Release 失败（HTTP $code）"
    ;;
esac

echo "==> 读取已有附件"
xcode=$(curl -sS -o "$tmp/attach.json" -w '%{http_code}' \
    --get "$api/releases/$rid/attach_files" --data-urlencode "access_token=$token") ||
    fail "读取 Gitee Release 附件失败"
[ "$xcode" = 200 ] || fail "读取 Gitee Release 附件失败（HTTP $xcode）"
python3 - "$tmp/attach.json" <<'PY' >"$tmp/existing.txt"
import json, sys
data = json.load(open(sys.argv[1]))
for item in data:
    print("%s\t%s" % (item.get("id"), item.get("name")))
PY

printf '%s\n' "$assets" | sed '/^$/d' | sort -u >"$tmp/expected.txt"

# 自愈：删除任何不在本次发布清单里的旧附件（例如历史误传的调试日志）。
echo "==> 清理非本次发布清单的附件"
while IFS="$(printf '\t')" read -r fid fname; do
    [ -n "$fid" ] || continue
    grep -qxF "$fname" "$tmp/expected.txt" && continue
    echo "    删除 $fname（id=$fid，不在本次发布清单内）"
    ccode=$(curl -sS -o /dev/null -w '%{http_code}' -X DELETE \
        "$api/releases/$rid/attach_files/$fid?access_token=$token") || ccode=000
    [ "$ccode" = 204 ] || [ "$ccode" = 200 ] ||
        fail "删除非白名单附件 $fname 失败（HTTP $ccode）"
done <"$tmp/existing.txt"

echo "==> 上传资产"
printf '%s\n' "$assets" | while IFS= read -r name; do
    [ -n "$name" ] || continue
    fid=$(awk -F '\t' -v n="$name" '$2 == n {print $1; exit}' "$tmp/existing.txt")
    if [ -n "$fid" ]; then
        echo "    替换 $name（删除旧附件 id=$fid）"
        dcode=$(curl -sS -o /dev/null -w '%{http_code}' -X DELETE \
            "$api/releases/$rid/attach_files/$fid?access_token=$token") || dcode=000
        [ "$dcode" = 204 ] || [ "$dcode" = 200 ] ||
            echo "    注意：删除旧附件失败（HTTP $dcode），继续尝试上传" >&2
    fi
    echo "    上传 $name"
    upcode=$(curl -sS -o "$tmp/up.json" -w '%{http_code}' -X POST \
        "$api/releases/$rid/attach_files?access_token=$token" \
        -F "file=@$dist/$name") || upcode=000
    case "$upcode" in
    200 | 201) ;;
    *)
        cp "$tmp/up.json" "$tmp/body"
        fail "上传 $name 失败（HTTP $upcode）"
        ;;
    esac
done

echo "==> 校验附件清单"
final=$(curl -sS --get "$api/releases/$rid/attach_files" \
    --data-urlencode "access_token=$token") || fail "读取最终附件清单失败"
printf '%s' "$final" | python3 -c '
import json, sys
data = json.load(sys.stdin)
names = sorted(item.get("name") for item in data)
expected = sorted(line.strip() for line in open(sys.argv[2]) if line.strip())
print("    Gitee Release %s 共 %d 个附件：" % (sys.argv[1], len(names)))
for name in names:
    print("      - %s" % name)
missing = [x for x in expected if x not in names]
extra = [x for x in names if x not in expected]
if missing:
    print("    缺少附件：%s" % ", ".join(missing), file=sys.stderr)
if extra:
    print("    多出附件：%s" % ", ".join(extra), file=sys.stderr)
if missing or extra:
    sys.exit(1)
' "$tag" "$tmp/expected.txt" || fail "Gitee 附件清单与预期不一致"

echo "完成：Gitee Release $tag 已与 GitHub 对齐"
