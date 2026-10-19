CXX      = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -pthread -MMD -MP -O2
LDFLAGS  = -pthread

SRC_DIR   = src
BUILD_DIR = build
TEST_DIR  = tests

# Recursive wildcard. The previous rule used $(SRC_DIR)/*.cpp only, so
# src/eviction/*.cpp was never compiled and the shipped binary contained no
# eviction code at all.
SRCS := $(shell find $(SRC_DIR) -name '*.cpp')
OBJS := $(patsubst $(SRC_DIR)/%.cpp, $(BUILD_DIR)/%.o, $(SRCS))
DEPS := $(OBJS:.o=.d)

SERVER_TARGET  = my_redis_server
POLICY_TARGET  = $(BUILD_DIR)/test_eviction
SMOKE_TARGET   = $(BUILD_DIR)/test_integration

all: $(SERVER_TARGET)

$(SERVER_TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) $(OBJS) -o $@ $(LDFLAGS)

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -Iinclude -Iinclude/eviction -c $< -o $@

# Policy unit tests link only the eviction translation units, so they stay
# independent of the server.
$(POLICY_TARGET): $(TEST_DIR)/test_eviction.cpp $(filter $(BUILD_DIR)/eviction/%,$(OBJS))
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -Iinclude -Iinclude/eviction $< \
		$(filter $(BUILD_DIR)/eviction/%,$(OBJS)) -o $@ $(LDFLAGS)

# End-to-end tests against a live server: commands, eviction, persistence.
# Excludes main.o, since the test supplies its own entry point and drives the
# server directly.
$(SMOKE_TARGET): $(TEST_DIR)/test_integration.cpp $(filter-out $(BUILD_DIR)/main.o,$(OBJS))
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -Iinclude -Iinclude/eviction $< \
		$(filter-out $(BUILD_DIR)/main.o,$(OBJS)) -o $@ $(LDFLAGS)

test: $(POLICY_TARGET) $(SMOKE_TARGET)
	./$(POLICY_TARGET)
	./$(SMOKE_TARGET)

test-policy: $(POLICY_TARGET)
	./$(POLICY_TARGET)

test-integration: $(SMOKE_TARGET)
	./$(SMOKE_TARGET)

clean:
	rm -rf $(BUILD_DIR) $(SERVER_TARGET)

rebuild: clean all

run: all
	./$(SERVER_TARGET)

-include $(DEPS)

.PHONY: all clean rebuild run test test-policy test-integration