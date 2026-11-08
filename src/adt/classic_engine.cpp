#include <erpl_adt/adt/classic_engine.hpp>

#include <cmrc/cmrc.hpp>

#include <array>
#include <cctype>
#include <sstream>

CMRC_DECLARE(erpl_abap);

namespace erpl_adt {

namespace {

constexpr const char* kEngineResource = "zcl_erpl_classic_engine.abap";
constexpr const char* kTemplateClass = "zcl_erpl_classic_engine";
constexpr const char* kOpsMarker = "\"@@OPS@@";
constexpr size_t kChunk = 200;

const std::string& EngineTemplate() {
    static const std::string text = [] {
        auto fs = cmrc::erpl_abap::get_filesystem();
        auto file = fs.open(kEngineResource);
        return std::string(file.begin(), file.end());
    }();
    return text;
}

std::string ToLower(std::string s) {
    for (auto& c : s) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return s;
}

void ReplaceAll(std::string& text, const std::string& from, const std::string& to) {
    size_t pos = 0;
    while ((pos = text.find(from, pos)) != std::string::npos) {
        text.replace(pos, from.size(), to);
        pos += to.size();
    }
}

Error EngineError(const std::string& message, const std::string& output) {
    Error err;
    err.operation = "ClassicEngine";
    err.endpoint = "classrun";
    err.message = message;
    err.sap_error = output.size() > 4000 ? output.substr(0, 4000) : output;
    err.category = ErrorCategory::Internal;
    return err;
}

// Splits "a b c rest of text" into the first `n` words and the remainder.
std::vector<std::string> SplitWords(const std::string& line, size_t n) {
    std::vector<std::string> parts;
    size_t pos = 0;
    while (parts.size() < n && pos < line.size()) {
        auto space = line.find(' ', pos);
        if (space == std::string::npos) {
            parts.push_back(line.substr(pos));
            pos = line.size();
            break;
        }
        parts.push_back(line.substr(pos, space - pos));
        pos = space + 1;
    }
    parts.push_back(pos < line.size() ? line.substr(pos) : std::string());
    return parts;
}

}  // namespace

std::string AbapQuote(const std::string& value) {
    std::string out = "'";
    for (char c : value) {
        out += c;
        if (c == '\'') {
            out += '\'';
        }
    }
    out += "'";
    return out;
}

EngineOp DomaGetOp(const std::string& name, const std::string& language) {
    return {"doma_get( name = " + AbapQuote(name) + " language = " + AbapQuote(language) + " ).",
            ""};
}

EngineOp DomaPutOp(const std::string& name, const std::string& language,
                   const std::string& devclass, const std::string& trkorr,
                   const std::string& asx_xml) {
    return {"doma_put( name = " + AbapQuote(name) + " language = " + AbapQuote(language) +
                " devclass = " + AbapQuote(devclass) + " trkorr = " + AbapQuote(trkorr) + " ).",
            asx_xml};
}

EngineOp DomaDeleteOp(const std::string& name, const std::string& trkorr) {
    return {"doma_delete( name = " + AbapQuote(name) + " trkorr = " + AbapQuote(trkorr) + " ).",
            ""};
}

std::string BuildEngineSource(const std::string& class_name, const std::vector<EngineOp>& ops) {
    std::ostringstream body;
    for (size_t i = 0; i < ops.size(); ++i) {
        body << "op = '" << (i + 1) << "'.\n";
        if (!ops[i].payload.empty()) {
            body << "    CLEAR chunks.\n";
            const auto b64 = EncodeBase64(ops[i].payload);
            for (size_t off = 0; off < b64.size(); off += kChunk) {
                body << "    APPEND `" << b64.substr(off, kChunk) << "` TO chunks.\n";
            }
        }
        body << "    " << ops[i].call << "\n    ";
    }

    std::string source = EngineTemplate();
    ReplaceAll(source, kTemplateClass, ToLower(class_name));
    ReplaceAll(source, kOpsMarker, body.str());
    return source;
}

Result<std::vector<EngineOpResult>, Error> ParseEngineOutput(const std::string& output,
                                                             size_t op_count) {
    std::vector<EngineOpResult> results(op_count);
    bool finished = false;

    std::istringstream stream(output);
    std::string line;
    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line == "@END") {
            finished = true;
            continue;
        }
        if (line.size() < 2 || line[0] != '@') {
            continue;  // console noise
        }
        auto words = SplitWords(line.substr(1), 2);
        if (words.size() < 3) {
            continue;
        }
        const auto& kind = words[1];
        const auto& rest = words[2];
        if (kind == "EXC") {
            return Result<std::vector<EngineOpResult>, Error>::Err(EngineError(
                "The SAP helper class raised an exception: " + rest, output));
        }
        size_t index = 0;
        try {
            index = static_cast<size_t>(std::stoul(words[0]));
        } catch (...) {
            continue;
        }
        if (index == 0 || index > op_count) {
            continue;
        }
        auto& r = results[index - 1];
        if (kind == "RC") {
            try {
                r.rc = std::stoi(rest);
            } catch (...) {
                r.rc = 99;
            }
        } else if (kind == "EXISTS") {
            r.exists = rest == "1";
        } else if (kind == "META") {
            auto meta = SplitWords(rest, 2);
            r.changed_by = meta[0];
            r.changed_on = meta.size() > 1 ? meta[1] : "";
            r.changed_at = meta.size() > 2 ? meta[2] : "";
        } else if (kind == "DATA") {
            auto data = SplitWords(rest, 1);
            r.data[data[0]] += data.size() > 1 ? data[1] : "";
        } else if (kind == "MSG") {
            auto msg = SplitWords(rest, 3);
            EngineMessage m;
            m.type = msg[0];
            m.id = msg.size() > 1 ? msg[1] : "";
            m.number = msg.size() > 2 ? msg[2] : "";
            m.text = msg.size() > 3 ? msg[3] : "";
            r.messages.push_back(std::move(m));
        } else if (kind == "ERR") {
            r.errors.push_back(rest);
        }
    }

    if (!finished) {
        return Result<std::vector<EngineOpResult>, Error>::Err(EngineError(
            "The SAP helper class did not finish (no @END marker) — it was probably "
            "cancelled by a short dump or timeout. Check ST22 for a dump.",
            output));
    }
    for (size_t i = 0; i < op_count; ++i) {
        if (results[i].rc < 0) {
            return Result<std::vector<EngineOpResult>, Error>::Err(EngineError(
                "The SAP helper class reported no result for operation " +
                    std::to_string(i + 1),
                output));
        }
        for (auto& [tag, b64] : results[i].data) {
            b64 = DecodeBase64(b64);
        }
    }
    return Result<std::vector<EngineOpResult>, Error>::Ok(std::move(results));
}

