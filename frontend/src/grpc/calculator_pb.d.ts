import * as jspb from 'google-protobuf'

export class Leg extends jspb.Message {
  getAction(): Leg.Action;
  setAction(value: Leg.Action): Leg;

  getType(): Leg.Type;
  setType(value: Leg.Type): Leg;

  getStrike(): number;
  setStrike(value: number): Leg;

  getExpirationDays(): number;
  setExpirationDays(value: number): Leg;

  getQuantity(): number;
  setQuantity(value: number): Leg;

  serializeBinary(): Uint8Array;
  toObject(includeInstance?: boolean): Leg.AsObject;
  static toObject(includeInstance: boolean, msg: Leg): Leg.AsObject;
  static serializeBinaryToWriter(message: Leg, writer: jspb.BinaryWriter): void;
  static deserializeBinary(bytes: Uint8Array): Leg;
  static deserializeBinaryFromReader(message: Leg, reader: jspb.BinaryReader): Leg;
}

export namespace Leg {
  export type AsObject = {
    action: Leg.Action,
    type: Leg.Type,
    strike: number,
    expirationDays: number,
    quantity: number,
  }

  export enum Action { 
    BUY = 0,
    SELL = 1,
  }

  export enum Type { 
    CALL = 0,
    PUT = 1,
    FUTURE = 2,
    STOCK = 3,
  }
}

export class StrategyRequest extends jspb.Message {
  getUnderlyingSymbol(): string;
  setUnderlyingSymbol(value: string): StrategyRequest;

  getCurrentPrice(): number;
  setCurrentPrice(value: number): StrategyRequest;

  getImpliedVolatility(): number;
  setImpliedVolatility(value: number): StrategyRequest;

  getRiskFreeRate(): number;
  setRiskFreeRate(value: number): StrategyRequest;

  getLegsList(): Array<Leg>;
  setLegsList(value: Array<Leg>): StrategyRequest;
  clearLegsList(): StrategyRequest;
  addLegs(value?: Leg, index?: number): Leg;

  serializeBinary(): Uint8Array;
  toObject(includeInstance?: boolean): StrategyRequest.AsObject;
  static toObject(includeInstance: boolean, msg: StrategyRequest): StrategyRequest.AsObject;
  static serializeBinaryToWriter(message: StrategyRequest, writer: jspb.BinaryWriter): void;
  static deserializeBinary(bytes: Uint8Array): StrategyRequest;
  static deserializeBinaryFromReader(message: StrategyRequest, reader: jspb.BinaryReader): StrategyRequest;
}

export namespace StrategyRequest {
  export type AsObject = {
    underlyingSymbol: string,
    currentPrice: number,
    impliedVolatility: number,
    riskFreeRate: number,
    legsList: Array<Leg.AsObject>,
  }
}

export class Greeks extends jspb.Message {
  getDelta(): number;
  setDelta(value: number): Greeks;

  getGamma(): number;
  setGamma(value: number): Greeks;

  getTheta(): number;
  setTheta(value: number): Greeks;

  getVega(): number;
  setVega(value: number): Greeks;

  getRho(): number;
  setRho(value: number): Greeks;

  serializeBinary(): Uint8Array;
  toObject(includeInstance?: boolean): Greeks.AsObject;
  static toObject(includeInstance: boolean, msg: Greeks): Greeks.AsObject;
  static serializeBinaryToWriter(message: Greeks, writer: jspb.BinaryWriter): void;
  static deserializeBinary(bytes: Uint8Array): Greeks;
  static deserializeBinaryFromReader(message: Greeks, reader: jspb.BinaryReader): Greeks;
}

export namespace Greeks {
  export type AsObject = {
    delta: number,
    gamma: number,
    theta: number,
    vega: number,
    rho: number,
  }
}

export class PnLPoint extends jspb.Message {
  getUnderlyingPrice(): number;
  setUnderlyingPrice(value: number): PnLPoint;

  getPnl(): number;
  setPnl(value: number): PnLPoint;

  serializeBinary(): Uint8Array;
  toObject(includeInstance?: boolean): PnLPoint.AsObject;
  static toObject(includeInstance: boolean, msg: PnLPoint): PnLPoint.AsObject;
  static serializeBinaryToWriter(message: PnLPoint, writer: jspb.BinaryWriter): void;
  static deserializeBinary(bytes: Uint8Array): PnLPoint;
  static deserializeBinaryFromReader(message: PnLPoint, reader: jspb.BinaryReader): PnLPoint;
}

export namespace PnLPoint {
  export type AsObject = {
    underlyingPrice: number,
    pnl: number,
  }
}

export class RiskMetrics extends jspb.Message {
  getVarParametric95(): number;
  setVarParametric95(value: number): RiskMetrics;

  getVarParametric99(): number;
  setVarParametric99(value: number): RiskMetrics;

  getCvarParametric95(): number;
  setCvarParametric95(value: number): RiskMetrics;

  getCvarParametric99(): number;
  setCvarParametric99(value: number): RiskMetrics;

  serializeBinary(): Uint8Array;
  toObject(includeInstance?: boolean): RiskMetrics.AsObject;
  static toObject(includeInstance: boolean, msg: RiskMetrics): RiskMetrics.AsObject;
  static serializeBinaryToWriter(message: RiskMetrics, writer: jspb.BinaryWriter): void;
  static deserializeBinary(bytes: Uint8Array): RiskMetrics;
  static deserializeBinaryFromReader(message: RiskMetrics, reader: jspb.BinaryReader): RiskMetrics;
}

