#pragma once

#include <erpl_adt/core/result.hpp>

#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace erpl_adt {

// ---------------------------------------------------------------------------
// PackageName — validated ABAP package name.
//
// Rules:
//   - Non-empty, max 30 characters
//   - Uppercase ASCII letters, digits, underscores, and '/' for namespaces
//   - Namespace form: /NAMESPACE/NAME (must start and end with /)
//   - Non-namespace: starts with letter, allows Z*, Y*, $TMP
// ---------------------------------------------------------------------------
class PackageName {
public:
    static Result<PackageName, std::string> Create(std::string_view name);

    [[nodiscard]] const std::string& Value() const noexcept { return value_; }

    bool operator==(const PackageName& other) const { return value_ == other.value_; }
    bool operator!=(const PackageName& other) const { return value_ != other.value_; }

    PackageName(const PackageName&) = default;
    PackageName& operator=(const PackageName&) = default;
    PackageName(PackageName&&) noexcept = default;
    PackageName& operator=(PackageName&&) noexcept = default;

private:
    explicit PackageName(std::string value) : value_(std::move(value)) {}
    std::string value_;
};

// ---------------------------------------------------------------------------
// RepoUrl — validated HTTPS repository URL.
// ---------------------------------------------------------------------------
class RepoUrl {
public:
    static Result<RepoUrl, std::string> Create(std::string_view url);

    [[nodiscard]] const std::string& Value() const noexcept { return value_; }

    bool operator==(const RepoUrl& other) const { return value_ == other.value_; }
    bool operator!=(const RepoUrl& other) const { return value_ != other.value_; }

    RepoUrl(const RepoUrl&) = default;
    RepoUrl& operator=(const RepoUrl&) = default;
    RepoUrl(RepoUrl&&) noexcept = default;
    RepoUrl& operator=(RepoUrl&&) noexcept = default;

private:
    explicit RepoUrl(std::string value) : value_(std::move(value)) {}
    std::string value_;
};

// ---------------------------------------------------------------------------
// BranchRef — git branch reference (default: "refs/heads/main").
// ---------------------------------------------------------------------------
class BranchRef {
public:
    static Result<BranchRef, std::string> Create(std::string_view ref);

    [[nodiscard]] const std::string& Value() const noexcept { return value_; }

    bool operator==(const BranchRef& other) const { return value_ == other.value_; }
    bool operator!=(const BranchRef& other) const { return value_ != other.value_; }

    BranchRef(const BranchRef&) = default;
    BranchRef& operator=(const BranchRef&) = default;
    BranchRef(BranchRef&&) noexcept = default;
    BranchRef& operator=(BranchRef&&) noexcept = default;

private:
    explicit BranchRef(std::string value) : value_(std::move(value)) {}
    std::string value_;
};

// ---------------------------------------------------------------------------
// RepoKey — opaque string key returned by SAP. Non-empty.
// ---------------------------------------------------------------------------
class RepoKey {
public:
    static Result<RepoKey, std::string> Create(std::string_view key);

    [[nodiscard]] const std::string& Value() const noexcept { return value_; }

    bool operator==(const RepoKey& other) const { return value_ == other.value_; }
    bool operator!=(const RepoKey& other) const { return value_ != other.value_; }

    RepoKey(const RepoKey&) = default;
    RepoKey& operator=(const RepoKey&) = default;
    RepoKey(RepoKey&&) noexcept = default;
    RepoKey& operator=(RepoKey&&) noexcept = default;

private:
    explicit RepoKey(std::string value) : value_(std::move(value)) {}
    std::string value_;
};

// ---------------------------------------------------------------------------
// SapClient — exactly 3 digits (e.g. "001").
// ---------------------------------------------------------------------------
class SapClient {
public:
    static Result<SapClient, std::string> Create(std::string_view client);

    [[nodiscard]] const std::string& Value() const noexcept { return value_; }

