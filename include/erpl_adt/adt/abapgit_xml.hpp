#pragma once

#include <erpl_adt/core/result.hpp>

#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace erpl_adt {

// ---------------------------------------------------------------------------
// abapGit's XML file format, read and written byte-exactly.
//
// abapGit writes object metadata (*.prog.xml, *.tran.xml, ...) through SAP
// iXML with its normalizing (pretty-printing) renderer: UTF-8 BOM, an
// `encoding="utf-8"` declaration, LF line endings, one space of indentation
// per level, leaf elements on one line, `<X/>` for empty elements, and the
// five predefined entities (&amp; &lt; &gt; &quot; &apos;) in text. Rendering
// with exactly those rules makes `export -> deploy -> export` byte-identical
// and keeps files interchangeable with abapGit itself.
//
// The payload travels as `values_xml`: the compact children of
// <asx:values> (no whitespace between tags), which is the form SAP's
// CALL TRANSFORMATION id reads and writes.
// ---------------------------------------------------------------------------

struct AbapGitXml {
    std::string serializer;          // e.g. "LCL_OBJECT_PROG"; empty = no serializer attributes
    std::string serializer_version;  // e.g. "v1.0.0"
    std::string values_xml;          // compact children of <asx:values>
};

// Parse an abapGit object file (*.prog.xml, *.tran.xml, ...).
[[nodiscard]] Result<AbapGitXml, Error> ParseAbapGitXml(std::string_view file_content);

// Render file bytes exactly as abapGit writes them.
[[nodiscard]] std::string RenderAbapGitXml(const AbapGitXml& xml);

// Extract compact values_xml from a SAP asXML document (BOM/declaration
// optional; a utf-16 declaration is tolerated since the bytes are UTF-8).
[[nodiscard]] Result<std::string, Error> ExtractAsxValues(std::string_view asxml);

// Wrap compact values_xml into a compact asXML document that SAP's
// CALL TRANSFORMATION id can read.
[[nodiscard]] std::string WrapAsxValues(std::string_view values_xml);

// Top-level children of values_xml as (element name, compact element xml incl.
// its own tags), in document order. Malformed input yields an empty vector.
[[nodiscard]] std::vector<std::pair<std::string, std::string>> SplitAsxValues(
    std::string_view values_xml);
[[nodiscard]] std::string JoinAsxValues(
    const std::vector<std::pair<std::string, std::string>>& parts);

// Canonical compact form for equality checks: parse + re-serialize compactly,
// dropping inter-element whitespace.
[[nodiscard]] Result<std::string, Error> CanonicalAsx(std::string_view values_xml);

// Escape text content with the same rules the renderer uses for text nodes.
[[nodiscard]] std::string EscapeXmlText(std::string_view text);

struct AbapGitRepoConfig {
    std::string master_language = "E";
    std::string starting_folder = "/src/";
    enum class FolderLogic { Prefix, Full };
    FolderLogic folder_logic = FolderLogic::Prefix;
    std::vector<std::string> ignore;
    std::string raw_values_xml;  // full compact <DATA>... so unknown fields survive a round trip
};

// Parse a repository's .abapgit.xml.
[[nodiscard]] Result<AbapGitRepoConfig, Error> ParseDotAbapGit(std::string_view content);

// Render .abapgit.xml byte-exactly. When raw_values_xml is set it is rendered
// as is; otherwise <DATA> is built from the typed fields.
[[nodiscard]] std::string RenderDotAbapGit(const AbapGitRepoConfig& config);

}  // namespace erpl_adt
