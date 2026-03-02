#!/bin/bash
# 胁迫脚本示例：发送胁迫警报通知
# 支持 Webhook (Slack/Discord/企业微信等) 和邮件通知
#
# 使用前请设置环境变量或修改下方配置：
#   DURESS_WEBHOOK_URL - Webhook 地址
#   DURESS_ALERT_EMAIL - 警报邮箱地址

# ---- 配置 ----
# 也可以直接在此处硬编码（不推荐，安全性更低）
WEBHOOK_URL="${DURESS_WEBHOOK_URL:-}"
ALERT_EMAIL="${DURESS_ALERT_EMAIL:-}"
# ---- 配置结束 ----

TIMESTAMP=$(date -u +"%Y-%m-%dT%H:%M:%SZ")
HOSTNAME=$(hostname)
USERNAME=$(whoami)

# 尝试获取公网 IP（超时 5 秒）
IP_ADDR=$(curl -s --connect-timeout 5 https://ifconfig.me 2>/dev/null || echo "unknown")

# 尝试获取地理位置
LOCATION=$(curl -s --connect-timeout 5 "https://ipinfo.io/${IP_ADDR}/city" 2>/dev/null || echo "unknown")

MESSAGE="⚠️ 胁迫警报 | 时间: ${TIMESTAMP} | 主机: ${HOSTNAME} | 用户: ${USERNAME} | IP: ${IP_ADDR} | 位置: ${LOCATION}"

# 通过 Webhook 发送 (兼容 Slack/Discord 格式)
if [[ -n "$WEBHOOK_URL" ]]; then
    curl -s --connect-timeout 10 \
        -X POST "$WEBHOOK_URL" \
        -H "Content-Type: application/json" \
        -d "{\"text\": \"${MESSAGE}\", \"content\": \"${MESSAGE}\"}" \
        2>/dev/null &
fi

# 通过邮件发送
if [[ -n "$ALERT_EMAIL" ]] && command -v mail &>/dev/null; then
    echo "$MESSAGE" | mail -s "胁迫警报 - ${HOSTNAME}" "$ALERT_EMAIL" 2>/dev/null &
fi

# 等待后台任务完成（最多 15 秒）
wait
