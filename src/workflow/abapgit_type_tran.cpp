// TRAN — transactions (abapGit serializer LCL_OBJECT_TRAN).
// SAP APIs: RPY_TRANSACTION_READ / RPY_TRANSACTION_INSERT /
// RPY_TRANSACTION_DELETE, the calls SE93 and abapGit make. The insert locks
// the transaction and records it on the transport itself (RS_CORR_INSERT with
// the transport number). There is no update API: an update is delete +
// insert, as in abapGit.
//
// Supported: dialog, report, parameter and variant transactions with their
// GUI flags and short text in the master language. Refused before anything
// is written (never silently dropped): OO transactions, authorization-object
// checks (TSTCA / CINFO x'04'), report transactions with a variant
// (CINFO x'10' — abapGit sets it with a direct UPDATE on TSTC), translations
// (I18N_TPOOL — abapGit writes TSTCT directly) and SU22 data. All of these
// are only reachable by writing repository tables directly.

#include "abapgit_types.hpp"

#include <erpl_adt/adt/classic_engine.hpp>

#include <algorithm>
#include <cstdio>
#include <optional>
#include <set>

namespace erpl_adt {

namespace {

EngineOp TranGetOp(const std::string& name, const std::string& language) {
    return {"tran_get( name = " + AbapQuote(name) + " language = " + AbapQuote(language) + " ).",
            ""};
}

EngineOp TranPutOp(const std::string& name, const std::string& language,
                   const std::string& devclass, const std::string& trkorr,
                   const std::string& asx_xml) {
    return {"tran_put( name = " + AbapQuote(name) + " language = " + AbapQuote(language) +
                " devclass = " + AbapQuote(devclass) + " trkorr = " + AbapQuote(trkorr) + " ).",
            asx_xml};
}

EngineOp TranDeleteOp(const std::string& name, const std::string& trkorr) {
    return {"tran_delete( name = " + AbapQuote(name) + " trkorr = " + AbapQuote(trkorr) + " ).",
            ""};
}

// The parts RPY_TRANSACTION_INSERT writes, in abapGit's order.
const std::vector<std::string>& WritableParts() {
    static const std::vector<std::string> parts = {"TSTC", "TSTCC", "TSTCT", "TSTCP"};
    return parts;
}

// Children of a compact section "<TSTC><TCODE>A</TCODE>...</TSTC>".
std::vector<std::pair<std::string, std::string>> Children(const std::string& section) {
    const auto open_end = section.find('>');
    const auto close = section.rfind("</");
    if (open_end == std::string::npos || close == std::string::npos || close <= open_end) {
        return {};
    }
    return SplitAsxValues(section.substr(open_end + 1, close - open_end - 1));
}

// Text of a leaf element "<CINFO>gA==</CINFO>".
std::string Text(const std::string& leaf) {
    const auto open_end = leaf.find('>');
    const auto close = leaf.rfind("</");
    if (open_end == std::string::npos || close == std::string::npos || close <= open_end) {
        return "";
    }
    return leaf.substr(open_end + 1, close - open_end - 1);
}

std::string Hex(unsigned char byte) {
    char buffer[3];
    std::snprintf(buffer, sizeof(buffer), "%02X", byte);
    return buffer;
}

void CheckFields(const std::string& part, const std::string& section,
                 const std::set<std::string>& allowed, std::vector<std::string>& problems) {
    for (const auto& [field, xml] : Children(section)) {
        if (allowed.count(field) == 0) {
            problems.push_back(part + "-" + field +
                               " (not settable through RPY_TRANSACTION_INSERT)");
        }
    }
}

// What in the file can't be deployed through the standard API. Empty: all of it can.
std::vector<std::string> UnsupportedParts(const AbapGitObjectKey& key,
                                          const std::vector<std::pair<std::string, std::string>>& sections,
                                          const std::string& language) {
    std::vector<std::string> problems;
    for (const auto& [part, section] : sections) {
        if (part == "AUTHORIZATIONS") {
            // Evidence (read on the A4H trial): RPY_TRANSACTION_INSERT has no
            // parameter for it; classic SE93 (LSEUKF01) and abapGit's
            // SAVE_AUTHORIZATIONS write TSTCA and the CINFO bit x'04' with
            // DELETE/INSERT/UPDATE; the only class that does it (CL_TRAN_WBI_P,
            // behind the new SAP-internal transaction editor) writes the same
            // tables directly, without lock or transport.
            problems.push_back(
                "AUTHORIZATIONS (authorization-object check, table TSTCA: no workbench API "
                "writes it - SE93 and abapGit update TSTCA/TSTC directly; maintain it in SE93)");
            continue;
        }
        if (part == "I18N_TPOOL") {
            problems.push_back("I18N_TPOOL (translations of the short text)");
            continue;
        }
        if (part == "TSTC") {
            CheckFields(part, section, {"TCODE", "PGMNA", "DYPNO", "CINFO"}, problems);
            for (const auto& [field, xml] : Children(section)) {
                if (field == "TCODE" && Text(xml) != key.name) {
                    problems.push_back("TSTC-TCODE " + Text(xml) + " (file is for " + key.name +
                                       ")");
                }
                if (field != "CINFO") {
                    continue;
                }
                const std::string bytes = DecodeBase64(Text(xml));
                const unsigned char cinfo =
                    bytes.size() == 1 ? static_cast<unsigned char>(bytes[0]) : 0xFF;
                if (cinfo == 0x00 || cinfo == 0x80 || cinfo == 0x02) {
                    continue;
                }
                std::string what = "CINFO x'" + Hex(cinfo) + "'";
                if ((cinfo & 0x08) != 0) {
                    what += " (OO transaction)";
                } else if ((cinfo & 0x10) != 0) {
                    what += " (report transaction with variant)";
                } else if ((cinfo & 0x04) != 0) {
                    what += " (authorization-object check)";
                } else if ((cinfo & 0x01) != 0) {
                    what += " (area menu)";
                }
                problems.push_back(what);
            }
            continue;
        }
        if (part == "TSTCC") {
            CheckFields(part, section, {"TCODE", "S_WEBGUI", "S_WIN32", "S_PLATIN", "S_SERVICE"},
                        problems);
            continue;
        }
        if (part == "TSTCT") {
            CheckFields(part, section, {"SPRSL", "TCODE", "TTEXT"}, problems);
            for (const auto& [field, xml] : Children(section)) {
                if (field == "SPRSL" && Text(xml) != language) {
                    problems.push_back("TSTCT in language " + Text(xml) + " (master language is " +
                                       language + ")");
                }
            }
            continue;
        }
        if (part == "TSTCP") {
            CheckFields(part, section, {"TCODE", "PARAM"}, problems);
            for (const auto& [field, xml] : Children(section)) {
                if (field == "PARAM" && !Text(xml).empty() && Text(xml)[0] == '\\') {
                    problems.push_back("TSTCP-PARAM " + Text(xml) + " (OO transaction)");
                }
            }
            continue;
        }
        problems.push_back(part + " (SU22 authorization defaults or unknown part)");
    }
    return problems;
}

std::map<std::string, std::string> SectionMap(const ObjectFileSet& files,
                                              const std::string& file_name) {
    std::map<std::string, std::string> out;
    auto it = files.find(file_name);
    if (it == files.end()) {
        return out;
    }
    auto parsed = ParseAbapGitXml(it->second);
    if (parsed.IsErr()) {
        return out;
    }
    for (auto& [name, xml] : SplitAsxValues(parsed.Value().values_xml)) {
        out[name] = xml;
    }
    return out;
}

// The file's sections without an I18N_TPOOL equal to the system's: an
// export writes the system's translations (as abapGit does) and such a file
// must deploy; translations the system does not hold cannot be written.
std::vector<std::pair<std::string, std::string>> WithoutSystemTranslations(
    std::vector<std::pair<std::string, std::string>> sections, const ObjectState& current,
    const std::string& file) {
    const auto have = SectionMap(current.files, file);
    auto it = have.find("I18N_TPOOL");
    sections.erase(std::remove_if(sections.begin(), sections.end(),
                                  [&](const auto& s) {
                                      return s.first == "I18N_TPOOL" && it != have.end() &&
                                             it->second == s.second;
                                  }),
                   sections.end());
    return sections;
}

Error UnsupportedError(const std::string& message, const std::vector<std::string>& problems) {
    Error err;
    err.operation = "abapGit";
    err.message = message;
    for (size_t i = 0; i < problems.size(); ++i) {
        err.message += (i ? "; " : "") + problems[i];
    }
    err.category = ErrorCategory::CheckError;
    return err;
}

// What the files ask for that cannot be written, and - for an existing
// transaction - what its system version holds that an update (delete +
// insert) cannot reproduce. The latter is refused whatever the files say and
// also with --force: it would silently drop an authorization check.
std::optional<Error> TranProblems(const AbapGitObjectKey& key,
                                  const std::vector<std::pair<std::string, std::string>>& sections,
                                  const ObjectState& current, const std::string& language) {
    const std::string file = FileName(key, "xml");
    if (current.exists) {
        std::vector<std::pair<std::string, std::string>> have;
        for (auto& entry : SectionMap(current.files, file)) {
            if (entry.first != "I18N_TPOOL") {  // translations: see the workflow
                have.push_back(entry);
            }
        }
        const auto lost = UnsupportedParts(key, have, language);
        if (!lost.empty()) {
            return UnsupportedError(
                "Transaction " + key.name + " in the system has parts an update (delete + "
                "insert through RPY_TRANSACTION_*) cannot reproduce, so it would lose them; "
                "nothing was written - maintain it in SE93: ",
                lost);
        }
    }
    const auto problems =
        UnsupportedParts(key, WithoutSystemTranslations(sections, current, file), language);
    if (!problems.empty()) {
        return UnsupportedError("Transaction " + key.name +
                                    " has parts erpl-adt cannot deploy through SE93's API "
                                    "(RPY_TRANSACTION_INSERT); nothing was written: ",
                                problems);
    }
    return std::nullopt;
}

class TranHandler final : public IClassicTypeHandler {
public:
    std::string Type() const override { return "TRAN"; }

