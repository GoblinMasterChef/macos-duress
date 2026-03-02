[English](README.md) | 中文

# macOS 胁迫密码插件 (pam_duress)

当你被胁迫解锁 macOS 时，输入预设的"胁迫密码"可以在后台静默执行保护脚本（清除敏感数据、发送警报等），而对胁迫者来说，解锁失败表现完全正常。

支持三种模式：
- **普通模式**：脚本在后台执行，解锁失败（默认行为）
- **解锁模式**：脚本同步执行完毕后正常解锁进入系统（适用于需要看起来一切正常的场景）
- **自毁模式**：脚本同步执行完毕后，自动卸载 pam_duress 并清除所有痕迹，然后正常解锁进入系统

## 工作原理

本插件是一个 macOS PAM (Pluggable Authentication Module) 模块：

1. 用户在锁屏/sudo/终端登录时输入密码
2. `pam_duress` 模块检查密码是否匹配任何预设的胁迫密码
3. 如果匹配：
   - **普通模式**：后台 fork 执行脚本 → 返回 `PAM_IGNORE` → 解锁失败
   - **解锁模式**：同步等待脚本执行完毕 → 返回 `PAM_SUCCESS` → 正常解锁进入系统
   - **自毁模式**：同步执行脚本 → 清除所有 pam_duress 痕迹 → 返回 `PAM_SUCCESS` → 正常解锁
4. 如无匹配 → 返回 `PAM_IGNORE`，交给后续 PAM 模块正常认证

密码验证使用 `SHA256(密码 + 脚本内容)` 签名机制，修改脚本内容会使签名失效（防篡改）。

### 多脚本与密码匹配规则

**多个脚本绑定同一个密码时，所有匹配的脚本都会被执行**，最终的认证结果由优先级最高的模式决定：

| 优先级 | 模式 | 执行方式 | 返回值 |
|-------|------|---------|--------|
| 3（最高） | 自毁 | 同步执行 | `PAM_SUCCESS` |
| 2 | 解锁 | 同步执行 | `PAM_SUCCESS` |
| 1（最低） | 普通 | 后台执行 | `PAM_IGNORE` |

例如，同一密码绑定了 3 个脚本（普通 + 解锁 + 自毁），输入该密码后：
1. 普通模式脚本 → 后台 fork 执行（不等待）
2. 解锁模式脚本 → 同步执行（等待完成）
3. 自毁模式脚本 → 同步执行（等待完成）
4. 最高优先级为自毁（3）→ 执行自毁清理 → 返回 `PAM_SUCCESS` → 正常解锁

**脚本被修改后**，由于签名是 `SHA256(密码 + 脚本内容)`，修改后的内容会导致计算出的签名与存储的签名不匹配。该脚本会被静默跳过——不执行、不报错、不记录日志，如同不存在。需要用 `duress_sign` 重新绑定密码才能恢复。

## 安装

### 前置要求

- macOS 12+ (Monterey 或更新版本)
- Xcode 命令行工具：`xcode-select --install`

### 安装步骤

```bash
# 克隆项目
git clone <repo-url> macosduress
cd macosduress

# 编译并安装（需要 root 权限）
sudo ./install.sh
```

安装程序会：
- 编译 universal binary (Intel + Apple Silicon)
- 安装模块到 `/usr/local/lib/pam/`
- 备份并修改 PAM 配置文件
- 安装 `duress_sign` 命令行工具

## 使用方法

### 1. 初始化

```bash
duress_sign --init
```

这将创建 `~/.duress/` 目录。

### 2. 创建胁迫脚本

可以使用示例脚本或编写自己的脚本：

```bash
# 使用示例脚本
cp scripts/examples/wipe_data.sh ~/.duress/
chmod 500 ~/.duress/wipe_data.sh

# 或创建自定义脚本
cat > ~/.duress/my_script.sh << 'EOF'
#!/bin/bash
# 你的保护操作
rm -rf ~/Documents/Sensitive/
EOF
chmod 500 ~/.duress/my_script.sh
```

### 3. 绑定胁迫密码

```bash
duress_sign ~/.duress/wipe_data.sh
# 输入胁迫密码（两次确认）
```

每个脚本可以绑定不同的胁迫密码，也可以多个脚本使用同一个密码。

### 4. 设置解锁模式（可选）

如果希望胁迫密码触发后脚本执行完毕再**正常解锁进入系统**（而不是解锁失败），可以为脚本设置解锁模式：

```bash
# 设置解锁模式
duress_sign --set-unlock ~/.duress/my_script.sh

# 取消解锁模式
duress_sign --unset-unlock ~/.duress/my_script.sh
```

设置后，`~/.duress/` 目录下会创建一个 `.unlock` 标记文件：

```
~/.duress/
├── wipe_data.sh           # 普通模式：后台执行，解锁失败
├── wipe_data.sh.sha256
├── my_script.sh           # 解锁模式：同步执行完毕后解锁
├── my_script.sh.sha256
└── my_script.sh.unlock    # ← 标记文件，存在即为解锁模式
```

