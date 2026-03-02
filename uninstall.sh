#!/bin/bash
set -euo pipefail

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

PAM_MODULE_DIR="/usr/local/lib/pam"
BIN_DIR="/usr/local/bin"
SYSTEM_DURESS_DIR="/etc/duress.d"

PAM_CONFIGS=(
    "/etc/pam.d/screensaver"
    "/etc/pam.d/authorization"
    "/etc/pam.d/sudo"
    "/etc/pam.d/login"
)

check_root() {
    if [[ $EUID -ne 0 ]]; then
        echo -e "${RED}错误: 此脚本需要 root 权限运行 (sudo ./uninstall.sh)${NC}"
        exit 1
    fi
}

remove_pam_entries() {
    echo -e "${GREEN}[1/3] 从 PAM 配置中移除 pam_duress...${NC}"

    for config in "${PAM_CONFIGS[@]}"; do
        if [[ ! -f "$config" ]]; then
            continue
        fi

        if grep -q "pam_duress" "$config"; then
            # 备份当前配置
            cp "$config" "${config}.duress_uninstall_bak"

            # 移除包含 pam_duress 的行
            local tmpfile
            tmpfile=$(mktemp)
            grep -v "pam_duress" "$config" > "$tmpfile"
            mv "$tmpfile" "$config"
            chmod 644 "$config"
            chown root:wheel "$config"
            echo "  已清理 ${config}"
        else
            echo "  跳过 ${config} (未包含 pam_duress)"
        fi
    done
    echo ""
}

remove_module() {
    echo -e "${GREEN}[2/3] 删除模块文件...${NC}"

    local files=(
        "${PAM_MODULE_DIR}/pam_duress.so"
        "${PAM_MODULE_DIR}/pam_duress.so.2"
        "${BIN_DIR}/duress_sign"
    )

    for f in "${files[@]}"; do
        if [[ -f "$f" ]] || [[ -L "$f" ]]; then
            rm -f "$f"
            echo "  已删除 ${f}"
        fi
    done
    echo ""
}

remove_data() {
    echo -e "${GREEN}[3/3] 清理数据目录...${NC}"

    # 系统目录
    if [[ -d "${SYSTEM_DURESS_DIR}" ]]; then
        read -p "是否删除系统胁迫脚本目录 ${SYSTEM_DURESS_DIR}? [y/N] " -n 1 -r
        echo
        if [[ $REPLY =~ ^[Yy]$ ]]; then
            rm -rf "${SYSTEM_DURESS_DIR}"
            echo "  已删除 ${SYSTEM_DURESS_DIR}"
        else
            echo "  保留 ${SYSTEM_DURESS_DIR}"
        fi
    fi

    # 用户目录提示
    echo ""
    echo -e "${YELLOW}注意: 用户胁迫脚本目录 (~/.duress/) 未被删除。${NC}"
    echo "如需删除，请手动运行: rm -rf ~/.duress/"
    echo ""
}

main() {
    echo ""
    echo "========================================="
    echo "  macOS 胁迫密码插件 卸载程序"
    echo "========================================="
    echo ""

    check_root

    echo -e "${YELLOW}此卸载程序将:${NC}"
    echo "  - 从 PAM 配置文件中移除 pam_duress 条目"
    echo "  - 删除 PAM 模块和 CLI 工具"
    echo "  - 可选删除系统胁迫脚本目录"
    echo ""
    read -p "是否继续卸载? [y/N] " -n 1 -r
    echo
    if [[ ! $REPLY =~ ^[Yy]$ ]]; then
        echo "已取消卸载。"
        exit 1
    fi
    echo ""

    remove_pam_entries
    remove_module
    remove_data

    echo -e "${GREEN}卸载完成！${NC}"
    echo ""

    # 检查是否有备份
    local backups
    backups=$(find /etc/pam.d/ -name ".duress_backup_*" -type d 2>/dev/null || true)
    if [[ -n "$backups" ]]; then
        echo -e "${YELLOW}PAM 配置备份仍保留在:${NC}"
        echo "$backups"
        echo "如不需要可手动删除。"
    fi
}

main "$@"
