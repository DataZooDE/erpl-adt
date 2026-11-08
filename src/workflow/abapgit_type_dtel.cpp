// DTEL — data elements (abapGit serializer LCL_OBJECT_DTEL).
// SAP APIs: DDIF_DTEL_GET / RS_CORR_INSERT + DDIF_DTEL_PUT + DDIF_DTEL_ACTIVATE
// / RS_DD_DELETE_OBJ, the same calls SE11 and abapGit make. The domain a
// data element refers to is deployed first (DeployRank DOMA < DTEL) and is
// activated right away by its own handler, so the data element activates.

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

class DtelHandler final : public IClassicTypeHandler {
public:
    std::string Type() const override { return "DTEL"; }

    std::vector<EngineOp> ReadOps(const AbapGitObjectKey& key,
                                  const std::string& language) const override {
        return {DtelGetOp(key.name, language)};
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
        auto rendered = RenderFromEngine(r.data.count("XML") ? r.data.at("XML") : "",
                                         "LCL_OBJECT_DTEL");
        if (rendered.IsErr()) {
            return Result<ObjectState, Error>::Err(rendered.Error());
        }
        state.files[FileName(key, "xml")] = rendered.Value();
        return Result<ObjectState, Error>::Ok(state);
    }

    Result<std::vector<std::string>, Error> Apply(const AbapGitObjectKey& key,
                                                  const ObjectFileSet& desired,
                                                  const ObjectState& /*current*/,
                                                  TypeContext& ctx) const override {
        using R = Result<std::vector<std::string>, Error>;
        auto payload = EnginePayload(desired, FileName(key, "xml"));
        if (payload.IsErr()) {
            return R::Err(payload.Error());
        }
        auto run = RunChecked(
            ctx.engine,
            {DtelPutOp(key.name, ctx.language, ctx.package, ctx.transport, payload.Value())},
            "Writing data element " + key.name);
        if (run.IsErr()) {
            return R::Err(run.Error());
        }
        return R::Ok({"DD04V"});
    }

    Result<void, Error> Delete(const AbapGitObjectKey& key, TypeContext& ctx) const override {
        auto run = RunChecked(ctx.engine, {DtelDeleteOp(key.name, ctx.transport)},
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
