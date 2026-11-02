# fvoci vocabulary format, version 1

Read this before authoring a deck. `vocabulary.schema.json` describes structure; `fvoci --validate FILE` also checks IDs, nonempty alternatives, article/gender/number consistency, supported slots, and overlapping answer groups.

## Required structure

```json
{
  "schema_version": 1,
  "id": "school-lecon-06",
  "revision": 1,
  "title": "Vocabulaire leçon 6",
  "languages": { "prompt": "de", "answer": "fr" },
  "entries": [
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
      "source": { "file": "lesson.pdf", "page": 1, "row": "4" }
    }
  ]
}
```

IDs use ASCII letters, digits, hyphens, and underscores. Deck IDs survive file moves. Entry IDs are unique within the deck; group IDs are unique within their French or German list. Do not change IDs merely because a spelling is corrected. `revision` is a positive integer. Optional `test_date` is `YYYY-MM-DD`. The root `entries` array is the default introduction order.

## Required groups versus alternatives

All `fr` objects are required. All form keys in each object are required. The strings within one form's array are accepted alternatives: any one satisfies that requirement.

For `behandeln → traiter / soigner`, use:

```json
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
}
```

Putting both verbs in one `infinitive` array would incorrectly allow either verb alone. Do not duplicate a normalized answer across required synonym groups.

`de.prompt` is the displayed forward question. For reverse questions, any one string in any `de.answers[].accept` list earns full credit, ignoring casing and punctuation. German singulars, plurals, and synonyms are alternatives even across separate group IDs. Include the relevant source forms and synonyms explicitly; fvoci does not infer German inflections or unlisted synonyms. Do not assume prompt punctuation defines accepted answers.

## Parts of speech

- `noun`: allowed form keys `m_sg`, `f_sg`, `m_pl`, `f_pl`. At least one. Full definite articles are mandatory: masculine singular `le` or `l’`, feminine singular `la` or `l’`, plural `les`. Apostrophe styles are equivalent. Opposite-gender counterparts are not automatically required.
- `adjective`: the same four available slots, with no articles. For a source listing `sain,e`, use only `m_sg: ["sain"]`, `f_sg: ["saine"]`. Add plural slots only when the assignment explicitly lists or requests them. Unchanged spellings can appear under both genders when both are requested.
- `verb`: every group has `infinitive`. Optional additional keys are `present.je`, `present.tu`, `present.il`, `present.nous`, `present.vous`, `present.ils`. Include subjects in those answers, e.g. `present.je: ["je me sens"]`. Preserve `se`, `s’`, and required prepositions in infinitives. Other tenses are not supported in version 1; request a format extension instead of inventing keys.
- `other`: each group has only `text`, e.g. `text: ["Je ne me sens pas bien."]`. Full sentences can set the entry-level `allow_self_override: true`; this flag is invalid on other types.

Omitted form keys are not tested. There is no `null` placeholder for an absent form. A four-form adjective needs four keys even if some spellings are identical. Never encode a compound phrase by mechanically appending letters without checking its actual form.

## Metadata and validation

Optional entry fields: `source` with `file`, `page`, `row`; `notes`; and `related_entry_ids`. Use these for split source rows and documented corrections. Keep this metadata out of answer strings. Do not include annotations such as `(m)`, `(pl)`, slash-separated forms, or abbreviated endings inside a stored answer.

Grading preserves accents, normalizes Unicode to NFC, trims/collapses whitespace, and equates straight/curly apostrophes. French word grading preserves case by default. French sentences ignore case and punctuation by default (configurable); German answers always ignore case and punctuation. It does not infer synonyms or judge semantic similarity. The first string in an alternatives list is the canonical display answer.

Examples of CLI checks:

```sh
fvoci --validate vocabulary.json
fvoci --vocab vocabulary.json --entry eye --grade answers.json
```

For the second command, `answers.json` could contain:

```json
[
  { "text": "l’œil", "gender": "m" },
  { "text": "les yeux", "gender": "m" }
]
```

The answer key is a list of requirements, not a dictionary of every possible inflection. Store only source-listed forms, fully written out. The learner can abbreviate those forms using text expansion; the app does not supply grammatical endings automatically. Keep expansion operators out of stored answers. For example, a source listing only `le corps` must not acquire a `les corps` requirement during conversion.
