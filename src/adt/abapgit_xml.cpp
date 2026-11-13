#include <erpl_adt/adt/abapgit_xml.hpp>

#include <cstdint>
#include <optional>

// A small, strict XML reader/writer for the asXML subset abapGit files use.
//
// Hand-written rather than tinyxml2 because byte-exact output needs control
// over what tinyxml2 does not expose: tinyxml2 silently passes unknown
// entities through as text, and its printer has its own escaping and
// indentation rules. The reader below accepts elements, attributes, text,
// the five predefined entities plus numeric character references, CDATA,
// comments and processing instructions; anything else is an error.

namespace erpl_adt {
namespace {

constexpr std::string_view kBom = "\xEF\xBB\xBF";
constexpr std::string_view kDecl = "<?xml version=\"1.0\" encoding=\"utf-8\"?>";
constexpr std::string_view kAsxNamespace = "http://www.sap.com/abapxml";
constexpr std::string_view kAbapGitVersion = "v1.0.0";

struct Node {
    std::string name;
    std::vector<std::pair<std::string, std::string>> attributes;
    std::vector<Node> children;
    std::string text;  // only meaningful when children is empty

    [[nodiscard]] const Node* Child(std::string_view child_name) const {
        for (const auto& child : children) {
            if (child.name == child_name) {
                return &child;
            }
        }
        return nullptr;
    }

