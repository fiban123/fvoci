# fvoci

A terminal vocabulary trainer for a German-speaking student learning French. The goal is accurate recall of school vocabulary, including gender, accents, required synonyms, and irregular forms, with as little repetitive typing as possible.

The target is approximately 95% of the available test marks. Practice defaults to 90% German → French and 10% French → German. Those percentages describe question direction, not a guarantee about eventual grades.

**Status:** fvoci 0.1 is implemented: a C++17 TUI with written and multiple-choice practice, pairwise answer expansion, Unicode accent macros, persistent progress, question-spaced sweeps, sentence overrides, and mock tests. A vocabulary-generation skill and versioned JSON Schema are included. The scheduling constants are starting heuristics to refine through actual use.

## Build, install, and run

Requirements: a C++17 compiler, GNU Make, `pkg-config`, and development packages for `ncursesw`, `libutf8proc`, `json-c`, and OpenSSL (`libcrypto`). Python 3 is needed for the integration tests. The Makefile resolves compiler/linker flags through `pkg-config`; no dependencies are downloaded during the build.

```sh
make
make test
make run
make clean
```

The executable is `build/fvoci`. On environments where `rm` is a wrapper for `trash-put`, use `make clean RM='/bin/rm -f'` if normal cleanup stalls.

Install the executable and demo deck:

```sh
sudo make install              # /usr/local/bin/fvoci and /usr/local/share/fvoci/demo.json
make install PREFIX="$HOME/.local"  # alternative without root
```

`PREFIX`, `BINDIR`, `DATADIR`, and `DESTDIR` are supported. `make uninstall` removes the installed executable and demo deck, leaving your vocabulary and progress untouched. A staged installation can be checked with `make install DESTDIR=/tmp/fvoci-stage`.

Start with the included 12-entry demo:

```sh
./build/fvoci
./build/fvoci --vocab vocab/demo.json --progress progress/school.json
./build/fvoci --vocab vocab/demo.json --progress progress/school.json --mode text
./build/fvoci --validate vocab/demo.json
./build/fvoci --help
```

The first command opens the starting screen. Select Learn with `j/k` and Enter, then choose a sweep. A new path passed through `--progress` is created when you start; an existing profile is opened. Without that argument, choose Browse or New on the starting screen. All learning runs locally and offline; AI is used only to author vocabulary files.

Useful switches include `--direction de_fr`, `--questions 20`, `--sweep full`, `--no-expansion`, `--no-macros`, `--mock`, `--seed N`, and `--config FILE`. `--dump-config` prints effective settings. For noninteractive grading, `--vocab FILE --entry ID --grade ANSWERS.json` reads an array of objects such as `[{"text":"le chat","gender":"m"}]`, prints grading JSON, and exits with 0 for correct, 1 for incorrect, or 2 for invalid input. This command does not modify progress.

### Vocabulary-generation skill

The reusable skill lives in [skills/fvoci-vocab/SKILL.md](skills/fvoci-vocab/SKILL.md), with its [format reference](skills/fvoci-vocab/references/format.md) and [JSON Schema](skills/fvoci-vocab/references/vocabulary.schema.json).

```sh
make install-skill
```

This installs into `$CODEX_HOME/skills/fvoci-vocab`, or `~/.codex/skills/fvoci-vocab` when `CODEX_HOME` is unset. Override `SKILLDIR` if needed; `DESTDIR` also works for skill installation. In Codex, use `$fvoci-vocab` to convert a vocabulary PDF to a specified JSON file. Reload your skill catalog/session if the new skill is not immediately listed. The skill requires visual verification of PDF accents, separates mixed parts of speech, preserves required synonyms, and validates output with `fvoci --validate`. It does not modify learner progress. The demo is a small hand-authored practice set with explicitly selected requirements. The supplied PDF’s full conversion is `vocab/lecon-6.json`: 124 entries, retaining source-listed forms only.

## Learning and grading model

An **entry** is one question with one part of speech. French production requires all listed groups and forms; German production accepts any one listed form or synonym. An entry has one or more **required answer groups**. Each French group represents a word or expression and contains its **required forms**. Each form may have several **accepted alternatives**.

- All required groups must be covered. If a prompt requires two French synonyms, supplying only one is incomplete.
- All listed forms within each group must be covered, by literal text or by the learner’s text expansion.
- Any one accepted alternative satisfies its form. Alternatives are not additional required synonyms.
- Order does not matter. Repeating an answer does not satisfy another distinct group.
- Omitted forms are not required. No extra genders, plurals, or conjugations are invented by the vocabulary loader.
- A mixed source row such as `die Krankheit / krank → la maladie / malade` becomes two entries, with separate progress. A shared source reference preserves the connection.

