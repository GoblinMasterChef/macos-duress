#include <CommonCrypto/CommonDigest.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <pwd.h>
#include <dirent.h>
#include <limits.h>
#include <errno.h>

#include "common.h"

static void print_usage(const char *prog) {
    fprintf(stderr,
        "用法:\n"
        "  %s <script>                        绑定胁迫密码到脚本\n"
        "  %s --verify <script>               验证脚本签名\n"
        "  %s --list                          列出已配置的胁迫脚本\n"
        "  %s --remove <script>               移除脚本及签名\n"
        "  %s --set-unlock <script>           设置脚本为解锁模式\n"
        "  %s --unset-unlock <script>         取消脚本的解锁模式\n"
        "  %s --set-selfdestruct <script>     设置脚本为自毁模式（执行后清除所有痕迹）\n"
        "  %s --unset-selfdestruct <script>   取消脚本的自毁模式\n"
        "  %s --init                          初始化 ~/.duress/ 目录\n",
        prog, prog, prog, prog, prog, prog, prog, prog, prog);
}

/* Read password from terminal with echo disabled, or from pipe */
static char *read_password(const char *prompt) {
    int is_tty = isatty(STDIN_FILENO);
    struct termios old_term;

    if (is_tty) {
        if (tcgetattr(STDIN_FILENO, &old_term) != 0)
            return NULL;

        struct termios new_term = old_term;
        new_term.c_lflag &= ~(tcflag_t)ECHO;
        tcsetattr(STDIN_FILENO, TCSANOW, &new_term);
        fprintf(stderr, "%s", prompt);
    }

    char *line = NULL;
    size_t len = 0;
    ssize_t nread = getline(&line, &len, stdin);

    if (is_tty) {
        fprintf(stderr, "\n");
        tcsetattr(STDIN_FILENO, TCSANOW, &old_term);
    }

    if (nread <= 0) {
        free(line);
        return NULL;
    }

    /* Remove trailing newline */
    if (line[nread - 1] == '\n')
        line[nread - 1] = '\0';

    return line;
}

/* Get the user's duress directory path */
static int get_user_duress_dir(char *buf, size_t buflen) {
    const char *home = getenv("HOME");
    if (!home) {
        struct passwd *pw = getpwuid(getuid());
        if (!pw) return -1;
        home = pw->pw_dir;
    }
    snprintf(buf, buflen, "%s/%s", home, DURESS_USER_DIR);
    return 0;
}

/* Initialize the ~/.duress/ directory */
static int cmd_init(void) {
    char dir[PATH_MAX];
    if (get_user_duress_dir(dir, sizeof(dir)) != 0) {
        fprintf(stderr, "错误: 无法获取用户主目录\n");
        return 1;
    }

    struct stat st;
    if (stat(dir, &st) == 0) {
        if (S_ISDIR(st.st_mode)) {
            printf("目录已存在: %s\n", dir);
            return 0;
        }
        fprintf(stderr, "错误: %s 已存在但不是目录\n", dir);
        return 1;
    }

    if (mkdir(dir, 0700) != 0) {
        fprintf(stderr, "错误: 无法创建目录 %s: %s\n", dir, strerror(errno));
        return 1;
    }

    printf("已创建胁迫脚本目录: %s\n", dir);
    printf("\n下一步:\n");
    printf("  1. 将脚本放入 %s/\n", dir);
    printf("  2. 设置权限: chmod 500 %s/<script>\n", dir);
    printf("  3. 绑定密码: duress_sign %s/<script>\n", dir);
    return 0;
}

