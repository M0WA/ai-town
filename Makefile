# AI Town - Convenience Makefile wrapping CMake/ctest
# Requires: VCPKG_ROOT env var set, cmake, ninja, ctest, lcov

BUILD_DIR        := build
PRESET           := ci-linux
COVERAGE_PRESET  := ci-linux-coverage
COVERAGE_INFO    := coverage.info
COVERAGE_FILTERED := coverage_filtered.info
COVERAGE_HTML    := coverage_html

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
	rm -rf $(BUILD_DIR) $(COVERAGE_INFO) $(COVERAGE_FILTERED) $(COVERAGE_HTML)

## Run unit + integration tests and generate an lcov coverage report
test:
	cmake --preset $(COVERAGE_PRESET)
	cmake --build $(BUILD_DIR)
	ctest --test-dir $(BUILD_DIR) -LE "integration|requires-opengl" --output-on-failure
	ctest --test-dir $(BUILD_DIR) -L "^integration$$" --output-on-failure
	lcov --capture --directory $(BUILD_DIR) --base-directory . \
	     --ignore-errors mismatch,inconsistent \
	     --output-file $(COVERAGE_INFO)
	lcov --remove $(COVERAGE_INFO) \
	     --ignore-errors unused \
	     '/usr/*' \
	     "*/.fetchcontent_cache/*" \
	     '*/tests/*' \
	     '*/mock_*.h' '*/mock_*.cpp' \
	     '*/manual_*.h' '*/manual_*.cpp' \
	     '*/Mock*.h' '*/Mock*.cpp' \
	     '*/Manual*.h' '*/Manual*.cpp' \
	     '*/src/rendering/*' '*/src/audio/*' '*/src/platform/*' \
	     --output-file $(COVERAGE_FILTERED)
	genhtml $(COVERAGE_FILTERED) --output-directory $(COVERAGE_HTML)
	lcov --summary $(COVERAGE_FILTERED)