For the deliberately hypothetical synonym set `le chat / le cola`, store both as required groups. If the source lists only those singulars, those are the only requirements. `le chat, le cola` covers both; `le chat/cola` also expands to both. Do not add unlisted plurals or opposite-gender forms during conversion.

Every submitted or expanded French answer must match a listed form or accepted alternative. An extra incorrect answer makes the attempt wrong, even when all required forms are also present. Duplicate correct answers are harmless. Expansion does not consult the answer key to filter guesses or manufacture grammatical endings.

## Vocabulary JSON: version 1

Files are UTF-8 JSON. IDs are stable identifiers, independent of spelling or array position. Required fields appear in the example; `source`, `related_entry_ids`, and `notes` are optional. The legacy optional `test_date` field accepts a local calendar date such as `2026-11-20`; it is metadata only and does not affect learning.

```json
{
  "schema_version": 1,
  "id": "school-french-lecon-06",
  "revision": 1,
  "title": "Vocabulaire leçon 6",
  "languages": { "prompt": "de", "answer": "fr" },
  "entries": [
    {
      "id": "cat",
      "type": "noun",
      "de": {
        "prompt": "die Katze",
        "answers": [{ "id": "cat-de", "accept": ["die Katze", "Katze"] }]
      },
      "fr": [
        {
          "id": "chat",
          "forms": { "m_sg": ["le chat"], "m_pl": ["les chats"] }
        }
      ],
      "notes": "Illustrative entry; only these masculine forms are required."
    },
    {
      "id": "eye",
      "type": "noun",
      "de": {
        "prompt": "das Auge, die Augen",
        "answers": [
          { "id": "singular", "accept": ["das Auge", "Auge"] },
          { "id": "plural", "accept": ["die Augen", "Augen"] }
        ]
      },
      "fr": [
        { "id": "oeil", "forms": { "m_sg": ["l’œil"], "m_pl": ["les yeux"] } }
      ],
      "source": { "file": "Vocabulaire leçon 6.pdf", "page": 1, "row": "4" }
    },
    {
      "id": "feel",
      "type": "verb",
      "de": {
        "prompt": "sich fühlen",
        "answers": [{ "id": "meaning", "accept": ["sich fühlen"] }]
      },
      "fr": [{ "id": "se-sentir", "forms": { "infinitive": ["se sentir"] } }]
    },
    {
      "id": "treat",
      "type": "verb",
      "de": {
        "prompt": "behandeln",
        "answers": [{ "id": "meaning", "accept": ["behandeln"] }]
      },
      "fr": [
        { "id": "traiter", "forms": { "infinitive": ["traiter"] } },
        { "id": "soigner", "forms": { "infinitive": ["soigner"] } }
      ]
    },
    {
      "id": "healthy",
      "type": "adjective",
      "de": {
        "prompt": "gesund",
        "answers": [{ "id": "meaning", "accept": ["gesund"] }]
      },
      "fr": [
        {
          "id": "sain",
          "forms": {
            "m_sg": ["sain"], "f_sg": ["saine"],
            "m_pl": ["sains"], "f_pl": ["saines"]
          }
        }
      ]
    },
    {
      "id": "feel-unwell",
      "type": "other",
      "de": {
        "prompt": "Ich fühle mich nicht gut.",
        "answers": [{ "id": "meaning", "accept": ["Ich fühle mich nicht gut."] }]
      },
      "fr": [
        { "id": "sentence", "forms": { "text": ["Je ne me sens pas bien."] } }
      ],
      "allow_self_override": true
    }
  ]
}
```

### Nouns

Allowed form keys are `m_sg`, `f_sg`, `m_pl`, and `f_pl`. At least one is required; absent keys are outside the entry's requirements. The key stores grammatical gender even when the written article is `l’` or `les`.

Every stored noun answer includes a definite article: `le`, `la`, `l’`, or `les`. The learner must also enter an article. Indefinite articles and bare nouns are outside the initial contract. Apostrophe style is normalized, so `l'œil` and `l’œil` are equivalent.

Each form's array contains alternative spellings of that same form. Distinct required synonyms go into separate objects in `fr`. The schema does not assume that every noun has a feminine counterpart or is used in both numbers.

### Verbs

The default required form is `infinitive`. Reflexive pronouns and required prepositions are part of the answer: `se sentir`, `s’habituer à`, and `dépendre de` must be entered in full.

Additional forms are required only when explicitly listed. The initial optional conjugation keys are `present.je`, `present.tu`, `present.il`, `present.nous`, `present.vous`, and `present.ils`. Their answers include the subject, for example `"present.je": ["je me sens"]`. Entries using them must also list `infinitive`; the prompt announces which extra forms are required. Additional tenses need a documented schema extension rather than ambiguous unnamed answer strings.

