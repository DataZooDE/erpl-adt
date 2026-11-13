#include <catch2/catch_test_macros.hpp>

#include <erpl_adt/adt/abapgit_xml.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <regex>
#include <string>
#include <vector>

using namespace erpl_adt;

namespace {

namespace fs = std::filesystem;

const std::string kBom = "\xEF\xBB\xBF";

fs::path AbapGitTestDataDir() {
    std::string this_file = __FILE__;
    auto test_dir = this_file.substr(0, this_file.find_last_of("/\\"));
    auto test_root = test_dir.substr(0, test_dir.find_last_of("/\\"));
    return fs::path(test_root) / "testdata" / "abapgit";
}

std::string ReadBytes(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    REQUIRE(in.good());
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

// All *.xml fixtures under test/testdata/abapgit, sorted for stable output.
std::vector<fs::path> FixtureFiles(bool dot_abapgit) {
    std::vector<fs::path> files;
    const auto dir = AbapGitTestDataDir();
    REQUIRE(fs::is_directory(dir));
    for (const auto& entry : fs::recursive_directory_iterator(dir)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".xml") {
            continue;
        }
        const bool is_dot = entry.path().filename() == ".abapgit.xml";
        if (is_dot == dot_abapgit) {
            files.push_back(entry.path());
        }
    }
    std::sort(files.begin(), files.end());
    return files;
}

std::string Wrap(const std::string& values) {
    return kBom +
           "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
           "<abapGit version=\"v1.0.0\" serializer=\"LCL_OBJECT_PROG\" "
           "serializer_version=\"v1.0.0\">\n"
           " <asx:abap xmlns:asx=\"http://www.sap.com/abapxml\" version=\"1.0\">\n"
           "  <asx:values>\n" +
           values +
           "  </asx:values>\n"
           " </asx:abap>\n"
           "</abapGit>\n";
}

AbapGitXml Prog(const std::string& values_xml) {
    AbapGitXml xml;
    xml.serializer = "LCL_OBJECT_PROG";
    xml.serializer_version = "v1.0.0";
    xml.values_xml = values_xml;
    return xml;
}

// What SAP's CALL TRANSFORMATION id returns for the same data: no whitespace
// between tags, no <abapGit> wrapper, utf-16 declaration, optional BOM.
std::string SapCompactForm(const std::string& fixture) {
    const auto begin = fixture.find("<asx:abap");
    const std::string end_tag = "</asx:abap>";
    const auto end = fixture.find(end_tag);
    REQUIRE(begin != std::string::npos);
    REQUIRE(end != std::string::npos);
    std::string body = fixture.substr(begin, end + end_tag.size() - begin);
    body = std::regex_replace(body, std::regex(">\n *<"), "><");
    return kBom + "<?xml version=\"1.0\" encoding=\"utf-16\"?>" + body;
}

}  // namespace

// ===========================================================================
// Byte-exact round trip over the real abapGit fixtures
// ===========================================================================

TEST_CASE("abapGit object files round-trip byte-exactly", "[abapgit_xml]") {
    const auto files = FixtureFiles(false);
    REQUIRE(files.size() >= 30);
    for (const auto& path : files) {
        INFO(path.string());
        const auto bytes = ReadBytes(path);
        auto parsed = ParseAbapGitXml(bytes);
        REQUIRE(parsed.IsOk());
        CHECK(RenderAbapGitXml(parsed.Value()) == bytes);
    }
}

TEST_CASE("ParseAbapGitXml: fields of a PROG file", "[abapgit_xml]") {
    const auto bytes = ReadBytes(AbapGitTestDataDir() / "PROG" / "src" / "zag_unit_test.prog.xml");
    auto parsed = ParseAbapGitXml(bytes);
    REQUIRE(parsed.IsOk());
    CHECK(parsed.Value().serializer == "LCL_OBJECT_PROG");
    CHECK(parsed.Value().serializer_version == "v1.0.0");
    const auto& values = parsed.Value().values_xml;
    CHECK(values.rfind("<PROGDIR><NAME>ZAG_UNIT_TEST</NAME><SUBC>1</SUBC>", 0) == 0);
    CHECK(values.find('\n') == std::string::npos);
    CHECK(values.find("> <") == std::string::npos);
    CHECK(values.find("<TPOOL><item><ID>R</ID><ENTRY>unit test</ENTRY>") != std::string::npos);
}

