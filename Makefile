.PHONY: release debug test test-integration-py test-integration-py-smoke clean

BUILD_DIR := build
VERSION ?= $(or $(shell git describe --tags --always 2>/dev/null | sed 's/^v//'),dev)

release:
	cmake -B $(BUILD_DIR) -G Ninja \
		-DCMAKE_BUILD_TYPE=Release \
		-DERPL_ADT_VERSION=$(VERSION) \
		-DCMAKE_TOOLCHAIN_FILE=$(VCPKG_ROOT)/scripts/buildsystems/vcpkg.cmake
	cmake --build $(BUILD_DIR)

debug:
	cmake -B $(BUILD_DIR) -G Ninja \
		-DCMAKE_BUILD_TYPE=Debug \
		-DERPL_ADT_VERSION=$(VERSION) \
		-DCMAKE_TOOLCHAIN_FILE=$(VCPKG_ROOT)/scripts/buildsystems/vcpkg.cmake
	cmake --build $(BUILD_DIR)

test: release
	cd $(BUILD_DIR) && ctest --output-on-failure

test-integration-py:
	cd test/integration_py && uv run pytest -v --tb=short

test-integration-py-smoke:
	cd test/integration_py && uv run pytest -v -m smoke

clean:
	rm -rf $(BUILD_DIR)
