#!/bin/bash
set -euo pipefail

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

PAM_MODULE_DIR="/usr/local/lib/pam"
PAM_MODULE="pam_duress.so"
SYSTEM_DURESS_DIR="/etc/duress.d"
BIN_DIR="/usr/local/bin"

# 要修改的 PAM 配置文件
PAM_CONFIGS=(
    "/etc/pam.d/screensaver"
    "/etc/pam.d/authorization"
    "/etc/pam.d/sudo"
    "/etc/pam.d/login"
)

# 插入 PAM 配置的行
PAM_LINE="auth       sufficient     ${PAM_MODULE_DIR}/${PAM_MODULE}"

check_root() {
    if [[ $EUID -ne 0 ]]; then
        echo -e "${RED}错误: 此脚本需要 root 权限运行 (sudo ./install.sh)${NC}"
        exit 1
    fi
}

check_prerequisites() {
    # 检查 Xcode CLI 工具
    if ! xcode-select -p &>/dev/null; then
        echo -e "${RED}错误: 未安装 Xcode 命令行工具${NC}"
        echo "请运行: xcode-select --install"
        exit 1
    fi

    # 检查 make
    if ! command -v make &>/dev/null; then
        echo -e "${RED}错误: 未找到 make 命令${NC}"
        exit 1
    fi
}

compile() {
    echo -e "${GREEN}[1/5] 编译 PAM 模块和工具...${NC}"
    make clean all sign
    echo ""
}

install_module() {
    echo -e "${GREEN}[2/5] 安装模块到 ${PAM_MODULE_DIR}/...${NC}"

    mkdir -p "${PAM_MODULE_DIR}"
    cp pam_duress.so "${PAM_MODULE_DIR}/pam_duress.so.2"
    ln -sf pam_duress.so.2 "${PAM_MODULE_DIR}/pam_duress.so"
    chmod 444 "${PAM_MODULE_DIR}/pam_duress.so.2"

    # 安装 CLI 工具
    mkdir -p "${BIN_DIR}"
    cp duress_sign "${BIN_DIR}/duress_sign"
    chmod 755 "${BIN_DIR}/duress_sign"

    echo "  模块: ${PAM_MODULE_DIR}/pam_duress.so.2"
    echo "  工具: ${BIN_DIR}/duress_sign"
    echo ""
}

install_system_dir() {
    echo -e "${GREEN}[3/5] 创建系统胁迫脚本目录...${NC}"

    if [[ ! -d "${SYSTEM_DURESS_DIR}" ]]; then
        mkdir -p "${SYSTEM_DURESS_DIR}"
        chmod 755 "${SYSTEM_DURESS_DIR}"
        chown root:wheel "${SYSTEM_DURESS_DIR}"
        echo "  目录: ${SYSTEM_DURESS_DIR}"
    else
        echo "  目录已存在: ${SYSTEM_DURESS_DIR}"
    fi
    echo ""
}

backup_and_modify_pam() {
    echo -e "${GREEN}[4/5] 修改 PAM 配置文件...${NC}"

    local backup_dir="/etc/pam.d/.duress_backup_$(date +%Y%m%d_%H%M%S)"
    mkdir -p "${backup_dir}"
    echo -e "  ${YELLOW}备份目录: ${backup_dir}${NC}"

    for config in "${PAM_CONFIGS[@]}"; do
        if [[ ! -f "$config" ]]; then
            echo "  跳过 ${config} (不存在)"
            continue
        fi

        # 备份
        cp "$config" "${backup_dir}/$(basename "$config")"

        # 检查是否已安装
        if grep -q "pam_duress" "$config"; then
            echo "  跳过 ${config} (pam_duress 已存在)"
            continue
        fi

        # 在第一行 auth 之前插入
        # 使用 awk 比 sed 更可靠
        local tmpfile
        tmpfile=$(mktemp)
        local inserted=0
        while IFS= read -r line; do
            if [[ $inserted -eq 0 ]] && [[ "$line" =~ ^auth ]]; then
                echo "$PAM_LINE" >> "$tmpfile"
                inserted=1
            fi
            echo "$line" >> "$tmpfile"
        done < "$config"

        if [[ $inserted -eq 1 ]]; then
            mv "$tmpfile" "$config"
            chmod 644 "$config"
            chown root:wheel "$config"
            echo -e "  ${GREEN}已修改 ${config}${NC}"
        else
            rm -f "$tmpfile"
            echo -e "  ${YELLOW}跳过 ${config} (未找到 auth 行)${NC}"
        fi
    done
    echo ""
}

