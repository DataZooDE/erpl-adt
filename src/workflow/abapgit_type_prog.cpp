// PROG — executable programs and module pools with their screens (dynpros),
// GUI status / titles (CUA) and text pool. abapGit serializer LCL_OBJECT_PROG
// (ZCL_ABAPGIT_OBJECT_PROG / ZCL_ABAPGIT_OBJECTS_PROGRAM).
//
// Files:  <name>.prog.xml               PROGDIR, DYNPROS, CUA, VARIS, TPOOL
//         <name>.prog.abap              source
//         <name>.prog.screen_NNNN.abap  flow logic of screen NNNN
//
// Write path (only the parts that differ are written; everything is saved
// inactive and activated together afterwards, as SE80 does):
//   * new program: RS_CORR_INSERT + RPY_PROGRAM_INSERT (works for every
//     program type, module pools included — ADT can only create reports) +
//     UPDATE_PROGDIR, in the engine;
//   * source of an existing program: ADT (lock + PUT .../source/main + unlock)
//     — the same lock/transport handling as SE38 / Eclipse, with corrNr;
//   * attributes: UPDATE_PROGDIR on the inactive version;
//   * screens: RPY_DYNPRO_INSERT / RS_SCRP_DELETE; CUA: RS_CUA_INTERNAL_WRITE;
//     text pool: INSERT TEXTPOOL ... STATE 'I';
//   * activation: RS_WORKING_OBJECTS_ACTIVATE at the end of the deploy's
//     write run, for all programs at once (ADT activation of the program does
//     not activate CUAD / REPT);
//   * system variants (VARIS, SAP&... / CUS&...): after the activation, as
//     SAP checks a variant against the generated selection screen —
//     RS_VARIANT_DELETE + RS_CREATE_VARIANT_255 + RS_CHANGE_CREATED_VARIANT_255
//     (abapGit's DESERIALIZE_VARIS). Variants have no inactive version;
//   * native dynpros (NAT_HEADER / NAT_FIELDS, e.g. a dynpro splitter
//     control, which the external format cannot hold): RPY_DYNPRO_INSERT_NATIVE,
//     as abapGit. Their field texts (NAT_TEXTS) are refused: abapGit writes
//     them into D021T directly, and no workbench API does;
//   * translations of the text pool (I18N_TPOOL) and the documentation
//     (LONGTEXTS, id RE): abapgit_i18n.hpp, written active after the
//     program in the same run. Everything else here sees the files without
//     those sections.

#include "abapgit_i18n.hpp"
#include "abapgit_types.hpp"

#include <erpl_adt/workflow/lock_workflow.hpp>

#include <algorithm>
#include <cctype>
#include <set>
#include <sstream>

