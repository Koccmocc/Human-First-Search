# URL-based selection for corpus export

`--exclude-export PATTERN` is repeatable. Saved configs use an optional
`export_exclusions` array of strings; omission or `[]` disables the filter.
Rules are explicit inputs, with no built-in host/category blacklist.

```json
"export_exclusions": [
  "https://www.python.org/jobs/*",
  "https://www.kernel.org/doc/html/latest/genindex.html"
]
```

## Matching policy: url-export-v1

- An absolute credential-free HTTP(S) URL matches exactly, ignoring the page's
  query string. A trailing `/*` matches that directory and all path suffixes.
- `https://example.org/jobs/*` matches `/jobs/` and `/jobs/one`, but not `/jobs`
  or `/jobshop/one`. Add the exact slashless URL if it is also needed.
- Rules are canonicalized through Lexbor: scheme/host case, default ports and
  dot segments follow the existing URL policy. Matching uses those serialized
  bytes. Path case and escaped reserved bytes stay significant.
- Scheme, hostname and nondefault port must match. Subdomains need their own
  rules. An allowed hostname appearing in a query/path cannot substitute for the
  real authority. Query strings never participate in selection.
- Only a terminal directory `/*` is supported. Other wildcards, relative URLs,
  credentials, query/fragment rules, backslashes and raw whitespace/controls are
  rejected before requests. Percent-encoded path characters are not decoded.

## Position in the crawl

The fetcher first enforces scope, robots, host delay, redirects and HTTP limits.
Only successful supported UTF-8 HTML reaches selection. The filter uses the
canonical final URL after redirects. An excluded redirect source does not exclude
a different final URL; an excluded final URL does exclude a different source.

The DOM is still parsed and all-document links are offered to the frontier.
Excluded text bypasses main-text extraction, language analysis, delivery and text
hash deduplication. A later allowed URL can export the same text. Completed final
URL deduplication still applies. Exclusions consume target/time/queue budgets,
while `max_pages` counts only delivered records. Existing depth/admission limits
still bound discovery; this is not a request-blocking rule.

The journal emits `export_excluded`, with known final URL/status/input result and
`language: null`. CLI/manifest summary adds `excluded_pages`; it participates in
the target-outcome total. Enabled manifests save normalized `export_exclusions`
and `processing.export_filter: "url-export-v1"`. Corpus schema, extraction version,
page IDs and text-hash algorithms are unchanged.

The updated verifier accepts historical manifests without `excluded_pages` as
zero excluded outcomes. It checks journal accounting, and page audit checks that
no saved corpus URL matches the recorded known-version rules. Current tooling is
needed to verify new runs with nonzero excluded outcomes. The packager includes
the verifier and these public docs from its checkout.

## Trial profile and offline impact

`config/pilot-500-filtered.json` copies the six-host 500-page profile and explicitly
excludes Python job/download trees, PostgreSQL downloads, RFC subject lists and
the Kernel symbol index. It targets 500 newly delivered pages under the same
existing request/robots/time/depth limits.

The actual C++ matcher was applied read-only to all 500 saved URLs from
`data/pilot-500-20261116T181730Z-run.json`. It matched 102 records: Python 28,
PostgreSQL 15, RFC Editor 58 and Kernel 1, totaling 1937347 extracted characters.
The remaining 398 records are an offline selection result, not a new crawler run
or a replacement corpus. Original files/manifests were retained. Inputs and
matched URLs are saved locally in `build/export-preview.json`.

The trial profile needs a fresh crawl to deliver 500 pages under these rules.
This feature does not remove navigation inside admitted pages, establish article
quality or eliminate near duplicates. The existing handoff remains the original
audited 500-record snapshot. Export selection can also omit useful lists, so
rules should be reviewed for the intended search corpus.
