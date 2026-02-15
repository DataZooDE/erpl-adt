#include <erpl_adt/adt/bw_locks.hpp>

#include <erpl_adt/adt/bw_hints.hpp>
#include <erpl_adt/core/url.hpp>
#include <tinyxml2.h>

#include <string>

namespace erpl_adt {

namespace {

const char* kBwLocksPath = "/sap/bw/modeling/utils/locks";

std::string Attr(const tinyxml2::XMLElement* el, const char* name) {
    const char* val = el->Attribute(name);
    return val ? val : "";
}

int IntAttr(const tinyxml2::XMLElement* el, const char* name) {
    int val = 0;
    el->QueryIntAttribute(name, &val);
    return val;
}

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
    if (doc.Parse(http.body.data(), http.body.size()) != tinyxml2::XML_SUCCESS) {
        return Result<std::vector<BwLockEntry>, Error>::Err(Error{
            "BwListLocks", url, std::nullopt,
            "Failed to parse locks response XML", std::nullopt});
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
                entry.client = Attr(el, "client");
                entry.user = Attr(el, "user");
                entry.mode = Attr(el, "mode");
                entry.table_name = Attr(el, "tableName");
                entry.table_desc = Attr(el, "tableDesc");
                entry.object = Attr(el, "object");
                entry.arg = Attr(el, "arg");
                entry.owner1 = Attr(el, "owner1");
                entry.owner2 = Attr(el, "owner2");
                entry.timestamp = Attr(el, "timestamp");
                entry.upd_count = IntAttr(el, "updCount");
                entry.dia_count = IntAttr(el, "diaCount");
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
