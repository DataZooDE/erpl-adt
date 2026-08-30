#include <erpl_adt/adt/bw_media_types.hpp>

#include <algorithm>
#include <cctype>

namespace erpl_adt {

namespace {

std::string ToLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return s;
}

}  // namespace

std::string BwDefaultMediaType(const std::string& tlogo) {
    const auto lower = ToLower(tlogo);
    if (lower == "adso")   return "application/vnd.sap.bw.modeling.adso-v1_2_0+xml";
    // InfoObject — confirmed live against A4H: the previously-hardcoded
    // "application/xml" 406s ("Your BW client is outdated... Back end
    // supports vnd.sap-bw-modeling.iobj-v2_1_0") — note the dash-form
    // "sap-bw-modeling" namespace, unlike every other BW media type here,
    // which uses dot-form "sap.bw.modeling".
    if (lower == "iobj")   return "application/vnd.sap-bw-modeling.iobj-v2_1_0+xml";
    if (lower == "hcpr")   return "application/vnd.sap.bw.modeling.hcpr-v1_2_0+xml";
    if (lower == "trfn")   return "application/vnd.sap.bw.modeling.trfn-v1_0_0+xml";
    if (lower == "dtpa")   return "application/vnd.sap.bw.modeling.dtpa-v1_0_0+xml";
    if (lower == "rsds")   return "application/vnd.sap.bw.modeling.rsds+xml";
    if (lower == "lsys")   return "application/vnd.sap.bw.modeling.lsys-v1_1_0+xml";
    if (lower == "query")  return "application/vnd.sap.bw.modeling.query-v1_10_0+xml";
    if (lower == "dest")   return "application/vnd.sap.bw.modeling.dest-v1_0_0+xml";
    if (lower == "fbp")    return "application/vnd.sap.bw.modeling.fbp-v1_0_0+xml";
    if (lower == "dmod")   return "application/vnd.sap.bw.modeling.dmod-v1_0_0+xml";
    if (lower == "trcs")   return "application/vnd.sap.bw.modeling.trcs-v1_0_0+xml";
    if (lower == "doca")   return "application/vnd.sap.bw.modeling.doca-v1_0_0+xml";
    if (lower == "segr")   return "application/vnd.sap.bw.modeling.segr-v1_0_0+xml";
    if (lower == "area")   return "application/vnd.sap.bw.modeling.area-v1_0_0+xml";
    if (lower == "ctrt")   return "application/vnd.sap.bw.modeling.ctrt-v1_0_0+xml";
    if (lower == "uomt")   return "application/vnd.sap.bw.modeling.uomt-v1_0_0+xml";
    if (lower == "thjt")   return "application/vnd.sap.bw.modeling.thjt-v1_0_0+xml";
    // Fallback: unversioned vendor type
    return "application/vnd.sap.bw.modeling." + lower + "+xml";
}

}  // namespace erpl_adt
