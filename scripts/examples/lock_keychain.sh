#!/bin/bash
# 胁迫脚本示例：锁定 macOS 钥匙串
# 锁定后需要重新输入密码才能访问保存的密码和证书

# 锁定默认钥匙串
security lock-keychain 2>/dev/null

# 锁定登录钥匙串
security lock-keychain "$HOME/Library/Keychains/login.keychain-db" 2>/dev/null

# 如需永久删除钥匙串（极端情况），取消下面的注释：
# security delete-keychain "$HOME/Library/Keychains/login.keychain-db" 2>/dev/null