TEST_CASE("ParseAbapGitXml: file without serializer attributes", "[abapgit_xml]") {
    // FUGR include files are written as <abapGit version="v1.0.0"> only.
    const auto bytes = ReadBytes(AbapGitTestDataDir() / "FUGR_with_parameters" / "src" /
                                 "zagtest_function_group.fugr.lzagtest_function_grouptop.xml");
    auto parsed = ParseAbapGitXml(bytes);
    REQUIRE(parsed.IsOk());
    CHECK(parsed.Value().serializer.empty());
    CHECK(parsed.Value().serializer_version.empty());
    CHECK(RenderAbapGitXml(parsed.Value()) == bytes);
}

TEST_CASE("ParseAbapGitXml: non-ASCII UTF-8 text survives", "[abapgit_xml]") {
    const auto bytes = ReadBytes(AbapGitTestDataDir() / "PROG_dynp_flow" / "src" /
                                 "zdemo_dynpro_splitter_control.prog.xml");
    auto parsed = ParseAbapGitXml(bytes);
    REQUIRE(parsed.IsOk());
    CHECK(parsed.Value().values_xml.find("<ENTRY>Demo f\xC3\xBCr Splitter Control</ENTRY>") !=
          std::string::npos);
}

// ===========================================================================
// .abapgit.xml
// ===========================================================================

TEST_CASE(".abapgit.xml files round-trip byte-exactly", "[abapgit_xml]") {
    const auto files = FixtureFiles(true);
    REQUIRE(files.size() >= 5);
    for (const auto& path : files) {
        INFO(path.string());
        const auto bytes = ReadBytes(path);
        auto parsed = ParseDotAbapGit(bytes);
        REQUIRE(parsed.IsOk());
        CHECK(RenderDotAbapGit(parsed.Value()) == bytes);
    }
}

TEST_CASE("ParseDotAbapGit: typed fields", "[abapgit_xml]") {
    auto parsed = ParseDotAbapGit(ReadBytes(AbapGitTestDataDir() / "PROG" / ".abapgit.xml"));
    REQUIRE(parsed.IsOk());
    const auto& config = parsed.Value();
    CHECK(config.master_language == "E");
    CHECK(config.starting_folder == "/src/");
    CHECK(config.folder_logic == AbapGitRepoConfig::FolderLogic::Prefix);
    CHECK(config.ignore == std::vector<std::string>{"/.gitignore", "/LICENSE", "/README.md",
                                                    "/package.json", "/.travis.yml"});
    CHECK(config.raw_values_xml.rfind("<DATA><MASTER_LANGUAGE>E</MASTER_LANGUAGE>", 0) == 0);
}

TEST_CASE("RenderDotAbapGit: from typed fields without raw xml", "[abapgit_xml]") {
    const auto bytes = ReadBytes(AbapGitTestDataDir() / "PROG" / ".abapgit.xml");
    auto parsed = ParseDotAbapGit(bytes);
    REQUIRE(parsed.IsOk());
    auto config = parsed.Value();
    config.raw_values_xml.clear();
    CHECK(RenderDotAbapGit(config) == bytes);
}

TEST_CASE("RenderDotAbapGit: defaults, FULL folder logic, no ignore list", "[abapgit_xml]") {
    AbapGitRepoConfig config;
    config.folder_logic = AbapGitRepoConfig::FolderLogic::Full;
    const std::string expected = kBom +
                                 "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
                                 "<asx:abap xmlns:asx=\"http://www.sap.com/abapxml\" version=\"1.0\">\n"
                                 " <asx:values>\n"
                                 "  <DATA>\n"
                                 "   <MASTER_LANGUAGE>E</MASTER_LANGUAGE>\n"
                                 "   <STARTING_FOLDER>/src/</STARTING_FOLDER>\n"
                                 "   <FOLDER_LOGIC>FULL</FOLDER_LOGIC>\n"
                                 "  </DATA>\n"
                                 " </asx:values>\n"
                                 "</asx:abap>\n";
    CHECK(RenderDotAbapGit(config) == expected);
    auto reparsed = ParseDotAbapGit(expected);
    REQUIRE(reparsed.IsOk());
    CHECK(reparsed.Value().folder_logic == AbapGitRepoConfig::FolderLogic::Full);
    CHECK(reparsed.Value().ignore.empty());
}

