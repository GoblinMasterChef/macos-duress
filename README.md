English | [中文](README_CN.md)

# macOS Duress Password Plugin (pam_duress)

When coerced into unlocking your macOS, entering a pre-configured "duress password" silently executes protective scripts in the background (wiping sensitive data, sending alerts, etc.), while the unlock failure appears completely normal to the coercer.

Three modes are supported:
- **Normal mode**: Scripts run in the background, unlock fails (default)
- **Unlock mode**: Scripts run synchronously, then the system unlocks normally (for scenarios where everything needs to appear normal)
- **Self-destruct mode**: Scripts run synchronously, then pam_duress is automatically uninstalled and all traces are removed, followed by a normal unlock

## How It Works

This plugin is a macOS PAM (Pluggable Authentication Module):

1. User enters a password at lock screen / sudo / terminal login
2. The `pam_duress` module checks if the password matches any pre-configured duress password
3. If matched:
   - **Normal mode**: Fork and execute script in background → return `PAM_IGNORE` → unlock fails
   - **Unlock mode**: Execute script synchronously → return `PAM_SUCCESS` → system unlocks
   - **Self-destruct mode**: Execute script synchronously → remove all pam_duress traces → return `PAM_SUCCESS` → system unlocks
4. If no match → return `PAM_IGNORE`, hand off to subsequent PAM modules for normal authentication

Password verification uses a `SHA256(password + script_content)` signature mechanism. Modifying the script content invalidates the signature (tamper-proof).

### Multi-Script and Password Matching Rules

**When multiple scripts are bound to the same password, all matching scripts will be executed.** The final authentication result is determined by the highest-priority mode:

| Priority | Mode | Execution | Return Value |
|----------|------|-----------|-------------|
| 3 (highest) | Self-destruct | Synchronous | `PAM_SUCCESS` |
| 2 | Unlock | Synchronous | `PAM_SUCCESS` |
| 1 (lowest) | Normal | Background | `PAM_IGNORE` |

For example, if the same password is bound to 3 scripts (normal + unlock + self-destruct), entering that password will:
1. Normal mode script → fork and execute in background (no wait)
2. Unlock mode script → execute synchronously (wait for completion)
3. Self-destruct mode script → execute synchronously (wait for completion)
4. Highest priority is self-destruct (3) → perform cleanup → return `PAM_SUCCESS` → system unlocks

**If a script is modified**, since the signature is `SHA256(password + script_content)`, the modified content will produce a signature that doesn't match the stored one. The script will be silently skipped — no execution, no errors, no logs, as if it doesn't exist. Use `duress_sign` to re-bind the password to restore functionality.

## Installation

### Prerequisites

- macOS 12+ (Monterey or later)
- Xcode Command Line Tools: `xcode-select --install`

### Install Steps

```bash
# Clone the project
git clone <repo-url> macosduress
cd macosduress

# Compile and install (requires root)
sudo ./install.sh
```

The installer will:
- Compile a universal binary (Intel + Apple Silicon)
- Install the module to `/usr/local/lib/pam/`
- Back up and modify PAM configuration files
- Install the `duress_sign` CLI tool

## Usage

### 1. Initialize

```bash
duress_sign --init
```

This creates the `~/.duress/` directory.

### 2. Create Duress Scripts

Use the example scripts or write your own:

```bash
# Use an example script
cp scripts/examples/wipe_data.sh ~/.duress/
chmod 500 ~/.duress/wipe_data.sh

# Or create a custom script
cat > ~/.duress/my_script.sh << 'EOF'
#!/bin/bash
# Your protective actions
rm -rf ~/Documents/Sensitive/
EOF
chmod 500 ~/.duress/my_script.sh
```

### 3. Bind a Duress Password

```bash
duress_sign ~/.duress/wipe_data.sh
# Enter the duress password (twice for confirmation)
```

Each script can be bound to a different duress password, or multiple scripts can share the same password.

### 4. Set Unlock Mode (Optional)

If you want the duress password to **unlock the system normally** after the script finishes (instead of failing), set unlock mode:

```bash
# Enable unlock mode
duress_sign --set-unlock ~/.duress/my_script.sh

# Disable unlock mode
duress_sign --unset-unlock ~/.duress/my_script.sh
```

After setting, a `.unlock` marker file is created in `~/.duress/`:

```
~/.duress/
├── wipe_data.sh           # Normal mode: background execution, unlock fails
├── wipe_data.sh.sha256
├── my_script.sh           # Unlock mode: synchronous execution, then unlock
├── my_script.sh.sha256
└── my_script.sh.unlock    # ← marker file, presence indicates unlock mode
```

