#pragma once

#include <erpl_adt/core/embedding_provider.hpp>

#include <memory>

namespace erpl_adt {

// ---------------------------------------------------------------------------
// GeminiEmbeddingProvider — default IEmbeddingProvider, calls the Gemini
// embeddings API (text-embedding-004, 768-d) over HTTPS. Requires an API
// key (GEMINI_API_KEY env var, or passed explicitly to Create).
// ---------------------------------------------------------------------------
class GeminiEmbeddingProvider : public IEmbeddingProvider {
public:
    // Reads the API key from the GEMINI_API_KEY environment variable.
    // Returns an error if the variable is unset — fail fast at construction
    // rather than on the first embedding call.
    [[nodiscard]] static Result<std::unique_ptr<GeminiEmbeddingProvider>, std::string>
    CreateFromEnv();

    [[nodiscard]] static Result<std::unique_ptr<GeminiEmbeddingProvider>, std::string>
    Create(std::string api_key);

    [[nodiscard]] Result<std::vector<float>, Error> EmbedText(const std::string& text) override;
    [[nodiscard]] int Dimensions() const override { return 768; }
    [[nodiscard]] std::string ModelName() const override { return "text-embedding-004"; }

private:
    explicit GeminiEmbeddingProvider(std::string api_key);
    std::string api_key_;
};

} // namespace erpl_adt
