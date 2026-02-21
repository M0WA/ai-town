FROM node:20

ARG TZ
ENV TZ="$TZ"

ARG CLAUDE_CODE_VERSION=latest

# vcpkg baseline — must match builtin-baseline in vcpkg.json
ARG VCPKG_COMMIT=ce35b1a53aac26d7fcdb8ee1ef7a8e4eea02d27b

# Install basic development tools, iptables/ipset, and C++ build toolchain
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
  build-essential \
  cmake \
  ninja-build \
  pkg-config \
  lcov \
  xvfb \
  libgl1-mesa-dev \
  libasound2-dev \
  libpulse-dev \
  libx11-dev \
  libxxf86vm-dev \
  libxext-dev \
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

# Create workspace and config directories and set permissions
RUN mkdir -p /workspace /home/node/.claude && \
  chown -R node:node /workspace /home/node/.claude

WORKDIR /workspace

ARG GIT_DELTA_VERSION=0.18.2
RUN ARCH=$(dpkg --print-architecture) && \
  wget "https://github.com/dandavison/delta/releases/download/${GIT_DELTA_VERSION}/git-delta_${GIT_DELTA_VERSION}_${ARCH}.deb" && \
  sudo dpkg -i "git-delta_${GIT_DELTA_VERSION}_${ARCH}.deb" && \
  rm "git-delta_${GIT_DELTA_VERSION}_${ARCH}.deb"

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


# Copy and set up firewall script
#COPY init-firewall.sh /usr/local/bin/
#USER root
#RUN chmod +x /usr/local/bin/init-firewall.sh && \
#RUN  echo "node ALL=(root) NOPASSWD: /usr/local/bin/init-firewall.sh" > /etc/sudoers.d/node-firewall && \
#  chmod 0440 /etc/sudoers.d/node-firewall
USER node
