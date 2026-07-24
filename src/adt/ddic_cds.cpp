#include <erpl_adt/adt/ddic_cds.hpp>

#include <erpl_adt/core/url.hpp>

#include <algorithm>
#include <cctype>
#include <regex>
#include <sstream>

namespace erpl_adt {

namespace {

std::string Trim(const std::string& s) {
    auto begin = s.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos) return "";
    auto end = s.find_last_not_of(" \t\r\n");
    return s.substr(begin, end - begin + 1);
}

std::string ToLowerCopy(const std::string& s) {
    std::string out = s;
    std::transform(out.begin(), out.end(), out.begin(),
                    [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

// Strips "//" line comments. A line whose trimmed content starts with "//"
// is dropped entirely; a line with trailing "// ..." after code is truncated
// at the "//". Best-effort — does not understand string literals.
std::string StripLineComments(const std::string& source) {
    std::ostringstream out;
    std::istringstream in(source);
    std::string line;
    while (std::getline(in, line)) {
        auto pos = line.find("//");
        if (pos != std::string::npos) {
            auto before = Trim(line.substr(0, pos));
            if (before.empty()) continue;  // comment-only line
            line = line.substr(0, pos);
        }
        out << line << "\n";
    }
    return out.str();
}

// Collapses all whitespace runs (including newlines) into single spaces.
std::string CollapseWhitespace(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    bool in_space = false;
    for (char c : s) {
        if (std::isspace(static_cast<unsigned char>(c))) {
            if (!in_space && !out.empty()) out += ' ';
            in_space = true;
        } else {
            out += c;
            in_space = false;
        }
    }
    while (!out.empty() && out.back() == ' ') out.pop_back();
    return out;
}

void ParseHeader(const std::string& clean_source, CdsViewInfo& info, size_t& brace_pos) {
    static const std::regex kDefineRe(
        R"(define\s+(?:root\s+|transient\s+|abstract\s+|projection\s+)?)"
        R"((?:view\s+entity|view|entity)\s+([A-Za-z_/][\w/]*))",
        std::regex::icase);
    static const std::regex kFromRe(
        R"(as\s+(?:select\s+from|projection\s+on)\s+([A-Za-z_/][\w/]*))", std::regex::icase);

    std::smatch m;
    if (std::regex_search(clean_source, m, kDefineRe)) {
        info.entity_name = m[1].str();
    }
    // A header-level annotation (e.g. `@AbapCatalog.extensibility: { ... }`)
    // can appear before `define view entity` with its own object-literal
    // value — searching for '{' from the very start of the source would
    // lock onto that annotation's brace instead of the real field-list
    // brace. The "as select from"/"as projection on" clause always precedes
    // the field list, so start the search for the field-list brace from
    // there instead of from position 0.
    size_t from_clause_end = 0;
    if (std::regex_search(clean_source, m, kFromRe)) {
        info.source_table = m[1].str();
        from_clause_end = static_cast<size_t>(m.position(0) + m.length(0));
    }

    brace_pos = clean_source.find('{', from_clause_end);

    // Association/composition header region: from after "define ..." up to
    // the first top-level '{'.
    if (brace_pos == std::string::npos) return;
    std::string header = clean_source.substr(0, brace_pos);
    std::string collapsed = CollapseWhitespace(header);

    // Find start positions of each "association"/"composition" clause.
    static const std::regex kKeywordRe(R"(\b(association|composition)\b)", std::regex::icase);
    std::vector<std::pair<size_t, bool>> clause_starts;  // (offset, is_composition)
    for (auto it = std::sregex_iterator(collapsed.begin(), collapsed.end(), kKeywordRe);
         it != std::sregex_iterator(); ++it) {
        clause_starts.emplace_back(static_cast<size_t>(it->position()),
                                    ToLowerCopy(it->str()) == "composition");
    }

    static const std::regex kClauseRe(
        R"((?:to\s+parent|to|of)\s+([A-Za-z_][\w]*)\s+as\s+(_?[A-Za-z_]\w*)\s*(on\s+(.*))?)",
        std::regex::icase);
    static const std::regex kCardRe(R"(\[[^\]]*\])");

    for (size_t i = 0; i < clause_starts.size(); ++i) {
        auto start = clause_starts[i].first;
        auto end = (i + 1 < clause_starts.size()) ? clause_starts[i + 1].first : collapsed.size();
        std::string clause = collapsed.substr(start, end - start);

        CdsAssociation assoc;
        assoc.is_composition = clause_starts[i].second;

        std::smatch cm;
        if (std::regex_search(clause, cm, kCardRe)) {
            assoc.cardinality = cm[0].str();
        }
        assoc.to_parent = std::regex_search(clause, std::regex(R"(\bto\s+parent\b)", std::regex::icase));

        std::smatch cl;
        if (std::regex_search(clause, cl, kClauseRe)) {
            assoc.target = cl[1].str();
            assoc.alias = cl[2].str();
            if (cl[4].matched) assoc.on_condition = Trim(cl[4].str());
        }

        if (!assoc.alias.empty()) {
            info.associations.push_back(std::move(assoc));
        }
    }
}

// Depth-tracks all three bracket kinds — not just parens. Annotation values
// can be array-of-object literals (e.g.
// `@Consumption.filter.hierarchyBinding: [ { type: #ELEMENT, value: '...',
// variableSequence: 1 }, { type: #CONSTANT, ... } ]`, real DDL from
// SABAPDEMOS's DEMO_CDS_ANNOTATION_ARRAY) — the commas inside that `[...]`
// are not field separators. Missing `{`/`[` tracking here previously split
// one such annotation into several garbage pseudo-fields (e.g. a field
// literally named "value: '...'", duplicated once per array element,
// producing a duplicate-field-id write failure).
std::vector<std::string> SplitTopLevel(const std::string& s, char sep) {
    std::vector<std::string> parts;
    int depth = 0;
    std::string current;
    for (char c : s) {
        if (c == '(' || c == '{' || c == '[') ++depth;
        if (c == ')' || c == '}' || c == ']') --depth;
        if (c == sep && depth <= 0) {
            parts.push_back(current);
            current.clear();
            continue;
        }
        current += c;
    }
    if (!Trim(current).empty()) parts.push_back(current);
    return parts;
}

// Finds the last " as " that sits at paren-depth 0 — the field's own alias
// separator, as opposed to an inner " as " inside a cast(...) expression
// (e.g. `cast( matnr as abap.char( 10 ) ) as MatnrChar` must split at the
// outer " as ", not the one inside cast(...)).
size_t FindTopLevelLastAs(const std::string& lower) {
    int depth = 0;
    size_t last = std::string::npos;
    for (size_t i = 0; i < lower.size(); ++i) {
        char c = lower[i];
        if (c == '(') { ++depth; continue; }
        if (c == ')') { if (depth > 0) --depth; continue; }
        if (depth == 0 && i + 4 <= lower.size() && lower.compare(i, 4, " as ") == 0) {
            last = i;
        }
    }
    return last;
}

CdsField ParseFieldSegment(const std::string& segment) {
    CdsField field;

    // Character-level scan rather than line-by-line: an annotation's value
    // can be a multi-line array-of-objects literal (e.g.
    // `@Consumption.filter.hierarchyBinding: [ { type: #ELEMENT, value:
    // '...', variableSequence: 1 }, {...} ]`, real DDL from SABAPDEMOS's
    // DEMO_CDS_ANNOTATION_ARRAY) whose closing bracket can be followed by
    // real field code on that SAME physical line — a per-line classifier
    // can't represent "annotation for the first half of this line, code
    // for the second half." Each `@...` clause is consumed until its own
    // opened brackets rebalance to 0, stopping exactly at the closing
    // bracket regardless of how many lines that spans; everything else is
    // field code.
    std::string expr;
    const size_t n = segment.size();
    size_t i = 0;
    while (i < n) {
        if (std::isspace(static_cast<unsigned char>(segment[i]))) {
            ++i;
            continue;
        }
        if (segment[i] == '@') {
            const size_t start = i;
            int depth = 0;
            bool closed_once = false;
            while (i < n) {
                const char c = segment[i];
                if (c == '(' || c == '{' || c == '[') {
                    ++depth;
                } else if (c == ')' || c == '}' || c == ']') {
                    --depth;
                    closed_once = true;
                } else if (c == '\n' && depth <= 0 && closed_once) {
                    break;  // single-line annotation, brackets (if any) already balanced
                } else if (c == '\n' && depth <= 0 && !closed_once) {
                    // No brackets opened at all yet — annotation ends at
                    // end of its own line (the common single-line case,
                    // e.g. `@EndUserText.label: 'X'`), UNLESS the line
                    // ends with nothing but a bare `:` (a key with no
                    // inline value — the array-annotation's own first
                    // line), in which case the value starts on the next
                    // line and this annotation is not yet complete.
                    auto so_far = Trim(segment.substr(start, i - start));
                    if (!so_far.empty() && so_far.back() == ':') {
                        // fall through — keep consuming into next line
                    } else {
                        break;
                    }
                }
                ++i;
                if (depth <= 0 && closed_once) {
                    break;  // consumed exactly through the closing bracket
                }
            }
            field.annotations.push_back(CollapseWhitespace(segment.substr(start, i - start)));
        } else {
            const size_t start = i;
            while (i < n && segment[i] != '@') ++i;
            expr += segment.substr(start, i - start);
            expr += ' ';
        }
    }
    expr = Trim(CollapseWhitespace(expr));

    // "key " prefix (word boundary).
    static const std::regex kKeyRe(R"(^key\s+)", std::regex::icase);
    if (std::regex_search(expr, kKeyRe)) {
        field.is_key = true;
        expr = Trim(std::regex_replace(expr, kKeyRe, ""));
    }

    // Find the last top-level " as " (case-insensitive) to split alias.
    std::string lower = ToLowerCopy(expr);
    auto pos = FindTopLevelLastAs(lower);
    if (pos != std::string::npos) {
        field.source_expression = Trim(expr.substr(0, pos));
        field.name = Trim(expr.substr(pos + 4));
    } else if (!expr.empty() && expr[0] == '_') {
        field.is_association = true;
        field.name = expr;
    } else {
        field.name = expr;
        field.source_expression = expr;
    }

    // Human-readable label, when the DDL carries one — the only per-field
    // metadata resolvable from text alone.
    static const std::regex kLabelRe(R"(@EndUserText\.label\s*:\s*'([^']*)')", std::regex::icase);
    for (const auto& annotation : field.annotations) {
        std::smatch m;
        if (std::regex_search(annotation, m, kLabelRe)) {
            field.description = m[1].str();
            break;
        }
    }

    return field;
}

} // anonymous namespace

CdsViewInfo ParseCdsSource(const std::string& ddl_source) {
    CdsViewInfo info;
    auto clean = StripLineComments(ddl_source);

    size_t brace_pos = std::string::npos;
    ParseHeader(clean, info, brace_pos);
    if (brace_pos == std::string::npos) return info;

    // Find the matching closing brace by depth counting.
    int depth = 0;
    size_t close_pos = std::string::npos;
    for (size_t i = brace_pos; i < clean.size(); ++i) {
        if (clean[i] == '{') ++depth;
        else if (clean[i] == '}') {
            --depth;
            if (depth == 0) { close_pos = i; break; }
        }
    }
    if (close_pos == std::string::npos) return info;

    std::string field_block = clean.substr(brace_pos + 1, close_pos - brace_pos - 1);
    for (const auto& segment : SplitTopLevel(field_block, ',')) {
        if (Trim(segment).empty()) continue;
        info.fields.push_back(ParseFieldSegment(segment));
    }

    return info;
}

Result<CdsViewInfo, Error> GetCdsStructure(IAdtSession& session, const std::string& cds_name) {
    auto url = "/sap/bc/adt/ddic/ddl/sources/" + UrlEncode(cds_name) + "/source/main";

    HttpHeaders headers;
    headers["Accept"] = "text/plain";

    auto response = session.Get(url, headers);
    if (response.IsErr()) {
        return Result<CdsViewInfo, Error>::Err(std::move(response).Error());
    }

    const auto& http = response.Value();
    if (http.status_code == 404) {
        return Result<CdsViewInfo, Error>::Err(Error{
            "GetCdsStructure", cds_name, 404,
            "CDS view not found", std::nullopt,
            ErrorCategory::NotFound});
    }
    if (http.status_code != 200) {
        return Result<CdsViewInfo, Error>::Err(
            Error::FromHttpStatus("GetCdsStructure", cds_name, http.status_code, http.body));
    }

    return Result<CdsViewInfo, Error>::Ok(ParseCdsSource(http.body));
}

} // namespace erpl_adt
