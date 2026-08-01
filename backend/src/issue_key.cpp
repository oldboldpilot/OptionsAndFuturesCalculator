// Issues an API key for a customer, from the same binary that verifies them.
//
// Deliberately NOT a shell script or a Python helper. The registry authenticates
// by comparing sha512(presented_key) against a stored digest, so a minting tool
// that computes that digest even slightly differently -- a trailing newline from
// `echo`, upper-case hex, a different encoding of the random bytes -- produces
// keys that are indistinguishable from forgeries at the door. Linking the same
// `generate_key` and `sha512_hex` the server uses removes the possibility
// rather than documenting it as a caution.
//
//   calculator_engine issue-key --id acme-risk --rpm 600 --cu 50000
//
// The key is printed ONCE. It is never written anywhere, because the whole
// point of storing a digest is that a stolen configuration file yields nothing
// usable; a tool that also saved the plaintext would undo that.

#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

import api_key;
import fastjson;

namespace {

using options_calculator::auth::KeyType;

/** Escapes a string for embedding in JSON. */
[[nodiscard]] auto json_escape(std::string_view s) -> std::string {
    std::string out;
    out.reserve(s.size() + 2);
    for (const char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                // Control characters are not legal raw in a JSON string.
                if (static_cast<unsigned char>(c) < 0x20) {
                    static const char* kHex = "0123456789abcdef";
                    out += "\\u00";
                    out += kHex[(static_cast<unsigned char>(c) >> 4) & 0xF];
                    out += kHex[static_cast<unsigned char>(c) & 0xF];
                } else {
                    out += c;
                }
        }
    }
    return out;
}

[[nodiscard]] auto json_string(std::string_view s) -> std::string {
    return "\"" + json_escape(s) + "\"";
}

/** Renders a double without a trailing `.000000`, so the output stays readable. */
[[nodiscard]] auto number_to_string(double v) -> std::string {
    if (v == static_cast<double>(static_cast<std::int64_t>(v))) {
        return std::to_string(static_cast<std::int64_t>(v));
    }
    std::string s = std::to_string(v);
    while (s.size() > 1 && s.back() == '0') s.pop_back();
    if (!s.empty() && s.back() == '.') s.pop_back();
    return s;
}

auto print_usage() -> void {
    std::cout << R"(Issue an API key for the sensen.finance.Finance service.

  calculator_engine issue-key --id NAME [options]

Identity
  --id NAME              Human label for the customer. Required. Appears in
                         logs and in refusal messages; it is not a secret.
  --tier NAME            Tier name, used when no per-key limit is given.
                         Default: business

Limits -- the numbers you are handing this customer
  --rpm N                Requests per minute.
  --cu N                 Compute units per hour. One unit is roughly one
                         closed-form call; a large Monte Carlo is thousands.
                         Either may be given alone. Omitting both makes the key
                         follow its tier's limits from QUOTA_POLICY.
                         Passing 0 explicitly means UNLIMITED on that axis.

Kind
  --secret               Server-side key, sk_live_. Default.
  --publishable          Browser key, pk_live_. Requires at least one --origin,
                         because a key that ships in a customer's HTML is
                         readable by every visitor and is only bound by origin.
  --origin URL           Allowed origin, repeatable. One leading wildcard label
                         is supported, e.g. https://*.acme.example

Lifetime and reach
  --expires YYYY-MM-DD   Stops working after this date. Default: never.
  --scope NAME           Service the key may call, repeatable. Default: finance

Output
  --merge                Read the current FINANCE_API_KEYS from the environment
                         and print the complete merged value, ready to paste
                         into the engine service's configuration.
)";
}

struct Options {
    std::string id;
    std::string tier = "business";
    std::string expires;
    std::vector<std::string> origins;
    std::vector<std::string> scopes;
    KeyType type = KeyType::Secret;
    bool has_rpm = false;
    bool has_cu = false;
    std::int64_t rpm = 0;
    double cu = 0.0;
    bool merge = false;
};

