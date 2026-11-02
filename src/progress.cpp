#include "progress.hpp"
#include <algorithm>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <numeric>
#include <cmath>
#include <limits>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

namespace fv {
namespace fs = std::filesystem;
static void system_error(const std::string &what) {
  throw std::runtime_error(what + ": " + std::strerror(errno));
}
void atomic_write(const std::string &path, const std::string &text) {
  fs::path p = fs::absolute(path);
  std::string temp = p.string() + ".tmp.XXXXXX";
  std::vector<char> name(temp.begin(), temp.end());
  name.push_back(0);
  int fd = mkstemp(name.data());
  if (fd < 0)
    system_error("Cannot create temporary file for " + path);
  try {
    size_t pos = 0;
    while (pos < text.size()) {
      auto n = write(fd, text.data() + pos, text.size() - pos);
      if (n < 0) {
        if (errno == EINTR)
          continue;
        system_error("Cannot write " + path);
      }
      pos += n;
    }
    if (fsync(fd) < 0)
      system_error("Cannot flush " + path);
    if (close(fd) < 0) {
      fd = -1;
      system_error("Cannot close " + path);
    }
    fd = -1;
    if (rename(name.data(), p.c_str()) < 0)
      system_error("Cannot replace " + path);
    int dir = open(p.parent_path().c_str(), O_RDONLY | O_DIRECTORY);
    if (dir >= 0) {
      fsync(dir);
      close(dir);
    }
  } catch (...) {
    if (fd >= 0)
      close(fd);
    unlink(name.data());
    throw;
  }
}
Progress::Progress(const std::string &path, bool create) {
  if (path.empty())
    throw std::runtime_error("Choose a progress file");
  path_ = fs::weakly_canonical(fs::absolute(path)).string();
  if (!fs::exists(path_) && !create)
    throw std::runtime_error("Progress file does not exist; choose New: " +
                             path_);
  if (create)
    fs::create_directories(fs::path(path_).parent_path());
  lock_ = open((path_ + ".lock").c_str(), O_RDWR | O_CREAT, 0600);
  if (lock_ < 0)
    system_error("Cannot open profile lock");
  if (flock(lock_, LOCK_EX | LOCK_NB) < 0) {
    close(lock_);
    lock_ = -1;
    throw std::runtime_error(
        "Progress profile is already open in another fvoci process");
  }
  try {
    if (fs::exists(path_)) {
      data = J::read(path_);
      if (data["schema_version"].num() != 1)
        throw std::runtime_error("Unsupported progress schema_version");
      if (!data["decks"].is_object() || !data["policies"].is_object() ||
          !data["schedules"].is_array() || !data["history"].is_array())
        throw std::runtime_error(
            "Malformed progress file; use its .bak file or another profile");
      for (const auto &s : data["schedules"].items()) {
        if (!s.is_object() || s["entry_id"].str().empty() ||
            s["deck_id"].str().empty() || !s["counters"].is_object() ||
            !s["units"].is_object())
          throw std::runtime_error("Malformed schedule record");
        s["stage"].num();
        unstamp(s["due_at"]);
        unstamp(s["last_attempt_at"]);
      }
      for (const auto &e : data["history"].items())
        if (!e.is_object())
          throw std::runtime_error("Malformed history record");
    } else
      data = J{{"schema_version", 1},
               {"profile_id", fs::path(path_).stem().string()},
               {"updated_at", stamp(now_seconds())},
               {"decks", J::object()},
               {"policies", J::object()},
               {"schedules", J::array()},
               {"history", J::array()}};
  } catch (...) {
    flock(lock_, LOCK_UN);
    close(lock_);
    lock_ = -1;
    throw;
  }
}
Progress::~Progress() {
  if (lock_ >= 0) {
    flock(lock_, LOCK_UN);
    close(lock_);
  }
}
void Progress::save() {
  data["updated_at"] = stamp(now_seconds());
  if (fs::exists(path_)) {
    std::ifstream in(path_);
    std::ostringstream s;
    s << in.rdbuf();
    if (!in.good() && !in.eof())
      throw std::runtime_error("Cannot back up progress");
    atomic_write(path_ + ".bak", s.str());
  }
  atomic_write(path_, data.dump(true) + "\n");
}
int Progress::register_entry(const Deck &d, const Entry &e) {
  auto &deck = data["decks"][d.id];
  deck["revision"] = d.revision;
  auto &revs = deck["entry_revisions"][e.id];
  if (revs.null())
    revs = J::array();
  if (!revs.is_array())
    throw std::runtime_error("Invalid entry revision history");
  auto fingerprint = hash(e.snapshot.dump());
  if (revs.size() &&
      revs.at(revs.size() - 1)["fingerprint"].str() == fingerprint)
    return revs.at(revs.size() - 1)["revision"].num();
  int revision =
      revs.size() ? revs.at(revs.size() - 1)["revision"].num() + 1 : 1;
  revs.push(J{{"revision", revision},
              {"fingerprint", fingerprint},
              {"requirements", e.snapshot}});
  return revision;
}
J &Progress::state(const Deck &d, const Entry &e, int revision,
                   const std::string &direction, const std::string &pid) {
  for (auto &s : data["schedules"].items())
    if (s["deck_id"].str() == d.id && s["entry_id"].str() == e.id &&
        s["entry_revision"].num() == revision &&
        s["direction"].str() == direction && s["policy_id"].str() == pid)
      return s;
  data["schedules"].push(J{{"deck_id", d.id},
                           {"entry_id", e.id},
                           {"entry_revision", revision},
                           {"direction", direction},
                           {"policy_id", pid},
                           {"stage", -1},
                           {"last_attempt_at", nullptr},
                           {"last_written_success_at", nullptr},
                           {"consecutive_text_failures", 0},
                           {"consecutive_choice_failures", 0},
                           {"counters", J{{"introductions", 0},
                                          {"attempts", 0},
                                          {"text_correct", 0},
                                          {"text_failed", 0},
                                          {"choice_correct", 0},
                                          {"choice_failed", 0},
                                          {"overrides", 0},
                                          {"reveals", 0},
                                          {"lapses", 0}}},
                           {"units", J::object()}});
  return data["schedules"].items().back();
}
J Task::json() const {
  J a = J::array();
  for (auto i : choices)
    a.push(int64_t(i));
  return J{{"entry", int64_t(entry)},
           {"direction", direction},
           {"mode", mode},
           {"reason", reason},
           {"introduction", introduction},
           {"early", early},
           {"choices", a}};
}
Task Task::from(const J &j) {
  Task t;
  t.entry = j["entry"].num();
  t.direction = j["direction"].str();
  t.mode = j["mode"].str();
  t.reason = j["reason"].str();
  t.introduction = j["introduction"].boolean();
  t.early = j["early"].boolean();
  for (const auto &x : j["choices"].items())
    t.choices.push_back(x.num());
  return t;
}
static int64_t active_revision(const J &progress, const Deck &deck, const Entry &entry) {
  const auto &revisions = progress["decks"][deck.id]["entry_revisions"][entry.id];
  if (revisions.is_array() && revisions.size()) {
    const auto &last = revisions.at(revisions.size() - 1);
    if (last["fingerprint"].str() == hash(entry.snapshot.dump())) return last["revision"].num();
  }
  return 0;
}
static J learning_record(const J &progress, const Deck &deck, const Entry &entry,
                         const std::string &pid) {
  auto revision = active_revision(progress, deck, entry);
  const auto &record = progress["learning"][deck.id][pid]["words"][entry.id];
  if (record.is_object() && record["revision"].num() == revision && revision) {
    if (!std::isfinite(record["score"].real()) || record["score"].real() < 0 ||
        record["score"].real() > 10 || record["full_sweeps"].num() < 0)
      throw std::runtime_error("Invalid saved difficulty score or sweep count");
    return record;
  }
  // Import evidence without pretending that old time-based sessions were full sweeps.
  double score = 0;
  int examined = 0;
  const auto &history = progress["history"];
  if (revision && history.is_array())
    for (auto it = history.items().rbegin(); it != history.items().rend() && examined < 20; ++it) {
      const auto &event = *it;
      if (event["event"].str("attempt") != "attempt" || event["deck_id"].str() != deck.id ||
          event["entry_id"].str() != entry.id || event["entry_revision"].num() != revision ||
          event["policy_id"].str() != pid) continue;
      ++examined;
      if (event["revealed"].boolean() || !event["effective_correct"].boolean(event["raw_correct"].boolean()))
        score += event["mode"].str() == "multiple_choice" ? 0.5 : 1.0;
    }
  return J{{"revision", revision}, {"score", std::min(10.0, score)}, {"full_sweeps", 0}};
}
std::string sweep_label(const std::string &mode) {
  if (mode == "full") return "Full sweep";
  if (mode == "difficult") return "Difficult sweep";
  if (mode == "very_difficult") return "Very difficult sweep";
  if (mode == "difficult_random") return "Difficult sweep + Random";
  if (mode == "very_difficult_random") return "Very difficult sweep + Random";
  return "Default";
}
J sweep_overview(const J &progress, const Deck &deck, const J &config) {
  auto pid = hash(policy(config).dump());
  int difficult = 0, very = 0;
  int64_t full = std::numeric_limits<int64_t>::max();
  for (const auto &entry : deck.entries) {
    auto record = learning_record(progress, deck, entry, pid);
    difficult += record["score"].real() >= config["difficulty_threshold"].real();
    very += record["score"].real() >= config["very_difficult_threshold"].real();
    full = std::min(full, record["full_sweeps"].num());
  }
  auto completed = progress["learning"][deck.id][pid]["completed_sweeps"].num();
  bool use_full = full < 2 || difficult == 0 || completed % config["full_sweep_every"].num() == 0;
  return J{{"full", int64_t(deck.entries.size())}, {"difficult", difficult},
           {"very_difficult", very}, {"full_sweeps", full},
           {"completed_sweeps", completed}, {"default_mode", use_full ? "full" : "difficult_random"}};
}
Engine::Engine(Progress &p, const Deck &d, J c, std::function<int64_t()> clock,
               uint32_t seed, bool mock)
    : progress_(p), deck_(d), config_(std::move(c)), clock_(std::move(clock)),
      rng_(seed ? seed : std::random_device{}()), mock_(mock) {
  if (mock_) config_["mode"] = "text";
  validate_config(config_);
  auto pol = policy(config_);
  policy_id_ = hash(pol.dump());
  p.data["policies"][policy_id_] = pol;
  for (const auto &e : d.entries) revisions_.push_back(p.register_entry(d, e));
  if (learning().null()) learning() = J{{"version", 1}, {"completed_sweeps", 0}, {"words", J::object()}};
  if (learning()["version"].num() != 1) throw std::runtime_error("Unsupported learning-state version");
  for (const auto &e : d.entries) {
    auto record = learning_record(p.data, d, e, policy_id_);
    learning()["words"][e.id] = record;
  }
  for (size_t i = 0; i < d.entries.size(); ++i)
    for (const auto &dir : {"de_fr", "fr_de"}) p.state(d, d.entries[i], revisions_[i], dir, policy_id_);
  J revisions = J::array();
  for (auto n : revisions_) revisions.push(n);
  J session_config = config_;
  for (auto k : {"recent_vocab", "recent_progress", "accent_macros", "accent_macros_enabled",
                 "keybindings", "keymap", "color", "theme", "profile_directory", "feedback_detail",
                 "new_entries_per_day", "retry_after_seconds", "retry_after_questions",
                 "review_intervals_seconds", "test_date"}) session_config.object_items().erase(k);
  auto signature = hash(J{{"scheduler", "sweeps-v1"}, {"deck", d.id}, {"revisions", revisions},
                          {"config", session_config}, {"mock", mock_}}.dump());
  auto &s = session();
  if (s.is_object() && s["signature"].str() == signature && !s["finished"].boolean()) {
    std::istringstream in(s["rng"].str()); in >> rng_;
    if (in.fail()) throw std::runtime_error("Invalid saved session random state");
    for (const auto &[id, item] : s["sweep"]["items"].object_items()) {
      auto index = item["entry"].num(-1);
      auto stage = item["stage"].str();
      if (index < 0 || size_t(index) >= d.entries.size() || d.entries[index].id != id ||
          (stage != "text" && stage != "multiple_choice" && stage != "done" && stage != "skipped") ||
          item["remaining"].num() < 0 || item["next_question"].num() < 0)
        throw std::runtime_error("Invalid saved sweep item");
    }
  } else {
    s = J{{"id", stamp(clock_()) + "-" + std::to_string(rng_())}, {"signature", signature},
          {"scheduler", "sweeps-v1"}, {"deck_id", d.id}, {"policy_id", policy_id_},
          {"settings", config_}, {"mock", mock_}, {"completed", 0}, {"bag", J::array()},
          {"skipped", J::array()}, {"mock_done", J::array()}, {"last_entry", ""},
          {"pending", nullptr}, {"draft", J::array()}, {"finished", false}, {"sweep_number", 0}};
    start_sweep();
    save_rng();
  }
  p.save();
}
J &Engine::session() { return progress_.data["session"]; }
J &Engine::learning() { return progress_.data["learning"][deck_.id][policy_id_]; }
J &Engine::word(size_t entry) { return learning()["words"][deck_.entries.at(entry).id]; }
J &Engine::state(const Task &t) {
  return progress_.state(deck_, deck_.entries.at(t.entry), revisions_.at(t.entry), t.direction, policy_id_);
}
void Engine::save_rng() {
  std::ostringstream out; out << rng_; session()["rng"] = out.str();
}
std::string Engine::next_direction() {
  auto &bag = session()["bag"];
  if (!bag.size()) {
    int a = config_["direction_weights"]["de_fr"].num(), b = config_["direction_weights"]["fr_de"].num();
    int gcd = std::gcd(a, b);
    for (int i = 0; i < a / gcd; ++i) bag.push("de_fr");
    for (int i = 0; i < b / gcd; ++i) bag.push("fr_de");
    std::shuffle(bag.items().begin(), bag.items().end(), rng_);
  }
  auto direction = bag.items().back().str(); bag.items().pop_back(); return direction;
}
void Engine::start_sweep() {
  auto overview = sweep_overview(progress_.data, deck_, config_);
  auto mode = mock_ ? "full" : config_["sweep"].str();
  if (mode == "default") mode = overview["default_mode"].str();
  bool warmup = !mock_ && overview["full_sweeps"].num() < 2;
  double threshold = mode.rfind("very_", 0) == 0 ? config_["very_difficult_threshold"].real()
                                                : config_["difficulty_threshold"].real();
  std::vector<size_t> pool, other;
  for (size_t i = 0; i < deck_.entries.size(); ++i) {
    if (mode == "full" || word(i)["score"].real() >= threshold) pool.push_back(i);
    else other.push_back(i);
  }
  std::set<size_t> sampled;
  if (mode.find("_random") != std::string::npos && config_["random_fraction"].real() > 0 && !other.empty()) {
    std::shuffle(other.begin(), other.end(), rng_);
    double fraction = config_["random_fraction"].real();
    size_t count = fraction >= 1 ? other.size() : std::max<size_t>(1, size_t(std::ceil(
        pool.empty() ? deck_.entries.size() * fraction : pool.size() * fraction / (1 - fraction))));
    count = std::min(count, other.size());
    for (size_t i = 0; i < count; ++i) { pool.push_back(other[i]); sampled.insert(other[i]); }
  }
  session().inc("sweep_number");
  session()["sweep"] = J{{"number", session()["sweep_number"]}, {"mode", mode},
                          {"warmup", warmup}, {"counted", false}, {"items", J::object()}};
  auto &items = session()["sweep"]["items"];
  for (auto i : pool) {
    auto direction = mock_ ? std::string() : next_direction();
    bool recognition = !mock_ && (config_["mode"].str() == "multiple_choice" ||
        (config_["mode"].str() == "adaptive" && (warmup || word(i)["score"].real() >= config_["choice_support_score"].real())));
    if (recognition && distractors(deck_, i, direction, config_).size() < 2) {
      if (config_["mode"].str() == "multiple_choice") continue;
      recognition = false;
    }
    int required = 1;
    if (!warmup && !mock_ && config_["mode"].str() != "multiple_choice")
      required = word(i)["score"].real() >= config_["very_difficult_threshold"].real() ? 3
                 : word(i)["score"].real() >= config_["difficulty_threshold"].real() ? 2 : 1;
    items[deck_.entries[i].id] = J{{"entry", int64_t(i)}, {"direction", direction},
        {"stage", recognition ? "multiple_choice" : "text"}, {"remaining", required},
        {"written_successes", 0}, {"mistakes", 0}, {"written_failures", 0},
        {"next_question", 0}, {"sampled", sampled.count(i) != 0}};
  }
}
void Engine::complete_sweep() {
  auto &sweep = session()["sweep"];
  if (sweep["counted"].boolean()) return;
  sweep["counted"] = true;
  if (mock_ || !sweep["items"].size()) return;
  learning().inc("completed_sweeps");
  bool full = sweep["mode"].str() == "full" && sweep["items"].size() == deck_.entries.size() &&
              config_["mode"].str() != "multiple_choice";
  for (const auto &[id, item] : sweep["items"].object_items())
    full = full && item["stage"].str() == "done" && item["written_successes"].num() > 0;
  if (full)
    for (const auto &[id, item] : sweep["items"].object_items()) word(item["entry"].num()).inc("full_sweeps");
}
std::optional<Task> Engine::next(bool) {
  auto &s = session();
  if (!s["pending"].null()) {
    auto task = Task::from(s["pending"]);
    if (task.entry >= deck_.entries.size() || (task.mode != "text" && task.mode != "multiple_choice") ||
        (task.direction != "de_fr" && task.direction != "fr_de"))
      throw std::runtime_error("Invalid saved question");
    for (auto choice : task.choices)
      if (choice >= deck_.entries.size()) throw std::runtime_error("Invalid saved choice");
    if (task.mode == "multiple_choice" && task.choices.size() < 3)
      throw std::runtime_error("Saved question has too few choices");
    return task;
  }
  auto finished = [&] {
    for (const auto &[id, item] : s["sweep"]["items"].object_items())
      if (item["stage"].str() != "done" && item["stage"].str() != "skipped") return false;
    return true;
  };
  if (finished()) complete_sweep();
  if (!mock_ && config_["session_questions"].num() > 0 &&
      s["completed"].num() >= config_["session_questions"].num()) { save_rng(); progress_.save(); return {}; }
  if (finished()) {
    if (mock_ || !s["sweep"]["items"].size()) { save_rng(); progress_.save(); return {}; }
    start_sweep();
  }
  std::vector<size_t> ready, waiting;
  bool ready_retry = false;
  int64_t question = s["completed"].num() + 1;
  for (const auto &[id, item] : s["sweep"]["items"].object_items()) {
    if (item["stage"].str() == "done" || item["stage"].str() == "skipped") continue;
    auto i = size_t(item["entry"].num()); waiting.push_back(i);
    if (item["next_question"].num() <= question) {
      ready.push_back(i);
      ready_retry |= item["next_question"].num() > 0;
    }
  }
  if (waiting.empty()) { save_rng(); progress_.save(); return {}; }
  if (ready_retry && !mock_) {
    int64_t earliest = std::numeric_limits<int64_t>::max();
    for (auto i : ready) {
      auto due = s["sweep"]["items"][deck_.entries[i].id]["next_question"].num();
      if (due > 0) earliest = std::min(earliest, due);
    }
    ready.erase(std::remove_if(ready.begin(), ready.end(), [&](size_t i) {
      return s["sweep"]["items"][deck_.entries[i].id]["next_question"].num() != earliest;
    }), ready.end());
  }
  auto &pool = ready.empty() ? waiting : ready;
  if (pool.size() > 1)
    pool.erase(std::remove_if(pool.begin(), pool.end(), [&](size_t i) { return deck_.entries[i].id == s["last_entry"].str(); }), pool.end());
  size_t selected;
  if (ready.empty()) {
    selected = *std::min_element(pool.begin(), pool.end(), [&](size_t a, size_t b) {
      return s["sweep"]["items"][deck_.entries[a].id]["next_question"].num() <
             s["sweep"]["items"][deck_.entries[b].id]["next_question"].num();
    });
  } else if (ready_retry || mock_)
    selected = pool[std::uniform_int_distribution<size_t>(0, pool.size() - 1)(rng_)];
  else selected = *std::min_element(pool.begin(), pool.end());
  auto &item = s["sweep"]["items"][deck_.entries[selected].id];
  if (item["direction"].str().empty()) item["direction"] = next_direction();
  Task t; t.entry = selected; t.direction = item["direction"].str(); t.mode = item["stage"].str();
  t.reason = sweep_label(s["sweep"]["mode"].str());
  if (t.mode == "multiple_choice") {
    auto ds = distractors(deck_, selected, t.direction, config_);
    std::shuffle(ds.begin(), ds.end(), rng_);
    ds.resize(std::min(ds.size(), size_t(config_["choice_options"].num() - 1)));
    t.choices = ds; t.choices.push_back(selected); std::shuffle(t.choices.begin(), t.choices.end(), rng_);
  }
  s["pending"] = t.json(); s["draft"] = J::array(); save_rng(); progress_.save(); return t;
}
void Engine::skip(const Task &t) {
  auto &item = session()["sweep"]["items"][deck_.entries[t.entry].id];
  if (!item.null()) item["stage"] = "skipped";
  session()["pending"] = nullptr; session()["draft"] = J::array();
  progress_.data["history"].push(J{{"event", "skip"}, {"at", stamp(clock_())},
      {"session_id", session()["id"]}, {"deck_id", deck_.id}, {"entry_id", deck_.entries[t.entry].id}, {"direction", t.direction}});
  save_rng(); progress_.save();
}
void Engine::schedule_outcome(J &s, const Task &t, bool correct, bool reveal, int64_t now) {
  s["last_attempt_at"] = stamp(now);
  s["counters"].inc("attempts");
  if (reveal) s["counters"].inc("reveals");
  bool choice = t.mode == "multiple_choice";
  s["counters"].inc(choice ? (correct ? "choice_correct" : "choice_failed") : (correct ? "text_correct" : "text_failed"));
  auto key = choice ? "consecutive_choice_failures" : "consecutive_text_failures";
  s[key] = correct ? 0 : s[key].num() + 1;
  if (!choice && correct) s["last_written_success_at"] = stamp(now);
}
void Engine::sweep_outcome(const Task &t, bool correct, int64_t question) {
  auto &record = word(t.entry);
  auto &item = session()["sweep"]["items"][deck_.entries[t.entry].id];
  if (!correct) record["score"] = std::min(10.0, record["score"].real() +
      config_[t.mode == "text" ? "written_mistake_weight" : "choice_mistake_weight"].real());
  if (item.null() || item["stage"].str() == "done" || item["stage"].str() == "skipped") return;
  if (!correct) item.inc("mistakes");
  if (mock_) {
    item["remaining"] = 0; item["stage"] = "done";
    if (correct) { item.inc("written_successes"); record["score"] = std::max(0.0, record["score"].real() - config_["clean_sweep_decay"].real()); }
    return;
  }
  if (t.mode == "multiple_choice") {
    if (correct) {
      item["written_failures"] = 0;
      item["stage"] = config_["mode"].str() == "multiple_choice" ? "done" : "text";
    }
  } else if (correct) {
    item.inc("written_successes"); item.inc("remaining", -1); item["written_failures"] = 0;
    item["stage"] = item["remaining"].num() <= 0 ? "done" : "text";
    if (item["stage"].str() == "done" && !session()["sweep"]["warmup"].boolean() && item["mistakes"].num() == 0)
      record["score"] = std::max(0.0, record["score"].real() - config_["clean_sweep_decay"].real());
  } else {
    item.inc("written_failures"); item["stage"] = "text";
    if (!session()["sweep"]["warmup"].boolean() && config_["mode"].str() == "adaptive" &&
        item["written_failures"].num() >= 2 &&
        record["score"].real() >= config_["choice_support_score"].real() &&
        distractors(deck_, t.entry, t.direction, config_).size() >= 2) item["stage"] = "multiple_choice";
  }
  if (item["stage"].str() != "done")
    item["next_question"] = question + 1 + std::uniform_int_distribution<int>(
        config_["retry_gap_min"].num(), config_["retry_gap_max"].num())(rng_);
}
size_t Engine::submit(const Task &t, const std::vector<Row> &rows,
                      const Grade &g, int64_t duration_ms, bool reveal,
                      int selected) {
  int64_t now = clock_();
  auto &s = state(t);
  J before = s;
  bool correct = g.correct && !reveal;
  auto learning_before = word(t.entry);
  auto item_before = session()["sweep"]["items"][deck_.entries[t.entry].id];
  if (t.mode == "text")
    for (const auto &u : units(deck_.entries[t.entry], t.direction)) {
      auto has = [&](const std::vector<std::string> &v) {
        return std::find(v.begin(), v.end(), u.id) != v.end();
      };
      if (t.direction == "fr_de" && correct && !has(g.explicit_units))
        continue; // Unused German alternatives were not tested.
      auto &stats = s["units"][u.id];
      stats.inc("attempts");
      stats.inc(!reveal && has(g.explicit_units)   ? "explicit"
                : !reveal && has(g.expanded_units) ? "expanded"
                : !reveal && has(g.inferred_units) ? "inferred"
                                                   : "failed");
    }
  schedule_outcome(s, t, correct, reveal, now);
  sweep_outcome(t, correct, session()["completed"].num() + 1);
  J event = g.json();
  event["id"] =
      "attempt-" + std::to_string(progress_.data["history"].size() + 1);
  event["event"] = "attempt";
  event["session_id"] = session()["id"];
  event["sequence"] = session()["completed"].num() + 1;
  event["at"] = stamp(now);
  event["deck_id"] = deck_.id;
  event["entry_id"] = deck_.entries[t.entry].id;
  event["entry_revision"] = revisions_[t.entry];
  event["direction"] = t.direction;
  event["policy_id"] = policy_id_;
  event["mode"] = t.mode;
  event["duration_ms"] = duration_ms;
  event["raw_correct"] = correct;
  event["effective_correct"] = correct;
  event["self_override"] = false;
  event["revealed"] = reveal;
  event["early_review"] = t.early;
  event["rows"] = J::array();
  for (const auto &r : rows)
    event["rows"].push(row_json(r));
  event["schedule_before"] = before;
  event["learning_before"] = learning_before;
  event["sweep_item_before"] = item_before;
  event["sweep_number"] = session()["sweep_number"];
  event["task"] = t.json();
  if (t.mode == "multiple_choice") {
    event["selected_option"] = selected;
    event["options"] = J::array();
    for (auto i : t.choices)
      event["options"].push(deck_.entries[i].id);
    event["explicit_units"] = J::array();
    event["inferred_units"] = J::array();
    event["expanded_units"] = J::array();
    event["expanded_answers"] = J::array();
    event["missing_units"] = J::array();
    event["rows"] = J::array();
  }
  auto index = progress_.data["history"].size();
  progress_.data["history"].push(event);
  session().inc("completed");
  session()["last_entry"] = deck_.entries[t.entry].id;
  session()["mock_done"].push(deck_.entries[t.entry].id);
  session()["pending"] = nullptr;
  session()["draft"] = J::array();
  save_rng();
  progress_.save();
  return index;
}
void Engine::override_sentence(size_t index) {
  auto &event = progress_.data["history"].at(index);
  auto t = Task::from(event["task"]);
  const auto &e = deck_.entries.at(t.entry);
  if (!e.override_allowed || e.type != "other" ||
      !config_["allow_sentence_self_override"].boolean() ||
      event["mode"].str() != "text" || event["revealed"].boolean() ||
      event["effective_correct"].boolean())
    throw std::runtime_error("This attempt cannot be overridden");
  for (size_t later = index + 1; later < progress_.data["history"].size(); ++later) {
    const auto &next = progress_.data["history"].at(later);
    if (next["event"].str() == "attempt" && next["deck_id"].str() == deck_.id &&
        next["policy_id"].str() == policy_id_ && next["entry_id"].str() == e.id)
      throw std::runtime_error("A newer answer for this word prevents overriding this attempt");
  }
  auto &s = state(t);
  auto raw_units = s["units"];
  s = event["schedule_before"];
  s["units"] = raw_units;
  schedule_outcome(s, t, true, false, unstamp(event["at"]));
  s["counters"].inc("overrides");
  if (event.has("learning_before")) {
    word(t.entry) = event["learning_before"];
    if (event["sweep_number"].num() == session()["sweep_number"].num()) {
      session()["sweep"]["items"][e.id] = event["sweep_item_before"];
      sweep_outcome(t, true, event["sequence"].num());
    }
  }
  save_rng();
  event["effective_correct"] = true;
  event["self_override"] = true;
  progress_.save();
}
void Engine::save_draft(const std::vector<Row> &rows) {
  session()["draft"] = J::array();
  for (const auto &r : rows)
    session()["draft"].push(row_json(r));
  progress_.save();
}
std::vector<Row> Engine::draft() const {
  std::vector<Row> rows;
  const auto &draft = progress_.data["session"]["draft"];
  if (draft.is_array())
    for (const auto &r : draft.items())
      rows.push_back({r["entered"].str(), r["gender"].str(),
                      r["gender_source"].str("manual")});
  return rows;
}
void Engine::finish() {
  session()["finished"] = true;
  session()["pending"] = nullptr;
  progress_.save();
}
WrittenAccuracy written_accuracy(const J &progress, const Deck &deck,
                                 const std::string &policy_id) {
  WrittenAccuracy result;
  result.total = deck.entries.size();
  std::map<std::string, int64_t> current;
  for (const auto &entry : deck.entries) {
    const auto &revisions =
        progress["decks"][deck.id]["entry_revisions"][entry.id];
    if (!revisions.is_array() || !revisions.size())
      continue;
    const auto &latest = revisions.at(revisions.size() - 1);
    if (latest["fingerprint"].str() == hash(entry.snapshot.dump()))
      current[entry.id] = latest["revision"].num();
  }
  if (!progress["history"].is_array())
    return result;
  std::set<std::string> seen;
  // History order is submission order. An override edits its original event;
  // it must not replace a newer written attempt on the same word.
  for (auto it = progress["history"].items().rbegin();
       it != progress["history"].items().rend(); ++it) {
    const auto &event = *it;
    if (event["event"].str("attempt") != "attempt" ||
        event["mode"].str() != "text" || event["deck_id"].str() != deck.id ||
        event["policy_id"].str() != policy_id)
      continue;
    auto found = current.find(event["entry_id"].str());
    if (found == current.end() ||
        found->second != event["entry_revision"].num() ||
        !seen.insert(found->first).second)
      continue;
    ++result.tested;
    if (!event["revealed"].boolean() &&
        event["effective_correct"].boolean(event["raw_correct"].boolean())) {
      ++result.correct;
      if (event["self_override"].boolean())
        ++result.overrides;
    }
    if (seen.size() == current.size())
      break;
  }
  return result;
}
WrittenAccuracy Engine::accuracy() const {
  return written_accuracy(progress_.data, deck_, policy_id_);
}
J Engine::summary() const {
  J out = J::object();
  out["text_attempts"] = 0;
  out["text_correct"] = 0;
  out["raw_correct"] = 0;
  out["choice_attempts"] = 0;
  out["choice_correct"] = 0;
  out["overrides"] = 0;
  J dirs = J::object();
  for (auto dir : {"de_fr", "fr_de"})
    dirs[dir] = J{{"n", 0}, {"correct", 0}, {"raw", 0}};
  for (auto it = progress_.data["history"].items().rbegin();
       it != progress_.data["history"].items().rend(); ++it) {
    const auto &ev = *it;
    if (ev["event"].str("attempt") != "attempt" ||
        ev["deck_id"].str() != deck_.id || ev["policy_id"].str() != policy_id_)
      continue;
    bool current = false;
    for (size_t i = 0; i < deck_.entries.size(); ++i)
      if (deck_.entries[i].id == ev["entry_id"].str() &&
          revisions_[i] == ev["entry_revision"].num())
        current = true;
    if (!current)
      continue;
    bool text = ev["mode"].str() == "text";
    if (ev["session_id"].str() == progress_.data["session"]["id"].str()) {
      out.inc(text ? "text_attempts" : "choice_attempts");
      if (ev["effective_correct"].boolean())
        out.inc(text ? "text_correct" : "choice_correct");
      if (text && ev["raw_correct"].boolean())
        out.inc("raw_correct");
      if (ev["self_override"].boolean())
        out.inc("overrides");
    }
    if (text) {
      auto &a = dirs[ev["direction"].str()];
      if (a["n"].num() < 20) {
        a.inc("n");
        if (ev["effective_correct"].boolean())
          a.inc("correct");
        if (ev["raw_correct"].boolean())
          a.inc("raw");
      }
    }
  }
  out["directions"] = dirs;
  auto latest = accuracy();
  out["written_accuracy"] = J{{"total", int64_t(latest.total)},
                              {"tested", int64_t(latest.tested)},
                              {"correct", int64_t(latest.correct)},
                              {"overrides", int64_t(latest.overrides)}};
  return out;
}
} // namespace fv