### 5. 设置自毁模式（可选）

如果希望胁迫密码触发后，脚本执行完毕后**自动卸载 pam_duress 并清除所有安装痕迹**，然后正常解锁进入系统：

```bash
# 设置自毁模式
duress_sign --set-selfdestruct ~/.duress/destroy.sh

# 取消自毁模式
duress_sign --unset-selfdestruct ~/.duress/destroy.sh
```

> **注意**：自毁模式与解锁模式互斥。设置自毁模式会自动移除解锁标记，反之亦然。

自毁触发后将按以下顺序清理：
1. 从 PAM 配置文件中移除 pam_duress 相关行
2. 删除所有用户的 `~/.duress/` 目录
3. 删除系统目录 `/etc/duress.d/`
4. 删除安装备份文件
5. 删除 `pam_duress.so` 和 `duress_sign` 二进制文件

目录结构示例：

```
~/.duress/
├── wipe_data.sh               # 普通模式
├── wipe_data.sh.sha256
├── destroy.sh                 # 自毁模式：同步执行 → 清理痕迹 → 解锁
├── destroy.sh.sha256
└── destroy.sh.selfdestruct    # ← 自毁标记文件
```

### 6. 验证和管理

```bash
# 列出所有已配置的脚本（会标注 [解锁] 或 [自毁]）
duress_sign --list

# 验证密码是否正确绑定
duress_sign --verify ~/.duress/wipe_data.sh

# 移除某个脚本的签名（也会清除 .unlock 和 .selfdestruct 标记）
duress_sign --remove ~/.duress/wipe_data.sh
```

## 示例脚本

| 脚本 | 功能 |
|------|------|
| `wipe_data.sh` | 安全删除敏感文件和目录、清空浏览器历史、回收站 |
| `factory_reset.sh` | 出厂重置：擦除所有用户数据、卸载第三方应用和 Homebrew 包（阶段一）+ 有免密 sudo 时擦除 APFS 数据卷并重启（阶段二） |
| `send_alert.sh` | 通过 Webhook/邮件发送胁迫警报（含 IP 和位置信息） |

> **`factory_reset.sh` — 免密 sudo 配置方法**
>
> 阶段二（系统级重置：擦除数据卷、卸载 `/Applications` 下第三方应用、重启）仅在用户拥有免密 sudo 权限时执行。配置方法：运行 `sudo visudo`，在文件末尾添加：
>
> ```
> 你的用户名 ALL=(ALL) NOPASSWD: ALL
> ```
>
> 将 `你的用户名` 替换为实际的 macOS 用户名。未配置时仅执行阶段一（用户级清理）。
| `lock_keychain.sh` | 锁定 macOS 钥匙串 |

## 卸载

```bash
sudo ./uninstall.sh
```

## 安全注意事项

- **脚本权限**：脚本必须设置为 `0500`（仅所有者可读可执行），不可被其他用户写入
- **签名完整性**：修改脚本内容后需重新绑定密码
- **系统更新**：macOS 系统更新可能重置 PAM 配置文件，更新后需重新运行 `sudo ./install.sh`
- **Touch ID 兼容**：使用 Touch ID 解锁时不会触发胁迫检查（无密码输入）
- **测试建议**：先用 `sudo` 命令测试，确认正常后再依赖锁屏场景

## 技术细节

- PAM 模块以 `auth sufficient` 方式加入认证链
- 普通模式返回 `PAM_IGNORE`，解锁模式和自毁模式返回 `PAM_SUCCESS`
- 使用 CommonCrypto (macOS 原生) 进行 SHA-256 计算
- 普通模式使用 double-fork 后台执行，解锁/自毁模式使用单 fork + waitpid 同步执行
- 自毁清理使用纯 C 实现（不 fork shell），不写 syslog，单步失败不中断后续清理
- 使用 `timingsafe_bcmp()` 防止时序攻击
- 编译为 universal binary (x86_64 + arm64)

## 参考项目

本项目的设计思路参考了以下开源项目：

- [nuvious/pam-duress](https://github.com/nuvious/pam-duress) — Linux PAM 胁迫密码模块，支持用户级和全局级胁迫脚本，密码验证后透明进入用户 shell。本项目的签名机制 `SHA256(密码 + 脚本内容)` 和双层目录结构（用户/系统）参考了该项目的设计。
- [rafket/pam_duress](https://github.com/rafket/pam_duress) — 另一个 Linux PAM 胁迫密码模块的 C 语言实现，提供了胁迫密码触发任意操作（发送邮件、删除文件等）的基础架构。
- [jcs/login_duress](https://github.com/jcs/login_duress) — BSD 认证模块的胁迫密码实现，将胁迫密码概念从 PAM 扩展到 BSD auth 框架。

本项目在上述项目基础上针对 macOS 进行了适配（使用 CommonCrypto、`-bundle` 编译、OpenPAM 接口），并新增了解锁模式和自毁模式。

## 许可证

MIT License