Each listed verb form and each required synonymous verb must be covered. `traiter/soigner` or `traiter, soigner` supplies both required synonyms; `traiter / soigner` is invalid because the slash must touch its alternatives. Phrases whose structure should not be treated as a verb exercise use `other`.

### Adjectives

Adjectives use the same four available form keys as nouns, but contain no articles. Only source-listed forms are required; expand a source abbreviation such as `sain,e` to `sain` and `saine`, without adding plurals. Exact spelling identifies the required forms, so adjective gender annotations are optional. An explicitly supplied `(m)`, `(f)`, or `(mf)` constrains matching and must be correct.

If `malade` is stored under both singular genders, entering `malade` covers both identical spellings. If `malades` is also explicitly listed, it must also be supplied, for example `malade.s`. No endings are added automatically.

### Other: sentences, expressions, and uninflected words

These use a single `text` form per required group. Matching uses normalized text equivalence, not a language model or semantic similarity. Alternative phrasings must be explicitly listed in the array.

`allow_self_override` defaults to false and is permitted only on `other` entries. Set it to true for full sentences where the learner may accept a small typo after seeing the correction. It does not enable automatic typo forgiveness.

### German answers and reverse questions

`de.prompt` is display text. For French → German questions, **any one string from any `de.answers[].accept` list earns full credit**. Singulars, plurals, and synonyms are all alternatives, regardless of their group IDs. German matching always ignores case and Unicode punctuation, including punctuation inside phrases. It still requires a listed answer: it does not invent synonyms or German inflections. Umlauts remain significant.

A reverse question displays the canonical French forms from all required groups; the first string in each form array is canonical. German answers need no gender annotations or morphological inference. One listed answer is enough; if several comma- or semicolon-separated answers are entered, any match suffices. German article-free answers are accepted only when explicitly listed, as in the example.

### Validation and authoring

The loader rejects unsupported schema versions, duplicate IDs within their scope, empty requirements, unknown form keys, incompatible noun articles/genders/numbers, and duplicate synonymous groups that require the same normalized answer. Identical adjective spellings across gender/number slots are valid. Alternative answers within one slot must not accidentally encode two required synonyms. Self-overrides on nouns, verbs, or adjectives are rejected.

Vocabulary files contain answers and requirements, not learner state. File order provides a predictable introduction order. The generation skill preserves source page/row information, splits mixed parts of speech, distinguishes synonyms from inflections, and verifies accents against the rendered PDF. The example PDF's extracted text loses accents visible on the page; text extraction alone is insufficient. Conversion is an AI-assisted authoring workflow, not an embedded PDF parser or an online dependency of the trainer.

## Pairwise text expansion

`text-expansion-v1` replaces grammatical inference. The vocabulary remains a structured answer key, containing full Unicode forms. The learner supplies a compact expression that expands into an answer list; the app checks that list against exactly the stored requirements.

Alternatives **align by index**, including across spaces. They never form a Cartesian product. Literal text and one-item groups repeat across every position. All other lists in one expression must have the same length; mismatched lengths are an input error. Keep duplicate alternatives because their positions matter.

| Input | Expanded answers |
| --- | --- |
| `le/la handicapé.e` | `le handicapé`, `la handicapée` |
| `le/les chat.s(m)` | `le chat`, `les chats (m)` |
| `le/la/les handicapé.e.es(mf)` | `le handicapé`, `la handicapée`, `les handicapées (mf)` |
| `traiter/soigner` | `traiter`, `soigner` |
| `hell{o,oe,bo}` | `hello`, `helloe`, `hellbo` |
| `hell{o,o,b}{,e,o}` | `hello`, `helloe`, `hellbo` |
| `hell.o.oe.bo` | `hell`, `hello`, `helloe`, `hellbo` |
| `{,re}faire` or `.re{faire}` | `faire`, `refaire` |

The third row demonstrates expansion syntax, not a claim that those forms and gender annotations are correct French or required by a particular deck. Every output is graded.

- A slash chooses between adjacent tokens or brace groups, with **no spaces around `/`**. `traiter / soigner` is invalid.
- A dot introduces optional text: `.e.es` is the ordered list `"", "e", "es"`. It includes the unchanged base. Thus `hell.o.oe.bo` fails when the required list contains only `hello`, `helloe`, and `hellbo`, because `hell` is extra.
- Braces contain comma-separated alternatives, including empty alternatives. Whitespace around each option is trimmed. Braces can be nested; adjacent groups align pairwise.
- Commas outside braces start independent answers. `traiter,soigner` and `traiter, soigner` are equivalent. `{hello,helloe, hellbo}` and `hell{o,oe}, hellb{o}` also cover the three-word example.
- Nouns still require definite articles. `le` and `la` supply gender; `l’` and `les` require `(m)`, `(f)`, or `(mf)`. An `(mf)` annotation lets a definite singular article select its gender; ambiguous articles still claim both genders and must match both listed requirements. A conflicting annotation is wrong. An individual brace alternative can carry its own annotation.