std::string SummarizeFailure(const EngineOpResult& result) {
    std::string text;
    for (const auto& m : result.messages) {
        if (!text.empty()) {
            text += "; ";
        }
        text += m.text.empty() ? (m.id + "/" + m.number) : m.text;
    }
    for (const auto& e : result.errors) {
        if (!text.empty()) {
            text += "; ";
        }
        text += e;
    }
    if (text.empty()) {
        text = "SAP function module failed with return code " + std::to_string(result.rc);
    }
    return text;
}

std::string EncodeBase64(const std::string& bytes) {
    static constexpr char kAlphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve(((bytes.size() + 2) / 3) * 4);
    size_t i = 0;
    for (; i + 2 < bytes.size(); i += 3) {
        const auto b0 = static_cast<unsigned char>(bytes[i]);
        const auto b1 = static_cast<unsigned char>(bytes[i + 1]);
        const auto b2 = static_cast<unsigned char>(bytes[i + 2]);
        out += kAlphabet[b0 >> 2];
        out += kAlphabet[((b0 & 0x03) << 4) | (b1 >> 4)];
        out += kAlphabet[((b1 & 0x0F) << 2) | (b2 >> 6)];
        out += kAlphabet[b2 & 0x3F];
    }
    if (i < bytes.size()) {
        const auto b0 = static_cast<unsigned char>(bytes[i]);
        out += kAlphabet[b0 >> 2];
        if (i + 1 < bytes.size()) {
            const auto b1 = static_cast<unsigned char>(bytes[i + 1]);
            out += kAlphabet[((b0 & 0x03) << 4) | (b1 >> 4)];
            out += kAlphabet[(b1 & 0x0F) << 2];
        } else {
            out += kAlphabet[(b0 & 0x03) << 4];
            out += '=';
        }
        out += '=';
    }
    return out;
}

std::string DecodeBase64(const std::string& text) {
    std::array<int, 256> table{};
    table.fill(-1);
    const std::string alphabet =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    for (size_t i = 0; i < alphabet.size(); ++i) {
        table[static_cast<unsigned char>(alphabet[i])] = static_cast<int>(i);
    }
    std::string out;
    int value = 0;
    int bits = -8;
    for (char c : text) {
        const int d = table[static_cast<unsigned char>(c)];
        if (d < 0) {
            continue;  // padding, whitespace
        }
        value = (value << 6) | d;
        bits += 6;
        if (bits >= 0) {
            out += static_cast<char>((value >> bits) & 0xFF);
            bits -= 8;
        }
    }
    return out;
}

}  // namespace erpl_adt
