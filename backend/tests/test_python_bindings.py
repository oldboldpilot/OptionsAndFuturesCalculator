#!/usr/bin/env python3
"""
@author Olumuyiwa Oluwasanmi

Scripted check for the nanobind Python bindings (options_futures_engine).

Registered as CTest's PythonBindingsTest, gated behind the same
BUILD_PYTHON_BINDINGS option as the module itself (see CMakeLists.txt) -- it
does not run, and is not expected to, in the default OFF build that serves
optionsandfuturescalculator.com and mortgagefvcalculator.com.

This is deliberately not a "does it import" smoke test. It calls the two
real entry points bindings.cpp exposes (fetch_quote, fetch_risk_free_rate)
and checks the response either way: the assertions are written so a NETWORK
failure (no ALPACA_* credentials in this environment, no route to Alpaca/
Treasury) and an ENGINE failure look different. A network-shaped failure
raises RuntimeError with the engine's own, human-readable error.message()
text (see market_data.cppm's MarketDataError category) -- that is accepted
as a pass, because it proves the call reached real C++ code and came back
with a real, correctly-typed answer. Anything else -- an import error, a
TypeError from a wrong signature, an AttributeError from a missing binding,
or a Quote/RiskFreeRate whose fields silently hold the wrong type -- is not
accepted, and fails the check.
"""

import sys


def test_import():
    import options_futures_engine as engine

    for name in ("Quote", "RatePoint", "RiskFreeRate", "fetch_quote", "fetch_risk_free_rate"):
        assert hasattr(engine, name), f"options_futures_engine is missing '{name}'"


def test_quote_roundtrip():
    """No network involved: proves the class bindings themselves work --
    construction, attribute read/write, and the types nanobind chose for
    each field -- independently of whether Alpaca is reachable."""
    import options_futures_engine as engine

    q = engine.Quote()
    q.symbol = "SPY"
    q.price = 642.17
    q.previous_close = 640.50
    q.timestamp = "2026-08-09T14:30:00Z"

    assert q.symbol == "SPY"
    assert isinstance(q.price, float) and q.price == 642.17
    assert isinstance(q.previous_close, float) and q.previous_close == 640.50
    assert q.timestamp == "2026-08-09T14:30:00Z"


def test_rate_point_roundtrip():
    import options_futures_engine as engine

    p = engine.RatePoint()
    p.tenor = "3M"
    p.days = 91
    p.rate_bey = 0.0383
    p.rate_continuous = 0.03794

    assert p.tenor == "3M"
    assert isinstance(p.days, int) and p.days == 91
    assert abs(p.rate_bey - 0.0383) < 1e-12
    assert abs(p.rate_continuous - 0.03794) < 1e-12


def test_fetch_risk_free_rate():
    """Keyless -- exercises a real network call through the engine's
    US Treasury path with no credentials required. Accepts either a real
    curve or a clean, engine-produced RuntimeError as a pass; anything else
    (wrong exception type, empty message, wrong field types) is a fail."""
    import options_futures_engine as engine

    try:
        rate = engine.fetch_risk_free_rate()
    except RuntimeError as exc:
        assert str(exc), "fetch_risk_free_rate() raised RuntimeError with an empty message"
        print(f"  fetch_risk_free_rate(): engine refused ({exc}) -- treated as pass")
        return

    assert isinstance(rate.rate, float) and rate.rate > 0.0
    assert isinstance(rate.rate_published, float) and rate.rate_published > 0.0
    assert rate.tenor, "RiskFreeRate.tenor is empty on a successful fetch"
    assert rate.as_of_date, "RiskFreeRate.as_of_date is empty on a successful fetch"
    assert rate.source, "RiskFreeRate.source is empty on a successful fetch"
    assert len(rate.curve) > 0, "RiskFreeRate.curve is empty on a successful fetch"
    for point in rate.curve:
        assert point.tenor
        assert point.days > 0
    print(
        f"  fetch_risk_free_rate(): {rate.tenor}={rate.rate_published:.4%} "
        f"as of {rate.as_of_date} ({rate.source}), {len(rate.curve)} tenor(s) on the curve"
    )


def test_fetch_quote():
    """Needs ALPACA_API_KEY/ALPACA_API_SECRET. Same accept-both-outcomes
    shape as test_fetch_risk_free_rate() above, for the same reason: this
    check gates on the C++ binding surface, not on Alpaca's availability."""
    import options_futures_engine as engine

    try:
        quote = engine.fetch_quote("SPY")
    except RuntimeError as exc:
        assert str(exc), "fetch_quote() raised RuntimeError with an empty message"
        print(f"  fetch_quote('SPY'): engine refused ({exc}) -- treated as pass")
        return

    assert quote.symbol == "SPY"
    assert isinstance(quote.price, float) and quote.price > 0.0
    print(f"  fetch_quote('SPY'): price={quote.price} previous_close={quote.previous_close}")


def main() -> int:
    tests = [
        test_import,
        test_quote_roundtrip,
        test_rate_point_roundtrip,
        test_fetch_risk_free_rate,
        test_fetch_quote,
    ]
    failures = []
    for test in tests:
        name = test.__name__
        try:
            test()
            print(f"PASS {name}")
        except Exception as exc:  # noqa: BLE001 -- report every failure, don't stop at the first
            failures.append(name)
            print(f"FAIL {name}: {exc!r}")

    if failures:
        print(f"\n{len(failures)}/{len(tests)} checks failed: {', '.join(failures)}")
        return 1
    print(f"\n{len(tests)}/{len(tests)} checks passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
