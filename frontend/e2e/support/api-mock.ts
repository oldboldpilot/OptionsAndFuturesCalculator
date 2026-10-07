/**
 * gRPC-Web transport mock for the browser specs.
 *
 * @author Olumuyiwa Oluwasanmi
 *
 * The generated client speaks `application/grpc-web-text`: the request body is
 * the base64 of one length-prefixed protobuf frame, and the response is the
 * base64 of a DATA frame followed by a TRAILERS frame carrying `grpc-status`.
 * Both directions are implemented here from that framing alone, so what the
 * page sees is exactly what the real ingress would have sent.
 *
 * Every request the page makes to any other origin is aborted. A spec that
 * silently depended on the live Treasury feed, a font host or an analytics
 * beacon would be measuring the network.
 */
import type { Page, Route } from '@playwright/test';
import * as pb from '../../src/grpc/calculator_pb';
import {
  INSTRUMENTS, chainResponse, priceStrategy, quoteResponse, rateResponse,
  type Instrument,
} from './mock-engine';

const API_HOST = 'api.optionsandfuturescalculator.com';

function frame(flag: number, body: Uint8Array): Buffer {
  const out = Buffer.alloc(5 + body.length);
  out.writeUInt8(flag, 0);
  out.writeUInt32BE(body.length, 1);
  Buffer.from(body).copy(out, 5);
  return out;
}

/** Base64 gRPC-Web body for a successful unary reply. */
export function okBody(message: { serializeBinary(): Uint8Array }): string {
  const data = frame(0x00, message.serializeBinary());
  const trailers = frame(0x80, Buffer.from('grpc-status: 0\r\n'));
  return Buffer.concat([data, trailers]).toString('base64');
}

/** Base64 gRPC-Web body for an error reply (trailers only). */
export function errBody(code: number, message: string): string {
  return frame(0x80, Buffer.from(`grpc-status: ${code}\r\ngrpc-message: ${encodeURIComponent(message)}\r\n`))
    .toString('base64');
}

function decodeRequest(b64: string | null): Uint8Array {
  if (!b64) return new Uint8Array();
  const raw = Buffer.from(b64, 'base64');
  const len = raw.readUInt32BE(1);
  return new Uint8Array(raw.subarray(5, 5 + len));
}

const CORS = {
  'access-control-allow-origin': '*',
  'access-control-expose-headers': 'grpc-status,grpc-message',
};

export interface MockOptions {
  /** Instant the engine considers "now". Fixed so dates are reproducible. */
  now?: number;
  /** When false (the default) a multi-leg position is refused with PERMISSION_DENIED. */
  pro?: boolean;
  /** Called for each CalculateStrategy request, so a spec can assert what was sent. */
  onCalculate?: (req: pb.StrategyRequest) => void;
  /** Symbols the quote service answers for; others reply NOT_FOUND. */
  instruments?: Record<string, Instrument>;
}

export interface MockHandle {
  calculations: pb.StrategyRequest[];
}

export const PRO_REFUSAL =
  'Multi-leg strategies are a Pro feature. Single-leg calls and puts remain free. This position has 2 legs.';

export async function installApiMock(page: Page, opts: MockOptions = {}): Promise<MockHandle> {
  const now = opts.now ?? Date.now();
  const instruments = opts.instruments ?? INSTRUMENTS;
  const handle: MockHandle = { calculations: [] };

  await page.route(
    (url) => url.hostname !== 'localhost' && url.hostname !== '127.0.0.1' && url.hostname !== API_HOST,
    (route) => route.abort(),
  );

  await page.route(`https://${API_HOST}/**`, async (route: Route) => {
    const req = route.request();
    if (req.method() === 'OPTIONS') {
      return route.fulfill({
        status: 204,
        headers: {
          ...CORS,
          'access-control-allow-methods': 'POST, OPTIONS',
          'access-control-allow-headers':
            'content-type, x-grpc-web, x-user-agent, x-api-key, authorization, accept, origin',
          'access-control-max-age': '600',
        },
      });
    }

    const method = new URL(req.url()).pathname.split('/').pop() ?? '';
    const bytes = decodeRequest(req.postData());
    const reply = (body: string) =>
      route.fulfill({
        status: 200,
        headers: { ...CORS, 'content-type': 'application/grpc-web-text+proto' },
        body,
      });

    switch (method) {
      case 'GetMarketQuote': {
        const q = pb.QuoteRequest.deserializeBinary(bytes);
        const inst = instruments[q.getSymbol()];
        return reply(inst ? okBody(quoteResponse(inst, now)) : errBody(5, `No quote for ${q.getSymbol()}`));
      }
      case 'GetMarketChain': {
        const c = pb.ChainRequest.deserializeBinary(bytes);
        const inst = instruments[c.getSymbol()];
        return reply(
          inst ? okBody(chainResponse(inst, now, c.getExpirationDate() || undefined))
               : errBody(5, `No chain for ${c.getSymbol()}`),
        );
      }
      case 'GetRiskFreeRate':
        return reply(okBody(rateResponse(now)));
      case 'CalculateStrategy': {
        const s = pb.StrategyRequest.deserializeBinary(bytes);
        handle.calculations.push(s);
        opts.onCalculate?.(s);
        if (!opts.pro && s.getLegsList().length > 1) return reply(errBody(7, PRO_REFUSAL));
        return reply(okBody(priceStrategy(s, now)));
      }
      case 'ListStrategies':
        return reply(errBody(16, 'sign in'));
      default:
        return reply(errBody(12, `unmocked method ${method}`));
    }
  });

  return handle;
}
