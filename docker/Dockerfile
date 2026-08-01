FROM gcc:16.1.0

ARG TARGETARCH

RUN apt-get update \
      && apt-get install -y --no-install-recommends jq \
      && rm -rf /var/lib/apt/lists/*

ARG BAZELISK_VERSION=v1.29.0
RUN curl -fsSL -o /usr/local/bin/bazel \
      "https://github.com/bazelbuild/bazelisk/releases/download/${BAZELISK_VERSION}/bazelisk-linux-${TARGETARCH}" \
      && chmod +x /usr/local/bin/bazel

ARG BUILDIFIER_VERSION=v8.5.1
RUN curl -fsSL -o /usr/local/bin/buildifier \
      "https://github.com/bazelbuild/buildtools/releases/download/${BUILDIFIER_VERSION}/buildifier-linux-${TARGETARCH}" \
      && chmod +x /usr/local/bin/buildifier

WORKDIR /src

COPY .bazelversion /src/.bazelversion

# Pulls specified bazel version and caches it
RUN bazel version
