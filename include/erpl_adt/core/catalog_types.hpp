#pragma once

#include <erpl_adt/core/result.hpp>

#include <string>
#include <string_view>

namespace erpl_adt {

// ---------------------------------------------------------------------------
// CatalogDomain — which subsystem a catalog entity originates from.
// ---------------------------------------------------------------------------
enum class CatalogDomain {
    Abap,
    Ddic,
    Cds,
    Bw,
};

[[nodiscard]] std::string ToString(CatalogDomain domain);
[[nodiscard]] Result<CatalogDomain, std::string> CatalogDomainFromString(std::string_view s);

// ---------------------------------------------------------------------------
// EntityId — opaque, stable identifier for a catalog entity.
//
// Always produced by DeriveEntityId() (adt/catalog_ids.hpp) —
// sha256(system_sid|domain|object_type|technical_name) as lowercase hex —
// but validated here only as a non-empty opaque string so it can also be
// round-tripped from MCP/CLI callers (e.g. catalog_get id=...).
// ---------------------------------------------------------------------------
class EntityId {
public:
    static Result<EntityId, std::string> Create(std::string_view id);

    [[nodiscard]] const std::string& Value() const noexcept { return value_; }

    bool operator==(const EntityId& other) const { return value_ == other.value_; }
    bool operator!=(const EntityId& other) const { return value_ != other.value_; }
    bool operator<(const EntityId& other) const { return value_ < other.value_; }

    EntityId(const EntityId&) = default;
    EntityId& operator=(const EntityId&) = default;
    EntityId(EntityId&&) noexcept = default;
    EntityId& operator=(EntityId&&) noexcept = default;

private:
    explicit EntityId(std::string value) : value_(std::move(value)) {}
    std::string value_;
};

} // namespace erpl_adt

namespace std {

template <>
struct hash<erpl_adt::EntityId> {
    size_t operator()(const erpl_adt::EntityId& id) const noexcept {
        return hash<string>{}(id.Value());
    }
};

} // namespace std
