#include "ui.hpp"
#include <clocale>
#include <filesystem>
#include <iostream>

namespace fs = std::filesystem;
using namespace fv;
static void merge_settings(J &target, const J &source) {
  for (const auto &[key, value] : source.object_items()) {
    if (value.is_object() && target[key].is_object())
      merge_settings(target[key], value);
    else
      target[key] = value;
  }
}
static void usage() {
  std::cout
      << "fvoci — offline German/French vocabulary trainer\n\n"
         "Usage: fvoci [--vocab FILE] [--progress FILE] [options]\n"
         "       fvoci --validate FILE\n"
         "       fvoci --vocab FILE --entry ID --grade ANSWERS.json "
         "[--direction de_fr|fr_de]\n\n"
         "  --mode adaptive|text|multiple_choice\n"
         "  --direction de_fr|fr_de|mixed    Practice direction (default "
         "90:10)\n"
         "  --questions N                  Optional question limit (0 = unlimited)\n"
         "  --sweep default|full|difficult|very_difficult|difficult_random|very_difficult_random\n"
         "  --no-expansion                 Disable answer text expansion\n"
         "  --no-macros                    Disable accent input macros\n"
         "  --mock                         Open a written mock test\n"
         "  --config FILE                  Use a particular settings file\n"
         "  --dump-config                  Print effective settings as JSON\n"
         "  --seed N                       Reproducible new-session ordering\n"
         "  --version                      Print version\n\n"
         "Answer files are arrays: [{\"text\":\"le chat\",\"gender\":\"m\"}].\n"
         "--grade exits 0 for correct, 1 for incorrect, 2 for invalid input.\n"
         "New --progress paths are created on Start; existing files are "
         "preserved.\n";
}
int main(int argc, char **argv) {
  setlocale(LC_ALL, "");
  try {
    Options options;
    const char *xdg = getenv("XDG_CONFIG_HOME");
    const char *home = getenv("HOME");
    options.config_path = xdg ? std::string(xdg) + "/fvoci/config.json"
                          : home
                              ? std::string(home) + "/.config/fvoci/config.json"
                              : "";
    for (int i = 1; i < argc; ++i) {
      std::string a = argv[i];
      if (a == "--help" || a == "-h") {
        usage();
        return 0;
      }
      if (a == "--version") {
        std::cout << "fvoci 0.1.0\n";
        return 0;
      }
      if (a == "--config") {
        if (++i == argc)
          throw std::runtime_error("--config requires a filename");
        options.config_path = argv[i];
      }
    }
    options.config = defaults();
    if (!options.config_path.empty() && fs::exists(options.config_path)) {
      auto saved = J::read(options.config_path);
      merge_settings(options.config, saved);
    }
    // Old wall-clock learning settings no longer participate in scheduling.
    for (auto key : {"new_entries_per_day", "retry_after_seconds", "retry_after_questions",
                     "review_intervals_seconds", "test_date"})
      options.config.object_items().erase(key);
    if (options.config["inference"].str() == "naive-v1")
      options.config["inference"] = "text-expansion-v1";
    // Question limits are per-run: legacy saved defaults must not end practice.
    options.config["session_questions"] = 0;
    std::string validate, grade_file, entry_id, direction = "de_fr";
    bool dump = false;
    for (int i = 1; i < argc; ++i) {
      std::string a = argv[i];
      auto value = [&]() {
        if (++i == argc)
          throw std::runtime_error(a + " requires a value");
        return std::string(argv[i]);
      };
      auto number = [&]() {
        auto s = value();
        size_t end = 0;
        int64_t n = std::stoll(s, &end);
        if (end != s.size())
          throw std::runtime_error("Invalid integer for " + a);
        return n;
      };
      if (a == "--config")
        value();
      else if (a == "--vocab")
        options.vocab = value();
      else if (a == "--progress") {
        options.progress = value();
        options.create_progress = true;
      } else if (a == "--validate")
        validate = value();
      else if (a == "--grade")
        grade_file = value();
      else if (a == "--entry")
        entry_id = value();
      else if (a == "--mode")
        options.config["mode"] = value();
      else if (a == "--direction") {
        auto d = value();
        if (d != "de_fr" && d != "fr_de" && d != "mixed")
          throw std::runtime_error("Unknown direction");
        direction = d == "mixed" ? "de_fr" : d;
        options.config["direction_weights"] = J{{"de_fr", d == "mixed"   ? 90
                                                          : d == "de_fr" ? 100
                                                                         : 0},
                                                {"fr_de", d == "mixed"   ? 10
                                                          : d == "fr_de" ? 100
                                                                         : 0}};
      } else if (a == "--questions")
        options.config["session_questions"] = number();
      else if (a == "--sweep")
        options.config["sweep"] = value();
      else if (a == "--seed")
        options.seed = number();
      else if (a == "--no-expansion" || a == "--no-inference")
        options.config["inference"] = "none";
      else if (a == "--no-macros")
        options.config["accent_macros_enabled"] = false;
      else if (a == "--mock")
        options.mock = true;
      else if (a == "--dump-config")
        dump = true;
      else
        throw std::runtime_error("Unknown argument: " + a);
    }
    validate_config(options.config);
    if (dump) {
      std::cout << options.config.dump(true) << '\n';
      return 0;
    }
    if (!validate.empty()) {
      auto deck = load_deck(J::read(validate));
      std::cout << "Valid: " << deck.title << " (" << deck.entries.size()
                << " entries)\n";
      return 0;
    }
    if (!grade_file.empty()) {
      if (options.vocab.empty() || entry_id.empty())
        throw std::runtime_error("--grade needs --vocab and --entry");
      auto deck = load_deck(J::read(options.vocab));
      auto answers = J::read(grade_file);
      std::vector<Row> rows;
      for (const auto &r : answers.items())
        rows.push_back({r["text"].str(), r["gender"].str(), "manual"});
      for (const auto &e : deck.entries)
        if (e.id == entry_id) {
          auto g = grade(e, direction, rows, options.config);
          std::cout << g.json().dump(true) << '\n';
          return g.correct ? 0 : 1;
        }
      throw std::runtime_error("Unknown entry ID: " + entry_id);
    }
    return run_ui(std::move(options));
  } catch (const std::exception &e) {
    std::cerr << "fvoci: " << e.what() << '\n';
    return 2;
  }
}
