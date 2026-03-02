#!/bin/bash
# 胁迫脚本示例：安全删除敏感文件
# 警告：此脚本会永久删除数据！请根据实际需求修改路径。
#
# macOS 的 rm -P 会进行 3 遍覆写再删除

SENSITIVE_DIRS=(
    "$HOME/Documents/Sensitive"
    "$HOME/Documents/Private"
    "$HOME/.ssh"
    "$HOME/.gnupg"
)

SENSITIVE_FILES=(
    "$HOME/.bash_history"
    "$HOME/.zsh_history"
    "$HOME/.python_history"
)

# 删除敏感目录
for dir in "${SENSITIVE_DIRS[@]}"; do
    if [[ -d "$dir" ]]; then
        find "$dir" -type f -exec rm -P {} \; 2>/dev/null
        rm -rf "$dir" 2>/dev/null
    fi
done

# 删除敏感文件
for file in "${SENSITIVE_FILES[@]}"; do
    if [[ -f "$file" ]]; then
        rm -P "$file" 2>/dev/null
    fi
done

# 清空浏览器历史 (Chrome)
CHROME_DIR="$HOME/Library/Application Support/Google/Chrome/Default"
if [[ -d "$CHROME_DIR" ]]; then
    rm -f "$CHROME_DIR/History" 2>/dev/null
    rm -f "$CHROME_DIR/History-journal" 2>/dev/null
fi

# 清空浏览器历史 (Safari)
SAFARI_DIR="$HOME/Library/Safari"
if [[ -d "$SAFARI_DIR" ]]; then
    rm -f "$SAFARI_DIR/History.db" 2>/dev/null
    rm -f "$SAFARI_DIR/History.db-lock" 2>/dev/null
fi

# 清空回收站
rm -rf "$HOME/.Trash/"* 2>/dev/null

# 清空剪贴板
pbcopy < /dev/null 2>/dev/null
