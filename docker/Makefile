# arm64 (aarch64) build container for Arches Instrumentation.
#
# The image is Ubuntu 22.04 arm64 — the Ubuntu generation JetPack 6 (L4T
# r36.x) runs on the Jetson fleet — so binaries produced inside link against
# the on-device glibc and run without a cross toolchain (see Dockerfile).
#
# On an x86_64 workstation the arm64 container runs under qemu-user; the
# `binfmt` target installs the kernel handlers (idempotent, needs
# --privileged once). On an aarch64 host (a Jetson) everything is native and
# binfmt is skipped automatically.

IMAGE      ?= arches/instrumentation-build
TAG        ?= aarch64
PLATFORM   ?= linux/arm64
BUILD_DIR  ?= build-aarch64
BUILD_TYPE ?= RelWithDebInfo
JOBS       ?= $(shell nproc)

ROOT        := $(abspath ..)
FULL_IMAGE  := $(IMAGE):$(TAG)
HOST_ARCH   := $(shell uname -m)

# Run as the invoking user so build outputs in the mounted repo are not
# root-owned; HOME under /tmp keeps tools that want a writable home happy.
RUN := docker run --rm --platform $(PLATFORM) \
       --user $(shell id -u):$(shell id -g) -e HOME=/tmp \
       -v $(ROOT):/src -w /src $(FULL_IMAGE)

.DEFAULT_GOAL := help
.PHONY: help image binfmt build test check-format format tidy verify shell clean

help:
	@echo 'arm64 build container — targets:'
	@echo '  make image   build the arm64 image ($(FULL_IMAGE))'
	@echo '  make build   cmake+Ninja build of the whole project -> /src/$(BUILD_DIR)'
	@echo '  make test    ctest inside the container (needs build first)'
	@echo '  make check-format  clang-format gate inside the container'
	@echo '  make format        apply .clang-format inside the container'
	@echo '  make tidy    clang-tidy inside the container (slow under qemu)'
	@echo '  make verify  show that the produced binaries are aarch64'
	@echo '  make shell   interactive shell in the container'
	@echo '  make binfmt  install qemu arm64 handlers (x86_64 hosts, once)'
	@echo '  make clean   remove $(BUILD_DIR)'
	@echo
	@echo 'Variables: IMAGE TAG PLATFORM BUILD_DIR BUILD_TYPE JOBS'

# qemu handlers first on x86_64: the arm64 build stages run under emulation
# and fail with "exec format error" without them. No-op on aarch64 hosts.
ifeq ($(HOST_ARCH),x86_64)
image: binfmt
endif
image:
	docker buildx build --platform $(PLATFORM) --load -f $(ROOT)/docker/Dockerfile -t $(FULL_IMAGE) $(ROOT)

binfmt:
	docker run --privileged --rm tonistiigi/binfmt --install arm64

build: image
	$(RUN) bash -ec '\
	  cmake -S /src -B /src/$(BUILD_DIR) -G Ninja \
	        -DCMAKE_BUILD_TYPE=$(BUILD_TYPE) -DBUILD_TESTING=ON \
	  && cmake --build /src/$(BUILD_DIR) -j $(JOBS)'

test: image
	$(RUN) bash -ec 'ctest --test-dir /src/$(BUILD_DIR) --output-on-failure'

# The §14 gates, in the same container and toolchain as the build. They only
# need a configured build dir, never a compiled one: configure (idempotent,
# cached when build-aarch64 already exists), then run the CMake target.
check-format: image
	$(RUN) bash -ec '\
	  cmake -S /src -B /src/$(BUILD_DIR) -G Ninja \
	        -DCMAKE_BUILD_TYPE=$(BUILD_TYPE) -DBUILD_TESTING=ON \
	  && cmake --build /src/$(BUILD_DIR) --target check-format'

format: image
	$(RUN) bash -ec '\
	  cmake -S /src -B /src/$(BUILD_DIR) -G Ninja \
	        -DCMAKE_BUILD_TYPE=$(BUILD_TYPE) -DBUILD_TESTING=ON \
	  && cmake --build /src/$(BUILD_DIR) --target format'

# clang-tidy over lib/, daemons/, tools/: every TU is re-analyzed, and under
# qemu on x86_64 hosts that is slow — expect this target to take a while.
tidy: image
	$(RUN) bash -ec '\
	  cmake -S /src -B /src/$(BUILD_DIR) -G Ninja \
	        -DCMAKE_BUILD_TYPE=$(BUILD_TYPE) -DBUILD_TESTING=ON \
	  && cmake --build /src/$(BUILD_DIR) --target run-clang-tidy'

# Proof the output really targets arm64: every executable must say aarch64.
verify:
	@non_aarch64=$$(find $(ROOT)/$(BUILD_DIR) -type f -executable \
	     \( -name 'instr-*' -o -name '*_ctrl' -o -name 'unit_*' -o -name 'integration_*' \) \
	     | xargs file | grep -v 'ARM aarch64' || true); \
	if [ -n "$$non_aarch64" ]; then \
	  echo 'NOT aarch64:'; echo "$$non_aarch64"; exit 1; \
	else \
	  echo 'all daemons/tools/tests in $(BUILD_DIR)/ are aarch64 ELF'; \
	fi

shell: image
	$(RUN) bash

clean:
	rm -rf $(ROOT)/$(BUILD_DIR)