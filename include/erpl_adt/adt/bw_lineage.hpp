#pragma once

#include <erpl_adt/adt/i_adt_session.hpp>
#include <erpl_adt/core/result.hpp>

#include <map>
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
    std::string id;
    std::string group_id;
    std::string group_description;
    std::string group_type;
    std::string source_field;
    std::string target_field;
    std::string rule_type;       // "StepDirect", "StepFormula", "StepConstant", etc.
    std::string formula;         // For StepFormula
    std::string constant;        // For StepConstant
    std::vector<std::string> source_fields;
    std::vector<std::string> target_fields;
    std::map<std::string, std::string> step_attributes;
};

struct BwTransformationDetail {
    std::string name;
    std::string description;
    std::string start_routine;
    std::string end_routine;
    std::string expert_routine;
    bool hana_runtime = false;
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
    std::string type;
    std::string source_name;
    std::string source_type;     // "RSDS", "ADSO"
    std::string target_name;
    std::string target_type;     // "ADSO", "DEST"
    std::string source_system;   // For RSDS sources
    std::string request_selection_mode;
    std::map<std::string, std::string> extraction_settings;
    std::map<std::string, std::string> execution_settings;
    std::map<std::string, std::string> runtime_properties;
    std::map<std::string, std::string> error_handling;
    std::map<std::string, std::string> dtp_execution;
    std::vector<std::string> semantic_group_fields;
    struct FilterSelection {
        std::string low;
        std::string high;
        std::string op;
        bool excluding = false;
    };
    struct FilterField {
        std::string name;
        std::string field;
        bool selected = false;
        std::string filter_selection;
        std::string selection_type;
        std::vector<FilterSelection> selections;
    };
    struct ProgramFlowNode {
        std::string id;
        std::string type;
        std::string name;
        std::string next;
    };
    std::vector<FilterField> filter_fields;
    std::vector<ProgramFlowNode> program_flow;
};

[[nodiscard]] Result<BwDtpDetail, Error> BwReadDtpDetail(
    IAdtSession& session,
    const std::string& name,
    const std::string& version = "a",
    const std::string& content_type = "");

} // namespace erpl_adt
