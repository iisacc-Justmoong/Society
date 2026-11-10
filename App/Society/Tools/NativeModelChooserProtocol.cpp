#include "NativeModelChooserProtocol.h"

#include <json-c/json.h>
#include <memory>
#include <stdexcept>

namespace society {
namespace {
using Json = std::unique_ptr<json_object, decltype(&json_object_put)>;
using Tokener = std::unique_ptr<json_tokener, decltype(&json_tokener_free)>;
constexpr std::size_t maxBytes = 16 * 1024 * 1024;

json_object *field(json_object *object, const char *key, json_type type)
{
    json_object *value = nullptr;
    if (!json_object_is_type(object, json_type_object) || !json_object_object_get_ex(object, key, &value)
        || !json_object_is_type(value, type)) throw std::invalid_argument("Invalid chooser request");
    return value;
}
std::string stringValue(json_object *value)
{
    if (!json_object_is_type(value, json_type_string)) throw std::invalid_argument("Expected a string");
    std::string result(json_object_get_string(value), json_object_get_string_len(value));
    if (result.find('\0') != std::string::npos) throw std::invalid_argument("NUL in chooser request");
    return result;
}
std::string output(json_object *object)
{
    Json owned(object, json_object_put);
    return json_object_to_json_string_ext(owned.get(), JSON_C_TO_STRING_PLAIN);
}
}

ModelChooserRequest parseModelChooserRequest(std::string_view input)
{
    if (input.empty() || input.size() > maxBytes) throw std::invalid_argument("Chooser request size invalid");
    Tokener tokener(json_tokener_new(), json_tokener_free);
    if (!tokener) throw std::bad_alloc();
    json_tokener_set_flags(tokener.get(), JSON_TOKENER_STRICT | JSON_TOKENER_VALIDATE_UTF8);
    Json document(json_tokener_parse_ex(tokener.get(), input.data(), static_cast<int>(input.size())), json_object_put);
    if (!document || json_tokener_get_error(tokener.get()) != json_tokener_success
        || json_tokener_get_parse_end(tokener.get()) != input.size())
        throw std::invalid_argument("Invalid chooser JSON");
    auto *models = field(document.get(), "models", json_type_array);
    auto *selected = field(document.get(), "selected", json_type_array);
    if (json_object_array_length(models) > 65536) throw std::invalid_argument("Too many models");
    ModelChooserRequest request;
    request.models.reserve(json_object_array_length(models));
    for (std::size_t i = 0; i < json_object_array_length(models); ++i) {
        auto *entry = json_object_array_get_idx(models, i);
        request.models.push_back({stringValue(field(entry, "path", json_type_string)),
                                  stringValue(field(entry, "name", json_type_string)),
                                  stringValue(field(entry, "category", json_type_string))});
    }
    for (std::size_t i = 0; i < json_object_array_length(selected); ++i)
        request.selected.push_back(stringValue(json_object_array_get_idx(selected, i)));
    ModelChooserState validated(request.models, request.selected);
    return request;
}

std::string modelChooserSelected(const std::vector<std::string> &paths)
{
    auto *object = json_object_new_object();
    auto *array = json_object_new_array();
    for (const auto &path : paths) json_object_array_add(array, json_object_new_string_len(path.data(), static_cast<int>(path.size())));
    json_object_object_add(object, "action", json_object_new_string("selected"));
    json_object_object_add(object, "paths", array);
    return output(object);
}
std::string modelChooserFolder(std::string_view path)
{
    auto *object = json_object_new_object();
    json_object_object_add(object, "action", json_object_new_string("folder"));
    json_object_object_add(object, "path", json_object_new_string_len(path.data(), static_cast<int>(path.size())));
    return output(object);
}
std::string modelChooserCancel()
{
    auto *object = json_object_new_object();
    json_object_object_add(object, "action", json_object_new_string("cancel"));
    return output(object);
}
}
