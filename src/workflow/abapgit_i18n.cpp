#include "abapgit_i18n.hpp"

#include "abapgit_types.hpp"

#include <algorithm>
#include <set>

namespace erpl_adt {

namespace {

std::string Inner(const std::string& element) {
    auto open_end = element.find('>');
    auto close = element.rfind("</");
    if (open_end == std::string::npos || close == std::string::npos || close <= open_end) {
        return "";  // <X/> or malformed: no children
    }
    return element.substr(open_end + 1, close - open_end - 1);
}

std::string FirstText(const std::string& xml, const std::string& tag) {
    const std::string open = "<" + tag + ">";
    auto start = xml.find(open);
    if (start == std::string::npos) {
        return "";
    }
    start += open.size();
    auto end = xml.find("</" + tag + ">", start);
    return end == std::string::npos ? "" : xml.substr(start, end - start);
}

// The language an entry of a translation / documentation section is in.
std::string EntryLanguage(const std::string& section, const std::string& entry_name,
                          const std::string& entry) {
    if (section == "I18N_LANGS") {
        return entry_name == "LANGU" ? Inner(entry) : "";
    }
    if (section == "I18N_TPOOL") {
        return FirstText(entry, "LANGUAGE");
    }
    if (IsLongtextSection(section)) {
        return FirstText(FirstText(entry, "DOKIL"), "LANGU");
    }
    return FirstText(entry, "DDLANGUAGE");
}

std::set<std::string> SectionLanguages(const std::string& section, const std::string& xml) {
    std::set<std::string> out;
    for (const auto& [name, entry] : SplitAsxValues(Inner(xml))) {
        out.insert(EntryLanguage(section, name, entry));
    }
    return out;
}

// `xml` (one section) limited to entries in `languages`; "" when none is left.
std::string KeepLanguages(const std::string& section, const std::string& xml,
                          const std::set<std::string>& languages) {
    std::string kept;
    for (const auto& [name, entry] : SplitAsxValues(Inner(xml))) {
        if (languages.count(EntryLanguage(section, name, entry)) != 0) {
            kept += entry;
        }
    }
    return kept.empty() ? std::string() : "<" + section + ">" + kept + "</" + section + ">";
}

const std::string* Find(const XmlSections& sections, const std::string& name) {
    for (const auto& [section, xml] : sections) {
        if (section == name) {
            return &xml;
        }
    }
    return nullptr;
}

EngineOp Op(const std::string& call, const std::string& payload = "") { return {call, payload}; }

std::string Args(const std::string& name, const std::string& language) {
    return "name = " + AbapQuote(name) + " language = " + AbapQuote(language);
}

std::string WriteArgs(const std::string& name, const std::string& language,
                      const std::string& devclass, const std::string& trkorr) {
    return Args(name, language) + " devclass = " + AbapQuote(devclass) +
           " trkorr = " + AbapQuote(trkorr);
}

}  // namespace

bool IsTranslationSection(const std::string& section) {
    static const std::set<std::string> kSections = {"I18N_LANGS", "DD01_TEXTS", "DD07_TEXTS",
                                                    "DD04_TEXTS", "I18N_TPOOL"};
    return kSections.count(section) != 0;
}

bool IsLongtextSection(const std::string& section) {
    return section == "LONGTEXTS" || section.rfind("LONGTEXTS_", 0) == 0;
}

XmlSections FileSections(const std::string& file_content) {
    auto parsed = ParseAbapGitXml(file_content);
    if (parsed.IsErr()) {
        return {};
    }
    return SplitAsxValues(parsed.Value().values_xml);
}

std::vector<std::string> ChangedSections(const XmlSections& want, const XmlSections& have,
                                         const SectionFilter& pick) {
    std::vector<std::string> out;
    for (const auto& [section, xml] : want) {
        if (!pick(section)) {
            continue;
        }
        const auto* other = Find(have, section);
        if (other == nullptr || *other != xml) {
            out.push_back(section);
        }
    }
    for (const auto& [section, xml] : have) {
        if (pick(section) && Find(want, section) == nullptr) {
            out.push_back(section);
        }
    }
    return out;
}

std::string SectionsPayload(const XmlSections& sections, const SectionFilter& pick) {
    XmlSections picked;
    for (const auto& entry : sections) {
        if (pick(entry.first)) {
            picked.push_back(entry);
        }
    }
    return WrapAsxValues(JoinAsxValues(picked));
}

bool SameMasterSections(const XmlSections& want, const XmlSections& have) {
    return ChangedSections(want, have, [](const std::string& s) { return !IsI18nSection(s); })
        .empty();
}

Result<std::string, Error> RenderWithI18n(const std::string& main_asxml,
                                          const std::vector<std::string>& extra_asxml,
                                          const std::string& serializer) {
    auto values = ExtractAsxValues(main_asxml);
    if (values.IsErr()) {
        return values;
    }
    std::string all = values.Value();
    for (const auto& extra : extra_asxml) {
        if (extra.empty()) {
            continue;
        }
        auto more = ExtractAsxValues(extra);
        if (more.IsErr()) {
            return more;
        }
        all += more.Value();
    }
    AbapGitXml xml;
    xml.serializer = serializer;
    xml.serializer_version = "v1.0.0";
    xml.values_xml = all;
    return Result<std::string, Error>::Ok(RenderAbapGitXml(xml));
}

ObjectState ScopeTranslations(const ObjectFileSet& desired, ObjectState current) {
    for (auto& [name, bytes] : current.files) {
        if (name.size() < 4 || name.compare(name.size() - 4, 4, ".xml") != 0) {
            continue;
        }
        auto parsed = ParseAbapGitXml(bytes);
        if (parsed.IsErr()) {
            continue;
        }
        const auto have = SplitAsxValues(parsed.Value().values_xml);
        const bool any = std::any_of(have.begin(), have.end(),
                                     [](const auto& s) { return IsI18nSection(s.first); });
        if (!any) {
            continue;
        }
        XmlSections want;
        auto wanted = desired.find(name);
        if (wanted != desired.end()) {
            want = FileSections(wanted->second);
        }
        XmlSections scoped;
        for (const auto& [section, xml] : have) {
            if (!IsI18nSection(section)) {
                scoped.emplace_back(section, xml);
                continue;
            }
            const auto* mine = Find(want, section);
            if (mine == nullptr) {
                continue;  // the files do not manage this section
            }
            auto kept = KeepLanguages(section, xml, SectionLanguages(section, *mine));
            if (!kept.empty()) {
                scoped.emplace_back(section, kept);
            }
        }
        AbapGitXml xml = parsed.Value();
        xml.values_xml = JoinAsxValues(scoped);
        bytes = RenderAbapGitXml(xml);
    }
    return current;
}

std::vector<std::string> UnmanagedI18n(const ObjectFileSet& desired, const ObjectState& current) {
    std::vector<std::string> out;
    const ObjectState scoped = ScopeTranslations(desired, current);
    for (const auto& [name, bytes] : current.files) {
        auto kept = scoped.files.find(name);
        const auto have = FileSections(bytes);
        const auto mine = kept == scoped.files.end() ? XmlSections{} : FileSections(kept->second);
        for (const auto& [section, xml] : have) {
            if (!IsI18nSection(section) || section == "I18N_LANGS") {
                continue;  // I18N_LANGS only lists the languages of the texts
            }
            const auto* left = Find(mine, section);
            std::set<std::string> extra = SectionLanguages(section, xml);
            if (left != nullptr) {
                for (const auto& l : SectionLanguages(section, *left)) {
                    extra.erase(l);
                }
            }
            if (extra.empty()) {
                continue;
            }
            std::string langs;
            for (const auto& l : extra) {
                langs += (langs.empty() ? "" : ", ") + (l.empty() ? std::string("?") : l);
            }
            out.push_back(name + " " + section + " (" + langs + ")");
        }
    }
    return out;
}

ObjectFileSet WithoutI18nSections(const ObjectFileSet& files) {
    ObjectFileSet out = files;
    for (auto& [name, bytes] : out) {
        if (name.size() < 4 || name.compare(name.size() - 4, 4, ".xml") != 0) {
            continue;
        }
        auto parsed = ParseAbapGitXml(bytes);
        if (parsed.IsErr()) {
            continue;
        }
        XmlSections kept;
        bool dropped = false;
        for (auto& section : SplitAsxValues(parsed.Value().values_xml)) {
            if (IsI18nSection(section.first)) {
                dropped = true;
            } else {
                kept.push_back(std::move(section));
            }
        }
        if (dropped) {
            AbapGitXml xml = parsed.Value();
            xml.values_xml = JoinAsxValues(kept);
            bytes = RenderAbapGitXml(xml);
        }
    }
    return out;
}

EngineOp I18nDomaGetOp(const std::string& name, const std::string& language) {
    return Op("i18n_doma_get( " + Args(name, language) + " ).");
}

EngineOp I18nDomaPutOp(const std::string& name, const std::string& language,
                       const std::string& devclass, const std::string& trkorr,
                       const std::string& payload) {
    return Op("i18n_doma_put( " + WriteArgs(name, language, devclass, trkorr) + " ).", payload);
}

EngineOp I18nDtelGetOp(const std::string& name, const std::string& language) {
    return Op("i18n_dtel_get( " + Args(name, language) + " ).");
}

EngineOp I18nDtelPutOp(const std::string& name, const std::string& language,
                       const std::string& devclass, const std::string& trkorr,
                       const std::string& payload) {
    return Op("i18n_dtel_put( " + WriteArgs(name, language, devclass, trkorr) + " ).", payload);
}

EngineOp I18nTpoolGetOp(const std::string& name, const std::string& language) {
    return Op("i18n_tpool_get( " + Args(name, language) + " ).");
}

EngineOp I18nTpoolPutOp(const std::string& name, const std::string& language,
                        const std::string& devclass, const std::string& trkorr,
                        const std::string& payload) {
    return Op("i18n_tpool_put( " + WriteArgs(name, language, devclass, trkorr) + " ).", payload);
}

EngineOp I18nDocuGetOp(const std::string& id, const std::string& object,
                       const std::string& section) {
    return Op("i18n_docu_get( id = " + AbapQuote(id) + " object = " + AbapQuote(object) +
              " section = " + AbapQuote(section) + " ).");
}

EngineOp I18nDocuPutOp(const std::string& section, const std::string& id,
                       const std::string& object, const std::string& corr_object,
                       const std::string& corr_class, const TypeContext& ctx,
                       const std::string& payload) {
    return Op("i18n_docu_put( section = " + AbapQuote(section) + " id = " + AbapQuote(id) +
                  " object = " + AbapQuote(object) + " corr_object = " + AbapQuote(corr_object) +
                  " corr_class = " + AbapQuote(corr_class) +
                  " devclass = " + AbapQuote(ctx.package) + " trkorr = " + AbapQuote(ctx.transport) +
                  " language = " + AbapQuote(ctx.language) + " ).",
              payload);
}

EngineOp I18nDocuDeleteOp(const std::string& id, const std::string& object) {
    return Op("i18n_docu_delete( id = " + AbapQuote(id) + " object = " + AbapQuote(object) +
              " ).");
}

Result<void, Error> CheckDocuIdentity(const AbapGitObjectKey& key, const ObjectFileSet& files,
                                      const std::string& file,
                                      const std::vector<DocuSection>& docu) {
    auto it = files.find(file);
    if (it == files.end()) {
        return Result<void, Error>::Ok();
    }
    const auto sections = FileSections(it->second);
    std::vector<std::string> problems;
    for (const auto& d : docu) {
        const auto* xml = Find(sections, d.section);
        if (xml == nullptr) {
            continue;
        }
        for (const auto& [unused, entry] : SplitAsxValues(Inner(*xml))) {
            const std::string dokil = FirstText(entry, "DOKIL");
            const std::string head = FirstText(entry, "HEAD");
            const std::string langu = FirstText(dokil, "LANGU");
            auto expect = [&](const std::string& what, const std::string& have,
                              const std::string& want, bool required) {
                if ((required || !have.empty()) && have != want) {
                    problems.push_back(d.section + " " + what + " " +
                                       (have.empty() ? std::string("(empty)") : have) +
                                       " (expected " + want + ")");
                }
            };
            expect("DOKIL-ID", FirstText(dokil, "ID"), d.id, true);
            expect("DOKIL-OBJECT", FirstText(dokil, "OBJECT"), key.name, true);
            expect("HEAD-TDOBJECT", FirstText(head, "TDOBJECT"), "DOKU", false);
            expect("HEAD-TDNAME", FirstText(head, "TDNAME"), key.name, false);
            expect("HEAD-TDID", FirstText(head, "TDID"), d.id, false);
            expect("HEAD-TDSPRAS", FirstText(head, "TDSPRAS"), langu, false);
            const std::string typ = FirstText(dokil, "TYP");
            if (typ != "E" && typ != "T") {
                problems.push_back(d.section + " DOKIL-TYP " +
                                   (typ.empty() ? std::string("(empty)") : typ) +
                                   " (DOCU_UPDATE writes only E and T)");
            }
        }
    }
    if (problems.empty()) {
        return Result<void, Error>::Ok();
    }
    Error err;
    err.operation = "abapGit";
    err.message = file + ": the documentation does not belong to " + key.type + " " + key.name +
                  " (copied from another object?); nothing was written: ";
    for (size_t i = 0; i < problems.size(); ++i) {
        err.message += (i ? "; " : "") + problems[i];
    }
    err.category = ErrorCategory::CheckError;
    return Result<void, Error>::Err(err);
}

Result<I18nReads, Error> CollectI18nReads(const AbapGitObjectKey& key,
                                          const std::vector<EngineOpResult>& results,
                                          size_t first) {
    I18nReads reads;
    for (size_t i = first; i < results.size(); ++i) {
        if (results[i].rc != 0) {
            return Result<I18nReads, Error>::Err(EngineFailure(
                "Reading the translations and documentation of " + key.type + " " + key.name,
                results[i]));
        }
        auto xml = results[i].data.find("XML");
        reads.asxml.push_back(xml == results[i].data.end() ? std::string() : xml->second);
        for (const auto& [part, user] : ChangedParts(results[i])) {
            reads.changed_by[part] = user;
        }
    }
    return Result<I18nReads, Error>::Ok(std::move(reads));
}

I18nPlan PlanI18n(const AbapGitObjectKey& key, const XmlSections& want, const XmlSections& have,
                  const std::function<EngineOp(const std::string& payload)>& put_texts,
                  const std::vector<DocuSection>& docu, const std::string& corr_object,
                  const std::string& corr_class, const TypeContext& ctx) {
    I18nPlan plan;
    const std::string what = key.type + " " + key.name;
    // Translations: only what the files carry is written (sections the
    // system has but the files lack were scoped away, see ScopeTranslations).
    auto texts = ChangedSections(want, have, IsTranslationSection);
    texts.erase(std::remove_if(texts.begin(), texts.end(),
                               [&](const std::string& s) { return Find(want, s) == nullptr; }),
                texts.end());
    if (!texts.empty()) {
        // I18N_LANGS only lists the languages; report the texts that changed.
        std::vector<std::string> shown;
        for (const auto& s : texts) {
            if (s != "I18N_LANGS") {
                shown.push_back(s);
            }
        }
        if (shown.empty()) {
            shown = texts;
        }
        plan.parts.insert(plan.parts.end(), shown.begin(), shown.end());
        PlannedOp op;
        op.op = put_texts(SectionsPayload(want, IsTranslationSection));
        op.what = "Writing the translations of " + what;
        plan.ops.push_back(std::move(op));
    }
    for (const auto& d : docu) {
        const auto* mine = Find(want, d.section);
        if (mine == nullptr) {
            continue;  // documentation the files do not carry is left alone
        }
        const auto* theirs = Find(have, d.section);
        if (theirs != nullptr && *theirs == *mine) {
            continue;
        }
        plan.parts.push_back(d.section);
        PlannedOp op;
        op.op = I18nDocuPutOp(d.section, d.id, key.name, corr_object, corr_class, ctx,
                              SectionsPayload(want, [&](const std::string& s) {
                                  return s == d.section;
                              }));
        op.what = "Writing the documentation of " + what;
        plan.ops.push_back(std::move(op));
    }
    return plan;
}

}  // namespace erpl_adt
