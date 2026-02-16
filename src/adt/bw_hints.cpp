#include <erpl_adt/adt/bw_hints.hpp>

#include <algorithm>
#include <string>

namespace erpl_adt {

namespace {

std::string ToLowerStr(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return s;
}

bool Contains(const std::string& haystack, const std::string& needle) {
    auto lower_hay = ToLowerStr(haystack);
    auto lower_ndl = ToLowerStr(needle);
    return lower_hay.find(lower_ndl) != std::string::npos;
}

bool IsBwEndpoint(const std::string& endpoint) {
    return Contains(endpoint, "/sap/bw/modeling/");
}

} // anonymous namespace

void AddBwHint(Error& error) {
    if (!IsBwEndpoint(error.endpoint)) return;

    // 406 on any BW endpoint → content type version mismatch
    if (error.http_status.has_value() && *error.http_status == 406) {
        error.hint = "Content type version mismatch. Run 'erpl-adt bw discover' "
                     "to check supported versions.";
        return;
    }

    // 404 on any BW endpoint → SICF activation needed
    if (error.http_status.has_value() && *error.http_status == 404) {
        error.hint = "Activate the BW Modeling API in transaction SICF "
                     "(path: /sap/bw/modeling/)";
        return;
    }

    // 500 with "not activated" or "not implemented" in the error text
    if (error.http_status.has_value() && *error.http_status == 500) {
        auto has_activation_error = [&]() {
            auto check = [](const std::string& text) {
                return Contains(text, "not activated") ||
                       Contains(text, "not implemented");
            };
            if (check(error.message)) return true;
            if (error.sap_error.has_value() && check(*error.sap_error)) return true;
            return false;
        };

        if (has_activation_error()) {
            if (Contains(error.endpoint, "bwsearch")) {
                error.hint = "Activate BW Search in transaction RSOSM";
            } else if (Contains(error.endpoint, "/cto/") ||
                       Contains(error.endpoint, "/cto?")) {
                error.hint = "Activate BW CTO (transport organizer) in transaction RSOSM";
            } else {
                error.hint = "Activate the required BW service in transaction RSOSM";
            }
        } else if (Contains(error.endpoint, "bwsearch")) {
            // BW search 500 without activation error — likely invalid type filter.
            error.hint = "Check your --type value. Valid BW types include: "
                         "IOBJ, ADSO, TRFN, DTPA, CUBE, MPRO, APRO, HCPR, "
                         "RSDS, LSYS, QUERY, DEST, FBP, DMOD, TRCS, DOCA";
        }
    }
}

} // namespace erpl_adt
