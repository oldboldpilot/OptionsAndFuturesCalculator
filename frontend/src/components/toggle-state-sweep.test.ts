/**
 * Every toggle in the workspace must say which member is selected.
 *
 * @author Olumuyiwa Oluwasanmi
 *
 * The chain's buy/sell buttons and the ticket's Buy/Sell, Call/Put and
 * Averaging segments were found announcing nothing about their own state, and
 * were fixed with a test written from the two components that had been
 * measured in a browser. That test proves those two are FIXED. It cannot prove
 * the defect is GONE, because it was written from the same list as the fix --
 * the `ad-routes.test.ts` failure this repository has now paid for five times.
 *
 * Sweeping the class immediately found seven more groups carrying the
 * identical defect: the chain's own Both/Calls/Puts filter, ExerciseStylePanel
 * twice, PayoffLadder, PnLMatrix, PnLSurface, StrategySelector's category row,
 * and ThemeToggle (which had a named group whose members were silent).
 *
 * So this file does not name a component. It reads every `.tsx` under
 * `src/components/`, finds every element that paints itself as selected with
 * `data-active`, and requires that element to expose the same fact to the
 * accessibility tree. A toggle added tomorrow in a file nobody here has heard
 * of is covered the day it lands, or this fails naming it.
 *
 * WHY `data-active` IS THE RIGHT DISCRIMINATOR. It is this codebase's own
 * marker for "this member is the chosen one" -- `segment-item[data-active]`
 * and the category pills are styled from it. An element carrying it has, by
 * construction, a selected state worth announcing; an element without it has
 * no state to announce. The rule is therefore derived from how the UI is
 * actually built rather than from a judgement about which controls matter.
 */
import fs from "node:fs";
import path from "node:path";
import { describe, expect, it } from "vitest";

const componentsDir = path.resolve(process.cwd(), "src/components");

interface JsxTag {
  name: string;
  attrs: string;
}

interface ActiveElement {
  file: string;
  name: string;
  attrs: string;
  /** Open ancestors at the point this element was opened, outermost first. */
  ancestors: JsxTag[];
  line: number;
}

/**
 * A brace-aware JSX tag walker.
 *
 * A plain `/<[^>]*>/` cannot be used here: `onClick={() => setSide('both')}`
 * contains a `>` inside an arrow function, so a naive matcher ends the tag in
 * the middle of its own attribute list and everything after that point becomes
 * invisible -- including the `aria-pressed` this file exists to check.
 *
 * MEASURED, because the honest version of this note is narrower than the
 * obvious one: replacing the brace tracking below with a naive `>` search
 * changes NOTHING about today's result. Every toggle in the tree happens to
 * write `aria-pressed` before `onClick`, so the truncated attribute list still
 * contains the attribute. The defect is therefore latent and ORDER-DEPENDENT
 * -- it arms itself the first time someone writes the two attributes the other
 * way round, and then reports correct code as broken. `scanJsx` is pinned
 * against exactly that ordering below rather than trusted to a comment.
 *
 * So the scan tracks brace depth and string literals, and only a `>` at depth
 * zero closes a tag.
 */
function scanJsx(src: string, file: string): ActiveElement[] {
  const found: ActiveElement[] = [];
  const stack: JsxTag[] = [];
  let i = 0;

  while (i < src.length) {
    const lt = src.indexOf("<", i);
    if (lt === -1) break;

    const next = src[lt + 1];
    // `<` that does not begin a tag: a comparison, a generic, an arrow.
    if (!next || !/[A-Za-z/]/.test(next)) {
      i = lt + 1;
      continue;
    }

    const closing = next === "/";
    let j = lt + 1 + (closing ? 1 : 0);
    const nameStart = j;
    while (j < src.length && /[A-Za-z0-9_.]/.test(src[j]!)) j++;
    const name = src.slice(nameStart, j);
    if (!name) {
      i = lt + 1;
      continue;
    }

    // Consume attributes to the tag's own `>`, ignoring braces and strings.
    let depth = 0;
    let quote: string | null = null;
    let k = j;
    for (; k < src.length; k++) {
      const c = src[k]!;
      if (quote) {
        if (c === quote) quote = null;
        continue;
      }
      if (c === '"' || c === "'" || c === "`") {
        quote = c;
        continue;
      }
      if (c === "{") depth++;
      else if (c === "}") depth--;
      else if (c === ">" && depth === 0) break;
    }
    if (k >= src.length) break;

    const attrs = src.slice(j, k);
    const selfClosing = attrs.trimEnd().endsWith("/");

    if (closing) {
      // Pop to the matching open tag. Tolerant of an unmatched close rather
      // than throwing: the count control below is what catches a bad parse.
      for (let s = stack.length - 1; s >= 0; s--) {
        if (stack[s]!.name === name) {
          stack.length = s;
          break;
        }
      }
    } else {
      if (/\bdata-active\s*=/.test(attrs)) {
        found.push({
          file,
          name,
          attrs,
          ancestors: [...stack],
          line: src.slice(0, lt).split("\n").length,
        });
      }
      if (!selfClosing) stack.push({ name, attrs });
    }

    i = k + 1;
  }

  return found;
}

