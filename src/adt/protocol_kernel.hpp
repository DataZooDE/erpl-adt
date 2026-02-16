#pragma once

#include "adt_utils.hpp"

#include <erpl_adt/adt/i_adt_session.hpp>
#include <erpl_adt/core/result.hpp>

#include <chrono>
#include <string>
#include <string_view>

namespace erpl_adt::protocol_kernel {

struct AsyncContract {
    std::string operation;
    std::string endpoint;
    std::string failed_message;
    std::string timeout_message;
    ErrorCategory failed_category = ErrorCategory::Internal;
};

inline Result<PollResult, Error> PollAcceptedOperation(
    IAdtSession& session,
    const HttpResponse& accepted_response,
    std::chrono::seconds timeout,
    const AsyncContract& contract) {
    if (accepted_response.status_code != 202) {
        return Result<PollResult, Error>::Err(Error{
            contract.operation,
            contract.endpoint,
            accepted_response.status_code,
            "expected HTTP 202 for async operation",
            std::nullopt,
            ErrorCategory::Internal});
    }

    auto location = adt_utils::RequireHeaderCi(
        accepted_response.headers,
        "Location",
        contract.operation,
        contract.endpoint,
        202);
    if (location.IsErr()) {
        return Result<PollResult, Error>::Err(std::move(location).Error());
    }

    auto poll = session.PollUntilComplete(location.Value(), timeout);
    if (poll.IsErr()) {
        return Result<PollResult, Error>::Err(std::move(poll).Error());
    }

    if (poll.Value().status == PollStatus::Failed) {
        return Result<PollResult, Error>::Err(Error{
            contract.operation,
            contract.endpoint,
            std::nullopt,
            contract.failed_message,
            std::nullopt,
            contract.failed_category});
    }

    if (poll.Value().status == PollStatus::Running) {
        return Result<PollResult, Error>::Err(Error{
            contract.operation,
            contract.endpoint,
            std::nullopt,
            contract.timeout_message.empty()
                ? "async operation did not complete within timeout"
                : contract.timeout_message,
            std::nullopt,
            ErrorCategory::Timeout});
    }

    return Result<PollResult, Error>::Ok(poll.Value());
}

} // namespace erpl_adt::protocol_kernel
