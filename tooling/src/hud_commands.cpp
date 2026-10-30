// hud.* commands: the HUD layout stored in project.json ("hud"). They edit ProjectFile::hud in memory
// (session().mutable_project()) and mark the project dirty; ProjectContext::save writes project.json.
// Elements are addressed by 0-based index into Hud::elements(). Field names mirror the persisted
// arcane.hud document except that offset and size are split into offset_x/offset_y/width/height.
// Runtime-only state (HudImage::texture, HudButton::state/focused) is never exposed.
#include <array>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <limits>

#include "arcane/hud.hpp"
#include "arcane_editor/asset_catalog.hpp"
#include "arcane_tooling/command.hpp"

namespace arcane::tooling {

namespace {

ToolError fail(const char* code, std::string message, std::string path = {}, std::string hint = {}) {
  return ToolError{code, std::move(message), "project.json", std::move(path), std::move(hint)};
}

constexpr std::array<const char*, 9> kAnchorNames = {"TopLeft", "Top",         "TopRight",   "Left",       "Center",
                                                     "Right",   "BottomLeft",  "Bottom",     "BottomRight"};
constexpr std::array<const char*, 4> kTypeNames = {"text", "image", "bar", "button"};

enum class Kind { String, Anchor, Float, Bool, Color, Uv, Asset };

struct FieldRef {
  const char* name;
  Kind kind;
  void* ptr;
};

// Persisted fields of one element, in document order.
std::vector<FieldRef> fields_of(HudElement& element) {
  if (auto* t = std::get_if<HudText>(&element)) {
    return {{"id", Kind::String, &t->id},         {"text", Kind::String, &t->text},
            {"anchor", Kind::Anchor, &t->anchor}, {"offset_x", Kind::Float, &t->offset_x},
            {"offset_y", Kind::Float, &t->offset_y}, {"font_scale", Kind::Float, &t->font_scale},
            {"color", Kind::Color, &t->color}};
  }
  if (auto* i = std::get_if<HudImage>(&element)) {
    return {{"id", Kind::String, &i->id},           {"texture_asset", Kind::Asset, &i->texture_asset},
            {"uv", Kind::Uv, &i->uv},               {"anchor", Kind::Anchor, &i->anchor},
            {"offset_x", Kind::Float, &i->offset_x}, {"offset_y", Kind::Float, &i->offset_y},
            {"width", Kind::Float, &i->width},       {"height", Kind::Float, &i->height},
            {"tint", Kind::Color, &i->tint}};
  }
  if (auto* b = std::get_if<HudProgressBar>(&element)) {
    return {{"id", Kind::String, &b->id},           {"anchor", Kind::Anchor, &b->anchor},
            {"offset_x", Kind::Float, &b->offset_x}, {"offset_y", Kind::Float, &b->offset_y},
            {"width", Kind::Float, &b->width},       {"height", Kind::Float, &b->height},
            {"fraction", Kind::Float, &b->fraction}, {"fill", Kind::Color, &b->fill},
            {"background", Kind::Color, &b->background}};
  }
  auto& b = std::get<HudButton>(element);
  return {{"id", Kind::String, &b.id},           {"label", Kind::String, &b.label},
          {"anchor", Kind::Anchor, &b.anchor},   {"offset_x", Kind::Float, &b.offset_x},
          {"offset_y", Kind::Float, &b.offset_y}, {"width", Kind::Float, &b.width},
          {"height", Kind::Float, &b.height},    {"font_scale", Kind::Float, &b.font_scale},
          {"enabled", Kind::Bool, &b.enabled},   {"normal", Kind::Color, &b.normal},
          {"hover", Kind::Color, &b.hover},      {"pressed", Kind::Color, &b.pressed},
          {"disabled", Kind::Color, &b.disabled}, {"text_color", Kind::Color, &b.text_color}};
}

const char* type_name(const HudElement& element) { return kTypeNames[element.index()]; }

std::string valid_fields(HudElement element) {
  std::string out;
  for (const FieldRef& f : fields_of(element)) out += (out.empty() ? "" : ", ") + std::string(f.name);
  return out;
}

// Shortest decimal that reads back as the same binary32, so outputs and inverses are exact and readable.
Json float_json(float v) {
  char buf[32];
  auto [end, ec] = std::to_chars(buf, buf + sizeof buf, v);
  return std::stod(std::string(buf, end));
}

Json color_json(const Color& c) { return Json::array({float_json(c.r), float_json(c.g), float_json(c.b), float_json(c.a)}); }

Json field_json(const FieldRef& f) {
  switch (f.kind) {
    case Kind::String: return *static_cast<std::string*>(f.ptr);
    case Kind::Anchor: return kAnchorNames[static_cast<std::size_t>(*static_cast<HudAnchor*>(f.ptr))];
    case Kind::Float: return float_json(*static_cast<float*>(f.ptr));
    case Kind::Bool: return *static_cast<bool*>(f.ptr);
    case Kind::Color: return color_json(*static_cast<Color*>(f.ptr));
    case Kind::Uv: {
      const auto& uv = *static_cast<UvRect*>(f.ptr);
      return Json::array({float_json(uv.u0), float_json(uv.v0), float_json(uv.u1), float_json(uv.v1)});
    }
    case Kind::Asset: return *static_cast<std::uint64_t*>(f.ptr);
  }
  return nullptr;
}

Json element_json(HudElement element, int index) {
  Json out = {{"index", index}, {"type", type_name(element)}};
  for (const FieldRef& f : fields_of(element)) out[f.name] = field_json(f);
  return out;
}

// All fields of an element as a hud.add `element` object.
Json element_fields(HudElement element) {
  Json out = Json::object();
  for (const FieldRef& f : fields_of(element)) out[f.name] = field_json(f);
  return out;
}

std::expected<float, ToolError> parse_float(const Json& v, const std::string& path) {
  if (!v.is_number()) return std::unexpected(fail("VALUE_INVALID", "expected a number", path));
  const double d = v.get<double>();
  if (!std::isfinite(d) || std::fabs(d) > std::numeric_limits<float>::max()) {
    return std::unexpected(fail("VALUE_INVALID", "number is not a finite binary32 value", path));
  }
  return static_cast<float>(d);
}

std::expected<std::array<float, 4>, ToolError> parse_quad(const Json& v, const std::string& path) {
  if (!v.is_array() || v.size() != 4) {
    return std::unexpected(fail("VALUE_INVALID", "expected an array of 4 numbers", path));
  }
  std::array<float, 4> out{};
  for (std::size_t i = 0; i < 4; ++i) {
    auto f = parse_float(v[i], path + "/" + std::to_string(i));
    if (!f) return std::unexpected(f.error());
    out[i] = *f;
  }
  return out;
}

// Validates `value` for field `f` and, only if valid, stores it. Cross-element rules (unique ids)
// are checked by the callers.
std::expected<void, ToolError> assign_field(ProjectContext& context, const FieldRef& f, const Json& value,
                                            const std::string& path) {
  switch (f.kind) {
    case Kind::String:
      if (!value.is_string()) return std::unexpected(fail("VALUE_INVALID", "expected a string", path));
      if (std::string(f.name) == "id" && value.get<std::string>().empty()) {
        return std::unexpected(fail("VALUE_INVALID", "id must not be empty", path));
      }
      *static_cast<std::string*>(f.ptr) = value.get<std::string>();
      return {};
    case Kind::Anchor: {
      if (value.is_string()) {
        for (std::size_t i = 0; i < kAnchorNames.size(); ++i) {
          if (value.get<std::string>() == kAnchorNames[i]) {
            *static_cast<HudAnchor*>(f.ptr) = static_cast<HudAnchor>(i);
            return {};
          }
        }
      }
      return std::unexpected(fail("VALUE_INVALID", "unknown anchor", path,
                                  "Use TopLeft, Top, TopRight, Left, Center, Right, BottomLeft, Bottom or BottomRight"));
    }
    case Kind::Float: {
      auto v = parse_float(value, path);
      if (!v) return std::unexpected(v.error());
      const std::string name = f.name;
      if ((name == "width" || name == "height") && *v < 0.0f) {
        return std::unexpected(fail("VALUE_INVALID", name + " must be >= 0", path));
      }
      if (name == "font_scale" && !(*v > 0.0f)) {
        return std::unexpected(fail("VALUE_INVALID", "font_scale must be > 0", path));
      }
      if (name == "fraction" && (*v < 0.0f || *v > 1.0f)) {
        return std::unexpected(fail("VALUE_INVALID", "fraction must be within [0, 1]", path));
      }
      *static_cast<float*>(f.ptr) = *v;
      return {};
    }
    case Kind::Bool:
      if (!value.is_boolean()) return std::unexpected(fail("VALUE_INVALID", "expected true or false", path));
      *static_cast<bool*>(f.ptr) = value.get<bool>();
      return {};
    case Kind::Color: {
      auto q = parse_quad(value, path);
      if (!q) return std::unexpected(q.error());
      *static_cast<Color*>(f.ptr) = Color{(*q)[0], (*q)[1], (*q)[2], (*q)[3]};
      return {};
    }
    case Kind::Uv: {
      auto q = parse_quad(value, path);
      if (!q) return std::unexpected(q.error());
      *static_cast<UvRect*>(f.ptr) = UvRect{(*q)[0], (*q)[1], (*q)[2], (*q)[3]};
      return {};
    }
    case Kind::Asset: {
      if (!value.is_number_unsigned() && !(value.is_number_integer() && value.get<std::int64_t>() >= 0)) {
        return std::unexpected(fail("VALUE_INVALID", "expected an asset id (unsigned integer, 0 = none)", path));
      }
      const std::uint64_t id = value.get<std::uint64_t>();
      if (id != 0) {
        bool found = false;
        for (const auto& asset : editor::list_image_assets(context.session().assets_dir())) {
          if (asset.id == id) found = true;
        }
        if (!found) {
          return std::unexpected(fail("VALUE_INVALID", "no image asset with id " + std::to_string(id), path,
                                      "Use an id of an image under assets/ (0 clears the texture)"));
        }
      }
      *static_cast<std::uint64_t*>(f.ptr) = id;
      return {};
    }
  }
  return {};
}

std::string& element_id(HudElement& element) {
  return std::visit([](auto& e) -> std::string& { return e.id; }, element);
}

bool id_in_use(Hud& hud, const std::string& id, int except = -1) {
  for (std::size_t i = 0; i < hud.elements().size(); ++i) {
    if (static_cast<int>(i) != except && element_id(hud.elements()[i]) == id) return true;
  }
  return false;
}

// Unused id: "button" for the first button, otherwise "<type>_<n>" (the scheme saving uses).
std::string fresh_id(Hud& hud, const HudElement& element) {
  const std::string base = type_name(element);
  if (base == "button" && !id_in_use(hud, base)) return base;
  for (int n = 1;; ++n) {
    std::string candidate = base + "_" + std::to_string(n);
    if (!id_in_use(hud, candidate)) return candidate;
  }
}

std::expected<HudElement, ToolError> default_element(const std::string& type) {
  if (type == "text") return HudText{.text = "Text", .color = {1, 1, 1, 1}};
  if (type == "image") return HudImage{};
  if (type == "bar") return HudProgressBar{};
  if (type == "button") return HudButton{.id = "button", .label = "Button"};
  return std::unexpected(fail("ARGS_INVALID", "unknown element type '" + type + "'", "/type", "Use text, image, bar or button"));
}

std::expected<int, ToolError> check_index(const Hud& hud, const Json& args, const char* key, bool allow_end = false) {
  const long long index = args[key].get<long long>();
  const long long limit = static_cast<long long>(hud.elements().size()) + (allow_end ? 1 : 0);
  if (index < 0 || index >= limit) {
    return std::unexpected(fail("VALUE_INVALID",
                                "HUD index " + std::to_string(index) + " is out of range (" +
                                    std::to_string(hud.elements().size()) + " elements)",
                                std::string("/") + key, "List elements with hud.list"));
  }
  return static_cast<int>(index);
}

Json schema(Json properties, std::initializer_list<const char*> required) {
  Json req = Json::array();
  for (const char* r : required) req.push_back(r);
  return {{"type", "object"}, {"properties", std::move(properties)}, {"required", req}, {"additionalProperties", false}};
}

const Json kIndex = {{"type", "integer"}};
const Json kTypeEnum = {{"type", "string"}, {"enum", Json::array({"text", "image", "bar", "button"})}};

}  // namespace

void register_hud_commands(CommandRegistry& registry) {
  registry.add(
      {"hud.list", "List the HUD elements (index, type and every persisted field).",
       schema(Json::object(), {}), Json{{"type", "object"}}, false},
      [](ProjectContext& context, const Json&) -> std::expected<CommandResult, ToolError> {
        Json elements = Json::array();
        int index = 0;
        for (const HudElement& e : context.session().project().hud.elements()) elements.push_back(element_json(e, index++));
        return CommandResult{{{"count", elements.size()}, {"elements", elements}}, std::nullopt, std::nullopt};
      });

  registry.add(
      {"hud.add",
       "Insert a HUD element (text, image, bar or button) with editor defaults; `element` overrides fields (see hud.set).",
       schema({{"type", kTypeEnum}, {"index", kIndex}, {"element", {{"type", "object"}}}}, {"type"}),
       Json{{"type", "object"}}, true},
      [](ProjectContext& context, const Json& args) -> std::expected<CommandResult, ToolError> {
        Hud& hud = context.session().mutable_project().hud;
        auto element = default_element(args["type"].get<std::string>());
        if (!element) return std::unexpected(element.error());
        int index = static_cast<int>(hud.elements().size());
        if (args.contains("index")) {
          auto checked = check_index(hud, args, "index", true);
          if (!checked) return std::unexpected(checked.error());
          index = *checked;
        }
        const bool explicit_id = args.contains("element") && args["element"].contains("id");
        if (args.contains("element")) {
          for (const auto& [key, value] : args["element"].items()) {
            const FieldRef* match = nullptr;
            auto fields = fields_of(*element);
            for (const FieldRef& f : fields) {
              if (key == f.name) match = &f;
            }
            if (!match) {
              return std::unexpected(fail("ARGS_INVALID", "'" + key + "' is not a field of a " + type_name(*element),
                                          "/element/" + key, "Valid fields: " + valid_fields(*element)));
            }
            if (auto ok = assign_field(context, *match, value, "/element/" + key); !ok) return std::unexpected(ok.error());
          }
        }
        if (explicit_id) {
          if (id_in_use(hud, element_id(*element))) {
            return std::unexpected(fail("VALUE_INVALID", "HUD id '" + element_id(*element) + "' is already in use",
                                        "/element/id", "Element ids are unique"));
          }
        } else if (id_in_use(hud, element_id(*element)) || element_id(*element).empty()) {
          element_id(*element) = fresh_id(hud, *element);
        }
        const std::string id = element_id(*element);
        hud.elements().insert(hud.elements().begin() + index, std::move(*element));
        context.mark_project_dirty();
        return CommandResult{{{"index", index}, {"id", id}}, Invocation{"hud.remove", {{"index", index}}}, std::nullopt};
      });

  registry.add(
      {"hud.remove", "Remove the HUD element at `index`.", schema({{"index", kIndex}}, {"index"}),
       Json{{"type", "object"}}, true},
      [](ProjectContext& context, const Json& args) -> std::expected<CommandResult, ToolError> {
        Hud& hud = context.session().mutable_project().hud;
        auto index = check_index(hud, args, "index");
        if (!index) return std::unexpected(index.error());
        HudElement removed = hud.elements()[static_cast<std::size_t>(*index)];
        Json restore = {{"type", type_name(removed)}, {"index", *index}, {"element", element_fields(removed)}};
        hud.elements().erase(hud.elements().begin() + *index);
        context.mark_project_dirty();
        return CommandResult{{{"index", *index}, {"id", element_id(removed)}, {"type", type_name(removed)}},
                             Invocation{"hud.add", std::move(restore)}, std::nullopt};
      });

  registry.add(
      {"hud.set",
       "Set one field of the HUD element at `index`. Common: id, anchor (TopLeft..BottomRight), offset_x, offset_y. "
       "text: text, font_scale, color; image: texture_asset (0 = none), uv [u0,v0,u1,v1], width, height, tint; "
       "bar: width, height, fraction, fill, background; button: label, width, height, font_scale, enabled, "
       "normal, hover, pressed, disabled, text_color. Colors are [r,g,b,a].",
       schema({{"index", kIndex}, {"field", {{"type", "string"}}}, {"value", Json::object()}}, {"index", "field", "value"}),
       Json{{"type", "object"}}, true},
      [](ProjectContext& context, const Json& args) -> std::expected<CommandResult, ToolError> {
        Hud& hud = context.session().mutable_project().hud;
        auto index = check_index(hud, args, "index");
        if (!index) return std::unexpected(index.error());
        HudElement& element = hud.elements()[static_cast<std::size_t>(*index)];
        const std::string name = args["field"].get<std::string>();
        const FieldRef* match = nullptr;
        auto fields = fields_of(element);
        for (const FieldRef& f : fields) {
          if (name == f.name) match = &f;
        }
        if (!match) {
          return std::unexpected(fail("ARGS_INVALID", "'" + name + "' is not a field of a " + type_name(element), "/field",
                                      "Valid fields: " + valid_fields(element)));
        }
        const Json previous = field_json(*match);
        HudElement trial = element;  // validate on a copy, so a rejected value changes nothing
        auto trial_fields = fields_of(trial);
        for (const FieldRef& f : trial_fields) {
          if (name != f.name) continue;
          if (auto ok = assign_field(context, f, args["value"], "/value"); !ok) return std::unexpected(ok.error());
          if (name == "id" && id_in_use(hud, element_id(trial), *index)) {
            return std::unexpected(fail("VALUE_INVALID", "HUD id '" + element_id(trial) + "' is already in use", "/value",
                                        "Element ids are unique"));
          }
          const Json now = field_json(f);
          Json value = {{"index", *index}, {"field", name}, {"previous", previous}, {"value", now}, {"changed", now != previous}};
          if (now == previous) return CommandResult{value, std::nullopt, std::nullopt};
          element = std::move(trial);
          context.mark_project_dirty();
          return CommandResult{value, Invocation{"hud.set", {{"index", *index}, {"field", name}, {"value", previous}}},
                               std::nullopt};
        }
        return std::unexpected(fail("ARGS_INVALID", "unreachable field", "/field"));
      });

  registry.add(
      {"hud.move", "Move the HUD element at `from` so it ends up at index `to` (draw order).",
       schema({{"from", kIndex}, {"to", kIndex}}, {"from", "to"}), Json{{"type", "object"}}, true},
      [](ProjectContext& context, const Json& args) -> std::expected<CommandResult, ToolError> {
        Hud& hud = context.session().mutable_project().hud;
        auto from = check_index(hud, args, "from");
        if (!from) return std::unexpected(from.error());
        auto to = check_index(hud, args, "to");
        if (!to) return std::unexpected(to.error());
        const Json value = {{"from", *from}, {"to", *to}, {"changed", *from != *to}};
        if (*from == *to) return CommandResult{value, std::nullopt, std::nullopt};
        HudElement moved = std::move(hud.elements()[static_cast<std::size_t>(*from)]);
        hud.elements().erase(hud.elements().begin() + *from);
        hud.elements().insert(hud.elements().begin() + *to, std::move(moved));
        context.mark_project_dirty();
        return CommandResult{value, Invocation{"hud.move", {{"from", *to}, {"to", *from}}}, std::nullopt};
      });
}

}  // namespace arcane::tooling
