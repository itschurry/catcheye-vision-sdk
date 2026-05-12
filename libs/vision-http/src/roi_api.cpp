#include "catcheye/http/roi_api.hpp"

#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "catcheye/roi/roi_repository.hpp"
#include "catcheye/roi/roi_validation.hpp"

namespace catcheye::http {
namespace {

std::vector<std::string> validation_issue_messages(const catcheye::roi::ValidationResult& validation)
{
    std::vector<std::string> details;
    details.reserve(validation.issues.size());
    for (const auto& issue : validation.issues) {
        std::ostringstream oss;
        oss << "zone_index=" << issue.zone_index
            << ", point_index=" << issue.point_index
            << ", message=" << issue.message;
        details.push_back(oss.str());
    }
    return details;
}

const std::string& roi_config_path(const RoiApiConfig& config, RoiConfigKind kind)
{
    return kind == RoiConfigKind::Pallet ? config.pallet_roi_path : config.person_roi_path;
}

HttpResponse handle_get_roi(const RoiApiConfig& config, RoiConfigKind kind)
{
    const auto parse_result = catcheye::roi::RoiRepository::load_from_file(roi_config_path(config, kind));
    if (!parse_result.success) {
        return HttpResponse{500, "Internal Server Error", json_error_body("failed to load ROI config file", parse_result.errors)};
    }

    const auto validation = catcheye::roi::validate_camera_roi_config(parse_result.config);
    if (!validation.valid) {
        return HttpResponse{500, "Internal Server Error", json_error_body("ROI config file is invalid", validation_issue_messages(validation))};
    }

    return HttpResponse{200, "OK", catcheye::roi::RoiRepository::to_json_string(parse_result.config, 2)};
}

HttpResponse handle_put_roi(const RoiApiConfig& config, RoiConfigKind kind, const std::string& body)
{
    const auto parse_result = catcheye::roi::RoiRepository::from_json_string(body);
    if (!parse_result.success) {
        return HttpResponse{400, "Bad Request", json_error_body("failed to parse ROI JSON", parse_result.errors)};
    }

    const auto validation = catcheye::roi::validate_camera_roi_config(parse_result.config);
    if (!validation.valid) {
        return HttpResponse{400, "Bad Request", json_error_body("ROI config failed validation", validation_issue_messages(validation))};
    }

    if (!catcheye::roi::RoiRepository::save_to_file(parse_result.config, roi_config_path(config, kind))) {
        return HttpResponse{500, "Internal Server Error", json_error_body("failed to save ROI config file")};
    }

    if (!config.apply || !config.apply(kind, parse_result.config)) {
        return HttpResponse{500, "Internal Server Error", json_error_body("failed to apply ROI config in memory")};
    }

    return HttpResponse{200, "OK", catcheye::roi::RoiRepository::to_json_string(parse_result.config, 2)};
}

HttpResponse handle_roi_request(const RoiApiConfig& config, RoiConfigKind kind, const HttpRequest& request)
{
    if (request.method == "GET") {
        return handle_get_roi(config, kind);
    }
    if (request.method == "PUT") {
        return handle_put_roi(config, kind, request.body);
    }
    return HttpResponse{405, "Method Not Allowed", json_error_body("method not allowed")};
}

} // namespace

void register_roi_routes(HttpServer& server, RoiApiConfig config)
{
    server.add_route("/api/roi", [config](const HttpRequest& request) {
        return handle_roi_request(config, RoiConfigKind::Person, request);
    });
    server.add_route("/api/pallet-roi", [config = std::move(config)](const HttpRequest& request) {
        return handle_roi_request(config, RoiConfigKind::Pallet, request);
    });
}

} // namespace catcheye::http
