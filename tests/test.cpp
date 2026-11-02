#include "progress.hpp"
#include <filesystem>
#include <iostream>
#include <unistd.h>
using namespace fv;
static int checks = 0;
static void check(bool value, const std::string &message) {
  ++checks;
  if (!value)
    throw std::runtime_error(message);
}
template <class F> static void rejects(F f, const std::string &message) {
  bool rejected = false;
  try {
    f();
  } catch (const std::exception &) {
    rejected = true;
  }
  check(rejected, message);
}
static const Entry &entry(const Deck &d, const std::string &id) {
  for (const auto &e : d.entries)
    if (e.id == id)
      return e;
  throw std::runtime_error("Missing fixture " + id);
}
static size_t index(const Deck &d, const std::string &id) {
  for (size_t i = 0; i < d.entries.size(); ++i)
    if (d.entries[i].id == id)
      return i;
  throw std::runtime_error("Missing fixture");
}
static std::string expand(const std::string &input) {
  std::wstring s;
  auto m = macros(defaults());
  size_t cursor = 0;
  for (auto ch : wide(input)) {
    s.insert(cursor++, 1, ch);
    expand_macro(s, cursor, m);
  }
  return utf8(s);
}
int main() {
  try {
    auto config = defaults();
    auto source = J::read("vocab/demo.json");
    auto d = load_deck(source);
    check(normalize("  l’ e\u0301te\u0301  ") == "l' été",
          "NFC, apostrophe and whitespace normalization");
    check(normalize("été") != "ete", "Accents must be preserved");
    check(expand("Noe:l") == "Noël", "Diaeresis after o");
    check(expand("Israe:l") == "Israël", "Diaeresis after a");
    check(expand("coe+ur") == "cœur", "Ligature + macro");
    check(expand("OE+ AE+ E+ c,") == "Œ Æ É ç", "Uppercase and cedilla macros");
    check(previous_grapheme(L"e\u0301", 2) == 0,
          "Delete whole decomposed grapheme");
    check(next_grapheme(L"e\u0301x", 0) == 2,
          "Move over whole decomposed grapheme");
    auto cat = entry(d, "cat"), eye = entry(d, "eye"),
         healthy = entry(d, "healthy"), ill = entry(d, "ill"),
         treat = entry(d, "treat");
    auto g = grade(cat, "de_fr", {{"le chat", ""}}, config);
    check(!g.correct && g.explicit_units.size() == 1 && g.inferred_units.empty(),
          "A listed plural is no longer inferred from a singular");
    check(!grade(cat, "de_fr", {{"les chats", "m"}}, config).correct,
          "A singular is no longer inferred from a plural");
    check(!grade(cat, "de_fr", {{"les chats", ""}}, config).correct,
          "Plural needs gender");
    check(!grade(cat, "de_fr", {{"le chat", "f"}}, config).correct,
          "Article cannot conflict with gender");
    check(!grade(cat, "de_fr", {{"les chats", "mf"}}, config).correct,
          "Cannot hedge gender using mf");
    check(!grade(cat, "de_fr", {{"chat", "m"}}, config).correct,
          "Nouns need articles");
    check(!grade(cat, "de_fr", {{"le chat", "m"}, {"le cola", "m"}}, config)
               .correct,
          "Incorrect extra rows must fail");
    check(grade(cat, "de_fr", {{"le chat", "m"}, {"le chat", "m"}, {"les chats", "m"}}, config)
              .correct,
          "Correct duplicate rows are harmless");
    check(!grade(eye, "de_fr", {{"l’œil", "m"}}, config).correct,
          "Irregular plural must be explicit");
    check(grade(eye, "de_fr", {{"l'œil", "m"}, {"les yeux", "m"}}, config)
              .correct,
          "Irregular forms plus gender");
    check(!grade(eye, "de_fr", {{"l'oeil", "m"}, {"les yeux", "m"}}, config)
               .correct,
          "No automatic ligature normalization");
    check(!grade(healthy, "de_fr", {{"sain", "m"}}, config).correct,
          "No automatic adjective endings");
    check(grade(healthy, "de_fr", {{"sain.e.s.es", ""}}, config).correct,
          "Explicit optional adjective suffixes cover the listed forms");
    check(!grade(ill, "de_fr", {{"malade", "m"}}, config).correct,
          "A gender annotation does not supply unentered adjective forms");
    check(grade(ill, "de_fr", {{"malade.s", "mf"}}, config).correct,
          "mf claims both unchanged adjective genders");
    check(
        !grade(entry(d, "body"), "de_fr", {{"le corps", "m"}}, config).correct,
        "Even unchanged noun plurals must be supplied when listed");
    check(grade(entry(d, "body"), "de_fr",
                {{"le corps", "m"}, {"les corps", "m"}}, config)
              .correct,
          "Explicit unchanged plural");
    check(!grade(treat, "de_fr", {{"traiter", ""}}, config).correct,
          "Must provide every synonym");
    check(grade(treat, "de_fr", {{"soigner", ""}, {"traiter", ""}}, config)
              .correct,
          "Synonym order does not matter");
    check(grade(cat, "fr_de", {{"Katze", ""}}, config).correct,
          "German alternatives are accepted");
    check(grade(eye, "fr_de", {{"das Auge", ""}}, config).correct,
          "One German singular form earns full credit");
    auto german = grade(eye, "fr_de", parse_answer(eye, "fr_de", "DIE, AUGEN!"), config);
    check(german.correct && german.missing.empty() && german.explicit_units.size() == 1,
          "One German plural ignores casing and internal punctuation, without missing alternatives");
    check(grade(eye, "fr_de", {{"AUGE", ""}, {"nonsense", ""}}, config).correct,
          "Any listed German form is sufficient");
    auto wrong_german = grade(eye, "fr_de", {{"nonsense", ""}}, config);
    auto punctuation_only = grade(eye, "fr_de", {{"!?", ""}}, config);
    check(!wrong_german.correct && !punctuation_only.correct,
          "Wrong or punctuation-only German answers still fail");
    check(expand("o: O: a: A: u: U:") == "ö Ö ä Ä ü Ü",
          "German umlaut macros support both cases");
    auto with_umlaut = grade(entry(d, "feel"), "fr_de", {{"SICH FÜHLEN!", ""}}, config);
    auto without_umlaut = grade(entry(d, "feel"), "fr_de", {{"sich fuhlen", ""}}, config);
    check(with_umlaut.correct && !without_umlaut.correct,
          "German ignores case and punctuation but retains umlauts");
    config["inference"] = "none";
    check(!grade(cat, "de_fr", {{"le chat", "m"}}, config).correct,
          "Explicit-only policy");
    config = defaults();
    auto bad = source;
    bad["entries"].push(bad["entries"].at(0));
    rejects([&] { load_deck(bad); }, "Duplicate IDs rejected");
    bad = source;
    bad["entries"].at(0)["fr"].at(0)["forms"]["m_sg"] = J::array({"la chat"});
    rejects([&] { load_deck(bad); }, "Invalid noun gender rejected");
    bad = source;
    bad["entries"].at(0)["fr"].push(bad["entries"].at(0)["fr"].at(0));
    bad["entries"].at(0)["fr"].at(1)["id"] = "another";
    rejects([&] { load_deck(bad); }, "Overlapping synonym groups rejected");
    rejects([] { J::parse("{\"x\":1} garbage"); },
            "JSON trailing garbage rejected");
    rejects([] { J::parse("{\"x\":1,}"); }, "Invalid JSON rejected");
    check(grade(cat, "de_fr", parse_answer(cat, "de_fr", "le chat (m), les chats (m)"), config).correct,
          "Single-bar explicit forms and gender");
    check(grade(cat, "de_fr", parse_answer(cat, "de_fr", "le/les chat.s(m)"), config).correct,
          "Single-bar pairwise article and suffix expansion");
    check(grade(eye, "de_fr", parse_answer(eye, "de_fr", "l'œil (m), les yeux (m)"), config).correct,
          "Single-bar irregular forms");
    check(!grade(eye, "de_fr", parse_answer(eye, "de_fr", "l'œil, les yeux"), config).correct,
          "Ambiguous articles still require gender");
    check(!grade(cat, "de_fr", parse_answer(cat, "de_fr", "le chat (f)"), config).correct,
          "Conflicting explicit gender fails");
    check(grade(healthy, "de_fr", parse_answer(healthy, "de_fr", "sain.e.s.es"), config).correct,
          "Single-bar adjective text expansion");
    check(grade(treat, "de_fr", parse_answer(treat, "de_fr", "soigner, traiter"), config).correct,
          "Single-bar required synonym groups");
    auto sentence_entry = entry(d, "feel-unwell");
    check(grade(sentence_entry, "de_fr", parse_answer(sentence_entry, "de_fr", "je ne me sens, pas bien;!"), config).correct,
          "Sentence casing and punctuation ignored by default");
    check(parse_answer(sentence_entry, "de_fr", "Oui, vraiment (bien).").size() == 1,
          "Sentence commas and parentheses remain literal input");
    auto strict = config;
    strict["sentence_case_sensitive"] = true;
    check(!grade(sentence_entry, "de_fr", {{"je ne me sens pas bien.", ""}}, strict).correct,
          "Sentence casing can be strict");
    strict = config;
    strict["sentence_ignore_punctuation"] = false;
    check(!grade(sentence_entry, "de_fr", {{"Je ne me sens pas bien", ""}}, strict).correct,
          "Sentence punctuation can be strict");
    check(hash(policy(strict).dump()) != hash(policy(config).dump()),
          "Sentence matching settings version grading evidence");
    check(answer_text(sufficient_rows(cat, "de_fr", config)) == "le chat, les chats (m)",
          "Suggested answer contains every listed noun form");
    check(sufficient_rows(healthy, "de_fr", config).size() == 4 &&
              grade(healthy, "de_fr", sufficient_rows(healthy, "de_fr", config), config).correct,
          "Every distinct listed adjective spelling is shown");
    check(answer_text(sufficient_rows(ill, "de_fr", config)) == "malade, malades",
          "Identical adjective spellings can be entered once");
    check(sufficient_rows(eye, "de_fr", config).size() == 2,
          "Irregular eye plural remains explicit");
    check(sufficient_rows(treat, "de_fr", config).size() == 2,
          "Every required French synonym is still represented");
    auto literal = config;
    literal["inference"] = "none";
    check(sufficient_rows(cat, "de_fr", literal).size() == 2,
          "Explicit-only settings retain required plural");
    for (const auto &word : d.entries)
      check(grade(word, "de_fr", sufficient_rows(word, "de_fr", config), config).correct,
            "Suggested answer must pass the actual grader: " + word.id);
    auto expansion_grade = [&](const Entry &word, const std::string &answer) {
      return grade(word, "de_fr", parse_answer(word, "de_fr", answer), config);
    };
    check(expand_text("hell{o,o,b}{,e,o}") == std::vector<std::string>({"hello", "helloe", "hellbo"}),
          "Alternatives align pairwise and duplicate positions are preserved");
    check(expand_text("le/la/les handicapé.e.es") ==
              std::vector<std::string>({"le handicapé", "la handicapée", "les handicapées"}),
          "Article and optional suffix alternatives align across spaces");
    check(expand_text("{,re}faire") == std::vector<std::string>({"faire", "refaire"}) &&
              expand_text(".re{faire}") == std::vector<std::string>({"faire", "refaire"}),
          "Optional prefixes work with braces or a dot before a prefix atom");
    check(expand_text("hell.o.oe.bo") == std::vector<std::string>({"hell", "hello", "helloe", "hellbo"}),
          "Dot alternatives include the unchanged base once");
    check(expand_text("{hel{lo,loe},hellbo}") == std::vector<std::string>({"hello", "helloe", "hellbo"}),
          "Nested braces expand within their alternative");
    rejects([] { expand_text("{a,b}{x,y,z}"); }, "Unequal alternative lengths are rejected");
    for (const auto &bad_expression : {"traiter / soigner", "traiter/ soigner", "traiter /soigner",
                                       "{hello", "hello}", "hello/", "hello.", "hello//world"})
      rejects([&] { expand_text(bad_expression); }, "Malformed expansion is rejected: " + std::string(bad_expression));
    rejects([] { expand_text(std::string(8193, 'a')); }, "Expression size is bounded");
    rejects([] { expand_text(std::string(17, '{') + "a" + std::string(17, '}')); }, "Nesting is bounded");
    std::string many = "{";
    for (int i = 0; i < 257; ++i) many += (i ? ",a" : "a");
    many += "}";
    rejects([&] { expand_text(many); }, "Alternative count is bounded");
    std::string wide_expression;
    for (int i = 0; i < 32; ++i) wide_expression += "{a,b}";
    auto two = expand_text(wide_expression);
    check(two == std::vector<std::string>({std::string(32, 'a'), std::string(32, 'b')}),
          "32 binary positions produce two answers, not a Cartesian explosion");
    auto hello = treat;
    hello.fr = {{"hello", "a", "infinitive", {"hello"}, 0},
                {"helloe", "b", "infinitive", {"helloe"}, 0},
                {"hellbo", "c", "infinitive", {"hellbo"}, 0}};
    for (const auto &answer : {"hello, helloe, hellbo", "{hello,helloe, hellbo}", "hell{o,oe,bo}",
                               "hell{o,o,b}{,e,o}", "hell{o,oe}, hellb{o}"})
      check(expansion_grade(hello, answer).correct, "User example accepted: " + std::string(answer));
    check(!expansion_grade(hello, "hell.o.oe.bo").correct,
          "Extra expanded base hell makes the dot example incorrect");
    check(!expansion_grade(hello, "{hello,helloe,hellbo,wrong}").correct,
          "Covering every requirement cannot hide an extra wrong answer");
    check(!expansion_grade(hello, "{hello,helloe}").correct, "Expansion still requires every synonym");
    for (const auto &answer : {"traiter/soigner", "traiter,soigner", "traiter, soigner"})
      check(expansion_grade(treat, answer).correct, "Synonym shorthand accepted: " + std::string(answer));
    auto spaced_slash = expansion_grade(treat, "traiter / soigner");
    check(!spaced_slash.correct && spaced_slash.errors.at(0)["kind"].str() == "expansion",
          "Spaced slash is a grading error, not an exception escaping the TUI");
    auto lesson = load_deck(J::read("vocab/lecon-6.json"));
    for (const auto &word : lesson.entries)
      check(grade(word, "de_fr", sufficient_rows(word, "de_fr", config), config).correct,
            "Listed lesson answers pass without grammatical inference: " + word.id);
    for (const auto &word : lesson.entries)
      for (const auto &form : word.fr)
        if (form.slot == "m_pl" || form.slot == "f_pl")
          check(word.id == "eye" || word.id == "brain" || word.id == "knee" ||
                    word.id == "skin" || word.id == "hospital" || word.id == "rescue-service",
                "Lesson plurals occur only in rows that explicitly list them");
    auto disabled_person = entry(lesson, "disabled-person");
    check(disabled_person.fr.size() == 2 &&
              expansion_grade(disabled_person, expand("le/la handicape+.e")).correct,
          "Listed male/female noun forms are covered by pairwise shorthand and acute macro");
    check(expansion_grade(disabled_person, "le/la handicapé.e(mf)").correct,
          "Shared mf annotation allows the article to select each expanded noun's gender");
    check(!expansion_grade(disabled_person, "le/la handicapé.e(m)").correct,
          "Expanded feminine article cannot be paired with a masculine-only annotation");
    auto with_plural = disabled_person;
    with_plural.fr.push_back({"plural-m", "disabled-person", "m_pl", {"les handicapées"}, 'm'});
    with_plural.fr.push_back({"plural-f", "disabled-person", "f_pl", {"les handicapées"}, 'f'});
    check(expansion_grade(with_plural, "le/la/les handicapé.e.es(mf)").correct,
          "Shared gender annotation applies to ambiguous expanded articles");
    auto singleton = cat;
    singleton.fr = {cat.fr[0].slot == "m_sg" ? cat.fr[0] : cat.fr[1]};
    check(expansion_grade(singleton, "le chat").correct &&
              !expansion_grade(singleton, "le/les chat.s(m)").correct,
          "Only the vocabulary's listed forms are required or accepted");
    check(expansion_grade(singleton, "le chat(mf)").correct &&
              expansion_grade(singleton, "{le chat}(mf)").correct,
          "Article gender behaves identically for literal and expanded answers");
    auto expanded_cat = expansion_grade(cat, "le/les chat.s(m)");
    check(expanded_cat.correct && expanded_cat.expanded_units.size() == 2 &&
              expanded_cat.inferred_units.empty() && expanded_cat.expanded_answers.size() == 2,
          "Progress distinguishes text expansion from old grammatical inference");
    check(expansion_grade(cat, "{le chat(m),les chats(m)}").correct,
          "Individual brace alternatives retain gender annotations");
    check(!expansion_grade(cat, "le/les chat.s").correct,
          "An expanded plural still needs gender");
    check(expand("le/la mange/manges E+ oe+ ae+") == "le/la mange/manges É œ æ",
          "Slash stays literal and the new acute macro preserves ligature macros");

    auto choices = distractors(d, index(d, "cat"), "de_fr", config);
    check(choices.size() >= 2, "Compatible distractors available");
    for (auto i : choices)
      check(!grade(cat, "de_fr", canonical_rows(d.entries[i], "de_fr"), config)
                 .correct,
            "Distractors cannot pass grader");
    char temp[] = "/tmp/fvoci-test-XXXXXX";
    auto dir = mkdtemp(temp);
    check(dir, "Create temporary profile directory");
    std::string root = dir;
    int64_t clock = 1800000000;
    config["mode"] = "text";
    config["direction_weights"] = J{{"de_fr", 100}, {"fr_de", 0}};
    auto submit_answer = [&](Engine &engine, const Deck &deck, const Task &t, bool correct = true) {
      auto rows = correct ? canonical_rows(deck.entries[t.entry], t.direction) : std::vector<Row>{};
      auto g = grade(deck.entries[t.entry], t.direction, rows, engine.config());
      if (t.mode == "multiple_choice") { rows.clear(); g = Grade{}; g.correct = correct; }
      return engine.submit(t, rows, g, 100, false, 0);
    };
    auto record = [&](Progress &p, const Deck &deck, Engine &engine, size_t i) -> J & {
      return p.data["learning"][deck.id][engine.policy_id()]["words"][deck.entries[i].id];
    };
    J pending;
    {
      Progress p(root + "/progress.json", true);
      Engine e(p, d, config, [&] { return clock; }, 42);
      auto t = e.next();
      check(t && t->entry == 0 && t->mode == "text", "Text-only sweep begins with a word");
      pending = t->json();
      e.save_draft({{"le cha", "", "bar"}});
      rejects([&] { Progress second(root + "/progress.json", false); }, "Exclusive profile locking");
    }
    {
      Progress p(root + "/progress.json", false);
      Engine e(p, d, config, [&] { return clock + 100000000; }, 7);
      auto t = e.next();
      check(t && t->json().dump() == pending.dump(), "Pending question survives restart and elapsed time");
      check(e.draft().front().text == "le cha", "Draft input resumes");
      submit_answer(e, d, *t, false);
      check(record(p, d, e, t->entry)["score"].real() == 1.0, "Written mistake adds 1.0");
      auto gap = e.session()["sweep"]["items"][d.entries[t->entry].id]["next_question"].num() - 2;
      check(gap >= 3 && gap <= 10, "Retry gap is 3–10 intervening questions");
      auto next = e.next();
      check(next && next->entry != t->entry, "Failed word waits while other words are available");
      auto score_before = record(p, d, e, next->entry)["score"].real();
      e.skip(*next);
      check(record(p, d, e, next->entry)["score"].real() == score_before, "Skipping does not change difficulty");
      Task sentence;
      sentence.entry = index(d, "feel-unwell");
      auto event = submit_answer(e, d, sentence, false);
      e.override_sentence(event);
      check(!p.data["history"].at(event)["raw_correct"].boolean() &&
                p.data["history"].at(event)["effective_correct"].boolean(), "Override preserves raw mismatch");
      check(record(p, d, e, sentence.entry)["score"].real() == 0 &&
                e.session()["sweep"]["items"][d.entries[sentence.entry].id]["stage"].str() == "done",
            "Sentence override rolls back mistake score and completes the sweep item");
      check(e.state(sentence)["units"]["sentence.text"]["failed"].num() == 1,
            "Override preserves raw per-form errors");
      rejects([&] { e.override_sentence(event); }, "Cannot override twice");
    }
    {
      auto changed = d;
      changed.entries[0].snapshot["de"]["prompt"] = "Katze (Tier)";
      Progress p(root + "/progress.json", false);
      Engine e(p, changed, config, [&] { return clock; }, 3);
      check(record(p, changed, e, 0)["revision"].num() == 2 &&
                record(p, changed, e, 0)["score"].real() == 0,
            "Changed requirements reset only revised word learning evidence");
      check(p.data["history"].size() > 0, "Revision preserves history");
    }
    Deck nouns = d;
    nouns.entries.clear();
    for (const auto &word : d.entries)
      if (word.type == "noun" && distractors(d, index(d, word.id), "de_fr", config).size() >= 2)
        nouns.entries.push_back(word);
    {
      auto adaptive = defaults();
      adaptive["direction_weights"] = J{{"de_fr", 100}, {"fr_de", 0}};
      Progress p(root + "/sweeps.json", true);
      Engine e(p, nouns, adaptive, [&] { return clock; }, 23);
      std::map<std::string, int> choices, written;
      bool missed_choice = false, missed_text = false;
      int steps = 0;
      for (; steps < 200; ++steps) {
        auto t = e.next();
        check(bool(t), "Sweep question available without advancing the clock");
        auto sweep = e.session()["sweep_number"].num();
        if (sweep == 3) break;
        auto key = std::to_string(sweep) + ":" + nouns.entries[t->entry].id;
        check(written[key] == 0, "Learned word is not repeated within introductory sweep");
        bool correct = true;
        if (sweep == 1 && t->entry == 0 && t->mode == "multiple_choice" && !missed_choice) {
          correct = false; missed_choice = true;
        } else if (sweep == 1 && t->entry == 0 && t->mode == "text" && !missed_text) {
          correct = false; missed_text = true;
        }
        if (t->mode == "text") check(choices[key] == 1, "Written stage follows recognition success");
        submit_answer(e, nouns, *t, correct);
        if (correct) (t->mode == "text" ? written : choices)[key]++;
      }
      check(steps < 200 && missed_choice && missed_text, "Two complete sweeps finish despite retries");
      check(sweep_overview(p.data, nouns, adaptive)["full_sweeps"].num() == 2,
            "Full coverage is counted only at sweep completion");
      check(record(p, nouns, e, 0)["score"].real() == 1.5,
            "Initial score combines written and recognition mistakes with different weights");
      check(e.session()["sweep"]["mode"].str() == "difficult_random", "Default switches to difficult + random after two passes");
      check(e.session()["sweep"]["items"].size() == 2, "One difficult word receives one sampled easier word");
      auto t = e.next();
      check(t->mode == "text" && e.session()["sweep"]["items"][nouns.entries[0].id]["remaining"].num() == 2,
            "Moderate difficulty uses two written successes without recognition");
      while (e.session()["sweep_number"].num() == 3) {
        t = e.next();
        if (e.session()["sweep_number"].num() != 3) break;
        check(t->mode == "text", "Post-introductory easy/moderate words use writing");
        submit_answer(e, nouns, *t);
      }
      check(record(p, nouns, e, 0)["score"].real() == 1.0, "Clean later sweep reduces score by 0.5");
      // Pin exact scores to test selection boundaries and support stages.
      record(p, nouns, e, 0)["score"] = 3.0;
      record(p, nouns, e, 1)["score"] = 1.5;
      auto overview = sweep_overview(p.data, nouns, adaptive);
      check(overview["difficult"].num() == 2 && overview["very_difficult"].num() == 1,
            "Menu counts use floating-point threshold comparisons");
      adaptive["sweep"] = "very_difficult";
      Engine hard(p, nouns, adaptive, [&] { return clock; }, 9);
      check(hard.session()["sweep"]["items"].size() == 1 && hard.next()->mode == "multiple_choice",
            "Very difficult selection includes recognition support for score 3");
      check(hard.session()["sweep"]["items"][nouns.entries[0].id]["remaining"].num() == 3,
            "Very difficult words need three written successes");
      adaptive["sweep"] = "difficult";
      Engine difficult(p, nouns, adaptive, [&] { return clock; }, 9);
      check(difficult.session()["sweep"]["items"].size() == 2, "Difficult sweep excludes below-threshold words");
    }
    {
      auto adaptive = defaults(); adaptive["direction_weights"] = J{{"de_fr", 100}, {"fr_de", 0}};
      Progress p(root + "/resume-sweep.json", true);
      Engine e(p, nouns, adaptive, [&] { return clock; }, 99);
      auto t = e.next(); submit_answer(e, nouns, *t, false);
      auto snapshot = e.session()["sweep"].dump();
      auto expected = e.next()->json().dump();
      Engine resumed(p, nouns, adaptive, [&] { return clock + 8640000; }, 1);
      check(resumed.session()["sweep"].dump() == snapshot && resumed.next()->json().dump() == expected,
            "Sweep stage, retry positions, and pending options survive resume");
    }
    {
      auto c = config;
      c["sweep"] = "full";
      c["random_order_max_score"] = 0.5;
      Progress p(root + "/random-threshold.json", true);
      Engine e(p, d, c, [&] { return clock; }, 31);
      check(!e.session()["sweep"]["random_order"].boolean(),
            "Unseen zero-score words do not unlock random order");
      for (size_t i = 0; i < d.entries.size(); ++i) record(p, d, e, i)["full_sweeps"] = 1;
      auto t = e.next();
      check(!e.session()["sweep"]["random_order"].boolean(), "One full pass is not enough to unlock random order");
      e.skip(*t);
      for (size_t i = 0; i < d.entries.size(); ++i) {
        record(p, d, e, i)["full_sweeps"] = 2;
        record(p, d, e, i)["score"] = 0.75;
      }
      t = e.next();
      check(!e.session()["sweep"]["random_order"].boolean(), "Average above the configured threshold keeps introductory ordering");
      e.skip(*t);
      for (size_t i = 0; i < d.entries.size(); ++i) record(p, d, e, i)["score"] = 0.5;
      t = e.next();
      check(e.session()["sweep"]["random_order"].boolean(), "Exact average threshold unlocks random order");
      e.skip(*t);
      for (size_t i = 0; i < d.entries.size(); ++i) record(p, d, e, i)["score"] = 3.0;
      e.next();
      check(e.session()["sweep"]["random_order"].boolean(), "Random ordering remains unlocked after later mistakes");
      for (double bad : {-0.01, 10.01}) {
        c["random_order_max_score"] = bad;
        rejects([&] { validate_config(c); }, "Random ordering threshold must be within score range");
      }
    }
    {
      auto c = config; c["sweep"] = "full";
      Progress p(root + "/random-order.json", true);
      Engine initial(p, d, c, [&] { return clock; }, 8);
      for (size_t i = 0; i < d.entries.size(); ++i) {
        record(p, d, initial, i)["full_sweeps"] = 2;
        record(p, d, initial, i)["score"] = 0.5;
      }
      initial.finish();
      auto prepared = p.data;
      Engine e(p, d, c, [&] { return clock; }, 31);
      std::vector<size_t> order;
      for (int i = 0; i < 3; ++i) {
        auto t = e.next(); order.push_back(t->entry); submit_answer(e, d, *t);
      }
      auto pending = e.next()->json().dump();
      e.save_draft({{"draft expansion", "", "bar"}});
      auto checkpoint = p.data;
      while (order.size() < d.entries.size()) {
        auto t = e.next();
        check(e.session()["sweep_number"].num() == 1, "Random full sweep covers every word before restarting");
        order.push_back(t->entry); submit_answer(e, d, *t);
      }
      check(std::set<size_t>(order.begin(), order.end()).size() == d.entries.size(),
            "Random order never repeats a completed word within the sweep");
      bool source_order = true;
      for (size_t i = 0; i < order.size(); ++i) source_order = source_order && order[i] == i;
      check(!source_order, "Well-learned full sweep no longer follows vocabulary file order");
      p.data = checkpoint;
      Engine resumed(p, d, c, [&] { return clock + 9999999; }, 123);
      check(resumed.next()->json().dump() == pending && resumed.draft().front().text == "draft expansion",
            "Random order preserves the pending question and draft on resume");
      for (size_t i = 3; i < order.size(); ++i) {
        auto t = resumed.next();
        check(t->entry == order[i], "Resume restores the future random sequence, independently of clock or seed");
        submit_answer(resumed, d, *t);
      }
      // A ready retry must compete with all other eligible words, not jump
      // deterministically to the front just because it has an old retry slot.
      std::set<size_t> selected;
      for (uint32_t seed = 1; seed <= 8; ++seed) {
        p.data = prepared;
        Engine random(p, d, c, [&] { return clock; }, seed);
        auto &items = random.session()["sweep"]["items"];
        items[d.entries[0].id]["next_question"] = 1;
        items[d.entries[1].id]["next_question"] = 99;
        auto t = random.next();
        selected.insert(t->entry);
        check(t->entry != 1, "Random ordering respects the retry gap while eligible alternatives remain");
      }
      check(selected.size() > 1, "Random ordering chooses among fresh words and ready retries without fixed priority");
      p.data = prepared;
      // One difficult word does not block random ordering when the deck's
      // average is good enough; selection still uses the chosen sweep pool.
      for (auto &[id, word] : p.data["learning"][d.id][e.policy_id()]["words"].object_items()) word["score"] = 0.0;
      record(p, d, e, 0)["score"] = 10.0;
      Engine average(p, d, c, [&] { return clock; }, 1);
      check(average.session()["sweep"]["random_order"].boolean(), "Unlock uses the whole-deck mean, not the hardest word");
      p.data = prepared;
      c.object_items().erase("random_order_max_score");
      Engine old_settings(p, d, c, [&] { return clock; }, 17);
      auto old_pending = old_settings.next()->json().dump();
      auto old_session_id = old_settings.session()["id"].str();
      c["random_order_max_score"] = 1.0;
      Engine upgraded(p, d, c, [&] { return clock; }, 99);
      check(upgraded.session()["id"].str() == old_session_id && upgraded.next()->json().dump() == old_pending,
            "Adding the default threshold does not restart older saved sessions");
    }
    {
      auto adaptive = defaults(); adaptive["direction_weights"] = J{{"de_fr", 100}, {"fr_de", 0}};
      Progress pa(root + "/time-a.json", true), pb(root + "/time-b.json", true);
      Engine a(pa, nouns, adaptive, [&] { return clock; }, 17);
      Engine b(pb, nouns, adaptive, [&] { return clock + 999999999; }, 17);
      for (int i = 0; i < 40; ++i) {
        auto ta = a.next(), tb = b.next();
        check(ta->json().dump() == tb->json().dump(), "Question selection is independent of elapsed time");
        submit_answer(a, nouns, *ta, i % 7 != 0); submit_answer(b, nouns, *tb, i % 7 != 0);
      }
    }
    {
      auto mixed = defaults(); mixed["mode"] = "text";
      Progress p(root + "/mixed.json", true);
      Engine e(p, d, mixed, [&] { return clock; }, 11, true);
      int forward = 0, reverse = 0;
      for (int i = 0; i < 10; ++i) {
        auto t = e.next(); check(bool(t), "Mock question available");
        (t->direction == "de_fr" ? forward : reverse)++;
        submit_answer(e, d, *t);
      }
      check(forward == 9 && reverse == 1, "90/10 shuffled mock direction bag");
      for (size_t i = 10; i < d.entries.size(); ++i) submit_answer(e, d, *e.next());
      check(!e.next() && sweep_overview(p.data, d, mixed)["full_sweeps"].num() == 0,
            "Mock tests end after one pass and do not count as introductory sweeps");
    }
    {
      Progress p(root + "/legacy.json", true);
      Engine initial(p, d, config, [&] { return clock; }, 3);
      auto pid = initial.policy_id();
      for (auto mode : {"text", "multiple_choice"})
        p.data["history"].push(J{{"event", "attempt"}, {"deck_id", d.id}, {"entry_id", d.entries[0].id},
            {"entry_revision", 1}, {"policy_id", pid}, {"mode", mode},
            {"raw_correct", false}, {"effective_correct", false}});
      p.data.object_items().erase("learning");
      p.data.object_items().erase("session");
      auto legacy = config;
      legacy["new_entries_per_day"] = 0;
      legacy["retry_after_seconds"] = 999999999;
      for (auto &old : p.data["schedules"].items()) old["due_at"] = "2099-01-01T00:00:00Z";
      Engine migrated(p, d, legacy, [&] { return clock; }, 3);
      check(record(p, d, migrated, 0)["score"].real() == 1.5 &&
                record(p, d, migrated, 0)["full_sweeps"].num() == 0,
            "Legacy errors seed weighted scores without inventing completed sweeps");
      check(migrated.next().has_value() && p.data["history"].size() == 2,
            "Legacy quotas/due dates cannot block practice; history survives migration");
    }
    {
      auto selected = config;
      Progress p(root + "/selection.json", true);
      Engine initial(p, d, selected, [&] { return clock; }, 3);
      for (size_t i = 0; i < d.entries.size(); ++i) record(p, d, initial, i)["full_sweeps"] = 2;
      selected["sweep"] = "difficult";
      Engine empty(p, d, selected, [&] { return clock; }, 3);
      check(!empty.next(), "Empty difficulty-only sweep does not silently ask other words");
      selected["sweep"] = "very_difficult_random";
      Engine random(p, d, selected, [&] { return clock; }, 3);
      check(random.session()["sweep"]["items"].size() == 3 && random.next().has_value(),
            "Random-only pool samples 20 percent when no hard words exist");
      for (const auto &[id, item] : random.session()["sweep"]["items"].object_items())
        check(item["sampled"].boolean(), "Saved sweep identifies random selections");
      record(p, d, random, 0)["score"] = 1.0;
      p.data["learning"][d.id][random.policy_id()]["completed_sweeps"] = 5;
      check(sweep_overview(p.data, d, selected)["default_mode"].str() == "full",
            "Default periodically checks the entire vocabulary");
    }
    {
      Progress p(root + "/german-alternatives.json", true);
      Engine e(p, d, config, [&] { return clock; }, 12);
      Task t; t.entry = index(d, "eye"); t.direction = "fr_de";
      auto rows = parse_answer(eye, "fr_de", "AUGEN!");
      e.submit(t, rows, grade(eye, t.direction, rows, config), 100);
      check(e.state(t)["units"]["plural"]["explicit"].num() == 1 &&
                e.state(t)["units"]["singular"]["attempts"].num() == 0,
            "Unanswered German alternatives do not accumulate failures or fabricated successes");
      check(e.accuracy().correct == 1, "One German alternative counts as full credit in accuracy");
    }
    {
      Deck small = d; small.entries = {cat};
      Progress p(root + "/unlimited.json", true);
      Engine e(p, small, config, [&] { return clock; }, 3);
      for (int i = 0; i < 25; ++i) {
        auto t = e.next(); check(t && t->mode == "text", "Tiny decks continue without clock waits");
        submit_answer(e, small, *t);
      }
      e.next();
      check(record(p, small, e, 0)["full_sweeps"].num() == 25,
            "Unlimited practice starts new sweeps without reasking finished words in old sweep");
    }
    {
      Progress p(root + "/accuracy.json", true);
      Engine e(p, d, config, [&] { return clock; }, 123);
      check(e.accuracy().total == d.entries.size() &&
                e.accuracy().tested == 0 && e.accuracy().percent() == 0,
            "Untested words stay in the accuracy denominator");
      Task cat_task;
      cat_task.entry = index(d, "cat");
      auto rows = canonical_rows(cat, "de_fr");
      auto correct_grade = grade(cat, "de_fr", rows, config);
      for (int i = 0; i < 25; ++i)
        e.submit(cat_task, rows, correct_grade, 100);
      check(e.accuracy().correct == 1 && e.accuracy().tested == 1,
            "Repeated successes count as one word, without a rolling window");
      check(e.accuracy().percent() > 8.3 && e.accuracy().percent() < 8.4,
            "Accuracy divides latest-correct words by all deck entries");
      e.submit(cat_task, {}, grade(cat, "de_fr", {}, config), 100);
      check(e.accuracy().correct == 0 && e.accuracy().tested == 1,
            "Latest written failure replaces an earlier success");
      cat_task.direction = "fr_de";
      auto german = canonical_rows(cat, "fr_de");
      e.submit(cat_task, german, grade(cat, "fr_de", german, config), 100);
      check(e.accuracy().correct == 1 && e.accuracy().tested == 1,
            "Latest written attempt in either direction determines the word");
      auto choice = cat_task;
      choice.mode = "multiple_choice";
      Grade wrong;
      e.submit(choice, {}, wrong, 100);
      check(e.accuracy().correct == 1,
            "A multiple-choice failure never changes written accuracy");
      e.skip(cat_task);
      check(e.accuracy().correct == 1,
            "Skipping does not replace a written result");
      Task sentence;
      sentence.entry = index(d, "feel-unwell");
      auto n =
          e.submit(sentence, {},
                   grade(d.entries[sentence.entry], "de_fr", {}, config), 100);
      check(e.accuracy().tested == 2 && e.accuracy().correct == 1,
            "A new failed word increases tested count, not correct count");
      e.override_sentence(n);
      check(e.accuracy().correct == 2 && e.accuracy().overrides == 1,
            "Accepted sentence typo updates the live accuracy");
      cat_task.direction = "de_fr";
      e.submit(cat_task, rows, correct_grade, 100, true);
      check(e.accuracy().correct == 1,
            "Revealing an answer is a failed latest written attempt");
      auto changed = d;
      changed.entries[sentence.entry].snapshot["de"]["prompt"] =
          "Changed requirement";
      auto changed_score = written_accuracy(p.data, changed, e.policy_id());
      check(changed_score.correct == 0 && changed_score.tested == 1 &&
                changed_score.total == d.entries.size(),
            "Changed vocabulary invalidates only that word's old result");
      auto stricter = config;
      stricter["inference"] = "none";
      check(written_accuracy(p.data, d, hash(policy(stricter).dump())).tested ==
                0,
            "Accuracy does not mix incompatible grading policies");
      auto removed = d;
      removed.entries.erase(removed.entries.begin() + sentence.entry);
      check(written_accuracy(p.data, removed, e.policy_id()).correct == 0 &&
                written_accuracy(p.data, removed, e.policy_id()).total ==
                    d.entries.size() - 1,
            "Removed entries leave both numerator and denominator");
    }
    {
      Progress p(root + "/accuracy.json", false);
      Engine e(p, d, config, [&] { return clock; }, 124);
      check(e.accuracy().correct == 1 && e.accuracy().tested == 2,
            "Latest-word accuracy survives saving and reopening");
    }
    check(std::filesystem::exists(root + "/progress.json.bak"),
          "Atomic saves retain backup");
    std::filesystem::remove_all(root);
    std::cout << "Passed " << checks
              << " core, grading, persistence, and scheduling checks.\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << "TEST FAILED: " << e.what() << "\n";
    return 1;
  }
}