namespace erpl_adt {

namespace {

using PartsResult = Result<std::vector<std::string>, Error>;

std::string Lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

std::string AbapBool(bool value) { return value ? "abap_true" : "abap_false"; }

Error ProgError(const std::string& message, ErrorCategory category) {
    Error err;
    err.operation = "abapGit";
    err.message = message;
    err.category = category;
    return err;
}

// --- engine operations (src/abap/engine_prog.abap) --------------------------

EngineOp ProgGetOp(const std::string& name, const std::string& language) {
    return {"prog_get( name = " + AbapQuote(name) + " language = " + AbapQuote(language) + " ).",
            ""};
}

struct PutFlags {
    bool create = false;
    bool write_progdir = false;
    bool write_cua = false;
    bool write_tpool = false;
};

EngineOp ProgPutOp(const std::string& name, const TypeContext& ctx, const PutFlags& flags,
                   const std::string& asx_xml) {
    return {"prog_put( name = " + AbapQuote(name) + " language = " + AbapQuote(ctx.language) +
                " devclass = " + AbapQuote(ctx.package) + " trkorr = " + AbapQuote(ctx.transport) +
                " create = " + AbapBool(flags.create) +
                " write_progdir = " + AbapBool(flags.write_progdir) +
                " write_cua = " + AbapBool(flags.write_cua) +
                " write_tpool = " + AbapBool(flags.write_tpool) + " ).",
            asx_xml};
}

EngineOp ProgPutVarisOp(const std::string& name, const TypeContext& ctx,
                        const std::string& asx_xml) {
    return {"prog_put_varis( name = " + AbapQuote(name) + " language = " +
                AbapQuote(ctx.language) + " devclass = " + AbapQuote(ctx.package) +
                " trkorr = " + AbapQuote(ctx.transport) + " ).",
            asx_xml};
}

EngineOp ProgDeleteOp(const std::string& name, const std::string& devclass,
                      const std::string& trkorr) {
    return {"prog_delete( name = " + AbapQuote(name) + " devclass = " + AbapQuote(devclass) +
                " trkorr = " + AbapQuote(trkorr) + " ).",
            ""};
}

// --- the .prog.xml split into abapGit's parts --------------------------------

struct ProgParts {
    std::string progdir;                         // <PROGDIR>...</PROGDIR>
    std::map<std::string, std::string> screens;  // "0100" -> <item>...</item>
    std::string cua;                             // <CUA>...</CUA>
    // System variants by name (unescaped, e.g. "CUS&OPEN_POS") -> <item>...</item>
    std::map<std::string, std::string> varis;
    std::string tpool;                           // <TPOOL>...</TPOOL>
};

std::string InnerXml(const std::string& element) {
    auto open_end = element.find('>');
    auto close = element.rfind("</");
    if (open_end == std::string::npos || close == std::string::npos || close <= open_end) {
        return "";  // <X/> or malformed: no children
    }
    return element.substr(open_end + 1, close - open_end - 1);
}

std::string ElementText(const std::string& xml, const std::string& tag) {
    const std::string open = "<" + tag + ">";
    auto start = xml.find(open);
    if (start == std::string::npos) {
        return "";
    }
    start += open.size();
    auto end = xml.find("</" + tag + ">", start);
    return end == std::string::npos ? "" : xml.substr(start, end - start);
}

std::string UnescapeXmlText(std::string text) {
    static const std::vector<std::pair<std::string, std::string>> entities = {
        {"&lt;", "<"}, {"&gt;", ">"}, {"&quot;", "\""}, {"&apos;", "'"}, {"&amp;", "&"}};
    for (const auto& [entity, plain] : entities) {
        size_t pos = 0;
        while ((pos = text.find(entity, pos)) != std::string::npos) {
            text.replace(pos, entity.size(), plain);
            pos += plain.size();
        }
    }
    return text;
}

// abapGit only serializes system variants (DESERIALIZE_VARIS refuses others):
// they live in client 000 and travel with the program.
bool IsSystemVariant(const std::string& name) {
    return name.rfind("SAP&", 0) == 0 || name.rfind("CUS&", 0) == 0;
}

Result<ProgParts, Error> SplitProgXml(const std::string& file_content,
                                      const std::string& file_name) {
    using R = Result<ProgParts, Error>;
    auto parsed = ParseAbapGitXml(file_content);
    if (parsed.IsErr()) {
        return R::Err(parsed.Error());
    }
    ProgParts parts;
    for (const auto& [element, xml] : SplitAsxValues(parsed.Value().values_xml)) {
        if (element == "PROGDIR") {
            parts.progdir = xml;
        } else if (element == "DYNPROS") {
            for (const auto& [item_name, item] : SplitAsxValues(InnerXml(xml))) {
                const std::string screen = ElementText(ElementText(item, "HEADER"), "SCREEN");
                if (item_name != "item" || screen.empty()) {
                    return R::Err(ProgError(file_name + ": DYNPROS entry without HEADER/SCREEN",
                                            ErrorCategory::Internal));
                }
                parts.screens[screen] = item;
            }
        } else if (element == "CUA") {
            parts.cua = xml;
        } else if (element == "TPOOL") {
            parts.tpool = xml;
        } else if (element == "VARIS") {
            for (const auto& [item_name, item] : SplitAsxValues(InnerXml(xml))) {
                const std::string variant = UnescapeXmlText(ElementText(item, "VARIANT"));
                if (item_name != "item" || variant.empty()) {
                    return R::Err(ProgError(file_name + ": VARIS entry without VARIANT",
                                            ErrorCategory::Internal));
                }
                parts.varis[variant] = item;
            }
        } else if (element == "I18N_TPOOL") {
            return R::Err(ProgError(file_name +
                                        ": I18N_TPOOL (text pool translations) is not supported "
                                        "by erpl-adt; deploy the master language only",
                                    ErrorCategory::Internal));
        } else if (element == "LONGTEXTS") {
            return R::Err(ProgError(file_name +
                                        ": LONGTEXTS (program documentation) are not supported "
                                        "by erpl-adt",
                                    ErrorCategory::Internal));
        } else {
            return R::Err(ProgError(file_name + ": unknown part <" + element + ">",
                                    ErrorCategory::Internal));
        }
    }
    if (parts.progdir.empty()) {
        return R::Err(ProgError(file_name + ": PROGDIR is missing", ErrorCategory::Internal));
    }
    return R::Ok(std::move(parts));
}

// Field texts of a native dynpro (<NAT_TEXTS> with rows).
bool HasNativeTexts(const std::string& screen_item) {
    const auto start = screen_item.find("<NAT_TEXTS>");
    return start != std::string::npos &&
           screen_item.find("</NAT_TEXTS>", start) != std::string::npos;
}

std::string FlowFileName(const AbapGitObjectKey& key, const std::string& screen) {
    return FileName(key, "screen_" + screen + ".abap");
}

std::string FileOrEmpty(const ObjectFileSet& files, const std::string& name) {
    auto it = files.find(name);
    return it == files.end() ? std::string() : it->second;
}

// ADT stores the source as lines; the file's final LF is not an extra line.
std::string SourceForAdt(std::string text) {
    if (!text.empty() && text.back() == '\n') {
        text.pop_back();
    }
    return text;
}

std::string ProgramSourceUri(const std::string& name) {
    std::string encoded;
    for (char c : Lower(name)) {
        encoded += c == '/' ? std::string("%2f") : std::string(1, c);
    }
    return "/sap/bc/adt/programs/programs/" + encoded + "/source/main";
}

// Documentation of a program (abapGit: LONGTEXTS, docu id RE).
const std::vector<DocuSection> kDocu = {{"RE", "LONGTEXTS"}};

class ProgHandler final : public IClassicTypeHandler {
public:
    std::string Type() const override { return "PROG"; }

