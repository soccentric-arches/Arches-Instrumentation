# Arches Instrumentation — top-level developer entry point (REQ §14).
#
# There is no native x86 build here. The default target builds aarch64 —
# the Jetson fleet — inside the arm64 container in docker/ (Ubuntu 22.04
# arm64, the JetPack 6 userland generation), so the host needs nothing but
# docker. Binaries land in build-aarch64/ and run on-device as-is.
# Formatting and static-analysis gates run in the same container, so the
# toolchain is identical for every developer and for the device target.

JOBS ?= $(shell nproc)

DOCKER_DIR   := docker
DOCKER_IMAGE ?= arches/instrumentation-build
DOCKER_TAG   ?= aarch64

.DEFAULT_GOAL := build
.PHONY: help build aarch64 image test check-format format tidy shell clean

help:
	@echo 'Arches Instrumentation — make targets (all build aarch64 in the docker/ arm64 container):'
	@echo '  make build    default: full aarch64 build -> build-aarch64/'
	@echo '  make image    build the arm64 image only'
	@echo '  make test     run the test suite inside the arm64 container'
	@echo '  make check-format  clang-format gate inside the container'
	@echo '  make format        apply .clang-format inside the container'
	@echo '  make tidy     clang-tidy inside the container (slow under qemu)'
	@echo '  make shell    interactive shell in the arm64 container'
	@echo '  make clean    remove build-aarch64/'
	@echo
	@echo 'Variables: JOBS DOCKER_IMAGE DOCKER_TAG'

build:
	$(MAKE) -C $(DOCKER_DIR) build IMAGE=$(DOCKER_IMAGE) TAG=$(DOCKER_TAG) JOBS=$(JOBS)

# Alias kept from before the container flow became the default.
aarch64: build

image:
	$(MAKE) -C $(DOCKER_DIR) image IMAGE=$(DOCKER_IMAGE) TAG=$(DOCKER_TAG)

test:
	$(MAKE) -C $(DOCKER_DIR) test IMAGE=$(DOCKER_IMAGE) TAG=$(DOCKER_TAG) JOBS=$(JOBS)

check-format:
	$(MAKE) -C $(DOCKER_DIR) check-format IMAGE=$(DOCKER_IMAGE) TAG=$(DOCKER_TAG)

format:
	$(MAKE) -C $(DOCKER_DIR) format IMAGE=$(DOCKER_IMAGE) TAG=$(DOCKER_TAG)

tidy:
	$(MAKE) -C $(DOCKER_DIR) tidy IMAGE=$(DOCKER_IMAGE) TAG=$(DOCKER_TAG)

shell:
	$(MAKE) -C $(DOCKER_DIR) shell IMAGE=$(DOCKER_IMAGE) TAG=$(DOCKER_TAG)

clean:
	$(MAKE) -C $(DOCKER_DIR) clean