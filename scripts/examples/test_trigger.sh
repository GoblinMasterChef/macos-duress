#!/bin/bash
# 测试脚本：触发胁迫密码后在临时目录输出一个文本文件
# 用于验证 pam_duress 是否正常工作，不会造成任何数据损失

echo "胁迫密码已触发 | 时间: $(date) | 用户: $(whoami)" > /tmp/duress_triggered.txt
