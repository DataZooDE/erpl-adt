// FUGR — function groups with function modules (abapGit serializer
// LCL_OBJECT_FUGR).
//
// Read (state / export): the engine mirrors abapGit's serializer
// (RS_FUNCTION_POOL_CONTENTS, RPY_FUNCTIONMODULE_READ_NEW, RS_GET_ALL_INCLUDES,
// READ_PROGDIR, READ REPORT), see src/abap/engine_fugr.abap.
//
// Write: through ADT, the way Eclipse creates and edits function groups.
// The classic route (RS_FUNCTIONMODULE_INSERT + INSERT REPORT +
// RS_WORKING_OBJECTS_ACTIVATE inside one helper run) was measured on the A4H
// trial and fails activation with "Function ... not found in Function
// Library". ADT instead takes the function module source in signature form
// and derives the interface (FUPARAREF, the `*"` comment block) from it:
//   group missing        -> POST /functions/groups            (CreateObject FUGR/F)
//   include missing      -> POST /functions/groups/{fg}/includes
//   module missing       -> POST /functions/groups/{fg}/fmodules
//   source differs       -> lock + PUT .../source/main + unlock
//   module not in files  -> lock + DELETE
//   then one activation request for everything written.
// Locks, transport recording (corrNr) and activation are SAP's own.

#include "abapgit_types.hpp"

#include <erpl_adt/adt/activation.hpp>
#include <erpl_adt/adt/function_group.hpp>
#include <erpl_adt/adt/locking.hpp>
#include <erpl_adt/adt/object.hpp>
#include <erpl_adt/adt/source.hpp>
#include <erpl_adt/workflow/abapgit_fugr.hpp>
#include <erpl_adt/workflow/lock_workflow.hpp>

#include <algorithm>
#include <map>
#include <set>

namespace erpl_adt {

namespace {

EngineOp FugrGetOp(const std::string& name, const std::string& language) {
    return {"fugr_get( name = " + AbapQuote(name) + " language = " + AbapQuote(language) + " ).",
            ""};
}

Error FugrError(const std::string& message, ErrorCategory category) {
    Error err;
    err.operation = "abapGit";
    err.message = message;
    err.category = category;
    return err;
}

std::string Upper(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return s;
}

std::string PartFile(const AbapGitObjectKey& key, const std::string& sub, const char* ext) {
    return FileName(key, FugrPartName(sub) + ext);
}

std::string Find(const ObjectFileSet& files, const std::string& name) {
    auto it = files.find(name);
    return it == files.end() ? std::string() : it->second;
}

std::optional<std::string> Transport(const TypeContext& ctx) {
    if (ctx.transport.empty()) {
        return std::nullopt;
    }
    return ctx.transport;
}

Result<ObjectUri, Error> MakeUri(const std::string& uri) {
    auto created = ObjectUri::Create(uri);
    if (created.IsErr()) {
        return Result<ObjectUri, Error>::Err(FugrError(created.Error(), ErrorCategory::Internal));
    }
    return Result<ObjectUri, Error>::Ok(std::move(created).Value());
}

// Lock, write source, unlock (LockGuard).
Result<void, Error> WriteObjectSource(TypeContext& ctx, const std::string& object_uri,
                                      const std::string& source) {
    auto uri = MakeUri(object_uri);
    if (uri.IsErr()) {
        return Result<void, Error>::Err(uri.Error());
    }
    auto guard = LockGuard::Acquire(ctx.session, uri.Value());
    if (guard.IsErr()) {
        return Result<void, Error>::Err(guard.Error());
    }
    // ADT expects CRLF-free text; SAP stores lines, so LF is fine.
    return WriteSource(ctx.session, object_uri + "/source/main", source,
                       guard.Value().Handle(), Transport(ctx));
}

Result<FugrDefinition, Error> ParseMain(const ObjectFileSet& files, const std::string& name) {
    auto it = files.find(name);
    if (it == files.end()) {
        return Result<FugrDefinition, Error>::Err(
            FugrError("Missing file " + name, ErrorCategory::Internal));
    }
    auto parsed = ParseAbapGitXml(it->second);
    if (parsed.IsErr()) {
        return Result<FugrDefinition, Error>::Err(parsed.Error());
    }
    return ParseFugrValues(parsed.Value().values_xml);
}

class FugrHandler final : public IClassicTypeHandler {
public:
    std::string Type() const override { return "FUGR"; }

