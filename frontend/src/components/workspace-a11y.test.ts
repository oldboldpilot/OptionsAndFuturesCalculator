/**
 * Regression tests for calculator workspace accessibility defects:
 *
 * 1. Option Chain Buy/Sell buttons:
 *    Ensures every buy/sell button in the chain has a unique accessible name
 *    containing side, option type, and its strike (and expiration if present),
 *    preventing screen readers from announcing identical generic names for
 *    different strikes.
 *
 * 2. Ticket toggle groups:
 *    Ensures each toggle group (Action, Option type, Averaging) has an accessible
 *    group name, exposes exactly one selected member via `aria-pressed="true"`,
 *    and moves selection appropriately when state changes.
 *
 * Render-testing note:
 * As confirmed in vitest.config.mts, Vitest runs in a 'node' environment without
 * @testing-library/react or jsdom. Assertions run against the component's rendered
 * output via React's built-in `renderToStaticMarkup` from `react-dom/server` without
 * introducing new test dependencies.
 *
 * @author Olumuyiwa Oluwasanmi
 */

import { describe, it, expect, beforeEach, vi } from 'vitest';
import React from 'react';
import { renderToStaticMarkup } from 'react-dom/server';
import { OptionChain } from './OptionChain';
import { OptionTicket } from './OptionTicket';
import { useCalculatorStore, type ChainStrike } from '../store/useCalculatorStore';

vi.mock('../grpc/CalculatorServiceClientPb', () => ({
  OptionsCalculatorClient: class {
    getMarketQuote = vi.fn();
    getRiskFreeRate = vi.fn();
    getMarketChain = vi.fn();
    calculateStrategy = vi.fn();
  },
}));

vi.mock('../lib/supabase/client', () => ({
  createClient: () => ({
    auth: {
      getSession: async () => ({ data: { session: null }, error: null }),
      onAuthStateChange: () => ({ data: { subscription: { unsubscribe() {} } } }),
    },
  }),
}));

vi.mock('../store/useCalculatorStore', async (importOriginal) => {
  const actual = await importOriginal<typeof import('../store/useCalculatorStore')>();
  const store = actual.useCalculatorStore;
  type StoreState = ReturnType<typeof store.getState>;
  const useMockStore = (<T,>(selector?: (state: StoreState) => T) => {
    const state = store.getState();
    return selector ? selector(state) : state;
  }) as typeof actual.useCalculatorStore;
  Object.assign(useMockStore, store);
  return {
    ...actual,
    useCalculatorStore: useMockStore,
  };
});

function makeChainStrike(strike: number, isAtm = false): ChainStrike {
  return {
    strike,
    isAtm,
    call: { bid: 4.0, ask: 4.2, delta: 0.5, iv: 0.2, volume: 100, openInterest: 500 },
    put: { bid: 3.8, ask: 4.0, delta: -0.5, iv: 0.2, volume: 80, openInterest: 400 },
  };
}

const FIXTURE_STRIKES: ChainStrike[] = [
  makeChainStrike(750),
  makeChainStrike(755),
  makeChainStrike(760),
  makeChainStrike(761.5, true),
  makeChainStrike(765),
  makeChainStrike(770),
];

interface ParsedButton {
  raw: string;
  className: string;
  title: string | null;
  ariaLabel: string | null;
  ariaPressed: string | null;
  text: string;
  accessibleName: string;
}

function parseButtons(html: string): ParsedButton[] {
  const buttonRegex = /<button\b([^>]*)>([\s\S]*?)<\/button>/gi;
  const buttons: ParsedButton[] = [];
  let match: RegExpExecArray | null;
  while ((match = buttonRegex.exec(html)) !== null) {
    const attrs = match[1];
    const text = match[2].replace(/<[^>]+>/g, '').trim();
    const classMatch = attrs.match(/\bclass="([^"]*)"/i);
    const titleMatch = attrs.match(/\btitle="([^"]*)"/i);
    const ariaLabelMatch = attrs.match(/\baria-label="([^"]*)"/i);
    const ariaPressedMatch = attrs.match(/\baria-pressed="([^"]*)"/i);

    const ariaLabel = ariaLabelMatch ? ariaLabelMatch[1] : null;
    const title = titleMatch ? titleMatch[1] : null;
    const ariaPressed = ariaPressedMatch ? ariaPressedMatch[1] : null;
    const className = classMatch ? classMatch[1] : '';

    // Accessible name computation: aria-label takes precedence over visible text and title
    const accessibleName = ariaLabel || text || title || '';

    buttons.push({
      raw: match[0],
      className,
      title,
      ariaLabel,
      ariaPressed,
      text,
      accessibleName,
    });
  }
  return buttons;
}