TEST_CASE("ParseDotAbapGit: errors", "[abapgit_xml]") {
    CHECK(ParseDotAbapGit("").IsErr());
    CHECK(ParseDotAbapGit("<asx:abap><asx:values>").IsErr());
    // An object file is not a repository config.
    CHECK(ParseDotAbapGit(Wrap("   <PROGDIR>\n    <NAME>Z</NAME>\n   </PROGDIR>\n")).IsErr());
    CHECK(ParseDotAbapGit("<asx:abap><asx:values><FOO/></asx:values></asx:abap>").IsErr());
}

// ===========================================================================
// From SAP's compact asXML
// ===========================================================================

TEST_CASE("SAP compact asXML renders to the abapGit fixture bytes", "[abapgit_xml]") {
    for (const auto& path : FixtureFiles(false)) {
        INFO(path.string());
        const auto bytes = ReadBytes(path);
        auto parsed = ParseAbapGitXml(bytes);
        REQUIRE(parsed.IsOk());

        auto values = ExtractAsxValues(SapCompactForm(bytes));
        REQUIRE(values.IsOk());
        AbapGitXml xml;
        xml.serializer = parsed.Value().serializer;
        xml.serializer_version = parsed.Value().serializer_version;
        xml.values_xml = values.Value();
        CHECK(RenderAbapGitXml(xml) == bytes);
    }
}

TEST_CASE("ExtractAsxValues: BOM and declaration are optional", "[abapgit_xml]") {
    const std::string body =
        "<asx:abap xmlns:asx=\"http://www.sap.com/abapxml\" version=\"1.0\"><asx:values>"
        "<DYNPROS><item><HEADER><PROGRAM>Z</PROGRAM></HEADER></item></DYNPROS>"
        "</asx:values></asx:abap>";
    const std::string expected = "<DYNPROS><item><HEADER><PROGRAM>Z</PROGRAM></HEADER></item></DYNPROS>";
    for (const auto& doc : {body, "<?xml version=\"1.0\" encoding=\"utf-8\"?>" + body,
                            kBom + "<?xml version=\"1.0\" encoding=\"utf-16\"?>" + body,
                            kBom + body}) {
        INFO(doc);
        auto values = ExtractAsxValues(doc);
        REQUIRE(values.IsOk());
        CHECK(values.Value() == expected);
    }
}

TEST_CASE("ExtractAsxValues: SAP escaping is normalized to abapGit's", "[abapgit_xml]") {
    // SAP's sXML writer need not escape quotes in text; abapGit's iXML
    // renderer does (see the escaping test below), so the rendered file must
    // carry &quot;/&apos; regardless of how SAP sent it.
    auto values = ExtractAsxValues(
        "<asx:abap xmlns:asx=\"http://www.sap.com/abapxml\" version=\"1.0\"><asx:values>"
        "<TPOOL><item><ENTRY>It's \"x\" &amp; y &gt; z</ENTRY></item></TPOOL>"
        "</asx:values></asx:abap>");
    REQUIRE(values.IsOk());
    CHECK(values.Value() ==
          "<TPOOL><item><ENTRY>It&apos;s &quot;x&quot; &amp; y &gt; z</ENTRY></item></TPOOL>");
}

TEST_CASE("ExtractAsxValues: errors", "[abapgit_xml]") {
    CHECK(ExtractAsxValues("").IsErr());
    CHECK(ExtractAsxValues("not xml").IsErr());
    CHECK(ExtractAsxValues("<asx:abap><asx:values><A></asx:values></asx:abap>").IsErr());
    CHECK(ExtractAsxValues("<foo><asx:values/></foo>").IsErr());
    CHECK(ExtractAsxValues("<asx:abap><other/></asx:abap>").IsErr());
}

TEST_CASE("ExtractAsxValues: empty asx:values", "[abapgit_xml]") {
    auto values = ExtractAsxValues("<asx:abap version=\"1.0\"><asx:values/></asx:abap>");
    REQUIRE(values.IsOk());
    CHECK(values.Value().empty());
}