    std::vector<EngineOp> ReadOps(const AbapGitObjectKey& key,
                                  const std::string& language) const override {
        return {FugrGetOp(key.name, language)};
    }

    Result<ObjectState, Error> BuildState(const AbapGitObjectKey& key,
                                          const std::vector<EngineOpResult>& results,
                                          TypeContext& /*ctx*/) const override {
        using R = Result<ObjectState, Error>;
        const auto& r = results.at(0);
        if (r.rc != 0) {
            return R::Err(EngineFailure("Reading function group " + key.name, r));
        }
        ObjectState state;
        state.exists = r.exists.value_or(false);
        if (!state.exists) {
            return R::Ok(state);
        }
        state.changed_by = r.changed_by;
        state.changed_on = r.changed_on;
        for (const auto& [tag, bytes] : r.data) {
            if (tag == "XML") {
                auto rendered = RenderFromEngine(bytes, "LCL_OBJECT_FUGR");
                if (rendered.IsErr()) {
                    return R::Err(rendered.Error());
                }
                state.files[FileName(key, "xml")] = rendered.Value();
            } else if (tag.rfind("FM:", 0) == 0) {
                state.files[PartFile(key, tag.substr(3), ".abap")] = bytes;
            } else if (tag.rfind("SRC:", 0) == 0) {
                state.files[PartFile(key, tag.substr(4), ".abap")] = bytes;
            } else if (tag.rfind("DIR:", 0) == 0) {
                auto values = ExtractAsxValues(bytes);
                if (values.IsErr()) {
                    return R::Err(values.Error());
                }
                AbapGitXml xml;  // include files carry no serializer attributes
                xml.values_xml = values.Value();
                state.files[PartFile(key, tag.substr(4), ".xml")] = RenderAbapGitXml(xml);
            }
        }
        // Last changers per include (REPOSRC-UNAM): an SE37 edit changes the
        // function module's include L<fg>U<nn>, not SAPL<fg>.
        const auto changed = ChangedParts(r);
        std::vector<std::string> fm_changers;
        for (const auto& [part, user] : changed) {
            if (part.rfind("FM:", 0) == 0) {
                state.part_changed_by[PartFile(key, part.substr(3), ".abap")] = user;
                fm_changers.push_back(user);
            } else if (part.rfind("SRC:", 0) == 0) {
                state.part_changed_by[PartFile(key, part.substr(4), ".abap")] = user;
                state.part_changed_by[PartFile(key, part.substr(4), ".xml")] = user;
            }
        }
        const std::string main_file = FileName(key, "xml");
        auto main = changed.find("MAIN");
        state.part_changed_by[main_file] = "";  // AREAT (TLIBT) has no changer
        state.part_changed_by[main_file + "#FUNCTIONS"] = CommonChanger(fm_changers);
        state.part_changed_by[main_file + "#INCLUDES"] =
            main == changed.end() ? std::string() : main->second;
        return R::Ok(state);
    }