export namespace RiskMetrics {
  export type AsObject = {
    varParametric95: number,
    varParametric99: number,
    cvarParametric95: number,
    cvarParametric99: number,
  }
}

export class StrategyResponse extends jspb.Message {
  getMaxProfit(): number;
  setMaxProfit(value: number): StrategyResponse;

  getMaxLoss(): number;
  setMaxLoss(value: number): StrategyResponse;

  getBreakEven(): number;
  setBreakEven(value: number): StrategyResponse;

  getExpectedValue(): number;
  setExpectedValue(value: number): StrategyResponse;

  getPop(): number;
  setPop(value: number): StrategyResponse;

  getNetGreeks(): Greeks | undefined;
  setNetGreeks(value?: Greeks): StrategyResponse;
  hasNetGreeks(): boolean;
  clearNetGreeks(): StrategyResponse;

  getRiskMetrics(): RiskMetrics | undefined;
  setRiskMetrics(value?: RiskMetrics): StrategyResponse;
  hasRiskMetrics(): boolean;
  clearRiskMetrics(): StrategyResponse;

  getPnlMatrixList(): Array<PnLPoint>;
  setPnlMatrixList(value: Array<PnLPoint>): StrategyResponse;
  clearPnlMatrixList(): StrategyResponse;
  addPnlMatrix(value?: PnLPoint, index?: number): PnLPoint;

  serializeBinary(): Uint8Array;
  toObject(includeInstance?: boolean): StrategyResponse.AsObject;
  static toObject(includeInstance: boolean, msg: StrategyResponse): StrategyResponse.AsObject;
  static serializeBinaryToWriter(message: StrategyResponse, writer: jspb.BinaryWriter): void;
  static deserializeBinary(bytes: Uint8Array): StrategyResponse;
  static deserializeBinaryFromReader(message: StrategyResponse, reader: jspb.BinaryReader): StrategyResponse;
}

export namespace StrategyResponse {
  export type AsObject = {
    maxProfit: number,
    maxLoss: number,
    breakEven: number,
    expectedValue: number,
    pop: number,
    netGreeks?: Greeks.AsObject,
    riskMetrics?: RiskMetrics.AsObject,
    pnlMatrixList: Array<PnLPoint.AsObject>,
  }
}

export class QuoteRequest extends jspb.Message {
  getSymbol(): string;
  setSymbol(value: string): QuoteRequest;
  serializeBinary(): Uint8Array;
  static deserializeBinary(bytes: Uint8Array): QuoteRequest;
}

export class QuoteResponse extends jspb.Message {
  getSymbol(): string;
  setSymbol(value: string): QuoteResponse;
  getPrice(): number;
  setPrice(value: number): QuoteResponse;
  getPreviousClose(): number;
  setPreviousClose(value: number): QuoteResponse;
  getForwardPe(): number;
  setForwardPe(value: number): QuoteResponse;
  getImpliedVolatility(): number;
  setImpliedVolatility(value: number): QuoteResponse;
  getAssetClass(): string;
  setAssetClass(value: string): QuoteResponse;
  serializeBinary(): Uint8Array;
  static deserializeBinary(bytes: Uint8Array): QuoteResponse;
}

export class OptionStrike extends jspb.Message {
  getStrike(): number;
  getCallBid(): number;
  getCallAsk(): number;
  getCallDelta(): number;
  getCallVolume(): number;
  getCallOpenInterest(): number;
  getCallIv(): number;
  getPutBid(): number;
  getPutAsk(): number;
  getPutDelta(): number;
  getPutVolume(): number;
  getPutOpenInterest(): number;
  getPutIv(): number;
  getIsAtm(): boolean;
  getExpirationDate(): string;
  serializeBinary(): Uint8Array;
  static deserializeBinary(bytes: Uint8Array): OptionStrike;
}

export class FuturesContract extends jspb.Message {
  getCode(): string;
  getDeliveryMonth(): string;
  getDaysToExpiry(): number;
  getFuturesPrice(): number;
  getBid(): number;
  getAsk(): number;
  getBasis(): number;
  getAnnualizedYield(): number;
  getVolume(): number;
  getOpenInterest(): number;
  getState(): string;
  serializeBinary(): Uint8Array;
  static deserializeBinary(bytes: Uint8Array): FuturesContract;
}

export class ExpirationDate extends jspb.Message {
  getDateStr(): string;
  getDaysToExpiry(): number;
  getLabel(): string;
  serializeBinary(): Uint8Array;
  static deserializeBinary(bytes: Uint8Array): ExpirationDate;
}

export class ChainRequest extends jspb.Message {
  getSymbol(): string;
  setSymbol(value: string): ChainRequest;
  getExpirationDays(): number;
  setExpirationDays(value: number): ChainRequest;
  getAssetClass(): string;
  setAssetClass(value: string): ChainRequest;
  getExpirationDate(): string;
  setExpirationDate(value: string): ChainRequest;
  serializeBinary(): Uint8Array;
  static deserializeBinary(bytes: Uint8Array): ChainRequest;
}

export class ChainResponse extends jspb.Message {
  getSymbol(): string;
  getSpotPrice(): number;
  getOptionStrikesList(): Array<OptionStrike>;
  getFuturesContractsList(): Array<FuturesContract>;
  getSelectedExpirationDate(): string;
  getAvailableExpirationsList(): Array<ExpirationDate>;
  serializeBinary(): Uint8Array;
  static deserializeBinary(bytes: Uint8Array): ChainResponse;
}