interface ParsedToggleGroup {
  raw: string;
  ariaLabel: string;
  buttons: ParsedButton[];
}

function parseToggleGroups(html: string): ParsedToggleGroup[] {
  const groupRegex = /<div\b[^>]*\brole="group"[^>]*>([\s\S]*?)<\/div>/gi;
  const groups: ParsedToggleGroup[] = [];
  let match: RegExpExecArray | null;
  while ((match = groupRegex.exec(html)) !== null) {
    const fullTag = match[0];
    const innerHtml = match[1];
    const labelMatch = fullTag.match(/\baria-label="([^"]*)"/i);
    const ariaLabel = labelMatch ? labelMatch[1] : '';
    const buttons = parseButtons(innerHtml);
    if (buttons.length > 0) {
      groups.push({
        raw: fullTag,
        ariaLabel,
        buttons,
      });
    }
  }
  return groups;
}

describe('Option Chain Accessibility (Defect 1)', () => {
  beforeEach(() => {
    useCalculatorStore.setState({
      chainStrikes: FIXTURE_STRIKES,
      chainStatus: 'ready',
      chainError: null,
      selectedExpiration: '2026-10-16',
    });
  });

  it('renders unique accessible names containing the strike on every buy/sell button', () => {
    // Derive the strike list directly from the component's own fixture, never hand-written
    const fixtureStrikeValues = FIXTURE_STRIKES.map((s) => s.strike);

    const html = renderToStaticMarkup(React.createElement(OptionChain));
    const allButtons = parseButtons(html);

    // Filter to the action buttons (Buy and Sell) in the chain ladder
    const actionButtons = allButtons.filter(
      (b) => b.className.includes('btn-buy') || b.className.includes('btn-sell')
    );

    // Each strike has 4 buttons: Buy CALL, Sell CALL, Buy PUT, Sell PUT
    const expectedButtonCount = fixtureStrikeValues.length * 4;
    expect(actionButtons.length).toBe(expectedButtonCount);

    // Every button must have an aria-label providing its accessible name
    for (const btn of actionButtons) {
      expect(btn.ariaLabel).toBeTruthy();
      // Keep visible B/S text and existing title
      expect(['B', 'S']).toContain(btn.text);
      expect(btn.title).toMatch(/^(Buy|Sell) (CALL|PUT)$/);
    }

    // Assert that the number of distinct accessible names EQUALS the total number of buttons.
    // The defect was 155 identical names; a test only checking "has an aria-label" would pass
    // if all buttons had aria-label="Buy CALL". Uniqueness across all buttons proves differentiation.
    const accessibleNames = actionButtons.map((b) => b.accessibleName);
    const distinctNames = new Set(accessibleNames);
    expect(
      distinctNames.size,
      `Expected ${actionButtons.length} distinct button accessible names, but only found ${distinctNames.size}`
    ).toBe(actionButtons.length);

    // Assert EVERY buy/sell button has an accessible name containing its strike
    for (const btn of actionButtons) {
      const containsStrike = fixtureStrikeValues.some((strike) =>
        btn.accessibleName.includes(strike.toFixed(2))
      );
      expect(
        containsStrike,
        `Expected button accessible name "${btn.accessibleName}" to contain its strike`
      ).toBe(true);
    }
  });

  it('asserts per-row strike matching across call and put buy/sell buttons', () => {
    const html = renderToStaticMarkup(React.createElement(OptionChain));

    // Inspect each table row in tbody to verify strike association
    const rowRegex = /<tr\b[^>]*>([\s\S]*?)<\/tr>/gi;
    const rows: string[] = [];
    let rMatch: RegExpExecArray | null;
    while ((rMatch = rowRegex.exec(html)) !== null) {
      // Exclude header rows
      if (!rMatch[1].includes('<th')) {
        rows.push(rMatch[1]);
      }
    }

    expect(rows.length).toBe(FIXTURE_STRIKES.length);

    FIXTURE_STRIKES.forEach((fixture, index) => {
      const rowHtml = rows[index];
      const rowButtons = parseButtons(rowHtml).filter(
        (b) => b.className.includes('btn-buy') || b.className.includes('btn-sell')
      );

      // 4 buttons per row: Buy Call, Sell Call, Buy Put, Sell Put
      expect(rowButtons.length).toBe(4);

      const strikeStr = fixture.strike.toFixed(2);
      for (const btn of rowButtons) {
        expect(btn.accessibleName).toContain(strikeStr);
      }

      // Within the row, all 4 buttons must also have distinct accessible names
      const rowNames = new Set(rowButtons.map((b) => b.accessibleName));
      expect(rowNames.size).toBe(4);
    });
  });
});

