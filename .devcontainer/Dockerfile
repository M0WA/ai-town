# CI base image — extends ghcr.io/m0wa/aitown-ci-linux.
# Both this file and ci.yml MUST pin identical digests (Atomicity Contract item 5).
FROM ghcr.io/m0wa/aitown-ci-linux@sha256:52f7ddf4d4130144f41f72466ffe4a3b85c662f73c509aa415b58f067fb3dc74

ARG TZ
ENV TZ="$TZ"

ARG CLAUDE_CODE_VERSION=latest

# Create node user (uid/gid 1000) to match the node:20 image convention
RUN groupadd --gid 1000 node \
  && useradd --uid 1000 --gid node --shell /bin/bash --create-home node

# Install Node.js 20 via NodeSource (nodistro works on any Debian/Ubuntu variant)
RUN apt-get update && apt-get install -y --no-install-recommends \
    ca-certificates curl gnupg \
  && mkdir -p /etc/apt/keyrings \
  && curl -fsSL --proto '=https' --tlsv1.2 https://deb.nodesource.com/gpgkey/nodesource-repo.gpg.key \
     | gpg --dearmor -o /etc/apt/keyrings/nodesource.gpg \
  && echo "deb [signed-by=/etc/apt/keyrings/nodesource.gpg] https://deb.nodesource.com/node_20.x nodistro main" \
     > /etc/apt/sources.list.d/nodesource.list \
  && apt-get update && apt-get install -y --no-install-recommends nodejs \
  && apt-get clean && rm -rf /var/lib/apt/lists/*

# Install developer-ergonomics packages not present in the CI base image.
# Excluded (already in CI image): git, curl, ca-certificates, jq, pkg-config,
#   build-essential, ccache, ninja-build, gcc-13, g++-13, xvfb, libgl1-mesa-dev,
#   libglu1-mesa-dev, libxxf86vm-dev, libx11-dev, libxext-dev, libglew-dev,
#   libasound2-dev, libpulse-dev, lcov, ffmpeg, sox, python3, python3-pip, vorbis-tools.
RUN apt-get update && apt-get install -y --no-install-recommends \
  less \
  procps \
  sudo \
  fzf \
  zsh \
  man-db \
  unzip \
  zip \
  gnupg2 \
  gh \
  iptables \
  ipset \
  iproute2 \
  dnsutils \
  aggregate \
  nano \
  vim \
  autoconf \
  automake \
  libtool \
  openssh-client \
  valgrind \
  binutils \
  xauth \
  libnvtt-bin \
  mediainfo \
  scrot \
  htop \
  tree \
  ripgrep \
  fd-find \
  bat \
  imagemagick \
  libimage-exiftool-perl \
  libmagickwand-dev \
  gdb \
  lldb \
  linux-perf \
  heaptrack \
  clang-format \
  clang-tidy \
  clangd \
  bear \
  pandoc \
  assimp-utils \
  pngcheck \
  python3-opencv \
  xxd \
  && apt-get clean && rm -rf /var/lib/apt/lists/* \
  && ln -sf /usr/bin/fdfind /usr/local/bin/fd \
  && (test -f /usr/bin/batcat && ln -sf /usr/bin/batcat /usr/local/bin/bat || true)

# Ensure default node user has access to /usr/local/share
RUN mkdir -p /usr/local/share/npm-global && \
  chown -R node:node /usr/local/share

ARG USERNAME=node

# Persist bash history.
RUN SNIPPET="export PROMPT_COMMAND='history -a' && export HISTFILE=/commandhistory/.bash_history" \
  && mkdir /commandhistory \
  && touch /commandhistory/.bash_history \
  && chown -R $USERNAME /commandhistory

# Set `DEVCONTAINER` environment variable to help with orientation
ENV DEVCONTAINER=true

# Force Mesa software rendering (llvmpipe) — no GPU hardware in container
ENV LIBGL_ALWAYS_SOFTWARE=1

# Create workspace and config directories and set permissions
RUN mkdir -p /workspace /home/node/.claude && \
  chown -R node:node /workspace /home/node/.claude

WORKDIR /workspace

ARG GIT_DELTA_VERSION=0.18.2
RUN ARCH=$(dpkg --print-architecture) && \
  curl --proto '=https' --tlsv1.2 --fail --location \
    --output "git-delta_${GIT_DELTA_VERSION}_${ARCH}.deb" \
    "https://github.com/dandavison/delta/releases/download/${GIT_DELTA_VERSION}/git-delta_${GIT_DELTA_VERSION}_${ARCH}.deb" && \
  dpkg -i "git-delta_${GIT_DELTA_VERSION}_${ARCH}.deb" && \
  rm "git-delta_${GIT_DELTA_VERSION}_${ARCH}.deb"

# AMD Compressonator CLI (amd64 only — no GPU required for CPU-based DXT compression)
ARG COMPRESSONATOR_VERSION=4.5.52
RUN ARCH=$(dpkg --print-architecture) && \
  if [ "$ARCH" = "amd64" ]; then \
    curl --proto '=https' --tlsv1.2 --fail --silent --location \
      --output "compressonatorcli_${COMPRESSONATOR_VERSION}_amd64.deb" \
      "https://github.com/GPUOpen-Tools/compressonator/releases/download/V${COMPRESSONATOR_VERSION}/compressonatorcli_${COMPRESSONATOR_VERSION}_amd64.deb" && \
    dpkg -i "compressonatorcli_${COMPRESSONATOR_VERSION}_amd64.deb" && \
    rm "compressonatorcli_${COMPRESSONATOR_VERSION}_amd64.deb"; \
  else \
    echo "Skipping compressonatorcli: only amd64 binaries available (arch=$ARCH)"; \
  fi

# actionlint — GitHub Actions workflow linter
ARG ACTIONLINT_VERSION=1.7.11
RUN ARCH=$(dpkg --print-architecture) && \
  curl --proto '=https' --tlsv1.2 --fail --silent --location \
    --output "actionlint_${ACTIONLINT_VERSION}_linux_${ARCH}.tar.gz" \
    "https://github.com/rhysd/actionlint/releases/download/v${ACTIONLINT_VERSION}/actionlint_${ACTIONLINT_VERSION}_linux_${ARCH}.tar.gz" && \
  tar -xzf "actionlint_${ACTIONLINT_VERSION}_linux_${ARCH}.tar.gz" actionlint && \
  mv actionlint /usr/local/bin/actionlint && \
  rm "actionlint_${ACTIONLINT_VERSION}_linux_${ARCH}.tar.gz"

# Blender headless (amd64 only — no GPU required for background Python export scripts)
ARG BLENDER_VERSION=4.3.2
RUN ARCH=$(dpkg --print-architecture) && \
  if [ "$ARCH" = "amd64" ]; then \
    BLENDER_MINOR="${BLENDER_VERSION%.*}" && \
    curl --proto '=https' --tlsv1.2 --fail --silent --location \
      --output "blender-${BLENDER_VERSION}-linux-x64.tar.xz" \
      "https://download.blender.org/release/Blender${BLENDER_MINOR}/blender-${BLENDER_VERSION}-linux-x64.tar.xz" && \
    tar -xJf "blender-${BLENDER_VERSION}-linux-x64.tar.xz" -C /opt && \
    mv "/opt/blender-${BLENDER_VERSION}-linux-x64" /opt/blender && \
    ln -sf /opt/blender/blender /usr/local/bin/blender && \
    rm "blender-${BLENDER_VERSION}-linux-x64.tar.xz"; \
  else \
    echo "Skipping Blender: only amd64 binaries available (arch=$ARCH)"; \
  fi

# NOTE: texdiag (DirectXTex DDS inspector) is Windows-only (links ole32/version).
# DDS inspection on Linux: use `identify -verbose file.dds` (ImageMagick).

# CMake is pre-installed in the CI base image at /opt/cmake/bin — PATH already set.

# Transfer ownership of vcpkg directories to the non-root user so that
# cmake --preset ci-linux can take vcpkg's write lock during configure.
# Both /opt/vcpkg (the toolchain) and /opt/vcpkg_installed (pre-built packages)
# are baked into the CI base image as root-owned; the node user needs write access.
RUN chown -R node:node /opt/vcpkg /opt/vcpkg_installed

# Set up non-root user
USER node

# Install global packages
ENV NPM_CONFIG_PREFIX=/usr/local/share/npm-global
ENV PATH=$PATH:/usr/local/share/npm-global/bin

# Set the default shell to zsh rather than sh
ENV SHELL=/bin/zsh

# Set the default editor and visual
ENV EDITOR=nano
ENV VISUAL=nano

# Default powerline10k theme
ARG ZSH_IN_DOCKER_VERSION=1.2.0
RUN sh -c "$(curl --proto '=https' --tlsv1.2 --fail --location https://github.com/deluan/zsh-in-docker/releases/download/v${ZSH_IN_DOCKER_VERSION}/zsh-in-docker.sh)" -- \
  -p git \
  -p fzf \
  -a "source /usr/share/doc/fzf/examples/key-bindings.zsh" \
  -a "source /usr/share/doc/fzf/examples/completion.zsh" \
  -a "export PROMPT_COMMAND='history -a' && export HISTFILE=/commandhistory/.bash_history" \
  -x

# Install Python packages
# numpy: terrain/cloud/atlas generation scripts
# Pillow: atlas cell coverage checks, billboard dimension validation, DDS mip header parsing
# trimesh: collision mesh pivot tolerance checks, footprint overflow detection
# Wand: ICC-preserving ImageMagick bindings for DXT5nm channel swizzle pipeline
# scipy: tileable terrain noise synthesis (gaussian_filter for seamless edge blending)
# pypng: byte-level PNG chunk introspection for sRGB/iCCP validation scripts
RUN pip3 install --break-system-packages numpy Pillow trimesh Wand scipy pypng

# Install Claude
RUN npm install -g --ignore-scripts @anthropic-ai/claude-code@${CLAUDE_CODE_VERSION}

USER node
