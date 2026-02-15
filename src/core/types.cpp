#include <erpl_adt/core/types.hpp>

#include <algorithm>
#include <cctype>

namespace erpl_adt {

namespace {

bool IsUpperAlphaOrDigitOrUnderscore(char c) {
    return (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
}

bool IsAllUpperAlphaDigitUnderscoreSlash(std::string_view s) {
    return std::all_of(s.begin(), s.end(), [](char c) {
        return IsUpperAlphaOrDigitOrUnderscore(c) || c == '/';
    });
}

bool StartsWithLetter(std::string_view s) {
    return !s.empty() && s[0] >= 'A' && s[0] <= 'Z';
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// PackageName
// ---------------------------------------------------------------------------
Result<PackageName, std::string> PackageName::Create(std::string_view name) {
    if (name.empty()) {
        return Result<PackageName, std::string>::Err("Package name must not be empty");
    }
    if (name.size() > 30) {
        return Result<PackageName, std::string>::Err(
            "Package name must be at most 30 characters, got " +
            std::to_string(name.size()));
    }

    // Allow $-prefixed package names (e.g. $TMP, $DEMO_SOI_DRAFT)
    if (name[0] == '$') {
        return Result<PackageName, std::string>::Ok(PackageName(std::string(name)));
    }

    if (!IsAllUpperAlphaDigitUnderscoreSlash(name)) {
        return Result<PackageName, std::string>::Err(
            "Package name must contain only uppercase letters, digits, underscores, "
            "and '/' for namespaces");
    }

    // Namespace form: /NAMESPACE/NAME
    if (name[0] == '/') {
        auto second_slash = name.find('/', 1);
        if (second_slash == std::string_view::npos) {
            return Result<PackageName, std::string>::Err(
                "Namespace package name must have the form /NAMESPACE/NAME");
        }
        if (second_slash == 1) {
            return Result<PackageName, std::string>::Err(
                "Namespace part must not be empty");
        }
        auto after_ns = name.substr(second_slash + 1);
        if (after_ns.empty()) {
            return Result<PackageName, std::string>::Err(
                "Package name after namespace must not be empty");
        }
        if (after_ns.find('/') != std::string_view::npos) {
            return Result<PackageName, std::string>::Err(
                "Package name must not contain additional '/' after namespace");
        }
        return Result<PackageName, std::string>::Ok(PackageName(std::string(name)));
    }

    // Non-namespace: must start with a letter
    if (!StartsWithLetter(name)) {
        return Result<PackageName, std::string>::Err(
            "Non-namespace package name must start with a letter");
    }

    return Result<PackageName, std::string>::Ok(PackageName(std::string(name)));
}

// ---------------------------------------------------------------------------
// RepoUrl
// ---------------------------------------------------------------------------
Result<RepoUrl, std::string> RepoUrl::Create(std::string_view url) {
    if (url.empty()) {
        return Result<RepoUrl, std::string>::Err("Repository URL must not be empty");
    }
    if (url.substr(0, 8) != "https://") {
        return Result<RepoUrl, std::string>::Err(
            "Repository URL must start with https://");
    }
    if (url.size() <= 8) {
        return Result<RepoUrl, std::string>::Err(
            "Repository URL must have a host after https://");
    }
    return Result<RepoUrl, std::string>::Ok(RepoUrl(std::string(url)));
}

// ---------------------------------------------------------------------------
// BranchRef
// ---------------------------------------------------------------------------
Result<BranchRef, std::string> BranchRef::Create(std::string_view ref) {
    if (ref.empty()) {
        return Result<BranchRef, std::string>::Err("Branch reference must not be empty");
    }
    return Result<BranchRef, std::string>::Ok(BranchRef(std::string(ref)));
}

// ---------------------------------------------------------------------------
// RepoKey
// ---------------------------------------------------------------------------
Result<RepoKey, std::string> RepoKey::Create(std::string_view key) {
    if (key.empty()) {
        return Result<RepoKey, std::string>::Err("Repository key must not be empty");
    }
    return Result<RepoKey, std::string>::Ok(RepoKey(std::string(key)));
}

// ---------------------------------------------------------------------------
// SapClient
// ---------------------------------------------------------------------------
Result<SapClient, std::string> SapClient::Create(std::string_view client) {
    if (client.size() != 3) {
        return Result<SapClient, std::string>::Err(
            "SAP client must be exactly 3 digits, got " +
            std::to_string(client.size()) + " characters");
    }
    if (!std::all_of(client.begin(), client.end(),
                     [](char c) { return c >= '0' && c <= '9'; })) {
        return Result<SapClient, std::string>::Err(
            "SAP client must contain only digits");
    }
    return Result<SapClient, std::string>::Ok(SapClient(std::string(client)));
}

// ---------------------------------------------------------------------------
// ObjectUri
// ---------------------------------------------------------------------------
Result<ObjectUri, std::string> ObjectUri::Create(std::string_view uri) {
    if (uri.empty()) {
        return Result<ObjectUri, std::string>::Err("Object URI must not be empty");
    }
    if (uri.substr(0, 12) != "/sap/bc/adt/") {
        return Result<ObjectUri, std::string>::Err(
            "Object URI must start with /sap/bc/adt/");
    }
    if (uri.size() <= 12) {
        return Result<ObjectUri, std::string>::Err(
            "Object URI must have a path after /sap/bc/adt/");
    }
    return Result<ObjectUri, std::string>::Ok(ObjectUri(std::string(uri)));
}

// ---------------------------------------------------------------------------
// ObjectType
// ---------------------------------------------------------------------------
Result<ObjectType, std::string> ObjectType::Create(std::string_view type) {
    if (type.empty()) {
        return Result<ObjectType, std::string>::Err("Object type must not be empty");
    }
    // Must contain a '/' separator (e.g. "CLAS/OC", "PROG/P")
    auto slash = type.find('/');
    if (slash == std::string_view::npos) {
        return Result<ObjectType, std::string>::Err(
            "Object type must contain a '/' separator (e.g. CLAS/OC)");
    }
    if (slash == 0) {
        return Result<ObjectType, std::string>::Err(
            "Object type category must not be empty");
    }
    if (slash == type.size() - 1) {
        return Result<ObjectType, std::string>::Err(
            "Object type subcategory must not be empty");
    }
    // Validate characters: uppercase letters, digits, underscores
    for (char c : type) {
        if (c != '/' && !IsUpperAlphaOrDigitOrUnderscore(c)) {
            return Result<ObjectType, std::string>::Err(
                "Object type must contain only uppercase letters, digits, "
                "underscores, and one '/' separator");
        }
    }
    return Result<ObjectType, std::string>::Ok(ObjectType(std::string(type)));
}

// ---------------------------------------------------------------------------
// TransportId
// ---------------------------------------------------------------------------
Result<TransportId, std::string> TransportId::Create(std::string_view id) {
    if (id.size() != 10) {
        return Result<TransportId, std::string>::Err(
            "Transport ID must be exactly 10 characters (e.g. NPLK900001), got " +
            std::to_string(id.size()));
    }
    // First 4 characters: uppercase letters
    for (size_t i = 0; i < 4; ++i) {
        if (id[i] < 'A' || id[i] > 'Z') {
            return Result<TransportId, std::string>::Err(
                "Transport ID must start with 4 uppercase letters");
        }
    }
    // Last 6 characters: digits
    for (size_t i = 4; i < 10; ++i) {
        if (id[i] < '0' || id[i] > '9') {
            return Result<TransportId, std::string>::Err(
                "Transport ID must end with 6 digits");
        }
    }
    return Result<TransportId, std::string>::Ok(TransportId(std::string(id)));
}

// ---------------------------------------------------------------------------
// LockHandle
// ---------------------------------------------------------------------------
Result<LockHandle, std::string> LockHandle::Create(std::string_view handle) {
    if (handle.empty()) {
        return Result<LockHandle, std::string>::Err("Lock handle must not be empty");
    }
    return Result<LockHandle, std::string>::Ok(LockHandle(std::string(handle)));
}

// ---------------------------------------------------------------------------
// CheckVariant
// ---------------------------------------------------------------------------
Result<CheckVariant, std::string> CheckVariant::Create(std::string_view variant) {
    if (variant.empty()) {
        return Result<CheckVariant, std::string>::Err("Check variant must not be empty");
    }
    return Result<CheckVariant, std::string>::Ok(CheckVariant(std::string(variant)));
}

// ---------------------------------------------------------------------------
// SapLanguage
// ---------------------------------------------------------------------------
Result<SapLanguage, std::string> SapLanguage::Create(std::string_view lang) {
    if (lang.size() != 2) {
        return Result<SapLanguage, std::string>::Err(
            "SAP language must be exactly 2 characters, got " +
            std::to_string(lang.size()));
    }
    if (lang[0] < 'A' || lang[0] > 'Z' || lang[1] < 'A' || lang[1] > 'Z') {
        return Result<SapLanguage, std::string>::Err(
            "SAP language must be 2 uppercase letters (e.g. EN)");
    }
    return Result<SapLanguage, std::string>::Ok(SapLanguage(std::string(lang)));
}

} // namespace erpl_adt
