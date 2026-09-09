# debian:bookworm, NOT ubuntu:22.04 — bookworm carries aarch64 gcc 12.2.0,
# which is what build.sh pins (DR32_GCC). Ubuntu 22.04 ships 11.4.0, so an
# image built from it produced artifacts the compiler assert then rejected —
# a fallback that can only ever fail is worse than no fallback.
FROM debian:bookworm
RUN apt-get update && apt-get install -y --no-install-recommends \
    gcc-aarch64-linux-gnu g++-aarch64-linux-gnu make \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /work
