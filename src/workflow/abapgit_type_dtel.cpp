// DTEL — data elements (abapGit serializer LCL_OBJECT_DTEL).
// SAP APIs: DDIF_DTEL_GET / RS_CORR_INSERT + DDIF_DTEL_PUT + DDIF_DTEL_ACTIVATE
// / RS_DD_DELETE_OBJ, the same calls SE11 and abapGit make. The domain a
// data element refers to is deployed first (DeployRank DOMA < DTEL) and is
// activated right away by its own handler, so the data element activates.
// Translations (I18N_LANGS, DD04_TEXTS) and documentation (LONGTEXTS, id DE;
// LONGTEXTS_DZ, supplementary documentation DZ): abapgit_i18n.hpp.

#include "abapgit_i18n.hpp"
#include "abapgit_types.hpp"

namespace erpl_adt {

namespace {

EngineOp DtelGetOp(const std::string& name, const std::string& language) {
    return {"dtel_get( name = " + AbapQuote(name) + " language = " + AbapQuote(language) + " ).",
            ""};
}

EngineOp DtelPutOp(const std::string& name, const std::string& language,
                   const std::string& devclass, const std::string& trkorr,
                   const std::string& asx_xml) {
    return {"dtel_put( name = " + AbapQuote(name) + " language = " + AbapQuote(language) +
                " devclass = " + AbapQuote(devclass) + " trkorr = " + AbapQuote(trkorr) + " ).",
            asx_xml};
}

EngineOp DtelDeleteOp(const std::string& name, const std::string& trkorr) {
    return {"dtel_delete( name = " + AbapQuote(name) + " trkorr = " + AbapQuote(trkorr) + " ).",
            ""};
}

// Documentation sections of a data element (DE) and its supplements (DZ).
const std::vector<DocuSection> kDocu = {{"DE", "LONGTEXTS"}, {"DZ", "LONGTEXTS_DZ"}};

class DtelHandler final : public IClassicTypeHandler {
public:
    std::string Type() const override { return "DTEL"; }

    std::vector<EngineOp> ReadOps(const AbapGitObjectKey& key,
                                  const std::string& language) const override {
        return {DtelGetOp(key.name, language), I18nDtelGetOp(key.name, language),
                I18nDocuGetOp("DE", key.name, "LONGTEXTS"),
                I18nDocuGetOp("DZ", key.name, "LONGTEXTS_DZ")};
    }

    Result<ObjectState, Error> BuildState(const AbapGitObjectKey& key,
                                          const std::vector<EngineOpResult>& results,
                                          TypeContext& /*ctx*/) const override {
        const auto& r = results.at(0);
        if (r.rc != 0) {
            return Result<ObjectState, Error>::Err(
                EngineFailure("Reading data element " + key.name, r));
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
                                       "LCL_OBJECT_DTEL");
        if (rendered.IsErr()) {
            return Result<ObjectState, Error>::Err(rendered.Error());
        }
        const std::string xml_name = FileName(key, "xml");
        state.files[xml_name] = rendered.Value();
        // SAP records no last changer for translations (DD04T).
        for (const char* section : {"I18N_LANGS", "DD04_TEXTS"}) {
            state.part_changed_by[xml_name + "#" + section] = "";
        }
        for (const char* section : {"LONGTEXTS", "LONGTEXTS_DZ"}) {
            state.part_changed_by[xml_name + "#" + section] =
                i18n.changed_by.count(section) ? i18n.changed_by.at(section) : "";
        }
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
    // programs written later in the deploy find the data element active.
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
            plan.parts = {"DD04V"};
            PlannedOp put;
            put.op = DtelPutOp(key.name, ctx.language, ctx.package, ctx.transport,
                               SectionsPayload(want, [](const std::string& s) {
                                   return !IsI18nSection(s);
                               }));
            put.what = "Writing data element " + key.name;
            plan.ddic.push_back(std::move(put));
        }
        auto i18n = PlanI18n(
            key, want, have,
            [&](const std::string& texts) {
                return I18nDtelPutOp(key.name, ctx.language, ctx.package, ctx.transport, texts);
            },
            kDocu, "DTEL" + key.name, "DICT", ctx);
        plan.parts.insert(plan.parts.end(), i18n.parts.begin(), i18n.parts.end());
        for (auto& op : i18n.ops) {
            plan.ddic.push_back(std::move(op));
        }
        return Result<DeployPlan, Error>::Ok(std::move(plan));
    }

    Result<void, Error> Delete(const AbapGitObjectKey& key, TypeContext& ctx) const override {
        // abapGit deletes the documentation with the object.
        auto run = RunChecked(ctx.engine,
                              {DtelDeleteOp(key.name, ctx.transport),
                               I18nDocuDeleteOp("DE", key.name), I18nDocuDeleteOp("DZ", key.name)},
                              "Deleting data element " + key.name);
        if (run.IsErr()) {
            return Result<void, Error>::Err(run.Error());
        }
        return Result<void, Error>::Ok();
    }
};

}  // namespace

std::shared_ptr<const IClassicTypeHandler> MakeDtelHandler() {
    return std::make_shared<DtelHandler>();
}

}  // namespace erpl_adt