Expansion applies to French nouns, adjectives, and verbs. Full sentences and other expressions retain literal punctuation, including commas and periods; German answers retain their forgiving literal matching. The acute-accent macro is now **`e+ → é`**, reserving `/` for alternatives. `oe+ → œ` and `ae+ → æ` keep working through longest-match macro expansion.

Malformed expressions produce feedback, without crashing or advancing past the correction. Expressions are limited to 8,192 UTF-8 bytes, 16 brace levels, 256 outputs per expression, and 1,024 outputs per submission. Even 32 adjacent `{a,b}` groups generate only two answers.

The Settings toggle or `--no-expansion` disables the shorthand and requires literal forms. The configuration key remains `inference` for compatibility, with values `text-expansion-v1` and `none`; old `naive-v1` settings upgrade on load. `--no-inference` remains an alias for `--no-expansion`.

Progress retains the submitted expressions, `expanded_answers`, and `expanded_units`; it no longer records new grammatical inference. Old histories remain intact under their original grading policy. The new matching policy has separate accuracy and learning evidence, so historical successes using automatic grammar do not count as proof of recall under these rules. Feedback lists the required full forms, with redundant noun gender labels omitted.

## Text normalization and accent macros

Matching first normalizes to Unicode NFC, trims the ends, collapses whitespace runs, and maps straight/curly apostrophes to one representation. It does not remove accents, change gender, drop words, or silently accept approximate matches. Case remains significant for French nouns, verbs, and adjectives by default. German answers always ignore case and punctuation, independently of the French matching settings.

For `other` entries, casing and all Unicode punctuation are ignored by default. The independent `sentence_case_sensitive` and `sentence_ignore_punctuation` settings can restore strict matching. Accents and word order remain significant. Ligatures are not silently replaced: use the macro below to enter `œ` explicitly.

Accent macros are text-entry conveniences, expanded while typing in insert mode, before text expansion and matching:

