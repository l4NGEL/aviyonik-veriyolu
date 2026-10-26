# Derleme, test, kapsama ve statik analiz ortamı (CI'daki ubuntu-24.04 ile aynı paketler)
FROM ubuntu:24.04
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
      build-essential gcc-14 g++-14 cmake ninja-build git ca-certificates \
      libgtest-dev cppcheck clang-tidy python3 \
      iproute2 can-utils \
    && rm -rf /var/lib/apt/lists/* \
    && update-alternatives --install /usr/bin/gcc gcc /usr/bin/gcc-14 140 \
       --slave /usr/bin/g++ g++ /usr/bin/g++-14 --slave /usr/bin/gcov gcov /usr/bin/gcov-14
WORKDIR /src
