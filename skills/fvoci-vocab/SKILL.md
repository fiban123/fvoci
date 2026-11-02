---
name: fvoci-vocab
description: Convert German–French school vocabulary PDFs, images, or text into validated fvoci vocabulary JSON, preserving required synonyms, gender, accents, and irregular forms.
---

# Generate fvoci vocabulary

Produce a UTF-8 vocabulary JSON file for fvoci. Read [the format reference](references/format.md) before converting; use [the JSON Schema](references/vocabulary.schema.json) for the structural contract. The application command `fvoci --validate FILE` is the authoritative semantic validator. If working in the fvoci checkout, use `make` and `./build/fvoci --validate FILE` instead.

## Source interpretation

Inspect the actual source, not only extracted text. In the supplied school PDF, `pdftotext` loses accents that remain visible on the page. For a PDF, extract its text for row alignment and render every relevant page for visual inspection (for example, `pdftoppm -r 150 -png INPUT.pdf /tmp/fvoci-page`). Use available image-viewing tools on the rendered pages. When image tools are unavailable, explicitly identify the visual verification gap; do not describe the result as verified.

Treat slashes according to their meaning, not as a universal separator:

- `l’œil / les yeux`: one noun group, masculine singular and plural.
- `traiter / soigner`: two required French groups under one German prompt. Both are required; they are not accepted alternatives.
- `die Krankheit / krank → la maladie / malade`: two entries of different parts of speech, linked with `related_entry_ids` and their shared source row.
- `fort,e`, `sain/saine`: one adjective with gender forms.
- `(se) blesser`: the reflexive and non-reflexive constructions have different German prompts and should normally become separate verb entries.

Keep the teacher's vocabulary and senses. Do not add outside synonyms just because a dictionary lists them. Accepted alternatives are equivalent answers to one requirement, not extra vocabulary the student is meant to remember. Preserve German distinctions such as `Hals (von aussen)` versus `Hals (von innen)`.

## Forms and spelling

Store the full forms explicitly requested by the source. Expand source abbreviations such as `handicapé,e` into their written-out forms, but do not add a grammatical syllabus. Student text-expansion shortcuts are input syntax, never answer-key content.

- Nouns: record only the listed genders and numbers. `le chat` alone requires only `m_sg`; do not add `les chats`. `l’œil / les yeux` requires both listed forms. Do not invent opposite-gender counterparts. Include definite articles, and encode gender in the form key even for `l’` or `les`.
- Adjectives: expand only explicitly listed forms or abbreviated endings. `sain,e` requires `sain` and `saine`, not unlisted plurals. Record unchanged forms in both gender slots only when both genders are requested. Verify exceptions; do not assume that appending `e` or `s` is always correct.
- Verbs: default to the infinitive including required reflexive pronouns and prepositions. Add supported present-tense forms only if the assignment requests them. Do not infer a conjugation syllabus from the existence of a verb.
- Full sentences and fixed expressions: `other`, exact text alternatives. Set `allow_self_override: true` for full sentences where a learner should be able to accept a small typo.

Preserve accents, capitalization, apostrophes, and ligatures. `œ` is not interchangeable with every `oe`; `moelle` stays unjoined, and `Noël` needs `ë`. Store actual Unicode letters, never typing macros such as `e+`, `e:` or `oe+`.

A visible source typo and a legitimate exception are different. Verify uncertain spellings/forms using a reliable dictionary when available. Retain the source spelling and explain any justified correction in `notes`. Do not silently convert uncertain guesses into accepted answers. If uncertainty affects correctness or which words are required, ask a focused question or produce a separate `OUTPUT.review.json` recording the affected source rows and proposed readings; clearly state that the deck is not ready for study until those items are resolved.

## Output and checks

Use stable, descriptive IDs. On updates, preserve deck, entry, and group IDs for unchanged meanings and increment the deck revision. Write only the requested destination, plus an uncertainty report if needed. Never alter an existing progress file. Put the exact source filename, one-based page, and printed row label on each entry where available. Report any omitted source rows and why.

Validate the generated file with fvoci, correct structural/semantic failures, and rerun validation. Structural validity does not verify the French: compare the final forms against the rendered source and any documented checks. When no fvoci executable or checkout is available, validate against the supplied schema using an available JSON Schema validator and disclose that application-level validation remains outstanding; do not install software just to hide that limitation.

Summarize the output path, entry count, split rows, corrections, unresolved issues, and validation performed. Avoid creating unrelated project files or changing the learner's scheduling settings.
