#include <erpl_adt/adt/bw_endpoint_resolver.hpp>

#include <erpl_adt/core/url.hpp>

#include <string>
#include <vector>

namespace erpl_adt {

namespace {

std::vector<std::string> SplitByComma(std::string_view in) {
    std::vector<std::string> out;
    std::string current;
    for (const char ch : in) {
        if (ch == ',') {
            if (!current.empty()) {
                out.push_back(current);
                current.clear();
            }
            continue;
        }
        current.push_back(ch);
    }
    if (!current.empty()) {
        out.push_back(current);
    }
    return out;
}

void AppendQueryParam(std::string& out, bool& first, const std::string& key,
                      const std::string& value) {
    out += (first ? '?' : '&');
    out += UrlEncode(key);
    out += '=';
    out += UrlEncode(value);
    first = false;
}

} // namespace

std::string BwExpandUriTemplate(std::string_view uri_template,
                                const BwTemplateParams& path_params,
                                const BwTemplateParams& query_params) {
    std::string out;
    out.reserve(uri_template.size() + 32);

    for (size_t i = 0; i < uri_template.size();) {
        const char ch = uri_template[i];
        if (ch != '{') {
            out.push_back(ch);
            ++i;
            continue;
        }

        const size_t end = uri_template.find('}', i + 1);
        if (end == std::string_view::npos) {
            out.push_back(ch);
            ++i;
            continue;
        }

        std::string expr(uri_template.substr(i + 1, end - (i + 1)));
        i = end + 1;

        if (expr.empty()) {
            continue;
        }

        if (expr[0] == '?') {
            auto vars = SplitByComma(std::string_view(expr).substr(1));
            bool first = true;
            for (const auto& key : vars) {
                const auto it = query_params.find(key);
                if (it == query_params.end() || it->second.empty()) {
                    continue;
                }
                AppendQueryParam(out, first, key, it->second);
            }
            continue;
        }

        const auto it = path_params.find(expr);
        if (it != path_params.end()) {
            out += UrlEncode(it->second);
        }
    }

    return out;
}

Result<std::string, Error> BwResolveAndExpandEndpoint(
    const BwDiscoveryResult& discovery, const std::string& scheme,
    const std::string& term, const BwTemplateParams& path_params,
    const BwTemplateParams& query_params) {
    auto endpoint = BwResolveEndpoint(discovery, scheme, term);
    if (endpoint.IsErr()) {
        return Result<std::string, Error>::Err(std::move(endpoint).Error());
    }

    return Result<std::string, Error>::Ok(BwExpandUriTemplate(
        endpoint.Value(), path_params, query_params));
}

Result<std::string, Error> BwDiscoverResolveAndExpandEndpoint(
    IAdtSession& session, const std::string& scheme, const std::string& term,
    const BwTemplateParams& path_params, const BwTemplateParams& query_params) {
    auto discovery = BwDiscover(session);
    if (discovery.IsErr()) {
        return Result<std::string, Error>::Err(std::move(discovery).Error());
    }

    return BwResolveAndExpandEndpoint(discovery.Value(), scheme, term,
                                      path_params, query_params);
}

} // namespace erpl_adt
