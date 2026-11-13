// DOMA — domains with fixed values (abapGit serializer LCL_OBJECT_DOMA).
// SAP APIs: DDIF_DOMA_GET / RS_CORR_INSERT + DDIF_DOMA_PUT + DDIF_DOMA_ACTIVATE
// / RS_DD_DELETE_OBJ, the same calls SE11 and abapGit make.

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


class DomaHandler final : public IClassicTypeHandler {
public:
    std::string Type() const override { return "DOMA"; }

    std::vector<EngineOp> ReadOps(const AbapGitObjectKey& key,
                                  const std::string& language) const override {
        return {DomaGetOp(key.name, language)};
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
        auto rendered = RenderFromEngine(r.data.count("XML") ? r.data.at("XML") : "",
                                         "LCL_OBJECT_DOMA");
        if (rendered.IsErr()) {
            return Result<ObjectState, Error>::Err(rendered.Error());
        }
        state.files[FileName(key, "xml")] = rendered.Value();
        return Result<ObjectState, Error>::Ok(state);
    }

    Result<void, Error> Validate(const AbapGitObjectKey& key, const ObjectFileSet& desired,
                                 const ObjectState& /*current*/,
                                 const std::string& /*language*/) const override {
        auto payload = EnginePayload(desired, FileName(key, "xml"));
        if (payload.IsErr()) {
            return Result<void, Error>::Err(payload.Error());
        }
        return Result<void, Error>::Ok();
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
            {DomaPutOp(key.name, ctx.language, ctx.package, ctx.transport, payload.Value())},
            "Writing domain " + key.name);
        if (run.IsErr()) {
            return R::Err(run.Error());
        }
        return R::Ok({"DD01V", "DD07V_TAB"});
    }

    Result<void, Error> Delete(const AbapGitObjectKey& key, TypeContext& ctx) const override {
        auto run = RunChecked(ctx.engine, {DomaDeleteOp(key.name, ctx.transport)},
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
