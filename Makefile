# AI Town - Convenience Makefile wrapping CMake/ctest
# Requires: VCPKG_ROOT env var set, cmake, ctest, lcov
# Inside devcontainer (ninja + ccache available): uses ci-linux / ci-linux-coverage presets.
# Outside devcontainer: auto-selects local / local-coverage presets (no ninja/ccache needed).

BUILD_DIR  := build
VCPKG_ROOT ?= /opt/vcpkg
export VCPKG_ROOT

# Auto-select preset based on available tools (override with PRESET=... on the command line).
# Uses ci-linux (Ninja + ccache) when both are present; falls back to local otherwise.
ifndef PRESET
  _HAVE_NINJA  := $(shell which ninja  2>/dev/null)
  _HAVE_CCACHE := $(shell which ccache 2>/dev/null)
  ifneq ($(and $(_HAVE_NINJA),$(_HAVE_CCACHE)),)
    PRESET := ci-linux
  else
    PRESET := local
  endif
endif

ifndef COVERAGE_PRESET
  ifeq ($(PRESET),ci-linux)
    COVERAGE_PRESET := ci-linux-coverage
  else
    COVERAGE_PRESET := local-coverage
  endif
endif
COVERAGE_INFO     := coverage.info
COVERAGE_FILTERED := coverage_filtered.info
COVERAGE_HTML     := coverage_html
# Minimum total line coverage % enforced by `make test`.
# Target range: 95–98%. Tests must stay above 95; aim for 98.
COVERAGE_MIN      := 95.0

DEBUG_DIR := build_debug

.PHONY: config build clean test debug

## Generate the CMake build configuration.
## Auto-selects ci-linux (Ninja+ccache) or local preset based on available tools.
## Override with: make config PRESET=ci-linux-coverage
config:
	cmake --preset $(PRESET)

## Build all binaries (runs config first if build/ is missing).
build: $(BUILD_DIR)/CMakeCache.txt
	cmake --build $(BUILD_DIR)

$(BUILD_DIR)/CMakeCache.txt:
	$(MAKE) config

## Build with debug symbols (RelWithDebInfo) into build_debug/.
## Enables core dump analysis: run with `ulimit -c unlimited` then `gdb ./build_debug/aitown core`.
debug:
	cmake -B $(DEBUG_DIR) -S . \
	  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
	  -DENABLE_COVERAGE=OFF \
	  -DCMAKE_TOOLCHAIN_FILE=$(VCPKG_ROOT)/scripts/buildsystems/vcpkg.cmake \
	  -DVCPKG_OVERLAY_PORTS=vcpkg-overlays
	cmake --build $(DEBUG_DIR) -- -j$$(nproc)

## Remove all build artifacts and coverage files.
clean:
	rm -rf $(BUILD_DIR) $(DEBUG_DIR) $(COVERAGE_INFO) $(COVERAGE_FILTERED) $(COVERAGE_HTML)

## Build with coverage, run unit + integration + OpenGL tests, generate lcov report,
## and enforce the $(COVERAGE_MIN)% total line coverage gate.
## Coverage report is written to $(COVERAGE_HTML)/index.html.
## If build/ already exists with ENABLE_COVERAGE=ON, the reconfigure step is skipped
## to save time; otherwise a full reconfigure is performed.
## Override the minimum with: make test COVERAGE_MIN=90.0
test:
	@if [ -f $(BUILD_DIR)/CMakeCache.txt ] && grep -q "ENABLE_COVERAGE:BOOL=ON\|ENABLE_COVERAGE=ON" $(BUILD_DIR)/CMakeCache.txt 2>/dev/null; then \
	  echo "Coverage build already configured — skipping reconfigure."; \
	else \
	  cmake --preset $(COVERAGE_PRESET); \
	fi
	cmake --build $(BUILD_DIR)
	ctest --test-dir $(BUILD_DIR) -LE "integration|requires-opengl" --output-on-failure
	ctest --test-dir $(BUILD_DIR) -L "^integration$$" --output-on-failure
	@if which xvfb-run > /dev/null 2>&1; then \
	  echo "xvfb-run found — running requires-opengl tests."; \
	  xvfb-run --auto-servernum ctest --test-dir $(BUILD_DIR) -L "^requires-opengl$$" --output-on-failure; \
	else \
	  echo "xvfb-run not found — skipping requires-opengl tests (not available in this environment)."; \
	fi
	lcov --capture --directory $(BUILD_DIR) --base-directory . \
	     --gcov-tool gcov-13 \
	     --ignore-errors mismatch,inconsistent,version,empty,path \
	     --output-file $(COVERAGE_INFO)
	lcov --remove $(COVERAGE_INFO) \
	     --ignore-errors unused,inconsistent \
	     '/usr/*' \
	     "/opt/vcpkg_installed/*" \
	     "*/build/vcpkg_installed/*" \
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
	@total=$$(awk '/^LH:/{lh+=substr($$0,4)+0} /^LF:/{lf+=substr($$0,4)+0} \
	  END{if(lf>0) printf "%.2f",lh/lf*100; else print 0}' $(COVERAGE_FILTERED)); \
	awk -v pct="$$total" -v min="$(COVERAGE_MIN)" 'BEGIN { \
	  if (pct+0 < min+0) { \
	    print "FAIL: total line coverage " pct "% < " min "% gate"; exit 1 \
	  } else { \
	    print "PASS: total line coverage " pct "% >= " min "%" \
	  } \
	}'
