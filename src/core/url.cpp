#include <erpl_adt/core/url.hpp>

#include <iomanip>
#include <sstream>

namespace erpl_adt {

std::string UrlEncode(const std::string& value) {
    std::ostringstream encoded;
    encoded.fill('0');
    encoded << std::hex << std::uppercase;
    for (unsigned char c : value) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            encoded << c;
        } else {
            encoded << '%' << std::setw(2) << static_cast<int>(c);
        }
    }
    return encoded.str();
}

} // namespace erpl_adt