    Result<std::vector<std::string>, Error> Apply(const AbapGitObjectKey& key,
                                                  const ObjectFileSet& desired,
                                                  const ObjectState& current,
                                                  TypeContext& ctx) const override {
        using R = Result<std::vector<std::string>, Error>;
        const std::string main_file = FileName(key, "xml");
        auto want = ParseMain(desired, main_file);
        if (want.IsErr()) {
            return R::Err(want.Error());
        }
        const auto& def = want.Value();

        FugrDefinition have;
        if (current.exists) {
            auto parsed = ParseMain(current.files, main_file);
            if (parsed.IsErr()) {
                return R::Err(parsed.Error());
            }
            have = parsed.Value();
        }

        // Refuse what this handler cannot write before touching anything.
        for (const auto& fm : def.functions) {
            if (!fm.remote_call.empty() || !fm.update_task.empty()) {
                return R::Err(FugrError(
                    "Function module " + fm.name +
                        ": RFC-enabled and update function modules are not supported by the "
                        "deploy yet (REMOTE_CALL/UPDATE_TASK set). Nothing was changed.",
                    ErrorCategory::ActivationError));
            }
            if (desired.count(PartFile(key, fm.name, ".abap")) == 0) {
                return R::Err(FugrError("Missing file " + PartFile(key, fm.name, ".abap"),
                                        ErrorCategory::Internal));
            }
        }
        if (current.exists && have.areat != def.areat) {
            return R::Err(FugrError(
                "Function group " + key.name + ": changing the short text ('" + have.areat +
                    "' -> '" + def.areat + "') is not supported by the deploy yet. "
                    "Nothing was changed.",
                ErrorCategory::ActivationError));
        }

        std::vector<std::string> parts;
        std::vector<ActivateObjectParams> activate;

        if (!current.exists) {
            CreateObjectParams params;
            params.object_type = "FUGR/F";
            params.name = key.name;
            params.package_name = ctx.package;
            params.description = def.areat;
            params.transport_number = Transport(ctx);
            auto created = CreateObject(ctx.session, params);
            if (created.IsErr()) {
                return R::Err(created.Error());
            }
            parts.push_back("function group");
        }

        // Includes (TOP, user includes). The main program SAPL<fg> is generated
        // by SAP and only written when it differs.
        const std::string main_program = def.includes.empty() ? "" : MainProgram(def, key.name);
        std::set<std::string> existing_includes(have.includes.begin(), have.includes.end());
        for (const auto& include : def.includes) {
            const auto file = PartFile(key, include, ".abap");
            const auto source = Find(desired, file);
            if (desired.count(file) == 0) {
                continue;
            }
            if (current.exists && Find(current.files, file) == source &&
                current.files.count(file) != 0) {
                continue;
            }
            std::string uri;
            if (include == main_program) {
                uri = FunctionGroupUri(key.name);
            } else {
                uri = FunctionGroupIncludeUri(key.name, include);
                const bool is_top = include.size() > 3 &&
                                    include.compare(include.size() - 3, 3, "TOP") == 0;
                if (existing_includes.count(include) == 0 && !is_top) {
                    auto created = CreateFunctionGroupInclude(ctx.session, key.name, include,
                                                              include, Transport(ctx));
                    if (created.IsErr()) {
                        return R::Err(created.Error());
                    }
                }
            }
            auto written = WriteObjectSource(ctx, uri, source);
            if (written.IsErr()) {
                return R::Err(written.Error());
            }
            activate.push_back(ActivationRef(uri, include == main_program ? "FUGR/F" : "FUGR/I",
                                             include == main_program ? key.name : include,
                                             include == main_program ? ""
                                                                     : FunctionGroupUri(key.name)));
            parts.push_back("include " + include);
        }

        // Function modules.
        std::map<std::string, const FmInterface*> have_fms;
        for (const auto& fm : have.functions) {
            have_fms[fm.name] = &fm;
        }
        for (const auto& fm : def.functions) {
            const auto file = PartFile(key, fm.name, ".abap");
            const auto source = Find(desired, file);
            auto it = have_fms.find(fm.name);
            if (it != have_fms.end() && it->second->item_xml == fm.item_xml &&
                Find(current.files, file) == source) {
                continue;
            }
            if (it == have_fms.end()) {
                auto created = CreateFunctionModule(ctx.session, key.name, fm.name, fm.short_text,
                                                    Transport(ctx));
                if (created.IsErr()) {
                    return R::Err(created.Error());
                }
            }
            auto signature = BuildFmSignatureSource(fm, source);
            if (signature.IsErr()) {
                return R::Err(signature.Error());
            }
            const auto uri = FunctionModuleUri(key.name, fm.name);
            auto written = WriteObjectSource(ctx, uri, signature.Value());
            if (written.IsErr()) {
                return R::Err(written.Error());
            }
            activate.push_back(ActivationRef(uri, "FUGR/FF", fm.name, FunctionGroupUri(key.name)));
            parts.push_back("function module " + fm.name);
        }

        // Function modules that are no longer in the files.
        for (const auto& fm : have.functions) {
            const bool wanted =
                std::any_of(def.functions.begin(), def.functions.end(),
                            [&fm](const FmInterface& d) { return d.name == fm.name; });
            if (wanted) {
                continue;
            }
            auto uri = MakeUri(FunctionModuleUri(key.name, fm.name));
            if (uri.IsErr()) {
                return R::Err(uri.Error());
            }
            auto deleted = DeleteObjectWithAutoLock(ctx.session, uri.Value(), Transport(ctx));
            if (deleted.IsErr()) {
                return R::Err(deleted.Error());
            }
            parts.push_back("deleted function module " + fm.name);
        }

        if (!activate.empty()) {
            auto activated = ActivateObjects(ctx.session, activate);
            if (activated.IsErr()) {
                return R::Err(activated.Error());
            }
            const auto& result = activated.Value();
            if (result.failed > 0) {
                std::string text;
                for (const auto& m : result.error_messages) {
                    text += (text.empty() ? "" : "; ") + m;
                }
                return R::Err(FugrError("Activation of function group " + key.name +
                                            " failed: " + text,
                                        ErrorCategory::ActivationError));
            }
        }

        // Include attributes (PROGDIR, text pool) are created by SAP; report
        // differences instead of silently claiming them deployed.
        for (const auto& [name, bytes] : desired) {
            const bool include_xml = name != main_file && name.size() > 4 &&
                                     name.compare(name.size() - 4, 4, ".xml") == 0;
            if (include_xml && current.exists && current.files.count(name) != 0 &&
                current.files.at(name) != bytes) {
                parts.push_back(name + " (include attributes differ; not deployable, unchanged)");
            }
        }
        return R::Ok(parts);
    }

