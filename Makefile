CC = clang
SDK_PATH := $(shell xcrun --show-sdk-path)

# Universal binary for Intel and Apple Silicon
ARCH_FLAGS = -arch x86_64 -arch arm64

CFLAGS = -Wall -Wextra -Werror -O2 \
         -isysroot $(SDK_PATH) \
         -fPIC \
         $(ARCH_FLAGS)

# PAM module: must use -bundle (not -dynamiclib)
LDFLAGS_MODULE = -bundle -lpam $(ARCH_FLAGS) -isysroot $(SDK_PATH)

# CLI tool: standard executable
LDFLAGS_CLI = $(ARCH_FLAGS) -isysroot $(SDK_PATH)

# CommonCrypto is part of libSystem, no extra -l needed

PREFIX = /usr/local
PAM_DIR = $(PREFIX)/lib/pam
BIN_DIR = $(PREFIX)/bin

.PHONY: all clean sign install uninstall

all: pam_duress.so duress_sign

pam_duress.so: src/pam_duress.c src/common.h
	$(CC) $(CFLAGS) -DPAM_SM_AUTH -DPAM_SM_ACCOUNT $(LDFLAGS_MODULE) -o $@ src/pam_duress.c

duress_sign: src/duress_sign.c src/common.h
	$(CC) $(CFLAGS) $(LDFLAGS_CLI) -o $@ src/duress_sign.c

sign: pam_duress.so duress_sign
	codesign -s - --force pam_duress.so
	codesign -s - --force duress_sign

clean:
	rm -f pam_duress.so duress_sign

install: all sign
	@echo "请使用 sudo ./install.sh 进行完整安装"
	@echo "或手动执行以下步骤:"
	@echo "  sudo mkdir -p $(PAM_DIR)"
	@echo "  sudo cp pam_duress.so $(PAM_DIR)/pam_duress.so.2"
	@echo "  sudo ln -sf pam_duress.so.2 $(PAM_DIR)/pam_duress.so"
	@echo "  sudo cp duress_sign $(BIN_DIR)/duress_sign"

uninstall:
	@echo "请使用 sudo ./uninstall.sh 进行完整卸载"
