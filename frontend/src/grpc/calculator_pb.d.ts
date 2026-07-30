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

  getPremium(): number;
  setPremium(value: number): Leg;

  getImpliedVolatility(): number;
  setImpliedVolatility(value: number): Leg;

  getContractMultiplier(): number;
  setContractMultiplier(value: number): Leg;

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
    premium: number,
    impliedVolatility: number,
    contractMultiplier: number,
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

  getPriceRangePercent(): number;
  setPriceRangePercent(value: number): StrategyRequest;

  getPriceSteps(): number;
  setPriceSteps(value: number): StrategyRequest;

  getDateSteps(): number;
  setDateSteps(value: number): StrategyRequest;

  getDividendYield(): number;
  setDividendYield(value: number): StrategyRequest;

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
    priceRangePercent: number,
    priceSteps: number,
    dateSteps: number,
    dividendYield: number,
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

  getVanna(): number;
  setVanna(value: number): Greeks;

  getVolga(): number;
  setVolga(value: number): Greeks;

  getCharm(): number;
  setCharm(value: number): Greeks;

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
    vanna: number,
    volga: number,
    charm: number,
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

export class MatrixCell extends jspb.Message {
  getPrice(): number;
  setPrice(value: number): MatrixCell;

  getDaysToExpiration(): number;
  setDaysToExpiration(value: number): MatrixCell;

  getDateStr(): string;
  setDateStr(value: string): MatrixCell;

  getPnlDollars(): number;
  setPnlDollars(value: number): MatrixCell;

  getReturnOnRiskPercent(): number;
  setReturnOnRiskPercent(value: number): MatrixCell;

  serializeBinary(): Uint8Array;
  toObject(includeInstance?: boolean): MatrixCell.AsObject;
  static toObject(includeInstance: boolean, msg: MatrixCell): MatrixCell.AsObject;
  static serializeBinaryToWriter(message: MatrixCell, writer: jspb.BinaryWriter): void;
  static deserializeBinary(bytes: Uint8Array): MatrixCell;
  static deserializeBinaryFromReader(message: MatrixCell, reader: jspb.BinaryReader): MatrixCell;
}

export namespace MatrixCell {
  export type AsObject = {
    price: number,
    daysToExpiration: number,
    dateStr: string,
    pnlDollars: number,
    returnOnRiskPercent: number,
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

  getRiskRewardRatio(): number;
  setRiskRewardRatio(value: number): StrategyResponse;

  getBreakevenPricesList(): Array<number>;
  setBreakevenPricesList(value: Array<number>): StrategyResponse;
  clearBreakevenPricesList(): StrategyResponse;
  addBreakevenPrices(value: number, index?: number): StrategyResponse;

  getMatrixList(): Array<MatrixCell>;
  setMatrixList(value: Array<MatrixCell>): StrategyResponse;
  clearMatrixList(): StrategyResponse;
  addMatrix(value?: MatrixCell, index?: number): MatrixCell;

  getCalculationTimeMicroseconds(): number;
  setCalculationTimeMicroseconds(value: number): StrategyResponse;

  getProbabilityOfTouch(): number;
  setProbabilityOfTouch(value: number): StrategyResponse;

  getProbabilityOfTargetProfit(): number;
  setProbabilityOfTargetProfit(value: number): StrategyResponse;

  getCurveDaysToExpiration(): number;
  setCurveDaysToExpiration(value: number): StrategyResponse;

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
    riskRewardRatio: number,
    breakevenPricesList: Array<number>,
    matrixList: Array<MatrixCell.AsObject>,
    calculationTimeMicroseconds: number,
    probabilityOfTouch: number,
    probabilityOfTargetProfit: number,
    curveDaysToExpiration: number,
  }
}

export class QuoteRequest extends jspb.Message {
  getSymbol(): string;
  setSymbol(value: string): QuoteRequest;

  serializeBinary(): Uint8Array;
  toObject(includeInstance?: boolean): QuoteRequest.AsObject;
  static toObject(includeInstance: boolean, msg: QuoteRequest): QuoteRequest.AsObject;
  static serializeBinaryToWriter(message: QuoteRequest, writer: jspb.BinaryWriter): void;
  static deserializeBinary(bytes: Uint8Array): QuoteRequest;
  static deserializeBinaryFromReader(message: QuoteRequest, reader: jspb.BinaryReader): QuoteRequest;
}