/** Parses argv, or explains exactly what was wrong and returns false. */
[[nodiscard]] auto parse_args(int argc, char** argv, Options& opt) -> bool {
    // A value-taking flag at the end of argv would otherwise read argv[argc].
    const auto need_value = [&](int i, const char* flag) -> const char* {
        if (i + 1 >= argc) {
            std::cerr << "issue-key: " << flag << " needs a value\n";
            return nullptr;
        }
        return argv[i + 1];
    };

    for (int i = 2; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--id" || arg == "--tier" || arg == "--expires" || arg == "--origin" ||
            arg == "--scope" || arg == "--rpm" || arg == "--cu") {
            const char* value = need_value(i, arg.c_str());
            if (value == nullptr) return false;
            ++i;
            if (arg == "--id") {
                opt.id = value;
            } else if (arg == "--tier") {
                opt.tier = value;
            } else if (arg == "--expires") {
                opt.expires = value;
            } else if (arg == "--origin") {
                opt.origins.emplace_back(value);
            } else if (arg == "--scope") {
                opt.scopes.emplace_back(value);
            } else if (arg == "--rpm") {
                try {
                    opt.rpm = std::stoll(value);
                } catch (const std::exception&) {
                    std::cerr << "issue-key: --rpm must be a whole number, got '" << value << "'\n";
                    return false;
                }
                if (opt.rpm < 0) {
                    std::cerr << "issue-key: --rpm cannot be negative\n";
                    return false;
                }
                opt.has_rpm = true;
            } else {
                try {
                    opt.cu = std::stod(value);
                } catch (const std::exception&) {
                    std::cerr << "issue-key: --cu must be a number, got '" << value << "'\n";
                    return false;
                }
                if (opt.cu < 0.0) {
                    std::cerr << "issue-key: --cu cannot be negative\n";
                    return false;
                }
                opt.has_cu = true;
            }
        } else if (arg == "--secret") {
            opt.type = KeyType::Secret;
        } else if (arg == "--publishable") {
            opt.type = KeyType::Publishable;
        } else if (arg == "--merge") {
            opt.merge = true;
        } else if (arg == "--help" || arg == "-h") {
            print_usage();
            std::exit(0);
        } else {
            // Ignoring an unknown flag would mean quietly issuing a key that
            // does not carry the limit the operator just typed.
            std::cerr << "issue-key: unknown option '" << arg << "'\n\n";
            print_usage();
            return false;
        }
    }

    if (opt.id.empty()) {
        std::cerr << "issue-key: --id is required. It is how this customer appears in the logs "
                     "and in the refusal messages they will send you.\n";
        return false;
    }
    if (opt.type == KeyType::Publishable && opt.origins.empty()) {
        std::cerr << "issue-key: a publishable key needs at least one --origin. It ships in the "
                     "customer's page, so any visitor can read it; the origin allowlist is the "
                     "only thing that stops it being replayed from somewhere else.\n";
        return false;
    }
    if (opt.scopes.empty()) opt.scopes.emplace_back("finance");
    return true;
}

/** Builds the FINANCE_API_KEYS entry for one key, as `"hash": {spec}`. */
[[nodiscard]] auto build_entry(const std::string& hash, const Options& opt) -> std::string {
    std::string spec = json_string(hash) + ": {";
    spec += "\"id\": " + json_string(opt.id);
    spec += ", \"tier\": " + json_string(opt.tier);
    spec += std::string{", \"type\": \""} +
            (opt.type == KeyType::Secret ? "secret" : "publishable") + "\"";
    if (opt.has_rpm) spec += ", \"requests_per_minute\": " + std::to_string(opt.rpm);
    if (opt.has_cu) spec += ", \"compute_units_per_hour\": " + number_to_string(opt.cu);
    if (!opt.expires.empty()) spec += ", \"expires\": " + json_string(opt.expires);

    spec += ", \"scopes\": [";
    for (std::size_t i = 0; i < opt.scopes.size(); ++i) {
        if (i > 0) spec += ", ";
        spec += json_string(opt.scopes[i]);
    }
    spec += "]";

    if (!opt.origins.empty()) {
        spec += ", \"origins\": [";
        for (std::size_t i = 0; i < opt.origins.size(); ++i) {
            if (i > 0) spec += ", ";
            spec += json_string(opt.origins[i]);
        }
        spec += "]";
    }
    spec += "}";
    return spec;
}

/**
 * Splices a new entry into an existing FINANCE_API_KEYS value.
 *
 * Textual rather than a parse-and-re-emit, so anything already in the value
 * that this tool does not know about survives verbatim. Re-emitting from the
 * parsed form would silently drop fields a future version adds -- and the
 * failure would land on whichever customer's entry happened to use one.
 *
 * Returns false and explains why if the existing value is not a JSON object, or
 * if the result would not parse. The output of this tool is pasted straight
 * into production configuration; it must not be capable of emitting something
 * the engine will reject at startup.
 */
