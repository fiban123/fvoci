#pragma once
#include "json.hpp"
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace fv {
std::string normalize(const std::string &, bool sensitive = true,
                      bool terminal_punctuation = false);
std::wstring wide(const std::string &);
std::string utf8(const std::wstring &);
std::string hash(const std::string &);
std::string stamp(int64_t);
int64_t unstamp(const J &);
int64_t now_seconds();
std::string local_day(int64_t);
J defaults();
void validate_config(const J &);
J policy(const J &);
std::map<std::wstring, std::wstring> macros(const J &);
bool expand_macro(std::wstring &, size_t &,
                  const std::map<std::wstring, std::wstring> &);
size_t previous_grapheme(const std::wstring &, size_t);
size_t next_grapheme(const std::wstring &, size_t);

struct Unit {
  std::string id, group, slot;
  std::vector<std::string> accept;
  char gender = 0;
};
struct Entry {
  std::string id, type, prompt;
  bool override_allowed = false;
  std::vector<Unit> fr, de;
  J snapshot;
};
struct Deck {
  std::string id, title, test_date;
  int revision = 1;
  std::vector<Entry> entries;
};
Deck load_deck(const J &);
struct Row {
  std::string text, gender, gender_source = "manual";
};
std::vector<Row> parse_answer(const Entry &, const std::string &direction,
                              const std::string &text);
std::string answer_text(const std::vector<Row> &);
struct Noun {
  std::string article, body;
  char gender = 0;
  bool plural = false, valid = false;
};
Noun noun(const std::string &);
// Alternatives align by index; scalar text repeats across all alternatives.
std::vector<std::string> expand_text(const std::string &);
struct Grade {
  bool correct = false;
  std::vector<std::string> explicit_units, expanded_units, inferred_units, missing;
  J expanded_answers = J::array();
  J errors = J::array();
  J json() const;
};
std::vector<Unit> units(const Entry &, const std::string &direction);
Grade grade(const Entry &, const std::string &, const std::vector<Row> &,
            const J &);
std::vector<Unit> display_units(const Entry &, const std::string &direction);
std::vector<Row> canonical_rows(const Entry &, const std::string &);
std::vector<Row> sufficient_rows(const Entry &, const std::string &, const J &);
std::string bundle(const Entry &, const std::string &);
std::string requirements_label(const Entry &, const std::string &);
std::vector<size_t> distractors(const Deck &, size_t, const std::string &,
                                const J &);
J row_json(const Row &);
} // namespace fv