| Input | Inserted text | Input | Inserted text |
| --- | --- | --- | --- |
| `e+` | `é` | `e\` | `è` |
| `a\` | `à` | `u\` | `ù` |
| `a^` | `â` | `e^` | `ê` |
| `i^` | `î` | `o^` | `ô` |
| `u^` | `û` | `e:` | `ë` |
| `i:` | `ï` | `u:` | `ü` |
| `a:` | `ä` | `o:` | `ö` |
| `y:` | `ÿ` | `c,` | `ç` |
| `oe+` | `œ` | `ae+` | `æ` |

Uppercase equivalents are supported, including `E+ → É`, `C, → Ç`, `OE+ → Œ`, and `AE+ → Æ`. Macro expansion uses the longest matching suffix. Ligatures use `+` so they do not conflict with the diaeresis macro: `Noe:l → Noël`, `Israe:l → Israël`, and `coe+ur → cœur`. Unmapped combinations remain literal. Direct Unicode typing and pasting are supported; pasted text is not rewritten as macros. `Ctrl-V` quotes the next typed character to suppress a macro. Backspace removes a displayed character, not one byte of UTF-8.

Macros apply in the answer bar, not filenames, the file picker, or Vim commands. Commas separate forms; use Ctrl-V before a comma when a literal comma immediately after `c` would otherwise expand to `ç`. The macro map is customizable and can be disabled. It does not affect grading: the submitted expanded Unicode text is what is checked. Preserve both the entered text and normalized submitted text in history; keystroke logging is unnecessary.

## Learning sweeps and difficulty scores

Learning is based on completed questions, never elapsed time. There is no daily new-word quota, due date, review interval, or waiting for a timer. Practice continues into the next sweep until you pause and exit. The scheduling rules are tunable heuristics, not a claim of an optimal memory model.

### First two full sweeps

In adaptive mode, each word starts with multiple choice. A wrong answer leaves it at that stage. A correct choice advances it to written recall. A wrong written answer stays at the written stage; a correct one finishes that word for the sweep. Finished words are not asked again until a later sweep.

After each unfinished attempt, draw a fresh target gap uniformly from **3–10 intervening questions**. New words fill the gaps; ready retries take priority, with random tie breaking. This interleaves words instead of drilling one repeatedly. Each word keeps the same translation direction through its stages. At the end of a sweep, or in a tiny deck where no other unfinished word is ready, shorten the gap to the next unfinished word. Do not reask finished words just to fill space, invent completed questions, or wait for time to pass.

All current entries must finish the full sweep for it to count toward the two introductory passes. Skipping a word does not count as mastering it. Text-only explicitly bypasses recognition; multiple-choice-only does not count as a completed introductory pass because it contains no written recall. Small or ambiguous decks with fewer than two valid distractors use writing in adaptive mode.

### Scores and later sweeps

A word has a floating-point difficulty score from **0 to 10**, shared across translation directions and keyed by its current requirements and grading policy.

| Outcome | Score change |
| --- | --- |
| Written mistake or revealed answer | +1.0 |
| Multiple-choice mistake | +0.5 |
| Correct answer during an introductory sweep | No change |
| Complete a later word's required written repetitions without any mistake in that sweep | −0.5 |
| Correct answer after a mistake in the same sweep | No reduction |
| Skip | No change |

Mistakes raise the score immediately. Successes reduce it only after a clean completion, so retrying the correction does not instantly erase the difficulty. Scores stop at zero and ten. A sentence typo override rolls back the mistake's score and sweep-state changes and applies the accepted success once, while preserving the original mismatch in history.

After the two introductory sweeps:

- Score below 1: one written success completes the word.
- Score from 1 to below 2: two separated written successes.
- Score 2 or higher: three separated written successes.
- Score 3 or higher: begin with a multiple-choice support question, then require the written successes. Two further consecutive written mistakes can bring back recognition support if the score is at least 3.

The required repetition count is fixed when a sweep starts. Recognition success never replaces a required written success. Words with no mistakes remain at score zero. The earlier blanket “90% accuracy disables all multiple choice” rule is replaced by this per-word score rule: an otherwise strong learner can still receive help on a particularly difficult word.

### Random question order

After every current word has completed two introductory full sweeps, an average difficulty score of **1.0 or lower** unlocks random question order. The average covers the entire selected vocabulary, including words outside a difficult-only sweep. New words' initial zero scores cannot unlock it before those introductory passes.

Once unlocked, each next question is chosen uniformly from the eligible unfinished words in the selected sweep. File order and oldest-retry priority no longer determine the sequence. Retry gaps, required stages and repetitions, and exclusion of completed words still apply. If no word is ready near the end of a sweep, choose randomly among the remaining unfinished words and shorten the gap. Immediate repeats are avoided when another eligible word exists.

The unlock is checked before selecting a question and saved per vocabulary, progress profile, and grading policy. It stays enabled even if later mistakes raise the average. Pending questions, drafts, and the random sequence survive resume. Mock tests already use random order regardless of scores. Customize the unlock threshold with `random_order_max_score` (0–10, default 1.0; lower means better recall is needed).

### Learn menu

| Selection | Words included |
| --- | --- |
| Default (current mode) | Automatically chooses Full sweep or Difficult sweep + Random |
| Full sweep (N) | Every word |
| Difficult sweep (N) | Score ≥ 1 |
| Very difficult sweep (N) | Score ≥ 2 |
| Difficult sweep + Random | Difficult words plus a random sample below that threshold |
| Very difficult sweep + Random | Very difficult words plus a random sample below that threshold |

Counts reflect the selected vocabulary and progress profile. Random selections omit a fixed count in their label. The random portion aims for 20% of the selected pool, with at least one easier word if any exist; it is sampled without replacement and saved with the sweep. If the chosen random mode has no difficult words, sample 20% of the deck instead. Empty non-random selections explain that no words match and let you choose another mode.

Default chooses full sweeps until two introductory full passes are complete. Afterwards it chooses Difficult sweep + Random, with a full sweep after each five completed sweeps, or whenever no difficult words remain. This keeps easy vocabulary in practice. Explicit selections keep using the selected kind of sweep; Default recalculates its choice at each boundary. A saved unfinished sweep can be resumed from Learn, including its draft, selected sample, stages, repetitions, retry positions, and random state.

### Direction, choices, and mock tests

New sweep items receive directions from a shuffled proportional bag, normally nine German → French and one French → German. Retries and follow-up stages keep their item's direction. Explicit direction settings remain available. A mock test is a shuffled, written-only pass through every entry, followed by a summary. It records performance and updates difficulty, but does not count as an introductory learning sweep.

Multiple-choice options are complete answer bundles. Prefer other entries with the same part of speech and answer shape. When too few shape-compatible options exist, broaden to other answers of the same part of speech; never fabricate French words. Reject duplicates and any distractor that the grader would accept. If two valid distractors still cannot be found, adaptive mode uses writing. Multiple-choice-only excludes those items and explains when none are available.

## Live accuracy

The meter is **100 × words correct on their latest written attempt / all words in the selected vocabulary**. A word means one entry, including all required French groups and forms. Either translation direction counts. Untested words remain in the denominator, repeated successes do not add weight, and a later mistake lowers the score. Recognition and skips do not change it. Reveals count as failures; accepted typo overrides count as correct while retaining raw mismatches.

Accuracy and difficulty scores have different purposes: accuracy shows current demonstrated recall, while scores control practice selection. Both use the selected profile, current entry revisions, and grading policy. The 95% goal does not weaken grading or guarantee a future school grade.

## Progress JSON: version 1

Progress remains separate from vocabulary. Existing files and their answer history are retained. The root schema stays at version 1; the new scheduler identifies itself as `sweeps-v1`. Old daily counters and due dates, if present, are ignored. Legacy mistakes seed each word's score from its most recent 20 matching attempts (written +1, recognition +0.5, capped at 10). Old sessions are not credited as full sweeps because full coverage was not tracked.

These excerpts show the new learning records and an unfinished sweep; deck snapshots, grading policies, aggregate counters, and event details are omitted here:

```json
{
  "learning": {
    "school-lecon-06": {
      "<grading-policy-id>": {
        "version": 1,
        "completed_sweeps": 2,
        "random_order_unlocked": true,
        "words": {
          "cat": { "revision": 1, "score": 1.5, "full_sweeps": 2 },
          "eye": { "revision": 1, "score": 0.0, "full_sweeps": 2 }
        }
      }
    }
  },
  "session": {
    "scheduler": "sweeps-v1",
    "completed": 12,
    "sweep_number": 3,
    "sweep": {
      "number": 3,
      "mode": "difficult_random",
      "warmup": false,
      "random_order": true,
      "counted": false,
      "items": {
        "cat": {
          "entry": 0,
          "direction": "de_fr",
          "stage": "text",
          "remaining": 2,
          "written_successes": 0,
          "mistakes": 0,
          "written_failures": 0,
          "next_question": 17,
          "sampled": false
        }
      }
    }
  }
}
```

`stage` is `multiple_choice`, `text`, `done`, or `skipped`. `next_question` is a question-sequence position, not a timestamp. The two introductory passes are tracked per current word through `full_sweeps`; adding or revising a word means it still needs its introductory coverage. A partial or skipped full sweep does not increment those counts.

The legacy-named `schedules` array now holds aggregate per-entry/direction/policy attempt counters and raw per-form statistics. Explicit, text-expanded, legacy-inferred, and failed coverage are separate; an unused German alternative receives neither success nor failure on a successful German answer. History retains raw/effective correctness, answer rows, choices, timing metadata, requirements revision, and snapshots needed to undo an accepted typo's score/stage changes. Timestamps are audit metadata only and never influence selection.

Requirement fingerprints cover `type`, `de`, `fr`, and `allow_self_override`; notes and titles do not reset learning. Changed requirements reset only that entry's learning evidence, preserving history. Grading-policy changes have separate records; appearance and keybindings do not reset a session. Selecting another sweep or changing learning settings starts a new sweep, retaining scores and historical attempts.

Every answer, skip, override, and paused draft is saved atomically with a backup and an exclusive profile lock. A malformed file is reported rather than reset. Newer unsupported root schema versions are rejected.

## Sentence typo overrides

After an incorrect text answer on an eligible `other` entry, show the submitted answer, expected answer, and its correction. Offer **I was correct — small typo** (`c` on the feedback screen).

The learner's explicit override sets `effective_correct` to true and counts as success for scheduling. It preserves `raw_correct: false`, the original text, raw errors, and `self_override: true`. The scheduler must apply the final outcome exactly once: if the mismatch was already saved, recompute from the pre-attempt state instead of applying failure and then a second independent success. The question remains one attempt.

Overrides do not modify the accepted vocabulary, infer that future similar typos are correct, or overwrite raw per-form statistics. Show literal and override-adjusted accuracy separately. There is no automatic edit-distance threshold: the learner is responsible for using the action for a typo rather than missing knowledge. It is unavailable after revealing an answer, in multiple choice, and on nouns, verbs, or adjectives. It can be disabled globally, including for mock tests.

## Terminal interface

### Starting screen and file selection

```text
fvoci