export namespace QuoteRequest {
  export type AsObject = {
    symbol: string,
  }
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

  getProvider(): string;
  setProvider(value: string): QuoteResponse;

  getQuoteTimestamp(): string;
  setQuoteTimestamp(value: string): QuoteResponse;

  serializeBinary(): Uint8Array;
  toObject(includeInstance?: boolean): QuoteResponse.AsObject;
  static toObject(includeInstance: boolean, msg: QuoteResponse): QuoteResponse.AsObject;
  static serializeBinaryToWriter(message: QuoteResponse, writer: jspb.BinaryWriter): void;
  static deserializeBinary(bytes: Uint8Array): QuoteResponse;
  static deserializeBinaryFromReader(message: QuoteResponse, reader: jspb.BinaryReader): QuoteResponse;
}

export namespace QuoteResponse {
  export type AsObject = {
    symbol: string,
    price: number,
    previousClose: number,
    forwardPe: number,
    impliedVolatility: number,
    assetClass: string,
    provider: string,
    quoteTimestamp: string,
  }
}

export class OptionStrike extends jspb.Message {
  getStrike(): number;
  setStrike(value: number): OptionStrike;

  getCallBid(): number;
  setCallBid(value: number): OptionStrike;

  getCallAsk(): number;
  setCallAsk(value: number): OptionStrike;

  getCallDelta(): number;
  setCallDelta(value: number): OptionStrike;

  getCallVolume(): number;
  setCallVolume(value: number): OptionStrike;

  getCallOpenInterest(): number;
  setCallOpenInterest(value: number): OptionStrike;

  getCallIv(): number;
  setCallIv(value: number): OptionStrike;

  getPutBid(): number;
  setPutBid(value: number): OptionStrike;

  getPutAsk(): number;
  setPutAsk(value: number): OptionStrike;

  getPutDelta(): number;
  setPutDelta(value: number): OptionStrike;

  getPutVolume(): number;
  setPutVolume(value: number): OptionStrike;

  getPutOpenInterest(): number;
  setPutOpenInterest(value: number): OptionStrike;

  getPutIv(): number;
  setPutIv(value: number): OptionStrike;

  getIsAtm(): boolean;
  setIsAtm(value: boolean): OptionStrike;

  getExpirationDate(): string;
  setExpirationDate(value: string): OptionStrike;

  getCallGamma(): number;
  setCallGamma(value: number): OptionStrike;

  getCallTheta(): number;
  setCallTheta(value: number): OptionStrike;

  getCallVega(): number;
  setCallVega(value: number): OptionStrike;

  getPutGamma(): number;
  setPutGamma(value: number): OptionStrike;

  getPutTheta(): number;
  setPutTheta(value: number): OptionStrike;

  getPutVega(): number;
  setPutVega(value: number): OptionStrike;

  serializeBinary(): Uint8Array;
  toObject(includeInstance?: boolean): OptionStrike.AsObject;
  static toObject(includeInstance: boolean, msg: OptionStrike): OptionStrike.AsObject;
  static serializeBinaryToWriter(message: OptionStrike, writer: jspb.BinaryWriter): void;
  static deserializeBinary(bytes: Uint8Array): OptionStrike;
  static deserializeBinaryFromReader(message: OptionStrike, reader: jspb.BinaryReader): OptionStrike;
}

export namespace OptionStrike {
  export type AsObject = {
    strike: number,
    callBid: number,
    callAsk: number,
    callDelta: number,
    callVolume: number,
    callOpenInterest: number,
    callIv: number,
    putBid: number,
    putAsk: number,
    putDelta: number,
    putVolume: number,
    putOpenInterest: number,
    putIv: number,
    isAtm: boolean,
    expirationDate: string,
    callGamma: number,
    callTheta: number,
    callVega: number,
    putGamma: number,
    putTheta: number,
    putVega: number,
  }
}

export class FuturesContract extends jspb.Message {
  getCode(): string;
  setCode(value: string): FuturesContract;

  getDeliveryMonth(): string;
  setDeliveryMonth(value: string): FuturesContract;

  getDaysToExpiry(): number;
  setDaysToExpiry(value: number): FuturesContract;