[[nodiscard]] auto merge_entry(const std::string& existing, const std::string& entry,
                               std::string& out) -> bool {
    std::string base = existing;
    // Trim, so the closing-brace search below cannot land on trailing whitespace.
    while (!base.empty() && std::isspace(static_cast<unsigned char>(base.back()))) base.pop_back();
    std::size_t begin = 0;
    while (begin < base.size() && std::isspace(static_cast<unsigned char>(base[begin]))) ++begin;
    base = base.substr(begin);

    if (base.empty()) {
        out = "{" + entry + "}";
        return true;
    }

    auto parsed = fastjson::parse(base);
    if (!parsed || !parsed->is_object()) {
        std::cerr << "issue-key: FINANCE_API_KEYS is set but is not a JSON object, so there is "
                     "nothing safe to merge into. Fix or unset it and try again.\n";
        return false;
    }
    if (base.back() != '}') {
        std::cerr << "issue-key: FINANCE_API_KEYS does not end in '}'; refusing to guess where "
                     "the object ends.\n";
        return false;
    }

    const bool empty_object = parsed->as_object().empty();
    out = base.substr(0, base.size() - 1);
    if (!empty_object) out += ",";
    out += entry + "}";

    // The gate: whatever this prints must load.
    auto check = fastjson::parse(out);
    if (!check || !check->is_object()) {
        std::cerr << "issue-key: the merged value did not parse; refusing to print it.\n";
        return false;
    }
    return true;
}

}  // namespace

auto IssueKeyMain(int argc, char** argv) -> int {
    Options opt;
    if (!parse_args(argc, argv, opt)) return 2;

    const std::string key = options_calculator::auth::generate_key(opt.type);
    const std::string hash = options_calculator::auth::sha512_hex(key);
    const std::string entry = build_entry(hash, opt);

    // The key goes to stdout once and is then unrecoverable -- there is no
    // store to read it back from, by design.
    std::cout << "\nAPI key issued for '" << opt.id << "'.\n\n";
    std::cout << "  key       " << key << "\n";
    std::cout << "  kind      "
              << (opt.type == KeyType::Secret ? "secret (server-side only)"
                                              : "publishable (safe in a browser)")
              << "\n";
    std::cout << "  tier      " << opt.tier << "\n";

    if (opt.has_rpm || opt.has_cu) {
        std::cout << "  limit     ";
        if (opt.has_rpm) {
            std::cout << (opt.rpm == 0 ? "unlimited requests" : std::to_string(opt.rpm) +
                                                                    " requests/minute");
        }
        if (opt.has_rpm && opt.has_cu) std::cout << ", ";
        if (opt.has_cu) {
            std::cout << (opt.cu == 0.0 ? "unlimited compute"
                                        : number_to_string(opt.cu) + " compute units/hour");
        }
        std::cout << "  (on the key, overrides the tier)\n";
    } else {
        std::cout << "  limit     whatever QUOTA_POLICY gives tier '" << opt.tier << "'\n";
    }

    for (const auto& o : opt.origins) std::cout << "  origin    " << o << "\n";
    std::cout << "  scopes    ";
    for (std::size_t i = 0; i < opt.scopes.size(); ++i) {
        if (i > 0) std::cout << ", ";
        std::cout << opt.scopes[i];
    }
    std::cout << "\n";
    std::cout << "  expires   " << (opt.expires.empty() ? "never" : opt.expires) << "\n";

    std::cout << "\nThe key above is shown ONCE. Only its SHA-512 digest is configured, so it "
                 "cannot be\nrecovered from the server -- if it is lost, issue another and remove "
                 "this entry.\n";

    if (opt.merge) {
        const char* raw = std::getenv("FINANCE_API_KEYS");
        std::string merged;
        if (!merge_entry(raw != nullptr ? raw : "", entry, merged)) return 1;
        std::cout << "\nSet FINANCE_API_KEYS to:\n\n" << merged << "\n\n";
    } else {
        std::cout << "\nAdd to FINANCE_API_KEYS on the engine service:\n\n  " << entry
                  << "\n\nOr re-run with --merge to print the complete value including the keys "
                     "already set.\n\n";
    }

    if (opt.type == KeyType::Secret && !opt.origins.empty()) {
        std::cout << "Note: origins were given for a SECRET key. A secret key arriving with an "
                     "Origin\nheader is treated as leaked and refused, so these only narrow it "
                     "further.\n\n";
    }
    return 0;
}
