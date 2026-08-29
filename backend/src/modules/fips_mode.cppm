/**
 * FIPS provider gate.
 *
 * @author Olumuyiwa Oluwasanmi
 *
 * ---------------------------------------------------------------------------
 * WHAT THIS IS, AND -- MORE IMPORTANTLY -- WHAT IT IS NOT
 *
 * This makes the engine FIPS-CAPABLE and lets an operator make FIPS-approved
 * cryptography MANDATORY. It does NOT make the engine FIPS certified, FIPS
 * validated, or "FIPS compliant", and no message this file emits uses those
 * words. The distinction is not pedantry -- it is the difference between a
 * true statement and a false one:
 *
 *   FIPS-capable    the process links an OpenSSL 3.x libcrypto that CAN load a
 *                   `fips` provider. Every OpenSSL 3 build is. Claims nothing.
 *   FIPS mode on    a provider named `fips` is loaded and default properties
 *                   are set to `fips=yes`, so every implicit algorithm fetch
 *                   resolves only to that provider. A CONFIGURATION STATE,
 *                   reachable with any provider, validated or not.
 *   validated       the loaded fips.so is byte-derived from the exact source
 *                   version on a CMVP certificate, built per that certificate's
 *                   Security Policy. Validation attaches to a MODULE VERSION
 *                   AND BUILD PROCEDURE, never to "OpenSSL" in general.
 *
 * This file can reach the second. It cannot see a certificate, so it must never
 * assert the third. `OSSL_PROVIDER_get0_name` returns a NAME; a name is not a
 * validation. That is why the boot banner prints the provider's own name,
 * version and buildinfo and stops there, leaving the reader to check those
 * against a certificate rather than taking this process's word for it.
 *
 * ---------------------------------------------------------------------------
 * MEASURED FACTS THAT BOUND EVERY CLAIM (2026-08-28)
 *
 *   - The deploy image is stock `ubuntu:24.04`, whose OpenSSL is 3.0.13 and
 *     whose ossl-modules directory contains ONLY `legacy.so`. There is no
 *     fips provider in production, so `FIPS_MODE=required` would refuse to
 *     start there TODAY. That is the correct behaviour and the reason the
 *     default is `off`: this gate is the mechanism, not the claim.
 *
 *   - The engine has exactly ONE TLS stack. `backend/CMakeLists.txt` FORCEs
 *     `gRPC_SSL_PROVIDER=package`, so gRPC links the SYSTEM OpenSSL rather
 *     than its vendored BoringSSL -- done originally to fix a segfault from
 *     two libssl symbol sets in one process, and load-bearing here for a
 *     second reason: a boundary with two crypto libraries in it cannot be
 *     reasoned about at all. `nm -D` shows zero defined `SSL_*` symbols.
 *
 *   - PUBLIC TLS IS NOT OURS. `api.optionsandfuturescalculator.com` is
 *     terminated at Railway's edge; `envoy.yaml` contains no TLS
 *     configuration whatsoever. So no honest claim can extend past
 *     "cryptography performed inside the application container", however much
 *     is done in here. This is a property of the architecture, not a gap to
 *     be closed by more code.
 *
 *   - The application's own crypto surface is two files (`api_key.cpp`,
 *     `api_key.cppm`) and every primitive in it is already FIPS-approved:
 *     SHA-512, HMAC-SHA-512 truncated to 256 bits, HMAC-SHA-256, and
 *     `RAND_bytes`. sensen -- the whole inference and finance path -- performs
 *     no cryptography at all. The gap here has never been algorithm CHOICE;
 *     it is module VALIDATION.
 *
 *   - One non-approved primitive exists and is unreachable: cpp-httplib
 *     carries an `EVP_md5` helper for HTTP digest auth. No upstream this
 *     engine calls (Alpaca, Treasury) uses digest auth. Under `fips=yes` that
 *     fetch fails rather than silently computing an MD5, which is the correct
 *     direction to fail.
 */
module;

#include <openssl/core_names.h>
#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/params.h>
#include <openssl/provider.h>

export module fips_mode;

import std;

export namespace fips {

/** What the operator asked for, from `FIPS_MODE`. */
enum class Mode : std::uint8_t {
    Off,       ///< default -- do not touch provider configuration
    Preferred, ///< load the fips provider if present; carry on if not
    Required,  ///< load it or REFUSE TO START
};

struct Status {
    Mode requested = Mode::Off;
    bool fips_provider_loaded = false;
    bool default_properties_fips = false;
    std::string provider_name;     ///< as reported by the provider itself
    std::string provider_version;
    std::string provider_buildinfo;
    std::vector<std::string> active_providers;
    std::string detail;            ///< why it failed, when it did
};

/**
 * Applies `FIPS_MODE` and reports what is actually in force.
 *
 * MUST be called before the listener binds. Under `Required` a failure has to
 * stop the process, and a process that has already accepted a request cannot
 * un-accept it -- serving one call with unapproved cryptography and exiting
 * afterwards is indistinguishable, to the caller who got that answer, from
 * never having had a gate.
 *
 * Loading `base` alongside `fips` is NOT optional. An explicit
 * `OSSL_PROVIDER_load` disables OpenSSL's automatic activation of the default
 * provider, and `base` is what supplies encoders, decoders and the X.509
 * plumbing that carry no cryptography of their own. Omitting it does not
 * weaken FIPS -- it breaks PEM parsing and TLS outright, which reads as an
 * unrelated failure somewhere far from this file.
 */
[[nodiscard]] auto apply_from_environment() -> Status;

/** Human-readable one-liner for the boot banner. Deliberately never contains
 *  the words "certified", "validated" or "compliant" -- see the file banner. */
[[nodiscard]] auto describe(const Status& s) -> std::string;

}  // namespace fips

