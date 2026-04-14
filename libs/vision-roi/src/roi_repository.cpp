#include "catcheye/roi/roi_repository.hpp"

#include <cctype>
#include <cmath>
#include <fstream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <utility>
#include <variant>

namespace catcheye::roi {
namespace {

struct JsonValue;
using JsonObject = std::map<std::string, JsonValue>;
using JsonArray = std::vector<JsonValue>;

struct JsonValue {
    using Variant = std::variant<std::nullptr_t, bool, double, std::string, JsonArray, JsonObject>;
    Variant value;

    [[nodiscard]] bool is_object() const { return std::holds_alternative<JsonObject>(value); }
    [[nodiscard]] bool is_array() const { return std::holds_alternative<JsonArray>(value); }
    [[nodiscard]] bool is_string() const { return std::holds_alternative<std::string>(value); }
    [[nodiscard]] bool is_boolean() const { return std::holds_alternative<bool>(value); }
    [[nodiscard]] bool is_number() const { return std::holds_alternative<double>(value); }

    [[nodiscard]] const JsonObject& as_object() const { return std::get<JsonObject>(value); }
    [[nodiscard]] const JsonArray& as_array() const { return std::get<JsonArray>(value); }
    [[nodiscard]] const std::string& as_string() const { return std::get<std::string>(value); }
    [[nodiscard]] bool as_bool() const { return std::get<bool>(value); }
    [[nodiscard]] double as_number() const { return std::get<double>(value); }
};

class JsonParser {
public:
    explicit JsonParser(const std::string& text) : text_(text) {}

    JsonValue parse()
    {
        skip_ws();
        JsonValue value = parse_value();
        skip_ws();
        if (pos_ != text_.size()) {
            throw std::runtime_error("unexpected trailing characters");
        }
        return value;
    }

private:
    JsonValue parse_value()
    {
        if (pos_ >= text_.size()) {
            throw std::runtime_error("unexpected end of input");
        }

        const char ch = text_[pos_];
        if (ch == '{') return parse_object();
        if (ch == '[') return parse_array();
        if (ch == '"') return JsonValue {parse_string()};
        if (ch == 't') return parse_true();
        if (ch == 'f') return parse_false();
        if (ch == 'n') return parse_null();
        if ((ch == '-') || std::isdigit(static_cast<unsigned char>(ch))) return JsonValue {parse_number()};

        throw std::runtime_error("invalid json value");
    }

    JsonValue parse_object()
    {
        expect('{');
        skip_ws();
        JsonObject obj;
        if (peek('}')) {
            expect('}');
            return JsonValue {obj};
        }

        while (true) {
            skip_ws();
            std::string key = parse_string();
            skip_ws();
            expect(':');
            skip_ws();
            obj.emplace(std::move(key), parse_value());
            skip_ws();
            if (peek('}')) {
                expect('}');
                break;
            }
            expect(',');
            skip_ws();
        }

        return JsonValue {obj};
    }

    JsonValue parse_array()
    {
        expect('[');
        skip_ws();
        JsonArray arr;

        if (peek(']')) {
            expect(']');
            return JsonValue {arr};
        }

        while (true) {
            arr.push_back(parse_value());
            skip_ws();
            if (peek(']')) {
                expect(']');
                break;
            }
            expect(',');
            skip_ws();
        }

        return JsonValue {arr};
    }

    std::string parse_string()
    {
        expect('"');
        std::string out;
        while (pos_ < text_.size()) {
            char ch = text_[pos_++];
            if (ch == '"') {
                return out;
            }
            if (ch == '\\') {
                if (pos_ >= text_.size()) {
                    throw std::runtime_error("unterminated escape sequence");
                }
                const char esc = text_[pos_++];
                switch (esc) {
                    case '"': out.push_back('"'); break;
                    case '\\': out.push_back('\\'); break;
                    case '/': out.push_back('/'); break;
                    case 'b': out.push_back('\b'); break;
                    case 'f': out.push_back('\f'); break;
                    case 'n': out.push_back('\n'); break;
                    case 'r': out.push_back('\r'); break;
                    case 't': out.push_back('\t'); break;
                    default: throw std::runtime_error("unsupported escape sequence");
                }
                continue;
            }
            out.push_back(ch);
        }

        throw std::runtime_error("unterminated string");
    }

    double parse_number()
    {
        const std::size_t start = pos_;
        if (peek('-')) {
            ++pos_;
        }

        if (peek('0')) {
            ++pos_;
        } else {
            parse_digits();
        }

        if (peek('.')) {
            ++pos_;
            parse_digits();
        }

        if (peek('e') || peek('E')) {
            ++pos_;
            if (peek('+') || peek('-')) {
                ++pos_;
            }
            parse_digits();
        }

        return std::stod(text_.substr(start, pos_ - start));
    }

