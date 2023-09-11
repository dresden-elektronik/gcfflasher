#!/bin/env bash


# This script cross compiles GCFFlasher for all platforms via Docker.
# The resultung .deb files are stored in debout/

rm -fr debout

dockfile=linux-cross.Dockerfile
imgname=gcfcross
archs=( "x64" "armv6-lts" "arm64-lts")

for arch in "${archs[@]}"
do
cat > $dockfile <<EOF
FROM dockcross/linux-${arch}

RUN apt-get install -y \
	libgpiod-dev

WORKDIR /src
COPY . /src
RUN ["bash", "build_cmake.sh"]
EOF

docker build -f $dockfile -t $imgname .
docker create --name gcfextract $imgname
docker cp gcfextract:/src/build/debout .
docker rm gcfextract
docker image rm $imgname

done

rm $dockfile