const sourceFiles = fs
  .readdirSync(componentsDir, { recursive: true, withFileTypes: true })
  .filter((e) => e.isFile() && e.name.endsWith(".tsx") && !e.name.includes(".test."))
  .map((e) => path.relative(componentsDir, path.join(e.parentPath ?? componentsDir, e.name)));

const sources = sourceFiles.map((f) => ({
  file: f,
  src: fs.readFileSync(path.join(componentsDir, f), "utf8"),
}));

const activeElements = sources.flatMap(({ file, src }) => scanJsx(src, file));

describe("the JSX walker itself", () => {
  /**
   * `scanJsx` is the instrument, and an instrument nothing checks is the
   * failure this repository records against `last_applied`, the LIVE badge and
   * Railway's SUCCESS. These two fixtures are written in the order today's
   * components do NOT use, which is the order that breaks a naive matcher.
   */
  it("sees an attribute that sits after an arrow function", () => {
    const jsx = `<div role="group" aria-label="Units">
      <button data-active={m === 'a'} onClick={() => set(m > 1 ? 'a' : 'b')} aria-pressed={m === 'a'}>A</button>
    </div>`;
    const found = scanJsx(jsx, "fixture.tsx");
    expect(found).toHaveLength(1);
    expect(
      /\baria-pressed\s*=/.test(found[0]!.attrs),
      "the walker stopped at the `>` of an arrow function and lost the attribute after it",
    ).toBe(true);
  });

  it("does not mistake a comparison in an expression for a tag", () => {
    const jsx = `<div role="group" aria-label="Units">
      {count > 0 && <button data-active={on} aria-pressed={on}>On</button>}
    </div>`;
    const found = scanJsx(jsx, "fixture.tsx");
    expect(found).toHaveLength(1);
    expect(found[0]!.ancestors.some((a) => /aria-label="Units"/.test(a.attrs))).toBe(true);
  });
});

describe("the sweep reaches what it claims to reach", () => {
  it("reads a healthy number of component files", () => {
    // An empty read passes every assertion below without looking at one line
    // of JSX. This repository has shipped a vacuous sweep before.
    expect(sourceFiles.length).toBeGreaterThan(10);
  });

  it("parses every data-active element in the tree, losing none to the walker", () => {
    /**
     * The control on the parser itself, and it is the load-bearing one. A
     * silently broken walker reports zero offenders, which reads exactly like
     * a clean codebase. So the structural scan is cross-checked against a
     * dumb textual count of the same marker: if they disagree, the walker
     * dropped an element and every other assertion here is worthless.
     */
    const textualCount = sources.reduce(
      (n, { src }) => n + (src.match(/\bdata-active\s*=/g) ?? []).length,
      0,
    );
    expect(textualCount).toBeGreaterThan(15);
    expect(
      activeElements.length,
      "the JSX walker lost elements the raw text can see; its brace or string handling is wrong",
    ).toBe(textualCount);
  });
});

describe("every selectable member announces that it is selected", () => {
  it("has no element that paints selection without exposing it", () => {
    /**
     * `data-active` drives the CSS that makes the chosen member look chosen.
     * Without a matching `aria-pressed` (or `aria-selected`/`aria-checked` for
     * a tab or radio idiom) a screen reader is handed a row of identical
     * buttons and no way to tell which one is in force -- the Buy/Sell, the
     * Call/Put, the $ versus % risk, the exercise style the price depends on.
     */
    const offenders = activeElements
      .filter(({ attrs }) => !/\baria-(pressed|selected|checked)\s*=/.test(attrs))
      .map(({ file, line, name }) => `${file}:${line} <${name}>`);

    expect(
      offenders,
      `these paint a selected state that no screen reader can read: ${offenders.join(", ")}`,
    ).toEqual([]);
  });

  it("puts every toggle inside a group that says what is being chosen", () => {
    /**
     * "Pressed" on its own is half an answer. Three segments sit side by side
     * in the ticket -- Buy/Sell, Call/Put, Averaging -- and a group with no
     * accessible name leaves "Call, pressed" floating with nothing saying
     * what it selects. A named `role="group"` is what turns it into a
     * sentence.
     *
     * The ancestor chain comes from the walker rather than from proximity in
     * the file, so a group wrapping a `.map()` counts for every member it
     * renders, which is how three of these are actually written.
     */
    const offenders = activeElements
      .filter(
        ({ ancestors }) =>
          !ancestors.some(
            (a) =>
              /\brole="(group|radiogroup|tablist)"/.test(a.attrs) &&
              /\baria-label="[^"]+"/.test(a.attrs),
          ),
      )
      .map(({ file, line, name }) => `${file}:${line} <${name}>`);

    expect(
      offenders,
      `these are inside no named group, so "pressed" is announced with nothing saying what it selects: ${offenders.join(", ")}`,
    ).toEqual([]);
  });
});
