# Portico — build/test do núcleo portável (Linux/macOS).
# O app iOS é compilado via Xcode: `make gen-xcodeproj` e abra Portico.xcodeproj.
SHELL := /bin/bash
CC      ?= gcc
CFLAGS  ?= -std=c11 -Wall -Wextra -Werror=implicit-function-declaration -O2 -g -DPR_ENABLE_ZLIB=1
INC      = -ISources/PorticoRuntime/include
LIBS     = -lz -lm -lpthread
BUILD    = build

CSRC  := $(wildcard Sources/PorticoRuntime/src/*.c)
CTEST := $(wildcard Tests/PorticoRuntimeTests/*.c)

.PHONY: all c-test swift-test test gen-xcodeproj clean setup-toolchain

all: test

setup-toolchain:
	./scripts/setup_toolchain.sh

$(BUILD)/pr_tests: $(CSRC) $(CTEST)
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) $(INC) -o $@ $(CSRC) $(CTEST) $(LIBS)

c-test: $(BUILD)/pr_tests
	./$(BUILD)/pr_tests

swift-test:
	@source ./scripts/env.sh && swift test

test: c-test swift-test
	@echo "== TODOS OS TESTES PASSARAM =="

gen-xcodeproj:
	python3 scripts/gen_xcodeproj.py

clean:
	rm -rf $(BUILD) .build