    Result<void, Error> Delete(const AbapGitObjectKey& key, TypeContext& ctx) const override {
        auto uri = MakeUri(FunctionGroupUri(key.name));
        if (uri.IsErr()) {
            return Result<void, Error>::Err(uri.Error());
        }
        return DeleteObjectWithAutoLock(ctx.session, uri.Value(), Transport(ctx));
    }

private:
    static std::string MainProgram(const FugrDefinition& def, const std::string& group) {
        // SAPL<group>, or /NS/SAPL<group> for namespaced groups.
        const auto upper = Upper(group);
        std::string expected;
        if (!upper.empty() && upper[0] == '/') {
            const auto end = upper.find('/', 1);
            expected = upper.substr(0, end + 1) + "SAPL" + upper.substr(end + 1);
        } else {
            expected = "SAPL" + upper;
        }
        for (const auto& include : def.includes) {
            if (include == expected) {
                return include;
            }
        }
        return expected;
    }

    static ActivateObjectParams ActivationRef(const std::string& uri, const std::string& type,
                                              const std::string& name,
                                              const std::string& parent_uri) {
        ActivateObjectParams p;
        p.parent_uri = parent_uri;
        p.uri = uri;
        p.type = type;
        p.name = Upper(name);
        return p;
    }
};

}  // namespace

std::shared_ptr<const IClassicTypeHandler> MakeFugrHandler() {
    return std::make_shared<FugrHandler>();
}

}  // namespace erpl_adt
