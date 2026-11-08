// PROG — executable programs and module pools with their screens (dynpros),
// GUI status / titles (CUA) and text pool. abapGit serializer LCL_OBJECT_PROG
// (ZCL_ABAPGIT_OBJECT_PROG / ZCL_ABAPGIT_OBJECTS_PROGRAM).
//
// Files:  <name>.prog.xml               PROGDIR, DYNPROS, CUA, TPOOL
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
//   * activation: RS_WORKING_OBJECTS_ACTIVATE in a separate engine run (ADT
//     activation of the program does not activate CUAD / REPT).

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

// `objects`: space separated REPS / DYNPnnnn / CUAD / REPT.
EngineOp ProgActivateOp(const std::string& name, const std::string& objects) {
    return {"prog_activate( name = " + AbapQuote(name) + " objects = " + AbapQuote(objects) +
                " ).",
            ""};
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
                if (item.find("<NAT_HEADER>") != std::string::npos ||
                    item.find("<NAT_FIELDS>") != std::string::npos) {
                    return R::Err(ProgError(
                        file_name + ": screen " + screen +
                            " is a native dynpro (NAT_HEADER/NAT_FIELDS, e.g. with a splitter "
                            "control) — not supported by erpl-adt",
                        ErrorCategory::Internal));
                }
                parts.screens[screen] = item;
            }
        } else if (element == "CUA") {
            parts.cua = xml;
        } else if (element == "TPOOL") {
            parts.tpool = xml;
        } else if (element == "VARIS") {
            return R::Err(ProgError(file_name +
                                        ": VARIS (system variants SAP&/CUS&) are not supported "
                                        "by erpl-adt",
                                    ErrorCategory::Internal));
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

class ProgHandler final : public IClassicTypeHandler {
public:
    std::string Type() const override { return "PROG"; }

    std::vector<EngineOp> ReadOps(const AbapGitObjectKey& key,
                                  const std::string& language) const override {
        return {ProgGetOp(key.name, language)};
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
        auto rendered = RenderFromEngine(xml->second, "LCL_OBJECT_PROG");
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
        return Result<ObjectState, Error>::Ok(state);
    }

    Result<void, Error> Validate(const AbapGitObjectKey& key, const ObjectFileSet& desired,
                                 const ObjectState& current,
                                 const std::string& /*language*/) const override {
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

    PartsResult Apply(const AbapGitObjectKey& key, const ObjectFileSet& desired,
                      const ObjectState& current, TypeContext& ctx) const override {
        auto valid = Validate(key, desired, current, ctx.language);
        if (valid.IsErr()) {
            return PartsResult::Err(valid.Error());
        }
        const std::string xml_name = FileName(key, "xml");
        const std::string abap_name = FileName(key, "abap");
        auto xml_it = desired.find(xml_name);
        if (xml_it == desired.end()) {
            return PartsResult::Err(ProgError("Missing file " + xml_name, ErrorCategory::Internal));
        }
        auto want = SplitProgXml(xml_it->second, xml_name);
        if (want.IsErr()) {
            return PartsResult::Err(want.Error());
        }
        if (desired.find(abap_name) == desired.end()) {
            return PartsResult::Err(
                ProgError("Missing file " + abap_name + " (program source)", ErrorCategory::Internal));
        }
        // Every flow logic file needs its screen and vice versa.
        const std::string flow_prefix = FileName(key, "screen_");
        for (const auto& [name, bytes] : desired) {
            if (name.rfind(flow_prefix, 0) == 0) {
                const std::string screen = name.substr(flow_prefix.size(), 4);
                if (want.Value().screens.count(screen) == 0) {
                    return PartsResult::Err(ProgError(
                        name + " has no screen " + screen + " in DYNPROS of " + xml_name,
                        ErrorCategory::Internal));
                }
            }
        }
        for (const auto& [screen, item] : want.Value().screens) {
            if (desired.find(FlowFileName(key, screen)) == desired.end()) {
                return PartsResult::Err(ProgError("Missing file " + FlowFileName(key, screen) +
                                                      " (flow logic of screen " + screen + ")",
                                                  ErrorCategory::Internal));
            }
        }

        ProgParts have;
        auto cur_xml = current.files.find(xml_name);
        if (cur_xml != current.files.end()) {
            auto split = SplitProgXml(cur_xml->second, xml_name + " (system)");
            if (split.IsErr()) {
                return PartsResult::Err(split.Error());
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
        const bool cua_changed = w.cua != have.cua;
        if (cua_changed && w.cua.empty()) {
            return PartsResult::Err(ProgError(
                "PROG " + key.name +
                    ": the file has no CUA but the system has a GUI status / title set; removing "
                    "it is not supported (abapGit does not remove it either) — delete it in SE41",
                ErrorCategory::Internal));
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

        // Source of an existing program: over ADT, before the engine run, so
        // the inactive version exists when UPDATE_PROGDIR runs.
        const bool write_source = !create && (source_changed || progdir_changed || source_pending);
        if (write_source) {
            auto written = WriteSourceWithAutoLock(
                ctx.session, ProgramSourceUri(key.name),
                SourceForAdt(FileOrEmpty(desired, abap_name)),
                ctx.transport.empty() ? std::nullopt : std::optional<std::string>(ctx.transport));
            if (written.IsErr()) {
                auto err = written.Error();
                err.message = "Writing the source of program " + key.name + ": " + err.message;
                return PartsResult::Err(err);
            }
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

        std::vector<std::string> parts;
        std::string activate;
        auto add_object = [&activate](const std::string& object) {
            activate += (activate.empty() ? "" : " ") + object;
        };
        if (create || source_changed || source_pending) {
            parts.push_back("source");
        }
        if (create || progdir_changed) {
            parts.push_back("PROGDIR");
        }
        if (create || write_source) {
            add_object("REPS");
        }
        for (const auto& screen : screens_to_write) {
            parts.push_back("screen " + screen);
            add_object("DYNP" + screen);
        }
        for (const auto& screen : screens_to_delete) {
            parts.push_back("screen " + screen + " (deleted)");
        }
        if (flags.write_cua) {
            parts.push_back("CUA");
            add_object("CUAD");
        }
        if (flags.write_tpool) {
            if (!(create && w.tpool.empty())) {
                parts.push_back("TPOOL");
            }
            add_object("REPT");
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
            auto run = RunChecked(ctx.engine, {ProgPutOp(key.name, ctx, flags, WrapAsxValues(values))},
                                  (create ? "Creating program " : "Writing program ") + key.name);
            if (run.IsErr()) {
                return PartsResult::Err(run.Error());
            }
        }

        // Activation in its own engine run: the writes above must be committed
        // before RS_WORKING_OBJECTS_ACTIVATE sees them.
        if (!activate.empty()) {
            auto run = RunChecked(ctx.engine, {ProgActivateOp(key.name, activate)},
                                  "Activating program " + key.name);
            if (run.IsErr()) {
                return PartsResult::Err(run.Error());
            }
        }
        return PartsResult::Ok(std::move(parts));
    }

    Result<void, Error> Delete(const AbapGitObjectKey& key, TypeContext& ctx) const override {
        auto run = RunChecked(ctx.engine, {ProgDeleteOp(key.name, ctx.package, ctx.transport)},
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
