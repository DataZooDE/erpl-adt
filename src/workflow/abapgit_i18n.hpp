#pragma once

// Private to src/workflow: translations and documentation in abapGit's
// in-XML format, shared by the DOMA, DTEL and PROG handlers.
//
// abapGit writes translations into the object's XML file unless the
// repository opts into LXE (.abapgit.xml USE_LXE + I18N_LANGUAGES, then
// *.i18n.<lang>.po files). The in-XML format is the default, so it is the
// one supported here:
//   DOMA  I18N_LANGS, DD01_TEXTS, DD07_TEXTS      LONGTEXTS (docu id DO)
//   DTEL  I18N_LANGS, DD04_TEXTS                  LONGTEXTS (DE), LONGTEXTS_DZ (DZ)
//   PROG  I18N_TPOOL                              LONGTEXTS (RE)
// The engine part is src/abap/engine_i18n.abap.
//
// Scope: a deploy manages exactly the languages its files name. Translations
// (and documentation languages) that exist only in the system are left alone
// and never compared - as abapGit leaves them alone - so an object whose
// files carry no translations deploys exactly as before.

#include <erpl_adt/adt/abapgit_xml.hpp>
#include <erpl_adt/adt/classic_engine.hpp>
#include <erpl_adt/workflow/abapgit_deploy.hpp>

#include <functional>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace erpl_adt {

// I18N_LANGS, DD01_TEXTS, DD07_TEXTS, DD04_TEXTS, I18N_TPOOL.
bool IsTranslationSection(const std::string& section);
// LONGTEXTS, LONGTEXTS_<id>.
bool IsLongtextSection(const std::string& section);
inline bool IsI18nSection(const std::string& section) {
    return IsTranslationSection(section) || IsLongtextSection(section);
}

using XmlSections = std::vector<std::pair<std::string, std::string>>;

// The top-level sections of an abapGit XML file; empty when it does not parse.
XmlSections FileSections(const std::string& file_content);

// Names of the sections (matching `pick`) whose content differs between the
// two files, in the order of `want` then those only in `have`.
using SectionFilter = std::function<bool(const std::string&)>;
std::vector<std::string> ChangedSections(const XmlSections& want, const XmlSections& have,
                                         const SectionFilter& pick);

// The sections matching `pick`, as an asXML document for the engine.
std::string SectionsPayload(const XmlSections& sections, const SectionFilter& pick);

// Every section that is not a translation or documentation section equal?
bool SameMasterSections(const XmlSections& want, const XmlSections& have);

// The engine's main asXML (DATA XML) plus the asXML of the translation /
// documentation reads, rendered as one abapGit file in abapGit's order (the
// extra sections are appended in the order given).
Result<std::string, Error> RenderWithI18n(const std::string& main_asxml,
                                          const std::vector<std::string>& extra_asxml,
                                          const std::string& serializer);

// The system state limited to what the files manage (see the header
// comment): per XML file, a translation section is kept only for the
// languages the file's same section names, a documentation section only for
// the languages of the file's documentation; sections the file does not have
// are dropped.
ObjectState ScopeTranslations(const ObjectFileSet& desired, ObjectState current);

// The files with the translation and documentation sections removed from
// every XML file (what the master-language logic of a handler works on).
ObjectFileSet WithoutI18nSections(const ObjectFileSet& files);

// --- engine operations (src/abap/engine_i18n.abap) -------------------------
EngineOp I18nDomaGetOp(const std::string& name, const std::string& language);
EngineOp I18nDomaPutOp(const std::string& name, const std::string& language,
                       const std::string& devclass, const std::string& trkorr,
                       const std::string& payload);
EngineOp I18nDtelGetOp(const std::string& name, const std::string& language);
EngineOp I18nDtelPutOp(const std::string& name, const std::string& language,
                       const std::string& devclass, const std::string& trkorr,
                       const std::string& payload);
EngineOp I18nTpoolGetOp(const std::string& name, const std::string& language);
EngineOp I18nTpoolPutOp(const std::string& name, const std::string& language,
                        const std::string& devclass, const std::string& trkorr,
                        const std::string& payload);
// Documentation `id` (DO, DE, DZ, RE) of `object`, read as `section`.
EngineOp I18nDocuGetOp(const std::string& id, const std::string& object,
                       const std::string& section);
// `corr_object` / `corr_class`: the object's RS_CORR_INSERT entry, e.g.
// "DTELZFOO"/"DICT" or "ZFOO"/"ABAP".
EngineOp I18nDocuPutOp(const std::string& section, const std::string& corr_object,
                       const std::string& corr_class, const TypeContext& ctx,
                       const std::string& payload);
EngineOp I18nDocuDeleteOp(const std::string& id, const std::string& object);

// One documentation section of an object type.
struct DocuSection {
    std::string id;       // DOKIL-ID
    std::string section;  // XML section name
};

// The documentation sections of `object`'s read results, starting at
// `results[first]`, merged into the rendered state: the asXML of each
// result (absent results - e.g. a test that scripts fewer ops - count as
// "none"), and the per-section last changers for drift protection.
struct I18nReads {
    std::vector<std::string> asxml;
    std::map<std::string, std::string> changed_by;  // section -> user ("" unknown)
};
I18nReads CollectI18nReads(const std::vector<EngineOpResult>& results, size_t first);

// Plans the translation and documentation writes of one XML file: the
// changed sections (for the report), and the engine operations. `put_texts`
// builds the type's translation write from a payload; `docu` lists the
// type's documentation sections and how they are recorded on the request.
struct I18nPlan {
    std::vector<std::string> parts;
    std::vector<PlannedOp> ops;
};
I18nPlan PlanI18n(const AbapGitObjectKey& key, const XmlSections& want, const XmlSections& have,
                  const std::function<EngineOp(const std::string& payload)>& put_texts,
                  const std::vector<DocuSection>& docu, const std::string& corr_object,
                  const std::string& corr_class, const TypeContext& ctx);

}  // namespace erpl_adt