/* Sign a script: bind a duress password to it */
static int cmd_sign(const char *script_path) {
    /* Verify script exists and is executable */
    struct stat st;
    if (stat(script_path, &st) != 0) {
        fprintf(stderr, "错误: 找不到脚本 %s: %s\n", script_path, strerror(errno));
        return 1;
    }
    if (!S_ISREG(st.st_mode)) {
        fprintf(stderr, "错误: %s 不是普通文件\n", script_path);
        return 1;
    }
    if (!(st.st_mode & S_IXUSR)) {
        fprintf(stderr, "错误: %s 不可执行，请先运行: chmod +x %s\n",
                script_path, script_path);
        return 1;
    }

    /* Warn about permissions */
    if (st.st_mode & (S_IWGRP | S_IWOTH)) {
        fprintf(stderr, "警告: %s 可被其他用户写入，建议运行: chmod 500 %s\n",
                script_path, script_path);
    }

    /* Read password twice for confirmation */
    char *pass1 = read_password("请输入胁迫密码: ");
    if (!pass1) {
        fprintf(stderr, "错误: 无法读取密码\n");
        return 1;
    }

    if (strlen(pass1) == 0) {
        fprintf(stderr, "错误: 密码不能为空\n");
        memset(pass1, 0, strlen(pass1));
        free(pass1);
        return 1;
    }

    char *pass2 = read_password("请再次输入胁迫密码: ");
    if (!pass2) {
        memset(pass1, 0, strlen(pass1));
        free(pass1);
        fprintf(stderr, "错误: 无法读取密码\n");
        return 1;
    }

    if (strcmp(pass1, pass2) != 0) {
        fprintf(stderr, "错误: 两次输入的密码不一致\n");
        memset(pass1, 0, strlen(pass1));
        memset(pass2, 0, strlen(pass2));
        free(pass1);
        free(pass2);
        return 1;
    }

    /* Zero and free the second copy */
    memset(pass2, 0, strlen(pass2));
    free(pass2);

    /* Compute signature */
    char hex_sig[SHA256_HEX_LENGTH + 1];
    if (compute_signature(pass1, script_path, hex_sig, sizeof(hex_sig)) != 0) {
        fprintf(stderr, "错误: 无法计算签名\n");
        memset(pass1, 0, strlen(pass1));
        free(pass1);
        return 1;
    }

    /* Zero and free the password */
    memset(pass1, 0, strlen(pass1));
    free(pass1);

    /* Write signature to .sha256 file */
    char sig_path[PATH_MAX];
    snprintf(sig_path, sizeof(sig_path), "%s%s", script_path, DURESS_HASH_EXT);

    FILE *fp = fopen(sig_path, "w");
    if (!fp) {
        fprintf(stderr, "错误: 无法创建签名文件 %s: %s\n",
                sig_path, strerror(errno));
        return 1;
    }
    fprintf(fp, "%s\n", hex_sig);
    fclose(fp);

    /* Set signature file permissions to read-only */
    chmod(sig_path, 0400);

    printf("已成功绑定胁迫密码到脚本:\n");
    printf("  脚本: %s\n", script_path);
    printf("  签名: %s\n", sig_path);
    return 0;
}

/* Verify a script's signature against a password */
static int cmd_verify(const char *script_path) {
    char sig_path[PATH_MAX];
    snprintf(sig_path, sizeof(sig_path), "%s%s", script_path, DURESS_HASH_EXT);

    /* Check if signature file exists */
    struct stat st;
    if (stat(sig_path, &st) != 0) {
        fprintf(stderr, "错误: 找不到签名文件 %s\n", sig_path);
        return 1;
    }

    /* Read stored signature */
    char stored_sig[SHA256_HEX_LENGTH + 1];
    if (read_stored_signature(sig_path, stored_sig, sizeof(stored_sig)) != 0) {
        fprintf(stderr, "错误: 无法读取签名文件\n");
        return 1;
    }

    /* Read password */
    char *password = read_password("请输入胁迫密码进行验证: ");
    if (!password) {
        fprintf(stderr, "错误: 无法读取密码\n");
        return 1;
    }

    /* Compute signature */
    char computed_sig[SHA256_HEX_LENGTH + 1];
    if (compute_signature(password, script_path, computed_sig,
                          sizeof(computed_sig)) != 0) {
        fprintf(stderr, "错误: 无法计算签名\n");
        memset(password, 0, strlen(password));
        free(password);
        return 1;
    }

    memset(password, 0, strlen(password));
    free(password);

    if (timingsafe_bcmp(computed_sig, stored_sig, SHA256_HEX_LENGTH) == 0) {
        printf("验证成功: 密码与脚本签名匹配\n");
        return 0;
    } else {
        printf("验证失败: 密码与脚本签名不匹配\n");
        return 1;
    }
}

