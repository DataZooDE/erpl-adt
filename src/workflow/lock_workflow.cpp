#include <erpl_adt/workflow/lock_workflow.hpp>

#include <erpl_adt/adt/locking.hpp>
#include <erpl_adt/adt/object.hpp>
#include <erpl_adt/adt/source.hpp>

#include <string>

namespace erpl_adt {

namespace {

Result<ObjectUri, Error> ObjectUriFromSourceUri(std::string_view source_uri) {
    const auto slash_pos = source_uri.find("/source/");
    if (slash_pos == std::string_view::npos) {
        return Result<ObjectUri, Error>::Err(Error{
            "WriteSourceWithAutoLock",
            std::string(source_uri),
            std::nullopt,
            "Cannot derive object URI from source URI (expected /source/ segment)",
            std::nullopt});
    }

    const std::string object_uri(source_uri.substr(0, slash_pos));
    auto uri = ObjectUri::Create(object_uri);
    if (uri.IsErr()) {
        return Result<ObjectUri, Error>::Err(Error{
            "WriteSourceWithAutoLock",
            object_uri,
            std::nullopt,
            "Invalid object URI derived from source URI: " + uri.Error(),
            std::nullopt});
    }
    return Result<ObjectUri, Error>::Ok(std::move(uri).Value());
}

} // anonymous namespace

Result<void, Error> DeleteObjectWithAutoLock(
    IAdtSession& session,
    const ObjectUri& object_uri,
    const std::optional<std::string>& transport) {
    auto guard = LockGuard::Acquire(session, object_uri);
    if (guard.IsErr()) {
        return Result<void, Error>::Err(std::move(guard).Error());
    }

    auto result = DeleteObject(
        session, object_uri, guard.Value().Handle(), transport);
    if (result.IsErr()) {
        return Result<void, Error>::Err(std::move(result).Error());
    }
    return Result<void, Error>::Ok();
}

Result<std::string, Error> WriteSourceWithAutoLock(
    IAdtSession& session,
    std::string_view source_uri,
    std::string_view source,
    const std::optional<std::string>& transport) {
    const std::string source_uri_str(source_uri);
    const std::string source_str(source);
    auto object_uri = ObjectUriFromSourceUri(source_uri);
    if (object_uri.IsErr()) {
        return Result<std::string, Error>::Err(std::move(object_uri).Error());
    }

    auto guard = LockGuard::Acquire(session, object_uri.Value());
    if (guard.IsErr()) {
        return Result<std::string, Error>::Err(std::move(guard).Error());
    }

    auto write_result = WriteSource(
        session, source_uri_str, source_str, guard.Value().Handle(), transport);
    if (write_result.IsErr()) {
        return Result<std::string, Error>::Err(std::move(write_result).Error());
    }

    return Result<std::string, Error>::Ok(object_uri.Value().Value());
}

} // namespace erpl_adt
