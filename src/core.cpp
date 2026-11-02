#include "core.hpp"
#include <algorithm>
#include <cmath>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <openssl/sha.h>
#include <sstream>
#include <utf8proc.h>

namespace fv {
std::wstring wide(const std::string &s) {
  std::wstring out;
  size_t i = 0;
  while (i < s.size()) {
    utf8proc_int32_t c;
    auto n = utf8proc_iterate(reinterpret_cast<const uint8_t *>(s.data() + i),
                              s.size() - i, &c);
    if (n < 0)
      throw std::runtime_error("Invalid UTF-8 text");
    out.push_back(static_cast<wchar_t>(c));
    i += n;
  }
  return out;
}
std::string utf8(const std::wstring &s) {
  std::string out;
  for (auto c : s) {
    uint8_t buf[4];
    auto n = utf8proc_encode_char(c, buf);
    if (!n)
      throw std::runtime_error("Invalid Unicode character");
    out.append(reinterpret_cast<char *>(buf), n);
  }
  return out;
}
std::string normalize(const std::string &s, bool sensitive, bool punctuation) {
  utf8proc_uint8_t *out = nullptr;
  auto flags = static_cast<utf8proc_option_t>(
      UTF8PROC_STABLE | UTF8PROC_COMPOSE | (sensitive ? 0 : UTF8PROC_CASEFOLD));
  auto len = utf8proc_map(reinterpret_cast<const uint8_t *>(s.data()), s.size(),
                          &out, flags);
  if (len < 0)
    throw std::runtime_error("Invalid Unicode text");
  std::string n(reinterpret_cast<char *>(out), len);
  free(out);
  std::wstring result;
  bool space = false;
  for (auto c : wide(n)) {
    auto cat = utf8proc_category(c);
    if (c == L'\t' || c == L'\n' || c == L'\r' || cat == UTF8PROC_CATEGORY_ZS ||
        cat == UTF8PROC_CATEGORY_ZL || cat == UTF8PROC_CATEGORY_ZP) {
      space = !result.empty();
      continue;
    }
    if (space)
      result += L' ';
    space = false;
    result += (c == 0x2019 || c == 0x2018) ? L'\'' : c;
  }
  if (punctuation)
    while (!result.empty() && (result.back() == L'.' || result.back() == L'!' ||
                               result.back() == L'?' || result.back() == L' '))
      result.pop_back();
  return utf8(result);
}
std::string hash(const std::string &s) {
  unsigned char bytes[SHA256_DIGEST_LENGTH];
  SHA256(reinterpret_cast<const unsigned char *>(s.data()), s.size(), bytes);
  std::ostringstream o;
  o << "sha256:";
  for (auto b : bytes)
    o << std::hex << std::setw(2) << std::setfill('0') << int(b);
  return o.str();
}
int64_t now_seconds() {
  return std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
}
std::string stamp(int64_t t) {
  time_t x = t;
  tm tm{};
  gmtime_r(&x, &tm);
  char b[32];
  strftime(b, sizeof b, "%Y-%m-%dT%H:%M:%SZ", &tm);
  return b;
}
int64_t unstamp(const J &j) {
  if (j.null())
    return 0;
  tm t{};
  std::istringstream in(j.str());
  in >> std::get_time(&t, "%Y-%m-%dT%H:%M:%SZ");
  if (in.fail())
    throw std::runtime_error("Invalid timestamp: " + j.str());
  return timegm(&t);
}
std::string local_day(int64_t t) {
  time_t x = t;
  tm tm{};
  localtime_r(&x, &tm);
  char b[16];
  strftime(b, sizeof b, "%Y-%m-%d", &tm);
  return b;
}
J defaults() {
  return J{{"schema_version", 1},
           {"mode", "adaptive"},
           {"direction_weights", J{{"de_fr", 90}, {"fr_de", 10}}},
           {"session_questions", 0},
           {"sweep", "default"},
           {"retry_gap_min", 3}, {"retry_gap_max", 10},
           {"difficulty_threshold", 1.0}, {"very_difficult_threshold", 2.0},
           {"choice_support_score", 3.0}, {"random_fraction", 0.2},
           {"full_sweep_every", 5},
           {"written_mistake_weight", 1.0}, {"choice_mistake_weight", 0.5},
           {"clean_sweep_decay", 0.5},
           {"inference", "text-expansion-v1"},
           {"case_sensitive", true},
           {"sentence_case_sensitive", false},
           {"sentence_ignore_punctuation", true},
           {"ignore_terminal_punctuation_for_other", false},
           {"allow_sentence_self_override", true},
           {"accent_macros_enabled", true},
           {"keymap", "vim"},
           {"color", "auto"},
           {"choice_options", 4},
           {"defer_mock_feedback", false},
           {"feedback_detail", "full"},
           {"profile_directory", "progress"},
           {"theme", J{{"accent", "#56b6c2"}, {"success", "#50d890"}, {"error", "#ff6464"}}},
           {"accent_macros", J::object()},
           {"keybindings", J::object()}};
}
void validate_config(const J &c) {
  if (c["schema_version"].num() != 1)
    throw std::runtime_error("Unsupported configuration schema_version");
  auto mode = c["mode"].str();
  if (mode != "adaptive" && mode != "text" && mode != "multiple_choice")
    throw std::runtime_error("mode must be adaptive, text, or multiple_choice");
  auto inference = c["inference"].str();
  if (inference != "none" && inference != "text-expansion-v1")
    throw std::runtime_error("Unknown text expansion policy");
  static const std::set<std::string> sweeps = {
      "default", "full", "difficult", "very_difficult", "difficult_random", "very_difficult_random"};
  if (!sweeps.count(c["sweep"].str())) throw std::runtime_error("Unknown sweep selection");
  if (c["session_questions"].num() < 0 || c["session_questions"].num() > 100000)
    throw std::runtime_error("Invalid session question limit");
  if (c["retry_gap_min"].num() < 1 || c["retry_gap_max"].num() < c["retry_gap_min"].num() ||
      c["retry_gap_max"].num() > 1000 || c["full_sweep_every"].num() < 1)
    throw std::runtime_error("Invalid sweep spacing settings");
  for (auto k : {"difficulty_threshold", "very_difficult_threshold", "choice_support_score",
                 "written_mistake_weight", "choice_mistake_weight", "clean_sweep_decay"})
    if (!std::isfinite(c[k].real()) || c[k].real() <= 0 || c[k].real() > 10)
      throw std::runtime_error(std::string("Invalid score setting: ") + k);
  if (c["very_difficult_threshold"].real() < c["difficulty_threshold"].real() ||
      !std::isfinite(c["random_fraction"].real()) || c["random_fraction"].real() < 0 || c["random_fraction"].real() > 1)
    throw std::runtime_error("Invalid sweep thresholds or random fraction");
  auto a = c["direction_weights"]["de_fr"].num(),
       b = c["direction_weights"]["fr_de"].num();
  if (a < 0 || b < 0 || a + b < 1 || a + b > 10000)
    throw std::runtime_error(
        "Direction weights must be nonnegative and sum to 1..10000");
  if (c["choice_options"].num() < 3 || c["choice_options"].num() > 9)
    throw std::runtime_error("choice_options must be 3..9");
  for (auto k : {"case_sensitive", "sentence_case_sensitive",
                 "sentence_ignore_punctuation", "ignore_terminal_punctuation_for_other",
                 "allow_sentence_self_override", "accent_macros_enabled",
                 "defer_mock_feedback"})
    c[k].boolean();
  for (const auto &[k, v] : c["accent_macros"].object_items())
    if (k.empty() || v.str().empty())
      throw std::runtime_error("Empty macro");
  if (c["keymap"].str() != "vim")
    throw std::runtime_error("Only the vim keymap is supported");
  auto color = c["color"].str();
  if (color != "auto" && color != "never" && color != "always")
    throw std::runtime_error("color must be auto, never, or always");
  for (auto k : {"accent", "success", "error"}) {
    const auto &value = c["theme"][k];
    if (value.is_string()) {
      auto hex = value.str();
      if (hex.size() != 7 || hex[0] != '#' ||
          hex.find_first_not_of("0123456789abcdefABCDEF", 1) != std::string::npos)
        throw std::runtime_error("Theme colors must be #RRGGBB");
    } else if (value.num() < 0 || value.num() > 7)
      throw std::runtime_error("Legacy theme indices must be 0..7");
  }
  if (c["feedback_detail"].str() != "full" &&
      c["feedback_detail"].str() != "errors")
    throw std::runtime_error("feedback_detail must be full or errors");
  if (c["profile_directory"].str().empty())
    throw std::runtime_error("profile_directory cannot be empty");
  for (const auto &[context, map] : c["keybindings"].object_items()) {
    if (context != "normal")
      throw std::runtime_error("Only normal-mode keybindings can be remapped");
    for (const auto &[from, to] : map.object_items())
      if (wide(from).size() != 1 || wide(to.str()).size() != 1)
        throw std::runtime_error(
            "Keybindings map one character to one normal-mode command");
  }
  if (!c["test_date"].null()) {
    std::string s = c["test_date"].str();
    tm t{};
    std::istringstream in(s);
    in >> std::get_time(&t, "%Y-%m-%d");
    if (in.fail() || s.size() != 10)
      throw std::runtime_error("test_date must be YYYY-MM-DD");
    if (stamp(timegm(&t)).substr(0, 10) != s)
      throw std::runtime_error("test_date is not a valid calendar date");
  }
}
J policy(const J &c) {
  return J{{"reverse_matching", "any-listed-case-punctuation-insensitive-v1"},
           {"answer_matching", "listed-forms-v2"},
           {"inference", c["inference"]},
           {"case_sensitive", c["case_sensitive"]},
           {"sentence_case_sensitive", c["sentence_case_sensitive"]},
           {"sentence_ignore_punctuation", c["sentence_ignore_punctuation"]},
           {"ignore_terminal_punctuation_for_other",
            c["ignore_terminal_punctuation_for_other"]},
           {"self_override", c["allow_sentence_self_override"].boolean()
                                 ? "other_when_allowed"
                                 : "disabled"}};
}
std::map<std::wstring, std::wstring> macros(const J &config) {
  std::map<std::wstring, std::wstring> m = {
      {L"e+", L"é"}, {L"e\\", L"è"}, {L"a\\", L"à"}, {L"u\\", L"ù"},
      {L"a^", L"â"}, {L"e^", L"ê"},  {L"i^", L"î"},  {L"o^", L"ô"},
      {L"u^", L"û"}, {L"e:", L"ë"},  {L"i:", L"ï"},  {L"u:", L"ü"},
      {L"a:", L"ä"}, {L"o:", L"ö"}, {L"y:", L"ÿ"}, {L"c,", L"ç"},  {L"oe+", L"œ"}, {L"ae+", L"æ"}};
  auto copy = m;
  for (const auto &[key, val] : copy) {
    auto k = key, v = val;
    for (auto &c : k)
      c = utf8proc_toupper(c);
    for (auto &c : v)
      c = utf8proc_toupper(c);
    m[k] = v;
  }
  if (config["accent_macros"].is_object())
    for (const auto &[k, v] : config["accent_macros"].object_items())
      m[wide(k)] = wide(v.str());
  return m;
}
bool expand_macro(std::wstring &s, size_t &cursor,
                  const std::map<std::wstring, std::wstring> &m) {
  size_t best = 0;
  std::wstring replacement;
  for (const auto &[key, val] : m)
    if (key.size() > best && cursor >= key.size() &&
        s.compare(cursor - key.size(), key.size(), key) == 0) {
      best = key.size();
      replacement = val;
    }
  if (!best)
    return false;
  s.replace(cursor - best, best, replacement);
  cursor = cursor - best + replacement.size();
  return true;
}
static std::vector<size_t> boundaries(const std::wstring &s) {
  std::vector<size_t> b{0};
  int32_t state = 0;
  for (size_t i = 1; i < s.size(); ++i)
    if (utf8proc_grapheme_break_stateful(s[i - 1], s[i], &state))
      b.push_back(i);
  b.push_back(s.size());
  return b;
}
size_t previous_grapheme(const std::wstring &s, size_t p) {
  size_t prev = 0;
  for (auto b : boundaries(s)) {
    if (b >= p)
      break;
    prev = b;
  }
  return prev;
}
size_t next_grapheme(const std::wstring &s, size_t p) {
  for (auto b : boundaries(s))
    if (b > p)
      return b;
  return s.size();
}
Noun noun(const std::string &input) {
  Noun n;
  auto s = normalize(input);
  for (auto art : {"les ", "le ", "la ", "l'"})
    if (s.rfind(art, 0) == 0) {
      n.article = std::string(art);
      n.body = s.substr(n.article.size());
      n.plural = n.article == "les ";
      n.gender = n.article == "le " ? 'm' : n.article == "la " ? 'f' : 0;
      n.valid = !n.body.empty();
      return n;
    }
  return n;
}
static void need(bool b, const std::string &msg) {
  if (!b)
    throw std::runtime_error(msg);
}
static void fields(const J &j, const std::set<std::string> &allowed,
                   const std::string &context) {
  need(j.is_object(), "Expected object in " + context);
  for (const auto &[key, value] : j.object_items())
    need(allowed.count(key), "Unknown field in " + context + ": " + key);
}
static std::string id(const J &j, const std::string &context) {
  auto s = j.str();
  need(!s.empty() &&
           s.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTU"
                               "VWXYZ0123456789-_") == std::string::npos,
       "Invalid ID in " + context + ": " + s);
  return s;
}
static std::vector<std::string> strings(const J &j,
                                        const std::string &context) {
  need(j.is_array() && j.size() > 0, "Empty or invalid answers in " + context);
  std::vector<std::string> v;
  std::set<std::string> seen;
  for (const auto &a : j.items()) {
    auto s = a.str();
    need(!normalize(s).empty(), "Empty answer in " + context);
    for (auto c : wide(s))
      need(c >= 32 && c != 127, "Control character in " + context);
    need(seen.insert(normalize(s)).second,
         "Duplicate alternative in " + context);
    v.push_back(s);
  }
  return v;
}
Deck load_deck(const J &j) {
  need(j.is_object() && j["schema_version"].num() == 1,
       "Unsupported vocabulary schema_version");
  fields(j,
         {"schema_version", "id", "revision", "title", "languages", "entries",
          "test_date"},
         "vocabulary");
  Deck d;
  d.id = id(j["id"], "deck");
  d.title = j["title"].str();
  need(!d.title.empty(), "Missing title");
  d.revision = j["revision"].num();
  need(d.revision > 0, "revision must be positive");
  d.test_date = j["test_date"].str();
  if (j.has("test_date")) {
    auto c = defaults();
    c["test_date"] = j["test_date"];
    validate_config(c);
  }
  fields(j["languages"], {"prompt", "answer"}, "languages");
  need(j["languages"]["prompt"].str() == "de" &&
           j["languages"]["answer"].str() == "fr",
       "Expected de/fr languages");
  need(j["entries"].is_array() && j["entries"].size() > 0,
       "Vocabulary has no entries");
  std::set<std::string> ids;
  for (const auto &e : j["entries"].items()) {
    fields(e,
           {"id", "type", "de", "fr", "allow_self_override", "source", "notes",
            "related_entry_ids"},
           "entry");
    Entry x;
    x.id = id(e["id"], "entry");
    need(ids.insert(x.id).second, "Duplicate entry ID: " + x.id);
    x.type = e["type"].str();
    need(x.type == "noun" || x.type == "verb" || x.type == "adjective" ||
             x.type == "other",
         "Unknown type in " + x.id);
    x.prompt = e["de"]["prompt"].str();
    need(!normalize(x.prompt).empty(), "Missing German prompt: " + x.id);
    x.override_allowed = e["allow_self_override"].boolean();
    need(!x.override_allowed || x.type == "other",
         "Self-override only allowed for other entries");
    if (e.has("notes"))
      need(e["notes"].is_string(), "notes must be text");
    if (e.has("source")) {
      fields(e["source"], {"file", "page", "row"}, "source");
      need(!e["source"]["file"].str().empty(), "source.file is required");
      if (e["source"].has("page"))
        need(e["source"]["page"].num() > 0, "source.page must be positive");
      if (e["source"].has("row"))
        need(!e["source"]["row"].str().empty(),
             "source.row must be nonempty text");
    }
    fields(e["de"], {"prompt", "answers"}, "German question");
    std::set<std::string> gids;
    std::map<std::string, std::string> seen;
    need(e["de"]["answers"].is_array() && e["de"]["answers"].size() > 0,
         "Missing German answers: " + x.id);
    for (const auto &a : e["de"]["answers"].items()) {
      fields(a, {"id", "accept"}, "German answer group");
      auto gid = id(a["id"], x.id);
      need(gids.insert(gid).second, "Duplicate German group ID: " + x.id);
      auto values = strings(a["accept"], x.id);
      for (const auto &s : values)
        need(seen.emplace(normalize(s), gid).second,
             "Overlapping German answer groups: " + x.id);
      x.de.push_back({gid, gid, "text", values, 0});
    }
    gids.clear();
    seen.clear();
    need(e["fr"].is_array() && e["fr"].size() > 0,
         "Missing French answers: " + x.id);
    for (const auto &g : e["fr"].items()) {
      fields(g, {"id", "forms"}, "French answer group");
      auto gid = id(g["id"], x.id);
      need(gids.insert(gid).second, "Duplicate French group ID: " + x.id);
      need(g["forms"].is_object() && g["forms"].size() > 0,
           "Missing forms: " + x.id);
      if (x.type == "verb")
        need(g["forms"].has("infinitive"), "Verb missing infinitive: " + x.id);
      for (const auto &[slot, a] : g["forms"].object_items()) {
        bool inflected = x.type == "noun" || x.type == "adjective";
        if (inflected)
          need(slot == "m_sg" || slot == "f_sg" || slot == "m_pl" ||
                   slot == "f_pl",
               "Unknown form key: " + slot);
        else if (x.type == "other")
          need(slot == "text", "Other entries require text form");
        else {
          static const std::set<std::string> valid = {
              "infinitive",   "present.je",   "present.tu", "present.il",
              "present.nous", "present.vous", "present.ils"};
          need(valid.count(slot), "Unsupported verb form: " + slot);
        }
        auto values = strings(a, x.id);
        char gender = inflected ? slot[0] : 0;
        for (const auto &s : values) {
          auto norm = normalize(s);
          auto key = norm + ":" + std::string(1, gender);
          auto it = seen.find(key);
          need(it == seen.end() || it->second == gid,
               "Overlapping French synonym groups: " + x.id);
          seen[key] = gid;
          if (x.type == "noun") {
            auto n = noun(s);
            need(n.valid, "Noun needs definite article: " + s);
            need(!n.gender || n.gender == gender,
                 "Noun gender/article conflict: " + s);
            need(n.plural == (slot.substr(2) == "pl"),
                 "Noun number/article conflict: " + s);
          } else if (x.type == "adjective") {
            need(!noun(s).valid,
                 "Adjective forms must not include articles: " + s);
          }
        }
        x.fr.push_back({gid + "." + slot, gid, slot, values, gender});
      }
    }
    x.snapshot = J{{"type", x.type},
                   {"de", e["de"]},
                   {"fr", e["fr"]},
                   {"allow_self_override", x.override_allowed}};
    d.entries.push_back(std::move(x));
  }
  for (const auto &e : j["entries"].items()) {
    if (!e.has("related_entry_ids"))
      continue;
    std::set<std::string> seen;
    for (const auto &related : e["related_entry_ids"].items()) {
      auto r = id(related, "related_entry_ids");
      need(ids.count(r) && seen.insert(r).second,
           "Unknown or duplicate related entry: " + r);
    }
  }
  return d;
}
std::vector<Unit> units(const Entry &e, const std::string &direction) {
  return direction == "de_fr" ? e.fr : e.de;
}
namespace {
// Text expansion deliberately has no knowledge of French or the answer key.
// Keep duplicate alternatives until after alignment: {o,o,b}{,e,o} relies on it.
class TextExpansion {
  using Parts = std::vector<std::string>;
  const std::string &input;
  size_t pos = 0;
  static constexpr size_t limit = 256;
  static bool space(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }
  static void append(Parts &to, const Parts &from) {
    if (to.size() + from.size() > limit) throw std::runtime_error("At most 256 expanded answers are allowed");
    to.insert(to.end(), from.begin(), from.end());
  }
  static Parts zip(const Parts &a, const Parts &b) {
    if (a.size() != b.size() && a.size() != 1 && b.size() != 1)
      throw std::runtime_error("Alternative lists must have equal lengths (literal text repeats)");
    Parts out;
    for (size_t i = 0; i < std::max(a.size(), b.size()); ++i) {
      auto value = a[a.size() == 1 ? 0 : i] + b[b.size() == 1 ? 0 : i];
      if (value.size() > 8192) throw std::runtime_error("Expanded answer is too long");
      out.push_back(std::move(value));
    }
    return out;
  }
  Parts atom(int depth) {
    if (pos == input.size()) throw std::runtime_error("Missing text after expansion operator");
    if (input[pos] == '{') {
      if (depth >= 16) throw std::runtime_error("Expansion braces are nested too deeply");
      ++pos;
      auto out = list(depth + 1);
      if (pos == input.size() || input[pos] != '}') throw std::runtime_error("Missing closing brace");
      ++pos;
      return out;
    }
    size_t begin = pos;
    while (pos < input.size() && !space(input[pos]) &&
           std::string("{},;/.").find(input[pos]) == std::string::npos) ++pos;
    if (pos == begin) throw std::runtime_error("Missing text beside expansion operator");
    return {input.substr(begin, pos - begin)};
  }
  Parts alternatives(int depth) {
    auto out = atom(depth);
    while (pos < input.size() && input[pos] == '/') {
      if (!pos || space(input[pos - 1]) || pos + 1 == input.size() || space(input[pos + 1]))
        throw std::runtime_error("A slash must touch the alternatives on both sides");
      ++pos;
      append(out, atom(depth));
    }
    return out;
  }
  Parts sequence(int depth) {
    Parts out{""};
    while (pos < input.size() && input[pos] != '}' && input[pos] != ',' && input[pos] != ';') {
      if (space(input[pos])) {
        size_t begin = pos;
        while (pos < input.size() && space(input[pos])) ++pos;
        out = zip(out, {input.substr(begin, pos - begin)});
      } else if (input[pos] == '.') {
        Parts suffix{""};
        while (pos < input.size() && input[pos] == '.') {
          ++pos;
          append(suffix, alternatives(depth));
        }
        out = zip(out, suffix);
      } else if (input[pos] == '/') {
        throw std::runtime_error("A slash must touch the alternatives on both sides");
      } else {
        out = zip(out, alternatives(depth));
      }
    }
    return out;
  }
  Parts list(int depth) {
    Parts out;
    while (true) {
      auto part = sequence(depth);
      for (auto &s : part) s = normalize(s);
      append(out, part);
      if (pos == input.size() || (input[pos] != ',' && input[pos] != ';')) break;
      ++pos;
    }
    return out;
  }
public:
  explicit TextExpansion(const std::string &s) : input(s) {}
  Parts run() {
    if (input.size() > 8192) throw std::runtime_error("Answer expression is too long");
    auto out = list(0);
    if (pos != input.size()) throw std::runtime_error("Unexpected closing brace");
    return out;
  }
};
} // namespace
std::vector<std::string> expand_text(const std::string &text) {
  return TextExpansion(text).run();
}
static std::string strip_accents(const std::string &s) {
  uint8_t *p = nullptr;
  auto n = utf8proc_map(
      reinterpret_cast<const uint8_t *>(s.data()), s.size(), &p,
      static_cast<utf8proc_option_t>(UTF8PROC_DECOMPOSE | UTF8PROC_STRIPMARK |
                                     UTF8PROC_STABLE));
  if (n < 0)
    return s;
  std::string out(reinterpret_cast<char *>(p), n);
  free(p);
  return out;
}
std::vector<Row> parse_answer(const Entry &entry, const std::string &direction,
                              const std::string &text) {
  if (direction == "fr_de")
    return {{text, ""}};
  // Sentences keep their commas and parentheses. Semicolons separate full
  // sentence alternatives; vocabulary forms use commas or semicolons.
  bool sentence = entry.type == "other";
  bool split_sentences = sentence && units(entry, direction).size() > 1;
  bool genders = direction == "de_fr" &&
                 (entry.type == "noun" || entry.type == "adjective");
  std::vector<Row> rows;
  size_t begin = 0;
  int depth = 0;
  for (size_t i = 0; i <= text.size(); ++i) {
    if (!sentence && i < text.size()) {
      if (text[i] == '{') ++depth;
      if (text[i] == '}') --depth;
    }
    if (i < text.size() &&
        !(depth == 0 && ((text[i] == ';' && (!sentence || split_sentences)) ||
          (text[i] == ',' && !sentence))))
      continue;
    Row row{normalize(text.substr(begin, i - begin)), ""};
    begin = i + 1;
    if (genders && !row.text.empty() && row.text.back() == ')') {
      auto open = row.text.rfind('(');
      if (open != std::string::npos) {
        row.gender = normalize(row.text.substr(open + 1, row.text.size() - open - 2));
        row.text = normalize(row.text.substr(0, open));
      }
    }
    if (genders && entry.type == "noun" && row.gender.empty()) {
      auto n = noun(row.text);
      if (n.gender) {
        row.gender = std::string(1, n.gender);
        row.gender_source = "article";
      }
    }
    if (!row.text.empty() || !row.gender.empty())
      rows.push_back(row);
  }
  return rows;
}
std::string answer_text(const std::vector<Row> &rows) {
  std::string result;
  for (const auto &row : rows) {
    if (!result.empty()) result += ", ";
    result += row.text;
    if (!row.gender.empty()) result += " (" + row.gender + ")";
  }
  return result;
}
Grade grade(const Entry &e, const std::string &direction,
            const std::vector<Row> &rows, const J &c) {
  auto target = units(e, direction);
  Grade result;
  std::set<std::string> direct, generated;
  bool forward = direction == "de_fr",
       inflected = forward && (e.type == "noun" || e.type == "adjective");
  auto norm = [&](const std::string &s) {
    auto result = normalize(s, !forward ? false : e.type == "other"
                                   ? c["sentence_case_sensitive"].boolean()
                                   : c["case_sensitive"].boolean(true),
                            e.type == "other" &&
                                c["ignore_terminal_punctuation_for_other"].boolean());
    if (!forward || (e.type == "other" && c["sentence_ignore_punctuation"].boolean(true))) {
      std::wstring clean;
      for (auto ch : wide(result)) {
        auto category = utf8proc_category(ch);
        if (category >= UTF8PROC_CATEGORY_PC && category <= UTF8PROC_CATEGORY_PO)
          continue;
        clean += ch;
      }
      result = normalize(utf8(clean));
    }
    return result;
  };
  if (!forward) {
    // All listed German groups/forms are alternatives. Compare complete text
    // first so punctuation inside a phrase cannot split a valid answer.
    for (size_t i = 0; i < rows.size(); ++i) {
      std::vector<std::string> candidates{rows[i].text};
      size_t start = 0;
      for (size_t end = 0; end <= rows[i].text.size(); ++end)
        if (end == rows[i].text.size() || rows[i].text[end] == ',' || rows[i].text[end] == ';') {
          candidates.push_back(rows[i].text.substr(start, end - start));
          start = end + 1;
        }
      bool matched = false;
      for (const auto &u : target)
        for (const auto &accepted : u.accept)
          for (const auto &candidate : candidates)
            if (!norm(candidate).empty() && norm(candidate) == norm(accepted)) {
              direct.insert(u.id);
              matched = true;
            }
      if (!matched && !normalize(rows[i].text).empty())
        result.errors.push(J{{"row", int(i + 1)}, {"kind", "spelling"},
                              {"message", "Unaccepted answer: " + rows[i].text}});
    }
    result.correct = !direct.empty();
    for (const auto &u : target) {
      if (direct.count(u.id)) result.explicit_units.push_back(u.id);
      else if (!result.correct) result.missing.push_back(u.id);
    }
    if (result.correct) result.errors = J::array();
    return result;
  }
  size_t expanded_count = 0;
  for (size_t index = 0; index < rows.size(); ++index) {
    auto error = [&](const std::string &kind, const std::string &message) {
      result.errors.push(J{{"row", int(index + 1)}, {"kind", kind}, {"message", message}});
    };
    // Preserve the submitted expression in history; expand only for matching.
    auto expressions = e.type == "other" ? std::vector<Row>{rows[index]}
                                         : parse_answer(e, direction, rows[index].text);
    for (auto expression : expressions) {
      if (!rows[index].gender.empty()) {
        if (!expression.gender.empty() && expression.gender_source != "article" &&
            expression.gender != rows[index].gender) {
          error("gender", "Conflicting gender annotations"); continue;
        }
        expression.gender = rows[index].gender;
      }
      bool expanded = e.type != "other" && c["inference"].str() != "none" &&
                      expression.text.find_first_of("{}/.") != std::string::npos;
      std::vector<std::string> values;
      try {
        values = expanded ? expand_text(expression.text) : std::vector<std::string>{expression.text};
      } catch (const std::exception &ex) {
        error("expansion", ex.what()); continue;
      }
      expanded_count += values.size();
      if (expanded_count > 1024) {
        error("expansion", "At most 1024 answers may be submitted at once");
        break;
      }
      for (const auto &value : values) {
        Row r{normalize(value), expression.gender, expression.gender_source};
        if (r.text.empty()) { error("expansion", "Expansion produced an empty answer"); continue; }
        // A brace alternative may carry its own gender annotation.
        if (inflected && r.text.back() == ')') {
          auto open = r.text.rfind('(');
          if (open != std::string::npos) {
            auto gender = normalize(r.text.substr(open + 1, r.text.size() - open - 2));
            if (!r.gender.empty() && r.gender != gender) {
              error("gender", "Conflicting gender annotations"); continue;
            }
            r.gender = gender;
            r.text = normalize(r.text.substr(0, open));
          }
        }
        if (r.gender == "fm") r.gender = "mf";
        if (inflected) {
          if (!r.gender.empty() && r.gender != "m" && r.gender != "f" && r.gender != "mf") {
            error("gender", "Gender must be m, f, or mf"); continue;
          }
          if (e.type == "noun") {
            auto n = noun(r.text);
            if (!n.valid || (n.gender && !r.gender.empty() &&
                r.gender.find(n.gender) == std::string::npos)) {
              error("gender", "Use a definite article and matching gender: " + r.text); continue;
            }
            if (n.gender) r.gender = std::string(1, n.gender);
            if (r.gender.empty()) {
              error("gender", "Add (m), (f), or (mf) for l' or les: " + r.text); continue;
            }
          }
        }
        if (expanded) result.expanded_answers.push(row_json(r));
        bool valid = true;
        std::set<std::string> matched_units;
        // Adjective spellings identify their listed forms; an optional explicit
        // gender constrains matching, but no grammatical form is manufactured.
        std::string claims = inflected && !r.gender.empty() ? r.gender : "_";
        for (char g : claims) {
          bool matched = false;
          for (const auto &u : target)
            if (g == '_' || u.gender == g)
              for (const auto &a : u.accept)
                if (norm(a) == norm(r.text)) {
                  matched = true;
                  matched_units.insert(u.id);
                }
          valid = valid && matched;
        }
        if (!valid) {
          std::string kind = "spelling";
          for (const auto &u : target)
            for (const auto &a : u.accept) {
              if (norm(a) == norm(r.text)) kind = "gender";
              else if (strip_accents(norm(a)) == strip_accents(norm(r.text))) kind = "accent";
            }
          error(kind, "Unaccepted answer: " + r.text);
          continue;
        }
        auto &coverage = expanded ? generated : direct;
        coverage.insert(matched_units.begin(), matched_units.end());
      }
    }
  }
  for (const auto &u : target) {
    if (direct.count(u.id))
      result.explicit_units.push_back(u.id);
    else if (generated.count(u.id))
      result.expanded_units.push_back(u.id);
    else
      result.missing.push_back(u.id);
  }
  result.correct = result.missing.empty() && result.errors.size() == 0;
  return result;
}
static J array_strings(const std::vector<std::string> &v) {
  J a = J::array();
  for (const auto &s : v)
    a.push(s);
  return a;
}
J Grade::json() const {
  return J{{"raw_correct", correct},
           {"effective_correct", correct},
           {"explicit_units", array_strings(explicit_units)},
           {"inferred_units", array_strings(inferred_units)},
           {"expanded_units", array_strings(expanded_units)},
           {"expanded_answers", expanded_answers},
           {"missing_units", array_strings(missing)},
           {"errors", errors}};
}
std::vector<Unit> display_units(const Entry &e, const std::string &direction) {
  auto result = units(e, direction);
  auto rank = [](const Unit &u) {
    if (u.slot == "m_sg") return 0;
    if (u.slot == "f_sg") return 1;
    if (u.slot == "m_pl") return 2;
    if (u.slot == "f_pl") return 3;
    if (u.slot == "infinitive") return 0;
    return 4;
  };
  // Keep required synonym groups in source order; sort forms within a group.
  for (auto first = result.begin(); first != result.end();) {
    auto last = first + 1;
    while (last != result.end() && last->group == first->group) ++last;
    std::stable_sort(first, last, [&](const Unit &a, const Unit &b) {
      return rank(a) < rank(b);
    });
    first = last;
  }
  return result;
}
std::vector<Row> canonical_rows(const Entry &e, const std::string &direction) {
  std::vector<Row> r;
  for (const auto &u : display_units(e, direction))
    r.push_back(
        {u.accept.front(), u.gender ? std::string(1, u.gender) : "", "manual"});
  return r;
}
std::vector<Row> sufficient_rows(const Entry &e, const std::string &direction,
                                 const J &config) {
  auto rows = canonical_rows(e, direction);
  if (direction == "fr_de") return {rows.front()};
  // Identical adjective forms can explicitly claim both genders, e.g. malade (mf).
  for (size_t i = 0; i < rows.size(); ++i)
    for (size_t j = rows.size(); j-- > i + 1;)
      if (rows[i].text == rows[j].text && !rows[i].gender.empty() && !rows[j].gender.empty()) {
        for (char gender : rows[j].gender)
          if (rows[i].gender.find(gender) == std::string::npos)
            rows[i].gender += gender;
        rows.erase(rows.begin() + j);
      }
  if (e.type == "adjective")
    for (auto &row : rows) row.gender.clear();
  if (e.type == "noun")
    for (auto &row : rows)
      if (noun(row.text).gender && row.gender.size() == 1)
        row.gender.clear();
  // Prefer the singular-first canonical order. The real grader decides
  // whether removing a form leaves a complete answer under the active policy.
  for (size_t i = rows.size(); i-- > 0;) {
    auto trial = rows;
    trial.erase(trial.begin() + i);
    if (grade(e, direction, trial, config).correct) rows = std::move(trial);
  }
  return rows;
}
std::string bundle(const Entry &e, const std::string &direction) {
  std::string out;
  for (const auto &u : display_units(e, direction)) {
    if (!out.empty())
      out += " / ";
    out += u.accept.front();
    if (u.gender)
      out += " (" + std::string(1, u.gender) + ")";
  }
  return out;
}
std::string requirements_label(const Entry &e, const std::string &direction) {
  auto us = units(e, direction);
  std::set<std::string> groups, slots;
  std::map<std::string, std::set<char>> genders;
  for (const auto &u : us) {
    groups.insert(u.group);
    if (u.gender)
      genders[u.group].insert(u.gender);
    slots.insert(e.type == "noun" && direction == "de_fr" ? u.slot.substr(2)
                                                          : u.slot);
  }
  std::string out = std::to_string(groups.size()) + " required word(s); ";
  const std::map<std::string, std::string> labels = {
      {"sg", "singular"},
      {"pl", "plural"},
      {"m_sg", "masculine singular"},
      {"f_sg", "feminine singular"},
      {"m_pl", "masculine plural"},
      {"f_pl", "feminine plural"},
      {"text", "text"},
      {"infinitive", "infinitive"}};
  bool first = true;
  for (const auto &s : slots) {
    if (!first)
      out += " / ";
    first = false;
    auto found = labels.find(s);
    out += found != labels.end()         ? found->second
           : s.rfind("present.", 0) == 0 ? "present: " + s.substr(8)
                                         : s;
  }
  if (e.type == "noun" && direction == "de_fr")
    for (const auto &[group, values] : genders)
      if (values.size() > 1) {
        out += "; both genders where applicable";
        break;
      }
  return out;
}
std::vector<size_t> distractors(const Deck &d, size_t index,
                                const std::string &direction, const J &c) {
  std::vector<size_t> out;
  std::set<std::string> seen{normalize(bundle(d.entries[index], direction))};
  auto shape = [&](const Entry &e) {
    std::map<std::string, std::vector<std::string>> g;
    for (const auto &u : units(e, direction))
      g[u.group].push_back(e.type == "noun" ? u.slot.substr(2) : u.slot);
    std::vector<std::vector<std::string>> s;
    for (auto &[k, v] : g) {
      std::sort(v.begin(), v.end());
      s.push_back(v);
    }
    std::sort(s.begin(), s.end());
    return s;
  };
  auto expected = shape(d.entries[index]);
  // Prefer matching answer shapes. If those cannot make a question, use
  // other valid answers of the same part of speech rather than fake words.
  for (int pass = 0; pass < 2; ++pass) {
    if (pass == 1 && out.size() >= 2) break;
    for (size_t i = 0; i < d.entries.size(); ++i) {
      if (i == index || d.entries[i].type != d.entries[index].type ||
          (pass == 0 && shape(d.entries[i]) != expected)) continue;
      auto display = normalize(bundle(d.entries[i], direction));
      if (!seen.insert(display).second) continue;
      if (grade(d.entries[index], direction, canonical_rows(d.entries[i], direction), c).correct) continue;
      out.push_back(i);
    }
  }
  return out;
}
J row_json(const Row &r) {
  return J{{"entered", r.text},
           {"text", normalize(r.text)},
           {"gender", r.gender},
           {"gender_source", r.gender_source}};
}
} // namespace fv
