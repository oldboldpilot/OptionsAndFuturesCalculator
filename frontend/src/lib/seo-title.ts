/**
 * @author Olumuyiwa Oluwasanmi
 *
 * Page title and metadata length constraints for SEO.
 *
 * Measured on 2026-09-19: `src/app/layout.tsx` set a Next.js title template
 * that unconditionally appended ` | Options & Futures Calculator` (31 characters)
 * to every page title. 53 of 59 exported HTML pages exceeded 70 characters,
 * the longest being 101 (`out/guides/futures-intercommodity-spread.html`).
 * Because Google truncates SERP titles past roughly 70 characters, the
 * distinguishing strategy-specific portion was cut off across most of the site.
 * In addition, 5 meta descriptions exceeded 160 characters, the longest being 193.
 *
 * A Next.js `title.template` cannot express conditional behavior. `pageTitle()`
 * solves this by trying the branded candidate first and falling back to the
 * bare authored title when the combined string exceeds TITLE_MAX (70). The brand
 * suffix gives way; the specific strategy never does.
 *
 * When an authored title alone exceeds TITLE_MAX, `pageTitle()` returns it
 * unchanged rather than silently truncating human prose mid-word; unit tests
 * fail in that case so the authored copy can be deliberately edited.
 */

export const TITLE_MAX = 70;
export const DESCRIPTION_MAX = 160;
export const BRAND_SUFFIX = ' | Options & Futures Calculator';

/**
 * Sizes a page title against the SERP display budget.
 *
 * Appends the brand suffix (` | Options & Futures Calculator`) if the combination
 * fits within `TITLE_MAX` (70 characters). If not, returns the authored title
 * bare, giving priority to the distinguishing specific content.
 *
 * If the authored title alone exceeds `TITLE_MAX`, returns it unchanged so tests
 * catch the defect instead of silently truncating human copy mid-word.
 */
export function pageTitle(authored: string): string {
  const candidate = `${authored}${BRAND_SUFFIX}`;
  if (candidate.length <= TITLE_MAX) {
    return candidate;
  }
  return authored;
}
