# syntax=docker/dockerfile:1
#
# A CONFORMANCE LEAF, and it is NOT an inference server.
#
# It exists because `sensen_serve` -- the real io_uring inference leaf -- does
# not build in this tree: SGEE's `transpiler.cppm` imports
# `logger.code_generator`, and in the EMBEDDED configuration the logger target
# is tbqwf's own `logger` (one module, `logger.cppm`) rather than the
# `sgee_logger` SGEE guards on, so the block that would put the logger's BMI on
# the module search path is skipped entirely and `SGEE_LOGGER_MODULE_DIR` points
# at a directory that does not exist. That is an upstream defect across three
# vendored repositories and not something to patch from a build tree.
#
# What this DOES do is satisfy the leaf contract the gateway's own prober
# defines, so the gateway's admission path can be exercised end to end:
# `gateway_tbqwf.cppm::curlProber` issues GET {base_url}/v1/models and requires
# a 200 carrying a model list -- anything else leaves the machine not-live, and
# a machine that is not live is never a candidate, so the gate compares against
# an EMPTY machine id and answers `no_permitted_placement`.
#
# Its replies say what they are. Swapping in the real leaf is a `base_url`
# change in one seed row and nothing else.
FROM python:3.13-slim
COPY leaf.py /srv/leaf.py
RUN useradd --system --no-create-home --uid 10002 leaf
USER 10002
ENV PORT=8080
# `PORT` is Railway's, and here it IS this container's only listener -- unlike
# the engine image, where PORT is Envoy's and reading it would be an outage.
CMD ["sh", "-c", "exec python3 /srv/leaf.py ${PORT:-8080} 0.0.0.0"]
