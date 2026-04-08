#!/bin/bash
# TsEngine 自动化发布脚本
# 编译 → strip → 打包 → 创建 GitHub Release
set -e

# ── 配置 ──
PROJECT="TsEngine"
VERSION=$(grep 'project.*VERSION' CMakeLists.txt | sed 's/.*VERSION \([0-9.]*\).*/\1/')
ARCH=$(uname -m)
BUILD_DIR="build"
RELEASE_DIR="release"
TARBALL="${PROJECT}-v${VERSION}-${ARCH}.tar.gz"

# 颜色
G='\033[32m'; Y='\033[33m'; C='\033[36m'; D='\033[90m'; RST='\033[0m'

info()  { echo -e "  ${G}+${RST} $*"; }
warn()  { echo -e "  ${Y}!${RST} $*"; }
step()  { echo -e "\n  ${C}[$1]${RST} $2"; }

# ── 参数 ──
DO_PUBLISH=false
TAG_NAME=""
NOTES=""

usage() {
    echo ""
    echo "  用法: $0 [选项]"
    echo ""
    echo "  选项:"
    echo "    -p, --publish        编译后创建 GitHub Release 并上传"
    echo "    -t, --tag <tag>      指定 tag 名 (默认: v\$VERSION)"
    echo "    -n, --notes <text>   Release 说明"
    echo "    -h, --help           帮助"
    echo ""
    echo "  示例:"
    echo "    ./release.sh                          # 仅编译打包"
    echo "    ./release.sh -p                       # 编译 + 发布到 GitHub"
    echo "    ./release.sh -p -t v0.2.0 -n '新功能' # 指定 tag 和说明"
    echo ""
    exit 0
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        -p|--publish) DO_PUBLISH=true; shift ;;
        -t|--tag)     TAG_NAME="$2"; shift 2 ;;
        -n|--notes)   NOTES="$2"; shift 2 ;;
        -h|--help)    usage ;;
        *) echo "  未知参数: $1"; usage ;;
    esac
done

[[ -z "$TAG_NAME" ]] && TAG_NAME="v${VERSION}"
[[ -z "$NOTES" ]]    && NOTES="${PROJECT} ${TAG_NAME} (${ARCH})"

echo ""
echo -e "  ${C}${PROJECT}${RST} release ${Y}${TAG_NAME}${RST} (${ARCH})"
echo ""

# ── Step 1: 编译 ──
step 1 "编译 (Release)"

cmake -B "$BUILD_DIR" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_FLAGS="-O2" 2>&1 | tail -1
cmake --build "$BUILD_DIR" -j"$(nproc)" 2>&1

if [[ ! -f "${BUILD_DIR}/${PROJECT}" ]]; then
    echo "  编译失败: ${BUILD_DIR}/${PROJECT} 不存在"
    exit 1
fi
info "编译完成: ${BUILD_DIR}/${PROJECT}"

# ── Step 2: Strip ──
step 2 "Strip 二进制"

SIZE_BEFORE=$(stat -c%s "${BUILD_DIR}/${PROJECT}" 2>/dev/null || stat -f%z "${BUILD_DIR}/${PROJECT}")
strip "${BUILD_DIR}/${PROJECT}"
SIZE_AFTER=$(stat -c%s "${BUILD_DIR}/${PROJECT}" 2>/dev/null || stat -f%z "${BUILD_DIR}/${PROJECT}")

info "strip: ${SIZE_BEFORE} -> ${SIZE_AFTER} bytes ($(( (SIZE_BEFORE - SIZE_AFTER) * 100 / SIZE_BEFORE ))% 减少)"

# ── Step 3: 编译测试目标 ──
step 3 "编译测试目标"

if [[ -f "test/target.cpp" ]]; then
    c++ -o "${BUILD_DIR}/target" test/target.cpp -O0 -g 2>&1 && \
        info "测试目标: ${BUILD_DIR}/target" || \
        warn "测试目标编译失败 (非致命)"
fi

# ── Step 4: 打包 ──
step 4 "打包"

rm -rf "$RELEASE_DIR"
mkdir -p "${RELEASE_DIR}/${PROJECT}"

# 复制文件
cp "${BUILD_DIR}/${PROJECT}" "${RELEASE_DIR}/${PROJECT}/"
cp README.md                 "${RELEASE_DIR}/${PROJECT}/" 2>/dev/null || true

# 如果测试目标编译成功也放进去
[[ -f "${BUILD_DIR}/target" ]] && cp "${BUILD_DIR}/target" "${RELEASE_DIR}/${PROJECT}/"

# 创建 tarball
(cd "$RELEASE_DIR" && tar czf "../${TARBALL}" "${PROJECT}/")

TARBALL_SIZE=$(stat -c%s "${TARBALL}" 2>/dev/null || stat -f%z "${TARBALL}")
info "打包完成: ${TARBALL} (${TARBALL_SIZE} bytes)"

# ── Step 5: 发布 (可选) ──
if $DO_PUBLISH; then
    step 5 "发布到 GitHub"

    if ! command -v gh &>/dev/null; then
        warn "gh CLI 未安装, 跳过发布"
        warn "手动安装: pkg install gh  或  apt install gh"
        warn "然后运行: gh release create ${TAG_NAME} ${TARBALL} --title '${TAG_NAME}' --notes '${NOTES}'"
    else
        # 创建 tag (如果不存在)
        if ! git rev-parse "$TAG_NAME" &>/dev/null; then
            git tag "$TAG_NAME"
            info "创建 tag: ${TAG_NAME}"
        fi

        # 推送 tag
        git push origin "$TAG_NAME" 2>&1 || true

        # 创建 release 并上传
        gh release create "$TAG_NAME" "$TARBALL" \
            --title "${PROJECT} ${TAG_NAME}" \
            --notes "$NOTES" \
            2>&1

        info "发布完成!"
        info "URL: $(gh release view "$TAG_NAME" --json url -q .url 2>/dev/null || echo '(查看 GitHub Releases 页面)')"
    fi
fi

# ── 完成 ──
echo ""
echo -e "  ${G}完成!${RST}"
echo ""
echo -e "  ${D}产物:${RST}"
echo -e "    二进制  ${C}${RELEASE_DIR}/${PROJECT}/${PROJECT}${RST}"
echo -e "    压缩包  ${C}${TARBALL}${RST}"

if ! $DO_PUBLISH; then
    echo ""
    echo -e "  ${D}发布到 GitHub:${RST}"
    echo -e "    ${C}./release.sh -p${RST}"
fi

echo ""