describe('Ticket Toggle Groups Accessibility (Defect 2)', () => {
  beforeEach(() => {
    useCalculatorStore.setState({
      ticket: {
        action: 'BUY',
        optionType: 'CALL',
        expiration: '2026-10-16',
        strike: 760,
        premium: 4.2,
        quantity: 1,
        impliedVolatility: 0.2,
        asianType: 'NOT_ASIAN',
      },
    });
  });

  it('asserts each ticket toggle group has an accessible name and exposes exactly one selected member', () => {
    const html = renderToStaticMarkup(React.createElement(OptionTicket));
    const groups = parseToggleGroups(html);

    // 3 toggle groups: Action (Buy/Sell), Option type (Call/Put), Averaging (Vanilla/Avg price/Avg strike)
    expect(groups.length).toBe(3);

    // Each group must have an accessible name
    for (const group of groups) {
      expect(group.ariaLabel).toBeTruthy();
      expect(group.ariaLabel.length).toBeGreaterThan(0);
    }

    expect(groups.map((g) => g.ariaLabel)).toEqual(['Action', 'Option type', 'Averaging']);

    // Each group must expose exactly one selected member via aria-pressed="true"
    for (const group of groups) {
      // Every button in the group must carry aria-pressed
      for (const btn of group.buttons) {
        expect(btn.ariaPressed).not.toBeNull();
        expect(['true', 'false']).toContain(btn.ariaPressed);
      }

      const pressedButtons = group.buttons.filter((b) => b.ariaPressed === 'true');
      expect(
        pressedButtons.length,
        `Group "${group.ariaLabel}" expected exactly one selected member, found ${pressedButtons.length}`
      ).toBe(1);
    }

    // Initial default state
    expect(groups[0].buttons.find((b) => b.ariaPressed === 'true')?.text).toBe('Buy');
    expect(groups[1].buttons.find((b) => b.ariaPressed === 'true')?.text).toBe('Call');
    expect(groups[2].buttons.find((b) => b.ariaPressed === 'true')?.text).toBe('Vanilla');
  });

  it('asserts selecting another member moves the pressed state in each group', () => {
    // Select the alternate choices in each toggle group
    useCalculatorStore.getState().setTicket({
      action: 'SELL',
      optionType: 'PUT',
      asianType: 'AVERAGE_PRICE',
    });

    const html = renderToStaticMarkup(React.createElement(OptionTicket));
    const groups = parseToggleGroups(html);

    expect(groups.length).toBe(3);

    for (const group of groups) {
      const pressedButtons = group.buttons.filter((b) => b.ariaPressed === 'true');
      expect(
        pressedButtons.length,
        `Group "${group.ariaLabel}" expected exactly one selected member, found ${pressedButtons.length}`
      ).toBe(1);
    }

    // Verified moved states
    expect(groups[0].buttons.find((b) => b.ariaPressed === 'true')?.text).toBe('Sell');
    expect(groups[1].buttons.find((b) => b.ariaPressed === 'true')?.text).toBe('Put');
    expect(groups[2].buttons.find((b) => b.ariaPressed === 'true')?.text).toBe('Avg price');

    // Move averaging to the 3rd choice: Avg strike
    useCalculatorStore.getState().setTicket({
      asianType: 'AVERAGE_STRIKE',
    });

    const html3 = renderToStaticMarkup(React.createElement(OptionTicket));
    const groups3 = parseToggleGroups(html3);
    expect(groups3[2].buttons.find((b) => b.ariaPressed === 'true')?.text).toBe('Avg strike');
    expect(groups3[2].buttons.filter((b) => b.ariaPressed === 'true').length).toBe(1);
  });
});
