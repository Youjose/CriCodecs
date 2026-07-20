ARG MANYLINUX_IMAGE=quay.io/pypa/manylinux_2_28_x86_64@sha256:b04887b645dde99b9e955aeae3ff4da414992d0bd88259f046295b56361c5614

FROM ${MANYLINUX_IMAGE} AS gcc-builder

ARG GCC_VERSION=16.1.0
ARG GCC_SHA256=50efb4d94c3397aff3b0d61a5abd748b4dd31d9d3f2ab7be05b171d36a510f79
ARG PREFIX=/opt/gcc-16.1

RUN dnf -y install \
        bzip2 \
        diffutils \
        flex \
        gcc \
        gcc-c++ \
        gmp-devel \
        isl-devel \
        libmpc-devel \
        make \
        mpfr-devel \
        patch \
        tar \
        texinfo \
        wget \
        xz \
    && dnf clean all

RUN set -eux; \
    cd /tmp; \
    wget -q "https://ftp.gnu.org/gnu/gcc/gcc-${GCC_VERSION}/gcc-${GCC_VERSION}.tar.xz"; \
    echo "${GCC_SHA256}  gcc-${GCC_VERSION}.tar.xz" | sha256sum -c -; \
    tar -xf "gcc-${GCC_VERSION}.tar.xz"; \
    mkdir gcc-build; \
    cd gcc-build; \
    "../gcc-${GCC_VERSION}/configure" \
        --prefix="${PREFIX}" \
        --disable-multilib \
        --enable-languages=c,c++ \
        --disable-bootstrap; \
    make -j4; \
    make install-strip

FROM ${MANYLINUX_IMAGE}

ARG PREFIX=/opt/gcc-16.1

LABEL org.opencontainers.image.title="CriCodecs manylinux GCC toolchain" \
      org.opencontainers.image.description="Pinned manylinux_2_28 image with GCC 16.1" \
      org.opencontainers.image.version="gcc16.1-manylinux2.28"

COPY --from=gcc-builder ${PREFIX} ${PREFIX}

ENV PATH=/opt/gcc-16.1/bin:/opt/clang/bin:/usr/local/bin:/opt/rh/gcc-toolset-14/root/usr/bin:/usr/local/sbin:/usr/bin:/sbin:/bin
ENV LD_LIBRARY_PATH=/opt/gcc-16.1/lib64:/opt/rh/gcc-toolset-14/root/usr/lib64:/opt/rh/gcc-toolset-14/root/usr/lib:/opt/rh/gcc-toolset-14/root/usr/lib64/dyninst:/opt/rh/gcc-toolset-14/root/usr/lib/dyninst
ENV CC=/opt/gcc-16.1/bin/gcc
ENV CXX=/opt/gcc-16.1/bin/g++

RUN printf '#include <flat_map>\nint main() { std::flat_map<int, int> value; value[1] = 2; }\n' \
        | g++ -std=c++23 -O3 -flto -x c++ - -o /tmp/flat_map \
    && /tmp/flat_map \
    && rm -f /tmp/flat_map
