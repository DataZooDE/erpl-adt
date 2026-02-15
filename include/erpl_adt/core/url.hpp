#pragma once

#include <string>

namespace erpl_adt {

// Percent-encode a string per RFC 3986.
// Unreserved characters (alphanumeric, '-', '_', '.', '~') pass through;
// everything else is replaced with %XX (uppercase hex).
std::string UrlEncode(const std::string& value);

} // namespace erpl_adt
