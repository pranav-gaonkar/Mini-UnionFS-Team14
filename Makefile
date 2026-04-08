TARGET := mini_unionfs
SRCS := src/fuse_core.cpp src/read_ops.cpp src/write_ops.cpp src/deletion_ops.cpp src/path_resolution.cpp
BUILD_DIR := build
OBJS := $(SRCS:src/%.cpp=$(BUILD_DIR)/%.o)

CXX ?= g++
CXXFLAGS ?= -O2
CXXFLAGS += -std=c++17 -Wall -Wextra -Wpedantic
FUSE_CFLAGS := $(shell pkg-config fuse3 --cflags 2>/dev/null)
FUSE_LIBS := $(shell pkg-config fuse3 --libs 2>/dev/null)
CXXFLAGS += $(FUSE_CFLAGS)
LDFLAGS += $(FUSE_LIBS)

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(OBJS) $(LDFLAGS) -o $@

$(BUILD_DIR):
	@mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/%.o: src/%.cpp src/common.h | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -rf $(BUILD_DIR) $(TARGET)

.PHONY: all clean
