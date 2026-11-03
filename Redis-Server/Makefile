CXX ?= g++
CXXFLAGS = -std=c++17 -Wall -pthread -MMD -MP -O2 -Iinclude

SRC_DIR = src
EVICT_DIR = eviction
BUILD_DIR = build

SRCS := $(wildcard $(SRC_DIR)/*.cpp) $(wildcard $(EVICT_DIR)/*.cpp)
OBJS := $(patsubst %.cpp, $(BUILD_DIR)/%.o, $(SRCS))

TARGET = my_redis_server

ifeq ($(OS),Windows_NT)
    LDFLAGS = -lws2_32
    TARGET_BIN = my_redis_server.exe
else
    LDFLAGS =
    TARGET_BIN = my_redis_server
endif

all: $(TARGET_BIN)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)/$(SRC_DIR) $(BUILD_DIR)/$(EVICT_DIR)

$(BUILD_DIR)/%.o: %.cpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(TARGET_BIN): $(OBJS)
	$(CXX) $(CXXFLAGS) $(OBJS) -o $(TARGET_BIN) $(LDFLAGS)

clean:
	rm -rf $(BUILD_DIR) $(TARGET) my_redis_server.exe dump.my_rdb

rebuild: clean all

run: all
	./$(TARGET_BIN)
