/**
 * Which kind of password-recovery link a visitor arrived on, if any.
 *
 * Three shapes reach /auth/reset-password (see ResetPasswordForm for why all
 * three are handled), and a fourth state exists that is not a link at all: the
 * visitor simply navigated there. The page used to treat that fourth state as
 * an EXPIRED link, so a direct visit opened with a red "This reset link is no
 * longer valid" -- an accusation about something the person never did. "none"
 * is its own answer, with its own neutral wording.
 *
 * Pure, over the two strings the browser hands over, so it can be tested
 * without a window.
 */
export type RecoveryLink = 'fragment' | 'code' | 'token_hash' | 'none';

export function recoveryLinkShape(hash: string, search: string): RecoveryLink {
  const frag = new URLSearchParams(hash.replace(/^#/, ''));
  if (frag.get('access_token') && frag.get('refresh_token')) return 'fragment';
  const query = new URLSearchParams(search.replace(/^\?/, ''));
  if (query.get('code')) return 'code';
  if (query.get('token_hash')) return 'token_hash';
  return 'none';
}