/* List all configured duress scripts */
static int list_scripts_in_dir(const char *dir_path, const char *label) {
    DIR *dir = opendir(dir_path);
    if (!dir) return 0;

    int count = 0;
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_name[0] == '.') continue;

        size_t name_len = strlen(entry->d_name);
        size_t hash_ext_len = strlen(DURESS_HASH_EXT);
        size_t unlock_ext_len = strlen(DURESS_UNLOCK_EXT);
        size_t sd_ext_len = strlen(DURESS_SELFDESTRUCT_EXT);

        /* Skip .sha256 files */
        if (name_len > hash_ext_len &&
            strcmp(entry->d_name + name_len - hash_ext_len, DURESS_HASH_EXT) == 0)
            continue;

        /* Skip .unlock files */
        if (name_len > unlock_ext_len &&
            strcmp(entry->d_name + name_len - unlock_ext_len, DURESS_UNLOCK_EXT) == 0)
            continue;

        /* Skip .selfdestruct files */
        if (name_len > sd_ext_len &&
            strcmp(entry->d_name + name_len - sd_ext_len, DURESS_SELFDESTRUCT_EXT) == 0)
            continue;

        char script_path[PATH_MAX];
        snprintf(script_path, sizeof(script_path), "%s/%s",
                 dir_path, entry->d_name);

        /* Check if signature file exists */
        char sig_path[PATH_MAX];
        snprintf(sig_path, sizeof(sig_path), "%s%s",
                 script_path, DURESS_HASH_EXT);

        /* Check if unlock marker exists */
        char unlock_path[PATH_MAX];
        snprintf(unlock_path, sizeof(unlock_path), "%s%s",
                 script_path, DURESS_UNLOCK_EXT);

        /* Check if selfdestruct marker exists */
        char sd_path[PATH_MAX];
        snprintf(sd_path, sizeof(sd_path), "%s%s",
                 script_path, DURESS_SELFDESTRUCT_EXT);

        struct stat st;
        int has_sig = (stat(sig_path, &st) == 0);
        int has_unlock = (stat(unlock_path, &st) == 0);
        int has_sd = (stat(sd_path, &st) == 0);

        struct stat script_st;
        int is_exec = 0;
        if (stat(script_path, &script_st) == 0) {
            is_exec = (script_st.st_mode & S_IXUSR) != 0;
        }

        printf("  [%s] %s/%s", label, dir_path, entry->d_name);
        if (has_sd) printf(" [自毁]");
        else if (has_unlock) printf(" [解锁]");
        if (!has_sig) printf(" (无签名)");
        if (!is_exec) printf(" (不可执行)");
        printf("\n");
        count++;
    }
    closedir(dir);
    return count;
}

static int cmd_list(void) {
    char user_dir[PATH_MAX];
    int total = 0;

    printf("已配置的胁迫脚本:\n\n");

    if (get_user_duress_dir(user_dir, sizeof(user_dir)) == 0) {
        total += list_scripts_in_dir(user_dir, "用户");
    }

    total += list_scripts_in_dir(DURESS_SYSTEM_DIR, "系统");

    if (total == 0) {
        printf("  (无)\n");
        printf("\n提示: 使用 'duress_sign --init' 初始化胁迫脚本目录\n");
    }

    return 0;
}