module :private;

namespace {

[[nodiscard]] auto parse_mode(const char* raw) -> fips::Mode {
    if (raw == nullptr || raw[0] == '\0') {
        return fips::Mode::Off;
    }
    std::string v;
    for (const char* p = raw; *p != '\0'; ++p) {
        v.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(*p))));
    }
    if (v == "required" || v == "require" || v == "1" || v == "on" || v == "true") {
        return fips::Mode::Required;
    }
    if (v == "preferred" || v == "prefer") {
        return fips::Mode::Preferred;
    }
    return fips::Mode::Off;
}

[[nodiscard]] auto provider_param(OSSL_PROVIDER* prov, const char* key) -> std::string {
    if (prov == nullptr) {
        return {};
    }
    const char* out = nullptr;
    std::array<OSSL_PARAM, 2> params{
        OSSL_PARAM_construct_utf8_ptr(key, const_cast<char**>(&out), 0),
        OSSL_PARAM_construct_end()};
    if (OSSL_PROVIDER_get_params(prov, params.data()) != 1 || out == nullptr) {
        return {};
    }
    return std::string{out};
}

auto collect_provider(OSSL_PROVIDER* prov, void* arg) -> int {
    auto* names = static_cast<std::vector<std::string>*>(arg);
    const char* n = OSSL_PROVIDER_get0_name(prov);
    names->emplace_back(n != nullptr ? n : "<unnamed>");
    return 1;
}

}  // namespace

namespace fips {

auto apply_from_environment() -> Status {
    Status s;
    s.requested = parse_mode(std::getenv("FIPS_MODE"));

    if (s.requested != Mode::Off) {
        // `base` first and unconditionally: an explicit load of ANY provider
        // suppresses the default provider's automatic activation, so the
        // non-cryptographic plumbing has to be asked for by name.
        if (OSSL_PROVIDER_load(nullptr, "base") == nullptr) {
            s.detail = "could not load the OpenSSL 'base' provider";
            return s;
        }
        OSSL_PROVIDER* fips_prov = OSSL_PROVIDER_load(nullptr, "fips");
        if (fips_prov == nullptr) {
            // The overwhelmingly common cause, and worth naming rather than
            // echoing an opaque OpenSSL error: stock Ubuntu ships no fips.so
            // at all, and where one exists it still refuses to load until
            // `openssl fipsinstall` has written a fipsmodule.cnf recording
            // its power-on self-test results.
            s.detail =
                "the OpenSSL 'fips' provider could not be loaded. On this image none is "
                "installed (stock ubuntu:24.04 ships only legacy.so in ossl-modules). Where one "
                "IS installed it must additionally have been initialised with `openssl "
                "fipsinstall`, which runs its power-on self-tests and writes fipsmodule.cnf";
            return s;
        }
        s.fips_provider_loaded = true;
        s.provider_name = provider_param(fips_prov, OSSL_PROV_PARAM_NAME);
        s.provider_version = provider_param(fips_prov, OSSL_PROV_PARAM_VERSION);
        s.provider_buildinfo = provider_param(fips_prov, OSSL_PROV_PARAM_BUILDINFO);

        // Pin every IMPLICIT fetch -- EVP_sha512(), the one-shot HMAC(),
        // RAND_bytes, and libssl's own internal fetches -- to fips=yes. Without
        // this the provider is merely loaded and the default provider still
        // answers, which is the failure mode that looks exactly like success.
        if (EVP_default_properties_enable_fips(nullptr, 1) != 1) {
            s.detail = "loaded the fips provider but could not set fips=yes default properties";
            return s;
        }
        s.default_properties_fips = EVP_default_properties_is_fips_enabled(nullptr) == 1;
    }

    OSSL_PROVIDER_do_all(nullptr, &collect_provider, &s.active_providers);
    return s;
}

auto describe(const Status& s) -> std::string {
    switch (s.requested) {
        case Mode::Off:
            break;
        case Mode::Preferred:
        case Mode::Required: {
            if (s.fips_provider_loaded && s.default_properties_fips) {
                std::string out = "FIPS_MODE=";
                out += (s.requested == Mode::Required ? "required" : "preferred");
                out += ": OpenSSL 'fips' provider ACTIVE and default properties pinned to "
                       "fips=yes (provider name='" +
                       s.provider_name + "' version='" + s.provider_version + "' buildinfo='" +
                       s.provider_buildinfo +
                       "'). Check that name and version against a CMVP certificate -- this "
                       "process cannot, and does not claim to, verify one.";
                return out;
            }
            break;
        }
    }
    std::string providers;
    for (const auto& p : s.active_providers) {
        if (!providers.empty()) {
            providers += ", ";
        }
        providers += p;
    }
    if (s.requested == Mode::Off) {
        return "FIPS_MODE=off: no FIPS claim is made. Cryptography uses the OpenSSL providers "
               "active by default (" +
               providers + "). All algorithms in use are FIPS-approved; the module is not.";
    }
    return "FIPS_MODE=" + std::string{s.requested == Mode::Required ? "required" : "preferred"} +
           " but FIPS is NOT in force: " + s.detail + ". Active providers: " + providers + ".";
}

}  // namespace fips
