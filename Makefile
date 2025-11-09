CXX = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -O2 -pthread



LDFLAGS =


ifeq ($(OS),Windows_NT)
# Windows-specific settings
LDFLAGS += -lws2_32 -lstdc++fs

SERVER_BIN = $(BUILD_DIR)/server.exe
CLIENT_BIN = $(BUILD_DIR)/client.exe
else
# POSIX (Linux/macOS) specific settings
# 'uname' will return 'Linux' or 'Darwin' (for macOS)
SERVER_BIN = $(BUILD_DIR)/server
CLIENT_BIN = $(BUILD_DIR)/client
endif


BUILD_DIR = build
SRC_DIR = .



SERVER_SRC = $(SRC_DIR)/server.cpp
CLIENT_SRC = $(SRC_DIR)/client.cpp



.PHONY: all clean


all: $(BUILD_DIR) $(SERVER_BIN) $(CLIENT_BIN)


$(BUILD_DIR):
	@mkdir -p $(BUILD_DIR)

$(SERVER_BIN): $(SERVER_SRC)
	$(CXX) $(CXXFLAGS) -o $@ $< $(LDFLAGS)
	@echo "Compiled Server: $@"

$(CLIENT_BIN): $(CLIENT_SRC)
	$(CXX) $(CXXFLAGS) -o $@ $< $(LDFLAGS)
	@echo "Compiled Client: $@"

clean:
	@rm -rf $(BUILD_DIR) *.exe
	@echo "Cleaned build artifacts."