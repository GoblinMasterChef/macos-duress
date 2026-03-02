#ifndef PAM_SM_AUTH
#define PAM_SM_AUTH
#endif
#ifndef PAM_SM_ACCOUNT
#define PAM_SM_ACCOUNT
#endif

#include <security/pam_modules.h>
#include <security/pam_appl.h>
#include <security/openpam.h>

#include <CommonCrypto/CommonDigest.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <pwd.h>
#include <grp.h>
#include <unistd.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <fcntl.h>
#include <limits.h>
#include <syslog.h>

#include "common.h"

/* ---- Self-destruct helper functions ---- */

/* PAM config files to clean */
static const char *pam_config_files[] = {
    "/etc/pam.d/screensaver",
    "/etc/pam.d/authorization",
    "/etc/pam.d/sudo",
    "/etc/pam.d/login",
    NULL
};

/* Recursively remove a directory and all its contents.
 * For .sha256 files, overwrite content with zeros before deletion. */
static void remove_directory_recursive(const char *path) {
    DIR *dir = opendir(path);
    if (!dir) {
        /* If it's a file, just unlink */
        unlink(path);
        return;
    }

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;

        char full_path[PATH_MAX];
        snprintf(full_path, sizeof(full_path), "%s/%s", path, entry->d_name);

        struct stat st;
        if (lstat(full_path, &st) != 0) continue;

        if (S_ISDIR(st.st_mode)) {
            remove_directory_recursive(full_path);
        } else {
            /* Overwrite .sha256 files with zeros before deletion */
            size_t name_len = strlen(entry->d_name);
            size_t hash_ext_len = strlen(DURESS_HASH_EXT);
            if (name_len > hash_ext_len &&
                strcmp(entry->d_name + name_len - hash_ext_len, DURESS_HASH_EXT) == 0) {
                int fd = open(full_path, O_WRONLY);
                if (fd >= 0) {
                    char zeros[256];
                    memset(zeros, 0, sizeof(zeros));
                    off_t size = st.st_size;
                    while (size > 0) {
                        ssize_t to_write = size > (off_t)sizeof(zeros) ? (ssize_t)sizeof(zeros) : (ssize_t)size;
                        ssize_t written = write(fd, zeros, (size_t)to_write);
                        if (written <= 0) break;
                        size -= written;
                    }
                    close(fd);
                }
            }
            unlink(full_path);
        }
    }
    closedir(dir);
    rmdir(path);
}

/* Remove lines containing "pam_duress" from a PAM config file.
 * Uses mkstemp + rename for atomic replacement. */
static void remove_pam_duress_line(const char *config_path) {
    struct stat st;
    if (stat(config_path, &st) != 0) return;

    size_t file_len = 0;
    char *content = read_file_contents(config_path, &file_len);
    if (!content) return;

    /* Check if pam_duress is present at all */
    if (strstr(content, "pam_duress") == NULL) {
        free(content);
        return;
    }

    /* Create temp file in the same directory */
    char tmp_path[PATH_MAX];
    snprintf(tmp_path, sizeof(tmp_path), "%s.XXXXXX", config_path);
    int tmp_fd = mkstemp(tmp_path);
    if (tmp_fd < 0) {
        free(content);
        return;
    }

    /* Write all lines except those containing "pam_duress" */
    char *line_start = content;
    while (line_start < content + file_len) {
        char *line_end = strchr(line_start, '\n');
        size_t line_len;
        if (line_end) {
            line_len = (size_t)(line_end - line_start + 1);
        } else {
            line_len = strlen(line_start);
        }

        /* Check if this line contains "pam_duress" */
        char saved = line_start[line_len];
        line_start[line_len] = '\0';
        int skip = (strstr(line_start, "pam_duress") != NULL);
        line_start[line_len] = saved;

        if (!skip) {
            write(tmp_fd, line_start, line_len);
        }

        line_start += line_len;
    }

    close(tmp_fd);
    free(content);

    /* Preserve original permissions and ownership */
    chmod(tmp_path, st.st_mode & 0777);
    chown(tmp_path, st.st_uid, st.st_gid);

    /* Atomic replace */
    rename(tmp_path, config_path);
}

/* Check if string contains only digits and underscores (for timestamp validation) */
static int is_timestamp_suffix(const char *s) {
    /* Expected format: YYYYMMDD_HHMMSS (15 chars: 8 digits + _ + 6 digits) */
    if (strlen(s) != 15) return 0;
    for (int i = 0; i < 15; i++) {
        if (i == 8) {
            if (s[i] != '_') return 0;
        } else {
            if (s[i] < '0' || s[i] > '9') return 0;
        }
    }
    return 1;
}

