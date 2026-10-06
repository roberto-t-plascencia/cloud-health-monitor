# ---- Build stage -------------------------------------------------------------
FROM ubuntu:24.04 AS build
RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential cmake git ca-certificates libcurl4-openssl-dev \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /src
COPY CMakeLists.txt ./
COPY include ./include
COPY src ./src
COPY tests ./tests
RUN cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DHEALTHMON_BUILD_TESTS=ON \
    && cmake --build build -j"$(nproc)" \
    && (cd build && ctest --output-on-failure) \
    && strip build/healthmon

# ---- Runtime stage -----------------------------------------------------------
FROM ubuntu:24.04
RUN apt-get update && apt-get install -y --no-install-recommends \
        libcurl4 ca-certificates \
    && rm -rf /var/lib/apt/lists/* \
    && useradd --system --uid 10001 --no-create-home healthmon
COPY --from=build /src/build/healthmon /usr/local/bin/healthmon
COPY config/targets.json /etc/healthmon/targets.json
USER 10001
EXPOSE 9100
ENV HEALTHMON_CONFIG=/etc/healthmon/targets.json
ENTRYPOINT ["/usr/local/bin/healthmon"]