    JsonValue parse_true()
    {
        expect_sequence("true");
        return JsonValue {true};
    }

    JsonValue parse_false()
    {
        expect_sequence("false");
        return JsonValue {false};
    }

    JsonValue parse_null()
    {
        expect_sequence("null");
        return JsonValue {nullptr};
    }

    void parse_digits()
    {
        if (pos_ >= text_.size() || !std::isdigit(static_cast<unsigned char>(text_[pos_]))) {
            throw std::runtime_error("invalid number");
        }

        while (pos_ < text_.size() && std::isdigit(static_cast<unsigned char>(text_[pos_]))) {
            ++pos_;
        }
    }

    void skip_ws()
    {
        while (pos_ < text_.size() && std::isspace(static_cast<unsigned char>(text_[pos_]))) {
            ++pos_;
        }
    }

    bool peek(char expected) const
    {
        return pos_ < text_.size() && text_[pos_] == expected;
    }

    void expect(char expected)
    {
        if (!peek(expected)) {
            throw std::runtime_error(std::string("expected '") + expected + "'");
        }
        ++pos_;
    }

    void expect_sequence(const char* sequence)
    {
        while (*sequence != '\0') {
            if (pos_ >= text_.size() || text_[pos_] != *sequence) {
                throw std::runtime_error("unexpected token");
            }
            ++pos_;
            ++sequence;
        }
    }