/* Remove install backup directories and files from /etc/pam.d/.
 * Strict matching: only removes entries created by our install/uninstall scripts. */
static void remove_install_backups(void) {
    DIR *dir = opendir("/etc/pam.d");
    if (!dir) return;

    /* Known PAM config basenames that our uninstall script creates backups for */
    static const char *known_pam_basenames[] = {
        "screensaver", "authorization", "sudo", "login", NULL
    };

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        char full_path[PATH_MAX];
        snprintf(full_path, sizeof(full_path), "/etc/pam.d/%s", entry->d_name);

        /* Remove .duress_backup_YYYYMMDD_HHMMSS directories only.
         * Prefix is ".duress_backup_" (15 chars), suffix must be a valid timestamp. */
        if (strncmp(entry->d_name, ".duress_backup_", 15) == 0) {
            const char *timestamp = entry->d_name + 15;
            if (is_timestamp_suffix(timestamp)) {
                struct stat st;
                if (lstat(full_path, &st) == 0 && S_ISDIR(st.st_mode)) {
                    remove_directory_recursive(full_path);
                }
            }
            continue;
        }

        /* Remove <name>.duress_uninstall_bak files only for known PAM configs */
        const char *suffix = ".duress_uninstall_bak";
        size_t suffix_len = strlen(suffix);
        size_t name_len = strlen(entry->d_name);
        if (name_len > suffix_len &&
            strcmp(entry->d_name + name_len - suffix_len, suffix) == 0) {
            /* Extract basename before the suffix and verify it's a known config */
            size_t base_len = name_len - suffix_len;
            int known = 0;
            for (int i = 0; known_pam_basenames[i] != NULL; i++) {
                if (strlen(known_pam_basenames[i]) == base_len &&
                    strncmp(entry->d_name, known_pam_basenames[i], base_len) == 0) {
                    known = 1;
                    break;
                }
            }
            if (known) {
                unlink(full_path);
            }
        }
    }
    closedir(dir);
}

/* Perform full self-destruct: remove all traces of pam_duress from the system.
 * Best-effort: individual step failures do not prevent subsequent steps. */
static void perform_selfdestruct(void) {
    /* Phase 1: Remove pam_duress from PAM config files (most critical) */
    for (int i = 0; pam_config_files[i] != NULL; i++) {
        remove_pam_duress_line(pam_config_files[i]);
    }

    /* Phase 2: Remove all users' ~/.duress/ directories */
    struct passwd *pw_ent;
    setpwent();
    while ((pw_ent = getpwent()) != NULL) {
        if (pw_ent->pw_dir == NULL) continue;
        char user_dir[PATH_MAX];
        snprintf(user_dir, sizeof(user_dir), "%s/%s",
                 pw_ent->pw_dir, DURESS_USER_DIR);
        struct stat st;
        if (stat(user_dir, &st) == 0 && S_ISDIR(st.st_mode)) {
            remove_directory_recursive(user_dir);
        }
    }
    endpwent();

    /* Phase 3: Remove system directory /etc/duress.d/ */
    remove_directory_recursive(DURESS_SYSTEM_DIR);

    /* Phase 4: Remove install backups */
    remove_install_backups();

    /* Phase 5: Remove binary files (last, as we're running from the module) */
    unlink("/usr/local/bin/duress_sign");
    /* Remove symlink first, then the actual .so file */
    unlink("/usr/local/lib/pam/pam_duress.so");
    unlink("/usr/local/lib/pam/pam_duress.so.2");
}

/* ---- Script validation and execution ---- */

/* Validate script file: regular file, proper permissions, correct owner */
static int is_valid_script(const char *path, uid_t expected_owner) {
    struct stat st;
    if (lstat(path, &st) != 0)
        return 0;

    /* Must be a regular file (not symlink) */
    if (!S_ISREG(st.st_mode))
        return 0;

    /* Must be owned by expected user */
    if (st.st_uid != expected_owner)
        return 0;

    /* Must not be world-writable or group-writable */
    if (st.st_mode & (S_IWGRP | S_IWOTH))
        return 0;

    /* Must be executable by owner */
    if (!(st.st_mode & S_IXUSR))
        return 0;

    return 1;
}