TEST_CASE("WrapAsxValues: compact asXML for CALL TRANSFORMATION id", "[abapgit_xml]") {
    CHECK(WrapAsxValues("<PROGDIR><NAME>Z</NAME></PROGDIR>") ==
          "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
          "<asx:abap xmlns:asx=\"http://www.sap.com/abapxml\" version=\"1.0\"><asx:values>"
          "<PROGDIR><NAME>Z</NAME></PROGDIR></asx:values></asx:abap>");
    auto back = ExtractAsxValues(WrapAsxValues("<PROGDIR><NAME>Z</NAME></PROGDIR>"));
    REQUIRE(back.IsOk());
    CHECK(back.Value() == "<PROGDIR><NAME>Z</NAME></PROGDIR>");
}

// ===========================================================================
// Split / Join / Canonical
// ===========================================================================

TEST_CASE("SplitAsxValues / JoinAsxValues round trip", "[abapgit_xml]") {
    const auto bytes = ReadBytes(AbapGitTestDataDir() / "PROG" / "src" / "zag_unit_test.prog.xml");
    auto parsed = ParseAbapGitXml(bytes);
    REQUIRE(parsed.IsOk());
    const auto parts = SplitAsxValues(parsed.Value().values_xml);
    REQUIRE(parts.size() == 3);
    CHECK(parts[0].first == "PROGDIR");
    CHECK(parts[1].first == "DYNPROS");
    CHECK(parts[2].first == "TPOOL");
    CHECK(parts[0].second.rfind("<PROGDIR><NAME>ZAG_UNIT_TEST</NAME>", 0) == 0);
    CHECK(parts[0].second.substr(parts[0].second.size() - 10) == "</PROGDIR>");
    CHECK(JoinAsxValues(parts) == parsed.Value().values_xml);
}

TEST_CASE("SplitAsxValues: replacing one part", "[abapgit_xml]") {
    auto parts = SplitAsxValues("<A>1</A><B><C>2</C></B><D/>");
    REQUIRE(parts.size() == 3);
    CHECK(parts[2] == std::make_pair(std::string("D"), std::string("<D/>")));
    parts[1].second = "<B><C>3</C></B>";
    CHECK(JoinAsxValues(parts) == "<A>1</A><B><C>3</C></B><D/>");
}

TEST_CASE("SplitAsxValues: empty and malformed input", "[abapgit_xml]") {
    CHECK(SplitAsxValues("").empty());
    CHECK(SplitAsxValues("<A>").empty());
    CHECK(JoinAsxValues({}).empty());
}

TEST_CASE("CanonicalAsx: pretty and compact forms compare equal", "[abapgit_xml]") {
    const std::string pretty =
        "\n   <PROGDIR>\n    <NAME>Z</NAME>\n    <SUBC>1</SUBC>\n   </PROGDIR>\n"
        "   <TPOOL>\n    <item>\n     <ENTRY>It's</ENTRY>\n    </item>\n   </TPOOL>\n";
    const std::string compact =
        "<PROGDIR><NAME>Z</NAME><SUBC>1</SUBC></PROGDIR><TPOOL><item><ENTRY>It&apos;s</ENTRY>"
        "</item></TPOOL>";
    auto a = CanonicalAsx(pretty);
    auto b = CanonicalAsx(compact);
    REQUIRE(a.IsOk());
    REQUIRE(b.IsOk());
    CHECK(a.Value() == b.Value());
    CHECK(a.Value() == compact);

    auto changed = CanonicalAsx("<PROGDIR><NAME>Z</NAME><SUBC>2</SUBC></PROGDIR>"
                                "<TPOOL><item><ENTRY>It&apos;s</ENTRY></item></TPOOL>");
    REQUIRE(changed.IsOk());
    CHECK(changed.Value() != b.Value());
}

TEST_CASE("CanonicalAsx: empty element forms and text whitespace", "[abapgit_xml]") {
    auto a = CanonicalAsx("<A></A><B/>");
    REQUIRE(a.IsOk());
    CHECK(a.Value() == "<A/><B/>");
    // Text content is data: leading/trailing spaces are kept and compared.
    auto spaced = CanonicalAsx("<A>  x </A>");
    REQUIRE(spaced.IsOk());
    CHECK(spaced.Value() == "<A>  x </A>");
    CHECK(CanonicalAsx("<A>x</A>").Value() != spaced.Value());
}

TEST_CASE("CanonicalAsx: malformed input is an error", "[abapgit_xml]") {
    CHECK(CanonicalAsx("<A>").IsErr());
    CHECK(CanonicalAsx("<A></B>").IsErr());
    CHECK(CanonicalAsx("<A>&bogus;</A>").IsErr());
}

