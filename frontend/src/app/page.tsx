import StrategyWorkspace from '../components/StrategyWorkspace';

/**
 * The home page: the calculator, and only the calculator.
 *
 * @author Olumuyiwa Oluwasanmi
 *
 * The written material that briefly lived below this workspace is now the
 * Guides tab, at `/guides`. Advertising went with it — this route is in
 * outside the ad-serving `/guides/<slug>` subtree, so it ships no ad code.
 *
 * That is the same shape mortgagefvcalculator.com uses and it is deliberate
 * rather than a concession: measured on 2026-08-16 its calculator page is 730
 * words of pure interface and carries no `adsbygoogle` at all. A tool screen
 * with no publisher content is fine; a tool screen with no publisher content
 * AND advertising is what got this site flagged.
 */
export default function Home() {
  return (
    <StrategyWorkspace
      // An expression, not a quoted string. JSX does not decode entities in
      // attribute values, so "&amp;" here would render as those five
      // characters on the page rather than as an ampersand.
      heading={'Options & Futures Profit Calculator'}
      guideHref="/guides"
    />
  );
}
