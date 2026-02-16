#pragma once

#include <erpl_adt/adt/i_adt_session.hpp>
#include <erpl_adt/core/result.hpp>

#include <optional>
#include <string>
#include <vector>

namespace erpl_adt {

struct BwSearchMetadataEntry {
    std::string name;
    std::string value;
    std::string description;
    std::string category;
};

struct BwFavoriteEntry {
    std::string name;
    std::string type;
    std::string description;
    std::string uri;
};

struct BwNodePathEntry {
    std::string name;
    std::string type;
    std::string uri;
};

struct BwApplicationLogEntry {
    std::string identifier;
    std::string user;
    std::string timestamp;
    std::string severity;
    std::string text;
};

struct BwMessageTextOptions {
    std::string identifier;
    std::string text_type;
    std::optional<std::string> msgv1;
    std::optional<std::string> msgv2;
    std::optional<std::string> msgv3;
    std::optional<std::string> msgv4;
};

struct BwMessageTextResult {
    std::string identifier;
    std::string text_type;
    std::string text;
    std::string raw_response;
};

struct BwApplicationLogOptions {
    std::optional<std::string> username;
    std::optional<std::string> start_timestamp;
    std::optional<std::string> end_timestamp;
};

[[nodiscard]] Result<std::vector<BwSearchMetadataEntry>, Error>
BwGetSearchMetadata(IAdtSession& session);

[[nodiscard]] Result<std::vector<BwFavoriteEntry>, Error>
BwListBackendFavorites(IAdtSession& session);

[[nodiscard]] Result<void, Error> BwDeleteAllBackendFavorites(IAdtSession& session);

[[nodiscard]] Result<std::vector<BwNodePathEntry>, Error>
BwGetNodePath(IAdtSession& session, const std::string& object_uri);

[[nodiscard]] Result<std::vector<BwApplicationLogEntry>, Error>
BwGetApplicationLog(IAdtSession& session, const BwApplicationLogOptions& options);

[[nodiscard]] Result<BwMessageTextResult, Error>
BwGetMessageText(IAdtSession& session, const BwMessageTextOptions& options);

}  // namespace erpl_adt
