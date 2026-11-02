#pragma once
#include "core.hpp"
#include <functional>
#include <random>

namespace fv {
void atomic_write(const std::string &, const std::string &);
struct WrittenAccuracy {
  size_t total = 0, tested = 0, correct = 0, overrides = 0;
  double percent() const { return total ? 100.0 * correct / total : 0.0; }
};
WrittenAccuracy written_accuracy(const J &progress, const Deck &deck,
                                 const std::string &policy_id);
J sweep_overview(const J &, const Deck &, const J &);
std::string sweep_label(const std::string &);
class Progress {
  std::string path_;
  int lock_ = -1;

public:
  J data;
  Progress(const std::string &path, bool create);
  ~Progress();
  Progress(const Progress &) = delete;
  Progress &operator=(const Progress &) = delete;
  void save();
  int register_entry(const Deck &, const Entry &);
  J &state(const Deck &, const Entry &, int, const std::string &,
           const std::string &);
  const std::string &path() const { return path_; }
};
struct Task {
  size_t entry = 0;
  std::string direction = "de_fr", mode = "text", reason;
  bool introduction = false, early = false;
  std::vector<size_t> choices;
  J json() const;
  static Task from(const J &);
};
class Engine {
  Progress &progress_;
  const Deck &deck_;
  J config_;
  std::function<int64_t()> clock_;
  std::mt19937 rng_;
  std::vector<int> revisions_;
  std::string policy_id_;
  bool mock_ = false;
  J &learning();
  J &word(size_t);
  void start_sweep();
  void complete_sweep();
  std::string next_direction();
  void save_rng();
  void sweep_outcome(const Task &, bool, int64_t);

  void schedule_outcome(J &, const Task &, bool, bool, int64_t);

public:
  Engine(Progress &, const Deck &, J,
         std::function<int64_t()> clock = now_seconds, uint32_t seed = 0,
         bool mock = false);
  J &session();
  J &state(const Task &);
  const J &config() const { return config_; }
  const std::string &policy_id() const { return policy_id_; }
  std::optional<Task> next(bool early = false);
  void skip(const Task &);
  size_t submit(const Task &, const std::vector<Row> &, const Grade &,
                int64_t duration_ms, bool reveal = false, int selected = -1);
  void override_sentence(size_t event);
  void save_draft(const std::vector<Row> &);
  std::vector<Row> draft() const;
  void finish();
  J summary() const;
  WrittenAccuracy accuracy() const;
};
} // namespace fv
