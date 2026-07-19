#include <erpl_adt/adt/catalog_ids.hpp>

#include <openssl/evp.h>

#include <array>
#include <string>

namespace erpl_adt {

namespace {

std::string Sha256Hex(std::string_view data) {
    std::array<unsigned char, EVP_MAX_MD_SIZE> digest{};
    unsigned int digest_len = 0;

    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    EVP_DigestInit_ex(ctx, EVP_sha256(), nullptr);
    EVP_DigestUpdate(ctx, data.data(), data.size());
    EVP_DigestFinal_ex(ctx, digest.data(), &digest_len);
    EVP_MD_CTX_free(ctx);

    static constexpr char kHex[] = "0123456789abcdef";
    std::string hex;
    hex.reserve(digest_len * 2);
    for (unsigned int i = 0; i < digest_len; ++i) {
        hex += kHex[(digest[i] >> 4) & 0x0f];
        hex += kHex[digest[i] & 0x0f];
    }
    return hex;
}

// Escapes '\' and '|' so that concatenating escaped fields with a literal
// '|' separator is unambiguous — without this, ("TABL|DT", "MARA") and
// ("TABL", "DT|MARA") would hash to identical bytes.
void AppendEscaped(std::string& out, std::string_view field) {
    for (char c : field) {
        if (c == '\\' || c == '|') out += '\\';
        out += c;
    }
}

} // anonymous namespace

EntityId DeriveEntityId(std::string_view system_sid,
                         CatalogDomain domain,
                         std::string_view object_type,
                         std::string_view technical_name) {
    std::string input;
    input.reserve(system_sid.size() + object_type.size() + technical_name.size() + 16);
    AppendEscaped(input, system_sid);
    input += '|';
    AppendEscaped(input, ToString(domain));
    input += '|';
    AppendEscaped(input, object_type);
    input += '|';
    AppendEscaped(input, technical_name);

    return EntityId::Create(Sha256Hex(input)).Value();
}

} // namespace erpl_adt