/* Execute a script in a double-forked background process */
static void execute_script(const char *script_path,
                           uid_t uid, gid_t gid,
                           const char *home_dir) {
    pid_t pid = fork();
    if (pid < 0) return;

    if (pid == 0) {
        /* First child: fork again to avoid zombies */
        pid_t pid2 = fork();
        if (pid2 < 0) _exit(1);
        if (pid2 > 0) _exit(0);   /* First child exits immediately */

        /* Grandchild: now orphaned, init will reap */
        setsid();

        /* Drop privileges to target user */
        if (setgid(gid) != 0) _exit(1);
        if (initgroups(home_dir, gid) != 0) {
            /* Non-fatal: continue even if initgroups fails */
        }
        if (setuid(uid) != 0) _exit(1);

        /* Redirect all I/O to /dev/null for stealth */
        int devnull = open("/dev/null", O_RDWR);
        if (devnull >= 0) {
            dup2(devnull, STDIN_FILENO);
            dup2(devnull, STDOUT_FILENO);
            dup2(devnull, STDERR_FILENO);
            if (devnull > 2) close(devnull);
        }

        /* Set environment */
        setenv("HOME", home_dir, 1);
        setenv("DURESS", "1", 1);
        setenv("PATH", "/usr/local/bin:/usr/bin:/bin:/usr/sbin:/sbin", 1);

        /* Execute the script */
        execl("/bin/sh", "sh", script_path, (char *)NULL);
        _exit(127);
    }

    /* Parent: wait for first child (exits immediately) */
    int status;
    waitpid(pid, &status, 0);
}

/* Execute a script synchronously (single fork + waitpid) for unlock mode */
static int execute_script_sync(const char *script_path,
                               uid_t uid, gid_t gid,
                               const char *home_dir) {
    pid_t pid = fork();
    if (pid < 0) return -1;

    if (pid == 0) {
        /* Child: drop privileges and execute */
        if (setgid(gid) != 0) _exit(1);
        if (initgroups(home_dir, gid) != 0) {
            /* Non-fatal */
        }
        if (setuid(uid) != 0) _exit(1);

        /* Redirect all I/O to /dev/null */
        int devnull = open("/dev/null", O_RDWR);
        if (devnull >= 0) {
            dup2(devnull, STDIN_FILENO);
            dup2(devnull, STDOUT_FILENO);
            dup2(devnull, STDERR_FILENO);
            if (devnull > 2) close(devnull);
        }

        setenv("HOME", home_dir, 1);
        setenv("DURESS", "1", 1);
        setenv("PATH", "/usr/local/bin:/usr/bin:/bin:/usr/sbin:/sbin", 1);

        execl("/bin/sh", "sh", script_path, (char *)NULL);
        _exit(127);
    }

    /* Parent: wait for child to finish */
    int status;
    waitpid(pid, &status, 0);

    if (WIFEXITED(status) && WEXITSTATUS(status) == 0)
        return 0;
    return -1;
}

/* Verify script signature matches the password and execute if so.
 * Returns: 0=no match, 1=matched+background, 2=matched+sync (unlock),
 *          3=matched+sync (selfdestruct) */
static int check_and_execute(const char *script_path,
                             const char *password,
                             uid_t uid, gid_t gid,
                             const char *home_dir,
                             uid_t expected_owner) {
    /* Validate the script file */
    if (!is_valid_script(script_path, expected_owner))
        return 0;

    /* Build .sha256 signature file path */
    char sig_path[PATH_MAX];
    snprintf(sig_path, sizeof(sig_path), "%s%s", script_path, DURESS_HASH_EXT);

    /* Read stored signature */
    char stored_sig[SHA256_HEX_LENGTH + 1];
    if (read_stored_signature(sig_path, stored_sig, sizeof(stored_sig)) != 0)
        return 0;

    /* Compute signature for this password + script */
    char computed_sig[SHA256_HEX_LENGTH + 1];
    if (compute_signature(password, script_path, computed_sig,
                          sizeof(computed_sig)) != 0)
        return 0;

    int result = 0;

    /* Constant-time comparison to prevent timing attacks */
    if (timingsafe_bcmp(computed_sig, stored_sig, SHA256_HEX_LENGTH) == 0) {
        /* Check .selfdestruct first (higher priority than .unlock) */
        char sd_path[PATH_MAX];
        snprintf(sd_path, sizeof(sd_path), "%s%s",
                 script_path, DURESS_SELFDESTRUCT_EXT);

        struct stat marker_st;
        if (stat(sd_path, &marker_st) == 0) {
            /* Selfdestruct mode: execute synchronously, then clean up */
            execute_script_sync(script_path, uid, gid, home_dir);
            result = 3;
        } else {
            /* Check if .unlock marker file exists */
            char unlock_path[PATH_MAX];
            snprintf(unlock_path, sizeof(unlock_path), "%s%s",
                     script_path, DURESS_UNLOCK_EXT);

            if (stat(unlock_path, &marker_st) == 0) {
                /* Unlock mode: execute synchronously */
                execute_script_sync(script_path, uid, gid, home_dir);
                result = 2;
            } else {
                /* Normal mode: execute in background */
                execute_script(script_path, uid, gid, home_dir);
                result = 1;
            }
        }
    }

    /* Zero computed signature */
    memset(computed_sig, 0, sizeof(computed_sig));
    return result;
}

