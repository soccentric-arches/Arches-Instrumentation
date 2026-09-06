# Arches Instrumentation — top-level developer entry point (REQ §14).
#
# Host targets use the Ninja-generator presets (ninja-host-*) for fast
# incremental builds. Jetson/aarch64 targets are delegated to docker/: an
# arm64 Ubuntu 22.04 container — the same Ubuntu generation as JetPack 6
# (L4T r36) — builds the binaries natively, so they run on-device as-is.
#
# Everything underneath is CMake with presets; this file only sequences the
# common invocations and never hides the underlying commands (they are all
# echoed by make).

NINJA_DEBUG   ?= ninja-host-debug
NINJA_RELEASE ?= ninja-host-release
JOBS          ?= $(shell nproc)

DOCKER_DIR   := docker
DOCKER_IMAGE ?= arches/instrumentation-build
DOCKER_TAG   ?= aarch64

.DEFAULT_GOAL := help
.PHONY: help build release test check-format format tidy \
        aarch64 aarch64-image aarch64-test aarch64-shell clean

help:
	@echo 'Arches Instrumentation — make targets:'
	@echo '  make build          host Debug build, Ninja (preset ninja-host-debug)'
	@echo '  make release        host Release build, Ninja'
	@echo '  make test           build + ctest (host, Debug)'
	@echo '  make check-format   clang-format gate (fails on violations)'
	@echo '  make format         apply .clang-format in place'
	@echo '  make tidy           run clang-tidy over lib/, daemons/, tools/'
	@echo '  make aarch64        build aarch64 binaries in the arm64 container'
	@echo '  make aarch64-image  build the arm64 image only (docker/)'
	@echo '  make aarch64-test   ctest inside the arm64 container'
	@echo '  make aarch64-shell  interactive shell in the arm64 container'
	@echo '  make clean          remove Ninja build dirs and the aarch64 output'
	@echo
	@echo 'Variables: JOBS NINJA_DEBUG NINJA_RELEASE DOCKER_IMAGE DOCKER_TAG'

build:
	cmake --preset $(NINJA_DEBUG)
	cmake --build --preset $(NINJA_DEBUG) -j $(JOBS)

release:
	cmake --preset $(NINJA_RELEASE)
	cmake --build --preset $(NINJA_RELEASE) -j $(JOBS)

test: build
	ctest --preset $(NINJA_DEBUG)

# The format/tidy targets live in CMakeLists.txt; they only need a configured
# build dir, so configure (idempotent) before invoking them.
check-format:
	cmake --preset $(NINJA_DEBUG)
	cmake --build --preset $(NINJA_DEBUG) --target check-format

format:
	cmake --preset $(NINJA_DEBUG)
	cmake --build --preset $(NINJA_DEBUG) --target format

tidy:
	cmake --preset $(NINJA_DEBUG)
	cmake --build --preset $(NINJA_DEBUG) --target run-clang-tidy

# ---- aarch64 (Jetson fleet): docker/ owns the arm64 container ----
aarch64:
	$(MAKE) -C $(DOCKER_DIR) build IMAGE=$(DOCKER_IMAGE) TAG=$(DOCKER_TAG) JOBS=$(JOBS)

aarch64-image:
	$(MAKE) -C $(DOCKER_DIR) image IMAGE=$(DOCKER_IMAGE) TAG=$(DOCKER_TAG)

aarch64-test:
	$(MAKE) -C $(DOCKER_DIR) test IMAGE=$(DOCKER_IMAGE) TAG=$(DOCKER_TAG) JOBS=$(JOBS)

aarch64-shell:
	$(MAKE) -C $(DOCKER_DIR) shell IMAGE=$(DOCKER_IMAGE) TAG=$(DOCKER_TAG)

# Scoped to what this Makefile creates: the Ninja host dirs and the
# container-built aarch64 output. Preset dirs from direct cmake use
# (build-host-debug, build-coverage, …) are left alone.
clean:
	rm -rf build-$(NINJA_DEBUG) build-$(NINJA_RELEASE) build-aarch64