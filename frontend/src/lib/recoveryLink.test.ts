/**
 * A direct visit to the reset page is not an expired link (OFC-008).
 *
 * Observed on production: opening /auth/reset-password with nothing in the URL
 * showed, at once and in red, "This reset link is no longer valid. Recovery
 * links expire, and each one can only be used once." The visitor had no link.
 */
import { describe, it, expect } from 'vitest';
import { recoveryLinkShape } from './recoveryLink';

describe('recoveryLinkShape', () => {
  it('is "none" for a visit with nothing in the URL', () => {
    expect(recoveryLinkShape('', '')).toBe('none');
  });

  it('is "none" for unrelated parameters', () => {
    expect(recoveryLinkShape('', '?utm_source=email')).toBe('none');
    expect(recoveryLinkShape('#section', '')).toBe('none');
  });

  it('recognises the implicit fragment GoTrue redirects with', () => {
    expect(recoveryLinkShape('#access_token=a&refresh_token=b&type=recovery', '')).toBe('fragment');
  });

  it('needs BOTH tokens in a fragment', () => {
    expect(recoveryLinkShape('#access_token=a', '')).toBe('none');
  });

  it('recognises a PKCE code and a token hash', () => {
    expect(recoveryLinkShape('', '?code=abc')).toBe('code');
    expect(recoveryLinkShape('', '?token_hash=xyz&type=recovery')).toBe('token_hash');
  });
});