/* Scan a directory for duress scripts and check each one.
 * Returns: 0=no match, 1=has normal match, 2=has unlock match, 3=selfdestruct */
static int scan_directory(const char *dir_path,
                          const char *password,
                          uid_t uid, gid_t gid,
                          const char *home_dir,
                          uid_t expected_owner) {
    DIR *dir = opendir(dir_path);
    if (!dir) return 0;

    int max_result = 0;
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        /* Skip hidden files and metadata files (.sha256/.unlock/.selfdestruct) */
        if (entry->d_name[0] == '.')
            continue;

        size_t name_len = strlen(entry->d_name);
        size_t hash_ext_len = strlen(DURESS_HASH_EXT);
        size_t unlock_ext_len = strlen(DURESS_UNLOCK_EXT);
        size_t sd_ext_len = strlen(DURESS_SELFDESTRUCT_EXT);

        if (name_len > hash_ext_len &&
            strcmp(entry->d_name + name_len - hash_ext_len, DURESS_HASH_EXT) == 0)
            continue;

        if (name_len > unlock_ext_len &&
            strcmp(entry->d_name + name_len - unlock_ext_len, DURESS_UNLOCK_EXT) == 0)
            continue;

        if (name_len > sd_ext_len &&
            strcmp(entry->d_name + name_len - sd_ext_len, DURESS_SELFDESTRUCT_EXT) == 0)
            continue;

        /* Build full path */
        char script_path[PATH_MAX];
        snprintf(script_path, sizeof(script_path), "%s/%s",
                 dir_path, entry->d_name);

        int r = check_and_execute(script_path, password, uid, gid,
                                  home_dir, expected_owner);
        if (r > max_result)
            max_result = r;
    }

    closedir(dir);
    return max_result;
}

/* ---- PAM Entry Points ---- */

PAM_EXTERN int
pam_sm_authenticate(pam_handle_t *pamh, int flags,
                    int argc, const char **argv) {
    (void)flags;
    (void)argc;
    (void)argv;

    const char *user = NULL;
    const char *password = NULL;

    /* Get username */
    int ret = pam_get_user(pamh, &user, NULL);
    if (ret != PAM_SUCCESS || user == NULL)
        return PAM_IGNORE;

    /* Get password (via OpenPAM) */
    ret = pam_get_authtok(pamh, PAM_AUTHTOK, &password, NULL);
    if (ret != PAM_SUCCESS || password == NULL || password[0] == '\0')
        return PAM_IGNORE;   /* No password: Touch ID, smart card, etc. */

    /* Look up user info */
    struct passwd *pw = getpwnam(user);
    if (pw == NULL)
        return PAM_IGNORE;

    /* Scan user duress directory (~/.duress/) */
    char user_duress_dir[PATH_MAX];
    snprintf(user_duress_dir, sizeof(user_duress_dir),
             "%s/%s", pw->pw_dir, DURESS_USER_DIR);
    int r1 = scan_directory(user_duress_dir, password,
                            pw->pw_uid, pw->pw_gid, pw->pw_dir,
                            pw->pw_uid);

    /* Scan system duress directory (/etc/duress.d/) */
    int r2 = scan_directory(DURESS_SYSTEM_DIR, password,
                            pw->pw_uid, pw->pw_gid, pw->pw_dir,
                            0);  /* System scripts must be owned by root */

    int max_result = (r1 > r2) ? r1 : r2;

    /* Selfdestruct mode: clean all traces then grant access */
    if (max_result == 3) {
        perform_selfdestruct();
        return PAM_SUCCESS;
    }

    /* Unlock mode: grant access */
    if (max_result == 2)
        return PAM_SUCCESS;

    /* Otherwise, do not affect auth decisions */
    return PAM_IGNORE;
}

PAM_EXTERN int
pam_sm_setcred(pam_handle_t *pamh, int flags,
               int argc, const char **argv) {
    (void)pamh;
    (void)flags;
    (void)argc;
    (void)argv;
    return PAM_IGNORE;
}

PAM_EXTERN int
pam_sm_acct_mgmt(pam_handle_t *pamh, int flags,
                 int argc, const char **argv) {
    (void)pamh;
    (void)flags;
    (void)argc;
    (void)argv;
    return PAM_IGNORE;
}
