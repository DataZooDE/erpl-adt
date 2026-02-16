#include <erpl_adt/adt/bw_locks.hpp>

#include "adt_utils.hpp"
#include "xml_utils.hpp"
#include <erpl_adt/adt/bw_hints.hpp>
#include <erpl_adt/core/url.hpp>
#include <tinyxml2.h>

#include <string>

namespace erpl_adt {

namespace {

const char* kBwLocksPath = "/sap/bw/modeling/utils/locks";

std::string BuildListUrl(const BwListLocksOptions& options) {
    std::string url = std::string(kBwLocksPath) + "?resultsize=" +
        std::to_string(options.max_results);
    if (options.user.has_value()) {
        url += "&user=" + UrlEncode(*options.user);
    }
    if (options.search.has_value()) {
        url += "&search=" + UrlEncode(*options.search);
    }
    return url;
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// BwListLocks
// ---------------------------------------------------------------------------
Result<std::vector<BwLockEntry>, Error> BwListLocks(
    IAdtSession& session,
    const BwListLocksOptions& options) {
    auto url = BuildListUrl(options);

    HttpHeaders headers;
    headers["Accept"] = "application/xml";

    auto response = session.Get(url, headers);
    if (response.IsErr()) {
        return Result<std::vector<BwLockEntry>, Error>::Err(
            std::move(response).Error());
    }

    const auto& http = response.Value();
    if (http.status_code != 200) {
        auto error = Error::FromHttpStatus("BwListLocks", url, http.status_code, http.body);
        AddBwHint(error);
        return Result<std::vector<BwLockEntry>, Error>::Err(std::move(error));
    }

    tinyxml2::XMLDocument doc;
    if (auto parse_error = adt_utils::ParseXmlOrError(
            doc, http.body, "BwListLocks", url,
            "Failed to parse locks response XML")) {
        return Result<std::vector<BwLockEntry>, Error>::Err(
            std::move(*parse_error));
    }

    std::vector<BwLockEntry> locks;
    auto* root = doc.RootElement();
    if (root) {
        for (auto* el = root->FirstChildElement(); el;
             el = el->NextSiblingElement()) {
            const char* name = el->Name();
            if (!name) continue;
            std::string n(name);

            if (n == "lock" || n.find(":lock") != std::string::npos) {
                BwLockEntry entry;
                entry.client = xml_utils::Attr(el, "client");
                entry.user = xml_utils::Attr(el, "user");
                entry.mode = xml_utils::Attr(el, "mode");
                entry.table_name = xml_utils::Attr(el, "tableName");
                entry.table_desc = xml_utils::Attr(el, "tableDesc");
                entry.object = xml_utils::Attr(el, "object");
                entry.arg = xml_utils::Attr(el, "arg");
                entry.owner1 = xml_utils::Attr(el, "owner1");
                entry.owner2 = xml_utils::Attr(el, "owner2");
                entry.timestamp = xml_utils::Attr(el, "timestamp");
                entry.upd_count = xml_utils::AttrIntOr(el, "updCount", 0);
                entry.dia_count = xml_utils::AttrIntOr(el, "diaCount", 0);
                locks.push_back(std::move(entry));
            }
        }
    }

    return Result<std::vector<BwLockEntry>, Error>::Ok(std::move(locks));
}

// ---------------------------------------------------------------------------
// BwDeleteLock
// ---------------------------------------------------------------------------
Result<void, Error> BwDeleteLock(
    IAdtSession& session,
    const BwDeleteLockOptions& options) {
    if (options.user.empty()) {
        return Result<void, Error>::Err(Error{
            "BwDeleteLock", kBwLocksPath, std::nullopt,
            "User must not be empty", std::nullopt});
    }

    auto url = std::string(kBwLocksPath) + "?user=" + UrlEncode(options.user);

    HttpHeaders headers;
    headers["BW_OBJNAME"] = options.table_name;
    headers["BW_ARGUMENT"] = options.arg;
    headers["BW_SCOPE"] = options.scope.empty() ? "1" : options.scope;
    headers["BW_TYPE"] = options.lock_mode;
    headers["BW_OWNER1"] = options.owner1;
    headers["BW_OWNER2"] = options.owner2;

    auto response = session.Delete(url, headers);
    if (response.IsErr()) {
        return Result<void, Error>::Err(std::move(response).Error());
    }

    const auto& http = response.Value();
    if (http.status_code != 200 && http.status_code != 204) {
        auto error = Error::FromHttpStatus("BwDeleteLock", url, http.status_code, http.body);
        AddBwHint(error);
        return Result<void, Error>::Err(std::move(error));
    }

    return Result<void, Error>::Ok();
}

} // namespace erpl_adt