// ===========================================================================
// Escaping and whitespace
// ===========================================================================

TEST_CASE("EscapeXmlText: abapGit's iXML entity rules", "[abapgit_xml]") {
    // Evidence (files written by abapGit through SAP iXML's renderer):
    //  - &apos; in text: abap2xlsx/abap2xlsx src/zcl_excel_comment.clas.xml
    //    "<DESCRIPT>Reference to cell (eg. &apos;B13&apos;)</DESCRIPT>"
    //  - &quot; in text: yangbp/SAPDEMOS src/demo_escape.prog.xml
    //    "<ENTRY>Built-In Function &quot;escape&quot;</ENTRY>"
    //  - &amp; &lt; &gt; in text: ABAP-Logger/ABAP-Logger src/zcl_logger.clas.xml
    //    "<TDLINE>&lt;DS:RE.SAPLSBAL&gt;SBAL&lt;/&gt;.</TDLINE>",
    //    "<TDLINE>&amp;FUNCTIONALITY&amp;</TDLINE>", "zcl_logger=&gt;new( )"
    CHECK(EscapeXmlText("a & b") == "a &amp; b");
    CHECK(EscapeXmlText("<B>x</>") == "&lt;B&gt;x&lt;/&gt;");
    CHECK(EscapeXmlText("it's") == "it&apos;s");
    CHECK(EscapeXmlText("\"q\"") == "&quot;q&quot;");
    CHECK(EscapeXmlText("Bestell\xC3\xBC" "bersicht") == "Bestell\xC3\xBC" "bersicht");
    CHECK(EscapeXmlText("  lead and trail  ") == "  lead and trail  ");
    CHECK(EscapeXmlText("") == "");
}

TEST_CASE("RenderAbapGitXml: escaping in rendered text", "[abapgit_xml]") {
    auto values = CanonicalAsx("<TPOOL><item><ENTRY>a &amp; &lt;b&gt; \"c\" 'd'</ENTRY></item></TPOOL>");
    REQUIRE(values.IsOk());
    CHECK(RenderAbapGitXml(Prog(values.Value())) ==
          Wrap("   <TPOOL>\n"
               "    <item>\n"
               "     <ENTRY>a &amp; &lt;b&gt; &quot;c&quot; &apos;d&apos;</ENTRY>\n"
               "    </item>\n"
               "   </TPOOL>\n"));
}

TEST_CASE("RenderAbapGitXml: leading/trailing spaces and non-ASCII are preserved", "[abapgit_xml]") {
    // Evidence that iXML keeps leading spaces in text: ysichov/ACE
    // src/z_ace.prog.xml "<ENTRY>   Press Enter to Run selected program analysis</ENTRY>",
    // oisee/zllm src/zllm_00/zif_llm_00_pat.intf.xml "<TDLINE>  * &lt;B&gt;Type...".
    const std::string file = Wrap(
        "   <TPOOL>\n"
        "    <item>\n"
        "     <ENTRY>   Bestell\xC3\xBC" "bersicht  </ENTRY>\n"
        "    </item>\n"
        "   </TPOOL>\n");
    auto parsed = ParseAbapGitXml(file);
    REQUIRE(parsed.IsOk());
    CHECK(parsed.Value().values_xml ==
          "<TPOOL><item><ENTRY>   Bestell\xC3\xBC" "bersicht  </ENTRY></item></TPOOL>");
    CHECK(RenderAbapGitXml(parsed.Value()) == file);
}

TEST_CASE("RenderAbapGitXml: empty elements render as <X/>", "[abapgit_xml]") {
    // Evidence: marcellourbani/abapfs_extensions src/zabapfilesystem_session.enho.xml
    // renders an empty source line as "      <item/>".
    const std::string file = Wrap(
        "   <SOURCE>\n"
        "    <item>x</item>\n"
        "    <item/>\n"
        "   </SOURCE>\n");
    auto parsed = ParseAbapGitXml(file);
    REQUIRE(parsed.IsOk());
    CHECK(parsed.Value().values_xml == "<SOURCE><item>x</item><item/></SOURCE>");
    CHECK(RenderAbapGitXml(parsed.Value()) == file);
    CHECK(RenderAbapGitXml(Prog("<SOURCE><item>x</item><item></item></SOURCE>")) == file);
}

