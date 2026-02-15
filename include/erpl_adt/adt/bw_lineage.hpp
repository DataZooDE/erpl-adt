#pragma once

#include <erpl_adt/adt/i_adt_session.hpp>
#include <erpl_adt/core/result.hpp>

#include <string>
#include <vector>

namespace erpl_adt {

// ---------------------------------------------------------------------------
// TRFN (Transformation) — field mapping rules for lineage.
// ---------------------------------------------------------------------------

struct BwTransformationField {
    std::string name;
    std::string type;            // intType attribute
    std::string aggregation;
    bool key = false;
};

struct BwTransformationRule {
    std::string source_field;
    std::string target_field;
    std::string rule_type;       // "StepDirect", "StepFormula", "StepConstant", etc.
    std::string formula;         // For StepFormula
    std::string constant;        // For StepConstant
};

struct BwTransformationDetail {
    std::string name;
    std::string description;
    std::string source_name;
    std::string source_type;
    std::string target_name;
    std::string target_type;
    std::vector<BwTransformationField> source_fields;
    std::vector<BwTransformationField> target_fields;
    std::vector<BwTransformationRule> rules;
};

[[nodiscard]] Result<BwTransformationDetail, Error> BwReadTransformation(
    IAdtSession& session,
    const std::string& name,
    const std::string& version = "a",
    const std::string& content_type = "");

// ---------------------------------------------------------------------------
// ADSO — field list for lineage.
// ---------------------------------------------------------------------------

struct BwAdsoField {
    std::string name;
    std::string description;
    std::string info_object;     // Referenced InfoObject
    std::string data_type;       // CHAR, DEC, CURR, etc.
    int length = 0;
    int decimals = 0;
    bool key = false;
};

struct BwAdsoDetail {
    std::string name;
    std::string description;
    std::string package_name;
    std::vector<BwAdsoField> fields;
};

[[nodiscard]] Result<BwAdsoDetail, Error> BwReadAdsoDetail(
    IAdtSession& session,
    const std::string& name,
    const std::string& version = "a",
    const std::string& content_type = "");

// ---------------------------------------------------------------------------
// DTP — source/target connections for lineage.
// ---------------------------------------------------------------------------

struct BwDtpDetail {
    std::string name;
    std::string description;
    std::string source_name;
    std::string source_type;     // "RSDS", "ADSO"
    std::string target_name;
    std::string target_type;     // "ADSO", "DEST"
    std::string source_system;   // For RSDS sources
};

[[nodiscard]] Result<BwDtpDetail, Error> BwReadDtpDetail(
    IAdtSession& session,
    const std::string& name,
    const std::string& version = "a",
    const std::string& content_type = "");

} // namespace erpl_adt