### 5. Set Self-Destruct Mode (Optional)

If you want the duress password to **automatically uninstall pam_duress and remove all installation traces** after the script finishes, then unlock normally:

```bash
# Enable self-destruct mode
duress_sign --set-selfdestruct ~/.duress/destroy.sh

# Disable self-destruct mode
duress_sign --unset-selfdestruct ~/.duress/destroy.sh
```

> **Note**: Self-destruct mode and unlock mode are mutually exclusive. Setting self-destruct automatically removes the unlock marker, and vice versa.

After self-destruct is triggered, cleanup proceeds in this order:
1. Remove pam_duress lines from PAM configuration files
2. Delete all users' `~/.duress/` directories
3. Delete the system directory `/etc/duress.d/`
4. Delete installation backup files
5. Delete `pam_duress.so` and `duress_sign` binaries

Directory structure example:

```
~/.duress/
├── wipe_data.sh               # Normal mode
├── wipe_data.sh.sha256
├── destroy.sh                 # Self-destruct: sync execute → cleanup → unlock
├── destroy.sh.sha256
└── destroy.sh.selfdestruct    # ← self-destruct marker file
```

### 6. Verify and Manage

```bash
# List all configured scripts (shows [unlock] or [self-destruct] markers)
duress_sign --list

# Verify a password is correctly bound
duress_sign --verify ~/.duress/wipe_data.sh

# Remove a script's signature (also removes .unlock and .selfdestruct markers)
duress_sign --remove ~/.duress/wipe_data.sh
```

## Example Scripts

| Script | Function |
|--------|----------|
| `wipe_data.sh` | Securely delete sensitive files/directories, clear browser history, empty trash |
| `factory_reset.sh` | Full factory reset: wipe all user data, uninstall third-party apps and Homebrew packages (phase 1) + erase APFS data volume and reboot with passwordless sudo (phase 2) |
| `send_alert.sh` | Send duress alerts via webhook/email (with IP and location info) |

> **`factory_reset.sh` — Passwordless sudo setup**
>
> Phase 2 (system-level reset: erase data volume, remove `/Applications` third-party apps, reboot) only runs when the user has passwordless sudo. To enable it, run `sudo visudo` and add the following line at the end:
>
> ```
> your_username ALL=(ALL) NOPASSWD: ALL
> ```
>
> Replace `your_username` with your actual macOS username. Without this, only phase 1 (user-level cleanup) will execute.
| `lock_keychain.sh` | Lock the macOS Keychain |

## Uninstall

```bash
sudo ./uninstall.sh
```

## Security Notes

- **Script permissions**: Scripts must be set to `0500` (owner read+execute only), not writable by other users
- **Signature integrity**: Re-bind the password after modifying script content
- **System updates**: macOS system updates may reset PAM configuration files; re-run `sudo ./install.sh` after updates
- **Touch ID compatibility**: Touch ID unlock does not trigger duress checks (no password input)
- **Testing**: Test with `sudo` first to confirm everything works before relying on lock screen scenarios

## Technical Details

- PAM module is added to the auth chain as `auth sufficient`
- Normal mode returns `PAM_IGNORE`; unlock and self-destruct modes return `PAM_SUCCESS`
- Uses CommonCrypto (macOS native) for SHA-256 computation
- Normal mode uses double-fork for background execution; unlock/self-destruct modes use single fork + waitpid for synchronous execution
- Self-destruct cleanup is implemented in pure C (no shell fork), writes no syslog, and uses a best-effort strategy (individual step failures don't block subsequent steps)
- Uses `timingsafe_bcmp()` to prevent timing attacks
- Compiled as universal binary (x86_64 + arm64)

## References

This project's design is inspired by the following open-source projects:

- [nuvious/pam-duress](https://github.com/nuvious/pam-duress) — Linux PAM duress password module supporting per-user and global duress scripts with transparent shell access. The signature mechanism `SHA256(password + script_content)` and dual-directory structure (user/system) are based on this project's design.
- [rafket/pam_duress](https://github.com/rafket/pam_duress) — Another C implementation of a Linux PAM duress password module, providing the foundational architecture for triggering arbitrary actions (sending emails, deleting files, etc.) via duress passwords.
- [jcs/login_duress](https://github.com/jcs/login_duress) — A BSD authentication module for duress passwords, extending the duress password concept from PAM to the BSD auth framework.

This project adapts the above for macOS (using CommonCrypto, `-bundle` compilation, OpenPAM interface), and adds unlock mode and self-destruct mode.

## License

MIT License