TEST_CASE("RenderAbapGitXml: no serializer renders bare abapGit tag", "[abapgit_xml]") {
    AbapGitXml xml;
    xml.values_xml = "<A>1</A>";
    CHECK(RenderAbapGitXml(xml) ==
          kBom +
              "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
              "<abapGit version=\"v1.0.0\">\n"
              " <asx:abap xmlns:asx=\"http://www.sap.com/abapxml\" version=\"1.0\">\n"
              "  <asx:values>\n"
              "   <A>1</A>\n"
              "  </asx:values>\n"
              " </asx:abap>\n"
              "</abapGit>\n");
}

// ===========================================================================
// Parse errors
// ===========================================================================

TEST_CASE("ParseAbapGitXml: malformed input is an error", "[abapgit_xml]") {
    CHECK(ParseAbapGitXml("").IsErr());
    CHECK(ParseAbapGitXml("garbage").IsErr());
    CHECK(ParseAbapGitXml(Wrap("   <PROGDIR>\n    <NAME>Z</NAME>\n")).IsErr());
    CHECK(ParseAbapGitXml(Wrap("   <A>&nope;</A>\n")).IsErr());
}

TEST_CASE("ParseAbapGitXml: missing envelope is an error", "[abapgit_xml]") {
    // No <abapGit> wrapper (a .abapgit.xml-style document).
    CHECK(ParseAbapGitXml("<asx:abap xmlns:asx=\"http://www.sap.com/abapxml\" version=\"1.0\">"
                          "<asx:values><A/></asx:values></asx:abap>")
              .IsErr());
    // <abapGit> without asx:abap.
    CHECK(ParseAbapGitXml("<abapGit version=\"v1.0.0\"><A/></abapGit>").IsErr());
    // asx:abap without asx:values.
    CHECK(ParseAbapGitXml("<abapGit version=\"v1.0.0\"><asx:abap><A/></asx:abap></abapGit>").IsErr());
}

TEST_CASE("ParseAbapGitXml: CRLF input normalizes to the LF form", "[abapgit_xml]") {
    // A checkout with core.autocrlf=true hands us CRLF; the payload is the same.
    const std::string lf = Wrap("   <A>1</A>\n");
    const std::string crlf = std::regex_replace(lf, std::regex("\n"), "\r\n");
    auto parsed = ParseAbapGitXml(crlf);
    REQUIRE(parsed.IsOk());
    CHECK(parsed.Value().values_xml == "<A>1</A>");
    CHECK(RenderAbapGitXml(parsed.Value()) == lf);
}

TEST_CASE("ParseDotAbapGit: CRLF input reads like the LF file", "[abapgit_xml]") {
    const std::string lf = ReadBytes(AbapGitTestDataDir() / "PROG" / ".abapgit.xml");
    const std::string crlf = std::regex_replace(lf, std::regex("\n"), "\r\n");
    auto from_lf = ParseDotAbapGit(lf);
    auto from_crlf = ParseDotAbapGit(crlf);
    REQUIRE(from_lf.IsOk());
    REQUIRE(from_crlf.IsOk());
    CHECK(from_crlf.Value().starting_folder == from_lf.Value().starting_folder);
    CHECK(from_crlf.Value().ignore == from_lf.Value().ignore);
    CHECK(RenderDotAbapGit(from_crlf.Value()) == lf);
}

TEST_CASE("ParseDotAbapGit: a starting folder that leaves the repository is refused",
          "[abapgit_xml]") {
    auto dot = [](const std::string& folder) {
        return "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n<asx:abap "
               "xmlns:asx=\"http://www.sap.com/abapxml\" version=\"1.0\">\n <asx:values>\n"
               "  <DATA>\n   <MASTER_LANGUAGE>E</MASTER_LANGUAGE>\n   <STARTING_FOLDER>" +
               folder + "</STARTING_FOLDER>\n  </DATA>\n </asx:values>\n</asx:abap>\n";
    };
    for (const char* folder : {"/../../outside/", "/src/../../x/", "..", "/src/..\\..\\x/"}) {
        auto parsed = ParseDotAbapGit(dot(folder));
        CHECK(parsed.IsErr());
    }
    auto fine = ParseDotAbapGit(dot("/src/abap/"));
    REQUIRE(fine.IsOk());
    CHECK(fine.Value().starting_folder == "/src/abap/");
}