    bool operator==(const SapClient& other) const { return value_ == other.value_; }
    bool operator!=(const SapClient& other) const { return value_ != other.value_; }

    SapClient(const SapClient&) = default;
    SapClient& operator=(const SapClient&) = default;
    SapClient(SapClient&&) noexcept = default;
    SapClient& operator=(SapClient&&) noexcept = default;

private:
    explicit SapClient(std::string value) : value_(std::move(value)) {}
    std::string value_;
};

// ---------------------------------------------------------------------------
// ObjectUri — validated ADT object URI (starts with /sap/bc/adt/).
// ---------------------------------------------------------------------------
class ObjectUri {
public:
    static Result<ObjectUri, std::string> Create(std::string_view uri);

    [[nodiscard]] const std::string& Value() const noexcept { return value_; }

    bool operator==(const ObjectUri& other) const { return value_ == other.value_; }
    bool operator!=(const ObjectUri& other) const { return value_ != other.value_; }

    ObjectUri(const ObjectUri&) = default;
    ObjectUri& operator=(const ObjectUri&) = default;
    ObjectUri(ObjectUri&&) noexcept = default;
    ObjectUri& operator=(ObjectUri&&) noexcept = default;

private:
    explicit ObjectUri(std::string value) : value_(std::move(value)) {}
    std::string value_;
};

// ---------------------------------------------------------------------------
// ObjectType — ABAP object type code (e.g. "CLAS/OC", "PROG/P").
// ---------------------------------------------------------------------------
class ObjectType {
public:
    static Result<ObjectType, std::string> Create(std::string_view type);

    [[nodiscard]] const std::string& Value() const noexcept { return value_; }

    bool operator==(const ObjectType& other) const { return value_ == other.value_; }
    bool operator!=(const ObjectType& other) const { return value_ != other.value_; }

    ObjectType(const ObjectType&) = default;
    ObjectType& operator=(const ObjectType&) = default;
    ObjectType(ObjectType&&) noexcept = default;
    ObjectType& operator=(ObjectType&&) noexcept = default;

private:
    explicit ObjectType(std::string value) : value_(std::move(value)) {}
    std::string value_;
};

// ---------------------------------------------------------------------------
// TransportId — transport request number (e.g. "NPLK900001").
// Pattern: 3 uppercase letters + 1 uppercase letter + 6 digits.
// ---------------------------------------------------------------------------
class TransportId {
public:
    static Result<TransportId, std::string> Create(std::string_view id);

    [[nodiscard]] const std::string& Value() const noexcept { return value_; }

    bool operator==(const TransportId& other) const { return value_ == other.value_; }
    bool operator!=(const TransportId& other) const { return value_ != other.value_; }

    TransportId(const TransportId&) = default;
    TransportId& operator=(const TransportId&) = default;
    TransportId(TransportId&&) noexcept = default;
    TransportId& operator=(TransportId&&) noexcept = default;

private:
    explicit TransportId(std::string value) : value_(std::move(value)) {}
    std::string value_;
};

// ---------------------------------------------------------------------------
// LockHandle — opaque lock handle string from _lock endpoint.
// ---------------------------------------------------------------------------
class LockHandle {
public:
    static Result<LockHandle, std::string> Create(std::string_view handle);

    [[nodiscard]] const std::string& Value() const noexcept { return value_; }

    bool operator==(const LockHandle& other) const { return value_ == other.value_; }
    bool operator!=(const LockHandle& other) const { return value_ != other.value_; }

    LockHandle(const LockHandle&) = default;
    LockHandle& operator=(const LockHandle&) = default;
    LockHandle(LockHandle&&) noexcept = default;
    LockHandle& operator=(LockHandle&&) noexcept = default;

private:
    explicit LockHandle(std::string value) : value_(std::move(value)) {}
    std::string value_;
};

