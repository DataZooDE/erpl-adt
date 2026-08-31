#include <erpl_adt/core/gemini_embedding_provider.hpp>

#include <httplib.h>
#include <nlohmann/json.hpp>

#include <cmath>
#include <cstdlib>

namespace erpl_adt {

namespace {
constexpr const char* kHost = "generativelanguage.googleapis.com";

// text-embedding-004 was retired: the endpoint answers HTTP 404, which is why
// `catalog search --mode vss` and `catalog build --embed` stopped working.
// gemini-embedding-001 replaces it and returns 3072 dimensions by default,
// so we ask for 768 — the width the catalog schema stores (FLOAT[768]) and the
// width every existing catalog was built at.
constexpr const char* kDefaultModel = "gemini-embedding-001";

// Truncated gemini-embedding-001 vectors are not unit length, unlike the ones
// text-embedding-004 returned. Normalising keeps cosine similarity meaningful
// and keeps new vectors comparable with those already stored.
void NormalizeInPlace(std::vector<float>& values) {
    double sum_of_squares = 0.0;
    for (const float v : values) {
        sum_of_squares += static_cast<double>(v) * static_cast<double>(v);
    }
    const double length = std::sqrt(sum_of_squares);
    if (length <= 0.0) {
        return;
    }
    for (float& v : values) {
        v = static_cast<float>(static_cast<double>(v) / length);
    }
}
}

// Overridable so the next retirement does not need a new release.
std::string GeminiEmbeddingProvider::ModelName() const {
    const char* override_name = std::getenv("ERPL_ADT_GEMINI_EMBED_MODEL");
    if (override_name != nullptr && *override_name != '\0') {
        return override_name;
    }
    return kDefaultModel;
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

    const auto model = ModelName();

    nlohmann::json body;
    body["model"] = "models/" + model;
    body["content"]["parts"] = nlohmann::json::array({{{"text", text}}});
    body["outputDimensionality"] = Dimensions();

    auto path = "/v1beta/models/" + model + ":embedContent?key=" + api_key_;
    auto response = client.Post(path, body.dump(), "application/json");

    if (!response) {
        return Result<std::vector<float>, Error>::Err(Error{
            "GeminiEmbeddingProvider::EmbedText", path, std::nullopt,
            "No response from Gemini embeddings API (network error)", std::nullopt,
            ErrorCategory::Connection});
    }
    if (response->status != 200) {
        auto error = Error::FromHttpStatus(
            "GeminiEmbeddingProvider::EmbedText",
            "/v1beta/models/" + model + ":embedContent", response->status,
            response->body);
        if (response->status == 404) {
            // This is what a retired model looks like, and it is the failure
            // that took vss/hybrid search out.
            error.message = "Gemini model '" + model + "' is not available";
            error.hint = "Set ERPL_ADT_GEMINI_EMBED_MODEL to a model this key "
                         "can use; the API lists them at /v1beta/models.";
        }
        return Result<std::vector<float>, Error>::Err(std::move(error));
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
        NormalizeInPlace(values);
        return Result<std::vector<float>, Error>::Ok(std::move(values));
    } catch (const std::exception& ex) {
        return Result<std::vector<float>, Error>::Err(Error{
            "GeminiEmbeddingProvider::EmbedText", path, response->status,
            std::string("Failed to parse Gemini response: ") + ex.what(), std::nullopt,
            ErrorCategory::Internal});
    }
}

} // namespace erpl_adt
