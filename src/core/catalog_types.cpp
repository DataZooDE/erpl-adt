#include <erpl_adt/core/catalog_types.hpp>

namespace erpl_adt {

std::string ToString(CatalogDomain domain) {
    switch (domain) {
        case CatalogDomain::Abap: return "ABAP";
        case CatalogDomain::Ddic: return "DDIC";
        case CatalogDomain::Cds:  return "CDS";
        case CatalogDomain::Bw:   return "BW";
    }
    return "UNKNOWN";
}

Result<CatalogDomain, std::string> CatalogDomainFromString(std::string_view s) {
    if (s == "ABAP") return Result<CatalogDomain, std::string>::Ok(CatalogDomain::Abap);
    if (s == "DDIC") return Result<CatalogDomain, std::string>::Ok(CatalogDomain::Ddic);
    if (s == "CDS")  return Result<CatalogDomain, std::string>::Ok(CatalogDomain::Cds);
    if (s == "BW")   return Result<CatalogDomain, std::string>::Ok(CatalogDomain::Bw);
    return Result<CatalogDomain, std::string>::Err(
        "Unknown catalog domain '" + std::string(s) + "' (expected ABAP|DDIC|CDS|BW)");
}

Result<EntityId, std::string> EntityId::Create(std::string_view id) {
    if (id.empty()) {
        return Result<EntityId, std::string>::Err("Entity ID must not be empty");
    }
    return Result<EntityId, std::string>::Ok(EntityId(std::string(id)));
}

} // namespace erpl_adt
