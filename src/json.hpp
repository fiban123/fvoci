#pragma once
#include <cctype>
#include <cstdint>
#include <fstream>
#include <json-c/json.h>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

namespace fv {
struct J {
  using Object = std::map<std::string, J>;
  using Array = std::vector<J>;
  std::variant<std::nullptr_t, bool, int64_t, double, std::string, Array,
               Object>
      v = nullptr;
  J() = default;
  J(std::nullptr_t) {}
  J(bool x) : v(x) {}
  J(int x) : v(int64_t(x)) {}
  J(int64_t x) : v(x) {}
  J(double x) : v(x) {}
  J(const char *x) : v(std::string(x)) {}
  J(std::string x) : v(std::move(x)) {}
  J(Array x) : v(std::move(x)) {}
  J(Object x) : v(std::move(x)) {}
  J(std::initializer_list<Object::value_type> x) : v(Object(x)) {}
  static J array(std::initializer_list<J> x = {}) { return Array(x); }
  static J object() { return Object{}; }
  bool null() const { return std::holds_alternative<std::nullptr_t>(v); }
  bool is_object() const { return std::holds_alternative<Object>(v); }
  bool is_array() const { return std::holds_alternative<Array>(v); }
  bool is_string() const { return std::holds_alternative<std::string>(v); }
  bool is_int() const { return std::holds_alternative<int64_t>(v); }
  bool is_bool() const { return std::holds_alternative<bool>(v); }
  Object &object_items() {
    if (!is_object())
      throw std::runtime_error("Expected JSON object");
    return std::get<Object>(v);
  }
  const Object &object_items() const {
    if (!is_object())
      throw std::runtime_error("Expected JSON object");
    return std::get<Object>(v);
  }
  Array &items() {
    if (!is_array())
      throw std::runtime_error("Expected JSON array");
    return std::get<Array>(v);
  }
  const Array &items() const {
    if (!is_array())
      throw std::runtime_error("Expected JSON array");
    return std::get<Array>(v);
  }
  bool has(const std::string &k) const {
    return is_object() && object_items().count(k);
  }
  J &operator[](const std::string &k) {
    if (null())
      v = Object{};
    return object_items()[k];
  }
  const J &operator[](const std::string &k) const {
    static const J empty;
    if (!has(k))
      return empty;
    return object_items().at(k);
  }
  J &at(size_t n) { return items().at(n); }
  const J &at(size_t n) const { return items().at(n); }
  void push(J x) {
    if (null())
      v = Array{};
    items().push_back(std::move(x));
  }
  size_t size() const {
    return is_array()    ? items().size()
           : is_object() ? object_items().size()
                         : 0;
  }
  std::string str(const std::string &def = "") const {
    if (null())
      return def;
    if (!is_string())
      throw std::runtime_error("Expected JSON string");
    return std::get<std::string>(v);
  }
  int64_t num(int64_t def = 0) const {
    if (null())
      return def;
    if (!is_int())
      throw std::runtime_error("Expected JSON integer");
    return std::get<int64_t>(v);
  }
  double real(double def = 0) const {
    if (null()) return def;
    if (is_int()) return double(std::get<int64_t>(v));
    if (std::holds_alternative<double>(v)) return std::get<double>(v);
    throw std::runtime_error("Expected JSON number");
  }
  bool boolean(bool def = false) const {
    if (null())
      return def;
    if (!is_bool())
      throw std::runtime_error("Expected JSON boolean");
    return std::get<bool>(v);
  }
  void inc(const std::string &k, int64_t amount = 1) {
    (*this)[k] = (*this)[k].num() + amount;
  }
  static J from(json_object *p) {
    if (!p)
      return {};
    switch (json_object_get_type(p)) {
    case json_type_null:
      return {};
    case json_type_boolean:
      return bool(json_object_get_boolean(p));
    case json_type_int:
      return int64_t(json_object_get_int64(p));
    case json_type_double:
      return json_object_get_double(p);
    case json_type_string:
      return std::string(json_object_get_string(p),
                         json_object_get_string_len(p));
    case json_type_array: {
      J a = array();
      for (size_t i = 0; i < json_object_array_length(p); ++i)
        a.push(from(json_object_array_get_idx(p, i)));
      return a;
    }
    case json_type_object: {
      J o = object();
      json_object_object_foreach(p, k, val) { o[k] = from(val); }
      return o;
    }
    }
    throw std::runtime_error("Invalid JSON type");
  }
  json_object *native() const {
    if (null())
      return nullptr;
    if (is_bool())
      return json_object_new_boolean(boolean());
    if (is_int())
      return json_object_new_int64(num());
    if (std::holds_alternative<double>(v))
      return json_object_new_double(std::get<double>(v));
    if (is_string()) {
      auto s = str();
      return json_object_new_string_len(s.data(), static_cast<int>(s.size()));
    }
    if (is_array()) {
      auto *p = json_object_new_array();
      for (const auto &x : items())
        json_object_array_add(p, x.native());
      return p;
    }
    auto *p = json_object_new_object();
    for (const auto &[k, x] : object_items())
      json_object_object_add(p, k.c_str(), x.native());
    return p;
  }
  std::string dump(bool pretty = false) const {
    auto *p = native();
    std::string s = json_object_to_json_string_ext(
        p, (pretty ? JSON_C_TO_STRING_PRETTY : JSON_C_TO_STRING_PLAIN) |
               JSON_C_TO_STRING_NOSLASHESCAPE);
    if (p)
      json_object_put(p);
    return s;
  }
  static J parse(const std::string &text) {
    auto *tok = json_tokener_new();
    json_tokener_set_flags(tok,
                           JSON_TOKENER_STRICT | JSON_TOKENER_VALIDATE_UTF8);
    auto *p = json_tokener_parse_ex(tok, text.c_str(),
                                    static_cast<int>(text.size() + 1));
    auto err = json_tokener_get_error(tok);
    size_t end = json_tokener_get_parse_end(tok);
    std::string error = json_tokener_error_desc(err);
    json_tokener_free(tok);
    if (err != json_tokener_success) {
      if (p)
        json_object_put(p);
      throw std::runtime_error("Invalid JSON: " + error);
    }
    for (size_t i = end; i < text.size(); ++i)
      if (!std::isspace(static_cast<unsigned char>(text[i]))) {
        if (p)
          json_object_put(p);
        throw std::runtime_error("Trailing content after JSON");
      }
    J result = from(p);
    if (p)
      json_object_put(p);
    return result;
  }
  static J read(const std::string &path) {
    std::ifstream f(path);
    if (!f)
      throw std::runtime_error("Cannot read " + path);
    std::ostringstream s;
    s << f.rdbuf();
    return parse(s.str());
  }
};
} // namespace fv