Vocabulary    ./vocab/lecon-06.json       [Browse]
Progress      ./progress/school.json     [Browse] [New]
Mode          Adaptive
Direction     90% Deutsch → Français / 10% Français → Deutsch
Practice      Unlimited sweeps · retries after 3–10 questions

Untested 10        Accuracy 81.8% (90/110 words)        Goal 95%

[Learn]   [Mock test]   [Settings]   [Quit]
```

The sketch above summarizes the information; the actual screen is a keyboard-driven menu with a colored selection and a visible 95% goal. Browse vocabulary and progress files independently. The picker supports directories, editable paths, and JSON filtering; the last selected paths are remembered. `j/k` selects, Enter opens, `h` goes to the parent directory, `/` filters the current list, and `p` enters a path directly (including a `.bak` profile). Choosing a vocabulary validates it and shows its title and entry count. Start refuses invalid files. The starting screen, practice screens, feedback, and summary share the same latest-written-answer accuracy meter.

New progress creates a separate named profile at the chosen path. An existing path must be opened, not silently overwritten. A missing parent directory can be created through an explicit picker action. On first launch, suggest a `progress/` directory; do not create a progress file until the user selects New. Remember recent paths in local application configuration, not inside the vocabulary.

A progress profile may already contain another deck; adding the selected deck retains that history. Resume offers a saved unfinished session when its files and policy still match. Terminal resize preserves input. The single answer bar scrolls horizontally to keep the cursor visible; correction details scroll vertically on small terminals.

### Written question screen

```text
fvoci     Leçon 6
────────────────────────────────────────────────────────


                  Translate into French:
                   das Auge, die Augen