    const std::string& text_;
    std::size_t pos_ {0};
};

const JsonValue* get_member(const JsonObject& obj, const std::string& key)
{
    const auto it = obj.find(key);
    if (it == obj.end()) {
        return nullptr;
    }
    return &it->second;
}

bool number_to_int(double in, int& out)
{
    const double truncated = std::trunc(in);
    if (std::fabs(in - truncated) > 1e-9) {
        return false;
    }

    out = static_cast<int>(truncated);
    return true;
}

std::string escape_string(const std::string& text)
{
    std::ostringstream oss;
    for (const char ch : text) {
        switch (ch) {
            case '"': oss << "\\\""; break;
            case '\\': oss << "\\\\"; break;
            case '\b': oss << "\\b"; break;
            case '\f': oss << "\\f"; break;
            case '\n': oss << "\\n"; break;
            case '\r': oss << "\\r"; break;
            case '\t': oss << "\\t"; break;
            default: oss << ch; break;
        }
    }
    return oss.str();
}

void append_indent(std::ostringstream& oss, int indent_level, int indent_size)
{
    for (int i = 0; i < indent_level * indent_size; ++i) {
        oss << ' ';
    }
}

RoiConfigParseResult parse_config_json(const JsonValue& root)
{
    RoiConfigParseResult result;
    if (!root.is_object()) {
        result.errors.push_back("root must be a JSON object");
        return result;
    }

    const auto& obj = root.as_object();
    const JsonValue* camera_id = get_member(obj, "camera_id");
    const JsonValue* image_width = get_member(obj, "image_width");
    const JsonValue* image_height = get_member(obj, "image_height");
    const JsonValue* allowed_zones = get_member(obj, "allowed_zones");

    if (camera_id == nullptr || !camera_id->is_string()) {
        result.errors.push_back("camera_id must be a string");
    } else {
        result.config.camera_id = camera_id->as_string();
    }

    if (image_width == nullptr || !image_width->is_number()) {
        result.errors.push_back("image_width must be a number");
    } else if (!number_to_int(image_width->as_number(), result.config.image_width)) {
        result.errors.push_back("image_width must be an integer");
    }

    if (image_height == nullptr || !image_height->is_number()) {
        result.errors.push_back("image_height must be a number");
    } else if (!number_to_int(image_height->as_number(), result.config.image_height)) {
        result.errors.push_back("image_height must be an integer");
    }

    if (allowed_zones == nullptr || !allowed_zones->is_array()) {
        result.errors.push_back("allowed_zones must be an array");
        return result;
    }

    const auto& zones = allowed_zones->as_array();
    for (std::size_t i = 0; i < zones.size(); ++i) {
        if (!zones[i].is_object()) {
            result.errors.push_back("allowed_zones[" + std::to_string(i) + "] must be an object");
            continue;
        }

        const auto& zone_obj = zones[i].as_object();
        const JsonValue* zone_id = get_member(zone_obj, "id");
        const JsonValue* zone_name = get_member(zone_obj, "name");
        const JsonValue* zone_enabled = get_member(zone_obj, "enabled");
        const JsonValue* zone_points = get_member(zone_obj, "points");

        if (zone_id == nullptr || !zone_id->is_string()) {
            result.errors.push_back("allowed_zones[" + std::to_string(i) + "].id must be a string");
            continue;
        }

        if (zone_name == nullptr || !zone_name->is_string()) {
            result.errors.push_back("allowed_zones[" + std::to_string(i) + "].name must be a string");
            continue;
        }

        if (zone_enabled == nullptr || !zone_enabled->is_boolean()) {
            result.errors.push_back("allowed_zones[" + std::to_string(i) + "].enabled must be a boolean");
            continue;
        }

        if (zone_points == nullptr || !zone_points->is_array()) {
            result.errors.push_back("allowed_zones[" + std::to_string(i) + "].points must be an array");
            continue;
        }

        RoiPolygon zone;
        zone.id = zone_id->as_string();
        zone.name = zone_name->as_string();
        zone.enabled = zone_enabled->as_bool();

        const auto& points = zone_points->as_array();
        for (std::size_t j = 0; j < points.size(); ++j) {
            if (!points[j].is_array()) {
                result.errors.push_back(
                    "allowed_zones[" + std::to_string(i) + "].points[" + std::to_string(j) + "] must be an array");
                continue;
            }

            const auto& point_values = points[j].as_array();
            if (point_values.size() != 2 || !point_values[0].is_number() || !point_values[1].is_number()) {
                result.errors.push_back(
                    "allowed_zones[" + std::to_string(i) + "].points[" + std::to_string(j) + "] must be [x, y]");
                continue;
            }

            zone.points.push_back(Point {point_values[0].as_number(), point_values[1].as_number()});
        }

        result.config.allowed_zones.push_back(std::move(zone));
    }

    result.success = result.errors.empty();
    return result;
}

void append_config_json(std::ostringstream& oss, const CameraRoiConfig& config, int indent, int level)
{
    const bool pretty = indent > 0;
    const auto newline = pretty ? "\n" : "";
    const auto separator = pretty ? " " : "";

    oss << "{" << newline;

    append_indent(oss, level + 1, indent);
    oss << "\"camera_id\":" << separator << "\"" << escape_string(config.camera_id) << "\"," << newline;

    append_indent(oss, level + 1, indent);
    oss << "\"image_width\":" << separator << config.image_width << "," << newline;

    append_indent(oss, level + 1, indent);
    oss << "\"image_height\":" << separator << config.image_height << "," << newline;

    append_indent(oss, level + 1, indent);
    oss << "\"allowed_zones\":" << separator << "[" << newline;

    for (std::size_t i = 0; i < config.allowed_zones.size(); ++i) {
        const auto& zone = config.allowed_zones[i];
        append_indent(oss, level + 2, indent);
        oss << "{" << newline;

        append_indent(oss, level + 3, indent);
        oss << "\"id\":" << separator << "\"" << escape_string(zone.id) << "\"," << newline;

        append_indent(oss, level + 3, indent);
        oss << "\"name\":" << separator << "\"" << escape_string(zone.name) << "\"," << newline;

        append_indent(oss, level + 3, indent);
        oss << "\"enabled\":" << separator << (zone.enabled ? "true" : "false") << "," << newline;

        append_indent(oss, level + 3, indent);
        oss << "\"points\":" << separator << "[";
        if (!zone.points.empty()) {
            oss << newline;
            for (std::size_t j = 0; j < zone.points.size(); ++j) {
                append_indent(oss, level + 4, indent);
                oss << "[" << zone.points[j].x << ", " << zone.points[j].y << "]";
                if (j + 1 < zone.points.size()) {
                    oss << ",";
                }
                oss << newline;
            }
            append_indent(oss, level + 3, indent);
        }
        oss << "]" << newline;

        append_indent(oss, level + 2, indent);
        oss << "}";
        if (i + 1 < config.allowed_zones.size()) {
            oss << ",";
        }
        oss << newline;
    }

    append_indent(oss, level + 1, indent);
    oss << "]" << newline;

    append_indent(oss, level, indent);
    oss << "}";
}

} // namespace

RoiConfigParseResult RoiRepository::from_json_string(const std::string& json_text)
{
    try {
        JsonParser parser(json_text);
        return parse_config_json(parser.parse());
    } catch (const std::exception& ex) {
        RoiConfigParseResult result;
        result.errors.push_back(ex.what());
        return result;
    }
}

RoiConfigParseResult RoiRepository::load_from_file(const std::string& path)
{
    std::ifstream ifs(path);
    if (!ifs.is_open()) {
        RoiConfigParseResult result;
        result.errors.push_back("failed to open file: " + path);
        return result;
    }

    std::ostringstream buffer;
    buffer << ifs.rdbuf();
    return from_json_string(buffer.str());
}

std::string RoiRepository::to_json_string(const CameraRoiConfig& config, int indent)
{
    std::ostringstream oss;
    append_config_json(oss, config, indent, 0);
    return oss.str();
}

bool RoiRepository::save_to_file(const CameraRoiConfig& config, const std::string& path)
{
    std::ofstream ofs(path);
    if (!ofs.is_open()) {
        return false;
    }

    ofs << to_json_string(config);
    return ofs.good();
}

} // namespace catcheye::roi
