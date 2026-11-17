#pragma once

#include <erpl_adt/core/result.hpp>

#include <optional>
#include <string>

namespace erpl_adt {

// ---------------------------------------------------------------------------
// The `mcp:` block of a YAML config (`erpl-adt mcp -c <file>`).
//
//   mcp:
//     abapgit_root: ./repos     # folder the abapgit_* tools may read/write
//
// A relative abapgit_root is resolved against the folder of the config file,
// so the setting means the same wherever the server is started. A config
// without an `mcp:` block is not an error.
// ---------------------------------------------------------------------------
struct McpServerFileSettings {
    std::optional<std::string> abapgit_root;  // absolute
};

[[nodiscard]] Result<McpServerFileSettings, Error> LoadMcpServerSettings(const std::string& path);

}  // namespace erpl_adt
