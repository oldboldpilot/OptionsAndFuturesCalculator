# Pinned rather than :latest -- this sits in front of auth, and an unattended
# base-image bump is not something to discover from a login outage.
FROM docker.io/library/caddy:2.8-alpine
# rootDirectory=deploy/mfv-gateway makes THIS directory the build context.
COPY Caddyfile /etc/caddy/Caddyfile
