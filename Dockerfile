FROM ubuntu:24.04 AS build

RUN apt-get update && \
    apt-get install -y \
        build-essential \
        cmake \
        git \
        && \
    rm -rf /var/lib/apt/lists/*

WORKDIR /app

# Copy the repository, including the git metadata needed to initialize
# recursive submodules.
COPY . .

# Make sure all nested dependencies are populated.
RUN git submodule update --init --recursive

WORKDIR /app/server

RUN cmake -S . -B build \
    -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_TESTS=OFF \
    -DBUILD_BENCHMARKS=OFF \
    -DBUILD_REGRESSION=OFF \
    -DBUILD_EXAMPLES=OFF

RUN cmake --build build --config Release -j$(nproc)


FROM ubuntu:24.04

RUN apt-get update && \
    apt-get install -y \
        libstdc++6 \
        && \
    rm -rf /var/lib/apt/lists/*

WORKDIR /app

COPY --from=build /app/server/build/Shrtn_server /app/Shrtn_server

RUN mkdir -p /app/data

EXPOSE 8080

CMD ["/app/Shrtn_server"]