╭──────────────────────────────────────────────────────╮
│              > l’œil (m), les yeux (m)▏                │
╰──────────────────────────────────────────────────────╯


       Accuracy: 81.8%  90/110 words  ·  Goal 95%
────────────────────────────────────────────────────────
Insert   Enter check   Esc normal   i edit   q pause
```

The prompt and answer area form a vertically centered block. Translation instructions, prompt, answer text, feedback, and accuracy are centered horizontally. Instructions and accuracy use dimmed terminal-default text; the prompt uses the accent color and the answer uses the green theme color. The full-width answer box has continuous rounded borders. The header and key hints remain aligned to the left.

Question counters, scheduling reasons, form counts, and entry instructions stay off the practice screen. Answer syntax and comma-separation instructions are available in Help (`Esc`, then `?`). Displayed French forms put singulars before plurals within each required synonym group, including reverse prompts, choices, and corrections.

Enter the source-listed forms into the central answer bar, using pairwise expansion when convenient. `le chat` is enough only if the vocabulary lists that singular alone. If it also lists `les chats`, use `le/les chat.s(m)` or `le chat, les chats (m)`. Separate independent answers with commas; commas inside braces separate alternatives. `l’œil (m), les yeux (m)` covers the irregular pair. Adjective spellings need no gender label unless you want to explicitly specify one. Required verb synonyms can be entered as `soigner/traiter` or `soigner, traiter`.

For `other` entries, commas and parentheses belong to the sentence. If several sentence groups are required, separate them with semicolons. For a single sentence group, semicolons also remain part of the text. Sentence grading ignores casing and Unicode punctuation by default, while preserving accents and word order. `sentence_case_sensitive: true` and `sentence_ignore_punctuation: false` independently restore strict matching for French; German answers always ignore both. Both options are available in Settings. The older `ignore_terminal_punctuation_for_other` option still permits ignoring final `.`, `!`, and `?` when general punctuation matching is strict.

Esc enters normal mode with a visible block cursor; insert mode uses a bar cursor. `i/a` resume editing, `I/A` insert at the beginning/end. Normal mode supports `h/l`, `w/b`, `0/$`, `x`, `dd` (clear the answer), and `u` (undo). Enter submits the bar. Tab is no longer needed. Letters remain text while in insert mode.

Use `?` for help and `q` to pause in normal mode. The pause menu offers resume, reveal, skip, save-and-exit, and help. Skips do not mark a word correct or failed and temporarily exclude it for that sweep. If constraints exclude every question, the app explains why and offers a return to the menu. Multiple choice uses `j/k` or number keys; `?` displays full options.

Corrections appear on the question screen. The prompt and answer box stay in place. Written feedback colors the bar green or red and shows “Correct” or “Incorrect” below it, followed by one sufficient answer computed using the actual grader. Redundant noun gender annotations and identical adjective spellings are omitted. Every distinct listed form remains visible; the vocabulary JSON defines exactly what is required. Multiple-choice options stay in their original order and positions: the wrong selection turns red, the right answer green, with sentence-case labels. Correct answers (written or multiple choice) advance automatically after about **0.2 seconds**; incorrect or revealed answers wait for Enter. Eligible sentence mistakes offer `c` for “I was correct”; the override updates accuracy immediately and retains the original mismatch.

The centered accuracy meter above the footer remains visible with the 95% goal. Recognition answers leave it unchanged. Color-disabled terminals retain explicit labels. Mock tests end after one pass; ordinary practice has no automatic endpoint unless an explicit per-run question limit was selected.

## Customization

Settings live in a separate application configuration file, suggested at `$XDG_CONFIG_HOME/fvoci/config.json`, falling back to `~/.config/fvoci/config.json`. Apply built-in defaults, then user configuration, then command-line options, then current-session choices. Deck contents determine answer requirements; configuration must not silently remove a required synonym or irregular form.

Configuration (omitted fields retain their built-in defaults):

```json
{
  "schema_version": 1,
  "mode": "adaptive",
  "direction_weights": { "de_fr": 90, "fr_de": 10 },
  "sweep": "default",
  "retry_gap_min": 3,
  "retry_gap_max": 10,
  "difficulty_threshold": 1.0,
  "very_difficult_threshold": 2.0,
  "choice_support_score": 3.0,
  "written_mistake_weight": 1.0,
  "choice_mistake_weight": 0.5,
  "clean_sweep_decay": 0.5,
  "random_fraction": 0.2,
  "full_sweep_every": 5,
  "random_order_max_score": 1.0,
  "inference": "text-expansion-v1",
  "case_sensitive": true,
  "sentence_case_sensitive": false,
  "sentence_ignore_punctuation": true,
  "ignore_terminal_punctuation_for_other": false,
  "allow_sentence_self_override": true,
  "accent_macros_enabled": true,
  "keymap": "vim",
  "color": "auto"
}
```

Additional settings:

- `accent_macros`: an object mapping input sequences to Unicode replacements, merged over the default table.
- `keybindings.normal`: maps individual typed characters to existing normal-mode commands, for example `{"H":"0","L":"$"}`. Insert-mode letters remain ordinary text.
- `choice_options`: 3–9, default 4.
- `theme`: explicit `#RRGGBB` colors for `accent`, `success`, and `error`, defaulting to `#56b6c2`, `#50d890`, and `#ff6464`. ncurses uses `xterm-direct` to emit truecolor ANSI escape sequences, independent of your terminal palette. If direct-color terminfo is unavailable, rendering falls back to the terminal’s supported colors. Legacy 0–7 settings map to a fixed RGB palette. `color` can be `auto`, `always`, or `never`.
- `feedback_detail`: `full` (default) or `errors` to hide the sufficient-answer suggestion on successful attempts.
- `profile_directory`: suggested directory for New progress, default `progress`.
- `defer_mock_feedback`: false by default; true shows all mock-test corrections at the end, with eligible sentence overrides still available.

