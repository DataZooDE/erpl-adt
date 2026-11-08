#pragma once

// Private to src/workflow: the per-type handlers behind IClassicTypeHandler.
// One translation unit per abapGit object type (abapgit_type_<type>.cpp).

#include <erpl_adt/adt/abapgit_xml.hpp>
#include <erpl_adt/workflow/abapgit_deploy.hpp>

#include <memory>
#include <string>
#include <vector>

namespace erpl_adt {

std::vector<std::shared_ptr<const IClassicTypeHandler>> MakeClassicTypeHandlers();

std::shared_ptr<const IClassicTypeHandler> MakeDomaHandler();
std::shared_ptr<const IClassicTypeHandler> MakeDtelHandler();
std::shared_ptr<const IClassicTypeHandler> MakeFugrHandler();

// --- shared helpers --------------------------------------------------------

// Error for a failed engine operation. SAP's own messages become the text;
// the category follows the evidence: lock messages -> LockConflict (exit 6),
// transport/CTS messages -> TransportError (exit 9), anything else the
// system rejected -> ActivationError (exit 5).
Error EngineFailure(const std::string& what, const EngineOpResult& result);

// Runs `ops` and fails on the first operation with rc != 0.
Result<std::vector<EngineOpResult>, Error> RunChecked(IEngineRunner& engine,
                                                      const std::vector<EngineOp>& ops,
                                                      const std::string& what);

// "<stem>.<part>", e.g. FileName({"DOMA","ZA"}, "xml") == "za.doma.xml".
std::string FileName(const AbapGitObjectKey& key, const std::string& part);

// The <asx:values> payload of one of the object's XML files, ready for the
// engine (compact asXML document).
Result<std::string, Error> EnginePayload(const ObjectFileSet& files, const std::string& file_name);

// Renders engine DATA (SAP asXML) as an abapGit file of `serializer`.
Result<std::string, Error> RenderFromEngine(const std::string& asxml,
                                            const std::string& serializer);

}  // namespace erpl_adt
