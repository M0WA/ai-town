# debian:trixie
FROM debian@sha256:348ca344599ceea2396cc6af4dc5c8477c39f53709f34ff3c6867b42260c70ef

# VCPKG_COMMIT must be supplied as a build-arg (no default — enforced by docker-ci-image.yml
# validation step, which compares this ARG against VCPKG_COMMIT_ID in ci.yml).
ARG VCPKG_COMMIT

# Install system dependencies in one RUN layer to keep the image lean.
# Packages (alphabetical): autoconf, automake, build-essential, ca-certificates,
#   ccache, curl, ffmpeg, g++-13, gcc-13, git, jq, lcov, libasound2-dev,
#   libgl1-mesa-dev, libglew-dev, libglu1-mesa-dev, libpulse-dev, libtool
#   (required by vcpkg alsa port), libx11-dev, libxext-dev, libxxf86vm-dev,
#   mesa-utils, ninja-build, pkg-config, python3, python3-pip, sox, tar,
#   unzip, vorbis-tools, xauth, xvfb, zip; gcovr installed via pip
RUN apt-get update && apt-get install -y --no-install-recommends \
    autoconf \
    automake \
    build-essential \
    ca-certificates \
    ccache \
    curl \
    ffmpeg \
    g++-13 \
    gcc-13 \
    git \
    jq \
    lcov \
    libasound2-dev \
    libgl1-mesa-dev \
    libglew-dev \
    libglu1-mesa-dev \
    libpulse-dev \
    libtool \
    libx11-dev \
    libxext-dev \
    libxxf86vm-dev \
    mesa-utils \
    ninja-build \
    pkg-config \
    python3 \
    python3-pip \
    sox \
    tar \
    unzip \
    vorbis-tools \
    xauth \
    xvfb \
    zip \
  && update-alternatives --install /usr/bin/gcc gcc /usr/bin/gcc-13 100 \
       --slave /usr/bin/g++ g++ /usr/bin/g++-13 \
  && update-alternatives --install /usr/bin/c++ c++ /usr/bin/g++-13 100 \
  && apt-get clean && rm -rf /var/lib/apt/lists/*

# Install Python packages required for asset validation.
# gcovr:   coverage report generation.
# mutagen: OGG/WAV metadata (validate_assets.py checks #16–#19, #21, #22).
# Pillow:  PNG dimension/mode check (validate_assets.py check #23, #24).
RUN pip3 install --break-system-packages \
    gcovr==8.6 \
    mutagen \
    Pillow

# Install CMake 3.31.10 (apt version in Debian Trixie is too old for vcpkg).
# Pinned to match the devcontainer.
ARG CMAKE_VERSION=3.31.10
RUN CMAKE_ARCH=$(uname -m) && \
  curl --proto '=https' --tlsv1.2 --fail --silent --location \
    --output "cmake-${CMAKE_VERSION}-linux-${CMAKE_ARCH}.tar.gz" \
    "https://github.com/Kitware/CMake/releases/download/v${CMAKE_VERSION}/cmake-${CMAKE_VERSION}-linux-${CMAKE_ARCH}.tar.gz" && \
  tar -xzf "cmake-${CMAKE_VERSION}-linux-${CMAKE_ARCH}.tar.gz" -C /opt && \
  ln -s "/opt/cmake-${CMAKE_VERSION}-linux-${CMAKE_ARCH}" /opt/cmake && \
  rm "cmake-${CMAKE_VERSION}-linux-${CMAKE_ARCH}.tar.gz"

ENV PATH=/opt/cmake/bin:$PATH

# Clone vcpkg and bootstrap at the pinned baseline commit.
# $VCPKG_COMMIT is supplied as a build-arg — must match VCPKG_COMMIT_ID in ci.yml.
RUN git clone https://github.com/microsoft/vcpkg.git /opt/vcpkg \
  && git -C /opt/vcpkg checkout "${VCPKG_COMMIT}" \
  && /opt/vcpkg/bootstrap-vcpkg.sh -disableMetrics

ENV VCPKG_ROOT=/opt/vcpkg
ENV VCPKG_DISABLE_METRICS=1
ENV PATH=$PATH:/opt/vcpkg

# Copy vcpkg overlay ports and manifest and pre-install all declared ports.
# The openal-soft overlay pins version 1.23.1 (avoids <format> GCC-12 requirement).
# Packages are installed to /opt/vcpkg_installed — a stable path baked into the image.
# CI jobs set -DVCPKG_INSTALLED_DIR=/opt/vcpkg_installed so cmake finds all packages
# already installed and skips recompilation entirely, regardless of ABI hash differences
# between the image build context and the runner environment.
COPY vcpkg-overlays/ /build/vcpkg-overlays/
COPY vcpkg.json /build/vcpkg.json
WORKDIR /build
RUN /opt/vcpkg/vcpkg install \
      --triplet x64-linux \
      --overlay-ports=/build/vcpkg-overlays \
      --x-install-root=/opt/vcpkg_installed

ENV VCPKG_INSTALLED_DIR=/opt/vcpkg_installed

# Add non-root CI user (UID/GID 1001 matches GitHub Actions runner UID — avoids
# git safe.directory UID mismatch without needing --user root in workflows).
RUN groupadd --gid 1001 ci && \
    useradd --uid 1001 --gid ci --shell /bin/bash --create-home ci && \
    chown -R ci:ci /opt/vcpkg_installed
USER ci

# Force Mesa software rendering (llvmpipe) — no GPU hardware in container.
ENV LIBGL_ALWAYS_SOFTWARE=1

WORKDIR /workspace