  getFuturesPrice(): number;
  setFuturesPrice(value: number): FuturesContract;

  getBid(): number;
  setBid(value: number): FuturesContract;

  getAsk(): number;
  setAsk(value: number): FuturesContract;

  getBasis(): number;
  setBasis(value: number): FuturesContract;

  getAnnualizedYield(): number;
  setAnnualizedYield(value: number): FuturesContract;

  getVolume(): number;
  setVolume(value: number): FuturesContract;

  getOpenInterest(): number;
  setOpenInterest(value: number): FuturesContract;

  getState(): string;
  setState(value: string): FuturesContract;

  serializeBinary(): Uint8Array;
  toObject(includeInstance?: boolean): FuturesContract.AsObject;
  static toObject(includeInstance: boolean, msg: FuturesContract): FuturesContract.AsObject;
  static serializeBinaryToWriter(message: FuturesContract, writer: jspb.BinaryWriter): void;
  static deserializeBinary(bytes: Uint8Array): FuturesContract;
  static deserializeBinaryFromReader(message: FuturesContract, reader: jspb.BinaryReader): FuturesContract;
}

export namespace FuturesContract {
  export type AsObject = {
    code: string,
    deliveryMonth: string,
    daysToExpiry: number,
    futuresPrice: number,
    bid: number,
    ask: number,
    basis: number,
    annualizedYield: number,
    volume: number,
    openInterest: number,
    state: string,
  }
}

export class ExpirationDate extends jspb.Message {
  getDateStr(): string;
  setDateStr(value: string): ExpirationDate;

  getDaysToExpiry(): number;
  setDaysToExpiry(value: number): ExpirationDate;

  getLabel(): string;
  setLabel(value: string): ExpirationDate;

  serializeBinary(): Uint8Array;
  toObject(includeInstance?: boolean): ExpirationDate.AsObject;
  static toObject(includeInstance: boolean, msg: ExpirationDate): ExpirationDate.AsObject;
  static serializeBinaryToWriter(message: ExpirationDate, writer: jspb.BinaryWriter): void;
  static deserializeBinary(bytes: Uint8Array): ExpirationDate;
  static deserializeBinaryFromReader(message: ExpirationDate, reader: jspb.BinaryReader): ExpirationDate;
}

export namespace ExpirationDate {
  export type AsObject = {
    dateStr: string,
    daysToExpiry: number,
    label: string,
  }
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
  toObject(includeInstance?: boolean): ChainRequest.AsObject;
  static toObject(includeInstance: boolean, msg: ChainRequest): ChainRequest.AsObject;
  static serializeBinaryToWriter(message: ChainRequest, writer: jspb.BinaryWriter): void;
  static deserializeBinary(bytes: Uint8Array): ChainRequest;
  static deserializeBinaryFromReader(message: ChainRequest, reader: jspb.BinaryReader): ChainRequest;
}

export namespace ChainRequest {
  export type AsObject = {
    symbol: string,
    expirationDays: number,
    assetClass: string,
    expirationDate: string,
  }
}

export class ChainResponse extends jspb.Message {
  getSymbol(): string;
  setSymbol(value: string): ChainResponse;

  getSpotPrice(): number;
  setSpotPrice(value: number): ChainResponse;

  getOptionStrikesList(): Array<OptionStrike>;
  setOptionStrikesList(value: Array<OptionStrike>): ChainResponse;
  clearOptionStrikesList(): ChainResponse;
  addOptionStrikes(value?: OptionStrike, index?: number): OptionStrike;

  getFuturesContractsList(): Array<FuturesContract>;
  setFuturesContractsList(value: Array<FuturesContract>): ChainResponse;
  clearFuturesContractsList(): ChainResponse;
  addFuturesContracts(value?: FuturesContract, index?: number): FuturesContract;

  getSelectedExpirationDate(): string;
  setSelectedExpirationDate(value: string): ChainResponse;

  getAvailableExpirationsList(): Array<ExpirationDate>;
  setAvailableExpirationsList(value: Array<ExpirationDate>): ChainResponse;
  clearAvailableExpirationsList(): ChainResponse;
  addAvailableExpirations(value?: ExpirationDate, index?: number): ExpirationDate;

  getProvider(): string;
  setProvider(value: string): ChainResponse;

