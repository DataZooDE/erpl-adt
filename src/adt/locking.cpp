#include <erpl_adt/adt/locking.hpp>

#include "adt_utils.hpp"
#include <tinyxml2.h>

namespace erpl_adt {

namespace {

Result<LockResult, Error> ParseLockResponse(
    std::string_view xml, const std::string& uri) {
    tinyxml2::XMLDocument doc;
    if (auto parse_error = adt_utils::ParseXmlOrError(
            doc, xml, "LockObject", uri,
            "Failed to parse lock response XML",
            ErrorCategory::LockConflict)) {
        return Result<LockResult, Error>::Err(std::move(*parse_error));
    }

    // Navigate: asx:abap > asx:values > DATA
    auto* root = doc.RootElement();
    if (!root) {
        return Result<LockResult, Error>::Err(Error{
            "LockObject", uri, std::nullopt,
            "Empty lock response", std::nullopt,
            ErrorCategory::LockConflict});
    }

    auto* values = root->FirstChildElement();
    if (!values) {
        return Result<LockResult, Error>::Err(Error{
            "LockObject", uri, std::nullopt,
            "Missing values element in lock response", std::nullopt,
            ErrorCategory::LockConflict});
    }

    auto* data = values->FirstChildElement("DATA");
    if (!data) {
        return Result<LockResult, Error>::Err(Error{
            "LockObject", uri, std::nullopt,
            "Missing DATA element in lock response", std::nullopt,
            ErrorCategory::LockConflict});
    }

    auto get_text = [&](const char* name) -> std::string {
        auto* el = data->FirstChildElement(name);
        if (el && el->GetText()) return el->GetText();
        return "";
    };

    auto handle_str = get_text("LOCK_HANDLE");
    if (handle_str.empty()) {
        return Result<LockResult, Error>::Err(Error{
            "LockObject", uri, std::nullopt,
            "Empty LOCK_HANDLE in lock response", std::nullopt,
            ErrorCategory::LockConflict});
    }

    auto handle_result = LockHandle::Create(handle_str);
    if (handle_result.IsErr()) {
        return Result<LockResult, Error>::Err(Error{
            "LockObject", uri, std::nullopt,
            "Invalid lock handle: " + handle_result.Error(), std::nullopt,
            ErrorCategory::LockConflict});
    }

    return Result<LockResult, Error>::Ok(LockResult{
        std::move(handle_result).Value(),
        get_text("CORRNR"),
        get_text("CORRUSER"),
        get_text("CORRTEXT")});
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// LockObject
// ---------------------------------------------------------------------------
Result<LockResult, Error> LockObject(
    IAdtSession& session,
    const ObjectUri& uri) {
    auto lock_url = uri.Value() + "?_action=LOCK&accessMode=MODIFY";

    HttpHeaders headers;
    headers["Accept"] = "application/*,application/vnd.sap.as+xml;charset=UTF-8;"
                        "dataname=com.sap.adt.lock.result";

    auto response = session.Post(lock_url, "", "application/xml", headers);
    if (response.IsErr()) {
        return Result<LockResult, Error>::Err(std::move(response).Error());
    }

    const auto& http = response.Value();
    if (http.status_code == 409) {
        return Result<LockResult, Error>::Err(Error{
            "LockObject", uri.Value(), 409,
            "Object is locked by another user", std::nullopt,
            ErrorCategory::LockConflict});
    }
    if (http.status_code != 200) {
        return Result<LockResult, Error>::Err(
            Error::FromHttpStatus("LockObject", uri.Value(), http.status_code, http.body));
    }

    return ParseLockResponse(http.body, uri.Value());
}

// ---------------------------------------------------------------------------
// UnlockObject
// ---------------------------------------------------------------------------
Result<void, Error> UnlockObject(
    IAdtSession& session,
    const ObjectUri& uri,
    const LockHandle& handle) {
    auto unlock_url = uri.Value() + "?_action=UNLOCK&lockHandle=" + handle.Value();

    auto response = session.Post(unlock_url, "", "application/xml");
    if (response.IsErr()) {
        return Result<void, Error>::Err(std::move(response).Error());
    }

    const auto& http = response.Value();
    if (http.status_code != 200 && http.status_code != 204) {
        return Result<void, Error>::Err(
            Error::FromHttpStatus("UnlockObject", uri.Value(), http.status_code, http.body));
    }

    return Result<void, Error>::Ok();
}

// ---------------------------------------------------------------------------
// LockGuard
// ---------------------------------------------------------------------------
LockGuard::LockGuard(IAdtSession& session, const ObjectUri& uri, LockResult result)
    : session_(&session), uri_(uri), result_(std::move(result)) {}

Result<LockGuard, Error> LockGuard::Acquire(
    IAdtSession& session,
    const ObjectUri& uri) {
    session.SetStateful(true);

    auto lock_result = LockObject(session, uri);
    if (lock_result.IsErr()) {
        session.SetStateful(false);
        return Result<LockGuard, Error>::Err(std::move(lock_result).Error());
    }

    return Result<LockGuard, Error>::Ok(
        LockGuard(session, uri, std::move(lock_result).Value()));
}

LockGuard::~LockGuard() {
    if (!released_ && session_) {
        // Best-effort unlock; ignore errors in destructor.
        (void)UnlockObject(*session_, uri_, result_.handle);
        session_->SetStateful(false);
    }
}

LockGuard::LockGuard(LockGuard&& other) noexcept
    : session_(other.session_),
      uri_(std::move(other.uri_)),
      result_(std::move(other.result_)),
      released_(other.released_) {
    other.released_ = true;
    other.session_ = nullptr;
}

LockGuard& LockGuard::operator=(LockGuard&& other) noexcept {
    if (this != &other) {
        if (!released_ && session_) {
            (void)UnlockObject(*session_, uri_, result_.handle);
            session_->SetStateful(false);
        }
        session_ = other.session_;
        uri_ = std::move(other.uri_);
        result_ = std::move(other.result_);
        released_ = other.released_;
        other.released_ = true;
        other.session_ = nullptr;
    }
    return *this;
}

} // namespace erpl_adt
