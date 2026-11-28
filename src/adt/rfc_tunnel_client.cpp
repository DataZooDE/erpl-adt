#include <erpl_adt/adt/rfc_tunnel_session.hpp>

#ifdef ERPL_ADT_WITH_RFC
#include <sapnwrfc.h>
#include <algorithm>
#include <charconv>
#include <limits>
#include <vector>

namespace erpl_adt {
namespace {
using Unicode = std::vector<SAP_UC>;
constexpr unsigned kMaxFieldBytes = 64U * 1024U * 1024U;

Error RfcError(const std::string& operation, const RFC_ERROR_INFO& detail) {
    Error error;
    error.operation = operation;
    error.endpoint = "SADT_REST_RFC_ENDPOINT";
    error.category = detail.code == RFC_LOGON_FAILURE ? ErrorCategory::Authentication
        : detail.code == RFC_TIMEOUT ? ErrorCategory::Timeout : ErrorCategory::Connection;
    unsigned length = 0;
    while (length < 512 && detail.message[length]) ++length;
    std::vector<uint8_t> bytes(length * 4 + 1);
    unsigned capacity = static_cast<unsigned>(bytes.size());
    unsigned written = 0;
    RFC_ERROR_INFO conversion{};
    if (RfcSAPUCToUTF8(detail.message, length, bytes.data(), &capacity, &written, &conversion) == RFC_OK) {
        error.message.assign(bytes.begin(), bytes.begin() + written);
    }
    if (error.message.empty()) error.message = "RFC operation failed (code " + std::to_string(detail.code) + ")";
    return error;
}
Error Invalid(const std::string& message) {
    Error error;
    error.operation = "RfcTunnel";
    error.endpoint = "SADT_REST_RFC_ENDPOINT";
    error.message = message;
    return error;
}
Result<Unicode, Error> Encode(std::string_view value) {
    if (value.size() >= kMaxFieldBytes) return Result<Unicode, Error>::Err(Invalid("RFC text field exceeds size limit"));
    Unicode buffer(value.size() + 1);
    unsigned capacity = static_cast<unsigned>(buffer.size());
    unsigned written = 0;
    RFC_ERROR_INFO error{};
    const auto* bytes = reinterpret_cast<const uint8_t*>(value.data());
    if (RfcUTF8ToSAPUC(bytes, static_cast<unsigned>(value.size()), buffer.data(),
                      &capacity, &written, &error) != RFC_OK) {
        return Result<Unicode, Error>::Err(RfcError("EncodeRfcText", error));
    }
    buffer.resize(written + 1);
    buffer[written] = 0;
    return Result<Unicode, Error>::Ok(std::move(buffer));
}
Unicode Name(std::string_view value) {
    Unicode result;
    for (const unsigned char c : value) result.push_back(c);
    result.push_back(0);
    return result;
}
Result<std::string, Error> Decode(const SAP_UC* value, unsigned length) {
    if (length > kMaxFieldBytes / 4) return Result<std::string, Error>::Err(Invalid("RFC text field exceeds size limit"));
    std::vector<uint8_t> bytes(length * 4 + 1);
    unsigned capacity = static_cast<unsigned>(bytes.size());
    unsigned written = 0;
    RFC_ERROR_INFO error{};
    if (RfcSAPUCToUTF8(value, length, bytes.data(), &capacity, &written, &error) != RFC_OK) {
        return Result<std::string, Error>::Err(RfcError("DecodeRfcText", error));
    }
    return Result<std::string, Error>::Ok(std::string(bytes.begin(), bytes.begin() + written));
}
Result<void, Error> SetText(DATA_CONTAINER_HANDLE container, const char* field, std::string_view value) {
    auto encoded = Encode(value);
    if (encoded.IsErr()) return Result<void, Error>::Err(std::move(encoded).Error());
    RFC_ERROR_INFO error{};
    const auto& text = encoded.Value();
    if (RfcSetString(container, Name(field).data(), text.data(),
                     static_cast<unsigned>(text.size() - 1), &error) != RFC_OK) {
        return Result<void, Error>::Err(RfcError("SetRfcField", error));
    }
    return Result<void, Error>::Ok();
}
Result<std::string, Error> GetText(DATA_CONTAINER_HANDLE container, const char* field) {
    unsigned length = 0;
    RFC_ERROR_INFO error{};
    const auto name = Name(field);
    if (RfcGetStringLength(container, name.data(), &length, &error) != RFC_OK) {
        return Result<std::string, Error>::Err(RfcError("ReadRfcField", error));
    }
    if (length > kMaxFieldBytes / 4) return Result<std::string, Error>::Err(Invalid("RFC text field exceeds size limit"));
    Unicode text(length + 1);
    unsigned written = 0;
    if (RfcGetString(container, name.data(), text.data(), static_cast<unsigned>(text.size()),
                     &written, &error) != RFC_OK) {
        return Result<std::string, Error>::Err(RfcError("ReadRfcField", error));
    }
    return Decode(text.data(), written);
}

// Handles below are opaque C ABI handles, wrapped immediately in RAII. Their
// child structures/tables are borrowed and live as long as the function.
class Function final {
public:
    explicit Function(RFC_FUNCTION_HANDLE handle) : handle_(handle) {}
    ~Function() { if (handle_) { RFC_ERROR_INFO error{}; (void)RfcDestroyFunction(handle_, &error); } }
    Function(const Function&) = delete;
    Function& operator=(const Function&) = delete;
    RFC_FUNCTION_HANDLE Get() const { return handle_; }
private:
    RFC_FUNCTION_HANDLE handle_;
};

class RfcTunnelClient final : public IRfcTunnelClient {
public:
    explicit RfcTunnelClient(std::map<std::string, std::string> parameters)
        : parameters_(std::move(parameters)) {}
    ~RfcTunnelClient() override { Close(); }
    std::string AuthenticatedUser() const override { return user_; }
    void Close() override {
        if (connection_) {
            RFC_ERROR_INFO error{};
            (void)RfcCloseConnection(connection_, &error);
            connection_ = nullptr;
        }
    }
    Result<void, Error> EnsureConnected() override {
        if (connection_) return Result<void, Error>::Ok();
        std::vector<Unicode> names;
        std::vector<Unicode> values;
        for (const auto& [name, value] : parameters_) {
            auto encoded_name = Encode(name);
            auto encoded_value = Encode(value);
            if (encoded_name.IsErr()) return Result<void, Error>::Err(std::move(encoded_name).Error());
            if (encoded_value.IsErr()) return Result<void, Error>::Err(std::move(encoded_value).Error());
            names.push_back(std::move(encoded_name).Value());
            values.push_back(std::move(encoded_value).Value());
        }
        std::vector<RFC_CONNECTION_PARAMETER> parameters;
        for (size_t i = 0; i < names.size(); ++i) parameters.push_back({names[i].data(), values[i].data()});
        RFC_ERROR_INFO error{};
        connection_ = RfcOpenConnection(parameters.data(), static_cast<unsigned>(parameters.size()), &error);
        if (!connection_) return Result<void, Error>::Err(RfcError("OpenRfcConnection", error));
        RFC_ATTRIBUTES attributes{};
        if (RfcGetConnectionAttributes(connection_, &attributes, &error) != RFC_OK) {
            Close();
            return Result<void, Error>::Err(RfcError("GetRfcConnectionAttributes", error));
        }
        unsigned length = 0;
        while (length < 13 && attributes.user[length]) ++length;
        auto user = Decode(attributes.user, length);
        if (user.IsErr()) { Close(); return Result<void, Error>::Err(std::move(user).Error()); }
        user_ = std::move(user).Value();
        if (user_.empty()) { Close(); return Result<void, Error>::Err(Invalid("RFC server did not report the authenticated user")); }
        return Result<void, Error>::Ok();
    }
    Result<HttpResponse, Error> Execute(std::string_view method, std::string_view path,
            std::string_view body, const HttpHeaders& headers) override {
        auto connected = EnsureConnected();
        if (connected.IsErr()) return Result<HttpResponse, Error>::Err(std::move(connected).Error());
        RFC_ERROR_INFO error{};
        auto description = RfcGetFunctionDesc(connection_, Name("SADT_REST_RFC_ENDPOINT").data(), &error);
        if (!description) return Result<HttpResponse, Error>::Err(RfcError("DescribeRfcTunnel", error));
        Function function(RfcCreateFunction(description, &error));
        if (!function.Get()) return Result<HttpResponse, Error>::Err(RfcError("CreateRfcTunnelCall", error));
        RFC_STRUCTURE_HANDLE request = nullptr;
        RFC_STRUCTURE_HANDLE line = nullptr;
        RFC_TABLE_HANDLE fields = nullptr;
        if (RfcGetStructure(function.Get(), Name("REQUEST").data(), &request, &error) != RFC_OK ||
            RfcGetStructure(request, Name("REQUEST_LINE").data(), &line, &error) != RFC_OK ||
            RfcGetTable(request, Name("HEADER_FIELDS").data(), &fields, &error) != RFC_OK) {
            return Result<HttpResponse, Error>::Err(RfcError("BuildRfcRequest", error));
        }
        for (const auto& item : {std::pair<const char*, std::string_view>{"METHOD", method},
                                {"URI", path}, {"VERSION", "HTTP/1.1"}}) {
            auto set = SetText(line, item.first, item.second);
            if (set.IsErr()) return Result<HttpResponse, Error>::Err(std::move(set).Error());
        }
        for (const auto& [name, value] : headers) {
            const auto row = RfcAppendNewRow(fields, &error);
            if (!row) return Result<HttpResponse, Error>::Err(RfcError("BuildRfcHeaders", error));
            auto set_name = SetText(row, "NAME", name);
            if (set_name.IsErr()) return Result<HttpResponse, Error>::Err(std::move(set_name).Error());
            auto set_value = SetText(row, "VALUE", value);
            if (set_value.IsErr()) return Result<HttpResponse, Error>::Err(std::move(set_value).Error());
        }
        if (body.size() > kMaxFieldBytes) return Result<HttpResponse, Error>::Err(Invalid("RFC body exceeds size limit"));
        if (RfcSetXString(request, Name("MESSAGE_BODY").data(),
                          reinterpret_cast<const SAP_RAW*>(body.data()),
                          static_cast<unsigned>(body.size()), &error) != RFC_OK) {
            return Result<HttpResponse, Error>::Err(RfcError("BuildRfcBody", error));
        }
        if (RfcInvoke(connection_, function.Get(), &error) != RFC_OK) {
            auto failure = RfcError("InvokeRfcTunnel", error);
            Close();
            return Result<HttpResponse, Error>::Err(std::move(failure));
        }
        RFC_STRUCTURE_HANDLE response = nullptr;
        if (RfcGetStructure(function.Get(), Name("RESPONSE").data(), &response, &error) != RFC_OK ||
            RfcGetStructure(response, Name("STATUS_LINE").data(), &line, &error) != RFC_OK ||
            RfcGetTable(response, Name("HEADER_FIELDS").data(), &fields, &error) != RFC_OK) {
            return Result<HttpResponse, Error>::Err(RfcError("ReadRfcResponse", error));
        }
        auto status = GetText(line, "STATUS_CODE");
        if (status.IsErr()) return Result<HttpResponse, Error>::Err(std::move(status).Error());
        HttpResponse result;
        const auto& raw_status = status.Value();
        const auto first = raw_status.find_first_not_of(" \t\r\n");
        const auto last = raw_status.find_last_not_of(" \t\r\n");
        const auto text = first == std::string::npos ? std::string_view{} :
            std::string_view(raw_status).substr(first, last - first + 1);
        const auto parsed = std::from_chars(text.data(), text.data() + text.size(), result.status_code);
        if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() ||
            result.status_code < 100 || result.status_code > 599) {
            return Result<HttpResponse, Error>::Err(Invalid("RFC tunnel returned an invalid HTTP status"));
        }
        unsigned rows = 0;
        if (RfcGetRowCount(fields, &rows, &error) != RFC_OK) return Result<HttpResponse, Error>::Err(RfcError("ReadRfcHeaders", error));
        if (rows > 10000) return Result<HttpResponse, Error>::Err(Invalid("RFC tunnel returned too many headers"));
        for (unsigned i = 0; i < rows; ++i) {
            if (RfcMoveTo(fields, i, &error) != RFC_OK) return Result<HttpResponse, Error>::Err(RfcError("ReadRfcHeaders", error));
            const auto row = RfcGetCurrentRow(fields, &error);
            if (!row) return Result<HttpResponse, Error>::Err(RfcError("ReadRfcHeaders", error));
            auto name = GetText(row, "NAME");
            auto value = GetText(row, "VALUE");
            if (name.IsErr()) return Result<HttpResponse, Error>::Err(std::move(name).Error());
            if (value.IsErr()) return Result<HttpResponse, Error>::Err(std::move(value).Error());
            result.headers[std::move(name).Value()] = std::move(value).Value();
        }
        unsigned length = 0;
        const auto body_name = Name("MESSAGE_BODY");
        if (RfcGetStringLength(response, body_name.data(), &length, &error) != RFC_OK) {
            return Result<HttpResponse, Error>::Err(RfcError("ReadRfcBody", error));
        }
        if (length > kMaxFieldBytes) return Result<HttpResponse, Error>::Err(Invalid("RFC response body exceeds size limit"));
        std::vector<SAP_RAW> bytes(length + 1);
        unsigned written = 0;
        if (RfcGetXString(response, body_name.data(), bytes.data(), length, &written, &error) != RFC_OK) {
            return Result<HttpResponse, Error>::Err(RfcError("ReadRfcBody", error));
        }
        result.body.assign(bytes.begin(), bytes.begin() + written);
        return Result<HttpResponse, Error>::Ok(std::move(result));
    }
private:
    const std::map<std::string, std::string> parameters_;
    RFC_CONNECTION_HANDLE connection_ = nullptr;
    std::string user_;
};
} // namespace
Result<std::unique_ptr<IRfcTunnelClient>, Error> CreateRfcTunnelClient(
        const std::map<std::string, std::string>& parameters) {
    auto client = std::make_unique<RfcTunnelClient>(parameters);
    auto connected = client->EnsureConnected();
    if (connected.IsErr()) return Result<std::unique_ptr<IRfcTunnelClient>, Error>::Err(std::move(connected).Error());
    return Result<std::unique_ptr<IRfcTunnelClient>, Error>::Ok(std::move(client));
}
} // namespace erpl_adt
#else
namespace erpl_adt {
Result<std::unique_ptr<IRfcTunnelClient>, Error> CreateRfcTunnelClient(
        const std::map<std::string, std::string>&) {
    Error error;
    error.operation = "OpenRfcConnection";
    error.category = ErrorCategory::Connection;
    error.message = "RFC tunnel support is not enabled in this build (ERPL_ADT_WITH_RFC)";
    return Result<std::unique_ptr<IRfcTunnelClient>, Error>::Err(std::move(error));
}
} // namespace erpl_adt
#endif
