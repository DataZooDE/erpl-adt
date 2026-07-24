#include <erpl_adt/core/gemini_embedding_provider.hpp>

#include <httplib.h>
#include <nlohmann/json.hpp>

#include <cstdlib>

namespace erpl_adt {

namespace {
constexpr const char* kHost = "generativelanguage.googleapis.com";
}

GeminiEmbeddingProvider::GeminiEmbeddingProvider(std::string api_key)
    : api_key_(std::move(api_key)) {}

Result<std::unique_ptr<GeminiEmbeddingProvider>, std::string>
GeminiEmbeddingProvider::CreateFromEnv() {
    const char* key = std::getenv("GEMINI_API_KEY");
    if (!key || std::string(key).empty()) {
        return Result<std::unique_ptr<GeminiEmbeddingProvider>, std::string>::Err(
            "GEMINI_API_KEY environment variable is not set");
    }
    return Create(key);
}

Result<std::unique_ptr<GeminiEmbeddingProvider>, std::string>
GeminiEmbeddingProvider::Create(std::string api_key) {
    if (api_key.empty()) {
        return Result<std::unique_ptr<GeminiEmbeddingProvider>, std::string>::Err(
            "Gemini API key must not be empty");
    }
    return Result<std::unique_ptr<GeminiEmbeddingProvider>, std::string>::Ok(
        std::unique_ptr<GeminiEmbeddingProvider>(new GeminiEmbeddingProvider(std::move(api_key))));
}

Result<std::vector<float>, Error> GeminiEmbeddingProvider::EmbedText(const std::string& text) {
    httplib::Client client(std::string("https://") + kHost);
    client.set_connection_timeout(10, 0);
    client.set_read_timeout(30, 0);

    nlohmann::json body;
    body["model"] = "models/text-embedding-004";
    body["content"]["parts"] = nlohmann::json::array({{{"text", text}}});

    auto path = "/v1beta/models/text-embedding-004:embedContent?key=" + api_key_;
    auto response = client.Post(path, body.dump(), "application/json");

    if (!response) {
        return Result<std::vector<float>, Error>::Err(Error{
            "GeminiEmbeddingProvider::EmbedText", path, std::nullopt,
            "No response from Gemini embeddings API (network error)", std::nullopt,
            ErrorCategory::Connection});
    }
    if (response->status != 200) {
        return Result<std::vector<float>, Error>::Err(Error::FromHttpStatus(
            "GeminiEmbeddingProvider::EmbedText", "/v1beta/models/text-embedding-004:embedContent",
            response->status, response->body));
    }

    try {
        auto j = nlohmann::json::parse(response->body);
        std::vector<float> values;
        for (const auto& v : j.at("embedding").at("values")) {
            values.push_back(v.get<float>());
        }
        if (values.empty()) {
            return Result<std::vector<float>, Error>::Err(Error{
                "GeminiEmbeddingProvider::EmbedText", path, response->status,
                "Gemini API returned an empty embedding", std::nullopt, ErrorCategory::Internal});
        }
        return Result<std::vector<float>, Error>::Ok(std::move(values));
    } catch (const std::exception& ex) {
        return Result<std::vector<float>, Error>::Err(Error{
            "GeminiEmbeddingProvider::EmbedText", path, response->status,
            std::string("Failed to parse Gemini response: ") + ex.what(), std::nullopt,
            ErrorCategory::Internal});
    }
}

} // namespace erpl_adt
