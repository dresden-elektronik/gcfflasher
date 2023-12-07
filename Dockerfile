FROM ubuntu:18.04 as builder

RUN apt-get update && \
    apt-get install --no-install-recommends -y \
    lsb-release \
    ca-certificates \
    build-essential \
    pkg-config \
    libgpiod-dev \
    dpkg-dev \
    fakeroot \
    cmake

WORKDIR /src

COPY . /src/gcfflasher/

RUN mkdir /src/build && cd /src/build \
    && cmake -DCMAKE_BUILD_TYPE=Release ../gcfflasher \
    && cmake --build . \
    && cpack -G "DEB;TGZ" .

FROM scratch
WORKDIR /src
COPY --from=builder /src/build ./
