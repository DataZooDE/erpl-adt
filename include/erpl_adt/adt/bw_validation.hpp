#pragma once

#include <erpl_adt/adt/i_adt_session.hpp>
#include <erpl_adt/core/result.hpp>

#include <optional>
#include <string>
#include <vector>

namespace erpl_adt {

struct BwValidationOptions {
    std::string object_type;
    std::string object_name;
    std::string action = "validate";
};

struct BwValidationMessage {
    std::string severity;
    std::string text;
    std::string object_type;
    std::string object_name;
    std::string code;
};

struct BwMoveRequestEntry {
    std::string request;
    std::string owner;
    std::string status;
    std::string description;
};

[[nodiscard]] Result<std::vector<BwValidationMessage>, Error>
BwValidateObject(IAdtSession& session, const BwValidationOptions& options);

[[nodiscard]] Result<std::vector<BwMoveRequestEntry>, Error>
BwListMoveRequests(IAdtSession& session);

}  // namespace erpl_adt
