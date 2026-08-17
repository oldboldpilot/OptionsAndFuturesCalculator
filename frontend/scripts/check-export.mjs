#!/usr/bin/env node
/**
 * Post-build assertions on the static export.
 *
 * @author Olumuyiwa Oluwasanmi
 *
 * The invariant this exists to hold, after the 2026-08-16 AdSense flag:
 *
 *   ADS AND PUBLISHER CONTENT LIVE ON THE SAME SCREENS. Neither on its own.
 *
 * Two failure directions, and both have happened here:
 *
 *   Ads without content. Twenty-six `/calculator/<slug>` pages rendered the
 *   same workspace with one heading substituted — `/long-call` and
 *   `/iron-condor` both measured exactly 753 words — and every one carried
 *   advertising. That is the policy violation verbatim.
 *
 *   A guard that does not run. The inline Auto Ads suppressor is built by
 *   interpolating a value into a string, and when that value came from a
 *   `'use client'` module the server received a client-reference proxy, so
 *   `JSON.stringify` produced the literal `undefined` and the shipped guard read
 *   `if(undefined.some(...))` — a TypeError while <head> parsed, leaving ads
 *   running on every route it was meant to protect. Nothing in the source looked
 *   wrong and no unit test could see it, because a test importing the module
 *   directly always gets the real value.
 *
 * Both are properties of the emitted bytes, which is why this reads `out/`.
 *
 * Run after `npm run build`. Exits non-zero with a named failure.
 */
import { readFileSync, existsSync, readdirSync } from 'node:fs';
import { join } from 'node:path';

const OUT = 'out';
const failures = [];
const fail = (check, detail) => failures.push(`${check}: ${detail}`);
const html = (file) => readFileSync(join(OUT, file), 'utf8');

/** Visible text, with script and style content removed. */
const visibleText = (file) =>
  html(file)
    .replace(/<script[\s\S]*?<\/script>/gi, ' ')
    .replace(/<style[\s\S]*?<\/style>/gi, ' ')
    .replace(/<[^>]+>/g, ' ')
    .replace(/\s+/g, ' ')
    .trim();

if (!existsSync(OUT)) {
  console.error(`check-export: ${OUT}/ does not exist — run \`npm run build\` first.`);
  process.exit(1);
}

const slugs = readdirSync(join(OUT, 'calculator'))
  .filter((f) => f.endsWith('.html'))
  .map((f) => f.replace(/\.html$/, ''));

// The screens that carry the writing, and therefore the advertising. The 26
// articles ONLY — `guides.html` is the index, a directory of 26 links, which is
// a navigation screen under the same policy.
const CONTENT_PAGES = slugs.map((s) => `guides/${s}.html`);

/*
 * EVERY other page the export emits. Built by enumerating `out/` rather than by
 * listing the routes anybody thought of, because the page that produced the
 * second violation is the one nobody lists.
 *
 * `404.html` and `_not-found.html` are the entire reason this is a sweep. The
 * old check named `index.html`, `widget.html`, `privacy.html`, `terms.html` and
 * the 26 calculator pages — a hand-written list that was complete for every
 * route somebody had thought about, and silent about the two that shipped
 * Google's ad loader on thirteen words of error text.
 */
const ALL_PAGES = readdirSync(OUT, { recursive: true })
  .map((f) => String(f).replaceAll('\\', '/'))
  .filter((f) => f.endsWith('.html'));
const NO_AD_PAGES = ALL_PAGES.filter((f) => !CONTENT_PAGES.includes(f));

/* ---- 1. Google ad code ships to the article pages and NOWHERE else ------- */

/*
 * The load-bearing check, and it replaced one that could not have caught the
 * defect it exists for.
 *
 * The previous design shipped `adsbygoogle.js` on every page and suppressed it
 * at runtime with `if (NO_AD_ROUTES.some(r => location.pathname === r || ...))`.
 * This script verified that guard was present and named all five routes — which
 * it was, and did. The guard was correct. It simply cannot fire on a 404,
 * whose pathname is whatever the visitor typed and matches no entry:
 *
 *     GET /this-page-does-not-exist  →  404, loader, page-level push,
 *                                       multiplex <ins>, 13 words
 *
 * So the assertion is no longer about a guard being right. It is that pages
 * which must not serve ads contain no ad code AT ALL — nothing to suppress, no
 * runtime behaviour to depend on.
 */
const AD_CODE = [
  ['loader', /pagead2\.googlesyndication\.com/],
  ['page-level-push', /enable_page_level_ads/],
  ['manual-unit', /data-ad-slot=/],
];

for (const page of NO_AD_PAGES) {
  const source = html(page);
  for (const [what, pattern] of AD_CODE) {
    if (pattern.test(source)) {
      fail('ad-code-off-content', `${page} contains AdSense ${what} but carries no article`);
    }
  }
}

