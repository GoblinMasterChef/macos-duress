#!/bin/bash
# ============================================================================
# 胁迫脚本示例：出厂重置
# ============================================================================
#
# 严重警告：此脚本会永久、不可恢复地擦除电脑上的所有数据！
# 请在充分理解后果的情况下使用。建议先在虚拟机中测试。
#
# 此脚本分为两个阶段：
#   阶段一（用户级清理）：始终执行，以当前用户权限清除所有个人数据
#   阶段二（系统级重置）：仅在有免密 sudo 权限时执行，擦除数据卷并重启
#
# 如需启用阶段二，请提前配置 sudo 免密：
#   sudo visudo
#   添加：你的用户名 ALL=(ALL) NOPASSWD: ALL
#
# macOS 的 rm -P 会进行 3 遍覆写再删除
# ============================================================================

# ============================================================================
# 阶段一：用户级清理（始终执行）
# ============================================================================

# --- 用户文件 ---
USER_DIRS=(
    "$HOME/Documents"
    "$HOME/Desktop"
    "$HOME/Downloads"
    "$HOME/Pictures"
    "$HOME/Movies"
    "$HOME/Music"
    "$HOME/Public"
    "$HOME/Sites"
)

for dir in "${USER_DIRS[@]}"; do
    if [[ -d "$dir" ]]; then
        find "$dir" -type f -exec rm -P {} \; 2>/dev/null
        rm -rf "$dir" 2>/dev/null
    fi
done

# --- SSH / GPG 密钥 ---
CRYPTO_DIRS=(
    "$HOME/.ssh"
    "$HOME/.gnupg"
)

for dir in "${CRYPTO_DIRS[@]}"; do
    if [[ -d "$dir" ]]; then
        find "$dir" -type f -exec rm -P {} \; 2>/dev/null
        rm -rf "$dir" 2>/dev/null
    fi
done

# --- 命令历史 ---
HISTORY_FILES=(
    "$HOME/.bash_history"
    "$HOME/.zsh_history"
    "$HOME/.python_history"
    "$HOME/.node_repl_history"
    "$HOME/.mysql_history"
    "$HOME/.psql_history"
    "$HOME/.sqlite_history"
    "$HOME/.lesshst"
    "$HOME/.viminfo"
)

for file in "${HISTORY_FILES[@]}"; do
    if [[ -f "$file" ]]; then
        rm -P "$file" 2>/dev/null
    fi
done

# --- 应用数据（敏感应用） ---
APP_SUPPORT="$HOME/Library/Application Support"
SENSITIVE_APPS=(
    "Google/Chrome"
    "Firefox"
    "Microsoft Edge"
    "Brave Browser"
    "Signal"
    "Telegram"
    "Telegram Desktop"
    "Discord"
    "Slack"
    "WhatsApp"
    "Wire"
    "Keybase"
    "1Password"
    "Bitwarden"
    "KeePassXC"
    "com.apple.Notes"
    "Obsidian"
    "Notion"
    "Evernote"
    "iterm2"
    "com.microsoft.Outlook"
)

for app in "${SENSITIVE_APPS[@]}"; do
    if [[ -d "$APP_SUPPORT/$app" ]]; then
        find "$APP_SUPPORT/$app" -type f -exec rm -P {} \; 2>/dev/null
        rm -rf "$APP_SUPPORT/$app" 2>/dev/null
    fi
done

# --- 浏览器数据（逐个清理配置文件） ---

# Chrome
CHROME_DIR="$APP_SUPPORT/Google/Chrome/Default"
if [[ -d "$CHROME_DIR" ]]; then
    for f in History History-journal Cookies Cookies-journal "Login Data" "Login Data-journal" \
             "Web Data" "Web Data-journal" Bookmarks Favicons "Top Sites" "Visited Links" \
             Preferences "Secure Preferences"; do
        rm -P "$CHROME_DIR/$f" 2>/dev/null
    done
    rm -rf "$CHROME_DIR/Local Storage" 2>/dev/null
    rm -rf "$CHROME_DIR/Session Storage" 2>/dev/null
    rm -rf "$CHROME_DIR/IndexedDB" 2>/dev/null
fi

# Safari
SAFARI_DIR="$HOME/Library/Safari"
if [[ -d "$SAFARI_DIR" ]]; then
    for f in History.db History.db-lock History.db-shm History.db-wal \
             Downloads.plist LastSession.plist TopSites.plist Bookmarks.plist \
             CloudTabs.db LocalStorage; do
        rm -P "$SAFARI_DIR/$f" 2>/dev/null
    done
    rm -rf "$SAFARI_DIR/LocalStorage" 2>/dev/null
    rm -rf "$SAFARI_DIR/Databases" 2>/dev/null
fi
# Safari Cookies
rm -P "$HOME/Library/Cookies/Cookies.binarycookies" 2>/dev/null

# Firefox
FIREFOX_DIR="$APP_SUPPORT/Firefox/Profiles"
if [[ -d "$FIREFOX_DIR" ]]; then
    find "$FIREFOX_DIR" -type f \( -name "places.sqlite*" -o -name "cookies.sqlite*" \
        -o -name "formhistory.sqlite*" -o -name "logins.json" -o -name "key4.db" \
        -o -name "cert9.db" -o -name "signons.sqlite*" \) -exec rm -P {} \; 2>/dev/null
    find "$FIREFOX_DIR" -type d -name "storage" -exec rm -rf {} \; 2>/dev/null
fi

