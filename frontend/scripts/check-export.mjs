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

/**
 * Routes the layout's inline guard must name. Kept as a literal rather than
 * imported from src/config/ad-routes.ts: this script's whole job is to check
 * the build against an independently stated expectation, and importing the same
 * constant the build used would make the assertion circular.
 */
const EXPECTED_NO_AD_ROUTES = ['/', '/calculator', '/widget', '/privacy', '/terms'];

// Screens with the calculator on them and no article. No Google ads.
const TOOL_PAGES = ['index.html', 'widget.html', ...slugs.map((s) => `calculator/${s}.html`)];
// Policy documents. Not publisher content, so also no ads.
const POLICY_PAGES = ['privacy.html', 'terms.html'];
// The screens that carry the writing, and therefore the advertising.
const CONTENT_PAGES = ['guides.html', ...slugs.map((s) => `guides/${s}.html`)];

/* ---- 1. The Auto Ads guard is present and well formed everywhere --------- */

for (const page of [...TOOL_PAGES, ...POLICY_PAGES, ...CONTENT_PAGES]) {
  if (!existsSync(join(OUT, page))) {
    fail('missing-page', page);
    continue;
  }
  const match = html(page).match(/<script>([^<]*pauseAdRequests[^<]*)<\/script>/);
  if (!match) {
    fail('auto-ads-guard', `${page} carries no pauseAdRequests guard`);
    continue;
  }
  const guard = match[1];
  if (guard.includes('undefined')) {
    fail('auto-ads-guard', `${page} guard contains "undefined" — a value failed to serialise: ${guard}`);
  }
  for (const route of EXPECTED_NO_AD_ROUTES) {
    if (!guard.includes(`"${route}"`)) {
      fail('auto-ads-guard', `${page} guard does not list ${route}`);
    }
  }
}

/* ---- 2. Tool and policy screens carry no manual ad unit ------------------ */

for (const page of [...TOOL_PAGES, ...POLICY_PAGES]) {
  if (!existsSync(join(OUT, page))) continue;
  // AdSlot renders its <ins> only after mount, so the static HTML should carry
  // none anywhere. A slot id in the markup of a tool page would mean a unit was
  // added outside AdSlot and outside the route guard entirely.
  if (/data-ad-slot=/.test(html(page))) {
    fail('ads-on-tool-screen', `${page} carries a manual ad unit but has no publisher content`);
  }
}

/* ---- 3. Every ad-serving screen carries real publisher content ----------- */

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
  `check-export: OK — ${CONTENT_PAGES.length} ad-serving pages all clear ${MIN_WORDS} words and are distinct; ` +
    `${TOOL_PAGES.length + POLICY_PAGES.length} tool/policy pages carry no ad unit; ` +
    `guard lists ${EXPECTED_NO_AD_ROUTES.join(' ')}; ${slugs.length} strategies cross-linked both ways.`,
);
