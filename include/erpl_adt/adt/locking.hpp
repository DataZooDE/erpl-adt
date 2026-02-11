#pragma once

#include <erpl_adt/adt/i_adt_session.hpp>
#include <erpl_adt/core/result.hpp>
#include <erpl_adt/core/types.hpp>

#include <string>

namespace erpl_adt {

// ---------------------------------------------------------------------------
// LockResult — result of a successful lock operation.
// ---------------------------------------------------------------------------
struct LockResult {
    LockHandle handle;
    std::string transport_number;  // CORRNR
    std::string transport_owner;   // CORRUSER
    std::string transport_text;    // CORRTEXT
};

// ---------------------------------------------------------------------------
// LockObject — acquire a lock on an ADT object.
//
// Endpoint: POST {objectUri}?_action=LOCK&accessMode=MODIFY
// Requires stateful session.
// ---------------------------------------------------------------------------
[[nodiscard]] Result<LockResult, Error> LockObject(
    IAdtSession& session,
    const ObjectUri& uri);

// ---------------------------------------------------------------------------
// UnlockObject — release a lock on an ADT object.
//
// Endpoint: POST {objectUri}?_action=UNLOCK&lockHandle={handle}
// ---------------------------------------------------------------------------
[[nodiscard]] Result<void, Error> UnlockObject(
    IAdtSession& session,
    const ObjectUri& uri,
    const LockHandle& handle);

// ---------------------------------------------------------------------------
// LockGuard — RAII wrapper for lock lifecycle.
//
// Acquires lock on construction (or fails). Unlocks on destruction.
// Enables stateful session for the lock duration.
// ---------------------------------------------------------------------------
class LockGuard {
public:
    // Factory: acquires lock and enables stateful session.
    [[nodiscard]] static Result<LockGuard, Error> Acquire(
        IAdtSession& session,
        const ObjectUri& uri);

    ~LockGuard();

    // Non-copyable, movable.
    LockGuard(const LockGuard&) = delete;
    LockGuard& operator=(const LockGuard&) = delete;
    LockGuard(LockGuard&& other) noexcept;
    LockGuard& operator=(LockGuard&& other) noexcept;

    [[nodiscard]] const LockHandle& Handle() const noexcept { return result_.handle; }
    [[nodiscard]] const LockResult& LockInfo() const noexcept { return result_; }

private:
    LockGuard(IAdtSession& session, const ObjectUri& uri, LockResult result);

    IAdtSession* session_;
    ObjectUri uri_;
    LockResult result_;
    bool released_ = false;
};

} // namespace erpl_adt
