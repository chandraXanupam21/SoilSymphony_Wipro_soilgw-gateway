CXX      ?= g++
CXXFLAGS ?= -std=c++17 -O2 -g -Wall -Wextra -Wpedantic -pthread
CPPFLAGS += -Iinclude -Idaemon/src
BUILD    := build

LIB_SRC := $(addprefix daemon/src/,protocol.cpp node.cpp node_manager.cpp config.cpp logger.cpp gateway.cpp)
LIB_OBJ := $(LIB_SRC:%.cpp=$(BUILD)/%.o)

.PHONY: all test integration driver clean
all: $(BUILD)/soilgwd $(BUILD)/soilgw-sim $(BUILD)/soilgw-cli $(BUILD)/run_tests

$(BUILD)/%.o: %.cpp
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) -MMD -c $< -o $@

$(BUILD)/soilgwd: $(LIB_OBJ) $(BUILD)/daemon/src/main.o
	$(CXX) $(CXXFLAGS) $^ -o $@
$(BUILD)/soilgw-sim: $(BUILD)/tools/sim.o $(BUILD)/daemon/src/protocol.o
	$(CXX) $(CXXFLAGS) $^ -o $@
$(BUILD)/soilgw-cli: $(BUILD)/tools/cli.o
	$(CXX) $(CXXFLAGS) $^ -o $@
$(BUILD)/run_tests: $(LIB_OBJ) $(BUILD)/tests/test_main.o
	$(CXX) $(CXXFLAGS) $^ -o $@

test: $(BUILD)/run_tests
	$(BUILD)/run_tests
integration: all
	./scripts/integration_test.sh
driver:
	$(MAKE) -C driver
clean:
	rm -rf $(BUILD)
	-$(MAKE) -C driver clean 2>/dev/null

-include $(shell find $(BUILD) -name '*.d' 2>/dev/null)
