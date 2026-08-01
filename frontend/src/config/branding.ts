export interface BrandingConfig {
  companyName: string;
  appName: string;
  description: string;
  themeColor: string;
  logoUrl: string;
  ogImageUrl: string;
  twitterHandle: string;
  canonicalUrl: string;
}

/**
 * The defaults are THIS site, not a placeholder.
 *
 * They used to fall back to sensen.io, and because `.env*` is gitignored the
 * build never had an override -- so every page shipped
 * `<link rel="canonical" href="https://sensen.io">`. A cross-domain canonical
 * is the strongest available signal that a page is duplicate content belonging
 * to someone else: it tells Google not to index this site but the other one.
 * Anything reviewing this domain -- AdSense included -- followed that straight
 * off the site.
 *
 * A default that only works when an untracked file supplies the real value is
 * not a default; it is a silent failure waiting for the first person who builds
 * without it. The environment variables still override, for previews and forks.
 */
const SITE_URL = "https://optionsandfuturescalculator.com";

export const branding: BrandingConfig = {
  companyName: process.env.NEXT_PUBLIC_COMPANY_NAME || "Options & Futures Calculator",
  appName: process.env.NEXT_PUBLIC_APP_NAME || "Options & Futures Calculator",
  description: process.env.NEXT_PUBLIC_APP_DESCRIPTION || "Price options and futures strategies on live market data: payoff curves, Greeks, implied volatility and term structure.",
  themeColor: process.env.NEXT_PUBLIC_THEME_COLOR || "#111111",
  logoUrl: process.env.NEXT_PUBLIC_LOGO_URL || "/logo.png",
  // Absolute, because og:image is resolved by crawlers that have no page
  // context. Relative, it was being resolved against the BUILD host -- which
  // shipped `http://localhost:3000/og-image.png` to production.
  ogImageUrl: process.env.NEXT_PUBLIC_OG_IMAGE_URL || `${SITE_URL}/og-image.png`,
  // Empty rather than "@Sensen", which is what this defaulted to. That handle
  // named an internal project, not this site, and it shipped on every page as
  // <meta name="twitter:creator">, publicly attributing the site to an account
  // that has nothing to do with it. Omitting the tag is correct when there is
  // no account to credit; a wrong handle is worse than an absent one, because
  // the card renders someone else's name under this site's content.
  // Set NEXT_PUBLIC_TWITTER_HANDLE once a real account exists.
  twitterHandle: process.env.NEXT_PUBLIC_TWITTER_HANDLE || "",
  canonicalUrl: process.env.NEXT_PUBLIC_CANONICAL_URL || SITE_URL,
};
