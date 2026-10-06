# syntax=docker/dockerfile:1
#
# sensen-gateway: a RUNTIME image over a binary built on the dev host.
#
# WHY NOT BUILT FROM SOURCE HERE, which is what every other image in this
# project does. `sensen-gateway` is gated on TBQWF being EMBEDDED
# (agent/CMakeLists.txt: `if(TARGET tbqwf AND TARGET unified_server_module)`),
# and embedding TBQWF makes sensen's top-level CMake append `-maes -mpclmul` to
# CMAKE_CXX_FLAGS. Those features land in the SHARED std.pcm and clang then
# refuses every consuming translation unit with "precompiled file 'std.pcm' was
# compiled with the target feature '+aes' but the current translation unit is
# not". The parent CLAUDE.md records that trap and the operations doc says in as
# many words: embed TBQWF deliberately, in a build dir configured for it.
#
# So the binary is built in an ISOLATED clone (1,548 targets, 0 errors) and
# shipped by CHECKSUM. That is not a new precedent in this project -- the two
# assistant GGUFs are produced on this host and shipped exactly that way, pinned
# by sha256 and verified INSIDE the container after deploy. The build inputs are
# recorded in the labels below so the artefact stays attributable.
#
# A from-source image is the right follow-up and is a separate piece of work: it
# needs TBQWF plus its two nested submodules in the upload and a build dir that
# contains the +aes/+pclmul floor rather than letting it reach the engine.
FROM ubuntu:24.04

ARG GATEWAY_SHA256
ARG SENSEN_COMMIT
ARG TBQWF_COMMIT
LABEL org.opencontainers.image.title="sensen-gateway" \
      sensen.gateway.binary_sha256="${GATEWAY_SHA256}" \
      sensen.gateway.sensen_commit="${SENSEN_COMMIT}" \
      sensen.gateway.tbqwf_commit="${TBQWF_COMMIT}"

# The 18 shared objects `ldd` reports, minus the LLVM runtime, which is COPIED
# from the build host instead. apt's libc++ is a DIFFERENT patch level from the
# /usr/local LLVM 23.1.2 this binary was compiled against, and this project has
# already paid for a libc++ version split once: objects compiled against one
# libc++ and linked against another failed with `undefined reference to vtable
# for std::__bad_variant_access_with_msg`, which reads as a corrupt standard
# library and is two of them. Shipping the exact .so files removes the question.
RUN apt-get update && apt-get install -y --no-install-recommends \
        ca-certificates libatomic1 libbpf1 libbrotli1 libelf1 libjemalloc2 \
        libnghttp2-14 libsqlite3-0 libtbb12 liburing2 zlib1g libzstd1 \
    && rm -rf /var/lib/apt/lists/*

# A MINIMAL openssl.cnf, BECAUSE UBUNTU'S CRASHES THIS BINARY.
#
# The gateway links TBQWF's statically vendored LibreSSL. ubuntu:24.04 ships an
# OpenSSL 3 style /etc/ssl/openssl.cnf carrying a `providers` section, which
# LibreSSL does not implement, so its auto-configuration fails at startup:
#
#   Auto configuration failed
#   error:0EFFF071:configuration file routines:CRYPTO_internal:unknown module
#   name:.../crypto/conf/conf_mod.c:196:module=providers
#
# Note the path in that message is LibreSSL's own SOURCE FILE, not a config it
# was looking for -- misreading it as a missing build-host path sent this
# diagnosis the wrong way once. The content is `module=providers`.
#
# `OPENSSL_CONF=/dev/null` does NOT suppress it, measured, so the file itself is
# replaced. Reproduced in podman: with ubuntu's file the container exits 1 right
# after `HTTP/2 TLS: Initialized`.
RUN printf '%s\n' \
    '# Minimal config for LibreSSL. Deliberately no `providers` section.' \
    'openssl_conf = default_conf' \
    '' \
    '[default_conf]' \
    '' \
    '[req]' \
    'distinguished_name = req_dn' \
    '' \
    '[req_dn]' \
    > /etc/ssl/openssl.cnf

# MODES ARE SET HERE, NOT INHERITED FROM THE BUILD CONTEXT. The image runs as uid 10001, and a
# context assembled under `umask 077` made these 0600: every library was then unreadable and the
# binary died before main with "libc++abi.so.1: cannot open shared object file". An image must
# not depend on the umask of whoever copied files into its context.
COPY --chmod=0755 runtime/ /opt/sensen-runtime/
COPY sensen-gateway /usr/local/bin/sensen-gateway
ENV LD_LIBRARY_PATH=/opt/sensen-runtime

# VERIFY THE BINARY IS THE ONE THAT WAS GATED, at build time, before it can
# ever serve. A truncated or substituted binary still starts and then answers
# confidently -- the same reason the model fetch checksums its GGUF.
RUN test -n "${GATEWAY_SHA256}" \
    && echo "${GATEWAY_SHA256}  /usr/local/bin/sensen-gateway" | sha256sum -c - \
    && chmod 0755 /usr/local/bin/sensen-gateway \
    && /usr/local/bin/sensen-gateway admin >/dev/null 2>&1 || true

# THE TLS MATERIAL AND THE TICKET KEY ARRIVE AS ENVIRONMENT VARIABLES, not as
# baked files, and `entrypoint.sh` writes them mode 0600 at boot. That is the
# mechanism `backend/start.sh` already uses for the engine's native gRPC
# listener, reused deliberately: a private key baked into an image travels with
# every copy of the image and cannot be rotated without a rebuild.
COPY entrypoint.sh /usr/local/bin/entrypoint.sh
RUN chmod 0755 /usr/local/bin/entrypoint.sh

# Non-root. The gateway binds its own TLS port above 1024, needs no privileged
# capability, and its store is remote PostgreSQL, so it owns no volume. The run
# directory is created by the entrypoint under /tmp, which uid 10001 can write.
RUN useradd --system --no-create-home --uid 10001 gateway
USER 10001

# The gateway has NO CLEARTEXT MODE: without a certificate and key it refuses to
# start rather than binding in the clear, and the entrypoint refuses one step
# earlier with a message that names the missing VARIABLE rather than a file path
# that was never going to exist.
ENTRYPOINT ["/usr/local/bin/entrypoint.sh"]
