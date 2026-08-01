# 4d-plugin-tidy-html5
4D implementation of [tidy-html5](https://github.com/htacg/tidy-html5).

![version](https://img.shields.io/badge/version-18%2B-EB8E5F)
![platform](https://img.shields.io/static/v1?label=platform&message=mac-intel%20|%20mac-arm%20|%20win-64&color=blue)
[![license](https://img.shields.io/github/license/miyako/4d-plugin-tidy-html5)](LICENSE)
![downloads](https://img.shields.io/github/downloads/miyako/4d-plugin-tidy-html5/total)

The `tidy-html5` plugin wraps [HTML Tidy](https://www.html-tidy.org/) (`libtidy`) to parse, validate, and repair HTML, XHTML, and generic XML documents from 4D. You pass it a document as a `BLOB` plus an optional object of `libtidy` option overrides, and it returns an `Object` containing the corrected markup (as `Text`), the detected document type, and a full error/warning report — all driven by the same option set `libtidy`'s command-line `tidy` tool exposes, just surfaced as object properties instead of CLI flags.

> **A note on scope for this doc:** the plugin's source (`4DPlugin-tidy-html5.cpp`, `.h`) exposes exactly **one** command through its `PluginMain` selector dispatch — there is only one `case` in the `switch(selector)` block. No `.4dm` sample/test files were supplied alongside the source for this revision, so the examples below are illustrative, built only from well-established 4D language fundamentals, and clearly flagged as such rather than copied from a verified test method. The 4D-facing command name itself (`Tidy`) is inferred from the C++ function name and the plugin's project name — the resource file that maps the internal selector to its public 4D command name wasn't part of what was reviewed, so confirm the exact name against your plugin's entry in the 4D Explorer/Language Reference if it differs.

---

## Summary

| Command | Returns | Purpose |
|---|---|---|
| [Tidy](#tidy) | `Object` | Parses and repairs an HTML/XHTML/XML document via `libtidy`, returning the corrected markup, detected doctype/version info, and error/warning diagnostics. |

**Platforms:** macOS and Windows. The header (`4DPlugin-tidy-html5.h`) contains one `#if VERSIONWIN` block, but it only guards a commented-out `#include` — no platform-specific behavior was found anywhere in the reviewed dispatch or option-handling code. Both platforms should behave identically.

---

## Requirements & platform notes

- **Only one command is exposed.** `Tidy` is the entire public surface of this plugin (selector `1` in `PluginMain`). If you were expecting separate commands for, say, reading a doctype or listing available tags, they don't exist here — everything goes through the single `Tidy` call and its `options` object.
- **Both parameters are read, but only the first is mandatory.** The `BLOB` is required for `Tidy` to do anything at all. The `options` object is genuinely optional — every property on it is checked individually before use, so you can omit `options` entirely, or provide it with only the handful of properties you care about, and everything else falls back to `libtidy`'s built-in defaults.
- **Passing an invalid/null `BLOB` fails silently, not with a 4D error.** If the first parameter isn't a usable `BLOB`, `Tidy` returns an **empty object** — no `status`, `html`, `errorCount`, or `info` properties exist on the result at all. Always check that the property you want is actually present before reading it (see [Error handling](#error-handling--troubleshooting)).
- **The `language` option is process-wide, not per-call.** `libtidy`'s `tidySetLanguage()` — which is what backs the `language` option below — sets a single shared setting inside the `libtidy` library itself, not a setting scoped to your particular `Tidy` call. In a 4D Server context where multiple client requests can invoke `Tidy` concurrently, one request's `language` value can affect the diagnostic-message language seen by another request running at the same moment. (The reviewed build serializes the whole `Tidy` call internally to prevent this from corrupting shared state, but the *value* of `language` is still effectively global for the duration of your call — don't rely on two concurrent calls with different `language` settings producing independently-languaged output.)
- **`html` is omitted entirely when there are real errors**, not just warnings. `libtidy` distinguishes warnings from errors; the plugin only calls `tidySaveBuffer` (and therefore only populates `html`) when `errorCount` is `0`. A document with warnings alone (e.g. auto-closed tags) still gets tidied output; a document with genuine parse errors does not — check `errorCount` before reading `html`.
- **Malformed `encoding`/`language` strings are not validated by the plugin.** Both are passed straight through to `libtidy` (`tidySetInCharEncoding` / `tidySetLanguage`); the plugin does not check their return codes. An unrecognized encoding name won't raise a 4D error — behavior falls back to whatever `libtidy` itself does internally for an unrecognized value.
- **Tag-suffix numbers omitted.** Because no verified sample file was available for this revision, the example code below doesn't include internal 4D command-tag suffixes (e.g. `:C1234`) on the built-in commands it uses — add your team's convention if you need them, but don't infer the numbers from this doc.

---

## Tidy

### Syntax
```4d
Tidy ( htmlBlob ; options ) -> Result
```

| Parameter | Type | Description |
|---|---|---|
| `htmlBlob` | `BLOB` | **Mandatory.** Raw bytes of the HTML/XHTML/XML document to process, in the input character encoding declared (or defaulted) via `options.encoding`. Passed directly to `libtidy` without copying. |
| `options` | `Object` | Optional. Bag of `libtidy` option overrides — see [Options object](#options-object) below. Omit it, or omit any of its properties, to keep `libtidy`'s default for that setting. |
| `Result` | `Object` | The tidied output plus diagnostics — see [Result object](#result-object) below. Its property set is data-dependent: `html` is entirely absent when `errorCount` is nonzero, and **every** property is absent if `htmlBlob` was invalid. |

### Description

Each call creates a fresh `libtidy` document, applies every property present on `options` to it, forces the *output* character encoding to UTF-8 (regardless of what `options.encoding` set for input), parses `htmlBlob`, and returns the outcome. The `libtidy` document, its internal buffers, and the lock on `htmlBlob` are all released before `Tidy` returns, whether it succeeds or hits an internal error partway through.

#### Options object

All properties are optional. Where a property isn't present on `options` (or `options` itself isn't passed), `Tidy` leaves the corresponding `libtidy` option at its built-in default.

**Boolean options** — pass `True`/`False`:

| Property | Description |
|---|---|
| `anchorAsName` | Controls whether `<a>` anchors are given a `name` attribute in addition to `id`. |
| `bodyOnly` | Outputs only the content of `<body>`, discarding the rest of the document shell. |
| `coerceEndTags` | Coerces mismatched end tags (e.g. `</i>` closing a `<b>`) instead of leaving them as-is. |
| `decorateInferredUL` | Marks list items that `libtidy` inferred should be wrapped in `<ul>` with a `class` attribute. |
| `dropEmptyElems` | Removes elements that end up empty after cleanup. |
| `dropEmptyParas` | Removes empty `<p>` elements. |
| `dropPropAttrs` | Removes proprietary (non-standard) attributes. |
| `duplicateAttrs` | Controls how duplicate attributes on the same element are resolved (keep-first vs keep-last, per `libtidy`'s convention for this flag). |
| `emacs` | Formats warning/error messages in a GNU Emacs-compatible style (affects the `info` output, not `html`). |
| `encloseBlockText` | Wraps stray text inside block-level elements in a `<p>`. |
| `encloseBodyText` | Wraps stray text directly inside `<body>` in a `<p>`. |
| `escapeCdata` | Converts `<![CDATA[]]>` sections to escaped ordinary text. |
| `escapeScripts` | Escapes `<`/`>` inside `<script>`/`<style>` content that looks like it might close the tag early. |
| `fixBackslash` | Rewrites backslashes in URLs to forward slashes. |
| `fixComments` | Repairs comments that use adjacent hyphens in invalid ways. |
| `fixUri` | Applies URI-encoding cleanup to `href`/`src`-style attribute values. *(Note: the source sets this option twice, back-to-back, from the same property — harmless duplication, not a distinct second setting.)* |
| `gDocClean` | Cleans up markup exported from Google Docs. |
| `hideComments` | Strips HTML comments from the output. |
| `htmlOut` | Forces plain HTML output (as opposed to XHTML/XML). |
| `joinClasses` | Combines multiple `class` values encountered on the same logical element. |
| `joinStyles` | Combines multiple inline `style` values the same way. |
| `keepFileTimes` | Preserves file modification times when `libtidy` writes files directly (not applicable to the blob-in/text-out flow this plugin uses, but the option is still forwarded). |
| `keepTabs` | Preserves literal tab characters in the output instead of expanding them. |
| `literalAttribs` | Leaves attribute values exactly as given, without whitespace normalization. |
| `logicalEmphasis` | Replaces presentational `<b>`/`<i>` with `<strong>`/`<em>`. |
| `lowerLiterals` | Lower-cases literal values in attributes such as enumerated types. |
| `makeBare` | Strips Microsoft Word-specific markup artifacts. |
| `makeClean` | Replaces presentational markup (e.g. `<font>`) with CSS-based equivalents. |
| `mergeDivs` | Merges nested `<div>` elements where possible. |
| `mergeEmphasis` | Merges nested emphasis elements (`<b><b>` → `<b>`). |
| `mergeSpans` | Merges nested `<span>` elements where possible. |
| `metaCharset` | Adds/updates a `<meta charset>` declaration to match the output encoding. |
| `NCR` | Controls whether characters outside the target encoding are output as numeric character references. |
| `newline` | Controls the newline convention `libtidy` normalizes to. |
| `numEntities` | Emits numeric entities (`&#160;`) instead of named ones (`&nbsp;`). |
| `omitOptionalTags` | Omits tags `libtidy` considers optional per the HTML spec (e.g. some closing tags). |
| `outputBOM` | Controls whether a byte-order mark is written to the output. |
| `pPrintTabs` | Pretty-prints using tabs for indentation instead of the equivalent number of spaces. |
| `preserveEntities` | Leaves existing entities untouched rather than normalizing them. |
| `priorityAttributes` | Reorders certain attributes (e.g. `id`, `name`, `class`) to appear first on an element. |
| `punctWrap` | Allows line-wrapping after punctuation inside attribute values. |
| `quiet` | Suppresses non-essential summary output in the diagnostic text. |
| `quoteAmpersand` | Escapes literal `&` characters as `&amp;`. |
| `quoteMarks` | Escapes literal `"` characters as `&quot;`. |
| `quoteNbsp` | Escapes non-breaking spaces as the `&nbsp;` entity rather than the raw byte. |
| `replaceColor` | Replaces numeric color attribute values with their named equivalents where one exists. |
| `showErrors` | Includes errors in the diagnostic report (`info`). |
| `showInfo` | Includes informational (non-warning, non-error) notes in the diagnostic report. |
| `showMarkup` | Controls whether `tidySaveBuffer`-style markup output is produced at all. |
| `showMetaChange` | Reports when `libtidy` changes/adds a `<meta>` tag. |
| `showWarnings` | Includes warnings in the diagnostic report (`info`). |
| `skipNested` | Skips further parsing of markup nested inside elements `libtidy` doesn't expect to contain markup (e.g. `<script>`/`<style>`), reducing false-positive diagnostics. |
| `sortAttributes` | Sorts attributes on each element alphabetically. |
| `strictTagsAttr` | Requires tags/attributes to be valid for the declared doctype. |
| `styleTags` | Moves inline `style` attributes into a `<style>` block where possible. |
| `upperCaseAttrs` | Upper-cases attribute names in the output. |
| `upperCaseTags` | Upper-cases tag names in the output. |
| `useCustomTags` | Enables recognition of custom (non-standard) tag names instead of treating them as errors. |
| `vertSpace` | Adds blank lines between some block-level elements for readability. |
| `warnPropAttrs` | Warns about proprietary attributes instead of silently dropping/keeping them. |
| `word2000` | Enables extra cleanup specific to Microsoft Word 2000 HTML export artifacts. |
| `wrapAsp` | Allows line-wrapping inside ASP (`<% %>`) pseudo-elements. |
| `wrapAttVals` | Allows line-wrapping inside attribute values. |
| `wrapJste` | Allows line-wrapping inside JSTE (`<# #>`) pseudo-elements. |
| `wrapPhp` | Allows line-wrapping inside PHP (`<?php ?>`) pseudo-elements. *(Deprecated in `libtidy` itself, but still a valid boolean toggle here.)* |
| `wrapScriptlets` | Allows line-wrapping inside string literals in scripts. *(Deprecated in `libtidy`.)* |
| `wrapSection` | Allows line-wrapping inside `<![ ]>` marked sections. *(Deprecated in `libtidy`.)* |
| `writeBack` | Controls whether the corrected markup is written back at all (independent of `showMarkup`). |
| `xhtmlOut` | Produces XHTML1-compatible output. |
| `xmlDecl` | Adds an `<?xml ... ?>` declaration to the output. |
| `xmlOut` | Produces well-formed generic XML output rather than HTML/XHTML. |
| `xmlPIs` | Treats `<? ?>` sequences as XML processing instructions rather than potential errors. |
| `xmlSpace` | Adds `xml:space="preserve"` where appropriate. |
| `xmlTags` | Treats the input as XML for tag-name case sensitivity purposes. |

**Integer options** — pass a `Number`:

| Property | Description |
|---|---|
| `accessibilityCheckLevel` | Enables `libtidy`'s accessibility (WCAG-style) checks at increasing levels of strictness. Consult the `libtidy` documentation bundled with your plugin build for the exact level values it supports — don't assume a specific numeric scale without checking, since this has varied across `libtidy` releases. |
| `indentSpaces` | Number of spaces used per indentation level when pretty-printing. |
| `indentAttributes` | Controls indentation of attributes across multiple lines. **Side effect:** setting this also resets `indentSpaces` to `libtidy`'s default if `indentSpaces` is currently `0` — so indentation still takes effect even if you didn't set `indentSpaces` explicitly yourself. |
| `indentCdata` | Controls indentation of CDATA sections. Has the same `indentSpaces`-reset side effect as `indentAttributes` above. |
| `indentContent` | Controls indentation of general block content. Has the same `indentSpaces`-reset side effect as `indentAttributes` above. |
| `tabSize` | Number of spaces a literal tab character is expanded to when read. |
| `wrapLen` | Column at which `libtidy` wraps long lines. Set to `0` to effectively disable wrapping. |

**String options** — pass `Text`:

| Property | Description |
|---|---|
| `altText` | Default `alt` text `libtidy` inserts for `<img>` elements missing one. |
| `CSSPrefix` | Prefix used for CSS class names `libtidy` generates itself (e.g. when converting presentational markup). |
| `doctype` | Overrides the doctype declaration `libtidy` writes. `libtidy` accepts either a literal doctype string or one of its own keyword shortcuts (e.g. `"strict"`, `"loose"`, `"omit"`) — see the `libtidy`/`tidy` documentation for the exact keyword set your build supports. |

**Tag-list options** — pass a `Collection` of `Text`:

| Property | Description |
|---|---|
| `blockTags` | Additional tag names `libtidy` should treat as block-level elements. |
| `emptyTags` | Additional tag names `libtidy` should treat as empty (self-closing, no content) elements. |
| `inlineTags` | Additional tag names `libtidy` should treat as inline elements. |
| `preTags` | Additional tag names `libtidy` should treat like `<pre>` (preserve whitespace). |

For each of these four, the plugin walks your `Collection` element by element and only honors elements that are actually `Text` — any non-text element in the collection is silently skipped (no error). Each text element is applied to `libtidy` as a separate call, which `libtidy` accumulates onto the option's existing tag list rather than each call replacing the previous one — but confirm this accumulation behavior against the exact `libtidy` version bundled with your plugin build if your list's final contents matter precisely.

**Global (process-wide) options:**

| Property | Type | Description |
|---|---|---|
| `encoding` | `Text` | Input character encoding name (e.g. `"utf8"`, `"win1252"`) passed to `libtidy`'s `tidySetInCharEncoding`. Output encoding is always forced to UTF-8 by the plugin regardless of this setting. An unrecognized name is not flagged — see [Requirements & platform notes](#requirements--platform-notes). |
| `language` | `Text` | Message/locale language `libtidy` uses when generating the `info` diagnostic text. **This is a `libtidy`-wide setting, not scoped to your call** — see the callout in [Requirements & platform notes](#requirements--platform-notes). |

#### Result object

| Property | Type | Description |
|---|---|---|
| `status` | `Number` | The raw return code from `libtidy`'s parse step. By `libtidy`'s own convention this is typically `0` (no warnings or errors), a positive value (warnings and/or errors were found), or a negative value (a severe internal/configuration error occurred) — check the `libtidy` documentation for your build for the exact meaning of nonzero values, since this is a pass-through of the library's own code, not something the plugin defines. |
| `detectedGenericXml` | `Boolean` | Whether `libtidy` detected the input as generic (non-HTML) XML. |
| `detectedHtmlVersion` | `Number` | An integer version code `libtidy` assigns to the HTML version it detected. The exact mapping of numbers to versions is defined by `libtidy` itself and isn't restated here with certainty — treat this as an opaque version identifier unless you've confirmed the mapping against your `libtidy` build. |
| `detectedXhtml` | `Boolean` | Whether `libtidy` detected the input as XHTML. |
| `errorCount` | `Number` | Count of genuine parse **errors** (not warnings) found while processing. |
| `html` | `Text` | The corrected markup, UTF-8 encoded. **Only present when `errorCount` is `0`.** Absent entirely otherwise. |
| `info` | `Text` | Diagnostic text: general parse info plus an error/warning summary, shaped by whichever `show*`/`quiet`/`emacs` options you set. Present whenever the plugin's internal error buffer was successfully attached to the `libtidy` document — in practice this means it's present on essentially every call, but it is not unconditionally guaranteed by the code. |

### Example

No `.4dm` sample/test file was supplied for this plugin revision, so there's no verified test method to quote verbatim. The snippet below is illustrative only, built from well-established 4D fundamentals (`New object`, `Case of`/`If`, `ALERT`) plus a generic call to convert `Text` to a `BLOB` — check your Language Reference for the exact blob-conversion command/parameters available on your 4D version, since that part isn't verified from this plugin's source.

```4d
var $html : Text
var $blob : Blob
var $options; $result : Object

$html:="<html><body><p>Hello, world!</body>"

// Convert to a BLOB using whichever command matches your 4D version
// (e.g. TEXT TO BLOB) -- check the Language Reference for the exact
// signature, including any optional text-encoding parameter.
TEXT TO BLOB($html; $blob)

$options:=New object
$options.indentSpaces:=2
$options.wrapLen:=0
$options.xhtmlOut:=True
$options.showWarnings:=True

$result:=Tidy($blob; $options)

If ($result.errorCount=0)
	ALERT($result.html)
Else
	ALERT("Tidy reported "+String($result.errorCount)+" error(s):\n"+$result.info)
End if 
```

A second example showing the tag-list and doctype options together:

```4d
var $blob : Blob
var $options; $result : Object
var $customTags : Collection

$customTags:=New collection("my-widget"; "my-panel")

$options:=New object
$options.doctype:="strict"
$options.blockTags:=$customTags
$options.useCustomTags:=True
$options.encoding:="utf8"

$result:=Tidy($blob; $options)

// The plugin has no companion helper commands for inspecting the result --
// read whatever properties you need directly with dot notation.
If ($result.errorCount=0)
	ALERT($result.html)
End if 
```

---

## Error handling & troubleshooting

- **Empty result object means an invalid/null `BLOB`.** If none of `status`, `errorCount`, `html`, or `info` exist on the return value, the `htmlBlob` parameter wasn't usable. This is not reported as a 4D error — check for the property's existence, not just a falsy value.
- **`html` missing does not necessarily mean total failure.** Check `errorCount` first: a nonzero value means `libtidy` found genuine errors (not just warnings) and the plugin deliberately skipped producing `html` — the diagnostic detail is in `info`.
- **Setting `language` affects more than your own call.** Because this maps to a process-wide `libtidy` setting, don't assume a `language` value you set is scoped to just the current `Tidy` invocation, especially under concurrent/server usage.
- **Unrecognized `encoding`/`language` strings fail without telling you.** The plugin does not check `libtidy`'s return codes for these two settings. If output looks wrong (garbled characters, unexpected message language), double-check the exact string value against `libtidy`'s accepted names rather than assuming the plugin would have surfaced an error.
- **Non-text elements in a tag-list `Collection` are dropped silently.** If a tag you added to `blockTags`/`emptyTags`/`inlineTags`/`preTags` doesn't seem to take effect, confirm every element in the collection is actually `Text` — numbers, objects, or nulls mixed into the collection are skipped without any indication.
- **`indentAttributes`/`indentCdata`/`indentContent` can silently turn on indentation you didn't ask for.** Each of these resets `indentSpaces` to `libtidy`'s default if it's currently `0`, specifically so the indentation setting you did ask for actually has an effect — expect indented output even if you never touched `indentSpaces` yourself.
- **`status`/`detectedHtmlVersion` are raw `libtidy` codes, not plugin-defined enums.** Don't hardcode assumptions about what a specific nonzero/version number means without checking the `libtidy` documentation for your build; the plugin passes these through unmodified.

---

## Quick reference

```4d
var $blob : Blob
var $options; $result : Object

// Minimal call, defaults for everything
$result:=Tidy($blob; Null)

// Common cleanup profile
$options:=New object
$options.xhtmlOut:=True
$options.indentSpaces:=2
$options.wrapLen:=0
$options.dropEmptyElems:=True
$options.mergeDivs:=True
$options.encoding:="utf8"

$result:=Tidy($blob; $options)

If (Not(OB Is defined($result; "errorCount")))
	 // htmlBlob was invalid -- $result has no properties at all in this build
Else 
	If ($result.errorCount=0)
		$html:=$result.html
	Else 
		$errors:=$result.info
	End if 
End if 
```