/* Set unlock mode for a script */
static int cmd_set_unlock(const char *script_path) {
    /* Verify script exists */
    struct stat st;
    if (stat(script_path, &st) != 0) {
        fprintf(stderr, "错误: 找不到脚本 %s: %s\n", script_path, strerror(errno));
        return 1;
    }

    /* Verify signature exists */
    char sig_path[PATH_MAX];
    snprintf(sig_path, sizeof(sig_path), "%s%s", script_path, DURESS_HASH_EXT);
    if (stat(sig_path, &st) != 0) {
        fprintf(stderr, "错误: 脚本尚未绑定密码，请先运行: duress_sign %s\n", script_path);
        return 1;
    }

    char unlock_path[PATH_MAX];
    snprintf(unlock_path, sizeof(unlock_path), "%s%s", script_path, DURESS_UNLOCK_EXT);

    if (stat(unlock_path, &st) == 0) {
        printf("脚本已处于解锁模式: %s\n", script_path);
        return 0;
    }

    /* Remove selfdestruct marker if present (mutually exclusive) */
    char sd_path[PATH_MAX];
    snprintf(sd_path, sizeof(sd_path), "%s%s", script_path, DURESS_SELFDESTRUCT_EXT);
    if (unlink(sd_path) == 0) {
        printf("已自动移除自毁标记（与解锁模式互斥）\n");
    }

    FILE *fp = fopen(unlock_path, "w");
    if (!fp) {
        fprintf(stderr, "错误: 无法创建标记文件 %s: %s\n", unlock_path, strerror(errno));
        return 1;
    }
    fclose(fp);
    chmod(unlock_path, 0400);

    printf("已设置解锁模式: %s\n", script_path);
    printf("触发此脚本的胁迫密码将同步执行脚本后解锁进入系统。\n");
    return 0;
}

/* Unset unlock mode for a script */
static int cmd_unset_unlock(const char *script_path) {
    char unlock_path[PATH_MAX];
    snprintf(unlock_path, sizeof(unlock_path), "%s%s", script_path, DURESS_UNLOCK_EXT);

    if (unlink(unlock_path) == 0) {
        printf("已取消解锁模式: %s\n", script_path);
        return 0;
    }

    if (errno == ENOENT) {
        printf("脚本未处于解锁模式: %s\n", script_path);
        return 0;
    }

    fprintf(stderr, "错误: 无法删除标记文件 %s: %s\n", unlock_path, strerror(errno));
    return 1;
}

/* Set selfdestruct mode for a script */
static int cmd_set_selfdestruct(const char *script_path) {
    /* Verify script exists */
    struct stat st;
    if (stat(script_path, &st) != 0) {
        fprintf(stderr, "错误: 找不到脚本 %s: %s\n", script_path, strerror(errno));
        return 1;
    }

    /* Verify signature exists */
    char sig_path[PATH_MAX];
    snprintf(sig_path, sizeof(sig_path), "%s%s", script_path, DURESS_HASH_EXT);
    if (stat(sig_path, &st) != 0) {
        fprintf(stderr, "错误: 脚本尚未绑定密码，请先运行: duress_sign %s\n", script_path);
        return 1;
    }

    char sd_path[PATH_MAX];
    snprintf(sd_path, sizeof(sd_path), "%s%s", script_path, DURESS_SELFDESTRUCT_EXT);

    if (stat(sd_path, &st) == 0) {
        printf("脚本已处于自毁模式: %s\n", script_path);
        return 0;
    }

    /* Remove unlock marker if present (mutually exclusive) */
    char unlock_path[PATH_MAX];
    snprintf(unlock_path, sizeof(unlock_path), "%s%s", script_path, DURESS_UNLOCK_EXT);
    if (unlink(unlock_path) == 0) {
        printf("已自动移除解锁标记（与自毁模式互斥）\n");
    }

    FILE *fp = fopen(sd_path, "w");
    if (!fp) {
        fprintf(stderr, "错误: 无法创建标记文件 %s: %s\n", sd_path, strerror(errno));
        return 1;
    }
    fclose(fp);
    chmod(sd_path, 0400);

    printf("已设置自毁模式: %s\n", script_path);
    printf("触发此脚本的胁迫密码将同步执行脚本，然后自动卸载 pam_duress 并清除所有痕迹。\n");
    return 0;
}

