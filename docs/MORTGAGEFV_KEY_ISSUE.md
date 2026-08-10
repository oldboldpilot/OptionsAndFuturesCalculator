# For Lovable — `ParseOperation` returning gRPC code 7

@author Olumuyiwa Oluwasanmi

**Short version: your API key is reaching us but being rejected on its shape,
before we ever look it up. It is not an entitlement problem — nothing needs to
change on your account.**

---

## What we see

Over the last window of traffic our logs show **206 requests carrying a key that
failed format validation**, and only 4 with no key at all. Zero authenticated
successfully.

```
auth would-deny: key=<none> method=ParseOperation outcome=malformed
pro-gate deny:   key=<anonymous> rpc=ParseOperation tier=free
```

`outcome=malformed` is the important word. If you were simply not sending the
header we would log `no-key`. `malformed` means the header **is** present and the
**value** is wrong.

Your account itself is correct and we have not changed it:

| | |
| --- | --- |
| id | `mortgagefvcalculator` |
| tier | `partner` (satisfies the Pro requirement) |
| scopes | `finance`, `assistant` |

## Why the calculations still work but the assistant does not

This is the confusing part from your side, and it is expected given the above.

Key enforcement on the Finance RPCs is currently in **observe** mode — a bad key
is logged but the call is still served. So `ComputeAmortization`,
`ComputeHomeNpv`, `ComputeFutureValue` all keep working.

The assistant is different: it is Pro-gated, and the gate reads the **identity**
the key produced. A malformed key produces *no* identity, so you are treated as
anonymous, and anonymous is denied with code 7.

**Same broken key. Two different visible outcomes.** That is why this looks like
a permissions problem rather than a formatting one.

## The fix

Send the key **raw**, in the `x-api-key` header:

```
x-api-key: pk_live_XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX
```

It must satisfy **all** of:

- starts with `pk_live_` (browser/publishable) or `sk_live_` (server-side)
- the part after the prefix is exactly **43** characters
- total length exactly **51**

Common causes of `malformed`, in the order we would check them:

1. **A `Bearer ` prefix left on** — this header takes the bare key, not an
   `Authorization`-style value.
2. **Truncation** — a key cut short when copied into an env var or a secrets UI.
   Check the length is 51.
3. **URL-encoding or quoting** — surrounding quotes, `%3D`, or escaped
   characters. Send the literal string.
4. **A placeholder never substituted** — e.g. the literal text
   `YOUR_API_KEY_HERE` or an unexpanded `${...}`.

Quick self-check, no request needed:

```bash
printf '%s' "$YOUR_KEY" | wc -c        # must print 51
printf '%s' "$YOUR_KEY" | cut -c1-8    # must print pk_live_ or sk_live_
```

And end-to-end:

```bash
curl -sS -X POST \
  https://api.optionsandfuturescalculator.com/mortgage.assistant.MortgageAssistant/ParseOperation \
  -H 'content-type: application/json' \
  -H "x-api-key: $YOUR_KEY" \
  -d '{"utterance":"Amortization schedule for a 15-year loan of $250,000 at 5 percent."}'
```

Expected on success: a `params` object naming a `sensen.finance.Finance`
operation. A `refusal` or `clarification` is also success — see the handoff doc,
§5. Code 7 means the key still is not parsing.

## If your key is genuinely lost or was truncated at issue time

We store only a SHA-512 hash, so we cannot read your key back to you or check
whether your copy matches. If the self-check above fails and you cannot recover
the original, say so and we will issue a replacement — it is a two-minute change
on our side and the old one stops working the moment we swap it.

## One thing we are fixing on our end

A malformed key currently produces the *same* "this is a Pro feature" message as
sending no key at all, which is what sent you looking at entitlements. We are
changing that so a malformed key says so explicitly. That would have made this a
thirty-second diagnosis instead of a support round trip, and the fault for that
is ours.
