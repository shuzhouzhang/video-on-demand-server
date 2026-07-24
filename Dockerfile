FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && \
    for attempt in 1 2 3; do \
    apt-get install -y --no-install-recommends \
    build-essential \
    ca-certificates \
    default-libmysqlclient-dev \
    libcpp-httplib-dev \
    libfmt-dev \
    libhiredis-dev \
    libjsoncpp-dev \
    libodb-dev \
    libodb-mysql-dev \
    libspdlog-dev \
    ffmpeg \
    make \
    python3 && break; \
    apt-get update; \
    sleep 5; \
    done && \
    rm -rf /var/lib/apt/lists/*

WORKDIR /app
COPY . .
