#!/usr/bin/env bash
# shellcheck disable=SC1091 # no need to follow references to other shell scripts
# Bump the UBUNTU_AOSPLESS_TAG for changes in this file to take effect.

set -e

source "./.ci/setup-test-env.sh"

DEPS=(
  ca-certificates
  git
  wget
  xz-utils
)

DEPS_FOR_AOSP=(
  curl
  gpg
  gpg-agent
  ssh
)

DEPS_FOR_BUILD=(
  clang
  clang-19
  git
  lld
  llvm
  make
  meson
  pkg-config
  rsync
)

DEPS_FOR_TIDY=(
  clang-tidy-19
)

DEPS_FOR_CHECK=(
  blueprint-tools
  clang-format-19
)

export DEBIAN_FRONTEND=noninteractive

section_start install_packages "install_packages"
set -x
apt-get update
apt-get upgrade -y
apt-get install -y --no-remove --no-install-recommends "${DEPS[@]}"
apt-get install -y --no-remove --no-install-recommends "${DEPS_FOR_AOSP[@]}"
apt-get install -y --no-remove --no-install-recommends "${DEPS_FOR_BUILD[@]}"
apt-get install -y --no-remove --no-install-recommends "${DEPS_FOR_TIDY[@]}"
apt-get install -y --no-remove --no-install-recommends "${DEPS_FOR_CHECK[@]}"

set +x
section_end install_packages

wget https://gitlab.freedesktop.org/-/project/5/uploads/cafa930dad28acf7ee44d50101d5e8f0/aospless_drm_hwcomposer_arm64.tar.xz

sha256sum aospless_drm_hwcomposer_arm64.tar.xz
if echo f792b1140861112f80c8a3a22e1af8e3eccf4910fe4449705e62d2032b713bf9 aospless_drm_hwcomposer_arm64.tar.xz | sha256sum --check; then
    tar --no-same-owner -xf aospless_drm_hwcomposer_arm64.tar.xz -C /
else
    echo "Tar file check failed"
    exit 1
fi
