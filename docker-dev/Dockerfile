# syntax=docker/dockerfile:1
FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive

# ---------- Base toolchain & deps ----------
RUN apt update \
    && apt install -y --no-install-recommends \
    # build-essential \
    git cmake ninja-build python3 python3-pip \
    python3-venv \
    libedit-dev libxml2-dev libssl-dev \
    zstd zlib1g-dev libzstd-dev tree vim make pkg-config \ 
    wget curl lsb-release gnupg software-properties-common openssh-server openssh-client \
    && rm -rf /var/lib/apt/lists/*

# Add LLVM repository and install LLVM toolchain
RUN mkdir -p /etc/apt/keyrings \
    && curl -fsSL https://apt.llvm.org/llvm-snapshot.gpg.key | tee /etc/apt/keyrings/llvm-snapshot.gpg.key > /dev/null \
    && echo "deb [signed-by=/etc/apt/keyrings/llvm-snapshot.gpg.key] https://apt.llvm.org/noble/ llvm-toolchain-noble-20 main" | tee /etc/apt/sources.list.d/llvm20.list \
    && apt update \
    && apt install -y --no-install-recommends \
    clang-20 clang-format-20 clangd-20 clang-tidy-20 \
    libc++-20-dev libc++abi-20-dev \
    llvm-20 llvm-20-dev llvm-20-tools \
    && update-alternatives --install /usr/bin/clang clang /usr/bin/clang-20 200 \
    && update-alternatives --install /usr/bin/clang++ clang++ /usr/bin/clang++-20 200 \
    && update-alternatives --install /usr/bin/clang-format clang-format /usr/bin/clang-format-20 200 \
    && update-alternatives --install /usr/bin/clang-tidy clang-tidy /usr/bin/clang-tidy-20 200 \
    && rm -rf /var/lib/apt/lists/*

# ---------- Workspace ----------
WORKDIR /workspace

RUN mkdir -p /workspace/bin

CMD ["/bin/bash"]