#include <erpl_adt/adt/testing.hpp>
#include "adt_utils.hpp"

#include <tinyxml2.h>

#include <cstdlib>
#include <string>

namespace erpl_adt {

namespace {

std::string BuildTestRunXml(const std::string& uri) {
    return
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<aunit:runConfiguration xmlns:aunit=\"http://www.sap.com/adt/aunit\">\n"
        "  <external>\n"
        "    <coverage active=\"false\"/>\n"
        "  </external>\n"
        "  <options>\n"
        "    <uriType value=\"semantic\"/>\n"
        "    <testDeterminationStrategy sameProgram=\"true\" assignedTests=\"false\"/>\n"
        "    <testRiskLevels harmless=\"true\" dangerous=\"true\" critical=\"true\"/>\n"
        "    <testDurations short=\"true\" medium=\"true\" long=\"true\"/>\n"
        "    <withNavigationUri enabled=\"true\"/>\n"
        "  </options>\n"
        "  <adtcore:objectSets xmlns:adtcore=\"http://www.sap.com/adt/core\">\n"
        "    <objectSet kind=\"inclusive\">\n"
        "      <adtcore:objectReferences>\n"
        "        <adtcore:objectReference adtcore:uri=\"" + adt_utils::XmlEscape(uri) + "\"/>\n"
        "      </adtcore:objectReferences>\n"
        "    </objectSet>\n"
        "  </adtcore:objectSets>\n"
        "</aunit:runConfiguration>\n";
}

// Get attribute trying namespaced then plain name.
const char* GetAttr(const tinyxml2::XMLElement* el,
                    const char* ns_name, const char* plain_name = nullptr) {
    const char* val = el->Attribute(ns_name);
    if (!val && plain_name) val = el->Attribute(plain_name);
    return val ? val : "";
}

Result<TestRunResult, Error> ParseTestRunResult(
    std::string_view xml) {
    tinyxml2::XMLDocument doc;
    if (auto parse_error = adt_utils::ParseXmlOrError(
            doc, xml, "RunTests", "",
            "Failed to parse test run response XML",
            ErrorCategory::TestFailure)) {
        return Result<TestRunResult, Error>::Err(std::move(*parse_error));
    }

    TestRunResult result;
    auto* root = doc.RootElement();
    if (!root) {
        return Result<TestRunResult, Error>::Ok(std::move(result));
    }

    // Navigate: runResult > program > testClasses > testClass
    for (auto* program = root->FirstChildElement(); program;
         program = program->NextSiblingElement()) {
        auto* test_classes = program->FirstChildElement("testClasses");
        if (!test_classes) continue;

        for (auto* tc = test_classes->FirstChildElement("testClass"); tc;
             tc = tc->NextSiblingElement("testClass")) {
            TestClassResult cls;
            cls.name = GetAttr(tc, "adtcore:name", "name");
            cls.uri = GetAttr(tc, "adtcore:uri", "uri");
            cls.risk_level = GetAttr(tc, "riskLevel");
            cls.duration_category = GetAttr(tc, "durationCategory");

            auto* methods = tc->FirstChildElement("testMethods");
            if (!methods) continue;

            for (auto* tm = methods->FirstChildElement("testMethod"); tm;
                 tm = tm->NextSiblingElement("testMethod")) {
                TestMethodResult method;
                method.name = GetAttr(tm, "adtcore:name", "name");

                const char* exec_time = tm->Attribute("executionTime");
                if (exec_time) {
                    method.execution_time_ms = std::atoi(exec_time);
                }

                auto* alerts_el = tm->FirstChildElement("alerts");
                if (alerts_el) {
                    for (auto* alert = alerts_el->FirstChildElement("alert"); alert;
                         alert = alert->NextSiblingElement("alert")) {
                        TestAlert ta;
                        ta.kind = GetAttr(alert, "kind");
                        ta.severity = GetAttr(alert, "severity");

                        auto* title = alert->FirstChildElement("title");
                        if (title && title->GetText()) {
                            ta.title = title->GetText();
                        }

                        auto* details = alert->FirstChildElement("details");
                        if (details) {
                            auto* detail = details->FirstChildElement("detail");
                            if (detail) {
                                const char* text = detail->Attribute("text");
                                if (text) ta.detail = text;
                            }
                        }

                        method.alerts.push_back(std::move(ta));
                    }
                }

                cls.methods.push_back(std::move(method));
            }

            result.classes.push_back(std::move(cls));
        }
    }

    return Result<TestRunResult, Error>::Ok(std::move(result));
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// RunTests
// ---------------------------------------------------------------------------
Result<TestRunResult, Error> RunTests(
    IAdtSession& session,
    const std::string& uri) {
    auto body = BuildTestRunXml(uri);

    HttpHeaders headers;
    headers["Accept"] = "application/*";

    auto response = session.Post(
        "/sap/bc/adt/abapunit/testruns", body, "application/*", headers);
    if (response.IsErr()) {
        return Result<TestRunResult, Error>::Err(std::move(response).Error());
    }

    const auto& http = response.Value();
    if (http.status_code != 200) {
        return Result<TestRunResult, Error>::Err(
            Error::FromHttpStatus("RunTests", uri, http.status_code, http.body));
    }

    return ParseTestRunResult(http.body);
}

} // namespace erpl_adt
