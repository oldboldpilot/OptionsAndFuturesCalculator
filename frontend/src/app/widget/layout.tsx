import type { Metadata } from 'next';
import { pageTitle } from '@/lib/seo-title';

export const metadata: Metadata = {
  title: pageTitle('Embeddable widget'),
  robots: { index: false, follow: true },
};

/**
 * The embeddable widget's layout: a landmark and nothing else.
 *
 * It sits OUTSIDE the `(site)` route group on purpose. That group's layout adds
 * the broker block, the 26-link directory and the legal footer, and an embed in
 * somebody else's page has no business carrying any of them -- measured, they
 * took the widget from a calculator-sized frame to 1,987px tall and put this
 * site's affiliate links inside a third party's page. The root layout still
 * supplies the document shell and the skip link; the site nav declines to
 * render here (see SiteNav).
 */
export default function WidgetLayout({
  children,
}: {
  children: React.ReactNode;
}) {
  return (
    <main id="main" tabIndex={-1}>
      {children}
    </main>
  );
}