    std::vector<EngineOp> ReadOps(const AbapGitObjectKey& key,
                                  const std::string& language) const override {
        return {ProgGetOp(key.name, language), I18nTpoolGetOp(key.name, language),
                I18nDocuGetOp("RE", key.name, "LONGTEXTS")};
    }

    Result<ObjectState, Error> BuildState(const AbapGitObjectKey& key,
                                          const std::vector<EngineOpResult>& results,
                                          TypeContext& /*ctx*/) const override {
        const auto& r = results.at(0);
        if (r.rc != 0) {
            return Result<ObjectState, Error>::Err(
                EngineFailure("Reading program " + key.name, r));
        }
        ObjectState state;
        state.exists = r.exists.value_or(false);
        state.changed_by = r.changed_by;
        state.changed_on = r.changed_on;
        auto pending = r.data.find("PENDING");
        if (pending != r.data.end()) {
            std::istringstream words(pending->second);
            std::string word;
            while (words >> word) {
                state.pending.push_back(word);
            }
        }
        auto xml = r.data.find("XML");
        if (!state.exists || xml == r.data.end()) {
            // Not there, or only an inactive version: no files to compare.
            return Result<ObjectState, Error>::Ok(state);
        }
        auto reads = CollectI18nReads(key, results, 1);
        if (reads.IsErr()) {
            return Result<ObjectState, Error>::Err(reads.Error());
        }
        const auto& i18n = reads.Value();
        auto rendered = RenderWithI18n(xml->second, i18n.asxml, "LCL_OBJECT_PROG");
        if (rendered.IsErr()) {
            return Result<ObjectState, Error>::Err(rendered.Error());
        }
        state.files[FileName(key, "xml")] = rendered.Value();
        state.files[FileName(key, "abap")] = FileOrEmpty(r.data, "SOURCE");
        for (const auto& [tag, bytes] : r.data) {
            if (tag.rfind("SCREEN_", 0) == 0) {
                state.files[FlowFileName(key, tag.substr(7))] = bytes;
                // Dynpros record no last changer (D020S has none): unknown.
                state.part_changed_by[FlowFileName(key, tag.substr(7))] = "";
            }
        }
        // Last changers per part: source and attributes REPOSRC-UNAM, text
        // pool REPOTEXT-UNAM, GUI status EUDB-VAUTOR. Everything else in the
        // XML (DYNPROS) is unknown.
        const auto changed = ChangedParts(r);
        auto changer = [&](const std::string& part) {
            auto it = changed.find(part);
            return it == changed.end() ? std::string() : it->second;
        };
        const std::string xml_name = FileName(key, "xml");
        state.part_changed_by[FileName(key, "abap")] = changer("SOURCE");
        state.part_changed_by[xml_name] = "";
        state.part_changed_by[xml_name + "#PROGDIR"] = changer("SOURCE");
        state.part_changed_by[xml_name + "#TPOOL"] = changer("TEXTS");
        state.part_changed_by[xml_name + "#CUA"] = changer("CUA");
        // System variants: VARID-AENAME / ENAME (the logon user when there
        // are none: nothing to overwrite).
        state.part_changed_by[xml_name + "#VARIS"] = changer("VARIS");
        state.part_changed_by[xml_name + "#I18N_TPOOL"] = "";
        state.part_changed_by[xml_name + "#LONGTEXTS"] =
            i18n.changed_by.count("LONGTEXTS") ? i18n.changed_by.at("LONGTEXTS") : "";
        return Result<ObjectState, Error>::Ok(state);
    }

