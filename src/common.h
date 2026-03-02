#ifndef DURESS_COMMON_H
#define DURESS_COMMON_H

#include <CommonCrypto/CommonDigest.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>

#define DURESS_USER_DIR     ".duress"
#define DURESS_SYSTEM_DIR   "/etc/duress.d"
#define DURESS_HASH_EXT     ".sha256"
#define DURESS_UNLOCK_EXT   ".unlock"
#define DURESS_SELFDESTRUCT_EXT ".selfdestruct"
#define SHA256_HEX_LENGTH   64
#define MAX_PASSWORD_LEN    1024

/* Convert binary digest to hex string */
static inline void digest_to_hex(const unsigned char *digest, size_t digest_len,
                                  char *hex_out) {
    for (size_t i = 0; i < digest_len; i++) {
        snprintf(hex_out + (i * 2), 3, "%02x", digest[i]);
    }
    hex_out[digest_len * 2] = '\0';
}

/* Read entire file into malloc'd buffer, caller must free */
static inline char *read_file_contents(const char *path, size_t *out_len) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) return NULL;

    struct stat st;
    if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode)) {
        close(fd);
        return NULL;
    }

    size_t file_size = (size_t)st.st_size;
    char *buf = (char *)malloc(file_size + 1);
    if (!buf) {
        close(fd);
        return NULL;
    }

    size_t total = 0;
    while (total < file_size) {
        ssize_t n = read(fd, buf + total, file_size - total);
        if (n <= 0) {
            free(buf);
            close(fd);
            return NULL;
        }
        total += (size_t)n;
    }
    close(fd);

    buf[file_size] = '\0';
    if (out_len) *out_len = file_size;
    return buf;
}

/* Compute SHA256(password + file_content) -> hex string
 * Returns 0 on success, -1 on failure */
static inline int compute_signature(const char *password,
                                     const char *file_path,
                                     char *hex_out,
                                     size_t hex_out_len) {
    if (hex_out_len < SHA256_HEX_LENGTH + 1) return -1;

    size_t file_len = 0;
    char *file_content = read_file_contents(file_path, &file_len);
    if (!file_content) return -1;

    CC_SHA256_CTX ctx;
    CC_SHA256_Init(&ctx);
    CC_SHA256_Update(&ctx, password, (CC_LONG)strlen(password));
    CC_SHA256_Update(&ctx, file_content, (CC_LONG)file_len);

    unsigned char digest[CC_SHA256_DIGEST_LENGTH];
    CC_SHA256_Final(digest, &ctx);

    /* Zero sensitive data before freeing */
    memset(file_content, 0, file_len);
    free(file_content);

    digest_to_hex(digest, CC_SHA256_DIGEST_LENGTH, hex_out);
    return 0;
}

/* Read stored signature from .sha256 file, trim whitespace
 * Returns 0 on success, -1 on failure */
static inline int read_stored_signature(const char *sig_path,
                                         char *sig_out,
                                         size_t sig_out_len) {
    if (sig_out_len < SHA256_HEX_LENGTH + 1) return -1;

    size_t len = 0;
    char *content = read_file_contents(sig_path, &len);
    if (!content) return -1;

    /* Trim trailing whitespace/newline */
    while (len > 0 && (content[len - 1] == '\n' || content[len - 1] == '\r'
                        || content[len - 1] == ' ')) {
        content[--len] = '\0';
    }

    if (len != SHA256_HEX_LENGTH) {
        free(content);
        return -1;
    }

    memcpy(sig_out, content, SHA256_HEX_LENGTH);
    sig_out[SHA256_HEX_LENGTH] = '\0';
    free(content);
    return 0;
}

#endif /* DURESS_COMMON_H */
