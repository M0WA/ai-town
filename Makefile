# AI Town - Convenience Makefile wrapping CMake/ctest
# Requires: VCPKG_ROOT env var set, cmake, ninja, ctest

BUILD_DIR := build
PRESET    := ci-linux

.PHONY: config build clean test

## Generate the CMake build configuration (uses the ci-linux preset)
config:
	cmake --preset $(PRESET)

## Build all binaries (runs config first if build dir is missing)
build: $(BUILD_DIR)/build.ninja
	cmake --build $(BUILD_DIR)

$(BUILD_DIR)/build.ninja:
	$(MAKE) config

## Remove all build artifacts
clean:
	rm -rf $(BUILD_DIR)

## Run unit tests and integration tests
test: $(BUILD_DIR)/build.ninja
	ctest --test-dir $(BUILD_DIR) -LE "integration|requires-opengl" --output-on-failure
	ctest --test-dir $(BUILD_DIR) -L "^integration$$" --output-on-failure
