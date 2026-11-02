#pragma once
#include "progress.hpp"
namespace fv {
struct Options {
  std::string vocab, progress, config_path;
  J config;
  bool mock = false, create_progress = false;
  uint32_t seed = 0;
};
int run_ui(Options);
} // namespace fv