    Result<void, Error> Validate(const AbapGitObjectKey& key, const ObjectFileSet& desired,
                                 const ObjectState& current,
                                 const std::string& language) const override {
        ObjectState master = current;
        master.files = WithoutI18nSections(current.files);
        auto valid = ValidateMaster(key, WithoutI18nSections(desired), master, language);
        if (valid.IsErr()) {
            return valid;
        }
        return CheckDocuIdentity(key, desired, FileName(key, "xml"), kDocu);
    }

    // The program first (PlanMaster), then its translations and documentation.
    Result<DeployPlan, Error> Plan(const AbapGitObjectKey& key, const ObjectFileSet& desired,
                                   const ObjectState& current,
                                   const TypeContext& ctx) const override {
        ObjectState master = current;
        master.files = WithoutI18nSections(current.files);
        auto plan = PlanMaster(key, WithoutI18nSections(desired), master, ctx);
        if (plan.IsErr()) {
            return plan;
        }
        const std::string xml_name = FileName(key, "xml");
        const auto want = FileSections(desired.at(xml_name));
        const auto have = current.files.count(xml_name) ? FileSections(current.files.at(xml_name))
                                                        : XmlSections{};
        auto i18n = PlanI18n(
            key, want, have,
            [&](const std::string& texts) {
                return I18nTpoolPutOp(key.name, ctx.language, ctx.package, ctx.transport, texts);
            },
            kDocu, key.name, "ABAP", ctx);
        auto out = std::move(plan).Value();
        out.parts.insert(out.parts.end(), i18n.parts.begin(), i18n.parts.end());
        for (auto& op : i18n.ops) {
            out.writes.push_back(std::move(op));
        }
        return Result<DeployPlan, Error>::Ok(std::move(out));
    }

private:
    Result<void, Error> ValidateMaster(const AbapGitObjectKey& key, const ObjectFileSet& desired,
                                       const ObjectState& current,
                                       const std::string& /*language*/) const {
        const std::string xml_name = FileName(key, "xml");
        const std::string abap_name = FileName(key, "abap");
        auto xml_it = desired.find(xml_name);
        if (xml_it == desired.end()) {
            return Result<void, Error>::Err(
                ProgError("Missing file " + xml_name, ErrorCategory::Internal));
        }
        auto want = SplitProgXml(xml_it->second, xml_name);
        if (want.IsErr()) {
            return Result<void, Error>::Err(want.Error());
        }
        if (desired.find(abap_name) == desired.end()) {
            return Result<void, Error>::Err(ProgError(
                "Missing file " + abap_name + " (program source)", ErrorCategory::Internal));
        }
        // Every flow logic file needs its screen and vice versa.
        const std::string flow_prefix = FileName(key, "screen_");
        for (const auto& [name, bytes] : desired) {
            if (name.rfind(flow_prefix, 0) == 0) {
                const std::string screen = name.substr(flow_prefix.size(), 4);
                if (want.Value().screens.count(screen) == 0) {
                    return Result<void, Error>::Err(ProgError(
                        name + " has no screen " + screen + " in DYNPROS of " + xml_name,
                        ErrorCategory::Internal));
                }
            }
        }
        for (const auto& [screen, item] : want.Value().screens) {
            if (desired.find(FlowFileName(key, screen)) == desired.end()) {
                return Result<void, Error>::Err(ProgError(
                    "Missing file " + FlowFileName(key, screen) + " (flow logic of screen " +
                        screen + ")",
                    ErrorCategory::Internal));
            }
        }
        // Native dynpros with field texts: only writable into D021T directly.
        ProgParts have;
        if (auto cur = current.files.find(xml_name); cur != current.files.end()) {
            auto split = SplitProgXml(cur->second, xml_name + " (system)");
            if (split.IsOk()) {
                have = split.Value();
            }
        }
        for (const auto& [screen, item] : want.Value().screens) {
            auto old = have.screens.find(screen);
            const bool same = old != have.screens.end() && old->second == item;
            if (!same && (HasNativeTexts(item) ||
                          (old != have.screens.end() && HasNativeTexts(old->second)))) {
                return Result<void, Error>::Err(ProgError(
                    xml_name + ": screen " + screen +
                        " is a native dynpro with field texts (NAT_TEXTS); RPY_DYNPRO_INSERT_NATIVE "
                        "does not write them and abapGit inserts them into table D021T directly, "
                        "which erpl-adt does not do - maintain the texts in SE51",
                    ErrorCategory::CheckError));
            }
        }
        for (const auto& [variant, item] : want.Value().varis) {
            if (!IsSystemVariant(variant)) {
                return Result<void, Error>::Err(ProgError(
                    xml_name + ": variant " + variant +
                        " is not a system variant; abapGit's VARIS holds only SAP&... and "
                        "CUS&... variants (client 000, transported with the program) - "
                        "rename it to CUS&" + variant.substr(0, 10),
                    ErrorCategory::CheckError));
            }
        }
        auto cur_xml = current.files.find(xml_name);
        if (cur_xml != current.files.end() && want.Value().cua.empty()) {
            auto have = SplitProgXml(cur_xml->second, xml_name + " (system)");
            if (have.IsOk() && !have.Value().cua.empty()) {
                return Result<void, Error>::Err(ProgError(
                    "PROG " + key.name +
                        ": the file has no CUA but the system has a GUI status / title set; "
                        "removing it is not supported (abapGit does not remove it either) — "
                        "delete it in SE41",
                    ErrorCategory::Internal));
            }
        }
        return Result<void, Error>::Ok();
    }

