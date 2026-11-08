#pragma once

#include <erpl_adt/adt/classic_engine.hpp>
#include <erpl_adt/adt/i_adt_session.hpp>
#include <erpl_adt/core/result.hpp>

#include <string>
#include <vector>

namespace erpl_adt {

// ---------------------------------------------------------------------------
// Temporary console helper classes.
//
// Classic GUI objects are only reachable through SAP function modules, which
// erpl-adt calls from a generated IF_OO_ADT_CLASSRUN class: create it, write
// and activate the source, run it via ADT classrun, delete it again.
//
// Enterprise rules baked in here:
//   * the helper lives in a local package ($TMP by default), never in the
//     customer's transportable package, so it never lands on a transport;
//   * a helper whose activation failed is never run — SAP would silently
//     execute the previous active version;
//   * the helper is deleted on success and failure (unless keep_helper).
// ---------------------------------------------------------------------------

struct ClassrunHelperOptions {
    std::string helper_class;          // e.g. ZCL_ERPLHLP_1A2B3C4D
    std::string package = "$TMP";      // must be a local ($...) package
    std::string description = "Temporary erpl-adt helper (safe to delete)";
    bool keep_helper = false;
};

// A fresh, practically unique helper class name: ZCL_ERPLHLP_<8 hex digits>.
std::string MakeHelperClassName();

// Ensures `options.helper_class` exists, writes `source`, activates it and
// runs it through `classrun_session` (a second, independent session — see
// screen_workflow.hpp for why). Returns the console output. Deletes the class
// again afterwards unless options.keep_helper is set.
[[nodiscard]] Result<std::string, Error> RunClassrunHelper(
    IAdtSession& session, IAdtSession& classrun_session,
    const ClassrunHelperOptions& options, const std::string& source);

// Runs engine operations (adt/classic_engine.hpp) and returns one result per
// operation. An interface so workflows take it by constructor injection and
// unit tests can substitute a hand-written fake.
class IEngineRunner {
public:
    virtual ~IEngineRunner() = default;
    [[nodiscard]] virtual Result<std::vector<EngineOpResult>, Error> Run(
        const std::vector<EngineOp>& ops) = 0;
};

// IEngineRunner backed by one temporary helper class. The class is created
// on the first Run, rewritten for each later Run, and deleted when the runner
// is destroyed (RAII) unless keep_helper is set.
class ClassrunEngineRunner final : public IEngineRunner {
public:
    ClassrunEngineRunner(IAdtSession& session, IAdtSession& classrun_session,
                         ClassrunHelperOptions options);
    ~ClassrunEngineRunner() override;
    ClassrunEngineRunner(const ClassrunEngineRunner&) = delete;
    ClassrunEngineRunner& operator=(const ClassrunEngineRunner&) = delete;

    [[nodiscard]] Result<std::vector<EngineOpResult>, Error> Run(
        const std::vector<EngineOp>& ops) override;

    const std::string& HelperClass() const { return options_.helper_class; }

private:
    IAdtSession& session_;
    IAdtSession& classrun_session_;
    ClassrunHelperOptions options_;
    bool created_ = false;
};

}  // namespace erpl_adt
