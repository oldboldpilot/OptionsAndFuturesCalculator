import type { Metadata } from 'next';

export const metadata: Metadata = {
  title: 'Embeddable widget',
  robots: { index: false, follow: true },
};

export default function WidgetLayout({
  children,
}: {
  children: React.ReactNode;
}) {
  return <>{children}</>;
}
