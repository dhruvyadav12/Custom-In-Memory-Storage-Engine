CXX ?= g++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -O3 -pthread -Iinclude
LDFLAGS ?= -pthread

BUILD_DIR = build
BIN_DIR = bin

CORE_SRCS = $(wildcard src/network/*.cpp) \
            $(wildcard src/protocol/*.cpp) \
            $(wildcard src/storage/*.cpp) \
            $(wildcard src/threadpool/*.cpp)

CORE_OBJS = $(patsubst src/%.cpp,$(BUILD_DIR)/%.o,$(CORE_SRCS))

TARGETS = $(BIN_DIR)/storage_server $(BIN_DIR)/storage_client $(BIN_DIR)/bench_storage \
          $(BIN_DIR)/test_avl_tree $(BIN_DIR)/test_min_heap $(BIN_DIR)/test_tlv \
          $(BIN_DIR)/test_thread_pool $(BIN_DIR)/test_storage_engine

.PHONY: all clean test bench run

all: $(TARGETS)

$(BUILD_DIR)/%.o: src/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BIN_DIR)/storage_server: src/server_main.cpp $(CORE_OBJS)
	@mkdir -p $(BIN_DIR)
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LDFLAGS)

$(BIN_DIR)/storage_client: client/storage_client.cpp $(CORE_OBJS)
	@mkdir -p $(BIN_DIR)
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LDFLAGS)

$(BIN_DIR)/bench_storage: benchmark/bench_storage.cpp $(CORE_OBJS)
	@mkdir -p $(BIN_DIR)
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LDFLAGS)

$(BIN_DIR)/test_%: tests/test_%.cpp $(CORE_OBJS)
	@mkdir -p $(BIN_DIR)
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LDFLAGS)

test: $(BIN_DIR)/test_avl_tree $(BIN_DIR)/test_min_heap $(BIN_DIR)/test_tlv $(BIN_DIR)/test_thread_pool $(BIN_DIR)/test_storage_engine
	@echo "Running all test suites..."
	@$(BIN_DIR)/test_avl_tree
	@$(BIN_DIR)/test_min_heap
	@$(BIN_DIR)/test_tlv
	@$(BIN_DIR)/test_thread_pool
	@$(BIN_DIR)/test_storage_engine
	@echo "All unit tests passed successfully!"

bench: $(BIN_DIR)/bench_storage
	@echo "Running storage engine benchmark..."
	@$(BIN_DIR)/bench_storage -c 20 -n 1000 -t SET
	@$(BIN_DIR)/bench_storage -c 20 -n 1000 -t GET
	@$(BIN_DIR)/bench_storage -c 20 -n 1000 -t ZADD

run: $(BIN_DIR)/storage_server
	@$(BIN_DIR)/storage_server

clean:
	rm -rf $(BUILD_DIR) $(BIN_DIR) dump.tlv