Set `inference` to `none` to disable answer expansion. Settings are validated before starting: direction weights must be nonnegative with a positive sum, retry gaps must be positive and ordered, score weights and thresholds must be positive finite numbers, and the random fraction must be between zero and one. The per-run question limit is zero (unlimited) or positive. Ratios use proportional shuffled bags, reducing integer weights by their greatest common divisor. Learning-setting changes start a fresh sweep while retaining scores and history; appearance, macros, and keybindings preserve a saved session.

Useful presets:

- **Write-only:** `mode: "text"`; only written questions.
- **French production only:** direction weights `100/0`.
- **Literal answers:** disable expansion and type all listed forms separately.
- **Recognition practice:** `mode: "multiple_choice"`; results do not establish written mastery.
- **Mock test:** text only, fixed sampled questions, optional disabled sentence overrides; defer corrections until the end if desired.

The target percentage is a dashboard goal, not a setting that weakens grading. Turning off accent macros still requires accents. Changing grading settings creates a distinct policy in progress; changing appearance or keybindings does not.

## Code and tests

- `src/core.*`: vocabulary validation, Unicode normalization/macros, pairwise text expansion, coverage grading, and distractor validation.
- `src/progress.*`: atomic profile storage, locks, revision snapshots, schedules, history, and resumable sessions. Tests inject the clock and random seed.
- `src/ui.*`: ncursesw screens, Unicode editing, file selection, feedback, and settings.
- `src/json.hpp`: a small value-type wrapper around json-c; `src/main.cpp` implements the CLI.
- `tests/test.cpp`: grading, expansion, Unicode, persistence, locking, overrides, and scheduling checks.
- `tests/integration.py`: real pseudo-terminal sessions covering editing, macros, resizing, sentence overrides, resume, multiple choice, visible feedback colors, accuracy updates, CLI validation, and staged installation. `tests/terminal_screen.py` reconstructs the terminal screen, so assertions check what is displayed rather than assuming every redraw emits complete strings.

Run `make test` after changes. The tests create isolated profiles and installation roots in the temporary directory. They do not install into system directories or modify personal progress. Skill structure can also be checked with the Codex skill-creator's `quick_validate.py`; the published schema documents the structural contract, and the application validator performs additional semantic checks.

The implementation targets a UTF-8 POSIX terminal. It uses `json-c` for parsing, `utf8proc` for normalization/grapheme boundaries, OpenSSL for content fingerprints, and ncursesw for rendering. No API key or network connection is required for practice. Additional verb tenses and a more sophisticated memory model should be introduced through explicit schema/algorithm versions after collecting useful learning evidence.