// ---------------------------------------------------------------------------
// CheckVariant — ATC check variant name (e.g. "FUNCTIONAL_DB_ADDITION").
// ---------------------------------------------------------------------------
class CheckVariant {
public:
    static Result<CheckVariant, std::string> Create(std::string_view variant);

    [[nodiscard]] const std::string& Value() const noexcept { return value_; }

    bool operator==(const CheckVariant& other) const { return value_ == other.value_; }
    bool operator!=(const CheckVariant& other) const { return value_ != other.value_; }

    CheckVariant(const CheckVariant&) = default;
    CheckVariant& operator=(const CheckVariant&) = default;
    CheckVariant(CheckVariant&&) noexcept = default;
    CheckVariant& operator=(CheckVariant&&) noexcept = default;

private:
    explicit CheckVariant(std::string value) : value_(std::move(value)) {}
    std::string value_;
};

// ---------------------------------------------------------------------------
// SapLanguage — ISO language code mapped to SAP language key (e.g. "EN").
// ---------------------------------------------------------------------------
class SapLanguage {
public:
    static Result<SapLanguage, std::string> Create(std::string_view lang);

    [[nodiscard]] const std::string& Value() const noexcept { return value_; }

    bool operator==(const SapLanguage& other) const { return value_ == other.value_; }
    bool operator!=(const SapLanguage& other) const { return value_ != other.value_; }

    SapLanguage(const SapLanguage&) = default;
    SapLanguage& operator=(const SapLanguage&) = default;
    SapLanguage(SapLanguage&&) noexcept = default;
    SapLanguage& operator=(SapLanguage&&) noexcept = default;

private:
    explicit SapLanguage(std::string value) : value_(std::move(value)) {}
    std::string value_;
};

} // namespace erpl_adt

// ---------------------------------------------------------------------------
// std::hash specializations
// ---------------------------------------------------------------------------
namespace std {

template <>
struct hash<erpl_adt::PackageName> {
    size_t operator()(const erpl_adt::PackageName& p) const noexcept {
        return hash<string>{}(p.Value());
    }
};

template <>
struct hash<erpl_adt::RepoUrl> {
    size_t operator()(const erpl_adt::RepoUrl& r) const noexcept {
        return hash<string>{}(r.Value());
    }
};

template <>
struct hash<erpl_adt::BranchRef> {
    size_t operator()(const erpl_adt::BranchRef& b) const noexcept {
        return hash<string>{}(b.Value());
    }
};

template <>
struct hash<erpl_adt::RepoKey> {
    size_t operator()(const erpl_adt::RepoKey& k) const noexcept {
        return hash<string>{}(k.Value());
    }
};

template <>
struct hash<erpl_adt::SapClient> {
    size_t operator()(const erpl_adt::SapClient& c) const noexcept {
        return hash<string>{}(c.Value());
    }
};

template <>
struct hash<erpl_adt::ObjectUri> {
    size_t operator()(const erpl_adt::ObjectUri& u) const noexcept {
        return hash<string>{}(u.Value());
    }
};

template <>
struct hash<erpl_adt::ObjectType> {
    size_t operator()(const erpl_adt::ObjectType& t) const noexcept {
        return hash<string>{}(t.Value());
    }
};

template <>
struct hash<erpl_adt::TransportId> {
    size_t operator()(const erpl_adt::TransportId& t) const noexcept {
        return hash<string>{}(t.Value());
    }
};

template <>
struct hash<erpl_adt::LockHandle> {
    size_t operator()(const erpl_adt::LockHandle& h) const noexcept {
        return hash<string>{}(h.Value());
    }
};

template <>
struct hash<erpl_adt::CheckVariant> {
    size_t operator()(const erpl_adt::CheckVariant& v) const noexcept {
        return hash<string>{}(v.Value());
    }
};

template <>
struct hash<erpl_adt::SapLanguage> {
    size_t operator()(const erpl_adt::SapLanguage& l) const noexcept {
        return hash<string>{}(l.Value());
    }
};

} // namespace std