    [[nodiscard]] std::string Attribute(std::string_view attr_name) const {
        for (const auto& [key, value] : attributes) {
            if (key == attr_name) {
                return value;
            }
        }
        return {};
    }
};

Error MakeError(std::string operation, std::string message) {
    Error error;
    error.operation = std::move(operation);
    error.message = std::move(message);
    error.category = ErrorCategory::Internal;
    return error;
}

// ---------------------------------------------------------------------------
// Reader
// ---------------------------------------------------------------------------

bool IsXmlSpace(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

bool IsAllSpace(std::string_view text) {
    for (const char c : text) {
        if (!IsXmlSpace(c)) {
            return false;
        }
    }
    return true;
}

bool IsNameChar(char c) {
    return !IsXmlSpace(c) && c != '<' && c != '>' && c != '/' && c != '=' && c != '"' &&
           c != '\'' && c != '&';
}

void AppendUtf8(std::uint32_t cp, std::string& out) {
    if (cp < 0x80) {
        out.push_back(static_cast<char>(cp));
    } else if (cp < 0x800) {
        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
}

std::optional<std::uint32_t> ParseCharRef(std::string_view ref) {
    // ref is the part between "&#" and ";".
    int base = 10;
    if (!ref.empty() && (ref.front() == 'x' || ref.front() == 'X')) {
        base = 16;
        ref.remove_prefix(1);
    }
    if (ref.empty() || ref.size() > 8) {
        return std::nullopt;
    }
    std::uint32_t value = 0;
    for (const char c : ref) {
        std::uint32_t digit = 0;
        if (c >= '0' && c <= '9') {
            digit = static_cast<std::uint32_t>(c - '0');
        } else if (base == 16 && c >= 'a' && c <= 'f') {
            digit = static_cast<std::uint32_t>(c - 'a' + 10);
        } else if (base == 16 && c >= 'A' && c <= 'F') {
            digit = static_cast<std::uint32_t>(c - 'A' + 10);
        } else {
            return std::nullopt;
        }
        value = value * static_cast<std::uint32_t>(base) + digit;
    }
    if (value == 0 || value > 0x10FFFF || (value >= 0xD800 && value <= 0xDFFF)) {
        return std::nullopt;
    }
    return value;
}

class Reader {
public:
    explicit Reader(std::string_view input) : input_(input) {}

    // A complete document: optional BOM, prolog, exactly one root element.
    std::optional<Node> ParseDocument() {
        if (input_.substr(0, kBom.size()) == kBom) {
            pos_ = kBom.size();
        }
        if (!SkipMisc()) {
            return std::nullopt;
        }
        if (AtEnd() || Peek() != '<') {
            return Fail("expected root element");
        }
        auto root = ParseElement();
        if (!root || !SkipMisc()) {
            return std::nullopt;
        }
        if (!AtEnd()) {
            return Fail("unexpected content after root element");
        }
        return root;
    }

    // A sequence of sibling elements (values_xml), whitespace between them.
    std::optional<std::vector<Node>> ParseFragment() {
        Node holder;
        if (!ParseContent(holder, /*expect_close=*/false)) {
            return std::nullopt;
        }
        return std::move(holder.children);
    }

    [[nodiscard]] const std::string& ErrorMessage() const { return error_; }

private:
    [[nodiscard]] bool AtEnd() const { return pos_ >= input_.size(); }
    [[nodiscard]] char Peek() const { return input_[pos_]; }
    [[nodiscard]] bool StartsWith(std::string_view prefix) const {
        return input_.substr(pos_, prefix.size()) == prefix;
    }

    std::nullopt_t Fail(std::string message) {
        if (error_.empty()) {
            error_ = std::move(message) + " at offset " + std::to_string(pos_);
        }
        return std::nullopt;
    }

    void SkipSpace() {
        while (!AtEnd() && IsXmlSpace(Peek())) {
            ++pos_;
        }
    }

    bool SkipPast(std::string_view terminator, const char* what) {
        const auto end = input_.find(terminator, pos_);
        if (end == std::string_view::npos) {
            Fail(std::string("unterminated ") + what);
            return false;
        }
        pos_ = end + terminator.size();
        return true;
    }

    // Whitespace, comments and processing instructions (incl. the XML declaration).
    bool SkipMisc() {
        for (;;) {
            SkipSpace();
            if (StartsWith("<?")) {
                if (!SkipPast("?>", "processing instruction")) {
                    return false;
                }
            } else if (StartsWith("<!--")) {
                if (!SkipPast("-->", "comment")) {
                    return false;
                }
            } else if (StartsWith("<!")) {
                Fail("DOCTYPE is not supported");
                return false;
            } else {
                return true;
            }
        }
    }

    std::optional<std::string> ParseName() {
        const auto start = pos_;
        while (!AtEnd() && IsNameChar(Peek())) {
            ++pos_;
        }
        if (pos_ == start) {
            return Fail("expected a name");
        }
        return std::string(input_.substr(start, pos_ - start));
    }

    // Decode text up to (not including) any char in `stops`.
    bool DecodeUntil(std::string_view stops, std::string& out) {
        while (!AtEnd() && stops.find(Peek()) == std::string_view::npos) {
            const char c = Peek();
            if (c == '&') {
                const auto semi = input_.find(';', pos_);
                if (semi == std::string_view::npos || semi - pos_ > 12) {
                    Fail("unterminated entity reference");
                    return false;
                }
                const auto entity = input_.substr(pos_ + 1, semi - pos_ - 1);
                if (entity == "amp") {
                    out.push_back('&');
                } else if (entity == "lt") {
                    out.push_back('<');
                } else if (entity == "gt") {
                    out.push_back('>');
                } else if (entity == "quot") {
                    out.push_back('"');
                } else if (entity == "apos") {
                    out.push_back('\'');
                } else if (!entity.empty() && entity.front() == '#') {
                    const auto cp = ParseCharRef(entity.substr(1));
                    if (!cp) {
                        Fail("invalid character reference");
                        return false;
                    }
                    AppendUtf8(*cp, out);
                } else {
                    Fail("unknown entity &" + std::string(entity) + ";");
                    return false;
                }
                pos_ = semi + 1;
            } else if (c == '\r') {
                // XML end-of-line handling: CRLF and lone CR become LF.
                out.push_back('\n');
                ++pos_;
                if (!AtEnd() && Peek() == '\n') {
                    ++pos_;
                }
            } else {
                out.push_back(c);
                ++pos_;
            }
        }
        return true;
    }

    std::optional<Node> ParseElement() {
        ++pos_;  // '<'
        Node node;
        auto name = ParseName();
        if (!name) {
            return std::nullopt;
        }
        node.name = std::move(*name);
        for (;;) {
            SkipSpace();
            if (AtEnd()) {
                return Fail("unterminated start tag <" + node.name + ">");
            }
            if (StartsWith("/>")) {
                pos_ += 2;
                return node;
            }
            if (Peek() == '>') {
                ++pos_;
                break;
            }
            auto attr_name = ParseName();
            if (!attr_name) {
                return std::nullopt;
            }
            SkipSpace();
            if (AtEnd() || Peek() != '=') {
                return Fail("expected '=' after attribute " + *attr_name);
            }
            ++pos_;
            SkipSpace();
            if (AtEnd() || (Peek() != '"' && Peek() != '\'')) {
                return Fail("expected quoted attribute value");
            }
            const char quote = Peek();
            ++pos_;
            std::string value;
            const char stops[] = {quote, '<', '\0'};
            if (!DecodeUntil(std::string_view(stops, 2), value)) {
                return std::nullopt;
            }
            if (AtEnd() || Peek() != quote) {
                return Fail("unterminated attribute value");
            }
            ++pos_;
            node.attributes.emplace_back(std::move(*attr_name), std::move(value));
        }
        if (!ParseContent(node, /*expect_close=*/true)) {
            return std::nullopt;
        }
        return node;
    }

    // Children and text of `node` up to its end tag (or end of input for a fragment).
    bool ParseContent(Node& node, bool expect_close) {
        std::string text;
        for (;;) {
            if (AtEnd()) {
                if (expect_close) {
                    Fail("missing end tag </" + node.name + ">");
                    return false;
                }
                break;
            }
            if (StartsWith("</")) {
                if (!expect_close) {
                    Fail("unexpected end tag");
                    return false;
                }
                pos_ += 2;
                auto name = ParseName();
                if (!name) {
                    return false;
                }
                if (*name != node.name) {
                    Fail("end tag </" + *name + "> does not match <" + node.name + ">");
                    return false;
                }
                SkipSpace();
                if (AtEnd() || Peek() != '>') {
                    Fail("unterminated end tag");
                    return false;
                }
                ++pos_;
                break;
            }
            if (StartsWith("<!--")) {
                if (!SkipPast("-->", "comment")) {
                    return false;
                }
            } else if (StartsWith("<![CDATA[")) {
                pos_ += 9;
                const auto end = input_.find("]]>", pos_);
                if (end == std::string_view::npos) {
                    Fail("unterminated CDATA section");
                    return false;
                }
                text.append(input_.substr(pos_, end - pos_));
                pos_ = end + 3;
            } else if (StartsWith("<?")) {
                if (!SkipPast("?>", "processing instruction")) {
                    return false;
                }
            } else if (StartsWith("<!")) {
                Fail("unexpected markup declaration");
                return false;
            } else if (Peek() == '<') {
                auto child = ParseElement();
                if (!child) {
                    return false;
                }
                node.children.push_back(std::move(*child));
            } else {
                if (!DecodeUntil("<", text)) {
                    return false;
                }
            }
        }
        if (!node.children.empty() || !expect_close) {
            // asXML has no mixed content: whitespace between elements is
            // layout, anything else is an error rather than silently dropped.
            if (!IsAllSpace(text)) {
                Fail("text mixed with element content");
                return false;
            }
            return true;
        }
        node.text = std::move(text);
        return true;
    }

    std::string_view input_;
    std::size_t pos_ = 0;
    std::string error_;
};

// ---------------------------------------------------------------------------
// Writer
// ---------------------------------------------------------------------------

// abapGit's iXML renderer escapes the five predefined entities in text and
// attribute values alike. A carriage return is written as a character
// reference because a raw CR would be folded into LF by any XML reader.
void AppendEscaped(std::string_view text, std::string& out) {
    for (const char c : text) {
        switch (c) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '"': out += "&quot;"; break;
            case '\'': out += "&apos;"; break;
            case '\r': out += "&#13;"; break;
            default: out.push_back(c); break;
        }
    }
}

void AppendStartTag(const Node& node, std::string& out) {
    out.push_back('<');
    out += node.name;
    for (const auto& [key, value] : node.attributes) {
        out.push_back(' ');
        out += key;
        out += "=\"";
        AppendEscaped(value, out);
        out.push_back('"');
    }
}

void AppendLeafRest(const Node& node, std::string& out) {
    if (node.text.empty()) {
        out += "/>";
        return;
    }
    out.push_back('>');
    AppendEscaped(node.text, out);
    out += "</";
    out += node.name;
    out.push_back('>');
}

void RenderCompact(const Node& node, std::string& out) {
    AppendStartTag(node, out);
    if (node.children.empty()) {
        AppendLeafRest(node, out);
        return;
    }
    out.push_back('>');
    for (const auto& child : node.children) {
        RenderCompact(child, out);
    }
    out += "</";
    out += node.name;
    out.push_back('>');
}

std::string RenderCompact(const std::vector<Node>& nodes) {
    std::string out;
    for (const auto& node : nodes) {
        RenderCompact(node, out);
    }
    return out;
}

// One space per level, one element per line, LF line endings.
void RenderPretty(const Node& node, std::size_t depth, std::string& out) {
    out.append(depth, ' ');
    AppendStartTag(node, out);
    if (node.children.empty()) {
        AppendLeafRest(node, out);
        out.push_back('\n');
        return;
    }
    out += ">\n";
    for (const auto& child : node.children) {
        RenderPretty(child, depth + 1, out);
    }
    out.append(depth, ' ');
    out += "</";
    out += node.name;
    out += ">\n";
}

std::string RenderFile(const Node& root) {
    std::string out;
    out += kBom;
    out += kDecl;
    out.push_back('\n');
    RenderPretty(root, 0, out);
    return out;
}

Node AsxAbapNode(std::vector<Node> values) {
    Node values_node;
    values_node.name = "asx:values";
    values_node.children = std::move(values);

    Node abap;
    abap.name = "asx:abap";
    abap.attributes.emplace_back("xmlns:asx", std::string(kAsxNamespace));
    abap.attributes.emplace_back("version", "1.0");
    abap.children.push_back(std::move(values_node));
    return abap;
}

Node LeafNode(std::string name, std::string text) {
    Node node;
    node.name = std::move(name);
    node.text = std::move(text);
    return node;
}

std::optional<std::vector<Node>> ParseValues(std::string_view values_xml, std::string* error) {
    Reader reader(values_xml);
    auto nodes = reader.ParseFragment();
    if (!nodes && error != nullptr) {
        *error = reader.ErrorMessage();
    }
    return nodes;
}

// Locate asx:abap/asx:values under `abap`, or explain why not.
Result<const Node*, Error> FindValues(const Node& abap, const std::string& operation) {
    if (abap.name != "asx:abap") {
        return Result<const Node*, Error>::Err(
            MakeError(operation, "expected <asx:abap>, found <" + abap.name + ">"));
    }
    const Node* values = abap.Child("asx:values");
    if (values == nullptr) {
        return Result<const Node*, Error>::Err(MakeError(operation, "missing <asx:values>"));
    }
    return Result<const Node*, Error>::Ok(values);
}

Result<Node, Error> ParseDocumentOrErr(std::string_view content, const std::string& operation) {
    Reader reader(content);
    auto root = reader.ParseDocument();
    if (!root) {
        return Result<Node, Error>::Err(
            MakeError(operation, "malformed XML: " + reader.ErrorMessage()));
    }
    return Result<Node, Error>::Ok(std::move(*root));
}

}  // namespace

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

Result<AbapGitXml, Error> ParseAbapGitXml(std::string_view file_content) {
    const std::string operation = "ParseAbapGitXml";
    auto root = ParseDocumentOrErr(file_content, operation);
    if (root.IsErr()) {
        return Result<AbapGitXml, Error>::Err(root.Error());
    }
    const Node& abapgit = root.Value();
    if (abapgit.name != "abapGit") {
        return Result<AbapGitXml, Error>::Err(
            MakeError(operation, "missing <abapGit> envelope, found <" + abapgit.name + ">"));
    }
    const Node* abap = abapgit.Child("asx:abap");
    if (abap == nullptr) {
        return Result<AbapGitXml, Error>::Err(MakeError(operation, "missing <asx:abap>"));
    }
    auto values = FindValues(*abap, operation);
    if (values.IsErr()) {
        return Result<AbapGitXml, Error>::Err(values.Error());
    }
    AbapGitXml xml;
    xml.serializer = abapgit.Attribute("serializer");
    xml.serializer_version = abapgit.Attribute("serializer_version");
    xml.values_xml = RenderCompact(values.Value()->children);
    return Result<AbapGitXml, Error>::Ok(std::move(xml));
}

std::string RenderAbapGitXml(const AbapGitXml& xml) {
    auto values = ParseValues(xml.values_xml, nullptr);
    Node abapgit;
    abapgit.name = "abapGit";
    abapgit.attributes.emplace_back("version", std::string(kAbapGitVersion));
    if (!xml.serializer.empty()) {
        abapgit.attributes.emplace_back("serializer", xml.serializer);
        abapgit.attributes.emplace_back("serializer_version", xml.serializer_version);
    }
    abapgit.children.push_back(AsxAbapNode(values ? std::move(*values) : std::vector<Node>{}));
    std::string out = RenderFile(abapgit);
    if (!values) {
        // Unparseable payload: keep it verbatim rather than drop it silently.
        const std::string marker = "  <asx:values/>\n";
        const auto at = out.find(marker);
        if (at != std::string::npos) {
            out.replace(at, marker.size(),
                        "  <asx:values>\n   " + xml.values_xml + "\n  </asx:values>\n");
        }
    }
    return out;
}

Result<std::string, Error> ExtractAsxValues(std::string_view asxml) {
    const std::string operation = "ExtractAsxValues";
    auto root = ParseDocumentOrErr(asxml, operation);
    if (root.IsErr()) {
        return Result<std::string, Error>::Err(root.Error());
    }
    auto values = FindValues(root.Value(), operation);
    if (values.IsErr()) {
        return Result<std::string, Error>::Err(values.Error());
    }
    return Result<std::string, Error>::Ok(RenderCompact(values.Value()->children));
}

std::string WrapAsxValues(std::string_view values_xml) {
    std::string out;
    out += kDecl;
    out += "<asx:abap xmlns:asx=\"";
    out += kAsxNamespace;
    out += "\" version=\"1.0\"><asx:values>";
    out += values_xml;
    out += "</asx:values></asx:abap>";
    return out;
}

std::vector<std::pair<std::string, std::string>> SplitAsxValues(std::string_view values_xml) {
    std::vector<std::pair<std::string, std::string>> parts;
    auto nodes = ParseValues(values_xml, nullptr);
    if (!nodes) {
        return parts;
    }
    parts.reserve(nodes->size());
    for (const auto& node : *nodes) {
        std::string xml;
        RenderCompact(node, xml);
        parts.emplace_back(node.name, std::move(xml));
    }
    return parts;
}

std::string JoinAsxValues(const std::vector<std::pair<std::string, std::string>>& parts) {
    std::string out;
    for (const auto& part : parts) {
        out += part.second;
    }
    return out;
}

Result<std::string, Error> CanonicalAsx(std::string_view values_xml) {
    std::string error;
    auto nodes = ParseValues(values_xml, &error);
    if (!nodes) {
        return Result<std::string, Error>::Err(
            MakeError("CanonicalAsx", "malformed XML: " + error));
    }
    return Result<std::string, Error>::Ok(RenderCompact(*nodes));
}

std::string EscapeXmlText(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    AppendEscaped(text, out);
    return out;
}

Result<AbapGitRepoConfig, Error> ParseDotAbapGit(std::string_view content) {
    const std::string operation = "ParseDotAbapGit";
    auto root = ParseDocumentOrErr(content, operation);
    if (root.IsErr()) {
        return Result<AbapGitRepoConfig, Error>::Err(root.Error());
    }
    auto values = FindValues(root.Value(), operation);
    if (values.IsErr()) {
        return Result<AbapGitRepoConfig, Error>::Err(values.Error());
    }
    const Node* data = values.Value()->Child("DATA");
    if (data == nullptr) {
        return Result<AbapGitRepoConfig, Error>::Err(MakeError(operation, "missing <DATA>"));
    }

    AbapGitRepoConfig config;
    if (const Node* lang = data->Child("MASTER_LANGUAGE")) {
        config.master_language = lang->text;
    }
    if (const Node* folder = data->Child("STARTING_FOLDER")) {
        config.starting_folder = folder->text;
    }
    if (const Node* logic = data->Child("FOLDER_LOGIC")) {
        if (logic->text == "PREFIX") {
            config.folder_logic = AbapGitRepoConfig::FolderLogic::Prefix;
        } else if (logic->text == "FULL") {
            config.folder_logic = AbapGitRepoConfig::FolderLogic::Full;
        } else {
            return Result<AbapGitRepoConfig, Error>::Err(
                MakeError(operation, "unknown FOLDER_LOGIC '" + logic->text + "'"));
        }
    }
    if (const Node* ignore = data->Child("IGNORE")) {
        for (const auto& item : ignore->children) {
            config.ignore.push_back(item.text);
        }
    }
    std::string raw;
    RenderCompact(*data, raw);
    config.raw_values_xml = std::move(raw);
    return Result<AbapGitRepoConfig, Error>::Ok(std::move(config));
}

std::string RenderDotAbapGit(const AbapGitRepoConfig& config) {
    if (!config.raw_values_xml.empty()) {
        if (auto nodes = ParseValues(config.raw_values_xml, nullptr)) {
            return RenderFile(AsxAbapNode(std::move(*nodes)));
        }
    }
    // abapGit serializes with initial components suppressed: empty fields and
    // an empty ignore table are omitted.
    Node data;
    data.name = "DATA";
    if (!config.master_language.empty()) {
        data.children.push_back(LeafNode("MASTER_LANGUAGE", config.master_language));
    }
    if (!config.starting_folder.empty()) {
        data.children.push_back(LeafNode("STARTING_FOLDER", config.starting_folder));
    }
    data.children.push_back(LeafNode(
        "FOLDER_LOGIC",
        config.folder_logic == AbapGitRepoConfig::FolderLogic::Full ? "FULL" : "PREFIX"));
    if (!config.ignore.empty()) {
        Node ignore;
        ignore.name = "IGNORE";
        for (const auto& entry : config.ignore) {
            ignore.children.push_back(LeafNode("item", entry));
        }
        data.children.push_back(std::move(ignore));
    }
    std::vector<Node> values;
    values.push_back(std::move(data));
    return RenderFile(AsxAbapNode(std::move(values)));
}

}  // namespace erpl_adt
