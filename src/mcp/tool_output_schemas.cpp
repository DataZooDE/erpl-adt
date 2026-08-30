#include <erpl_adt/mcp/tool_metadata.hpp>

#include <nlohmann/json.hpp>

namespace erpl_adt {

namespace {

using nlohmann::json;

// Small builders in the spirit of MakeSchema/StringProp on the input side, so
// a schema below reads as a shape rather than as punctuation.
json Str(const std::string& description) {
    return {{"type", "string"}, {"description", description}};
}
json Int(const std::string& description) {
    return {{"type", "integer"}, {"description", description}};
}
json Num(const std::string& description) {
    return {{"type", "number"}, {"description", description}};
}
json Bool(const std::string& description) {
    return {{"type", "boolean"}, {"description", description}};
}
json Obj(json properties, json required = json::array()) {
    return {{"type", "object"},
            {"properties", std::move(properties)},
            {"required", std::move(required)}};
}
json ArrayOf(json items, const std::string& description) {
    return {{"type", "array"},
            {"description", description},
            {"items", std::move(items)}};
}

// -- adt_run_tests ----------------------------------------------------------
// The feedback loop an agent runs after every edit: it has to branch on
// whether anything failed and read the assertion out of the alert.
json RunTestsSchema() {
    const json alert = Obj({{"kind", Str("Alert kind, e.g. failedAssertion")},
                            {"severity", Str("Alert severity")},
                            {"title", Str("Short assertion message")},
                            {"detail", Str("Full assertion detail")}});
    const json method = Obj({{"name", Str("Test method name")},
                             {"execution_time_ms", Int("Runtime in milliseconds")},
                             {"passed", Bool("True when the method raised no alert")},
                             {"alerts", ArrayOf(alert, "Assertions that failed")}},
                            {"name", "passed"});
    const json test_class =
        Obj({{"name", Str("Test class name")},
             {"uri", Str("ADT URI of the test class")},
             {"skipped", Bool("True when the class ran no methods")},
             {"methods", ArrayOf(method, "Test methods in this class")},
             {"alerts", ArrayOf(alert, "Class-level alerts, e.g. a setup dump")}},
            {"name"});
    return Obj({{"total_methods", Int("Number of test methods executed")},
                {"total_failed", Int("Number of methods that failed")},
                {"total_skipped", Int("Number of methods skipped")},
                {"all_passed", Bool("True when nothing failed")},
                {"classes", ArrayOf(test_class, "Per-class results")}},
               {"total_methods", "total_failed", "all_passed", "classes"});
}

// -- adt_run_atc ------------------------------------------------------------
json RunAtcSchema() {
    const json finding = Obj({{"uri", Str("ADT URI the finding points at")},
                              {"message", Str("Finding message")},
                              {"priority", Int("ATC priority; 1 is most severe")},
                              {"check_title", Str("Name of the check that fired")},
                              {"message_title", Str("Short message title")}},
                             {"uri", "message", "priority"});
    return Obj({{"worklist_id", Str("ATC worklist this run created")},
                {"error_count", Int("Findings of error severity")},
                {"warning_count", Int("Findings of warning severity")},
                {"findings", ArrayOf(finding, "All findings from the run")}},
               {"error_count", "warning_count", "findings"});
}

// -- adt_check_syntax -------------------------------------------------------
// The result is the bare array of messages, so the schema is an array, not an
// object — JSON Schema 2020-12 and structuredContent both allow that.
json CheckSyntaxSchema() {
    const json message = Obj({{"type", Str("Message type, e.g. E for error")},
                              {"text", Str("Message text")},
                              {"uri", Str("ADT URI including the position")},
                              {"line", Int("1-based line number")},
                              {"offset", Int("Column offset within the line")}},
                             {"type", "text", "line"});
    return ArrayOf(message, "Syntax messages; empty when the object is clean");
}

// -- adt_read_table ---------------------------------------------------------
json ReadTableSchema() {
    const json field =
        Obj({{"name", Str("Field name")},
             {"type", Str("Data element or built-in type")},
             {"description", Str("Field description")},
             {"key_field", Bool("True when the field is part of the primary key")},
             {"length", Int("Field length, when the type has one")},
             {"decimals", Int("Decimal places, for numeric types")},
             {"abap_type", Str("ABAP type category")},
             {"check_table", Str("Foreign-key check table, when one exists")}},
            {"name", "type", "key_field"});
    return Obj({{"name", Str("Table name")},
                {"description", Str("Table description")},
                {"delivery_class", Str("SAP delivery class, e.g. A")},
                {"fields", ArrayOf(field, "Fields in table order")}},
               {"name", "fields"});
}

// -- catalog_search ---------------------------------------------------------
json CatalogEntitySchema(bool with_score) {
    json properties = {
        {"id", Str("Stable catalog entity id")},
        {"domain", Str("Catalog domain, e.g. abap or bw")},
        {"object_type", Str("Object type, e.g. ADSO")},
        {"object_subtype", Str("Object subtype, when the type has one")},
        {"technical_name", Str("Technical name in SAP")},
        {"display_name", Str("Human-readable name")},
        {"package_or_infoarea", Str("Owning package or InfoArea")},
        {"biz_definition", Str("Curated business definition")},
        {"biz_owner", Str("Curated business owner")},
        {"biz_lob", Str("Curated line of business")},
        {"biz_confidentiality", Str("Curated confidentiality classification")},
        {"biz_curated_by", Str("Who last curated this entity")},
        {"biz_curated_at", Str("When it was last curated, ISO 8601")},
        {"extracted_at", Str("When it was last extracted from SAP, ISO 8601")},
        {"changed_at", Str("Last change timestamp in SAP, ISO 8601")},
        {"source_table", Str("Source table the row came from")},
    };
    if (with_score) {
        properties["score"] = Num("Relevance score for this hit");
    }
    return Obj(std::move(properties), {"id", "technical_name"});
}

json CatalogSearchSchema() {
    return Obj(
        {{"hits", ArrayOf(CatalogEntitySchema(/*with_score=*/true), "Matching entities")},
         {"has_more", Bool("True when more results follow this page")},
         // Feed straight back as the `cursor` argument; null on the last page.
         {"next_cursor", {{"type", json::array({"integer", "null"})},
                          {"description", "Cursor for the next page, or null"}}}},
        {"hits", "has_more"});
}

// -- catalog_lineage --------------------------------------------------------
json CatalogLineageSchema() {
    const json edge = Obj({{"id", Str("Edge id")},
                           {"from_id", Str("Source entity id")},
                           {"to_id", Str("Target entity id")},
                           {"kind", Str("Relationship kind")},
                           {"resolution", Str("How the edge was resolved")},
                           {"field_mapping", Str("Field-level mapping, when known")},
                           {"detail", Str("Free-form detail")}},
                          {"from_id", "to_id", "kind"});
    return Obj({{"chain", ArrayOf(edge, "Lineage edges, nearest first")}},
               {"chain"});
}

}  // namespace

size_t ApplyToolOutputSchemas(ToolRegistry& registry) {
    // The six tools whose results a model must branch on rather than quote.
    // Everything else stays text + structuredContent without a declared
    // schema; adding one is cheap when a tool proves to need it.
    const std::vector<std::pair<std::string, json>> schemas = {
        {"adt_run_tests", RunTestsSchema()},
        {"adt_run_atc", RunAtcSchema()},
        {"adt_check_syntax", CheckSyntaxSchema()},
        {"adt_read_table", ReadTableSchema()},
        {"catalog_search", CatalogSearchSchema()},
        {"catalog_lineage", CatalogLineageSchema()},
    };

    size_t applied = 0;
    for (const auto& [name, schema] : schemas) {
        if (registry.SetOutputSchema(name, schema)) {
            ++applied;
        }
    }
    return applied;
}

}  // namespace erpl_adt
