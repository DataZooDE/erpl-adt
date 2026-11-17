// DOMA — domains with fixed values (abapGit serializer LCL_OBJECT_DOMA).
// SAP APIs: DDIF_DOMA_GET / RS_CORR_INSERT + DDIF_DOMA_PUT + DDIF_DOMA_ACTIVATE
// / RS_DD_DELETE_OBJ, the same calls SE11 and abapGit make. Translations
// (I18N_LANGS, DD01_TEXTS, DD07_TEXTS) and documentation (LONGTEXTS, id DO):
// abapgit_i18n.hpp.

#include "abapgit_i18n.hpp"
#include "abapgit_types.hpp"

namespace erpl_adt {

namespace {

// Engine operations of src/abap/engine_doma.abap. Arguments are quoted as
// ABAP literals; names have passed the customer-object check.
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


// Documentation sections of a domain (abapGit: LONGTEXTS, docu id DO).
const std::vector<DocuSection> kDocu = {{"DO", "LONGTEXTS"}};

class DomaHandler final : public IClassicTypeHandler {
public:
    std::string Type() const override { return "DOMA"; }

    std::vector<EngineOp> ReadOps(const AbapGitObjectKey& key,
                                  const std::string& language) const override {
        return {DomaGetOp(key.name, language), I18nDomaGetOp(key.name, language),
                I18nDocuGetOp("DO", key.name, "LONGTEXTS")};
    }

    Result<ObjectState, Error> BuildState(const AbapGitObjectKey& key,
                                          const std::vector<EngineOpResult>& results,
                                          TypeContext& /*ctx*/) const override {
        const auto& r = results.at(0);
        if (r.rc != 0) {
            return Result<ObjectState, Error>::Err(
                EngineFailure("Reading domain " + key.name, r));
        }
        ObjectState state;
        state.exists = r.exists.value_or(false);
        if (!state.exists) {
            return Result<ObjectState, Error>::Ok(state);
        }
        state.changed_by = r.changed_by;
        state.changed_on = r.changed_on;
        auto reads = CollectI18nReads(key, results, 1);
        if (reads.IsErr()) {
            return Result<ObjectState, Error>::Err(reads.Error());
        }
        const auto& i18n = reads.Value();
        auto rendered = RenderWithI18n(r.data.count("XML") ? r.data.at("XML") : "", i18n.asxml,
                                       "LCL_OBJECT_DOMA");
        if (rendered.IsErr()) {
            return Result<ObjectState, Error>::Err(rendered.Error());
        }
        const std::string xml_name = FileName(key, "xml");
        state.files[xml_name] = rendered.Value();
        // SAP records no last changer for translations (DD01T, DD07T).
        for (const char* section : {"I18N_LANGS", "DD01_TEXTS", "DD07_TEXTS"}) {
            state.part_changed_by[xml_name + "#" + section] = "";
        }
        state.part_changed_by[xml_name + "#LONGTEXTS"] =
            i18n.changed_by.count("LONGTEXTS") ? i18n.changed_by.at("LONGTEXTS") : "";
        return Result<ObjectState, Error>::Ok(state);
    }

    Result<void, Error> Validate(const AbapGitObjectKey& key, const ObjectFileSet& desired,
                                 const ObjectState& /*current*/,
                                 const std::string& /*language*/) const override {
        auto payload = EnginePayload(desired, FileName(key, "xml"));
        if (payload.IsErr()) {
            return Result<void, Error>::Err(payload.Error());
        }
        return CheckDocuIdentity(key, desired, FileName(key, "xml"), kDocu);
    }

    // DDIC phase: put + activate in one operation, so data elements and
    // programs written later in the deploy find the domain active.
    // Translations and documentation follow the master language in the same
    // phase, each only when it differs.
    Result<DeployPlan, Error> Plan(const AbapGitObjectKey& key, const ObjectFileSet& desired,
                                   const ObjectState& current,
                                   const TypeContext& ctx) const override {
        const std::string xml_name = FileName(key, "xml");
        auto payload = EnginePayload(desired, xml_name);
        if (payload.IsErr()) {
            return Result<DeployPlan, Error>::Err(payload.Error());
        }
        const auto want = FileSections(desired.at(xml_name));
        const auto have = current.files.count(xml_name) ? FileSections(current.files.at(xml_name))
                                                        : XmlSections{};
        DeployPlan plan;
        if (!current.exists || !SameMasterSections(want, have) || !current.pending.empty()) {
            plan.parts = {"DD01V", "DD07V_TAB"};
            PlannedOp put;
            put.op = DomaPutOp(key.name, ctx.language, ctx.package, ctx.transport,
                               SectionsPayload(want, [](const std::string& s) {
                                   return !IsI18nSection(s);
                               }));
            put.what = "Writing domain " + key.name;
            plan.ddic.push_back(std::move(put));
        }
        auto i18n = PlanI18n(
            key, want, have,
            [&](const std::string& texts) {
                return I18nDomaPutOp(key.name, ctx.language, ctx.package, ctx.transport, texts);
            },
            kDocu, "DOMA" + key.name, "DICT", ctx);
        plan.parts.insert(plan.parts.end(), i18n.parts.begin(), i18n.parts.end());
        for (auto& op : i18n.ops) {
            plan.ddic.push_back(std::move(op));
        }
        return Result<DeployPlan, Error>::Ok(std::move(plan));
    }

    Result<void, Error> Delete(const AbapGitObjectKey& key, TypeContext& ctx) const override {
        // abapGit deletes the documentation with the object.
        auto run = RunChecked(ctx.engine,
                              {DomaDeleteOp(key.name, ctx.transport),
                               I18nDocuDeleteOp("DO", key.name)},
                              "Deleting domain " + key.name);
        if (run.IsErr()) {
            return Result<void, Error>::Err(run.Error());
        }
        return Result<void, Error>::Ok();
    }
};

}  // namespace

std::shared_ptr<const IClassicTypeHandler> MakeDomaHandler() {
    return std::make_shared<DomaHandler>();
}

}  // namespace erpl_adt
