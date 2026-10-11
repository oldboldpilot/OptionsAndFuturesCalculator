import SiteFooter from "@/components/SiteFooter";

/**
 * The layout for every page that is part of THE SITE, as opposed to the embeddable
 * widget: the one <main> landmark, then the broker block and footer beneath it.
 *
 * It is a route GROUP, so it changes no URL (`/guides/long-call` is still
 * `/guides/long-call`). What it changes is which pages inherit the chrome. The
 * widget used to inherit it from the root layout and had no way to refuse it.
 *
 * `<main>` is owned here, and by the widget's own layout, rather than by the root
 * layout, for the same reason: the footer has to sit OUTSIDE the main landmark to
 * be the page's contentinfo, and a root-level `<main>` would have had the nested
 * layout's footer inside it.
 */
export default function SiteLayout({ children }: { children: React.ReactNode }) {
  return (
    <>
      <main id="main" tabIndex={-1}>
        {children}
      </main>
      <SiteFooter />
    </>
  );
}
