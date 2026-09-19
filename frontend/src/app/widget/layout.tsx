import type { Metadata } from 'next';
import { pageTitle } from '@/lib/seo-title';

export const metadata: Metadata = {
  title: pageTitle('Embeddable widget'),
  robots: { index: false, follow: true },
};

export default function WidgetLayout({
  children,
}: {
  children: React.ReactNode;
}) {
  return <>{children}</>;
}