    // An update is delete + insert: SE63 translations of the short text
    // (TSTCT in other languages) would go with it.
    bool KeepsUnmanagedTranslations() const override { return false; }

    std::vector<EngineOp> ReadOps(const AbapGitObjectKey& key,
                                  const std::string& language) const override {
        return {TranGetOp(key.name, language)};
    }

    Result<ObjectState, Error> BuildState(const AbapGitObjectKey& key,
                                          const std::vector<EngineOpResult>& results,
                                          TypeContext& /*ctx*/) const override {
        const auto& r = results.at(0);
        if (r.rc != 0) {
            return Result<ObjectState, Error>::Err(
                EngineFailure("Reading transaction " + key.name, r));
        }
        ObjectState state;
        state.exists = r.exists.value_or(false);
        if (!state.exists) {
            return Result<ObjectState, Error>::Ok(state);
        }
        // No last changer: TSTC/TSTCT/TSTCC/TSTCP carry no user, SE93 keeps no
        // version in VRSD, and the TADIR author is the creator. Left empty
        // ("unknown"), so drift protection asks for git history or --force.
        auto rendered = RenderFromEngine(r.data.count("XML") ? r.data.at("XML") : "",
                                         "LCL_OBJECT_TRAN");
        if (rendered.IsErr()) {
            return Result<ObjectState, Error>::Err(rendered.Error());
        }
        state.files[FileName(key, "xml")] = rendered.Value();
        return Result<ObjectState, Error>::Ok(state);
    }

