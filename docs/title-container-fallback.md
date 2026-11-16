# Title-container fallback: semantic-text-v5

Some SQLite pages use a plain title `div` instead of a page h1. Section headings
can contain several h1s, and a title/contents wrapper can precede introductory
prose. The existing unique-heading rule intentionally rejects those cases, so
body fallback retained the site's preceding menu/search block.

## Bounded selection

The previous priority is preserved: main, article, exact role-main, then heading.
Only after those fail does v5 try `title_container`, before filtered body.

- The document must have a nonempty title. A candidate is a non-skipped HTML
  `div` or `section` directly inside body.
- Its first child with semantic text must be a block wrapper whose normalized
  text equals the full document title. Allowed title blocks are div/section/header
  or h1-h6. Matching collapses HTML whitespace/NBSP after entity decoding, retains
  case and does not strip site-name suffixes.
- The title may instead occupy the first nonempty child of one leading
  div/section/header wrapper. This supports a title-only block followed by a TOC
  inside that leading wrapper. There is no arbitrary ancestor/descendant search.
- Following siblings of the candidate's leading child must contribute at least
  200 normalized UTF-8 bytes. Text inside the leading wrapper does not count
  toward that gate. A title-only wrapper or short teaser cannot satisfy it.
- Exactly one candidate must qualify. Multiple matches fall back to body.

Selection retains the entire candidate with the existing semantic skip rules.
It keeps its title, TOC, introductory prose, section headings and subsequent text.
Direct text before a proposed title is not skipped to find a later match. Body
text and all-document link discovery retain their behavior, including links in
the removed outer menu. Traversal stays iterative; bounded wrapper inspection
over disjoint body children keeps the additional work linear in DOM size.

This is structural evidence, not article-quality classification. Useful text
outside the selected candidate can be omitted; navigation inside it can remain.
Missing/mismatched titles, deeper wrappers and other legacy layouts still use
the older branches or body fallback. No domain/class/ID selector, CSS evaluation,
general readability score or JavaScript execution is added.

## Controlled comparison

Both versions processed the identical bytes of all 21 locally saved HTML files
from the [v4 review](h2-fallback.md). Their recorded HTML fingerprints were
checked before comparison. Seven SQLite pages changed from body to title-container;
fourteen outputs were unchanged, including prior heading selections.

| SQLite path | v4 text bytes | v5 text bytes |
| --- | ---: | ---: |
| `/whentouse.html` | 15244 | 15048 |
| `/fullsql.html` | 1798 | 1602 |
| `/amalgamation.html` | 6831 | 6635 |
| `/fasterthanfs.html` | 17503 | 17307 |
| `/testing.html` | 45298 | 45102 |
| `/fileformat.html` | 82069 | 81873 |
| `/lts.html` | 3831 | 3635 |

Every changed output is an exact suffix of the old normalized text. In each case
the removed prefix is the same inspected 196-byte menu/search/slogan prelude;
no trailing bytes were removed. The selected title, TOC, introduction and all
following content remain byte for byte. This establishes a narrow observed
improvement on these snapshots, not general extraction accuracy. HTML snapshots,
comparison JSON and extracted texts stay under ignored `build/title-review-v5/`
and `build/body-review-v4/`; no live recrawl was needed for this check.

## Export and tests

New exports carry `extractor_version: "semantic-text-v5"` and can use
`text_source: "title_container"`. Corpus schema and field types remain unchanged.
For affected pages the text hash changes while URL-based page IDs remain stable;
indexers should preserve this provenance and apply their usual update logic.
The current verifier recognizes the new source; older snapshot versions remain
supported. URL export selection itself remains `url-export-v1`.

Five GoogleTest cases cover direct/nested title blocks, TOC/intro preservation,
ambiguity/content/title guards, legacy branch priority, UTF-8 byte counting and
deep markup. A loopback CLI case checks the new source/version, exact-text hash
and discovery of a link outside the selected container. The verifier source-count
case also covers the new label. All 180 CTest entries passed in Debug and Release
on MinGW GCC 15.2.0. Existing saved 500-page handoffs remain their original v4
snapshots; the current verifier still accepts the saved 500-page/six-host run.
