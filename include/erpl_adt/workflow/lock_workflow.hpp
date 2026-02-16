#pragma once

#include <erpl_adt/adt/i_adt_session.hpp>
#include <erpl_adt/core/result.hpp>
#include <erpl_adt/core/types.hpp>

#include <optional>
#include <string>
#include <string_view>

namespace erpl_adt {

[[nodiscard]] Result<void, Error> DeleteObjectWithAutoLock(
    IAdtSession& session,
    const ObjectUri& object_uri,
    const std::optional<std::string>& transport);

[[nodiscard]] Result<std::string, Error> WriteSourceWithAutoLock(
    IAdtSession& session,
    std::string_view source_uri,
    std::string_view source,
    const std::optional<std::string>& transport);

} // namespace erpl_adt
