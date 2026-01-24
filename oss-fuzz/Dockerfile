# OSS-Fuzz Dockerfile for DotLanth VM
# https://google.github.io/oss-fuzz/getting-started/new-project-guide/

FROM gcr.io/oss-fuzz-base/base-builder

# Install build dependencies
RUN apt-get update && apt-get install -y \
    cmake \
    ninja-build \
    git \
    && rm -rf /var/lib/apt/lists/*

# Clone the repository
RUN git clone --depth 1 https://github.com/ademclk/dotlanth.git $SRC/dotlanth

# Copy the build script
COPY build.sh $SRC/

# Set working directory
WORKDIR $SRC/dotlanth