    Result<void, Error> Validate(const AbapGitObjectKey& key, const ObjectFileSet& desired,
                                 const ObjectState& current,
                                 const std::string& language) const override {
        const std::string file = FileName(key, "xml");
        auto it = desired.find(file);
        if (it == desired.end()) {
            return Result<void, Error>::Err(EnginePayload(desired, file).Error());
        }
        auto parsed = ParseAbapGitXml(it->second);
        if (parsed.IsErr()) {
            return Result<void, Error>::Err(parsed.Error());
        }
        if (auto problem =
                TranProblems(key, SplitAsxValues(parsed.Value().values_xml), current, language)) {
            return Result<void, Error>::Err(*problem);
        }
        return Result<void, Error>::Ok();
    }

    // Write phase: one tran_put, after the programs are activated (a dialog
    // transaction's program and screen must exist).
    Result<DeployPlan, Error> Plan(const AbapGitObjectKey& key, const ObjectFileSet& desired,
                                   const ObjectState& current,
                                   const TypeContext& ctx) const override {
        using R = Result<DeployPlan, Error>;
        const std::string file = FileName(key, "xml");
        auto it = desired.find(file);
        if (it == desired.end()) {
            return R::Err(EnginePayload(desired, file).Error());
        }
        auto parsed = ParseAbapGitXml(it->second);
        if (parsed.IsErr()) {
            return R::Err(parsed.Error());
        }
        const auto sections = SplitAsxValues(parsed.Value().values_xml);

        if (auto problem = TranProblems(key, sections, current, ctx.language)) {
            return R::Err(*problem);
        }

        // Payload: the four writable parts, each present (empty when absent),
        // so the engine's transformation always finds all of them.
        std::map<std::string, std::string> by_name(sections.begin(), sections.end());
        std::vector<std::pair<std::string, std::string>> payload;
        for (const auto& part : WritableParts()) {
            auto found = by_name.find(part);
            payload.emplace_back(part, found != by_name.end() ? found->second : "<" + part + "/>");
        }

        DeployPlan plan;
        PlannedOp put;
        put.op = TranPutOp(key.name, ctx.language, ctx.package, ctx.transport,
                           WrapAsxValues(JoinAsxValues(payload)));
        put.what = "Writing transaction " + key.name;
        plan.writes.push_back(std::move(put));

        // Report only the parts that differ (the whole transaction is
        // re-inserted either way: there is no partial update).
        const auto before = SectionMap(current.files, file);
        std::vector<std::string> changed;
        for (const auto& part : WritableParts()) {
            auto want = by_name.find(part);
            auto have = before.find(part);
            const std::string w = want != by_name.end() ? want->second : "";
            const std::string h = have != before.end() ? have->second : "";
            if (w != h) {
                changed.push_back(part);
            }
        }
        for (const auto& [part, xml] : before) {
            if (by_name.count(part) == 0 &&
                std::find(changed.begin(), changed.end(), part) == changed.end()) {
                changed.push_back(part);  // e.g. AUTHORIZATIONS removed by the re-insert
            }
        }
        plan.parts = std::move(changed);
        return R::Ok(std::move(plan));
    }

    Result<void, Error> Delete(const AbapGitObjectKey& key, TypeContext& ctx) const override {
        auto run = RunChecked(ctx.engine, {TranDeleteOp(key.name, ctx.transport)},
                              "Deleting transaction " + key.name);
        if (run.IsErr()) {
            return Result<void, Error>::Err(run.Error());
        }
        return Result<void, Error>::Ok();
    }
};

}  // namespace

std::shared_ptr<const IClassicTypeHandler> MakeTranHandler() {
    return std::make_shared<TranHandler>();
}

}  // namespace erpl_adt
