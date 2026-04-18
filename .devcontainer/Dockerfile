# CI base image — extends ghcr.io/m0wa/aitown-ci-linux.
# Both this file and ci.yml MUST pin identical digests (Atomicity Contract item 5).
FROM ghcr.io/m0wa/aitown-ci-linux@sha256:3339c8973fd67d052f82e1e193c5ffbe0258137adeef2165f7b2a07f999dca51
# The CI base image ends with USER ci. Switch to root for all build-time
# installs below; switch back to ci (with NOPASSWD sudo) at the end.
USER root

ARG TARGETARCH
ARG TZ
ENV TZ="$TZ"

ARG CLAUDE_CODE_VERSION=latest

# Install Node.js 20 via NodeSource, then install all dev-ergonomics packages in one layer.
# Excluded (already in CI image): git, curl, ca-certificates, jq, pkg-config,
#   build-essential, ccache, ninja-build, gcc-13, g++-13, xvfb, libgl1-mesa-dev,
#   libglu1-mesa-dev, libxxf86vm-dev, libx11-dev, libxext-dev, libglew-dev,
#   libasound2-dev, libpulse-dev, lcov, ffmpeg, sox, python3, python3-pip, vorbis-tools.
RUN apt-get update && apt-get install -y --no-install-recommends \
    ca-certificates curl gnupg \
  && mkdir -p /etc/apt/keyrings \
  && curl -fsSL --proto '=https' --tlsv1.2 https://deb.nodesource.com/gpgkey/nodesource-repo.gpg.key \
     | gpg --dearmor -o /etc/apt/keyrings/nodesource.gpg \
  && echo "deb [signed-by=/etc/apt/keyrings/nodesource.gpg] https://deb.nodesource.com/node_20.x nodistro main" \
     > /etc/apt/sources.list.d/nodesource.list \
  && apt-get update && apt-get install -y --no-install-recommends \
  aggregate \
  git-lfs \
  assimp-utils \
  autoconf \
  automake \
  bat \
  bear \
  binutils \
  clang-format \
  clang-tidy \
  clangd \
  dnsutils \
  fd-find \
  fzf \
  gdb \
  gh \
  gnupg2 \
  heaptrack \
  htop \
  imagemagick \
  iproute2 \
  ipset \
  iptables \
  less \
  libimage-exiftool-perl \
  libmagickwand-dev \
  libnvtt-bin \
  libtool \
  linux-perf \
  lldb \
  man-db \
  mediainfo \
  nano \
  nodejs \
  openssh-client \
  pandoc \
  pngcheck \
  procps \
  python3-opencv \
  ripgrep \
  scrot \
  sudo \
  tree \
  unzip \
  valgrind \
  vim \
  xauth \
  xxd \
  zip \
  zsh \
  && apt-get clean && rm -rf /var/lib/apt/lists/* \
  && ln -sf /usr/bin/fdfind /usr/local/bin/fd \
  && (test -f /usr/bin/batcat && ln -sf /usr/bin/batcat /usr/local/bin/bat || true)

# Grant ci user passwordless sudo (devcontainer only — not present in the CI image).
RUN echo "ci ALL=(ALL) NOPASSWD:ALL" > /etc/sudoers.d/ci && \
    chmod 0440 /etc/sudoers.d/ci

# Allow unlimited core dumps for the ci user (permanent — applied via PAM limits).
RUN echo "ci soft core unlimited" >> /etc/security/limits.conf && \
    echo "ci hard core unlimited" >> /etc/security/limits.conf

# Ensure ci user has access to /usr/local/share
RUN mkdir -p /usr/local/share/npm-global && \
  chown -R ci:ci /usr/local/share

ARG USERNAME=ci

# Persist bash history.
RUN SNIPPET="export PROMPT_COMMAND='history -a' && export HISTFILE=/commandhistory/.bash_history" \
  && mkdir /commandhistory \
  && touch /commandhistory/.bash_history \
  && chown -R "$USERNAME" /commandhistory

# Set `DEVCONTAINER` environment variable to help with orientation
ENV DEVCONTAINER=true

# Force Mesa software rendering (llvmpipe) — no GPU hardware in container
ENV LIBGL_ALWAYS_SOFTWARE=1

# Create workspace and config directories and set permissions
RUN mkdir -p /workspace /home/ci/.claude && \
  chown -R ci:ci /workspace /home/ci/.claude

WORKDIR /workspace

ARG GIT_DELTA_VERSION=0.18.2
RUN curl --proto '=https' --tlsv1.2 --fail --location \
    --output "git-delta_${GIT_DELTA_VERSION}_${TARGETARCH}.deb" \
    "https://github.com/dandavison/delta/releases/download/${GIT_DELTA_VERSION}/git-delta_${GIT_DELTA_VERSION}_${TARGETARCH}.deb" && \
  dpkg -i "git-delta_${GIT_DELTA_VERSION}_${TARGETARCH}.deb" && \
  rm "git-delta_${GIT_DELTA_VERSION}_${TARGETARCH}.deb"

# AMD Compressonator CLI (amd64 only — no GPU required for CPU-based DXT compression)
ARG COMPRESSONATOR_VERSION=4.5.52
RUN if [ "${TARGETARCH}" = "amd64" ]; then \
    curl --proto '=https' --tlsv1.2 --fail --silent --location \
      --output "compressonatorcli_${COMPRESSONATOR_VERSION}_amd64.deb" \
      "https://github.com/GPUOpen-Tools/compressonator/releases/download/V${COMPRESSONATOR_VERSION}/compressonatorcli_${COMPRESSONATOR_VERSION}_amd64.deb" && \
    dpkg -i "compressonatorcli_${COMPRESSONATOR_VERSION}_amd64.deb" && \
    rm "compressonatorcli_${COMPRESSONATOR_VERSION}_amd64.deb"; \
  else \
    echo "Skipping compressonatorcli: only amd64 binaries available (arch=${TARGETARCH})"; \
  fi

# actionlint — GitHub Actions workflow linter
ARG ACTIONLINT_VERSION=1.7.11
RUN curl --proto '=https' --tlsv1.2 --fail --silent --location \
    --output "actionlint_${ACTIONLINT_VERSION}_linux_${TARGETARCH}.tar.gz" \
    "https://github.com/rhysd/actionlint/releases/download/v${ACTIONLINT_VERSION}/actionlint_${ACTIONLINT_VERSION}_linux_${TARGETARCH}.tar.gz" && \
  tar -xzf "actionlint_${ACTIONLINT_VERSION}_linux_${TARGETARCH}.tar.gz" actionlint && \
  mv actionlint /usr/local/bin/actionlint && \
  rm "actionlint_${ACTIONLINT_VERSION}_linux_${TARGETARCH}.tar.gz"

# Blender headless (amd64 only — no GPU required for background Python export scripts)
ARG BLENDER_VERSION=4.3.2
RUN if [ "${TARGETARCH}" = "amd64" ]; then \
    BLENDER_MINOR="${BLENDER_VERSION%.*}" && \
    curl --proto '=https' --tlsv1.2 --fail --silent --location \
      --output "blender-${BLENDER_VERSION}-linux-x64.tar.xz" \
      "https://download.blender.org/release/Blender${BLENDER_MINOR}/blender-${BLENDER_VERSION}-linux-x64.tar.xz" && \
    tar -xJf "blender-${BLENDER_VERSION}-linux-x64.tar.xz" -C /opt && \
    mv "/opt/blender-${BLENDER_VERSION}-linux-x64" /opt/blender && \
    ln -sf /opt/blender/blender /usr/local/bin/blender && \
    rm "blender-${BLENDER_VERSION}-linux-x64.tar.xz"; \
  else \
    echo "Skipping Blender: only amd64 binaries available (arch=${TARGETARCH})"; \
  fi

# NOTE: texdiag (DirectXTex DDS inspector) is Windows-only (links ole32/version).
# DDS inspection on Linux: use `identify -verbose file.dds` (ImageMagick).

# CMake is pre-installed in the CI base image at /opt/cmake/bin — PATH already set.

# Transfer ownership of /opt/vcpkg to ci so cmake --preset ci-linux can take
# vcpkg's write lock during configure. /opt/vcpkg_installed is already ci-owned
# (baked into the CI base image).
RUN chown -R ci:ci /opt/vcpkg

USER ci

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
# tree-sitter + tree-sitter-cpp: cognitive complexity analyzer (tools/cognitive_complexity.py)
RUN pip3 install --break-system-packages numpy Pillow trimesh Wand scipy pypng tree-sitter tree-sitter-cpp

# Install Claude
RUN npm install -g --ignore-scripts "@anthropic-ai/claude-code@${CLAUDE_CODE_VERSION}"

USER ci
