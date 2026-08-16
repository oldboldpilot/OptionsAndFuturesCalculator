import StrategyWorkspace from '../components/StrategyWorkspace';
import SiteGuide from '../components/SiteGuide';

/**
 * The home page.
 *
 * @author Olumuyiwa Oluwasanmi
 *
 * A SERVER component now — it was `'use client'` and returned the workspace
 * alone. That mattered for more than tidiness: this is a static export, so
 * anything a crawler or a policy reviewer reads has to be in the HTML the CDN
 * serves, not assembled after React hydrates. The guide below is the page's
 * only publisher content and it now ships in the document itself.
 *
 * `StrategyWorkspace` carries its own `'use client'`, so rendering it from here
 * is fine and it still hydrates exactly as before.
 */
export default function Home() {
  return (
    <>
      {/*
        The site had NO <h1> on its home page at all — the workspace renders one
        only when a strategy page passes a heading in. A document whose main
        subject is stated nowhere in its own markup is hard for a crawler to
        place and impossible for a reviewer to summarise.
      */}
      <StrategyWorkspace
        // An expression, not a quoted string. JSX does not decode entities in
        // attribute values, so "&amp;" here would render as those five
        // characters on the page rather than as an ampersand.
        heading={'Options & Futures Profit Calculator'}
        guideHref="#guide"
      />
      <SiteGuide />
    </>
  );
}
