# syntax=docker/dockerfile:1
FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive

# ---------- Base toolchain & deps ----------
RUN apt update \
    && apt install -y  \
    build-essential git cmake ninja-build python3 python3-pip \
    libedit-dev libxml2-dev libssl-dev \
    zstd zlib1g-dev wget curl lsb-release gnupg software-properties-common 

RUN  mkdir -p /etc/apt/keyrings
RUN curl -fsSL https://apt.llvm.org/llvm-snapshot.gpg.key | tee /etc/apt/keyrings/llvm-snapshot.gpg.key > /dev/null
RUN echo "deb [signed-by=/etc/apt/keyrings/llvm-snapshot.gpg.key] https://apt.llvm.org/noble/ llvm-toolchain-noble-20 main" |  tee /etc/apt/sources.list.d/llvm20.list
RUN apt update && apt install -y clang-20 clang-format clangd
RUN apt update && apt install -y libc++-20-dev libc++abi-20-dev
RUN update-alternatives --install /usr/bin/clang clang /usr/bin/clang-20 200
RUN update-alternatives --install /usr/bin/clang++ clang++ /usr/bin/clang++-20 200

# ---------- Workspace ----------
WORKDIR /workspace


RUN mkdir -p /workspace/bin

CMD ["!/bin/bash"]