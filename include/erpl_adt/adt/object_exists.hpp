#pragma once

#include <erpl_adt/adt/i_adt_session.hpp>
#include <erpl_adt/core/result.hpp>

#include <string>

namespace erpl_adt {

// ---------------------------------------------------------------------------
// Does the object at `uri` exist?
//
// Several ADT endpoints answer HTTP 200 for a target that is not there:
// classrun returns "Object X of type CLAS does not exist." as its *output*,
// an ATC run returns an empty finding list, a test run returns "no test
// methods", and a transport release accepts the job. Read literally, each of
// those says the work succeeded — "no findings" is indistinguishable from
// "clean", which is the reading that matters when an agent runs ATC after
// every edit and a typo in the URI looks like a pass.
//
// SAP's own message would be a fragile thing to match on (it is translated),
// so the check is a plain GET: 200 means it is there, 404 means it is not.
// ---------------------------------------------------------------------------

// Err with a NotFound error when the object is absent, Ok when it is present.
// A transport error (or any status other than 200/404) is passed through, so a
// broken connection never reads as "the object is missing".
[[nodiscard]] Result<void, Error> EnsureObjectExists(IAdtSession& session,
                                                     const std::string& uri,
                                                     const std::string& operation,
                                                     const std::string& what);

}  // namespace erpl_adt
