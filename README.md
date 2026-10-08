# Options & Futures Calculator

@author Olumuyiwa Oluwasanmi

A C++23 calculation engine and the website built on it. The engine also serves a second site:

1. **optionsandfuturescalculator.com**: multi-leg option and futures strategy modelling, payoff curves, live option chains and a futures term structure, and a natural-language strategy assistant. The web client is in `frontend/`.
2. **mortgagefvcalculator.com**: time value of money, amortization, HELOC, refinance, rent-vs-buy, rental cash flow and closing-cost calculations through the `sensen.finance.Finance` service. A gRPC-Web client for that service, meant to be copied into the site's codebase, is in `clients/mortgagefv/`.

## Architecture

One native engine (`calculator_engine`) behind an Envoy proxy (`backend/envoy.yaml`), a PostgreSQL database, and a Next.js static-export frontend served from Cloudflare Workers. `scripts/railway_deploy.sh` deploys the backend and `npx wrangler deploy` in `frontend/` deploys the site; see [CLAUDE.md](CLAUDE.md) for the deployment details and the engineering record.

### gRPC services

The three services in `backend/proto/` declare 56 RPCs:

| Service | Proto | RPCs |
| --- | --- | --- |
| `calculator.OptionsCalculator` | `calculator.proto` | 7: `CalculateStrategy`, `GetMarketQuote`, `GetMarketChain`, `GetRiskFreeRate`, and the saved-scenario calls `SaveStrategy`, `ListStrategies`, `DeleteStrategy` |
| `sensen.finance.Finance` | `finance.proto` | 48: time value of money (10), mortgages and amortization (6), HELOC and refinance (2), real estate and rent-vs-buy (6), cash flow, NPV and IRR (6), depreciation (1), bonds and T-bills (2), futures, margin and hedging (5), option pricing (4), portfolio statistics and optimization (3), closing costs (1), state assumptions (2) |
| `calculator.assistant.StrategyAssistant` | `assistant.proto` | 1: `ParseStrategy`, a plain-English request turned into strategy legs |

The mortgage assistant (`mortgage.assistant.MortgageAssistant`, `ParseOperation`) is no longer served here: since 2026-10 it runs as its own service from the `mortgage-nest-egg` repository, which holds its contract.

Money fields in `finance.proto` are decimal strings backed by an exact fixed-point `BigDecimal`; fields the library computes in `double` are `double`. See [docs/FINANCE_API.md](docs/FINANCE_API.md).

### Edge (`backend/envoy.yaml`)

- gRPC-Web for browsers, and JSON over `POST /<package>.<Service>/<Method>` through `grpc_json_transcoder` for callers without a proto toolchain.
- `GET` of `/health`, `/healthz`, `/live`, `/livez`, `/ready` or `/readyz` answers `200 ok` without reaching the engine; any other `GET` or `HEAD` answers `404 not found`.
- A local token-bucket rate limit per Envoy instance: 10 requests per second refill, burst of 100.

## Directory layout

```
backend/            engine, Envoy config, container entrypoint
  proto/            the four service contracts
  src/              engine sources (src/modules holds the C++ modules)
  tests/            test suites
  migrations/       PostgreSQL migrations 01 to 09
  envoy.yaml        proxy and transcoder configuration
  start.sh          container entrypoint: engine on :50051 with Envoy in front
frontend/           the optionsandfuturescalculator.com web app (see frontend/README.md)
clients/mortgagefv/ gRPC-Web client for sensen.finance.Finance
deploy/             queue-node (SGEE queue cluster), assistant-worker (template for a separate
                    weights-holding engine service), mfv-gateway (Caddy origin for GoTrue and PostgREST)
docs/               documentation; start at docs/INDEX.md
scripts/            operator and verification scripts
```

## Operator scripts (`scripts/`)

- `railway_deploy.sh`: uploads and deploys the backend service.
- `backup_database_to_nas.sh`: dumps the project's databases to the NAS, encrypted at rest, and checks each dump with `pg_restore --list`. Credentials come from the environment and are never printed.
- `gen_proto.sh`: regenerates the frontend's gRPC-Web stubs in `frontend/src/grpc/` from `backend/proto/`.
- `probe_live_assistant.py`: checks the deployed strategy assistant over gRPC-Web.

## Documentation

Start at [docs/INDEX.md](docs/INDEX.md). Frequently needed:

- [gRPC surface reference](docs/api/GRPC_SURFACE.md)
- [Feature inventory, as built](docs/FEATURES.md)
- [Finance API](docs/FINANCE_API.md)
- [API security and key handling](docs/API_SECURITY.md)
- [Frontend README](frontend/README.md)
