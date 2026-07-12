.PHONY: release debug test test-integration-py test-integration-py-smoke clean submodules

BUILD_DIR := build

# Ensure vendored deps (third_party/posthog-telemetry) are present. Best-effort:
# if there's no .git (tarball checkout) or no network, the build falls back to
# telemetry-compiled-out via the CMake guard.
submodules:
	@git submodule update --init --recursive 2>/dev/null || true
VERSION ?= $(or $(shell git describe --tags --always 2>/dev/null | sed 's/^v//'),dev)
JOBS ?= $(shell nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)

NINJA := $(shell command -v ninja 2>/dev/null)
ifdef NINJA
    CMAKE_GENERATOR := -G Ninja
else
    BUILD_PARALLEL := --parallel $(JOBS)
endif

release: submodules
	cmake -B $(BUILD_DIR) $(CMAKE_GENERATOR) \
		-DCMAKE_BUILD_TYPE=Release \
		-DERPL_ADT_VERSION=$(VERSION) \
		-DCMAKE_TOOLCHAIN_FILE=$(VCPKG_ROOT)/scripts/buildsystems/vcpkg.cmake
	cmake --build $(BUILD_DIR) $(BUILD_PARALLEL)

debug: submodules
	cmake -B $(BUILD_DIR) $(CMAKE_GENERATOR) \
		-DCMAKE_BUILD_TYPE=Debug \
		-DERPL_ADT_VERSION=$(VERSION) \
		-DCMAKE_TOOLCHAIN_FILE=$(VCPKG_ROOT)/scripts/buildsystems/vcpkg.cmake
	cmake --build $(BUILD_DIR) $(BUILD_PARALLEL)

test: release
	cd $(BUILD_DIR) && ctest --output-on-failure -j $(JOBS)

test-integration-py:
	cd test/integration_py && uv run pytest -v --tb=short

test-integration-py-smoke:
	cd test/integration_py && uv run pytest -v -m smoke

clean:
	rm -rf $(BUILD_DIR)
