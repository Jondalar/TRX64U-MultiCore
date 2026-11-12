# Vivado 2024.1 (Artix-7 only) for batch builds on the NAS, tools baked in.
# Build context: a finished install tree (the directory that holds Vivado/),
# see build_image.sh. Left out by dockerignore: Vitis HLS, the installer
# client and Vivado's simulation data (xsim, secureip, simmodels), none of
# which a synthesis/implementation run reads.
FROM ubuntu:22.04
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends locales lsb-release ca-certificates curl python3 libc6-dev-i386 net-tools graphviz make unzip zip g++ libtinfo5 xvfb git libtool libyaml-dev libncurses5 libncurses5-dev libx11-6 libxext6 libxrender1 libxtst6 libxi6 libfreetype6 fontconfig && locale-gen en_US.UTF-8 && rm -rf /var/lib/apt/lists/*
ENV LANG=en_US.UTF-8 LC_ALL=en_US.UTF-8
COPY Vivado/2024.1 /opt/Xilinx/Vivado/2024.1
# What Vivado's .settings64-Vivado.sh sets (settings64.sh itself also wants
# the Vitis HLS tree, which is not in the image).
ENV XILINX_VIVADO=/opt/Xilinx/Vivado/2024.1 PATH=/opt/Xilinx/Vivado/2024.1/bin:$PATH
# Without libudev preloaded Vivado crashes right after route_design
# (free()/realloc(): invalid pointer); XILINX_LOCAL_USER_DATA=no keeps it
# from writing a per-user cache.
ENV XILINX_LOCAL_USER_DATA=no LD_PRELOAD=/lib/x86_64-linux-gnu/libudev.so.1
