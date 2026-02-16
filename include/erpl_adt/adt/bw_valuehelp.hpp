#pragma once

#include <erpl_adt/adt/i_adt_session.hpp>
#include <erpl_adt/core/result.hpp>

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace erpl_adt {

struct BwValueHelpOptions {
    std::string domain;
    std::optional<std::string> raw_query;
    std::optional<int> max_rows;
    std::optional<std::string> pattern;
    std::optional<std::string> object_type;
    std::optional<std::string> infoprovider;
};

struct BwValueHelpRow {
    std::map<std::string, std::string> fields;
};

[[nodiscard]] Result<std::vector<BwValueHelpRow>, Error>
BwGetValueHelp(IAdtSession& session, const BwValueHelpOptions& options);

[[nodiscard]] Result<std::vector<BwValueHelpRow>, Error>
BwGetVirtualFolders(IAdtSession& session,
                    const std::optional<std::string>& package_name,
                    const std::optional<std::string>& object_type,
                    const std::optional<std::string>& user_name);

[[nodiscard]] Result<std::vector<BwValueHelpRow>, Error>
BwGetDataVolumes(IAdtSession& session,
                 const std::optional<std::string>& infoprovider,
                 const std::optional<int>& max_rows);

}  // namespace erpl_adt