verify_installation() {
    echo -e "${GREEN}[5/5] 验证安装...${NC}"

    local ok=1

    # 检查模块文件
    if [[ -f "${PAM_MODULE_DIR}/pam_duress.so.2" ]]; then
        echo "  模块文件: OK"
    else
        echo -e "  ${RED}模块文件: 未找到${NC}"
        ok=0
    fi

    # 检查代码签名
    if codesign -v "${PAM_MODULE_DIR}/pam_duress.so.2" 2>/dev/null; then
        echo "  代码签名: OK"
    else
        echo -e "  ${RED}代码签名: 失败${NC}"
        ok=0
    fi

    # 检查 CLI 工具
    if [[ -x "${BIN_DIR}/duress_sign" ]]; then
        echo "  CLI 工具: OK"
    else
        echo -e "  ${RED}CLI 工具: 未找到${NC}"
        ok=0
    fi

    # 检查 PAM 配置
    for config in "${PAM_CONFIGS[@]}"; do
        if [[ -f "$config" ]] && grep -q "pam_duress" "$config"; then
            echo "  PAM 配置 $(basename "$config"): OK"
        elif [[ -f "$config" ]]; then
            echo -e "  ${YELLOW}PAM 配置 $(basename "$config"): 未修改${NC}"
        fi
    done

    echo ""
    if [[ $ok -eq 1 ]]; then
        echo -e "${GREEN}安装完成！${NC}"
    else
        echo -e "${YELLOW}安装完成，但有警告。${NC}"
    fi
}

print_next_steps() {
    echo ""
    echo "========================================="
    echo "  下一步操作"
    echo "========================================="
    echo ""
    echo "1. 初始化用户胁迫脚本目录:"
    echo "   duress_sign --init"
    echo ""
    echo "2. 创建胁迫脚本 (例如清除数据):"
    echo "   cp scripts/examples/wipe_data.sh ~/.duress/"
    echo "   chmod 500 ~/.duress/wipe_data.sh"
    echo ""
    echo "3. 绑定胁迫密码:"
    echo "   duress_sign ~/.duress/wipe_data.sh"
    echo ""
    echo "4. 验证配置:"
    echo "   duress_sign --list"
    echo ""
    echo "5. 卸载:"
    echo "   sudo ./uninstall.sh"
    echo ""
    echo -e "${YELLOW}警告: macOS 系统更新可能会重置 PAM 配置文件，"
    echo -e "更新后请重新运行 sudo ./install.sh${NC}"
}

main() {
    echo ""
    echo "========================================="
    echo "  macOS 胁迫密码插件 安装程序"
    echo "========================================="
    echo ""

    check_root
    check_prerequisites

    echo -e "${YELLOW}此安装程序将:${NC}"
    echo "  - 编译 PAM 模块 (pam_duress.so)"
    echo "  - 安装到 ${PAM_MODULE_DIR}/"
    echo "  - 修改 PAM 配置文件 (会先备份)"
    echo "  - 安装 duress_sign 命令行工具"
    echo ""
    read -p "是否继续安装? [y/N] " -n 1 -r
    echo
    if [[ ! $REPLY =~ ^[Yy]$ ]]; then
        echo "已取消安装。"
        exit 1
    fi
    echo ""

    compile
    install_module
    install_system_dir
    backup_and_modify_pam
    verify_installation
    print_next_steps
}

main "$@"