    // ADT phase: the source of an existing program (lock + PUT + unlock, as
    // SE38 / Eclipse). Write run: everything else in one prog_put, saved
    // inactive; then REPS / DYNPnnnn / CUAD / REPT are activated with the
    // other programs of the deploy (ADT activation of a program does not
    // activate CUAD / REPT).
    Result<DeployPlan, Error> PlanMaster(const AbapGitObjectKey& key, const ObjectFileSet& desired,
                                         const ObjectState& current,
                                         const TypeContext& ctx) const {
        using R = Result<DeployPlan, Error>;
        auto valid = Validate(key, desired, current, ctx.language);
        if (valid.IsErr()) {
            return R::Err(valid.Error());
        }
        const std::string xml_name = FileName(key, "xml");
        const std::string abap_name = FileName(key, "abap");
        auto want = SplitProgXml(desired.at(xml_name), xml_name);
        if (want.IsErr()) {
            return R::Err(want.Error());
        }

        ProgParts have;
        auto cur_xml = current.files.find(xml_name);
        if (cur_xml != current.files.end()) {
            auto split = SplitProgXml(cur_xml->second, xml_name + " (system)");
            if (split.IsErr()) {
                return R::Err(split.Error());
            }
            have = split.Value();
        }
        const auto& w = want.Value();
        const bool create = !current.exists;

        const bool source_changed =
            FileOrEmpty(desired, abap_name) != FileOrEmpty(current.files, abap_name);
        const bool progdir_changed = w.progdir != have.progdir;
        std::vector<std::string> screens_to_write;
        for (const auto& [screen, item] : w.screens) {
            auto it = have.screens.find(screen);
            if (it == have.screens.end() || it->second != item ||
                FileOrEmpty(desired, FlowFileName(key, screen)) !=
                    FileOrEmpty(current.files, FlowFileName(key, screen))) {
                screens_to_write.push_back(screen);
            }
        }
        std::vector<std::string> screens_to_delete;
        for (const auto& [screen, item] : have.screens) {
            if (w.screens.count(screen) == 0) {
                screens_to_delete.push_back(screen);
            }
        }
        const bool cua_changed = w.cua != have.cua;  // removal: refused by Validate
        std::vector<std::string> varis_to_write;
        for (const auto& [variant, item] : w.varis) {
            auto it = have.varis.find(variant);
            if (create || it == have.varis.end() || it->second != item) {
                varis_to_write.push_back(variant);
            }
        }
        std::vector<std::string> varis_to_delete;
        for (const auto& [variant, item] : have.varis) {
            if (w.varis.count(variant) == 0) {
                varis_to_delete.push_back(variant);
            }
        }
        bool tpool_changed = w.tpool != have.tpool;

        // Parts with an inactive version (a previous deploy failed to
        // activate): rewrite them from the files so nothing stays inactive.
        bool source_pending = false;
        bool cua_pending = false;
        for (const auto& part : current.pending) {
            if (part == "REPS") {
                source_pending = true;
            } else if (part == "CUAD") {
                cua_pending = !w.cua.empty();
            } else if (part == "REPT") {
                tpool_changed = true;
            } else if (part.rfind("DYNP", 0) == 0 && part.size() == 8) {
                const std::string screen = part.substr(4);
                if (w.screens.count(screen) != 0) {
                    if (std::find(screens_to_write.begin(), screens_to_write.end(), screen) ==
                        screens_to_write.end()) {
                        screens_to_write.push_back(screen);
                    }
                } else if (std::find(screens_to_delete.begin(), screens_to_delete.end(),
                                     screen) == screens_to_delete.end()) {
                    screens_to_delete.push_back(screen);
                }
            }
        }
        std::sort(screens_to_write.begin(), screens_to_write.end());

        DeployPlan plan;
        // Source of an existing program: over ADT, before the write run, so
        // the inactive version exists when UPDATE_PROGDIR runs.
        const bool write_source = !create && (source_changed || progdir_changed || source_pending);
        if (write_source) {
            const std::string name = key.name;
            const std::string source = SourceForAdt(FileOrEmpty(desired, abap_name));
            plan.adt = [name, source](TypeContext& adt_ctx) -> PartsResult {
                auto written = WriteSourceWithAutoLock(
                    adt_ctx.session, ProgramSourceUri(name), source,
                    adt_ctx.transport.empty() ? std::nullopt
                                              : std::optional<std::string>(adt_ctx.transport));
                if (written.IsErr()) {
                    auto err = written.Error();
                    err.message = "Writing the source of program " + name + ": " + err.message;
                    return PartsResult::Err(err);
                }
                return PartsResult::Ok({});
            };
        }

        PutFlags flags;
        flags.create = create;
        // After an ADT source write the attributes are re-applied to the new
        // inactive version too: ADT may rebuild it with default attributes.
        flags.write_progdir = create || progdir_changed || write_source;
        flags.write_cua = (create && !w.cua.empty()) || (!create && (cua_changed || cua_pending));
        flags.write_tpool = create || tpool_changed;
        if (create) {
            screens_to_write.clear();
            for (const auto& [screen, item] : w.screens) {
                screens_to_write.push_back(screen);
            }
        }

        std::vector<std::string>& parts = plan.parts;
        auto add_object = [&plan](const std::string& object, const std::string& obj_name) {
            plan.activate.push_back({"", object, obj_name});
        };
        if (create || source_changed || source_pending) {
            parts.push_back("source");
        }
        if (create || progdir_changed) {
            parts.push_back("PROGDIR");
        }
        if (create || write_source) {
            add_object("REPS", key.name);
        }
        for (const auto& screen : screens_to_write) {
            parts.push_back("screen " + screen);
            add_object("DYNP", DynproWorkingName(key.name, screen));
        }
        for (const auto& screen : screens_to_delete) {
            parts.push_back("screen " + screen + " (deleted)");
        }
        if (flags.write_cua) {
            parts.push_back("CUA");
            add_object("CUAD", key.name);
        }
        for (const auto& variant : varis_to_write) {
            parts.push_back("variant " + variant);
        }
        for (const auto& variant : varis_to_delete) {
            parts.push_back("variant " + variant + " (deleted)");
        }
        if (flags.write_tpool) {
            if (!(create && w.tpool.empty())) {
                parts.push_back("TPOOL");
            }
            add_object("REPT", key.name);
        }
        if (!varis_to_write.empty() || !varis_to_delete.empty()) {
            std::string values = "<VARIS>";
            for (const auto& variant : varis_to_write) {
                values += w.varis.at(variant);
            }
            values += "</VARIS><DELETE_VARIS>";
            for (const auto& variant : varis_to_delete) {
                values += "<item>" + EscapeXmlText(variant) + "</item>";
            }
            values += "</DELETE_VARIS>";
            PlannedOp put;
            put.op = ProgPutVarisOp(key.name, ctx, WrapAsxValues(values));
            put.what = "Writing the variants of program " + key.name;
            plan.after_activation.push_back(std::move(put));
        }

        const bool engine_write = create || flags.write_progdir || !screens_to_write.empty() ||
                                  !screens_to_delete.empty() || flags.write_cua ||
                                  flags.write_tpool;
        if (engine_write) {
            std::string values = w.progdir;
            if (!screens_to_write.empty()) {
                values += "<DYNPROS>";
                for (const auto& screen : screens_to_write) {
                    values += w.screens.at(screen);
                }
                values += "</DYNPROS><FLOWS>";
                for (const auto& screen : screens_to_write) {
                    values += "<item><SCREEN>" + screen + "</SCREEN><SOURCE>" +
                              EscapeXmlText(FileOrEmpty(desired, FlowFileName(key, screen))) +
                              "</SOURCE></item>";
                }
                values += "</FLOWS>";
            }
            if (!screens_to_delete.empty()) {
                values += "<DELETE_SCREENS>";
                for (const auto& screen : screens_to_delete) {
                    values += "<item>" + screen + "</item>";
                }
                values += "</DELETE_SCREENS>";
            }
            if (flags.write_cua) {
                values += w.cua;
            }
            if (flags.write_tpool) {
                values += w.tpool;
            }
            if (create) {
                values += "<SOURCE>" + EscapeXmlText(FileOrEmpty(desired, abap_name)) + "</SOURCE>";
            }
            PlannedOp put;
            put.op = ProgPutOp(key.name, ctx, flags, WrapAsxValues(values));
            put.what = (create ? "Creating program " : "Writing program ") + key.name;
            plan.writes.push_back(std::move(put));
        }
        return R::Ok(std::move(plan));
    }

public:
    Result<void, Error> Delete(const AbapGitObjectKey& key, TypeContext& ctx) const override {
        // abapGit deletes the documentation with the object.
        auto run = RunChecked(ctx.engine,
                              {ProgDeleteOp(key.name, ctx.package, ctx.transport),
                               I18nDocuDeleteOp("RE", key.name)},
                              "Deleting program " + key.name);
        if (run.IsErr()) {
            return Result<void, Error>::Err(run.Error());
        }
        return Result<void, Error>::Ok();
    }
};

}  // namespace

std::shared_ptr<const IClassicTypeHandler> MakeProgHandler() {
    return std::make_shared<ProgHandler>();
}

}  // namespace erpl_adt