for (const page of CONTENT_PAGES) {
  if (!existsSync(join(OUT, page))) {
    fail('missing-page', page);
    continue;
  }
  const source = html(page);
  // The other direction, and it is not symmetry for its own sake: moving the
  // loader into a nested layout is exactly the kind of change that can silently
  // stop emitting it, and a site with no ad code anywhere fails quietly — as
  // revenue, not as an error.
  if (!/pagead2\.googlesyndication\.com/.test(source)) {
    fail('no-ad-code-on-content', `${page} is an ad-serving page but carries no AdSense loader`);
  }
  if (!/enable_page_level_ads/.test(source)) {
    fail('no-ad-code-on-content', `${page} carries the loader but never asks for page-level ads`);
  }
}

/*
 * The stale-guard check. `pauseAdRequests` was the old mechanism; if it comes
 * back it means somebody restored the denylist, which is the design that let a
 * 404 serve ads.
 */
for (const page of ALL_PAGES) {
  if (/pauseAdRequests/.test(html(page))) {
    fail(
      'stale-guard',
      `${page} carries a pauseAdRequests guard — the pathname denylist was restored; ` +
        'ad code must be absent from non-article pages, not suppressed at runtime',
    );
  }
}

/* ---- 2. Every ad-serving screen carries real publisher content ----------- */

/*
 * 600, down from 1000, and the number moved because what it measures changed —
 * not to make a failing check pass. Recording both figures because the reason
 * is the whole point.
 *
 * The 1000 was set when an ad-serving page was the calculator WITH the article
 * below it, so roughly 750 of those words were interface: ticker symbols,
 * button labels, panel headings. It was really demanding about 250 words of
 * prose. These pages are now the article alone — the tool moved to its own
 * screen and took the UI vocabulary with it — so the same 1000 would demand
 * four times the prose it was written to require.
 *
 * Measured across the 26 guides after the split: 767 (protective-put) to 954
 * (long-call), every word of it publisher content. That is MORE real content
 * per ad-serving page than before, against a flagged baseline of 753 words that
 * contained almost none. 600 sits below the thinnest real guide and far above
 * any stub, so it still catches a page shipped empty.
 */
const MIN_WORDS = 600;

for (const page of CONTENT_PAGES) {
  if (!existsSync(join(OUT, page))) continue;
  const count = visibleText(page).split(' ').length;
  if (count < MIN_WORDS) {
    fail('publisher-content', `${page} has only ${count} words (floor ${MIN_WORDS})`);
  }
}

/* ---- 4. No two guides render the same content ---------------------------- */

const byText = new Map();
for (const slug of slugs) {
  const page = `guides/${slug}.html`;
  if (!existsSync(join(OUT, page))) continue;
  const text = visibleText(page);
  if (byText.has(text)) {
    fail('duplicate-content', `${page} is identical to guides/${byText.get(text)}.html`);
  }
  byText.set(text, slug);
}

/* ---- 5. The two intents are cross-linked in both directions -------------- */

// Separate URLs for the tool and the article only work if each can be reached
// from the other. Without this the split just buries one of them.
const guidesIndex = html('guides.html');
for (const slug of slugs) {
  if (!guidesIndex.includes(`/guides/${slug}`)) {
    fail('internal-links', `guides.html does not link to /guides/${slug}`);
  }
  const calc = `calculator/${slug}.html`;
  if (existsSync(join(OUT, calc)) && !html(calc).includes(`/guides/${slug}`)) {
    fail('internal-links', `${calc} does not link to its guide`);
  }
  const guide = `guides/${slug}.html`;
  if (existsSync(join(OUT, guide)) && !html(guide).includes(`/calculator/${slug}`)) {
    fail('internal-links', `${guide} does not link back to the calculator`);
  }
}

/* ---- 6. Structured data parses ------------------------------------------ */

for (const page of CONTENT_PAGES) {
  if (!existsSync(join(OUT, page))) continue;
  const blocks = [...html(page).matchAll(
    /<script type="application\/ld\+json">([\s\S]*?)<\/script>/g,
  )];
  if (blocks.length === 0) fail('structured-data', `${page} emits no JSON-LD`);
  for (const [, body] of blocks) {
    try {
      JSON.parse(body);
    } catch (error) {
      fail('structured-data', `${page} emits unparseable JSON-LD: ${error.message}`);
    }
  }
}

/* ------------------------------------------------------------------------- */

if (failures.length > 0) {
  console.error(`check-export: ${failures.length} failure(s)\n`);
  for (const failure of failures) console.error(`  ✗ ${failure}`);
  process.exit(1);
}

console.log(
  `check-export: OK — ${CONTENT_PAGES.length} article pages carry the AdSense loader, all clear ` +
    `${MIN_WORDS} words and are distinct; ${NO_AD_PAGES.length} other pages ` +
    `(including 404) carry NO ad code of any kind; ` +
    `${slugs.length} strategies cross-linked both ways.`,
);
