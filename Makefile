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

.PHONY: config build clean test

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

## Remove all build artifacts and coverage files.
clean:
	rm -rf $(BUILD_DIR) $(COVERAGE_INFO) $(COVERAGE_FILTERED) $(COVERAGE_HTML)

## Build with coverage, run unit + integration tests, generate lcov report,
## and enforce the $(COVERAGE_MIN)% total line coverage gate.
## Coverage report is written to $(COVERAGE_HTML)/index.html.
test:
	cmake --preset $(COVERAGE_PRESET)
	cmake --build $(BUILD_DIR)
	ctest --test-dir $(BUILD_DIR) -LE "integration|requires-opengl" --output-on-failure
	ctest --test-dir $(BUILD_DIR) -L "^integration$$" --output-on-failure
	lcov --capture --directory $(BUILD_DIR) --base-directory . \
	     --gcov-tool gcov-13 \
	     --ignore-errors mismatch,inconsistent,version,empty,empty \
	     --output-file $(COVERAGE_INFO)
	lcov --remove $(COVERAGE_INFO) \
	     --ignore-errors unused,inconsistent \
	     '/usr/*' \
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