/* Unset selfdestruct mode for a script */
static int cmd_unset_selfdestruct(const char *script_path) {
    char sd_path[PATH_MAX];
    snprintf(sd_path, sizeof(sd_path), "%s%s", script_path, DURESS_SELFDESTRUCT_EXT);

    if (unlink(sd_path) == 0) {
        printf("已取消自毁模式: %s\n", script_path);
        return 0;
    }

    if (errno == ENOENT) {
        printf("脚本未处于自毁模式: %s\n", script_path);
        return 0;
    }

    fprintf(stderr, "错误: 无法删除标记文件 %s: %s\n", sd_path, strerror(errno));
    return 1;
}

/* Remove a script and its signature */
static int cmd_remove(const char *script_path) {
    char sig_path[PATH_MAX];
    snprintf(sig_path, sizeof(sig_path), "%s%s", script_path, DURESS_HASH_EXT);

    int removed = 0;

    if (unlink(sig_path) == 0) {
        printf("已删除签名文件: %s\n", sig_path);
        removed++;
    } else if (errno != ENOENT) {
        fprintf(stderr, "错误: 无法删除签名文件: %s\n", strerror(errno));
    }

    /* Also remove .unlock marker if present */
    char unlock_path[PATH_MAX];
    snprintf(unlock_path, sizeof(unlock_path), "%s%s", script_path, DURESS_UNLOCK_EXT);
    if (unlink(unlock_path) == 0) {
        printf("已删除解锁标记: %s\n", unlock_path);
        removed++;
    }

    /* Also remove .selfdestruct marker if present */
    char sd_path[PATH_MAX];
    snprintf(sd_path, sizeof(sd_path), "%s%s", script_path, DURESS_SELFDESTRUCT_EXT);
    if (unlink(sd_path) == 0) {
        printf("已删除自毁标记: %s\n", sd_path);
        removed++;
    }

    struct stat st;
    if (stat(script_path, &st) == 0) {
        fprintf(stderr, "注意: 脚本文件未删除: %s\n", script_path);
        fprintf(stderr, "如需删除脚本，请手动运行: rm %s\n", script_path);
    }

    if (removed == 0) {
        fprintf(stderr, "未找到可删除的文件\n");
        return 1;
    }

    return 0;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }

    if (strcmp(argv[1], "--init") == 0) {
        return cmd_init();
    }

    if (strcmp(argv[1], "--list") == 0) {
        return cmd_list();
    }

    if (strcmp(argv[1], "--verify") == 0) {
        if (argc < 3) {
            fprintf(stderr, "错误: --verify 需要脚本路径\n");
            return 1;
        }
        return cmd_verify(argv[2]);
    }

    if (strcmp(argv[1], "--remove") == 0) {
        if (argc < 3) {
            fprintf(stderr, "错误: --remove 需要脚本路径\n");
            return 1;
        }
        return cmd_remove(argv[2]);
    }

    if (strcmp(argv[1], "--set-unlock") == 0) {
        if (argc < 3) {
            fprintf(stderr, "错误: --set-unlock 需要脚本路径\n");
            return 1;
        }
        return cmd_set_unlock(argv[2]);
    }

    if (strcmp(argv[1], "--unset-unlock") == 0) {
        if (argc < 3) {
            fprintf(stderr, "错误: --unset-unlock 需要脚本路径\n");
            return 1;
        }
        return cmd_unset_unlock(argv[2]);
    }

    if (strcmp(argv[1], "--set-selfdestruct") == 0) {
        if (argc < 3) {
            fprintf(stderr, "错误: --set-selfdestruct 需要脚本路径\n");
            return 1;
        }
        return cmd_set_selfdestruct(argv[2]);
    }

    if (strcmp(argv[1], "--unset-selfdestruct") == 0) {
        if (argc < 3) {
            fprintf(stderr, "错误: --unset-selfdestruct 需要脚本路径\n");
            return 1;
        }
        return cmd_unset_selfdestruct(argv[2]);
    }

    if (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0) {
        print_usage(argv[0]);
        return 0;
    }

    if (argv[1][0] == '-') {
        fprintf(stderr, "未知选项: %s\n", argv[1]);
        print_usage(argv[0]);
        return 1;
    }

    /* Default: sign a script */
    return cmd_sign(argv[1]);
}
