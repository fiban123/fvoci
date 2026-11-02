#include "ui.hpp"
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <limits.h>
#include <ncurses.h>
#include <sstream>
#include <unistd.h>
#include <wchar.h>

namespace fv {
namespace fs = std::filesystem;
namespace {
struct Screen {
  SCREEN *terminal = nullptr;
  explicit Screen(const J &c) {
    // Use ncurses' direct-color terminfo so it emits RGB ANSI sequences,
    // independent of the terminal's customizable 16-color palette.
    if (c["color"].str() != "never")
      terminal = newterm("xterm-direct", stdout, stdin);
    if (!terminal)
      terminal = newterm(nullptr, stdout, stdin);
    if (!terminal)
      throw std::runtime_error("Cannot initialize terminal");
    cbreak();
    noecho();
    keypad(stdscr, true);
    set_escdelay(25);
    curs_set(0);
    if (c["color"].str() != "never" && has_colors()) {
      start_color();
      use_default_colors();
      const int palette[] = {0x20242c, 0xff6464, 0x50d890, 0xe5c07b,
                             0x61afef, 0xc678dd, 0x56b6c2, 0xe5e9f0};
      int pair = 1;
      for (auto name : {"accent", "success", "error"}) {
        const auto &value = c["theme"][name];
        int rgb = value.is_string() ? std::stoi(value.str().substr(1), nullptr, 16)
                                    : palette[value.num()];
        if (COLORS >= 0x1000000)
          init_extended_pair(pair, rgb, -1);
        else if (COLORS >= 256) {
          int r = (rgb >> 16) & 255, g = (rgb >> 8) & 255, b = rgb & 255;
          init_pair(pair, 16 + 36 * ((r * 5 + 127) / 255) +
                              6 * ((g * 5 + 127) / 255) + (b * 5 + 127) / 255, -1);
        } else
          init_pair(pair, pair == 1 ? COLOR_CYAN : pair == 2 ? COLOR_GREEN : COLOR_RED, -1);
        ++pair;
      }
    }
    std::cout << "\033[?2004h" << std::flush;
  }
  ~Screen() {
    std::cout << "\033[?2004l\033[0 q" << std::flush;
    endwin();
    delscreen(terminal);
  }
};
void editing_cursor(bool insert) {
  curs_set(1);
  std::cout << (insert ? "\033[6 q" : "\033[2 q") << std::flush;
}
struct Key {
  int code = 0;
  bool special = false;
};
Key key() {
  wint_t k = 0;
  int r = get_wch(&k);
  return r == ERR ? Key{-1, false} : Key{int(k), r == KEY_CODE_YES};
}
bool enter(Key k) {
  return k.code == '\n' || k.code == '\r' || (k.special && k.code == KEY_ENTER);
}
int width(const std::wstring &s) {
  int n = 0;
  for (auto c : s)
    n += std::max(wcwidth(c), 0);
  return n;
}
std::wstring clipped(const std::wstring &text, int limit) {
  std::wstring out;
  int used = 0;
  for (auto c : text) {
    int w = std::max(wcwidth(c), 0);
    if (used + w > limit)
      break;
    if (c >= 32 && c != 127)
      out += c;
    used += w;
  }
  return out;
}
std::vector<std::string> wrapped(const std::string &text, int limit) {
  limit = std::max(1, limit);
  auto remaining = wide(text);
  std::vector<std::string> result;
  while (!remaining.empty()) {
    auto part = clipped(remaining, limit);
    if (part.empty())
      part = remaining.substr(0, 1);
    size_t end = part.size();
    if (end < remaining.size()) {
      auto space = part.find_last_of(L' ');
      if (space != std::wstring::npos && space > 0)
        end = space;
    }
    result.push_back(utf8(remaining.substr(0, end)));
    remaining.erase(0, end);
    while (!remaining.empty() && remaining.front() == L' ')
      remaining.erase(0, 1);
  }
  if (result.empty())
    result.push_back("");
  return result;
}
std::vector<std::string> wrap_lines(const std::vector<std::string> &text) {
  std::vector<std::string> result;
  for (const auto &line : text) {
    auto pieces = wrapped(line, COLS - 5);
    result.insert(result.end(), pieces.begin(), pieces.end());
  }
  return result;
}
void put(int y, int x, const std::string &text, int attr = 0) {
  if (y < 0 || y >= LINES || x < 0 || x >= COLS)
    return;
  auto w = clipped(wide(text), COLS - x - 1);
  attron(attr);
  mvaddnwstr(y, x, w.c_str(), int(w.size()));
  attroff(attr);
}
void centered(int y, const std::string &text, int attr = 0) {
  auto visible = clipped(wide(text), std::max(1, COLS - 4));
  put(y, std::max(0, (COLS - width(visible)) / 2), utf8(visible), attr);
}
void header(const std::string &title) {
  erase();
  put(0, 1, " fvoci ", A_BOLD | A_REVERSE | COLOR_PAIR(1));
  put(0, 10, title, A_BOLD);
  if (LINES > 2)
    mvhline(1, 0, ACS_HLINE, COLS);
}
std::string percentage(const WrittenAccuracy &accuracy) {
  std::ostringstream out;
  out << std::fixed << std::setprecision(1) << accuracy.percent() << '%';
  return out.str();
}
std::string accuracy_label(const WrittenAccuracy &accuracy) {
  return "Accuracy: " + percentage(accuracy) + "  " +
         std::to_string(accuracy.correct) + "/" +
         std::to_string(accuracy.total) + " words";
}
void accuracy_meter(const WrittenAccuracy &accuracy) {
  if (LINES < 5) return;
  auto label = accuracy_label(accuracy);
  auto goal = "  ·  Goal 95%";
  auto untested = "  ·  " + std::to_string(accuracy.total - accuracy.tested) + " untested";
  if (width(wide(label + goal)) <= COLS - 4) label += goal;
  if (width(wide(label + untested)) <= COLS - 4) label += untested;
  centered(LINES - 3, label, A_DIM);
}
void footer(const std::string &text,
            const WrittenAccuracy *accuracy = nullptr) {
  if (accuracy)
    accuracy_meter(*accuracy);
  if (LINES > 2) {
    mvhline(LINES - 2, 0, ACS_HLINE, COLS);
    put(LINES - 1, 1, text, A_DIM);
  }
}
void lines(const std::vector<std::string> &text, int start = 3,
           int scroll = 0) {
  int y = start;
  for (size_t i = scroll; i < text.size() && y < LINES - 3; ++i)
    put(y++, 2, text[i]);
}
void message(const std::string &title, const std::vector<std::string> &content,
             const WrittenAccuracy *accuracy = nullptr) {
  int scroll = 0;
  while (true) {
    header(title);
    auto display = wrap_lines(content);
    lines(display, 3, scroll);
    footer("j/k scroll   Enter / Esc back", accuracy);
    refresh();
    auto k = key();
    if (enter(k) || k.code == 27 || k.code == 'q')
      break;
    if (k.code == 'j' || k.code == KEY_DOWN)
      scroll = std::min<int>(scroll + 1, std::max<int>(0, display.size() - 1));
    if (k.code == 'k' || k.code == KEY_UP)
      scroll = std::max(0, scroll - 1);
  }
}
int menu(const std::string &title, const std::vector<std::string> &options,
         int selected = 0, const WrittenAccuracy *accuracy = nullptr) {
  while (true) {
    header(title);
    bool start_screen = title.rfind("Start", 0) == 0;
    int top = start_screen && LINES >= 20 ? 6 : 3;
    if (top == 6) {
      put(2, 3, "Learn French. Be ready for your test.",
          A_BOLD | COLOR_PAIR(1));
      put(3, 3, "Choose your vocabulary and profile, then start practising.");
      put(4, 3, "Goal: 95% of words correct on their latest written answer.",
          A_DIM);
    }
    int visible = std::max(1, LINES - top - 4),
        start = std::max(0, selected - visible + 1);
    for (int i = start; i < int(options.size()) && i < start + visible; ++i) {
      auto label = (i == selected ? " > " : "   ") + options[i];
      if (i == selected)
        label += std::string(std::max(0, COLS - 5 - width(wide(label))), ' ');
      put(top + i - start, 2, label,
          i == selected ? A_REVERSE | A_BOLD | COLOR_PAIR(1) : 0);
    }
    footer("j/k move   Enter select   Esc back", accuracy);
    refresh();
    auto k = key();
    if (k.code == 27 || k.code == 'q')
      return -1;
    if (k.code == 'j' || k.code == KEY_DOWN)
      selected = (selected + 1) % options.size();
    if (k.code == 'k' || k.code == KEY_UP)
      selected = (selected + options.size() - 1) % options.size();
    if (enter(k))
      return selected;
  }
}
int question_panel(const Deck &deck, const Task &task) {
  const bool french = task.direction == "de_fr";
  header(deck.title);
  auto prompt = wrapped(french ? deck.entries[task.entry].prompt
                               : bundle(deck.entries[task.entry], "de_fr"),
                        COLS - 6);
  int count = std::min<int>(prompt.size(), std::max(1, LINES - 17));
  // Center the prompt and answer as one block in the space between the header
  // and footer. Reserve a stable area below it for inline corrections.
  int content = count + 5;
  if (task.mode == "multiple_choice") {
    content = count + 3;
    for (auto choice : task.choices)
      content += wrapped("1. " + bundle(deck.entries[choice], task.direction), COLS - 5).size() + 1;
  }
  int top = std::max(3, 2 + (LINES - 5 - content) / 2);
  centered(top, french ? "Translate into French:" : "Translate into German:", A_DIM);
  for (int i = 0; i < count; ++i)
    centered(top + 1 + i, prompt[i], A_BOLD | COLOR_PAIR(1));
  return top + count + 3;
}
std::optional<std::string> input(const std::string &title,
                                 const std::string &initial) {
  std::wstring value = wide(initial);
  size_t cursor = value.size();
  curs_set(1);
  while (true) {
    header(title);
    int room = std::max(1, COLS - 5);
    size_t start = 0;
    while (start < cursor && width(value.substr(start, cursor - start)) >= room)
      start = next_grapheme(value, start);
    put(3, 2, utf8(value.substr(start)));
    footer("Enter accept   Esc cancel");
    move(std::min(3, LINES - 1),
         std::min(COLS - 1, 2 + width(value.substr(start, cursor - start))));
    refresh();
    auto k = key();
    if (k.code == 27) {
      curs_set(0);
      return {};
    }
    if (enter(k)) {
      curs_set(0);
      return utf8(value);
    }
    if (k.code == KEY_LEFT && k.special)
      cursor = previous_grapheme(value, cursor);
    else if (k.code == KEY_RIGHT && k.special)
      cursor = next_grapheme(value, cursor);
    else if (k.code == KEY_HOME && k.special)
      cursor = 0;
    else if (k.code == KEY_END && k.special)
      cursor = value.size();
    else if (k.code == 127 || k.code == 8 ||
             (k.special && k.code == KEY_BACKSPACE)) {
      if (cursor) {
        auto p = previous_grapheme(value, cursor);
        value.erase(p, cursor - p);
        cursor = p;
      }
    } else if (!k.special && k.code >= 32 && k.code <= 0x10ffff) {
      value.insert(cursor++, 1, wchar_t(k.code));
    }
  }
}
std::string home_expand(std::string p) {
  if (p.rfind("~/", 0) == 0) {
    const char *h = getenv("HOME");
    if (h)
      p = std::string(h) + p.substr(1);
  }
  return p;
}
std::optional<std::string> picker(const std::string &initial) {
  fs::path dir = initial.empty() ? fs::current_path() : fs::absolute(initial);
  if (!fs::is_directory(dir))
    dir = dir.parent_path();
  std::string filter;
  int selected = 0;
  while (true) {
    std::vector<fs::directory_entry> entries;
    std::error_code ec;
    for (const auto &e : fs::directory_iterator(
             dir, fs::directory_options::skip_permission_denied, ec)) {
      if ((e.is_directory() || e.path().extension() == ".json") &&
          e.path().filename().string().find(filter) != std::string::npos)
        entries.push_back(e);
    }
    std::sort(entries.begin(), entries.end(), [](const auto &a, const auto &b) {
      return a.is_directory() != b.is_directory()
                 ? a.is_directory() > b.is_directory()
                 : a.path().filename() < b.path().filename();
    });
    selected = std::clamp(selected, 0, std::max(0, int(entries.size()) - 1));
    header("Choose JSON file");
    put(2, 2, dir.string());
    put(3, 2, "Filter: " + filter);
    int visible = std::max(1, LINES - 8),
        start = std::max(0, selected - visible + 1);
    for (int i = start; i < int(entries.size()) && i < start + visible; ++i)
      put(5 + i - start, 2,
          (i == selected ? "> " : "  ") +
              entries[i].path().filename().string() +
              (entries[i].is_directory() ? "/" : ""),
          i == selected ? A_REVERSE : 0);
    footer("j/k select   h parent   / filter   p path   Enter open   Esc back");
    refresh();
    auto k = key();
    if (k.code == 27)
      return {};
    if (k.code == 'j' || k.code == KEY_DOWN)
      selected = std::min(selected + 1, std::max(0, int(entries.size()) - 1));
    if (k.code == 'k' || k.code == KEY_UP)
      selected = std::max(0, selected - 1);
    if (k.code == 'h' || k.code == KEY_LEFT) {
      dir = dir.parent_path();
      selected = 0;
    }
    if (k.code == '/') {
      auto s = input("Filter files", filter);
      if (s)
        filter = *s;
      selected = 0;
    }
    if (k.code == 'p') {
      auto s = input("Path", dir.string() + "/");
      if (s) {
        fs::path p = home_expand(*s);
        if (fs::is_directory(p))
          dir = fs::absolute(p);
        else if (fs::is_regular_file(p))
          return fs::absolute(p).string();
        else
          message("Path not found", {p.string()});
      }
    }
    if (enter(k) && !entries.empty()) {
      auto p = entries[selected].path();
      if (entries[selected].is_directory()) {
        dir = p;
        selected = 0;
      } else
        return p.string();
    }
  }
}
void help() {
  message("Keys and answer rules",
          {"Answer exactly the forms listed in the vocabulary.",
           "Pairwise: le/les chat.s (m) gives le chat, les chats (m).",
           "Nouns: le/la give gender; les/l' need (m), (f), or (mf).",
           "Separate forms with commas; full sentence alternatives with semicolons.",
           "Esc: normal mode with block cursor. i/a: edit. h/l: cursor. w/b: word.",
           "0/$: start/end. x: delete character. dd: clear answer. u: undo.",
           "q in normal mode: pause. ? in normal mode: this help.",
           "Macros: e+ é, e\\ è, a\\ à, u\\ ù, vowel^ circumflex, vowel: "
           "diaeresis.",
           "c, ç; oe+ œ; ae+ æ. Uppercase equivalents work too.",
           "Ctrl-V quotes the next character. Bracketed paste skips macros.",
           "French: cover all required synonyms and forms.",
           "German: any listed form or synonym; case and punctuation ignored.",
           "Braces: hell{o,o,b}{,e,o} gives hello, helloe, hellbo.",
           "Alternatives align by index; .e.es means empty/e/es. No grammar inference.",
           "Multiple choice: j/k + Enter, or 1-9. Not written mastery.",
           "Learn selects full or difficulty-based sweeps, with optional random words.",
           "Retries use 3–10 other questions, never minutes or days.",
           "The first two full sweeps teach recognition then writing (adaptive mode)."});
}
void settings(J &c) {
  while (true) {
    int s = menu(
        "Settings",
        {"Mode: " + c["mode"].str(),
         "Directions: " +
             std::to_string(c["direction_weights"]["de_fr"].num()) + ":" +
             std::to_string(c["direction_weights"]["fr_de"].num()),
         "Question limit: " + (c["session_questions"].num() == 0
                                  ? std::string("unlimited")
                                  : std::to_string(c["session_questions"].num())),
         std::string("Text expansion: ") + (c["inference"].str() == "none" ? "off" : "on"),
         std::string("Accent macros: ") +
             (c["accent_macros_enabled"].boolean() ? "on" : "off"),
         std::string("Sentence self-override: ") +
             (c["allow_sentence_self_override"].boolean() ? "on" : "off"),
         std::string("Sentence casing: ") + (c["sentence_case_sensitive"].boolean() ? "strict" : "ignored"),
         std::string("Sentence punctuation: ") + (c["sentence_ignore_punctuation"].boolean() ? "ignored" : "strict"), "Done"});
    if (s < 0 || s == 8)
      return;
    try {
      if (s == 0) {
        int m = menu("Learning mode",
                     {"Adaptive", "Text only", "Multiple choice only"});
        if (m >= 0)
          c["mode"] = m == 0 ? "adaptive" : m == 1 ? "text" : "multiple_choice";
      } else if (s == 1) {
        int m = menu("Directions", {"90% German to French / 10% reverse",
                                    "French production only",
                                    "German production only", "50 / 50"});
        if (m >= 0)
          c["direction_weights"] = J{{"de_fr", m == 0   ? 90
                                               : m == 1 ? 100
                                               : m == 2 ? 0
                                                        : 50},
                                     {"fr_de", m == 0   ? 10
                                               : m == 1 ? 0
                                               : m == 2 ? 100
                                                        : 50}};
      } else if (s == 2) {
        auto k = "session_questions";
        auto v = input(k, std::to_string(c[k].num()));
        if (v) {
          J next = c;
          size_t used = 0;
          auto n = std::stoll(*v, &used);
          if (used != v->size())
            throw std::runtime_error("Use a whole number");
          next[k] = int64_t(n);
          validate_config(next);
          c = next;
        }
      } else if (s == 3)
        c["inference"] = c["inference"].str() == "none" ? "text-expansion-v1" : "none";
      else if (s == 4)
        c["accent_macros_enabled"] = !c["accent_macros_enabled"].boolean();
      else if (s == 5)
        c["allow_sentence_self_override"] =
            !c["allow_sentence_self_override"].boolean();
      else if (s == 6)
        c["sentence_case_sensitive"] = !c["sentence_case_sensitive"].boolean();
      else if (s == 7)
        c["sentence_ignore_punctuation"] = !c["sentence_ignore_punctuation"].boolean();
    } catch (const std::exception &e) {
      message("Invalid setting", {e.what()});
    }
  }
}
enum class Action { Submit, Reveal, Skip, Exit };
struct Answer {
  Action action = Action::Submit;
  std::vector<Row> rows;
  int64_t duration = 0;
  int selected = -1;
  std::string entered;
};
int binding(Key k, const J &config, const std::string &context) {
  if (k.special)
    return k.code;
  std::string typed = utf8(std::wstring(1, wchar_t(k.code)));
  auto map = config["keybindings"][context];
  if (map.has(typed)) {
    auto out = wide(map[typed].str());
    if (out.size() == 1)
      return out.front();
  }
  return k.code;
}
std::string read_paste() {
  std::wstring tail;
  while (true) {
    auto k = key();
    if (k.code < 0 || k.special)
      continue;
    tail.push_back(wchar_t(k.code));
    if (tail.size() >= 6 && tail.substr(tail.size() - 6) == L"\033[201~") {
      tail.resize(tail.size() - 6);
      return utf8(tail);
    }
    if (tail.size() > 100000)
      throw std::runtime_error("Paste exceeds 100000 characters");
  }
}
// The same bar is drawn during editing and correction, keeping the question
// and answer anchored in place. Long input scrolls horizontally at the cursor.
void answer_bar(int y, const std::wstring &value, size_t cursor, int attr,
                bool active = false) {
  int room = std::max(1, COLS - 6);
  size_t start = 0;
  while (start < cursor && width(value.substr(start, cursor - start)) >= room)
    start = next_grapheme(value, start);
  auto view = clipped(value.substr(start), room);
  attron(attr | A_BOLD);
  mvaddwstr(y - 1, 0, (L"╭" + std::wstring(std::max(0, COLS - 2), L'─') + L"╮").c_str());
  mvaddwstr(y, 0, L"│");
  mvaddwstr(y, COLS - 1, L"│");
  mvaddwstr(y + 1, 0, (L"╰" + std::wstring(std::max(0, COLS - 2), L'─') + L"╯").c_str());
  attroff(attr | A_BOLD);
  int x = std::max(2, (COLS - width(view) - 2) / 2);
  put(y, x, "> " + utf8(view), attr | A_BOLD);
  if (active)
    move(y, std::min(COLS - 2, x + 2 + width(value.substr(start, cursor - start))));
}
Answer text_answer(Engine &engine, const Deck &deck, const Task &t) {
  const auto &entry = deck.entries[t.entry];
  auto draft = engine.draft();
  std::wstring field = wide(draft.size() == 1 && draft[0].gender_source == "bar"
                               ? draft[0].text : answer_text(draft));
  size_t cursor = field.size();
  std::vector<std::pair<std::wstring, size_t>> undo;
  bool insert = true, quote = false, delete_pending = false;
  std::string error;
  auto macro = macros(engine.config());
  const auto accuracy = engine.accuracy();
  auto begin = std::chrono::steady_clock::now();
  int64_t paused = 0;
  auto snapshot = [&]() {
    undo.push_back({field, cursor});
    if (undo.size() > 100) undo.erase(undo.begin());
  };
  auto result = [&](Action action) {
    curs_set(0);
    auto raw = utf8(field);
    return Answer{action, parse_answer(entry, t.direction, raw),
                  std::chrono::duration_cast<std::chrono::milliseconds>(
                      std::chrono::steady_clock::now() - begin).count() - paused,
                  -1, raw};
  };
  while (true) {
    cursor = std::min(cursor, field.size());
    int start = question_panel(deck, t);
    centered(LINES - 4, error, COLOR_PAIR(3));
    footer(std::string(insert ? "Insert" : "Normal") +
               "  Enter check  Esc normal  i edit  q pause  ? help", &accuracy);
    answer_bar(start, field, cursor, COLOR_PAIR(2), true);
    editing_cursor(insert);
    refresh();
    auto k = key();
    if (k.code < 0 || k.code == KEY_RESIZE) continue;
    int code = insert ? k.code : binding(k, engine.config(), "normal");
    if (!k.special && code == 27) {
      nodelay(stdscr, true);
      std::wstring seq;
      for (int i = 0; i < 5; ++i) {
        wint_t ch = 0;
        if (get_wch(&ch) == ERR) break;
        seq.push_back(wchar_t(ch));
      }
      nodelay(stdscr, false);
      if (seq == L"[200~") {
        auto paste = wide(normalize(read_paste()));
        if (insert) {
          snapshot(); field.insert(cursor, paste); cursor += paste.size();
        }
        continue;
      }
      for (auto it = seq.rbegin(); it != seq.rend(); ++it) unget_wch(*it);
      if (insert && cursor) cursor = previous_grapheme(field, cursor);
      insert = false; delete_pending = false; quote = false;
      continue;
    }
    if (enter(k)) {
      if (normalize(utf8(field)).empty()) {
        error = "Type an answer, or pause to reveal or skip.";
        continue;
      }
      return result(Action::Submit);
    }
    if (!insert && (code == '?' || code == 'q')) {
      curs_set(0);
      auto paused_at = std::chrono::steady_clock::now();
      int action = 0;
      if (code == '?') help();
      else {
        engine.save_draft({{utf8(field), "", "bar"}});
        action = menu("Paused", {"Resume", "Reveal answer (counts as missed)",
                                "Skip question", "Save and exit session", "Help"});
        if (action == 4) help();
      }
      paused += std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - paused_at).count();
      if (action == 1) return result(Action::Reveal);
      if (action == 2) return result(Action::Skip);
      if (action == 3) return result(Action::Exit);
      continue;
    }
    if (!insert && code == 'u') {
      if (!undo.empty()) {
        field = undo.back().first; cursor = undo.back().second; undo.pop_back();
      }
      continue;
    }
    if (!insert && code == 'd') {
      if (delete_pending) {
        snapshot(); field.clear(); cursor = 0;
      }
      delete_pending = !delete_pending;
      continue;
    }
    delete_pending = false;
    if ((k.special && code == KEY_LEFT) || (!insert && code == 'h'))
      cursor = previous_grapheme(field, cursor);
    else if ((k.special && code == KEY_RIGHT) || (!insert && code == 'l'))
      cursor = next_grapheme(field, cursor);
    else if ((k.special && code == KEY_HOME) || (!insert && code == '0'))
      cursor = 0;
    else if ((k.special && code == KEY_END) || (!insert && code == '$'))
      cursor = insert || field.empty() ? field.size() : previous_grapheme(field, field.size());
    else if (!insert && (code == 'w' || code == 'b')) {
      if (code == 'w') {
        while (cursor < field.size() && field[cursor] != L' ') cursor = next_grapheme(field, cursor);
        while (cursor < field.size() && field[cursor] == L' ') ++cursor;
      } else {
        while (cursor && field[cursor - 1] == L' ') --cursor;
        while (cursor && field[cursor - 1] != L' ') cursor = previous_grapheme(field, cursor);
      }
    } else if (!insert && (code == 'i' || code == 'a' || code == 'A' || code == 'I')) {
      if (code == 'a') cursor = next_grapheme(field, cursor);
      if (code == 'A') cursor = field.size();
      if (code == 'I') cursor = 0;
      insert = true;
    } else if ((k.special && code == KEY_DC) || (!insert && code == 'x')) {
      if (cursor < field.size()) {
        snapshot(); field.erase(cursor, next_grapheme(field, cursor) - cursor);
      }
    } else if (insert && (code == 127 || code == 8 || (k.special && code == KEY_BACKSPACE))) {
      if (cursor) {
        snapshot(); auto previous = previous_grapheme(field, cursor);
        field.erase(previous, cursor - previous); cursor = previous;
      }
    } else if (insert && !k.special && code == 22) quote = true;
    else if (insert && !k.special && code >= 32 && code <= 0x10ffff) {
      snapshot(); field.insert(cursor++, 1, wchar_t(code));
      if (!quote && engine.config()["accent_macros_enabled"].boolean())
        expand_macro(field, cursor, macro);
      quote = false; error.clear();
    }
    if (!insert && cursor == field.size() && cursor)
      cursor = previous_grapheme(field, cursor);
  }
}
Answer choice_answer(Engine &engine, const Deck &deck, const Task &t) {
  int selected = 0, scroll = 0;
  const auto accuracy = engine.accuracy();
  auto begin = std::chrono::steady_clock::now();
  int64_t paused = 0;
  while (true) {
    int start =
        question_panel(deck, t);
    centered(start - 1, "Choose one answer", A_DIM);
    int visible = std::max(1, LINES - start - 3);
    std::vector<std::pair<std::string, int>> display;
    int selected_start = 0, selected_end = 0;
    for (size_t i = 0; i < t.choices.size(); ++i) {
      auto pieces = wrapped(std::to_string(i + 1) + ". " +
                                bundle(deck.entries[t.choices[i]], t.direction),
                            COLS - 5);
      if (int(i) == selected)
        selected_start = display.size();
      for (const auto &part : pieces)
        display.push_back({part, int(i)});
      if (int(i) == selected)
        selected_end = display.size();
      display.push_back({"", -1});
    }
    if (selected_start < scroll)
      scroll = selected_start;
    if (selected_end > scroll + visible)
      scroll = std::max(selected_start, selected_end - visible);
    for (int i = scroll; i < int(display.size()) && i < scroll + visible; ++i)
      centered(start + i - scroll, display[i].first,
          display[i].second == selected ? A_REVERSE | A_BOLD | COLOR_PAIR(1)
                                        : 0);
    footer("j/k select   Enter or 1-9 answer   ? full options   q pause",
           &accuracy);
    refresh();
    auto k = key();
    if (k.code == '?') {
      std::vector<std::string> content{
          t.direction == "de_fr" ? deck.entries[t.entry].prompt
                                 : bundle(deck.entries[t.entry], "de_fr")};
      for (size_t i = 0; i < t.choices.size(); ++i)
        content.push_back(std::to_string(i + 1) + ". " +
                          bundle(deck.entries[t.choices[i]], t.direction));
      auto p = std::chrono::steady_clock::now();
      message("Full question and choices", content);
      paused += std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - p)
                    .count();
      continue;
    }
    if (k.code == 'j' || k.code == KEY_DOWN)
      selected = (selected + 1) % t.choices.size();
    if (k.code == 'k' || k.code == KEY_UP)
      selected = (selected + t.choices.size() - 1) % t.choices.size();
    bool done = enter(k);
    if (k.code >= '1' && k.code <= '9' &&
        k.code - '1' < int(t.choices.size())) {
      selected = k.code - '1';
      done = true;
    }
    if (done)
      return {Action::Submit,
              {},
              std::chrono::duration_cast<std::chrono::milliseconds>(
                  std::chrono::steady_clock::now() - begin)
                      .count() -
                  paused,
              selected, ""};
    if (k.code == 'q' || k.code == 27) {
      auto p = std::chrono::steady_clock::now();
      int a =
          menu("Paused", {"Resume", "Skip question", "Save and exit session"});
      paused += std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - p)
                    .count();
      if (a == 1)
        return {Action::Skip, {}, 0, -1, ""};
      if (a == 2)
        return {Action::Exit, {}, 0, -1, ""};
    }
  }
}
void feedback(Engine &engine, const Deck &deck, const Task &t, const Answer &a,
              const Grade &g, size_t event) {
  bool overridden = false;
  std::optional<std::chrono::steady_clock::time_point> advance_at;
  int scroll = 0;
  while (true) {
    const bool success =
        overridden || (g.correct && a.action != Action::Reveal);
    const auto accuracy = engine.accuracy();
    int start = question_panel(deck, t);
    const int good = A_BOLD | COLOR_PAIR(2), bad = A_BOLD | COLOR_PAIR(3);
    std::string status = overridden ? "Accepted by you — small typo"
                         : success ? "Correct — well done"
                         : a.action == Action::Reveal ? "Answer revealed — try again later"
                         : "Incorrect — review the correction";
    if (t.mode == "text") {
      auto raw = wide(a.entered.empty() ? answer_text(a.rows) : a.entered);
      answer_bar(start, raw, raw.size(), success ? good : bad);
      centered(start + 2, status, success ? good : bad);
      start += 4;
    } else {
      centered(start - 1, status, success ? good : bad);
    }
    struct StyledLine {
      std::string text;
      int attr;
    };
    std::vector<StyledLine> display;
    auto add = [&](const std::string &text, int attr = 0) {
      for (const auto &part : wrapped(text, COLS - 5))
        display.push_back({part, attr});
    };
    if (t.mode == "multiple_choice") {
      for (size_t i = 0; i < t.choices.size(); ++i) {
        bool chosen = int(i) == a.selected;
        bool correct = t.choices[i] == t.entry;
        std::string label = chosen    ? (correct ? "Your answer — correct"
                                                 : "Your answer — incorrect")
                            : correct ? "Correct answer"
                                      : "";
        std::string prefix = std::to_string(i + 1) + ". ";

        add(prefix + bundle(deck.entries[t.choices[i]], t.direction),
            correct  ? good
            : chosen ? bad
                     : A_DIM);
        add(label, correct ? good : chosen ? bad : A_DIM);
      }
    } else {
      if (t.direction == "de_fr") {
        if (engine.config()["feedback_detail"].str() != "errors" || !success) {
        add("One sufficient answer", good);
        for (const auto &row : sufficient_rows(deck.entries[t.entry], t.direction, engine.config()))
          add(row.text + (row.gender.empty() ? "" : " (" + row.gender + ")"), good);
        }
      } else {
        add("Accepted answers (any one)", good);
        for (const auto &u : display_units(deck.entries[t.entry], t.direction)) {
          bool typed = std::find(g.explicit_units.begin(), g.explicit_units.end(), u.id) != g.explicit_units.end();
          add(u.accept.front() + (typed ? "  [entered]" : "  [alternative]"), good);
        }
      }
      for (const auto &error : g.errors.items())
        add(error["kind"].str() + ": " + error["message"].str(), bad);

    }
    bool can = deck.entries[t.entry].override_allowed &&
               engine.config()["allow_sentence_self_override"].boolean() &&
               t.mode == "text" && !g.correct && a.action != Action::Reveal &&
               !overridden;
    if (overridden)
      add("Your correction counts; the original mismatch is retained.", good);
    int visible = std::max(1, LINES - start - 4);
    scroll = std::clamp(scroll, 0, std::max(0, int(display.size()) - visible));
    for (int i = scroll; i < int(display.size()) && i < scroll + visible; ++i)
      centered(start + i - scroll, display[i].text, display[i].attr);
    if (int(display.size()) > visible)
      centered(LINES - 4,
          "j/k scroll answers (" + std::to_string(scroll + 1) + "–" +
              std::to_string(std::min<int>(display.size(), scroll + visible)) +
              " of " + std::to_string(display.size()) + ")",
          A_DIM);
    footer(success ? "Next word…" : can
               ? "Enter continue   c I was correct (small typo)   j/k scroll"
               : "Enter next question   j/k scroll answers", &accuracy);
    refresh();
    if (success && !advance_at)
      advance_at = std::chrono::steady_clock::now() + std::chrono::milliseconds(200);
    if (advance_at) {
      auto left = std::chrono::duration_cast<std::chrono::milliseconds>(
          *advance_at - std::chrono::steady_clock::now()).count();
      if (left <= 0) return;
      wtimeout(stdscr, int(left));
    }
    auto k = key();
    wtimeout(stdscr, -1);
    if (advance_at && k.code < 0) return;
    if (enter(k))
      return;
    if (k.code == 'c' && can) {
      engine.override_sentence(event);
      overridden = true;
    }
    if (k.code == 'j' || k.code == KEY_DOWN)
      ++scroll;
    if (k.code == 'k' || k.code == KEY_UP)
      scroll = std::max(0, scroll - 1);
  }
}
std::vector<std::string> summary_lines(const J &s, const J &) {
  const auto &latest = s["written_accuracy"];
  WrittenAccuracy accuracy{
      size_t(latest["total"].num()), size_t(latest["tested"].num()),
      size_t(latest["correct"].num()), size_t(latest["overrides"].num())};
  return {accuracy_label(accuracy) + "  |  Goal 95%",
          "Latest written answer per word, in either direction.",
          std::to_string(accuracy.total - accuracy.tested) +
              " words have no written attempt yet.",
          std::to_string(accuracy.overrides) +
              " latest answers accepted with a sentence typo override.",
          "",
          "This session",
          "Written: " + std::to_string(s["text_correct"].num()) + " / " +
              std::to_string(s["text_attempts"].num()) + " correct",
          "Literal written matches: " + std::to_string(s["raw_correct"].num()),
          "Recognition: " + std::to_string(s["choice_correct"].num()) + " / " +
              std::to_string(s["choice_attempts"].num()) + " correct",
          "Multiple choice does not change written accuracy."};
}
void study(Progress &progress, const Deck &deck, const J &config, bool mock,
           uint32_t seed) {
  Engine engine(progress, deck, config, now_seconds, seed, mock);
  while (true) {
    auto task = engine.next();
    if (!task) {
      bool finished =
          mock ? engine.session()["mock_done"].size() >= deck.entries.size()
               : config["session_questions"].num() > 0 &&
                     engine.session()["completed"].num() >= config["session_questions"].num();
      if (!finished) {
        message("No words in this sweep", {
            "The selected difficulty range has no remaining words.",
            "Choose a Full sweep or a sweep with Random words from Learn.",
            "Multiple-choice-only practice also needs enough distinct options."});
        engine.finish();
        return;
      }
      engine.finish();
      if (mock && config["defer_mock_feedback"].boolean()) {
        // A mock asks each entry once, so its deferred feedback can safely
        // revise that entry's outcome without overwriting a later attempt.
        std::vector<size_t> events;
        for (size_t i = 0; i < progress.data["history"].size(); ++i)
          if (progress.data["history"].at(i)["session_id"].str() ==
                  engine.session()["id"].str() &&
              progress.data["history"].at(i)["event"].str() == "attempt")
            events.push_back(i);
        for (auto i : events) {
          const auto ev = progress.data["history"].at(i);
          auto t = Task::from(ev["task"]);
          Answer answer;
          answer.action =
              ev["revealed"].boolean() ? Action::Reveal : Action::Submit;
          for (const auto &row : ev["rows"].items())
            answer.rows.push_back({row["entered"].str(), row["gender"].str(),
                                   row["gender_source"].str()});
          auto g = grade(deck.entries[t.entry], t.direction, answer.rows,
                         engine.config());
          feedback(engine, deck, t, answer, g, i);
        }
      }
      auto accuracy = engine.accuracy();
      message("Session summary",
              summary_lines(engine.summary(), engine.config()), &accuracy);
      return;
    }
    auto t = *task;
    Answer answer = t.mode == "text" ? text_answer(engine, deck, t)
                                     : choice_answer(engine, deck, t);
    if (answer.action == Action::Exit) {
      if (t.mode == "text")
        engine.save_draft({{answer.entered, "", "bar"}});
      return;
    }
    if (answer.action == Action::Skip) {
      engine.skip(t);
      continue;
    }
    Grade g;
    if (t.mode == "text")
      g = grade(deck.entries[t.entry], t.direction, answer.rows,
                engine.config());
    else
      g.correct = answer.selected >= 0 && t.choices[answer.selected] == t.entry;
    if (answer.action == Action::Reveal) {
      g.correct = false;
      g.explicit_units.clear();
      g.inferred_units.clear();
      g.expanded_units.clear();
      g.missing.clear();
      for (const auto &u : display_units(deck.entries[t.entry], t.direction))
        g.missing.push_back(u.id);
    }
    auto index =
        engine.submit(t, answer.rows, g, answer.duration,
                      answer.action == Action::Reveal, answer.selected);
    if (!mock || !config["defer_mock_feedback"].boolean())
      feedback(engine, deck, t, answer, g, index);
  }
}
void save_settings(const Options &options) {
  if (options.config_path.empty())
    return;
  fs::create_directories(fs::absolute(options.config_path).parent_path());
  J c = options.config;
  c.object_items().erase("session_questions");
  c.object_items().erase("sweep");
  c["recent_vocab"] = options.vocab;
  c["recent_progress"] = options.progress;
  atomic_write(options.config_path, c.dump(true) + "\n");
}
std::string sample_path() {
  for (const auto &p : {fs::path("vocab/demo.json"),
                        fs::path("/usr/local/share/fvoci/demo.json"),
                        fs::path("/usr/share/fvoci/demo.json")})
    if (fs::exists(p))
      return fs::absolute(p).string();
  char b[PATH_MAX];
  auto n = readlink("/proc/self/exe", b, sizeof b - 1);
  if (n > 0) {
    b[n] = 0;
    auto p = fs::path(b).parent_path().parent_path() / "share/fvoci/demo.json";
    if (fs::exists(p))
      return p.string();
  }
  return {};
}
bool select_sweep(Progress &progress, const Deck &deck, J &config) {
  const std::vector<std::string> modes = {"default", "full", "difficult", "very_difficult", "difficult_random", "very_difficult_random"};
  while (true) {
    auto overview = sweep_overview(progress.data, deck, config);
    std::vector<std::string> choices{
        "Default (" + sweep_label(overview["default_mode"].str()) + ")",
        "Full sweep (" + std::to_string(overview["full"].num()) + ")",
        "Difficult sweep (" + std::to_string(overview["difficult"].num()) + ")",
        "Very difficult sweep (" + std::to_string(overview["very_difficult"].num()) + ")",
        "Difficult sweep + Random", "Very difficult sweep + Random"};
    const auto &saved = progress.data["session"];
    bool resume = saved["scheduler"].str() == "sweeps-v1" && !saved["finished"].boolean() &&
                  saved["deck_id"].str() == deck.id && saved["policy_id"].str() == hash(policy(config).dump());
    if (resume) choices.push_back("Resume current sweep (" + sweep_label(saved["sweep"]["mode"].str()) + ")");
    int selected = 0;
    for (size_t i = 0; i < modes.size(); ++i) if (modes[i] == config["sweep"].str()) selected = i;
    auto accuracy = written_accuracy(progress.data, deck, hash(policy(config).dump()));
    int choice = menu("Learn", choices, selected, &accuracy);
    if (choice < 0) return false;
    if (choice == 6 && resume) {
      auto settings = saved["settings"];
      for (auto key : {"accent_macros", "accent_macros_enabled", "keybindings", "keymap", "color", "theme", "profile_directory", "feedback_detail"})
        settings[key] = config[key];
      config = settings;
      return true;
    }
    if ((choice == 2 && overview["difficult"].num() == 0) ||
        (choice == 3 && overview["very_difficult"].num() == 0)) {
      message("No matching words", {"No words currently meet this difficulty threshold.",
                                    "Choose Full sweep or add Random words."});
      continue;
    }
    config["sweep"] = modes.at(choice);
    return true;
  }
}
} // namespace
int run_ui(Options options) {
  if (!isatty(STDIN_FILENO) || !isatty(STDOUT_FILENO))
    throw std::runtime_error("The TUI needs a terminal. Use --validate or "
                             "--grade for noninteractive commands.");
  Screen screen(options.config);
  if (options.vocab.empty())
    options.vocab = options.config["recent_vocab"].str();
  if (options.vocab.empty())
    options.vocab = sample_path();
  if (options.progress.empty())
    options.progress = options.config["recent_progress"].str();
  bool first = true;
  while (true) {
    std::optional<Deck> deck;
    std::string error;
    try {
      if (!options.vocab.empty())
        deck = load_deck(J::read(options.vocab));
    } catch (const std::exception &e) {
      error = e.what();
    }
    std::string info = deck ? deck->title + " (" +
                                  std::to_string(deck->entries.size()) +
                                  " entries)"
                       : error.empty() ? "Choose a vocabulary file"
                                       : error;
    WrittenAccuracy accuracy;
    if (deck) {
      accuracy.total = deck->entries.size();
      if (!options.progress.empty() && fs::exists(options.progress)) {
        try {
          accuracy = written_accuracy(J::read(options.progress), *deck,
                                      hash(policy(options.config).dump()));
        } catch (const std::exception &) { /* Start reports profile errors. */
        }
      }
    }
    int selection;
    if (first && options.mock) {
      selection = 5;
      first = false;
    } else {
      first = false;
      selection =
          menu("Start — " + info,
               {"Vocabulary: " +
                    (options.vocab.empty() ? "[Browse]" : options.vocab),
                "Progress: " +
                    (options.progress.empty() ? "[Browse]" : options.progress),
                "New progress profile",
                "Settings (" + options.config["mode"].str() + ")",
                "Learn",
                "Mock test", "Help", "Quit"},
               0, deck ? &accuracy : nullptr);
    }
    if (selection < 0 || selection == 7)
      return 0;
    try {
      if (selection == 0) {
        auto p = picker(options.vocab);
        if (p) {
          load_deck(J::read(*p));
          options.vocab = *p;
        }
      } else if (selection == 1) {
        auto p = picker(options.progress);
        if (p) {
          Progress check(*p, false);
          options.progress = *p;
          options.create_progress = false;
        }
      } else if (selection == 2) {
        auto p = input("New progress file",
                       options.progress.empty()
                           ? options.config["profile_directory"].str() +
                                 "/school.json"
                           : options.progress);
        if (p && !p->empty()) {
          auto path = home_expand(*p);
          if (!fs::exists(fs::path(fs::absolute(path)).parent_path())) {
            int a = menu("Create missing parent directory?",
                         {"Create directory and profile", "Cancel"});
            if (a != 0)
              continue;
          }
          Progress created(path, true);
          created.save();
          options.progress = created.path();
          options.create_progress = false;
        }
      } else if (selection == 3) {
        settings(options.config);
        save_settings(options);
      } else if (selection == 4 || selection == 5) {
        if (!deck)
          throw std::runtime_error(
              error.empty() ? "Choose a valid vocabulary file first" : error);
        if (options.progress.empty())
          throw std::runtime_error("Choose an existing progress file or select "
                                   "New progress profile");
        if (fs::weakly_canonical(fs::absolute(options.vocab)) ==
            fs::weakly_canonical(fs::absolute(options.progress)))
          throw std::runtime_error(
              "Vocabulary and progress must be separate files");
        Progress progress(options.progress, options.create_progress);
        options.create_progress = false;
        try {
          save_settings(options);
        } catch (const std::exception &e) {
          message("Settings were not saved",
                  {e.what(), "The learning session can continue."});
        }
        if (selection == 4 && !select_sweep(progress, *deck, options.config)) continue;
        study(progress, *deck, options.config, selection == 5, options.seed);
      } else if (selection == 6)
        help();
    } catch (const std::exception &e) {
      message("Cannot continue",
              {e.what(),
               "Existing vocabulary and progress files have not been reset.",
               "If a profile is corrupt, select its .bak file to "
               "inspect/recover it."});
    }
  }
}
} // namespace fv
