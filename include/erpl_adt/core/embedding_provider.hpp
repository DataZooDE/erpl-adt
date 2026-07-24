#pragma once

#include <erpl_adt/core/result.hpp>

#include <string>
#include <vector>

namespace erpl_adt {

// ---------------------------------------------------------------------------
// IEmbeddingProvider — pluggable text-embedding backend for VSS semantic
// search (catalog_search --mode vss|hybrid). Abstracted so the catalog
// storage layer never depends on a specific embedding API; GeminiEmbeddingProvider
// is the default implementation, but a local/offline provider can be swapped
// in later without touching the store.
// ---------------------------------------------------------------------------
class IEmbeddingProvider {
public:
    virtual ~IEmbeddingProvider() = default;

    [[nodiscard]] virtual Result<std::vector<float>, Error> EmbedText(
        const std::string& text) = 0;

    // Vector width this provider produces — the entity_embeddings table's
    // HNSW index is fixed-width per model, so callers need this up front.
    [[nodiscard]] virtual int Dimensions() const = 0;

    // Identifies the model/provider, stored alongside each embedding
    // (entity_embeddings.model) so a future re-embed with a different model
    // is detectable rather than silently mixing incompatible vectors.
    [[nodiscard]] virtual std::string ModelName() const = 0;
};

} // namespace erpl_adt
