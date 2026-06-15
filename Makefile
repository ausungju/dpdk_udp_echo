SERVER_APP = server
CLIENT_APP = client

BUILD_DIR ?= build
SRC_DIR := src
INC_DIR := include

SERVER_SRCS-y := $(SRC_DIR)/server/main.c \
				$(SRC_DIR)/server/cli.c \
				$(SRC_DIR)/server/server.c
CLIENT_SRCS-y := $(SRC_DIR)/client/main.c \
				$(SRC_DIR)/client/cli.c \
				$(SRC_DIR)/client/client.c
COMMON_SRCS-y := $(SRC_DIR)/common/cli.c \
				$(SRC_DIR)/common/runtime.c \
				$(SRC_DIR)/common/stats.c

COMMON_HDRS-y := $(INC_DIR)/common.h
SERVER_HDRS-y := $(INC_DIR)/server.h
CLIENT_HDRS-y := $(INC_DIR)/client.h

PKGCONF ?= pkg-config

ifneq ($(shell $(PKGCONF) --exists libdpdk && echo 0),0)
$(error "no installation of DPDK found")
endif

.PHONY: all server client clean re

all: server client

server: $(BUILD_DIR)/$(SERVER_APP)

client: $(BUILD_DIR)/$(CLIENT_APP)

PC_FILE := $(shell $(PKGCONF) --path libdpdk 2>/dev/null)
CFLAGS += -O3 -I$(INC_DIR) $(shell $(PKGCONF) --cflags libdpdk)
CFLAGS += -DALLOW_EXPERIMENTAL_API
LDFLAGS_SHARED = $(shell $(PKGCONF) --libs libdpdk)

$(BUILD_DIR)/$(SERVER_APP): $(SERVER_SRCS-y) $(COMMON_SRCS-y) $(COMMON_HDRS-y) $(SERVER_HDRS-y) Makefile $(PC_FILE) | $(BUILD_DIR)
	@$(CC) $(CFLAGS) $(SERVER_SRCS-y) $(COMMON_SRCS-y) -o $@ $(LDFLAGS) $(LDFLAGS_SHARED)

$(BUILD_DIR)/$(CLIENT_APP): $(CLIENT_SRCS-y) $(COMMON_SRCS-y) $(COMMON_HDRS-y) $(CLIENT_HDRS-y) Makefile $(PC_FILE) | $(BUILD_DIR)
	@$(CC) $(CFLAGS) $(CLIENT_SRCS-y) $(COMMON_SRCS-y) -o $@ $(LDFLAGS) $(LDFLAGS_SHARED)

$(BUILD_DIR):
	@mkdir -p $@

clean:
	@rm -f $(BUILD_DIR)/$(SERVER_APP) $(BUILD_DIR)/$(CLIENT_APP)
	@rmdir $(BUILD_DIR) 2>/dev/null || true

re: clean all