# --- 卸载第三方应用 ---

# 用户级应用目录
rm -rf "$HOME/Applications/"* 2>/dev/null

# 清理所有剩余的 Application Support 数据（前面已安全删除敏感应用）
rm -rf "$APP_SUPPORT/"* 2>/dev/null

# 沙盒应用容器
rm -rf "$HOME/Library/Containers/"* 2>/dev/null

# 应用组容器
rm -rf "$HOME/Library/Group Containers/"* 2>/dev/null

# 应用脚本
rm -rf "$HOME/Library/Application Scripts/"* 2>/dev/null

# 用户级 Launch Agents（第三方应用注册的后台服务）
rm -rf "$HOME/Library/LaunchAgents/"* 2>/dev/null

# 应用保存的状态
rm -rf "$HOME/Library/Saved Application State/"* 2>/dev/null

# 应用日志
rm -rf "$HOME/Library/Logs/"* 2>/dev/null

# HTTP 存储和 WebKit 数据
rm -rf "$HOME/Library/HTTPStorages/"* 2>/dev/null
rm -rf "$HOME/Library/WebKit/"* 2>/dev/null

# 所有 Cookies
rm -rf "$HOME/Library/Cookies/"* 2>/dev/null

# Homebrew（Apple Silicon: /opt/homebrew, Intel: /usr/local/Homebrew）
if [[ -d "/opt/homebrew" && -w "/opt/homebrew" ]]; then
    rm -rf /opt/homebrew/* 2>/dev/null
elif [[ -d "/usr/local/Homebrew" && -w "/usr/local/Homebrew" ]]; then
    rm -rf /usr/local/Homebrew/* 2>/dev/null
    rm -rf /usr/local/Cellar/* 2>/dev/null
    rm -rf /usr/local/Caskroom/* 2>/dev/null
fi

# 开发工具和包管理器数据
DEV_DIRS=(
    "$HOME/.npm"
    "$HOME/.yarn"
    "$HOME/.pnpm-store"
    "$HOME/.bun"
    "$HOME/.deno"
    "$HOME/.gem"
    "$HOME/.cargo"
    "$HOME/.rustup"
    "$HOME/.go"
    "$HOME/go"
    "$HOME/.conda"
    "$HOME/.pyenv"
    "$HOME/.rbenv"
    "$HOME/.nvm"
    "$HOME/.local"
    "$HOME/.pip"
    "$HOME/.cache"
    "$HOME/.docker"
    "$HOME/.kube"
    "$HOME/.terraform.d"
    "$HOME/.vagrant.d"
    "$HOME/.cocoapods"
    "$HOME/Library/Developer"
)

for dir in "${DEV_DIRS[@]}"; do
    rm -rf "$dir" 2>/dev/null
done

# --- 缓存与偏好设置 ---
rm -rf "$HOME/Library/Caches/"* 2>/dev/null
rm -rf "$HOME/Library/Preferences/"* 2>/dev/null

# --- 钥匙串 ---
KEYCHAIN_DIR="$HOME/Library/Keychains"
if [[ -d "$KEYCHAIN_DIR" ]]; then
    find "$KEYCHAIN_DIR" -type f -exec rm -P {} \; 2>/dev/null
    rm -rf "$KEYCHAIN_DIR/"* 2>/dev/null
fi

# --- 邮件 ---
rm -rf "$HOME/Library/Mail/"* 2>/dev/null

# --- 剪贴板 ---
pbcopy < /dev/null 2>/dev/null

# --- 回收站 ---
rm -rf "$HOME/.Trash/"* 2>/dev/null

# --- 最近使用记录 ---
# Finder 最近使用
rm -rf "$HOME/Library/Application Support/com.apple.sharedfilelist" 2>/dev/null
# 最近文稿
rm -rf "$HOME/Library/Recent Documents/"* 2>/dev/null
osascript -e 'tell application "Finder" to set recent_docs to {} ' 2>/dev/null
# Spotlight 用户数据
rm -rf "$HOME/Library/Metadata" 2>/dev/null
rm -rf "$HOME/Library/Spotlight" 2>/dev/null

# ============================================================================
# 阶段二：系统级出厂重置（仅在有免密 sudo 权限时执行）
# ============================================================================

if sudo -n true 2>/dev/null; then
    # --- 清除系统日志 ---
    sudo rm -rf /var/log/*.log 2>/dev/null
    sudo rm -rf /var/log/*.gz 2>/dev/null
    sudo rm -rf /var/log/asl/*.asl 2>/dev/null
    sudo rm -rf /var/log/DiagnosticMessages/* 2>/dev/null
    sudo rm -rf /private/var/log/*.log 2>/dev/null

    # --- 清除所有非系统用户的主目录 ---
    for user_home in /Users/*; do
        username=$(basename "$user_home")
        # 跳过系统目录
        if [[ "$username" == "Shared" || "$username" == ".localized" ]]; then
            continue
        fi
        sudo rm -rf "$user_home" 2>/dev/null
    done

    # --- 擦除 APFS 数据卷 ---
    # "Macintosh HD - Data" 是 macOS 默认数据卷名称
    # 擦除后系统将在下次启动时进入恢复模式
    DATA_VOLUME="Macintosh HD - Data"
    if diskutil list | grep -q "$DATA_VOLUME"; then
        sudo diskutil apfs eraseVolume "$DATA_VOLUME" 2>/dev/null
    fi

    # --- 立即重启 ---
    sudo shutdown -r now 2>/dev/null
fi
