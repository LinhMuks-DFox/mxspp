# syntax=docker/dockerfile:1
FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive

# ---------- Base toolchain & deps ----------
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential git cmake ninja-build python3 python3-pip \
    libedit-dev libz-dev libxml2-dev libssl-dev \
    llvm clang-format clangd ninja-build \
    && rm -rf /var/lib/apt/lists/*

# ---------- Workspace ----------
WORKDIR /workspace

# Download & build LLVM locally into ./lib
#   只复制脚本，不复制整个源码树；项目代码在运行时通过 volume 挂载
# COPY docker-dev/download_dep.py /tmp/download_dep.py
# RUN python3 /tmp/download_dep.py && rm /tmp/download_dep.py

# 预建输出目录（与 CMAKE_RUNTIME_OUTPUT_DIRECTORY 对齐）
RUN mkdir -p /workspace/bin

CMD ["/bin/bash"]