FROM debian:trixie

ARG TZ
ENV TZ="$TZ"

ARG CLAUDE_CODE_VERSION=latest

# vcpkg baseline — must match builtin-baseline in vcpkg.json
ARG VCPKG_COMMIT=ce35b1a53aac26d7fcdb8ee1ef7a8e4eea02d27b

# Create node user (uid/gid 1000) to match the node:20 image convention
RUN groupadd --gid 1000 node \
  && useradd --uid 1000 --gid node --shell /bin/bash --create-home node

# Install Node.js 20 via NodeSource (nodistro works on any Debian/Ubuntu variant)
RUN apt-get update && apt-get install -y --no-install-recommends \
    ca-certificates curl gnupg \
  && mkdir -p /etc/apt/keyrings \
  && curl -fsSL https://deb.nodesource.com/gpgkey/nodesource-repo.gpg.key \
     | gpg --dearmor -o /etc/apt/keyrings/nodesource.gpg \
  && echo "deb [signed-by=/etc/apt/keyrings/nodesource.gpg] https://deb.nodesource.com/node_20.x nodistro main" \
     > /etc/apt/sources.list.d/nodesource.list \
  && apt-get update && apt-get install -y --no-install-recommends nodejs \
  && apt-get clean && rm -rf /var/lib/apt/lists/*

# Install basic development tools, iptables/ipset, and C++ build toolchain
# gcc-13 is the default compiler in Debian Trixie (testing)
RUN apt-get update && apt-get install -y --no-install-recommends \
  less \
  git \
  procps \
  sudo \
  fzf \
  zsh \
  man-db \
  unzip \
  zip \
  curl \
  wget \
  gnupg2 \
  gh \
  iptables \
  ipset \
  iproute2 \
  dnsutils \
  aggregate \
  jq \
  nano \
  vim \
  autoconf \
  automake \
  libtool \
  build-essential \
  ninja-build \
  pkg-config \
  openssh-client \
  lcov \
  xvfb \
  libgl1-mesa-dev \
  libglu1-mesa-dev \
  libasound2-dev \
  libpulse-dev \
  libx11-dev \
  libxxf86vm-dev \
  libxext-dev \
  gcc-13 \
  g++-13 \
  xauth \
  libnvtt-bin \
  && update-alternatives --install /usr/bin/gcc gcc /usr/bin/gcc-13 100 \
     --slave /usr/bin/g++ g++ /usr/bin/g++-13 \
  && update-alternatives --install /usr/bin/c++ c++ /usr/bin/g++-13 100 \
  && apt-get clean && rm -rf /var/lib/apt/lists/*

# Bootstrap vcpkg at the pinned baseline commit (matches vcpkg.json builtin-baseline).
# Packages are NOT pre-installed here — vcpkg manifest mode runs on first cmake configure.
# chown so the node user can run vcpkg install without sudo.
RUN git clone https://github.com/microsoft/vcpkg.git /opt/vcpkg \
  && git -C /opt/vcpkg checkout ${VCPKG_COMMIT} \
  && /opt/vcpkg/bootstrap-vcpkg.sh -disableMetrics \
  && chown -R node:node /opt/vcpkg

ENV VCPKG_ROOT=/opt/vcpkg
ENV VCPKG_DISABLE_METRICS=1
ENV PATH=$PATH:/opt/vcpkg

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
  wget "https://github.com/dandavison/delta/releases/download/${GIT_DELTA_VERSION}/git-delta_${GIT_DELTA_VERSION}_${ARCH}.deb" && \
  dpkg -i "git-delta_${GIT_DELTA_VERSION}_${ARCH}.deb" && \
  rm "git-delta_${GIT_DELTA_VERSION}_${ARCH}.deb"

# AMD Compressonator CLI (amd64 only — no GPU required for CPU-based DXT compression)
ARG COMPRESSONATOR_VERSION=4.5.52
RUN ARCH=$(dpkg --print-architecture) && \
  if [ "$ARCH" = "amd64" ]; then \
    wget -q "https://github.com/GPUOpen-Tools/compressonator/releases/download/V${COMPRESSONATOR_VERSION}/compressonatorcli_${COMPRESSONATOR_VERSION}_amd64.deb" && \
    dpkg -i "compressonatorcli_${COMPRESSONATOR_VERSION}_amd64.deb" && \
    rm "compressonatorcli_${COMPRESSONATOR_VERSION}_amd64.deb"; \
  else \
    echo "Skipping compressonatorcli: only amd64 binaries available (arch=$ARCH)"; \
  fi

# Install CMake 3.31.10 (apt version in Debian Trixie is too old for vcpkg)
ARG CMAKE_VERSION=3.31.10
RUN CMAKE_ARCH=$(uname -m) && \
  wget -q "https://github.com/Kitware/CMake/releases/download/v${CMAKE_VERSION}/cmake-${CMAKE_VERSION}-linux-${CMAKE_ARCH}.tar.gz" && \
  tar -xzf "cmake-${CMAKE_VERSION}-linux-${CMAKE_ARCH}.tar.gz" -C /opt && \
  ln -s "/opt/cmake-${CMAKE_VERSION}-linux-${CMAKE_ARCH}" /opt/cmake && \
  rm "cmake-${CMAKE_VERSION}-linux-${CMAKE_ARCH}.tar.gz"

ENV PATH=/opt/cmake/bin:$PATH

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
RUN sh -c "$(wget -O- https://github.com/deluan/zsh-in-docker/releases/download/v${ZSH_IN_DOCKER_VERSION}/zsh-in-docker.sh)" -- \
  -p git \
  -p fzf \
  -a "source /usr/share/doc/fzf/examples/key-bindings.zsh" \
  -a "source /usr/share/doc/fzf/examples/completion.zsh" \
  -a "export PROMPT_COMMAND='history -a' && export HISTFILE=/commandhistory/.bash_history" \
  -x

# Install Claude
RUN npm install -g @anthropic-ai/claude-code@${CLAUDE_CODE_VERSION}

USER node
