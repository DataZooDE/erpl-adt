#pragma once

#include <erpl_adt/adt/i_adt_session.hpp>

#include <optional>
#include <string>

namespace erpl_adt {

// Extra BW request-context headers used during lock/transport flows.
struct BwContextHeaders {
    std::optional<std::string> transport_lock_holder;
    std::optional<std::string> foreign_objects;
    std::optional<std::string> foreign_object_locks;
    std::optional<std::string> foreign_correction_number;
    std::optional<std::string> foreign_package;
};

void ApplyBwContextHeaders(const BwContextHeaders& context, HttpHeaders& headers);

}  // namespace erpl_adt