  serializeBinary(): Uint8Array;
  toObject(includeInstance?: boolean): ChainResponse.AsObject;
  static toObject(includeInstance: boolean, msg: ChainResponse): ChainResponse.AsObject;
  static serializeBinaryToWriter(message: ChainResponse, writer: jspb.BinaryWriter): void;
  static deserializeBinary(bytes: Uint8Array): ChainResponse;
  static deserializeBinaryFromReader(message: ChainResponse, reader: jspb.BinaryReader): ChainResponse;
}

export namespace ChainResponse {
  export type AsObject = {
    symbol: string,
    spotPrice: number,
    optionStrikesList: Array<OptionStrike.AsObject>,
    futuresContractsList: Array<FuturesContract.AsObject>,
    selectedExpirationDate: string,
    availableExpirationsList: Array<ExpirationDate.AsObject>,
    provider: string,
  }
}

export class RiskFreeRateRequest extends jspb.Message {
  serializeBinary(): Uint8Array;
  toObject(includeInstance?: boolean): RiskFreeRateRequest.AsObject;
  static toObject(includeInstance: boolean, msg: RiskFreeRateRequest): RiskFreeRateRequest.AsObject;
  static serializeBinaryToWriter(message: RiskFreeRateRequest, writer: jspb.BinaryWriter): void;
  static deserializeBinary(bytes: Uint8Array): RiskFreeRateRequest;
  static deserializeBinaryFromReader(message: RiskFreeRateRequest, reader: jspb.BinaryReader): RiskFreeRateRequest;
}

export namespace RiskFreeRateRequest {
  export type AsObject = {
  }
}

export class RatePoint extends jspb.Message {
  getTenor(): string;
  setTenor(value: string): RatePoint;

  getDays(): number;
  setDays(value: number): RatePoint;

  getRateBey(): number;
  setRateBey(value: number): RatePoint;

  getRateContinuous(): number;
  setRateContinuous(value: number): RatePoint;

  serializeBinary(): Uint8Array;
  toObject(includeInstance?: boolean): RatePoint.AsObject;
  static toObject(includeInstance: boolean, msg: RatePoint): RatePoint.AsObject;
  static serializeBinaryToWriter(message: RatePoint, writer: jspb.BinaryWriter): void;
  static deserializeBinary(bytes: Uint8Array): RatePoint;
  static deserializeBinaryFromReader(message: RatePoint, reader: jspb.BinaryReader): RatePoint;
}

export namespace RatePoint {
  export type AsObject = {
    tenor: string,
    days: number,
    rateBey: number,
    rateContinuous: number,
  }
}

export class RiskFreeRateResponse extends jspb.Message {
  getRate(): number;
  setRate(value: number): RiskFreeRateResponse;

  getRatePublished(): number;
  setRatePublished(value: number): RiskFreeRateResponse;

  getTenor(): string;
  setTenor(value: string): RiskFreeRateResponse;

  getAsOfDate(): string;
  setAsOfDate(value: string): RiskFreeRateResponse;

  getSource(): string;
  setSource(value: string): RiskFreeRateResponse;

  getCurveList(): Array<RatePoint>;
  setCurveList(value: Array<RatePoint>): RiskFreeRateResponse;
  clearCurveList(): RiskFreeRateResponse;
  addCurve(value?: RatePoint, index?: number): RatePoint;

  getFetchedAt(): string;
  setFetchedAt(value: string): RiskFreeRateResponse;

  serializeBinary(): Uint8Array;
  toObject(includeInstance?: boolean): RiskFreeRateResponse.AsObject;
  static toObject(includeInstance: boolean, msg: RiskFreeRateResponse): RiskFreeRateResponse.AsObject;
  static serializeBinaryToWriter(message: RiskFreeRateResponse, writer: jspb.BinaryWriter): void;
  static deserializeBinary(bytes: Uint8Array): RiskFreeRateResponse;
  static deserializeBinaryFromReader(message: RiskFreeRateResponse, reader: jspb.BinaryReader): RiskFreeRateResponse;
}

export namespace RiskFreeRateResponse {
  export type AsObject = {
    rate: number,
    ratePublished: number,
    tenor: string,
    asOfDate: string,
    source: string,
    curveList: Array<RatePoint.AsObject>,
    fetchedAt: string,
  }
}

