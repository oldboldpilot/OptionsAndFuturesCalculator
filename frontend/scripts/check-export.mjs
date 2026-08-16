#!/usr/bin/env node
/**
 * Post-build assertions on the static export.
 *
 * @author Olumuyiwa Oluwasanmi
 *
 * Unit tests import modules and see the real values. This checks the BYTES that
 * are actually served, which is the only place two of these failures are
 * visible at all:
 *
 *   The inline Auto Ads guard is built by interpolating a value into a string.
 *   When that value came from a `'use client'` module the server received a
 *   client-reference proxy, `JSON.stringify` returned the literal `undefined`,
 *   and the shipped guard read `if(undefined.some(...))` — a TypeError at
 *   head-parse time that silently left ads running on every route it was meant
 *   to protect. Nothing in the source looked wrong and no unit test could see it.
 *
 *   Page content is what a policy reviewer reads. Twenty-six pages that render
 *   identically is the condition this site was flagged for on 2026-08-16, and
 *   it is a property of the rendered output rather than of any one module.
 *
 * Run after `npm run build`. Exits non-zero with a named failure.
 */
import { readFileSync, existsSync, readdirSync } from 'node:fs';
import { join } from 'node:path';

const OUT = 'out';
const failures = [];

function fail(check, detail) {
  failures.push(`${check}: ${detail}`);
}

function html(file) {
  return readFileSync(join(OUT, file), 'utf8');
}

/** Visible text, with script and style content removed. */
function visibleText(file) {
  return html(file)
    .replace(/<script[\s\S]*?<\/script>/gi, ' ')
    .replace(/<style[\s\S]*?<\/style>/gi, ' ')
    .replace(/<[^>]+>/g, ' ')
    .replace(/\s+/g, ' ')
    .trim();
}

if (!existsSync(OUT)) {
  console.error(`check-export: ${OUT}/ does not exist — run \`npm run build\` first.`);
  process.exit(1);
}

const strategyFiles = readdirSync(join(OUT, 'calculator')).filter((f) => f.endsWith('.html'));
const contentPages = ['index.html', ...strategyFiles.map((f) => `calculator/${f}`)];
const allPages = [...contentPages, 'privacy.html', 'terms.html', 'widget.html'];

/* ---- 1. The Auto Ads guard is well formed on every page ------------------ */

for (const page of allPages) {
  const source = html(page);
  const match = source.match(/<script>([^<]*pauseAdRequests[^<]*)<\/script>/);
  if (!match) {
    fail('auto-ads-guard', `${page} carries no pauseAdRequests guard`);
    continue;
  }
  const guard = match[1];
  // The exact shape the proxy bug produced. Named explicitly so a failure here
  // points straight at the cause rather than at the symptom.
  if (guard.includes('undefined')) {
    fail('auto-ads-guard', `${page} guard contains "undefined" — a value failed to serialise: ${guard}`);
  }
  for (const route of ['/widget', '/privacy', '/terms']) {
    if (!guard.includes(`"${route}"`)) {
      fail('auto-ads-guard', `${page} guard does not list ${route}`);
    }
  }
}

/* ---- 2. Every ad-serving page carries real publisher content ------------- */

// 700 words was the ENTIRE content of a flagged page, and essentially all of it
// was UI chrome -- ticker symbols, button labels, panel headings. The floor is
// set above that so passing it requires prose, not more interface.
const MIN_WORDS = 1000;

for (const page of contentPages) {
  const count = visibleText(page).split(' ').length;
  if (count < MIN_WORDS) {
    fail('publisher-content', `${page} has only ${count} words (floor ${MIN_WORDS})`);
  }
}

/* ---- 3. No two strategy pages render the same content -------------------- */

const byText = new Map();
for (const file of strategyFiles) {
  const text = visibleText(`calculator/${file}`);
  if (byText.has(text)) {
    fail('duplicate-content', `calculator/${file} is identical to calculator/${byText.get(text)}`);
  }
  byText.set(text, file);
}

/* ---- 4. The home page links to every strategy page ----------------------- */

// Until the index existed these pages were reachable only from sitemap.xml. A
// crawler following links from the home page found none of them, and a reader
// found none either -- the in-app strategy picker changes client state, it does
// not navigate.
const home = html('index.html');
for (const file of strategyFiles) {
  const slug = file.replace(/\.html$/, '');
  if (!home.includes(`/calculator/${slug}`)) {
    fail('internal-links', `index.html does not link to /calculator/${slug}`);
  }
}

/* ---- 5. Structured data parses ------------------------------------------ */

for (const page of contentPages) {
  const blocks = [...html(page).matchAll(
    /<script type="application\/ld\+json">([\s\S]*?)<\/script>/g,
  )];
  if (blocks.length === 0) {
    fail('structured-data', `${page} emits no JSON-LD`);
  }
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
  `check-export: OK — ${allPages.length} pages, ${strategyFiles.length} strategy pages all distinct, ` +
    `guard lists /widget /privacy /terms, content floor ${MIN_WORDS} words met.`,
);
