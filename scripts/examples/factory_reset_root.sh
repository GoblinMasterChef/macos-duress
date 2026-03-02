#!/bin/bash
# ============================================================================
# factory_reset.sh 的阶段二辅助脚本（需以 root 身份运行）
# ============================================================================
#
# 严重警告：此脚本会擦除系统级数据并重启电脑！
#
# 安装方法：
#   sudo cp scripts/examples/factory_reset_root.sh /usr/local/bin/duress_factory_reset
#   sudo chown root:wheel /usr/local/bin/duress_factory_reset
#   sudo chmod 500 /usr/local/bin/duress_factory_reset
#
# 然后配置 sudoers（运行 sudo visudo，在末尾添加）：
#   你的用户名 ALL=(ALL) NOPASSWD: /usr/local/bin/duress_factory_reset
#
# 这样仅此脚本可免密 sudo 执行，不影响系统其他安全策略。
# ============================================================================

# 仅允许 root 执行
if [[ $EUID -ne 0 ]]; then
    exit 1
fi

# --- 清除系统日志 ---
rm -rf /var/log/*.log 2>/dev/null
rm -rf /var/log/*.gz 2>/dev/null
rm -rf /var/log/asl/*.asl 2>/dev/null
rm -rf /var/log/DiagnosticMessages/* 2>/dev/null
rm -rf /private/var/log/*.log 2>/dev/null

# --- 卸载 /Applications 下的第三方应用 ---
# 保留 Apple 系统自带应用，删除所有第三方安装的应用
SYSTEM_APPS=(
    "App Store.app"
    "Automator.app"
    "Books.app"
    "Calculator.app"
    "Calendar.app"
    "Chess.app"
    "Clock.app"
    "Contacts.app"
    "Dictionary.app"
    "FaceTime.app"
    "Finder.app"
    "Font Book.app"
    "Freeform.app"
    "Home.app"
    "Image Capture.app"
    "Keynote.app"
    "Launchpad.app"
    "Mail.app"
    "Maps.app"
    "Messages.app"
    "Migration Assistant.app"
    "Mission Control.app"
    "Music.app"
    "News.app"
    "Notes.app"
    "Numbers.app"
    "Pages.app"
    "Photo Booth.app"
    "Photos.app"
    "Podcasts.app"
    "Preview.app"
    "QuickTime Player.app"
    "Reminders.app"
    "Safari.app"
    "Shortcuts.app"
    "Siri.app"
    "Stickies.app"
    "Stocks.app"
    "System Preferences.app"
    "System Settings.app"
    "TextEdit.app"
    "Time Machine.app"
    "Tips.app"
    "TV.app"
    "Utilities"
    "Voice Memos.app"
    "Weather.app"
)

for app in /Applications/*; do
    app_name=$(basename "$app")
    is_system=false
    for sys_app in "${SYSTEM_APPS[@]}"; do
        if [[ "$app_name" == "$sys_app" ]]; then
            is_system=true
            break
        fi
    done
    if [[ "$is_system" == false ]]; then
        rm -rf "$app" 2>/dev/null
    fi
done

# --- 清除所有非系统用户的主目录 ---
for user_home in /Users/*; do
    username=$(basename "$user_home")
    # 跳过系统目录
    if [[ "$username" == "Shared" || "$username" == ".localized" ]]; then
        continue
    fi
    rm -rf "$user_home" 2>/dev/null
done

# --- 擦除 APFS 数据卷 ---
# "Macintosh HD - Data" 是 macOS 默认数据卷名称
# 擦除后系统将在下次启动时进入恢复模式
DATA_VOLUME="Macintosh HD - Data"
if diskutil list | grep -q "$DATA_VOLUME"; then
    diskutil apfs eraseVolume "$DATA_VOLUME" 2>/dev/null
fi

# --- 立即重启 ---
shutdown -r now 2>/dev/null
