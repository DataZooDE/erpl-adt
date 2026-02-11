#pragma once

#include <erpl_adt/adt/i_adt_session.hpp>

#include <deque>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>

namespace erpl_adt {
namespace testing {

// ---------------------------------------------------------------------------
// MockAdtSession — hand-written mock for offline unit testing.
//
// Usage:
//   MockAdtSession mock;
//   mock.EnqueueGet(Result<HttpResponse, Error>::Ok({200, {}, "<xml/>"}));
//   auto result = mock.Get("/sap/bc/adt/packages/ZTEST", {});
//   CHECK(mock.GetCallCount() == 1);
//   CHECK(mock.GetCalls()[0].path == "/sap/bc/adt/packages/ZTEST");
//
// Responses are consumed FIFO. If the queue is empty when a method is called,
// the mock returns a descriptive error rather than crashing.
// ---------------------------------------------------------------------------

// -- Call records -----------------------------------------------------------

struct GetCall {
    std::string path;
    HttpHeaders headers;
};

struct PostCall {
    std::string path;
    std::string body;
    std::string content_type;
    HttpHeaders headers;
};

struct PutCall {
    std::string path;
    std::string body;
    std::string content_type;
    HttpHeaders headers;
};

struct DeleteCall {
    std::string path;
    HttpHeaders headers;
};

struct PollCall {
    std::string location_url;
    std::chrono::seconds timeout;
};

// -- Mock class -------------------------------------------------------------

class MockAdtSession : public IAdtSession {
public:
    MockAdtSession() = default;

    // -- Enqueue canned responses -------------------------------------------

    void EnqueueGet(Result<HttpResponse, Error> response) {
        get_responses_.push_back(std::move(response));
    }

    void EnqueuePost(Result<HttpResponse, Error> response) {
        post_responses_.push_back(std::move(response));
    }

    void EnqueuePut(Result<HttpResponse, Error> response) {
        put_responses_.push_back(std::move(response));
    }

    void EnqueueDelete(Result<HttpResponse, Error> response) {
        delete_responses_.push_back(std::move(response));
    }

    void EnqueueCsrfToken(Result<std::string, Error> response) {
        csrf_responses_.push_back(std::move(response));
    }

    void EnqueuePoll(Result<PollResult, Error> response) {
        poll_responses_.push_back(std::move(response));
    }

    // -- Call history accessors ----------------------------------------------

    [[nodiscard]] const std::vector<GetCall>& GetCalls() const noexcept {
        return get_calls_;
    }
    [[nodiscard]] size_t GetCallCount() const noexcept {
        return get_calls_.size();
    }

    [[nodiscard]] const std::vector<PostCall>& PostCalls() const noexcept {
        return post_calls_;
    }
    [[nodiscard]] size_t PostCallCount() const noexcept {
        return post_calls_.size();
    }

    [[nodiscard]] const std::vector<PutCall>& PutCalls() const noexcept {
        return put_calls_;
    }
    [[nodiscard]] size_t PutCallCount() const noexcept {
        return put_calls_.size();
    }

    [[nodiscard]] const std::vector<DeleteCall>& DeleteCalls() const noexcept {
        return delete_calls_;
    }
    [[nodiscard]] size_t DeleteCallCount() const noexcept {
        return delete_calls_.size();
    }

    [[nodiscard]] const std::vector<PollCall>& PollCalls() const noexcept {
        return poll_calls_;
    }
    [[nodiscard]] size_t PollCallCount() const noexcept {
        return poll_calls_.size();
    }

    [[nodiscard]] size_t CsrfCallCount() const noexcept {
        return csrf_call_count_;
    }

    // -- Reset ---------------------------------------------------------------

    void Reset() {
        get_responses_.clear();
        post_responses_.clear();
        put_responses_.clear();
        delete_responses_.clear();
        csrf_responses_.clear();
        poll_responses_.clear();
        get_calls_.clear();
        post_calls_.clear();
        put_calls_.clear();
        delete_calls_.clear();
        poll_calls_.clear();
        csrf_call_count_ = 0;
        stateful_ = false;
    }

    // -- IAdtSession implementation ------------------------------------------

    Result<HttpResponse, Error> Get(
        std::string_view path,
        const HttpHeaders& headers) override {
        get_calls_.push_back({std::string(path), headers});
        return Dequeue(get_responses_, "Get", path);
    }

    Result<HttpResponse, Error> Post(
        std::string_view path,
        std::string_view body,
        std::string_view content_type,
        const HttpHeaders& headers) override {
        post_calls_.push_back({
            std::string(path),
            std::string(body),
            std::string(content_type),
            headers,
        });
        return Dequeue(post_responses_, "Post", path);
    }

    Result<HttpResponse, Error> Put(
        std::string_view path,
        std::string_view body,
        std::string_view content_type,
        const HttpHeaders& headers) override {
        put_calls_.push_back({
            std::string(path),
            std::string(body),
            std::string(content_type),
            headers,
        });
        return Dequeue(put_responses_, "Put", path);
    }

    Result<HttpResponse, Error> Delete(
        std::string_view path,
        const HttpHeaders& headers) override {
        delete_calls_.push_back({std::string(path), headers});
        return Dequeue(delete_responses_, "Delete", path);
    }

    void SetStateful(bool enabled) override {
        stateful_ = enabled;
    }

    [[nodiscard]] bool IsStateful() const override {
        return stateful_;
    }

    Result<std::string, Error> FetchCsrfToken() override {
        ++csrf_call_count_;
        if (csrf_responses_.empty()) {
            return Result<std::string, Error>::Err(Error{
                "FetchCsrfToken", "", std::nullopt,
                "MockAdtSession: no CSRF responses enqueued", std::nullopt});
        }
        auto response = std::move(csrf_responses_.front());
        csrf_responses_.pop_front();
        return response;
    }

    Result<PollResult, Error> PollUntilComplete(
        std::string_view location_url,
        std::chrono::seconds timeout) override {
        poll_calls_.push_back({std::string(location_url), timeout});
        if (poll_responses_.empty()) {
            return Result<PollResult, Error>::Err(Error{
                "PollUntilComplete", std::string(location_url), std::nullopt,
                "MockAdtSession: no poll responses enqueued", std::nullopt});
        }
        auto response = std::move(poll_responses_.front());
        poll_responses_.pop_front();
        return response;
    }

private:
    static Result<HttpResponse, Error> Dequeue(
        std::deque<Result<HttpResponse, Error>>& queue,
        std::string_view operation,
        std::string_view path) {
        if (queue.empty()) {
            return Result<HttpResponse, Error>::Err(Error{
                std::string(operation), std::string(path), std::nullopt,
                "MockAdtSession: no responses enqueued", std::nullopt});
        }
        auto response = std::move(queue.front());
        queue.pop_front();
        return response;
    }

    // Response queues (FIFO)
    std::deque<Result<HttpResponse, Error>> get_responses_;
    std::deque<Result<HttpResponse, Error>> post_responses_;
    std::deque<Result<HttpResponse, Error>> put_responses_;
    std::deque<Result<HttpResponse, Error>> delete_responses_;
    std::deque<Result<std::string, Error>> csrf_responses_;
    std::deque<Result<PollResult, Error>> poll_responses_;

    // Call history
    std::vector<GetCall> get_calls_;
    std::vector<PostCall> post_calls_;
    std::vector<PutCall> put_calls_;
    std::vector<DeleteCall> delete_calls_;
    std::vector<PollCall> poll_calls_;
    size_t csrf_call_count_ = 0;
    bool stateful_ = false;
};

} // namespace testing
} // namespace erpl_adt
