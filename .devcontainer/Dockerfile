FROM mcr.microsoft.com/devcontainers/cpp:1-ubuntu-20.04

ARG REINSTALL_CMAKE_VERSION_FROM_SOURCE="3.27.7"

COPY ./reinstall-cmake.sh /tmp/

COPY ./enable-hugepages.sh /tmp/

RUN if [ "${REINSTALL_CMAKE_VERSION_FROM_SOURCE}" != "none" ]; then \
        chmod +x /tmp/reinstall-cmake.sh && /tmp/reinstall-cmake.sh ${REINSTALL_CMAKE_VERSION_FROM_SOURCE}; \
    fi \
    && rm -f /tmp/reinstall-cmake.sh

RUN apt-get update && export DEBIAN_FRONTEND=noninteractive \
    && apt-get -y install --no-install-recommends gcc-10 g++-10 libjsoncpp-dev \
       python3 python3-pip python3-setuptools python3-wheel ninja-build \
    && pip3 install meson pyelftools

RUN wget https://fast.dpdk.org/rel/dpdk-21.11.5.tar.xz \
    && tar xJf dpdk-21.11.5.tar.xz && rm -f dpdk-21.11.5.tar.xz && cd dpdk-stable-21.11.5/ \
    && meson setup build && cd build && ninja && meson install && ldconfig

