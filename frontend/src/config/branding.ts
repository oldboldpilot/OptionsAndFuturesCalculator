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

export const branding: BrandingConfig = {
  companyName: process.env.NEXT_PUBLIC_COMPANY_NAME || "Sensen",
  appName: process.env.NEXT_PUBLIC_APP_NAME || "Options & Futures Calculator",
  description: process.env.NEXT_PUBLIC_APP_DESCRIPTION || "Advanced deterministic modeling, multi-leg options execution, and stochastic simulations.",
  themeColor: process.env.NEXT_PUBLIC_THEME_COLOR || "#111111",
  logoUrl: process.env.NEXT_PUBLIC_LOGO_URL || "/logo.png",
  ogImageUrl: process.env.NEXT_PUBLIC_OG_IMAGE_URL || "/og-image.png",
  twitterHandle: process.env.NEXT_PUBLIC_TWITTER_HANDLE || "@Sensen",
  canonicalUrl: process.env.NEXT_PUBLIC_CANONICAL_URL || "https://sensen.io",
};
