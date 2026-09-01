# Pinned rather than :latest -- this sits in front of auth, and an unattended
# base-image bump is not something to discover from a login outage.
FROM docker.io/library/caddy:2.8-alpine
# Repo-root build context: Railway's railwayConfigFile selects the config, not
# the context, so this path is from the repository root.
COPY deploy/mfv-gateway/Caddyfile /etc/caddy/Caddyfile
