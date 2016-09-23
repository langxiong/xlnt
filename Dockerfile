FROM alpine:latest

MAINTAINER "langxiong <x583194811l@gmail.com>"

RUN apk add --update \
              python \
              g++ \
              git \
              cmake \
              make \
              linux-headers \
    && rm /var/cache/apk/* \
    && git clone https://github.com/tfussell/xlnt.git  /usr/local/xlnt \
    && cd /usr/local/xlnt \
    && git submodule update --init \
    && mkdir build \
    && cd build \
    && cmake -D TESTS=1 -D SHARED=0 -D STATIC=1 .. \
    && cmake --build . \
    && ./bin/xlnt.test

WORKDIR /usr/local/xlnt
