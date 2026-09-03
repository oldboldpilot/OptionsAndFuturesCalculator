import * as jspb from 'google-protobuf'



export class DecimalResponse extends jspb.Message {
  getValue(): string;
  setValue(value: string): DecimalResponse;

  serializeBinary(): Uint8Array;
  toObject(includeInstance?: boolean): DecimalResponse.AsObject;
  static toObject(includeInstance: boolean, msg: DecimalResponse): DecimalResponse.AsObject;
  static serializeBinaryToWriter(message: DecimalResponse, writer: jspb.BinaryWriter): void;
  static deserializeBinary(bytes: Uint8Array): DecimalResponse;
  static deserializeBinaryFromReader(message: DecimalResponse, reader: jspb.BinaryReader): DecimalResponse;
}

export namespace DecimalResponse {
  export type AsObject = {
    value: string,
  }
}

export class DoubleResponse extends jspb.Message {
  getValue(): number;
  setValue(value: number): DoubleResponse;

  serializeBinary(): Uint8Array;
  toObject(includeInstance?: boolean): DoubleResponse.AsObject;
  static toObject(includeInstance: boolean, msg: DoubleResponse): DoubleResponse.AsObject;
  static serializeBinaryToWriter(message: DoubleResponse, writer: jspb.BinaryWriter): void;
  static deserializeBinary(bytes: Uint8Array): DoubleResponse;
  static deserializeBinaryFromReader(message: DoubleResponse, reader: jspb.BinaryReader): DoubleResponse;
}

export namespace DoubleResponse {
  export type AsObject = {
    value: number,
  }
}

export class PaymentRequest extends jspb.Message {
  getRate(): string;
  setRate(value: string): PaymentRequest;

  getPeriods(): number;
  setPeriods(value: number): PaymentRequest;

  getPresentValue(): string;
  setPresentValue(value: string): PaymentRequest;

  getFutureValue(): string;
  setFutureValue(value: string): PaymentRequest;

  getTiming(): AnnuityTiming;
  setTiming(value: AnnuityTiming): PaymentRequest;

  serializeBinary(): Uint8Array;
  toObject(includeInstance?: boolean): PaymentRequest.AsObject;
  static toObject(includeInstance: boolean, msg: PaymentRequest): PaymentRequest.AsObject;
  static serializeBinaryToWriter(message: PaymentRequest, writer: jspb.BinaryWriter): void;
  static deserializeBinary(bytes: Uint8Array): PaymentRequest;
  static deserializeBinaryFromReader(message: PaymentRequest, reader: jspb.BinaryReader): PaymentRequest;
}

export namespace PaymentRequest {
  export type AsObject = {
    rate: string,
    periods: number,
    presentValue: string,
    futureValue: string,
    timing: AnnuityTiming,
  }
}

export class PresentValueRequest extends jspb.Message {
  getRate(): string;
  setRate(value: string): PresentValueRequest;

  getPeriods(): number;
  setPeriods(value: number): PresentValueRequest;

  getPayment(): string;
  setPayment(value: string): PresentValueRequest;

  getFutureValue(): string;
  setFutureValue(value: string): PresentValueRequest;

  getTiming(): AnnuityTiming;
  setTiming(value: AnnuityTiming): PresentValueRequest;

  serializeBinary(): Uint8Array;
  toObject(includeInstance?: boolean): PresentValueRequest.AsObject;
  static toObject(includeInstance: boolean, msg: PresentValueRequest): PresentValueRequest.AsObject;
  static serializeBinaryToWriter(message: PresentValueRequest, writer: jspb.BinaryWriter): void;
  static deserializeBinary(bytes: Uint8Array): PresentValueRequest;
  static deserializeBinaryFromReader(message: PresentValueRequest, reader: jspb.BinaryReader): PresentValueRequest;
}

export namespace PresentValueRequest {
  export type AsObject = {
    rate: string,
    periods: number,
    payment: string,
    futureValue: string,
    timing: AnnuityTiming,
  }
}

export class FutureValueRequest extends jspb.Message {
  getRate(): string;
  setRate(value: string): FutureValueRequest;

  getPeriods(): number;
  setPeriods(value: number): FutureValueRequest;

  getPayment(): string;
  setPayment(value: string): FutureValueRequest;

  getPresentValue(): string;
  setPresentValue(value: string): FutureValueRequest;

  getTiming(): AnnuityTiming;
  setTiming(value: AnnuityTiming): FutureValueRequest;

  serializeBinary(): Uint8Array;
  toObject(includeInstance?: boolean): FutureValueRequest.AsObject;
  static toObject(includeInstance: boolean, msg: FutureValueRequest): FutureValueRequest.AsObject;
  static serializeBinaryToWriter(message: FutureValueRequest, writer: jspb.BinaryWriter): void;
  static deserializeBinary(bytes: Uint8Array): FutureValueRequest;
  static deserializeBinaryFromReader(message: FutureValueRequest, reader: jspb.BinaryReader): FutureValueRequest;
}

export namespace FutureValueRequest {
  export type AsObject = {
    rate: string,
    periods: number,
    payment: string,
    presentValue: string,
    timing: AnnuityTiming,
  }
}

export class FutureValueDetailedRequest extends jspb.Message {
  getAnnualRate(): string;
  setAnnualRate(value: string): FutureValueDetailedRequest;

  getYears(): number;
  setYears(value: number): FutureValueDetailedRequest;

  getAnnualContribution(): string;
  setAnnualContribution(value: string): FutureValueDetailedRequest;

  getCurrentPrincipal(): string;
  setCurrentPrincipal(value: string): FutureValueDetailedRequest;

  getAnnualInflationRate(): string;
  setAnnualInflationRate(value: string): FutureValueDetailedRequest;

  getCompoundFrequency(): number;
  setCompoundFrequency(value: number): FutureValueDetailedRequest;

  serializeBinary(): Uint8Array;
  toObject(includeInstance?: boolean): FutureValueDetailedRequest.AsObject;
  static toObject(includeInstance: boolean, msg: FutureValueDetailedRequest): FutureValueDetailedRequest.AsObject;
  static serializeBinaryToWriter(message: FutureValueDetailedRequest, writer: jspb.BinaryWriter): void;
  static deserializeBinary(bytes: Uint8Array): FutureValueDetailedRequest;
  static deserializeBinaryFromReader(message: FutureValueDetailedRequest, reader: jspb.BinaryReader): FutureValueDetailedRequest;
}

export namespace FutureValueDetailedRequest {
  export type AsObject = {
    annualRate: string,
    years: number,
    annualContribution: string,
    currentPrincipal: string,
    annualInflationRate: string,
    compoundFrequency: number,
  }
}

export class FutureValueDetailedResponse extends jspb.Message {
  getNominalFv(): string;
  setNominalFv(value: string): FutureValueDetailedResponse;

  getInflationAdjustedFv(): string;
  setInflationAdjustedFv(value: string): FutureValueDetailedResponse;

  getTotalContributions(): string;
  setTotalContributions(value: string): FutureValueDetailedResponse;

  getTotalInterestEarned(): string;
  setTotalInterestEarned(value: string): FutureValueDetailedResponse;

  serializeBinary(): Uint8Array;
  toObject(includeInstance?: boolean): FutureValueDetailedResponse.AsObject;
  static toObject(includeInstance: boolean, msg: FutureValueDetailedResponse): FutureValueDetailedResponse.AsObject;
  static serializeBinaryToWriter(message: FutureValueDetailedResponse, writer: jspb.BinaryWriter): void;
  static deserializeBinary(bytes: Uint8Array): FutureValueDetailedResponse;
  static deserializeBinaryFromReader(message: FutureValueDetailedResponse, reader: jspb.BinaryReader): FutureValueDetailedResponse;
}

export namespace FutureValueDetailedResponse {
  export type AsObject = {
    nominalFv: string,
    inflationAdjustedFv: string,
    totalContributions: string,
    totalInterestEarned: string,
  }
}

export class PeriodPaymentRequest extends jspb.Message {
  getRate(): string;
  setRate(value: string): PeriodPaymentRequest;

  getPeriod(): number;
  setPeriod(value: number): PeriodPaymentRequest;

  getPeriods(): number;
  setPeriods(value: number): PeriodPaymentRequest;

  getPresentValue(): string;
  setPresentValue(value: string): PeriodPaymentRequest;

  getFutureValue(): string;
  setFutureValue(value: string): PeriodPaymentRequest;

  getTiming(): AnnuityTiming;
  setTiming(value: AnnuityTiming): PeriodPaymentRequest;

  serializeBinary(): Uint8Array;
  toObject(includeInstance?: boolean): PeriodPaymentRequest.AsObject;
  static toObject(includeInstance: boolean, msg: PeriodPaymentRequest): PeriodPaymentRequest.AsObject;
  static serializeBinaryToWriter(message: PeriodPaymentRequest, writer: jspb.BinaryWriter): void;
  static deserializeBinary(bytes: Uint8Array): PeriodPaymentRequest;
  static deserializeBinaryFromReader(message: PeriodPaymentRequest, reader: jspb.BinaryReader): PeriodPaymentRequest;
}

export namespace PeriodPaymentRequest {
  export type AsObject = {
    rate: string,
    period: number,
    periods: number,
    presentValue: string,
    futureValue: string,
    timing: AnnuityTiming,
  }
}

export class RateRequest extends jspb.Message {
  getPeriods(): number;
  setPeriods(value: number): RateRequest;

  getPayment(): string;
  setPayment(value: string): RateRequest;

  getPresentValue(): string;
  setPresentValue(value: string): RateRequest;

  getFutureValue(): string;
  setFutureValue(value: string): RateRequest;

  getTiming(): AnnuityTiming;
  setTiming(value: AnnuityTiming): RateRequest;

  getGuess(): string;
  setGuess(value: string): RateRequest;

  serializeBinary(): Uint8Array;
  toObject(includeInstance?: boolean): RateRequest.AsObject;
  static toObject(includeInstance: boolean, msg: RateRequest): RateRequest.AsObject;
  static serializeBinaryToWriter(message: RateRequest, writer: jspb.BinaryWriter): void;
  static deserializeBinary(bytes: Uint8Array): RateRequest;
  static deserializeBinaryFromReader(message: RateRequest, reader: jspb.BinaryReader): RateRequest;
}

export namespace RateRequest {
  export type AsObject = {
    periods: number,
    payment: string,
    presentValue: string,
    futureValue: string,
    timing: AnnuityTiming,
    guess: string,
  }
}

export class PeriodsRequest extends jspb.Message {
  getRate(): string;
  setRate(value: string): PeriodsRequest;

  getPayment(): string;
  setPayment(value: string): PeriodsRequest;

  getPresentValue(): string;
  setPresentValue(value: string): PeriodsRequest;

  getFutureValue(): string;
  setFutureValue(value: string): PeriodsRequest;

  getTiming(): AnnuityTiming;
  setTiming(value: AnnuityTiming): PeriodsRequest;

  serializeBinary(): Uint8Array;
  toObject(includeInstance?: boolean): PeriodsRequest.AsObject;
  static toObject(includeInstance: boolean, msg: PeriodsRequest): PeriodsRequest.AsObject;
  static serializeBinaryToWriter(message: PeriodsRequest, writer: jspb.BinaryWriter): void;
  static deserializeBinary(bytes: Uint8Array): PeriodsRequest;
  static deserializeBinaryFromReader(message: PeriodsRequest, reader: jspb.BinaryReader): PeriodsRequest;
}

export namespace PeriodsRequest {
  export type AsObject = {
    rate: string,
    payment: string,
    presentValue: string,
    futureValue: string,
    timing: AnnuityTiming,
  }
}

export class RateConversionRequest extends jspb.Message {
  getDirection(): RateConversionRequest.Direction;
  setDirection(value: RateConversionRequest.Direction): RateConversionRequest;

  getRate(): number;
  setRate(value: number): RateConversionRequest;

  getPeriodsPerYear(): number;
  setPeriodsPerYear(value: number): RateConversionRequest;

  serializeBinary(): Uint8Array;
  toObject(includeInstance?: boolean): RateConversionRequest.AsObject;
  static toObject(includeInstance: boolean, msg: RateConversionRequest): RateConversionRequest.AsObject;
  static serializeBinaryToWriter(message: RateConversionRequest, writer: jspb.BinaryWriter): void;
  static deserializeBinary(bytes: Uint8Array): RateConversionRequest;
  static deserializeBinaryFromReader(message: RateConversionRequest, reader: jspb.BinaryReader): RateConversionRequest;
}

export namespace RateConversionRequest {
  export type AsObject = {
    direction: RateConversionRequest.Direction,
    rate: number,
    periodsPerYear: number,
  }

  export enum Direction { 
    NOMINAL_TO_EFFECTIVE = 0,
    EFFECTIVE_TO_NOMINAL = 1,
  }
}

export class FisherRequest extends jspb.Message {
  getDirection(): FisherRequest.Direction;
  setDirection(value: FisherRequest.Direction): FisherRequest;

  getRate(): number;
  setRate(value: number): FisherRequest;

  getInflationRate(): number;
  setInflationRate(value: number): FisherRequest;

  serializeBinary(): Uint8Array;
  toObject(includeInstance?: boolean): FisherRequest.AsObject;
  static toObject(includeInstance: boolean, msg: FisherRequest): FisherRequest.AsObject;
  static serializeBinaryToWriter(message: FisherRequest, writer: jspb.BinaryWriter): void;
  static deserializeBinary(bytes: Uint8Array): FisherRequest;
  static deserializeBinaryFromReader(message: FisherRequest, reader: jspb.BinaryReader): FisherRequest;
}

export namespace FisherRequest {
  export type AsObject = {
    direction: FisherRequest.Direction,
    rate: number,
    inflationRate: number,
  }

  export enum Direction { 
    NOMINAL_TO_REAL = 0,
    REAL_TO_NOMINAL = 1,
  }
}

export class AmortizationRequest extends jspb.Message {
  getLoanAmount(): string;
  setLoanAmount(value: string): AmortizationRequest;

  getAnnualRate(): string;
  setAnnualRate(value: string): AmortizationRequest;

  getTermMonths(): number;
  setTermMonths(value: number): AmortizationRequest;

  getMonthlyOverpayment(): string;
  setMonthlyOverpayment(value: string): AmortizationRequest;

  getPmiAnnualRate(): string;
  setPmiAnnualRate(value: string): AmortizationRequest;

  getOriginalHomeValue(): string;
  setOriginalHomeValue(value: string): AmortizationRequest;

  serializeBinary(): Uint8Array;
  toObject(includeInstance?: boolean): AmortizationRequest.AsObject;
  static toObject(includeInstance: boolean, msg: AmortizationRequest): AmortizationRequest.AsObject;
  static serializeBinaryToWriter(message: AmortizationRequest, writer: jspb.BinaryWriter): void;
  static deserializeBinary(bytes: Uint8Array): AmortizationRequest;
  static deserializeBinaryFromReader(message: AmortizationRequest, reader: jspb.BinaryReader): AmortizationRequest;
}

export namespace AmortizationRequest {
  export type AsObject = {
    loanAmount: string,
    annualRate: string,
    termMonths: number,
    monthlyOverpayment: string,
    pmiAnnualRate: string,
    originalHomeValue: string,
  }
}

export class AmortizationRow extends jspb.Message {
  getPeriod(): number;
  setPeriod(value: number): AmortizationRow;

  getStartBalance(): string;
  setStartBalance(value: string): AmortizationRow;

  getScheduledPayment(): string;
  setScheduledPayment(value: string): AmortizationRow;

  getExtraPayment(): string;
  setExtraPayment(value: string): AmortizationRow;

  getInterestPaid(): string;
  setInterestPaid(value: string): AmortizationRow;

  getPrincipalPaid(): string;
  setPrincipalPaid(value: string): AmortizationRow;

  getPmiPaid(): string;
  setPmiPaid(value: string): AmortizationRow;

  getEndBalance(): string;
  setEndBalance(value: string): AmortizationRow;

  serializeBinary(): Uint8Array;
  toObject(includeInstance?: boolean): AmortizationRow.AsObject;
  static toObject(includeInstance: boolean, msg: AmortizationRow): AmortizationRow.AsObject;
  static serializeBinaryToWriter(message: AmortizationRow, writer: jspb.BinaryWriter): void;
  static deserializeBinary(bytes: Uint8Array): AmortizationRow;
  static deserializeBinaryFromReader(message: AmortizationRow, reader: jspb.BinaryReader): AmortizationRow;
}

export namespace AmortizationRow {
  export type AsObject = {
    period: number,
    startBalance: string,
    scheduledPayment: string,
    extraPayment: string,
    interestPaid: string,
    principalPaid: string,
    pmiPaid: string,
    endBalance: string,
  }
}

export class MortgageSummary extends jspb.Message {
  getTotalPrincipalPaid(): string;
  setTotalPrincipalPaid(value: string): MortgageSummary;

  getTotalInterestPaid(): string;
  setTotalInterestPaid(value: string): MortgageSummary;

  getTotalPmiPaid(): string;
  setTotalPmiPaid(value: string): MortgageSummary;

  getTotalPaymentsPaid(): string;
  setTotalPaymentsPaid(value: string): MortgageSummary;

  getActualTermMonths(): number;
  setActualTermMonths(value: number): MortgageSummary;

  serializeBinary(): Uint8Array;
  toObject(includeInstance?: boolean): MortgageSummary.AsObject;
  static toObject(includeInstance: boolean, msg: MortgageSummary): MortgageSummary.AsObject;
  static serializeBinaryToWriter(message: MortgageSummary, writer: jspb.BinaryWriter): void;
  static deserializeBinary(bytes: Uint8Array): MortgageSummary;
  static deserializeBinaryFromReader(message: MortgageSummary, reader: jspb.BinaryReader): MortgageSummary;
}

export namespace MortgageSummary {
  export type AsObject = {
    totalPrincipalPaid: string,
    totalInterestPaid: string,
    totalPmiPaid: string,
    totalPaymentsPaid: string,
    actualTermMonths: number,
  }
}

export class AmortizationResponse extends jspb.Message {
  getScheduleList(): Array<AmortizationRow>;
  setScheduleList(value: Array<AmortizationRow>): AmortizationResponse;
  clearScheduleList(): AmortizationResponse;
  addSchedule(value?: AmortizationRow, index?: number): AmortizationRow;

  getSummary(): MortgageSummary | undefined;
  setSummary(value?: MortgageSummary): AmortizationResponse;
  hasSummary(): boolean;
  clearSummary(): AmortizationResponse;

  serializeBinary(): Uint8Array;
  toObject(includeInstance?: boolean): AmortizationResponse.AsObject;
  static toObject(includeInstance: boolean, msg: AmortizationResponse): AmortizationResponse.AsObject;
  static serializeBinaryToWriter(message: AmortizationResponse, writer: jspb.BinaryWriter): void;
  static deserializeBinary(bytes: Uint8Array): AmortizationResponse;
  static deserializeBinaryFromReader(message: AmortizationResponse, reader: jspb.BinaryReader): AmortizationResponse;
}

export namespace AmortizationResponse {
  export type AsObject = {
    scheduleList: Array<AmortizationRow.AsObject>,
    summary?: MortgageSummary.AsObject,
  }
}

export class DetailedAmortizationRequest extends jspb.Message {
  getLoanAmount(): string;
  setLoanAmount(value: string): DetailedAmortizationRequest;

  getAnnualRate(): string;
  setAnnualRate(value: string): DetailedAmortizationRequest;

  getTermMonths(): number;
  setTermMonths(value: number): DetailedAmortizationRequest;

  getMonthlyOverpayment(): string;
  setMonthlyOverpayment(value: string): DetailedAmortizationRequest;

  getPmiAnnualRate(): string;
  setPmiAnnualRate(value: string): DetailedAmortizationRequest;

  getOriginalHomeValue(): string;
  setOriginalHomeValue(value: string): DetailedAmortizationRequest;

  getAnnualTaxRate(): string;
  setAnnualTaxRate(value: string): DetailedAmortizationRequest;

  serializeBinary(): Uint8Array;
  toObject(includeInstance?: boolean): DetailedAmortizationRequest.AsObject;
  static toObject(includeInstance: boolean, msg: DetailedAmortizationRequest): DetailedAmortizationRequest.AsObject;
  static serializeBinaryToWriter(message: DetailedAmortizationRequest, writer: jspb.BinaryWriter): void;
  static deserializeBinary(bytes: Uint8Array): DetailedAmortizationRequest;
  static deserializeBinaryFromReader(message: DetailedAmortizationRequest, reader: jspb.BinaryReader): DetailedAmortizationRequest;
}

export namespace DetailedAmortizationRequest {
  export type AsObject = {
    loanAmount: string,
    annualRate: string,
    termMonths: number,
    monthlyOverpayment: string,
    pmiAnnualRate: string,
    originalHomeValue: string,
    annualTaxRate: string,
  }
}

export class DetailedAmortizationRow extends jspb.Message {
  getPeriod(): number;
  setPeriod(value: number): DetailedAmortizationRow;

  getStartBalance(): string;
  setStartBalance(value: string): DetailedAmortizationRow;

  getScheduledPayment(): string;
  setScheduledPayment(value: string): DetailedAmortizationRow;

  getExtraPayment(): string;
  setExtraPayment(value: string): DetailedAmortizationRow;

  getInterestPaid(): string;
  setInterestPaid(value: string): DetailedAmortizationRow;

  getPrincipalPaid(): string;
  setPrincipalPaid(value: string): DetailedAmortizationRow;

  getPmiPaid(): string;
  setPmiPaid(value: string): DetailedAmortizationRow;

  getTaxSavings(): string;
  setTaxSavings(value: string): DetailedAmortizationRow;

  getEndBalance(): string;
  setEndBalance(value: string): DetailedAmortizationRow;

  serializeBinary(): Uint8Array;
  toObject(includeInstance?: boolean): DetailedAmortizationRow.AsObject;
  static toObject(includeInstance: boolean, msg: DetailedAmortizationRow): DetailedAmortizationRow.AsObject;
  static serializeBinaryToWriter(message: DetailedAmortizationRow, writer: jspb.BinaryWriter): void;
  static deserializeBinary(bytes: Uint8Array): DetailedAmortizationRow;
  static deserializeBinaryFromReader(message: DetailedAmortizationRow, reader: jspb.BinaryReader): DetailedAmortizationRow;
}

export namespace DetailedAmortizationRow {
  export type AsObject = {
    period: number,
    startBalance: string,
    scheduledPayment: string,
    extraPayment: string,
    interestPaid: string,
    principalPaid: string,
    pmiPaid: string,
    taxSavings: string,
    endBalance: string,
  }
}

export class DetailedMortgageSummary extends jspb.Message {
  getTotalPrincipalPaid(): string;
  setTotalPrincipalPaid(value: string): DetailedMortgageSummary;

  getTotalInterestPaid(): string;
  setTotalInterestPaid(value: string): DetailedMortgageSummary;

  getTotalPmiPaid(): string;
  setTotalPmiPaid(value: string): DetailedMortgageSummary;

  getTotalPaymentsPaid(): string;
  setTotalPaymentsPaid(value: string): DetailedMortgageSummary;

  getTotalTaxSavings(): string;
  setTotalTaxSavings(value: string): DetailedMortgageSummary;

  getActualTermMonths(): number;
  setActualTermMonths(value: number): DetailedMortgageSummary;

  serializeBinary(): Uint8Array;
  toObject(includeInstance?: boolean): DetailedMortgageSummary.AsObject;
  static toObject(includeInstance: boolean, msg: DetailedMortgageSummary): DetailedMortgageSummary.AsObject;
  static serializeBinaryToWriter(message: DetailedMortgageSummary, writer: jspb.BinaryWriter): void;
  static deserializeBinary(bytes: Uint8Array): DetailedMortgageSummary;
  static deserializeBinaryFromReader(message: DetailedMortgageSummary, reader: jspb.BinaryReader): DetailedMortgageSummary;
}

export namespace DetailedMortgageSummary {
  export type AsObject = {
    totalPrincipalPaid: string,
    totalInterestPaid: string,
    totalPmiPaid: string,
    totalPaymentsPaid: string,
    totalTaxSavings: string,
    actualTermMonths: number,
  }
}

export class DetailedAmortizationResponse extends jspb.Message {
  getScheduleList(): Array<DetailedAmortizationRow>;
  setScheduleList(value: Array<DetailedAmortizationRow>): DetailedAmortizationResponse;
  clearScheduleList(): DetailedAmortizationResponse;
  addSchedule(value?: DetailedAmortizationRow, index?: number): DetailedAmortizationRow;

  getSummary(): DetailedMortgageSummary | undefined;
  setSummary(value?: DetailedMortgageSummary): DetailedAmortizationResponse;
  hasSummary(): boolean;
  clearSummary(): DetailedAmortizationResponse;

  serializeBinary(): Uint8Array;
  toObject(includeInstance?: boolean): DetailedAmortizationResponse.AsObject;
  static toObject(includeInstance: boolean, msg: DetailedAmortizationResponse): DetailedAmortizationResponse.AsObject;
  static serializeBinaryToWriter(message: DetailedAmortizationResponse, writer: jspb.BinaryWriter): void;
  static deserializeBinary(bytes: Uint8Array): DetailedAmortizationResponse;
  static deserializeBinaryFromReader(message: DetailedAmortizationResponse, reader: jspb.BinaryReader): DetailedAmortizationResponse;
}

export namespace DetailedAmortizationResponse {
  export type AsObject = {
    scheduleList: Array<DetailedAmortizationRow.AsObject>,
    summary?: DetailedMortgageSummary.AsObject,
  }
}

export class AmortizationBatchRequest extends jspb.Message {
  getLoanAmountsList(): Array<number>;
  setLoanAmountsList(value: Array<number>): AmortizationBatchRequest;
  clearLoanAmountsList(): AmortizationBatchRequest;
  addLoanAmounts(value: number, index?: number): AmortizationBatchRequest;

  getAnnualRatesList(): Array<number>;
  setAnnualRatesList(value: Array<number>): AmortizationBatchRequest;
  clearAnnualRatesList(): AmortizationBatchRequest;
  addAnnualRates(value: number, index?: number): AmortizationBatchRequest;

  getTermMonthsList(): Array<number>;
  setTermMonthsList(value: Array<number>): AmortizationBatchRequest;
  clearTermMonthsList(): AmortizationBatchRequest;
  addTermMonths(value: number, index?: number): AmortizationBatchRequest;

  getExtraPaymentsList(): Array<number>;
  setExtraPaymentsList(value: Array<number>): AmortizationBatchRequest;
  clearExtraPaymentsList(): AmortizationBatchRequest;
  addExtraPayments(value: number, index?: number): AmortizationBatchRequest;

  getPmiRatesList(): Array<number>;
  setPmiRatesList(value: Array<number>): AmortizationBatchRequest;
  clearPmiRatesList(): AmortizationBatchRequest;
  addPmiRates(value: number, index?: number): AmortizationBatchRequest;

  getHomeValuesList(): Array<number>;
  setHomeValuesList(value: Array<number>): AmortizationBatchRequest;
  clearHomeValuesList(): AmortizationBatchRequest;
  addHomeValues(value: number, index?: number): AmortizationBatchRequest;

  serializeBinary(): Uint8Array;
  toObject(includeInstance?: boolean): AmortizationBatchRequest.AsObject;
  static toObject(includeInstance: boolean, msg: AmortizationBatchRequest): AmortizationBatchRequest.AsObject;
  static serializeBinaryToWriter(message: AmortizationBatchRequest, writer: jspb.BinaryWriter): void;
  static deserializeBinary(bytes: Uint8Array): AmortizationBatchRequest;
  static deserializeBinaryFromReader(message: AmortizationBatchRequest, reader: jspb.BinaryReader): AmortizationBatchRequest;
}

export namespace AmortizationBatchRequest {
  export type AsObject = {
    loanAmountsList: Array<number>,
    annualRatesList: Array<number>,
    termMonthsList: Array<number>,
    extraPaymentsList: Array<number>,
    pmiRatesList: Array<number>,
    homeValuesList: Array<number>,
  }
}

export class AmortizationBatchResponse extends jspb.Message {
  getSummariesList(): Array<MortgageSummary>;
  setSummariesList(value: Array<MortgageSummary>): AmortizationBatchResponse;
  clearSummariesList(): AmortizationBatchResponse;
  addSummaries(value?: MortgageSummary, index?: number): MortgageSummary;

  serializeBinary(): Uint8Array;
  toObject(includeInstance?: boolean): AmortizationBatchResponse.AsObject;
  static toObject(includeInstance: boolean, msg: AmortizationBatchResponse): AmortizationBatchResponse.AsObject;
  static serializeBinaryToWriter(message: AmortizationBatchResponse, writer: jspb.BinaryWriter): void;
  static deserializeBinary(bytes: Uint8Array): AmortizationBatchResponse;
  static deserializeBinaryFromReader(message: AmortizationBatchResponse, reader: jspb.BinaryReader): AmortizationBatchResponse;
}

export namespace AmortizationBatchResponse {
  export type AsObject = {
    summariesList: Array<MortgageSummary.AsObject>,
  }
}

export class HelocRequest extends jspb.Message {
  getHomeValue(): string;
  setHomeValue(value: string): HelocRequest;

  getCurrentMortgageBalance(): string;
  setCurrentMortgageBalance(value: string): HelocRequest;

  getMaxLtvRate(): string;
  setMaxLtvRate(value: string): HelocRequest;

  getDrawnAmount(): string;
  setDrawnAmount(value: string): HelocRequest;

  getAnnualRate(): string;
  setAnnualRate(value: string): HelocRequest;

  getRepaymentTermYears(): number;
  setRepaymentTermYears(value: number): HelocRequest;

  getPaymentsPerYear(): number;
  setPaymentsPerYear(value: number): HelocRequest;

  serializeBinary(): Uint8Array;
  toObject(includeInstance?: boolean): HelocRequest.AsObject;
  static toObject(includeInstance: boolean, msg: HelocRequest): HelocRequest.AsObject;
  static serializeBinaryToWriter(message: HelocRequest, writer: jspb.BinaryWriter): void;
  static deserializeBinary(bytes: Uint8Array): HelocRequest;
  static deserializeBinaryFromReader(message: HelocRequest, reader: jspb.BinaryReader): HelocRequest;
}

export namespace HelocRequest {
  export type AsObject = {
    homeValue: string,
    currentMortgageBalance: string,
    maxLtvRate: string,
    drawnAmount: string,
    annualRate: string,
    repaymentTermYears: number,
    paymentsPerYear: number,
  }
}

export class HelocResponse extends jspb.Message {
  getAvailableEquity(): string;
  setAvailableEquity(value: string): HelocResponse;

  getDrawPeriodPayment(): string;
  setDrawPeriodPayment(value: string): HelocResponse;

  getRepaymentPeriodPayment(): string;
  setRepaymentPeriodPayment(value: string): HelocResponse;

  serializeBinary(): Uint8Array;
  toObject(includeInstance?: boolean): HelocResponse.AsObject;
  static toObject(includeInstance: boolean, msg: HelocResponse): HelocResponse.AsObject;
  static serializeBinaryToWriter(message: HelocResponse, writer: jspb.BinaryWriter): void;
  static deserializeBinary(bytes: Uint8Array): HelocResponse;
  static deserializeBinaryFromReader(message: HelocResponse, reader: jspb.BinaryReader): HelocResponse;
}

export namespace HelocResponse {
  export type AsObject = {
    availableEquity: string,
    drawPeriodPayment: string,
    repaymentPeriodPayment: string,
  }
}

export class RefinanceRequest extends jspb.Message {
  getCurrentLoanBalance(): string;
  setCurrentLoanBalance(value: string): RefinanceRequest;

  getCurrentMonthlyPayment(): string;
  setCurrentMonthlyPayment(value: string): RefinanceRequest;

  getCurrentAnnualRate(): string;
  setCurrentAnnualRate(value: string): RefinanceRequest;

  getCurrentRemainingMonths(): number;
  setCurrentRemainingMonths(value: number): RefinanceRequest;

  getPropertyValue(): string;
  setPropertyValue(value: string): RefinanceRequest;

  getNewAnnualRate(): string;
  setNewAnnualRate(value: string): RefinanceRequest;

  getNewTermYears(): number;
  setNewTermYears(value: number): RefinanceRequest;

  getClosingCosts(): string;
  setClosingCosts(value: string): RefinanceRequest;

  getClosingCostType(): RefinanceRequest.ClosingCostType;
  setClosingCostType(value: RefinanceRequest.ClosingCostType): RefinanceRequest;

  getCashOutAmount(): string;
  setCashOutAmount(value: string): RefinanceRequest;

  getCurrentPmiMonthly(): string;
  setCurrentPmiMonthly(value: string): RefinanceRequest;

  getNewPmiMonthly(): string;
  setNewPmiMonthly(value: string): RefinanceRequest;

  getPmiDropOffLtv(): string;
  setPmiDropOffLtv(value: string): RefinanceRequest;

  getPaymentsPerYear(): number;
  setPaymentsPerYear(value: number): RefinanceRequest;

  serializeBinary(): Uint8Array;
  toObject(includeInstance?: boolean): RefinanceRequest.AsObject;
  static toObject(includeInstance: boolean, msg: RefinanceRequest): RefinanceRequest.AsObject;
  static serializeBinaryToWriter(message: RefinanceRequest, writer: jspb.BinaryWriter): void;
  static deserializeBinary(bytes: Uint8Array): RefinanceRequest;
  static deserializeBinaryFromReader(message: RefinanceRequest, reader: jspb.BinaryReader): RefinanceRequest;
}

export namespace RefinanceRequest {
  export type AsObject = {
    currentLoanBalance: string,
    currentMonthlyPayment: string,
    currentAnnualRate: string,
    currentRemainingMonths: number,
    propertyValue: string,
    newAnnualRate: string,
    newTermYears: number,
    closingCosts: string,
    closingCostType: RefinanceRequest.ClosingCostType,
    cashOutAmount: string,
    currentPmiMonthly: string,
    newPmiMonthly: string,
    pmiDropOffLtv: string,
    paymentsPerYear: number,
  }

  export enum ClosingCostType { 
    PAID_IN_CASH = 0,
    ROLLED_INTO_LOAN = 1,
  }
}

export class RefinanceResponse extends jspb.Message {
  getNewLoanAmount(): string;
  setNewLoanAmount(value: string): RefinanceResponse;

  getNewMonthlyPayment(): string;
  setNewMonthlyPayment(value: string): RefinanceResponse;

  getMonthlySavingsInitial(): string;
  setMonthlySavingsInitial(value: string): RefinanceResponse;

  getCurrentLoanPmiDropOffMonths(): number;
  setCurrentLoanPmiDropOffMonths(value: number): RefinanceResponse;

  getNewLoanPmiDropOffMonths(): number;
  setNewLoanPmiDropOffMonths(value: number): RefinanceResponse;

  getPayoffDateShiftMonths(): number;
  setPayoffDateShiftMonths(value: number): RefinanceResponse;

  getSimpleBreakEvenMonths(): number;
  setSimpleBreakEvenMonths(value: number): RefinanceResponse;

  getCashFlowBreakEvenMonths(): number;
  setCashFlowBreakEvenMonths(value: number): RefinanceResponse;

  getEquityAdjustedBreakEvenMonths(): number;
  setEquityAdjustedBreakEvenMonths(value: number): RefinanceResponse;

  getTotalSavingsOverLife(): number;
  setTotalSavingsOverLife(value: number): RefinanceResponse;

  serializeBinary(): Uint8Array;
  toObject(includeInstance?: boolean): RefinanceResponse.AsObject;
  static toObject(includeInstance: boolean, msg: RefinanceResponse): RefinanceResponse.AsObject;
  static serializeBinaryToWriter(message: RefinanceResponse, writer: jspb.BinaryWriter): void;
  static deserializeBinary(bytes: Uint8Array): RefinanceResponse;
  static deserializeBinaryFromReader(message: RefinanceResponse, reader: jspb.BinaryReader): RefinanceResponse;
}

export namespace RefinanceResponse {
  export type AsObject = {
    newLoanAmount: string,
    newMonthlyPayment: string,
    monthlySavingsInitial: string,
    currentLoanPmiDropOffMonths: number,
    newLoanPmiDropOffMonths: number,
    payoffDateShiftMonths: number,
    simpleBreakEvenMonths: number,
    cashFlowBreakEvenMonths: number,
    equityAdjustedBreakEvenMonths: number,
    totalSavingsOverLife: number,
  }
}

export class PayoffTimingRequest extends jspb.Message {
  getCurrentLoanBalance(): string;
  setCurrentLoanBalance(value: string): PayoffTimingRequest;

  getAnnualRate(): string;
  setAnnualRate(value: string): PayoffTimingRequest;

  getCurrentMonthlyPayment(): string;
  setCurrentMonthlyPayment(value: string): PayoffTimingRequest;

  getExtraMonthlyPayment(): string;
  setExtraMonthlyPayment(value: string): PayoffTimingRequest;

  getPaymentsPerYear(): number;
  setPaymentsPerYear(value: number): PayoffTimingRequest;

  serializeBinary(): Uint8Array;
  toObject(includeInstance?: boolean): PayoffTimingRequest.AsObject;
  static toObject(includeInstance: boolean, msg: PayoffTimingRequest): PayoffTimingRequest.AsObject;
  static serializeBinaryToWriter(message: PayoffTimingRequest, writer: jspb.BinaryWriter): void;
  static deserializeBinary(bytes: Uint8Array): PayoffTimingRequest;
  static deserializeBinaryFromReader(message: PayoffTimingRequest, reader: jspb.BinaryReader): PayoffTimingRequest;
}

export namespace PayoffTimingRequest {
  export type AsObject = {
    currentLoanBalance: string,
    annualRate: string,
    currentMonthlyPayment: string,
    extraMonthlyPayment: string,
    paymentsPerYear: number,
  }
}

export class PayoffTimingResponse extends jspb.Message {
  getOriginalMonthsRemaining(): number;
  setOriginalMonthsRemaining(value: number): PayoffTimingResponse;

  getNewMonthsRemaining(): number;
  setNewMonthsRemaining(value: number): PayoffTimingResponse;

  getMonthsSaved(): number;
  setMonthsSaved(value: number): PayoffTimingResponse;

  getTotalInterestSaved(): string;
  setTotalInterestSaved(value: string): PayoffTimingResponse;

  serializeBinary(): Uint8Array;
  toObject(includeInstance?: boolean): PayoffTimingResponse.AsObject;
  static toObject(includeInstance: boolean, msg: PayoffTimingResponse): PayoffTimingResponse.AsObject;
  static serializeBinaryToWriter(message: PayoffTimingResponse, writer: jspb.BinaryWriter): void;
  static deserializeBinary(bytes: Uint8Array): PayoffTimingResponse;
  static deserializeBinaryFromReader(message: PayoffTimingResponse, reader: jspb.BinaryReader): PayoffTimingResponse;
}

export namespace PayoffTimingResponse {
  export type AsObject = {
    originalMonthsRemaining: number,
    newMonthsRemaining: number,
    monthsSaved: number,
    totalInterestSaved: string,
  }
}

export class MortgageRecastRequest extends jspb.Message {
  getCurrentLoanBalance(): string;
  setCurrentLoanBalance(value: string): MortgageRecastRequest;

  getCurrentMonthlyPayment(): string;
  setCurrentMonthlyPayment(value: string): MortgageRecastRequest;

  getLumpSumPayment(): string;
  setLumpSumPayment(value: string): MortgageRecastRequest;

  getAnnualRate(): string;
  setAnnualRate(value: string): MortgageRecastRequest;

  getRemainingMonths(): number;
  setRemainingMonths(value: number): MortgageRecastRequest;

  getPaymentsPerYear(): number;
  setPaymentsPerYear(value: number): MortgageRecastRequest;

  serializeBinary(): Uint8Array;
  toObject(includeInstance?: boolean): MortgageRecastRequest.AsObject;
  static toObject(includeInstance: boolean, msg: MortgageRecastRequest): MortgageRecastRequest.AsObject;
  static serializeBinaryToWriter(message: MortgageRecastRequest, writer: jspb.BinaryWriter): void;
  static deserializeBinary(bytes: Uint8Array): MortgageRecastRequest;
  static deserializeBinaryFromReader(message: MortgageRecastRequest, reader: jspb.BinaryReader): MortgageRecastRequest;
}

export namespace MortgageRecastRequest {
  export type AsObject = {
    currentLoanBalance: string,
    currentMonthlyPayment: string,
    lumpSumPayment: string,
    annualRate: string,
    remainingMonths: number,
    paymentsPerYear: number,
  }
}

export class MortgageRecastResponse extends jspb.Message {
  getNewMonthlyPayment(): string;
  setNewMonthlyPayment(value: string): MortgageRecastResponse;

  getMonthlySavings(): string;
  setMonthlySavings(value: string): MortgageRecastResponse;

  serializeBinary(): Uint8Array;
  toObject(includeInstance?: boolean): MortgageRecastResponse.AsObject;
  static toObject(includeInstance: boolean, msg: MortgageRecastResponse): MortgageRecastResponse.AsObject;
  static serializeBinaryToWriter(message: MortgageRecastResponse, writer: jspb.BinaryWriter): void;
  static deserializeBinary(bytes: Uint8Array): MortgageRecastResponse;
  static deserializeBinaryFromReader(message: MortgageRecastResponse, reader: jspb.BinaryReader): MortgageRecastResponse;
}

export namespace MortgageRecastResponse {
  export type AsObject = {
    newMonthlyPayment: string,
    monthlySavings: string,
  }
}

export class NpvRequest extends jspb.Message {
  getRate(): number;
  setRate(value: number): NpvRequest;

  getValuesList(): Array<number>;
  setValuesList(value: Array<number>): NpvRequest;
  clearValuesList(): NpvRequest;
  addValues(value: number, index?: number): NpvRequest;

  serializeBinary(): Uint8Array;
  toObject(includeInstance?: boolean): NpvRequest.AsObject;
  static toObject(includeInstance: boolean, msg: NpvRequest): NpvRequest.AsObject;
  static serializeBinaryToWriter(message: NpvRequest, writer: jspb.BinaryWriter): void;
  static deserializeBinary(bytes: Uint8Array): NpvRequest;
  static deserializeBinaryFromReader(message: NpvRequest, reader: jspb.BinaryReader): NpvRequest;
}

export namespace NpvRequest {
  export type AsObject = {
    rate: number,
    valuesList: Array<number>,
  }
}

export class IrrRequest extends jspb.Message {
  getValuesList(): Array<number>;
  setValuesList(value: Array<number>): IrrRequest;
  clearValuesList(): IrrRequest;
  addValues(value: number, index?: number): IrrRequest;

  getGuess(): number;
  setGuess(value: number): IrrRequest;

  serializeBinary(): Uint8Array;
  toObject(includeInstance?: boolean): IrrRequest.AsObject;
  static toObject(includeInstance: boolean, msg: IrrRequest): IrrRequest.AsObject;
  static serializeBinaryToWriter(message: IrrRequest, writer: jspb.BinaryWriter): void;
  static deserializeBinary(bytes: Uint8Array): IrrRequest;
  static deserializeBinaryFromReader(message: IrrRequest, reader: jspb.BinaryReader): IrrRequest;
}

export namespace IrrRequest {
  export type AsObject = {
    valuesList: Array<number>,
    guess: number,
  }
}

export class DatedCashFlowRequest extends jspb.Message {
  getRate(): number;
  setRate(value: number): DatedCashFlowRequest;

  getValuesList(): Array<number>;
  setValuesList(value: Array<number>): DatedCashFlowRequest;
  clearValuesList(): DatedCashFlowRequest;
  addValues(value: number, index?: number): DatedCashFlowRequest;

  getDatesList(): Array<number>;
  setDatesList(value: Array<number>): DatedCashFlowRequest;
  clearDatesList(): DatedCashFlowRequest;
  addDates(value: number, index?: number): DatedCashFlowRequest;

  getGuess(): number;
  setGuess(value: number): DatedCashFlowRequest;

  serializeBinary(): Uint8Array;
  toObject(includeInstance?: boolean): DatedCashFlowRequest.AsObject;
  static toObject(includeInstance: boolean, msg: DatedCashFlowRequest): DatedCashFlowRequest.AsObject;
  static serializeBinaryToWriter(message: DatedCashFlowRequest, writer: jspb.BinaryWriter): void;
  static deserializeBinary(bytes: Uint8Array): DatedCashFlowRequest;
  static deserializeBinaryFromReader(message: DatedCashFlowRequest, reader: jspb.BinaryReader): DatedCashFlowRequest;
}

export namespace DatedCashFlowRequest {
  export type AsObject = {
    rate: number,
    valuesList: Array<number>,
    datesList: Array<number>,
    guess: number,
  }
}

export class PaybackRequest extends jspb.Message {
  getValuesList(): Array<number>;
  setValuesList(value: Array<number>): PaybackRequest;
  clearValuesList(): PaybackRequest;
  addValues(value: number, index?: number): PaybackRequest;

  getDiscounted(): boolean;
  setDiscounted(value: boolean): PaybackRequest;

  getRate(): number;
  setRate(value: number): PaybackRequest;

  serializeBinary(): Uint8Array;
  toObject(includeInstance?: boolean): PaybackRequest.AsObject;
  static toObject(includeInstance: boolean, msg: PaybackRequest): PaybackRequest.AsObject;
  static serializeBinaryToWriter(message: PaybackRequest, writer: jspb.BinaryWriter): void;
  static deserializeBinary(bytes: Uint8Array): PaybackRequest;
  static deserializeBinaryFromReader(message: PaybackRequest, reader: jspb.BinaryReader): PaybackRequest;
}

export namespace PaybackRequest {
  export type AsObject = {
    valuesList: Array<number>,
    discounted: boolean,
    rate: number,
  }
}

export class CumulativeRequest extends jspb.Message {
  getComponent(): CumulativeRequest.Component;
  setComponent(value: CumulativeRequest.Component): CumulativeRequest;

  getRate(): number;
  setRate(value: number): CumulativeRequest;

  getPeriods(): number;
  setPeriods(value: number): CumulativeRequest;

  getPresentValue(): number;
  setPresentValue(value: number): CumulativeRequest;

  getStartPeriod(): number;
  setStartPeriod(value: number): CumulativeRequest;

  getEndPeriod(): number;
  setEndPeriod(value: number): CumulativeRequest;

  getTiming(): AnnuityTiming;
  setTiming(value: AnnuityTiming): CumulativeRequest;

  serializeBinary(): Uint8Array;
  toObject(includeInstance?: boolean): CumulativeRequest.AsObject;
  static toObject(includeInstance: boolean, msg: CumulativeRequest): CumulativeRequest.AsObject;
  static serializeBinaryToWriter(message: CumulativeRequest, writer: jspb.BinaryWriter): void;
  static deserializeBinary(bytes: Uint8Array): CumulativeRequest;
  static deserializeBinaryFromReader(message: CumulativeRequest, reader: jspb.BinaryReader): CumulativeRequest;
}

export namespace CumulativeRequest {
  export type AsObject = {
    component: CumulativeRequest.Component,
    rate: number,
    periods: number,
    presentValue: number,
    startPeriod: number,
    endPeriod: number,
    timing: AnnuityTiming,
  }

  export enum Component { 
    INTEREST = 0,
    PRINCIPAL = 1,
  }
}

export class DepreciationRequest extends jspb.Message {
  getMethod(): DepreciationRequest.Method;
  setMethod(value: DepreciationRequest.Method): DepreciationRequest;

  getCost(): number;
  setCost(value: number): DepreciationRequest;

  getSalvage(): number;
  setSalvage(value: number): DepreciationRequest;

  getLife(): number;
  setLife(value: number): DepreciationRequest;

  getPeriod(): number;
  setPeriod(value: number): DepreciationRequest;

  getFactor(): number;
  setFactor(value: number): DepreciationRequest;

  getRecoveryPeriod(): number;
  setRecoveryPeriod(value: number): DepreciationRequest;

  getYear(): number;
  setYear(value: number): DepreciationRequest;

  serializeBinary(): Uint8Array;
  toObject(includeInstance?: boolean): DepreciationRequest.AsObject;
  static toObject(includeInstance: boolean, msg: DepreciationRequest): DepreciationRequest.AsObject;
  static serializeBinaryToWriter(message: DepreciationRequest, writer: jspb.BinaryWriter): void;
  static deserializeBinary(bytes: Uint8Array): DepreciationRequest;
  static deserializeBinaryFromReader(message: DepreciationRequest, reader: jspb.BinaryReader): DepreciationRequest;
}

export namespace DepreciationRequest {
  export type AsObject = {
    method: DepreciationRequest.Method,
    cost: number,
    salvage: number,
    life: number,
    period: number,
    factor: number,
    recoveryPeriod: number,
    year: number,
  }

  export enum Method { 
    STRAIGHT_LINE = 0,
    SUM_OF_YEARS_DIGITS = 1,
    DECLINING_BALANCE = 2,
    MACRS = 3,
  }
}

export class BondRequest extends jspb.Message {
  getPar(): number;
  setPar(value: number): BondRequest;

  getCouponRate(): number;
  setCouponRate(value: number): BondRequest;

  getFrequency(): number;
  setFrequency(value: number): BondRequest;

  getYearsToMaturity(): number;
  setYearsToMaturity(value: number): BondRequest;

  getRedemption(): number;
  setRedemption(value: number): BondRequest;

  getYield(): number;
  setYield(value: number): BondRequest;

  getPrice(): number;
  setPrice(value: number): BondRequest;

  getYieldGuess(): number;
  setYieldGuess(value: number): BondRequest;

  getKnownCase(): BondRequest.KnownCase;

  serializeBinary(): Uint8Array;
  toObject(includeInstance?: boolean): BondRequest.AsObject;
  static toObject(includeInstance: boolean, msg: BondRequest): BondRequest.AsObject;
  static serializeBinaryToWriter(message: BondRequest, writer: jspb.BinaryWriter): void;
  static deserializeBinary(bytes: Uint8Array): BondRequest;
  static deserializeBinaryFromReader(message: BondRequest, reader: jspb.BinaryReader): BondRequest;
}

export namespace BondRequest {
  export type AsObject = {
    par: number,
    couponRate: number,
    frequency: number,
    yearsToMaturity: number,
    redemption: number,
    yield: number,
    price: number,
    yieldGuess: number,
  }

  export enum KnownCase { 
    KNOWN_NOT_SET = 0,
    YIELD = 6,
    PRICE = 7,
  }
}

export class BondResponse extends jspb.Message {
  getPrice(): number;
  setPrice(value: number): BondResponse;

  getYield(): number;
  setYield(value: number): BondResponse;

  getMacaulayDuration(): number;
  setMacaulayDuration(value: number): BondResponse;

  getConvexity(): number;
  setConvexity(value: number): BondResponse;

  serializeBinary(): Uint8Array;
  toObject(includeInstance?: boolean): BondResponse.AsObject;
  static toObject(includeInstance: boolean, msg: BondResponse): BondResponse.AsObject;
  static serializeBinaryToWriter(message: BondResponse, writer: jspb.BinaryWriter): void;
  static deserializeBinary(bytes: Uint8Array): BondResponse;
  static deserializeBinaryFromReader(message: BondResponse, reader: jspb.BinaryReader): BondResponse;
}

export namespace BondResponse {
  export type AsObject = {
    price: number,
    yield: number,
    macaulayDuration: number,
    convexity: number,
  }
}

export class TreasuryBillRequest extends jspb.Message {
  getFaceValue(): number;
  setFaceValue(value: number): TreasuryBillRequest;

  getDaysToMaturity(): number;
  setDaysToMaturity(value: number): TreasuryBillRequest;

  getDiscountRate(): number;
  setDiscountRate(value: number): TreasuryBillRequest;

  getPrice(): number;
  setPrice(value: number): TreasuryBillRequest;

  getKnownCase(): TreasuryBillRequest.KnownCase;

  serializeBinary(): Uint8Array;
  toObject(includeInstance?: boolean): TreasuryBillRequest.AsObject;
  static toObject(includeInstance: boolean, msg: TreasuryBillRequest): TreasuryBillRequest.AsObject;
  static serializeBinaryToWriter(message: TreasuryBillRequest, writer: jspb.BinaryWriter): void;
  static deserializeBinary(bytes: Uint8Array): TreasuryBillRequest;
  static deserializeBinaryFromReader(message: TreasuryBillRequest, reader: jspb.BinaryReader): TreasuryBillRequest;
}

export namespace TreasuryBillRequest {
  export type AsObject = {
    faceValue: number,
    daysToMaturity: number,
    discountRate: number,
    price: number,
  }

  export enum KnownCase { 
    KNOWN_NOT_SET = 0,
    DISCOUNT_RATE = 3,
    PRICE = 4,
  }
}

export class TreasuryBillResponse extends jspb.Message {
  getPrice(): number;
  setPrice(value: number): TreasuryBillResponse;

  getBondEquivalentYield(): number;
  setBondEquivalentYield(value: number): TreasuryBillResponse;

  getMoneyMarketYield(): number;
  setMoneyMarketYield(value: number): TreasuryBillResponse;

  getBankDiscountYield(): number;
  setBankDiscountYield(value: number): TreasuryBillResponse;

  serializeBinary(): Uint8Array;
  toObject(includeInstance?: boolean): TreasuryBillResponse.AsObject;
  static toObject(includeInstance: boolean, msg: TreasuryBillResponse): TreasuryBillResponse.AsObject;
  static serializeBinaryToWriter(message: TreasuryBillResponse, writer: jspb.BinaryWriter): void;
  static deserializeBinary(bytes: Uint8Array): TreasuryBillResponse;
  static deserializeBinaryFromReader(message: TreasuryBillResponse, reader: jspb.BinaryReader): TreasuryBillResponse;
}

export namespace TreasuryBillResponse {
  export type AsObject = {
    price: number,
    bondEquivalentYield: number,
    moneyMarketYield: number,
    bankDiscountYield: number,
  }
}

export class FuturesPricingRequest extends jspb.Message {
  getSpot(): number;
  setSpot(value: number): FuturesPricingRequest;

  getRate(): number;
  setRate(value: number): FuturesPricingRequest;

  getCostOfCarry(): number;
  setCostOfCarry(value: number): FuturesPricingRequest;

  getYearsToMaturity(): number;
  setYearsToMaturity(value: number): FuturesPricingRequest;

  getContinuous(): boolean;
  setContinuous(value: boolean): FuturesPricingRequest;

  serializeBinary(): Uint8Array;
  toObject(includeInstance?: boolean): FuturesPricingRequest.AsObject;
  static toObject(includeInstance: boolean, msg: FuturesPricingRequest): FuturesPricingRequest.AsObject;
  static serializeBinaryToWriter(message: FuturesPricingRequest, writer: jspb.BinaryWriter): void;
  static deserializeBinary(bytes: Uint8Array): FuturesPricingRequest;
  static deserializeBinaryFromReader(message: FuturesPricingRequest, reader: jspb.BinaryReader): FuturesPricingRequest;
}

export namespace FuturesPricingRequest {
  export type AsObject = {
    spot: number,
    rate: number,
    costOfCarry: number,
    yearsToMaturity: number,
    continuous: boolean,
  }
}

export class FuturesValuationRequest extends jspb.Message {
  getCurrentSpot(): number;
  setCurrentSpot(value: number): FuturesValuationRequest;

  getDeliveryPrice(): number;
  setDeliveryPrice(value: number): FuturesValuationRequest;

  getRate(): number;
  setRate(value: number): FuturesValuationRequest;

  getYearsToMaturity(): number;
  setYearsToMaturity(value: number): FuturesValuationRequest;

  getIsLong(): boolean;
  setIsLong(value: boolean): FuturesValuationRequest;

  serializeBinary(): Uint8Array;
  toObject(includeInstance?: boolean): FuturesValuationRequest.AsObject;
  static toObject(includeInstance: boolean, msg: FuturesValuationRequest): FuturesValuationRequest.AsObject;
  static serializeBinaryToWriter(message: FuturesValuationRequest, writer: jspb.BinaryWriter): void;
  static deserializeBinary(bytes: Uint8Array): FuturesValuationRequest;
  static deserializeBinaryFromReader(message: FuturesValuationRequest, reader: jspb.BinaryReader): FuturesValuationRequest;
}

export namespace FuturesValuationRequest {
  export type AsObject = {
    currentSpot: number,
    deliveryPrice: number,
    rate: number,
    yearsToMaturity: number,
    isLong: boolean,
  }
}

export class MarginSimulationRequest extends jspb.Message {
  getInitialDeposit(): number;
  setInitialDeposit(value: number): MarginSimulationRequest;

  getInitialMarginRequirement(): number;
  setInitialMarginRequirement(value: number): MarginSimulationRequest;

  getMaintenanceMarginRequirement(): number;
  setMaintenanceMarginRequirement(value: number): MarginSimulationRequest;

  getContractSize(): number;
  setContractSize(value: number): MarginSimulationRequest;

  getEntryPrice(): number;
  setEntryPrice(value: number): MarginSimulationRequest;

  getDailyPricesList(): Array<number>;
  setDailyPricesList(value: Array<number>): MarginSimulationRequest;
  clearDailyPricesList(): MarginSimulationRequest;
  addDailyPrices(value: number, index?: number): MarginSimulationRequest;

  getIsLong(): boolean;
  setIsLong(value: boolean): MarginSimulationRequest;

  serializeBinary(): Uint8Array;
  toObject(includeInstance?: boolean): MarginSimulationRequest.AsObject;
  static toObject(includeInstance: boolean, msg: MarginSimulationRequest): MarginSimulationRequest.AsObject;
  static serializeBinaryToWriter(message: MarginSimulationRequest, writer: jspb.BinaryWriter): void;
  static deserializeBinary(bytes: Uint8Array): MarginSimulationRequest;
  static deserializeBinaryFromReader(message: MarginSimulationRequest, reader: jspb.BinaryReader): MarginSimulationRequest;
}

export namespace MarginSimulationRequest {
  export type AsObject = {
    initialDeposit: number,
    initialMarginRequirement: number,
    maintenanceMarginRequirement: number,
    contractSize: number,
    entryPrice: number,
    dailyPricesList: Array<number>,
    isLong: boolean,
  }
}

export class MarginSimulationResponse extends jspb.Message {
  getBalance(): number;
  setBalance(value: number): MarginSimulationResponse;

  getInitialMargin(): number;
  setInitialMargin(value: number): MarginSimulationResponse;

  getMaintenanceMargin(): number;
  setMaintenanceMargin(value: number): MarginSimulationResponse;

  getContractSize(): number;
  setContractSize(value: number): MarginSimulationResponse;

  getMarginCall(): boolean;
  setMarginCall(value: boolean): MarginSimulationResponse;

  serializeBinary(): Uint8Array;
  toObject(includeInstance?: boolean): MarginSimulationResponse.AsObject;
  static toObject(includeInstance: boolean, msg: MarginSimulationResponse): MarginSimulationResponse.AsObject;
  static serializeBinaryToWriter(message: MarginSimulationResponse, writer: jspb.BinaryWriter): void;
  static deserializeBinary(bytes: Uint8Array): MarginSimulationResponse;
  static deserializeBinaryFromReader(message: MarginSimulationResponse, reader: jspb.BinaryReader): MarginSimulationResponse;
}

export namespace MarginSimulationResponse {
  export type AsObject = {
    balance: number,
    initialMargin: number,
    maintenanceMargin: number,
    contractSize: number,
    marginCall: boolean,
  }
}

export class HedgeRequest extends jspb.Message {
  getAssetVolatility(): number;
  setAssetVolatility(value: number): HedgeRequest;

  getFuturesVolatility(): number;
  setFuturesVolatility(value: number): HedgeRequest;

  getCorrelation(): number;
  setCorrelation(value: number): HedgeRequest;

  getSpotValue(): number;
  setSpotValue(value: number): HedgeRequest;

  getContractMultiplier(): number;
  setContractMultiplier(value: number): HedgeRequest;

  getFuturesPrice(): number;
  setFuturesPrice(value: number): HedgeRequest;

  serializeBinary(): Uint8Array;
  toObject(includeInstance?: boolean): HedgeRequest.AsObject;
  static toObject(includeInstance: boolean, msg: HedgeRequest): HedgeRequest.AsObject;
  static serializeBinaryToWriter(message: HedgeRequest, writer: jspb.BinaryWriter): void;
  static deserializeBinary(bytes: Uint8Array): HedgeRequest;
  static deserializeBinaryFromReader(message: HedgeRequest, reader: jspb.BinaryReader): HedgeRequest;
}

export namespace HedgeRequest {
  export type AsObject = {
    assetVolatility: number,
    futuresVolatility: number,
    correlation: number,
    spotValue: number,
    contractMultiplier: number,
    futuresPrice: number,
  }
}

export class HedgeResponse extends jspb.Message {
  getHedgeRatio(): number;
  setHedgeRatio(value: number): HedgeResponse;

  getContracts(): number;
  setContracts(value: number): HedgeResponse;

  getContractsComputed(): boolean;
  setContractsComputed(value: boolean): HedgeResponse;

  serializeBinary(): Uint8Array;
  toObject(includeInstance?: boolean): HedgeResponse.AsObject;
  static toObject(includeInstance: boolean, msg: HedgeResponse): HedgeResponse.AsObject;
  static serializeBinaryToWriter(message: HedgeResponse, writer: jspb.BinaryWriter): void;
  static deserializeBinary(bytes: Uint8Array): HedgeResponse;
  static deserializeBinaryFromReader(message: HedgeResponse, reader: jspb.BinaryReader): HedgeResponse;
}

export namespace HedgeResponse {
  export type AsObject = {
    hedgeRatio: number,
    contracts: number,
    contractsComputed: boolean,
  }
}

export class CommoditySpreadRequest extends jspb.Message {
  getSpread(): CommoditySpreadRequest.Spread;
  setSpread(value: CommoditySpreadRequest.Spread): CommoditySpreadRequest;

  getA(): number;
  setA(value: number): CommoditySpreadRequest;

  getB(): number;
  setB(value: number): CommoditySpreadRequest;

  getC(): number;
  setC(value: number): CommoditySpreadRequest;

  serializeBinary(): Uint8Array;
  toObject(includeInstance?: boolean): CommoditySpreadRequest.AsObject;
  static toObject(includeInstance: boolean, msg: CommoditySpreadRequest): CommoditySpreadRequest.AsObject;
  static serializeBinaryToWriter(message: CommoditySpreadRequest, writer: jspb.BinaryWriter): void;
  static deserializeBinary(bytes: Uint8Array): CommoditySpreadRequest;
  static deserializeBinaryFromReader(message: CommoditySpreadRequest, reader: jspb.BinaryReader): CommoditySpreadRequest;
}

export namespace CommoditySpreadRequest {
  export type AsObject = {
    spread: CommoditySpreadRequest.Spread,
    a: number,
    b: number,
    c: number,
  }

  export enum Spread { 
    CRACK_321 = 0,
    SPARK = 1,
    CRUSH = 2,
  }
}

export class RentalRoiRequest extends jspb.Message {
  getPropertyValue(): string;
  setPropertyValue(value: string): RentalRoiRequest;

  getTotalCashInvested(): string;
  setTotalCashInvested(value: string): RentalRoiRequest;

  getPeriodicGrossRent(): string;
  setPeriodicGrossRent(value: string): RentalRoiRequest;

  getPeriodicOperatingExpenses(): string;
  setPeriodicOperatingExpenses(value: string): RentalRoiRequest;

  getPeriodicMortgagePayment(): string;
  setPeriodicMortgagePayment(value: string): RentalRoiRequest;

  getPeriodsPerYear(): number;
  setPeriodsPerYear(value: number): RentalRoiRequest;

  serializeBinary(): Uint8Array;
  toObject(includeInstance?: boolean): RentalRoiRequest.AsObject;
  static toObject(includeInstance: boolean, msg: RentalRoiRequest): RentalRoiRequest.AsObject;
  static serializeBinaryToWriter(message: RentalRoiRequest, writer: jspb.BinaryWriter): void;
  static deserializeBinary(bytes: Uint8Array): RentalRoiRequest;
  static deserializeBinaryFromReader(message: RentalRoiRequest, reader: jspb.BinaryReader): RentalRoiRequest;
}

export namespace RentalRoiRequest {
  export type AsObject = {
    propertyValue: string,
    totalCashInvested: string,
    periodicGrossRent: string,
    periodicOperatingExpenses: string,
    periodicMortgagePayment: string,
    periodsPerYear: number,
  }
}

export class RentalRoiResponse extends jspb.Message {
  getNetOperatingIncome(): string;
  setNetOperatingIncome(value: string): RentalRoiResponse;

  getAnnualCashFlow(): string;
  setAnnualCashFlow(value: string): RentalRoiResponse;

  getCashOnCashReturn(): string;
  setCashOnCashReturn(value: string): RentalRoiResponse;

  getCapRate(): string;
  setCapRate(value: string): RentalRoiResponse;

  getGrossRentMultiplier(): string;
  setGrossRentMultiplier(value: string): RentalRoiResponse;

  serializeBinary(): Uint8Array;
  toObject(includeInstance?: boolean): RentalRoiResponse.AsObject;
  static toObject(includeInstance: boolean, msg: RentalRoiResponse): RentalRoiResponse.AsObject;
  static serializeBinaryToWriter(message: RentalRoiResponse, writer: jspb.BinaryWriter): void;
  static deserializeBinary(bytes: Uint8Array): RentalRoiResponse;
  static deserializeBinaryFromReader(message: RentalRoiResponse, reader: jspb.BinaryReader): RentalRoiResponse;
}

export namespace RentalRoiResponse {
  export type AsObject = {
    netOperatingIncome: string,
    annualCashFlow: string,
    cashOnCashReturn: string,
    capRate: string,
    grossRentMultiplier: string,
  }
}

export class HomeFutureValueRequest extends jspb.Message {
  getCurrentPropertyValue(): string;
  setCurrentPropertyValue(value: string): HomeFutureValueRequest;

  getAnnualAppreciationRate(): string;
  setAnnualAppreciationRate(value: string): HomeFutureValueRequest;

  getCurrentLoanBalance(): string;
  setCurrentLoanBalance(value: string): HomeFutureValueRequest;

  getAnnualMortgageRate(): string;
  setAnnualMortgageRate(value: string): HomeFutureValueRequest;

  getCurrentMonthlyPayment(): string;
  setCurrentMonthlyPayment(value: string): HomeFutureValueRequest;

  getTargetYears(): number;
  setTargetYears(value: number): HomeFutureValueRequest;

  getPaymentsPerYear(): number;
  setPaymentsPerYear(value: number): HomeFutureValueRequest;

  serializeBinary(): Uint8Array;
  toObject(includeInstance?: boolean): HomeFutureValueRequest.AsObject;
  static toObject(includeInstance: boolean, msg: HomeFutureValueRequest): HomeFutureValueRequest.AsObject;
  static serializeBinaryToWriter(message: HomeFutureValueRequest, writer: jspb.BinaryWriter): void;
  static deserializeBinary(bytes: Uint8Array): HomeFutureValueRequest;
  static deserializeBinaryFromReader(message: HomeFutureValueRequest, reader: jspb.BinaryReader): HomeFutureValueRequest;
}

export namespace HomeFutureValueRequest {
  export type AsObject = {
    currentPropertyValue: string,
    annualAppreciationRate: string,
    currentLoanBalance: string,
    annualMortgageRate: string,
    currentMonthlyPayment: string,
    targetYears: number,
    paymentsPerYear: number,
  }
}

export class HomeFutureValueResponse extends jspb.Message {
  getFuturePropertyValue(): number;
  setFuturePropertyValue(value: number): HomeFutureValueResponse;

  getFutureLoanBalance(): string;
  setFutureLoanBalance(value: string): HomeFutureValueResponse;

  getFutureEquity(): string;
  setFutureEquity(value: string): HomeFutureValueResponse;

  serializeBinary(): Uint8Array;
  toObject(includeInstance?: boolean): HomeFutureValueResponse.AsObject;
  static toObject(includeInstance: boolean, msg: HomeFutureValueResponse): HomeFutureValueResponse.AsObject;
  static serializeBinaryToWriter(message: HomeFutureValueResponse, writer: jspb.BinaryWriter): void;
  static deserializeBinary(bytes: Uint8Array): HomeFutureValueResponse;
  static deserializeBinaryFromReader(message: HomeFutureValueResponse, reader: jspb.BinaryReader): HomeFutureValueResponse;
}

export namespace HomeFutureValueResponse {
  export type AsObject = {
    futurePropertyValue: number,
    futureLoanBalance: string,
    futureEquity: string,
  }
}

export class RentVsBuyRequest extends jspb.Message {
  getPropertyPrice(): string;
  setPropertyPrice(value: string): RentVsBuyRequest;

  getDownPayment(): string;
  setDownPayment(value: string): RentVsBuyRequest;

  getMonthlyPitiAndMaintenance(): string;
  setMonthlyPitiAndMaintenance(value: string): RentVsBuyRequest;

  getAnnualHomeAppreciation(): string;
  setAnnualHomeAppreciation(value: string): RentVsBuyRequest;

  getCurrentMonthlyRent(): string;
  setCurrentMonthlyRent(value: string): RentVsBuyRequest;

  getAnnualRentIncrease(): string;
  setAnnualRentIncrease(value: string): RentVsBuyRequest;

  getAnnualInvestmentReturn(): string;
  setAnnualInvestmentReturn(value: string): RentVsBuyRequest;

  getYears(): number;
  setYears(value: number): RentVsBuyRequest;

  getLoanAnnualRate(): string;
  setLoanAnnualRate(value: string): RentVsBuyRequest;

  getLoanTermYears(): number;
  setLoanTermYears(value: number): RentVsBuyRequest;

  getLoanAmount(): string;
  setLoanAmount(value: string): RentVsBuyRequest;

  getMonthlyTaxesInsMaintenance(): string;
  setMonthlyTaxesInsMaintenance(value: string): RentVsBuyRequest;

  getClosingCostsBuy(): string;
  setClosingCostsBuy(value: string): RentVsBuyRequest;

  getSellingCostPercent(): string;
  setSellingCostPercent(value: string): RentVsBuyRequest;

  getAnnualInflationRate(): string;
  setAnnualInflationRate(value: string): RentVsBuyRequest;

  serializeBinary(): Uint8Array;
  toObject(includeInstance?: boolean): RentVsBuyRequest.AsObject;
  static toObject(includeInstance: boolean, msg: RentVsBuyRequest): RentVsBuyRequest.AsObject;
  static serializeBinaryToWriter(message: RentVsBuyRequest, writer: jspb.BinaryWriter): void;
  static deserializeBinary(bytes: Uint8Array): RentVsBuyRequest;
  static deserializeBinaryFromReader(message: RentVsBuyRequest, reader: jspb.BinaryReader): RentVsBuyRequest;
}

export namespace RentVsBuyRequest {
  export type AsObject = {
    propertyPrice: string,
    downPayment: string,
    monthlyPitiAndMaintenance: string,
    annualHomeAppreciation: string,
    currentMonthlyRent: string,
    annualRentIncrease: string,
    annualInvestmentReturn: string,
    years: number,
    loanAnnualRate: string,
    loanTermYears: number,
    loanAmount: string,
    monthlyTaxesInsMaintenance: string,
    closingCostsBuy: string,
    sellingCostPercent: string,
    annualInflationRate: string,
  }
}

export class RentVsBuyResponse extends jspb.Message {
  getTotalCostOfBuying(): number;
  setTotalCostOfBuying(value: number): RentVsBuyResponse;

  getTotalCostOfRenting(): number;
  setTotalCostOfRenting(value: number): RentVsBuyResponse;

  getIsBuyingBetter(): boolean;
  setIsBuyingBetter(value: boolean): RentVsBuyResponse;

  getBuyingAdvantage(): number;
  setBuyingAdvantage(value: number): RentVsBuyResponse;

  getTotalCostOfBuyingExact(): string;
  setTotalCostOfBuyingExact(value: string): RentVsBuyResponse;

  getTotalCostOfRentingExact(): string;
  setTotalCostOfRentingExact(value: string): RentVsBuyResponse;

  getBuyingAdvantageExact(): string;
  setBuyingAdvantageExact(value: string): RentVsBuyResponse;

  getOwnerTerminalWealth(): string;
  setOwnerTerminalWealth(value: string): RentVsBuyResponse;

  getRenterTerminalWealth(): string;
  setRenterTerminalWealth(value: string): RentVsBuyResponse;

  getFinalLoanBalance(): string;
  setFinalLoanBalance(value: string): RentVsBuyResponse;

  getHomeSalePrice(): string;
  setHomeSalePrice(value: string): RentVsBuyResponse;

  getSellingCosts(): string;
  setSellingCosts(value: string): RentVsBuyResponse;

  getTotalPrincipalPaid(): string;
  setTotalPrincipalPaid(value: string): RentVsBuyResponse;

  getTotalInterestPaid(): string;
  setTotalInterestPaid(value: string): RentVsBuyResponse;

  getTotalRentPaid(): string;
  setTotalRentPaid(value: string): RentVsBuyResponse;

  getRealBuyingAdvantage(): string;
  setRealBuyingAdvantage(value: string): RentVsBuyResponse;

  getRealOwnerTerminalWealth(): string;
  setRealOwnerTerminalWealth(value: string): RentVsBuyResponse;

  getRealRenterTerminalWealth(): string;
  setRealRenterTerminalWealth(value: string): RentVsBuyResponse;

  serializeBinary(): Uint8Array;
  toObject(includeInstance?: boolean): RentVsBuyResponse.AsObject;
  static toObject(includeInstance: boolean, msg: RentVsBuyResponse): RentVsBuyResponse.AsObject;
  static serializeBinaryToWriter(message: RentVsBuyResponse, writer: jspb.BinaryWriter): void;
  static deserializeBinary(bytes: Uint8Array): RentVsBuyResponse;
  static deserializeBinaryFromReader(message: RentVsBuyResponse, reader: jspb.BinaryReader): RentVsBuyResponse;
}

export namespace RentVsBuyResponse {
  export type AsObject = {
    totalCostOfBuying: number,
    totalCostOfRenting: number,
    isBuyingBetter: boolean,
    buyingAdvantage: number,
    totalCostOfBuyingExact: string,
    totalCostOfRentingExact: string,
    buyingAdvantageExact: string,
    ownerTerminalWealth: string,
    renterTerminalWealth: string,
    finalLoanBalance: string,
    homeSalePrice: string,
    sellingCosts: string,
    totalPrincipalPaid: string,
    totalInterestPaid: string,
    totalRentPaid: string,
    realBuyingAdvantage: string,
    realOwnerTerminalWealth: string,
    realRenterTerminalWealth: string,
  }
}

export class RentVsBuyBatchRequest extends jspb.Message {
  getScenariosList(): Array<RentVsBuyRequest>;
  setScenariosList(value: Array<RentVsBuyRequest>): RentVsBuyBatchRequest;
  clearScenariosList(): RentVsBuyBatchRequest;
  addScenarios(value?: RentVsBuyRequest, index?: number): RentVsBuyRequest;

  serializeBinary(): Uint8Array;
  toObject(includeInstance?: boolean): RentVsBuyBatchRequest.AsObject;
  static toObject(includeInstance: boolean, msg: RentVsBuyBatchRequest): RentVsBuyBatchRequest.AsObject;
  static serializeBinaryToWriter(message: RentVsBuyBatchRequest, writer: jspb.BinaryWriter): void;
  static deserializeBinary(bytes: Uint8Array): RentVsBuyBatchRequest;
  static deserializeBinaryFromReader(message: RentVsBuyBatchRequest, reader: jspb.BinaryReader): RentVsBuyBatchRequest;
}

export namespace RentVsBuyBatchRequest {
  export type AsObject = {
    scenariosList: Array<RentVsBuyRequest.AsObject>,
  }
}

export class RentVsBuyBatchResult extends jspb.Message {
  getResult(): RentVsBuyResponse | undefined;
  setResult(value?: RentVsBuyResponse): RentVsBuyBatchResult;
  hasResult(): boolean;
  clearResult(): RentVsBuyBatchResult;

  getError(): string;
  setError(value: string): RentVsBuyBatchResult;

  getOutcomeCase(): RentVsBuyBatchResult.OutcomeCase;

  serializeBinary(): Uint8Array;
  toObject(includeInstance?: boolean): RentVsBuyBatchResult.AsObject;
  static toObject(includeInstance: boolean, msg: RentVsBuyBatchResult): RentVsBuyBatchResult.AsObject;
  static serializeBinaryToWriter(message: RentVsBuyBatchResult, writer: jspb.BinaryWriter): void;
  static deserializeBinary(bytes: Uint8Array): RentVsBuyBatchResult;
  static deserializeBinaryFromReader(message: RentVsBuyBatchResult, reader: jspb.BinaryReader): RentVsBuyBatchResult;
}

export namespace RentVsBuyBatchResult {
  export type AsObject = {
    result?: RentVsBuyResponse.AsObject,
    error: string,
  }

  export enum OutcomeCase { 
    OUTCOME_NOT_SET = 0,
    RESULT = 1,
    ERROR = 2,
  }
}

export class RentVsBuyBatchResponse extends jspb.Message {
  getResultsList(): Array<RentVsBuyBatchResult>;
  setResultsList(value: Array<RentVsBuyBatchResult>): RentVsBuyBatchResponse;
  clearResultsList(): RentVsBuyBatchResponse;
  addResults(value?: RentVsBuyBatchResult, index?: number): RentVsBuyBatchResult;

  serializeBinary(): Uint8Array;
  toObject(includeInstance?: boolean): RentVsBuyBatchResponse.AsObject;
  static toObject(includeInstance: boolean, msg: RentVsBuyBatchResponse): RentVsBuyBatchResponse.AsObject;
  static serializeBinaryToWriter(message: RentVsBuyBatchResponse, writer: jspb.BinaryWriter): void;
  static deserializeBinary(bytes: Uint8Array): RentVsBuyBatchResponse;
  static deserializeBinaryFromReader(message: RentVsBuyBatchResponse, reader: jspb.BinaryReader): RentVsBuyBatchResponse;
}

export namespace RentVsBuyBatchResponse {
  export type AsObject = {
    resultsList: Array<RentVsBuyBatchResult.AsObject>,
  }
}

export class HomeNpvRequest extends jspb.Message {
  getPropertyPrice(): string;
  setPropertyPrice(value: string): HomeNpvRequest;

  getDownPayment(): string;
  setDownPayment(value: string): HomeNpvRequest;

  getClosingCostsBuy(): string;
  setClosingCostsBuy(value: string): HomeNpvRequest;

  getLoanAmount(): string;
  setLoanAmount(value: string): HomeNpvRequest;

  getLoanAnnualRate(): string;
  setLoanAnnualRate(value: string): HomeNpvRequest;

  getLoanTermYears(): number;
  setLoanTermYears(value: number): HomeNpvRequest;

  getMonthlyTaxesInsHoa(): string;
  setMonthlyTaxesInsHoa(value: string): HomeNpvRequest;

  getMonthlyMaintenance(): string;
  setMonthlyMaintenance(value: string): HomeNpvRequest;

  getAnnualAppreciationRate(): string;
  setAnnualAppreciationRate(value: string): HomeNpvRequest;

  getSellingClosingCostPercent(): string;
  setSellingClosingCostPercent(value: string): HomeNpvRequest;

  getMonthlyRentSaved(): string;
  setMonthlyRentSaved(value: string): HomeNpvRequest;

  getAnnualRentIncrease(): string;
  setAnnualRentIncrease(value: string): HomeNpvRequest;

  getAnnualDiscountRate(): string;
  setAnnualDiscountRate(value: string): HomeNpvRequest;

  getHoldingPeriodYears(): number;
  setHoldingPeriodYears(value: number): HomeNpvRequest;

  getAnnualInflationRate(): string;
  setAnnualInflationRate(value: string): HomeNpvRequest;

  serializeBinary(): Uint8Array;
  toObject(includeInstance?: boolean): HomeNpvRequest.AsObject;
  static toObject(includeInstance: boolean, msg: HomeNpvRequest): HomeNpvRequest.AsObject;
  static serializeBinaryToWriter(message: HomeNpvRequest, writer: jspb.BinaryWriter): void;
  static deserializeBinary(bytes: Uint8Array): HomeNpvRequest;
  static deserializeBinaryFromReader(message: HomeNpvRequest, reader: jspb.BinaryReader): HomeNpvRequest;
}

export namespace HomeNpvRequest {
  export type AsObject = {
    propertyPrice: string,
    downPayment: string,
    closingCostsBuy: string,
    loanAmount: string,
    loanAnnualRate: string,
    loanTermYears: number,
    monthlyTaxesInsHoa: string,
    monthlyMaintenance: string,
    annualAppreciationRate: string,
    sellingClosingCostPercent: string,
    monthlyRentSaved: string,
    annualRentIncrease: string,
    annualDiscountRate: string,
    holdingPeriodYears: number,
    annualInflationRate: string,
  }
}

export class HomeNpvResponse extends jspb.Message {
  getNetPresentValue(): number;
  setNetPresentValue(value: number): HomeNpvResponse;

  getInternalRateOfReturn(): number;
  setInternalRateOfReturn(value: number): HomeNpvResponse;

  getFutureSalePrice(): number;
  setFutureSalePrice(value: number): HomeNpvResponse;

  getFutureEquity(): number;
  setFutureEquity(value: number): HomeNpvResponse;

  getRealInternalRateOfReturn(): number;
  setRealInternalRateOfReturn(value: number): HomeNpvResponse;

  getRealFutureSalePrice(): number;
  setRealFutureSalePrice(value: number): HomeNpvResponse;

  getRealFutureEquity(): number;
  setRealFutureEquity(value: number): HomeNpvResponse;

  serializeBinary(): Uint8Array;
  toObject(includeInstance?: boolean): HomeNpvResponse.AsObject;
  static toObject(includeInstance: boolean, msg: HomeNpvResponse): HomeNpvResponse.AsObject;
  static serializeBinaryToWriter(message: HomeNpvResponse, writer: jspb.BinaryWriter): void;
  static deserializeBinary(bytes: Uint8Array): HomeNpvResponse;
  static deserializeBinaryFromReader(message: HomeNpvResponse, reader: jspb.BinaryReader): HomeNpvResponse;
}

export namespace HomeNpvResponse {
  export type AsObject = {
    netPresentValue: number,
    internalRateOfReturn: number,
    futureSalePrice: number,
    futureEquity: number,
    realInternalRateOfReturn: number,
    realFutureSalePrice: number,
    realFutureEquity: number,
  }
}

export class OptionTreeRequest extends jspb.Message {
  getSpot(): number;
  setSpot(value: number): OptionTreeRequest;

  getStrike(): number;
  setStrike(value: number): OptionTreeRequest;

  getRate(): number;
  setRate(value: number): OptionTreeRequest;

  getVolatility(): number;
  setVolatility(value: number): OptionTreeRequest;

  getYearsToExpiry(): number;
  setYearsToExpiry(value: number): OptionTreeRequest;

  getSteps(): number;
  setSteps(value: number): OptionTreeRequest;

  getOptionType(): OptionType;
  setOptionType(value: OptionType): OptionTreeRequest;

  getExerciseType(): ExerciseType;
  setExerciseType(value: ExerciseType): OptionTreeRequest;

  getBermudanDatesList(): Array<number>;
  setBermudanDatesList(value: Array<number>): OptionTreeRequest;
  clearBermudanDatesList(): OptionTreeRequest;
  addBermudanDates(value: number, index?: number): OptionTreeRequest;

  getAsianType(): AsianType;
  setAsianType(value: AsianType): OptionTreeRequest;

  getAveragingStates(): number;
  setAveragingStates(value: number): OptionTreeRequest;

  getLambda(): number;
  setLambda(value: number): OptionTreeRequest;

  serializeBinary(): Uint8Array;
  toObject(includeInstance?: boolean): OptionTreeRequest.AsObject;
  static toObject(includeInstance: boolean, msg: OptionTreeRequest): OptionTreeRequest.AsObject;
  static serializeBinaryToWriter(message: OptionTreeRequest, writer: jspb.BinaryWriter): void;
  static deserializeBinary(bytes: Uint8Array): OptionTreeRequest;
  static deserializeBinaryFromReader(message: OptionTreeRequest, reader: jspb.BinaryReader): OptionTreeRequest;
}

export namespace OptionTreeRequest {
  export type AsObject = {
    spot: number,
    strike: number,
    rate: number,
    volatility: number,
    yearsToExpiry: number,
    steps: number,
    optionType: OptionType,
    exerciseType: ExerciseType,
    bermudanDatesList: Array<number>,
    asianType: AsianType,
    averagingStates: number,
    lambda: number,
  }
}

export class OptionPricingResponse extends jspb.Message {
  getValue(): number;
  setValue(value: number): OptionPricingResponse;

  getDelta(): number;
  setDelta(value: number): OptionPricingResponse;

  getGamma(): number;
  setGamma(value: number): OptionPricingResponse;

  getTheta(): number;
  setTheta(value: number): OptionPricingResponse;

  serializeBinary(): Uint8Array;
  toObject(includeInstance?: boolean): OptionPricingResponse.AsObject;
  static toObject(includeInstance: boolean, msg: OptionPricingResponse): OptionPricingResponse.AsObject;
  static serializeBinaryToWriter(message: OptionPricingResponse, writer: jspb.BinaryWriter): void;
  static deserializeBinary(bytes: Uint8Array): OptionPricingResponse;
  static deserializeBinaryFromReader(message: OptionPricingResponse, reader: jspb.BinaryReader): OptionPricingResponse;
}

export namespace OptionPricingResponse {
  export type AsObject = {
    value: number,
    delta: number,
    gamma: number,
    theta: number,
  }
}

export class BlackScholesRequest extends jspb.Message {
  getSpot(): number;
  setSpot(value: number): BlackScholesRequest;

  getStrike(): number;
  setStrike(value: number): BlackScholesRequest;

  getRate(): number;
  setRate(value: number): BlackScholesRequest;

  getVolatility(): number;
  setVolatility(value: number): BlackScholesRequest;

  getYearsToExpiry(): number;
  setYearsToExpiry(value: number): BlackScholesRequest;

  getOptionType(): OptionType;
  setOptionType(value: OptionType): BlackScholesRequest;

  serializeBinary(): Uint8Array;
  toObject(includeInstance?: boolean): BlackScholesRequest.AsObject;
  static toObject(includeInstance: boolean, msg: BlackScholesRequest): BlackScholesRequest.AsObject;
  static serializeBinaryToWriter(message: BlackScholesRequest, writer: jspb.BinaryWriter): void;
  static deserializeBinary(bytes: Uint8Array): BlackScholesRequest;
  static deserializeBinaryFromReader(message: BlackScholesRequest, reader: jspb.BinaryReader): BlackScholesRequest;
}

export namespace BlackScholesRequest {
  export type AsObject = {
    spot: number,
    strike: number,
    rate: number,
    volatility: number,
    yearsToExpiry: number,
    optionType: OptionType,
  }
}

export class BlackScholesResponse extends jspb.Message {
  getValue(): number;
  setValue(value: number): BlackScholesResponse;

  getDelta(): number;
  setDelta(value: number): BlackScholesResponse;

  getGamma(): number;
  setGamma(value: number): BlackScholesResponse;

  getTheta(): number;
  setTheta(value: number): BlackScholesResponse;

  getVega(): number;
  setVega(value: number): BlackScholesResponse;

  getRho(): number;
  setRho(value: number): BlackScholesResponse;

  getVanna(): number;
  setVanna(value: number): BlackScholesResponse;

  getVolga(): number;
  setVolga(value: number): BlackScholesResponse;

  getCharm(): number;
  setCharm(value: number): BlackScholesResponse;

  getColor(): number;
  setColor(value: number): BlackScholesResponse;

  getSpeed(): number;
  setSpeed(value: number): BlackScholesResponse;

  serializeBinary(): Uint8Array;
  toObject(includeInstance?: boolean): BlackScholesResponse.AsObject;
  static toObject(includeInstance: boolean, msg: BlackScholesResponse): BlackScholesResponse.AsObject;
  static serializeBinaryToWriter(message: BlackScholesResponse, writer: jspb.BinaryWriter): void;
  static deserializeBinary(bytes: Uint8Array): BlackScholesResponse;
  static deserializeBinaryFromReader(message: BlackScholesResponse, reader: jspb.BinaryReader): BlackScholesResponse;
}

export namespace BlackScholesResponse {
  export type AsObject = {
    value: number,
    delta: number,
    gamma: number,
    theta: number,
    vega: number,
    rho: number,
    vanna: number,
    volga: number,
    charm: number,
    color: number,
    speed: number,
  }
}

export class MonteCarloRequest extends jspb.Message {
  getSpot(): number;
  setSpot(value: number): MonteCarloRequest;

  getStrike(): number;
  setStrike(value: number): MonteCarloRequest;

  getRate(): number;
  setRate(value: number): MonteCarloRequest;

  getVolatility(): number;
  setVolatility(value: number): MonteCarloRequest;

  getYearsToExpiry(): number;
  setYearsToExpiry(value: number): MonteCarloRequest;

  getPaths(): number;
  setPaths(value: number): MonteCarloRequest;

  getSteps(): number;
  setSteps(value: number): MonteCarloRequest;

  getOptionType(): OptionType;
  setOptionType(value: OptionType): MonteCarloRequest;

  getAsianType(): AsianType;
  setAsianType(value: AsianType): MonteCarloRequest;

  getNumThreads(): number;
  setNumThreads(value: number): MonteCarloRequest;

  serializeBinary(): Uint8Array;
  toObject(includeInstance?: boolean): MonteCarloRequest.AsObject;
  static toObject(includeInstance: boolean, msg: MonteCarloRequest): MonteCarloRequest.AsObject;
  static serializeBinaryToWriter(message: MonteCarloRequest, writer: jspb.BinaryWriter): void;
  static deserializeBinary(bytes: Uint8Array): MonteCarloRequest;
  static deserializeBinaryFromReader(message: MonteCarloRequest, reader: jspb.BinaryReader): MonteCarloRequest;
}

export namespace MonteCarloRequest {
  export type AsObject = {
    spot: number,
    strike: number,
    rate: number,
    volatility: number,
    yearsToExpiry: number,
    paths: number,
    steps: number,
    optionType: OptionType,
    asianType: AsianType,
    numThreads: number,
  }
}

export class ProbabilityTreeRequest extends jspb.Message {
  getRate(): number;
  setRate(value: number): ProbabilityTreeRequest;

  getVolatility(): number;
  setVolatility(value: number): ProbabilityTreeRequest;

  getYearsToExpiry(): number;
  setYearsToExpiry(value: number): ProbabilityTreeRequest;

  getSteps(): number;
  setSteps(value: number): ProbabilityTreeRequest;

  getLambda(): number;
  setLambda(value: number): ProbabilityTreeRequest;

  serializeBinary(): Uint8Array;
  toObject(includeInstance?: boolean): ProbabilityTreeRequest.AsObject;
  static toObject(includeInstance: boolean, msg: ProbabilityTreeRequest): ProbabilityTreeRequest.AsObject;
  static serializeBinaryToWriter(message: ProbabilityTreeRequest, writer: jspb.BinaryWriter): void;
  static deserializeBinary(bytes: Uint8Array): ProbabilityTreeRequest;
  static deserializeBinaryFromReader(message: ProbabilityTreeRequest, reader: jspb.BinaryReader): ProbabilityTreeRequest;
}

export namespace ProbabilityTreeRequest {
  export type AsObject = {
    rate: number,
    volatility: number,
    yearsToExpiry: number,
    steps: number,
    lambda: number,
  }
}

export class ProbabilityTreeResponse extends jspb.Message {
  getStockPricesList(): Array<number>;
  setStockPricesList(value: Array<number>): ProbabilityTreeResponse;
  clearStockPricesList(): ProbabilityTreeResponse;
  addStockPrices(value: number, index?: number): ProbabilityTreeResponse;

  getStateProbabilitiesList(): Array<number>;
  setStateProbabilitiesList(value: Array<number>): ProbabilityTreeResponse;
  clearStateProbabilitiesList(): ProbabilityTreeResponse;
  addStateProbabilities(value: number, index?: number): ProbabilityTreeResponse;

  getSteps(): number;
  setSteps(value: number): ProbabilityTreeResponse;

  serializeBinary(): Uint8Array;
  toObject(includeInstance?: boolean): ProbabilityTreeResponse.AsObject;
  static toObject(includeInstance: boolean, msg: ProbabilityTreeResponse): ProbabilityTreeResponse.AsObject;
  static serializeBinaryToWriter(message: ProbabilityTreeResponse, writer: jspb.BinaryWriter): void;
  static deserializeBinary(bytes: Uint8Array): ProbabilityTreeResponse;
  static deserializeBinaryFromReader(message: ProbabilityTreeResponse, reader: jspb.BinaryReader): ProbabilityTreeResponse;
}

export namespace ProbabilityTreeResponse {
  export type AsObject = {
    stockPricesList: Array<number>,
    stateProbabilitiesList: Array<number>,
    steps: number,
  }
}

export class PortfolioStatsRequest extends jspb.Message {
  getPortfolioReturnsList(): Array<number>;
  setPortfolioReturnsList(value: Array<number>): PortfolioStatsRequest;
  clearPortfolioReturnsList(): PortfolioStatsRequest;
  addPortfolioReturns(value: number, index?: number): PortfolioStatsRequest;

  getMarketReturnsList(): Array<number>;
  setMarketReturnsList(value: Array<number>): PortfolioStatsRequest;
  clearMarketReturnsList(): PortfolioStatsRequest;
  addMarketReturns(value: number, index?: number): PortfolioStatsRequest;

  getRiskFreeRate(): number;
  setRiskFreeRate(value: number): PortfolioStatsRequest;

  serializeBinary(): Uint8Array;
  toObject(includeInstance?: boolean): PortfolioStatsRequest.AsObject;
  static toObject(includeInstance: boolean, msg: PortfolioStatsRequest): PortfolioStatsRequest.AsObject;
  static serializeBinaryToWriter(message: PortfolioStatsRequest, writer: jspb.BinaryWriter): void;
  static deserializeBinary(bytes: Uint8Array): PortfolioStatsRequest;
  static deserializeBinaryFromReader(message: PortfolioStatsRequest, reader: jspb.BinaryReader): PortfolioStatsRequest;
}

export namespace PortfolioStatsRequest {
  export type AsObject = {
    portfolioReturnsList: Array<number>,
    marketReturnsList: Array<number>,
    riskFreeRate: number,
  }
}

export class PortfolioStatsResponse extends jspb.Message {
  getSharpeRatio(): number;
  setSharpeRatio(value: number): PortfolioStatsResponse;

  getSortinoRatio(): number;
  setSortinoRatio(value: number): PortfolioStatsResponse;

  getTreynorRatio(): number;
  setTreynorRatio(value: number): PortfolioStatsResponse;

  getBeta(): number;
  setBeta(value: number): PortfolioStatsResponse;

  getAlpha(): number;
  setAlpha(value: number): PortfolioStatsResponse;

  getMaxDrawdown(): number;
  setMaxDrawdown(value: number): PortfolioStatsResponse;

  getVarHistorical95(): number;
  setVarHistorical95(value: number): PortfolioStatsResponse;

  getVarHistorical99(): number;
  setVarHistorical99(value: number): PortfolioStatsResponse;

  getCvarHistorical95(): number;
  setCvarHistorical95(value: number): PortfolioStatsResponse;

  getCvarHistorical99(): number;
  setCvarHistorical99(value: number): PortfolioStatsResponse;

  getVarParametric95(): number;
  setVarParametric95(value: number): PortfolioStatsResponse;

  getVarParametric99(): number;
  setVarParametric99(value: number): PortfolioStatsResponse;

  getCvarParametric95(): number;
  setCvarParametric95(value: number): PortfolioStatsResponse;

  getCvarParametric99(): number;
  setCvarParametric99(value: number): PortfolioStatsResponse;

  getOmegaRatio(): number;
  setOmegaRatio(value: number): PortfolioStatsResponse;

  getCalmarRatio(): number;
  setCalmarRatio(value: number): PortfolioStatsResponse;

  getInformationRatio(): number;
  setInformationRatio(value: number): PortfolioStatsResponse;

  getTrackingError(): number;
  setTrackingError(value: number): PortfolioStatsResponse;

  getBenchmarkSupplied(): boolean;
  setBenchmarkSupplied(value: boolean): PortfolioStatsResponse;

  serializeBinary(): Uint8Array;
  toObject(includeInstance?: boolean): PortfolioStatsResponse.AsObject;
  static toObject(includeInstance: boolean, msg: PortfolioStatsResponse): PortfolioStatsResponse.AsObject;
  static serializeBinaryToWriter(message: PortfolioStatsResponse, writer: jspb.BinaryWriter): void;
  static deserializeBinary(bytes: Uint8Array): PortfolioStatsResponse;
  static deserializeBinaryFromReader(message: PortfolioStatsResponse, reader: jspb.BinaryReader): PortfolioStatsResponse;
}

export namespace PortfolioStatsResponse {
  export type AsObject = {
    sharpeRatio: number,
    sortinoRatio: number,
    treynorRatio: number,
    beta: number,
    alpha: number,
    maxDrawdown: number,
    varHistorical95: number,
    varHistorical99: number,
    cvarHistorical95: number,
    cvarHistorical99: number,
    varParametric95: number,
    varParametric99: number,
    cvarParametric95: number,
    cvarParametric99: number,
    omegaRatio: number,
    calmarRatio: number,
    informationRatio: number,
    trackingError: number,
    benchmarkSupplied: boolean,
  }
}

export class PortfolioOptimizeRequest extends jspb.Message {
  getExpectedReturnsList(): Array<number>;
  setExpectedReturnsList(value: Array<number>): PortfolioOptimizeRequest;
  clearExpectedReturnsList(): PortfolioOptimizeRequest;
  addExpectedReturns(value: number, index?: number): PortfolioOptimizeRequest;

  getCovarianceList(): Array<number>;
  setCovarianceList(value: Array<number>): PortfolioOptimizeRequest;
  clearCovarianceList(): PortfolioOptimizeRequest;
  addCovariance(value: number, index?: number): PortfolioOptimizeRequest;

  getSize(): number;
  setSize(value: number): PortfolioOptimizeRequest;

  getRiskFreeRate(): number;
  setRiskFreeRate(value: number): PortfolioOptimizeRequest;

  getMaxSharpe(): boolean;
  setMaxSharpe(value: boolean): PortfolioOptimizeRequest;

  serializeBinary(): Uint8Array;
  toObject(includeInstance?: boolean): PortfolioOptimizeRequest.AsObject;
  static toObject(includeInstance: boolean, msg: PortfolioOptimizeRequest): PortfolioOptimizeRequest.AsObject;
  static serializeBinaryToWriter(message: PortfolioOptimizeRequest, writer: jspb.BinaryWriter): void;
  static deserializeBinary(bytes: Uint8Array): PortfolioOptimizeRequest;
  static deserializeBinaryFromReader(message: PortfolioOptimizeRequest, reader: jspb.BinaryReader): PortfolioOptimizeRequest;
}

export namespace PortfolioOptimizeRequest {
  export type AsObject = {
    expectedReturnsList: Array<number>,
    covarianceList: Array<number>,
    size: number,
    riskFreeRate: number,
    maxSharpe: boolean,
  }
}

export class PortfolioOptimizeResponse extends jspb.Message {
  getWeightsList(): Array<number>;
  setWeightsList(value: Array<number>): PortfolioOptimizeResponse;
  clearWeightsList(): PortfolioOptimizeResponse;
  addWeights(value: number, index?: number): PortfolioOptimizeResponse;

  getExpectedReturn(): number;
  setExpectedReturn(value: number): PortfolioOptimizeResponse;

  getVolatility(): number;
  setVolatility(value: number): PortfolioOptimizeResponse;

  getSharpeRatio(): number;
  setSharpeRatio(value: number): PortfolioOptimizeResponse;

  serializeBinary(): Uint8Array;
  toObject(includeInstance?: boolean): PortfolioOptimizeResponse.AsObject;
  static toObject(includeInstance: boolean, msg: PortfolioOptimizeResponse): PortfolioOptimizeResponse.AsObject;
  static serializeBinaryToWriter(message: PortfolioOptimizeResponse, writer: jspb.BinaryWriter): void;
  static deserializeBinary(bytes: Uint8Array): PortfolioOptimizeResponse;
  static deserializeBinaryFromReader(message: PortfolioOptimizeResponse, reader: jspb.BinaryReader): PortfolioOptimizeResponse;
}

export namespace PortfolioOptimizeResponse {
  export type AsObject = {
    weightsList: Array<number>,
    expectedReturn: number,
    volatility: number,
    sharpeRatio: number,
  }
}

export class RiskContributionRequest extends jspb.Message {
  getWeightsList(): Array<number>;
  setWeightsList(value: Array<number>): RiskContributionRequest;
  clearWeightsList(): RiskContributionRequest;
  addWeights(value: number, index?: number): RiskContributionRequest;

  getCovarianceList(): Array<number>;
  setCovarianceList(value: Array<number>): RiskContributionRequest;
  clearCovarianceList(): RiskContributionRequest;
  addCovariance(value: number, index?: number): RiskContributionRequest;

  getSize(): number;
  setSize(value: number): RiskContributionRequest;

  serializeBinary(): Uint8Array;
  toObject(includeInstance?: boolean): RiskContributionRequest.AsObject;
  static toObject(includeInstance: boolean, msg: RiskContributionRequest): RiskContributionRequest.AsObject;
  static serializeBinaryToWriter(message: RiskContributionRequest, writer: jspb.BinaryWriter): void;
  static deserializeBinary(bytes: Uint8Array): RiskContributionRequest;
  static deserializeBinaryFromReader(message: RiskContributionRequest, reader: jspb.BinaryReader): RiskContributionRequest;
}

export namespace RiskContributionRequest {
  export type AsObject = {
    weightsList: Array<number>,
    covarianceList: Array<number>,
    size: number,
  }
}

export class RiskContributionResponse extends jspb.Message {
  getContributionsList(): Array<number>;
  setContributionsList(value: Array<number>): RiskContributionResponse;
  clearContributionsList(): RiskContributionResponse;
  addContributions(value: number, index?: number): RiskContributionResponse;

  serializeBinary(): Uint8Array;
  toObject(includeInstance?: boolean): RiskContributionResponse.AsObject;
  static toObject(includeInstance: boolean, msg: RiskContributionResponse): RiskContributionResponse.AsObject;
  static serializeBinaryToWriter(message: RiskContributionResponse, writer: jspb.BinaryWriter): void;
  static deserializeBinary(bytes: Uint8Array): RiskContributionResponse;
  static deserializeBinaryFromReader(message: RiskContributionResponse, reader: jspb.BinaryReader): RiskContributionResponse;
}

export namespace RiskContributionResponse {
  export type AsObject = {
    contributionsList: Array<number>,
  }
}

export class ClosingCostsRequest extends jspb.Message {
  getHomePrice(): string;
  setHomePrice(value: string): ClosingCostsRequest;

  getDownPaymentPercent(): string;
  setDownPaymentPercent(value: string): ClosingCostsRequest;

  getAnnualRate(): string;
  setAnnualRate(value: string): ClosingCostsRequest;

  getOriginationFeePercent(): string;
  setOriginationFeePercent(value: string): ClosingCostsRequest;

  getDiscountPointsPercent(): string;
  setDiscountPointsPercent(value: string): ClosingCostsRequest;

  getOtherLenderFees(): string;
  setOtherLenderFees(value: string): ClosingCostsRequest;

  getTitleSettlementPercent(): string;
  setTitleSettlementPercent(value: string): ClosingCostsRequest;

  getAppraisalFee(): string;
  setAppraisalFee(value: string): ClosingCostsRequest;

  getInspectionFee(): string;
  setInspectionFee(value: string): ClosingCostsRequest;

  getRecordingFees(): string;
  setRecordingFees(value: string): ClosingCostsRequest;

  getTransferTaxPercent(): string;
  setTransferTaxPercent(value: string): ClosingCostsRequest;

  getHomeownersInsuranceAnnual(): string;
  setHomeownersInsuranceAnnual(value: string): ClosingCostsRequest;

  getPropertyTaxAnnual(): string;
  setPropertyTaxAnnual(value: string): ClosingCostsRequest;

  getTaxEscrowMonths(): number;
  setTaxEscrowMonths(value: number): ClosingCostsRequest;

  getSellerLenderCredits(): string;
  setSellerLenderCredits(value: string): ClosingCostsRequest;

  getPrepaidInterestDays(): number;
  setPrepaidInterestDays(value: number): ClosingCostsRequest;
  hasPrepaidInterestDays(): boolean;
  clearPrepaidInterestDays(): ClosingCostsRequest;

  serializeBinary(): Uint8Array;
  toObject(includeInstance?: boolean): ClosingCostsRequest.AsObject;
  static toObject(includeInstance: boolean, msg: ClosingCostsRequest): ClosingCostsRequest.AsObject;
  static serializeBinaryToWriter(message: ClosingCostsRequest, writer: jspb.BinaryWriter): void;
  static deserializeBinary(bytes: Uint8Array): ClosingCostsRequest;
  static deserializeBinaryFromReader(message: ClosingCostsRequest, reader: jspb.BinaryReader): ClosingCostsRequest;
}

export namespace ClosingCostsRequest {
  export type AsObject = {
    homePrice: string,
    downPaymentPercent: string,
    annualRate: string,
    originationFeePercent: string,
    discountPointsPercent: string,
    otherLenderFees: string,
    titleSettlementPercent: string,
    appraisalFee: string,
    inspectionFee: string,
    recordingFees: string,
    transferTaxPercent: string,
    homeownersInsuranceAnnual: string,
    propertyTaxAnnual: string,
    taxEscrowMonths: number,
    sellerLenderCredits: string,
    prepaidInterestDays?: number,
  }

  export enum PrepaidInterestDaysCase { 
    _PREPAID_INTEREST_DAYS_NOT_SET = 0,
    PREPAID_INTEREST_DAYS = 16,
  }
}

export class ClosingCostsResponse extends jspb.Message {
  getOriginationFee(): string;
  setOriginationFee(value: string): ClosingCostsResponse;

  getDiscountPoints(): string;
  setDiscountPoints(value: string): ClosingCostsResponse;

  getOtherLenderFees(): string;
  setOtherLenderFees(value: string): ClosingCostsResponse;

  getTitleSettlement(): string;
  setTitleSettlement(value: string): ClosingCostsResponse;

  getAppraisalFee(): string;
  setAppraisalFee(value: string): ClosingCostsResponse;

  getInspectionFee(): string;
  setInspectionFee(value: string): ClosingCostsResponse;

  getRecordingFees(): string;
  setRecordingFees(value: string): ClosingCostsResponse;

  getTransferTax(): string;
  setTransferTax(value: string): ClosingCostsResponse;

  getHomeownersInsurancePrepaid(): string;
  setHomeownersInsurancePrepaid(value: string): ClosingCostsResponse;

  getPropertyTaxEscrow(): string;
  setPropertyTaxEscrow(value: string): ClosingCostsResponse;

  getPrepaidInterest(): string;
  setPrepaidInterest(value: string): ClosingCostsResponse;

  getPrepaidInterestDays(): number;
  setPrepaidInterestDays(value: number): ClosingCostsResponse;

  getItemisedSubtotal(): string;
  setItemisedSubtotal(value: string): ClosingCostsResponse;

  getSellerLenderCredits(): string;
  setSellerLenderCredits(value: string): ClosingCostsResponse;

  getTotalClosingCosts(): string;
  setTotalClosingCosts(value: string): ClosingCostsResponse;

  getLoanAmount(): string;
  setLoanAmount(value: string): ClosingCostsResponse;

  getDownPayment(): string;
  setDownPayment(value: string): ClosingCostsResponse;

  getTotalCashToClose(): string;
  setTotalCashToClose(value: string): ClosingCostsResponse;

  getClosingCostsPercentOfPrice(): number;
  setClosingCostsPercentOfPrice(value: number): ClosingCostsResponse;

  serializeBinary(): Uint8Array;
  toObject(includeInstance?: boolean): ClosingCostsResponse.AsObject;
  static toObject(includeInstance: boolean, msg: ClosingCostsResponse): ClosingCostsResponse.AsObject;
  static serializeBinaryToWriter(message: ClosingCostsResponse, writer: jspb.BinaryWriter): void;
  static deserializeBinary(bytes: Uint8Array): ClosingCostsResponse;
  static deserializeBinaryFromReader(message: ClosingCostsResponse, reader: jspb.BinaryReader): ClosingCostsResponse;
}

export namespace ClosingCostsResponse {
  export type AsObject = {
    originationFee: string,
    discountPoints: string,
    otherLenderFees: string,
    titleSettlement: string,
    appraisalFee: string,
    inspectionFee: string,
    recordingFees: string,
    transferTax: string,
    homeownersInsurancePrepaid: string,
    propertyTaxEscrow: string,
    prepaidInterest: string,
    prepaidInterestDays: number,
    itemisedSubtotal: string,
    sellerLenderCredits: string,
    totalClosingCosts: string,
    loanAmount: string,
    downPayment: string,
    totalCashToClose: string,
    closingCostsPercentOfPrice: number,
  }
}

export class RefreshStateAssumptionsRequest extends jspb.Message {
  getDryRun(): boolean;
  setDryRun(value: boolean): RefreshStateAssumptionsRequest;

  getDataYear(): number;
  setDataYear(value: number): RefreshStateAssumptionsRequest;

  serializeBinary(): Uint8Array;
  toObject(includeInstance?: boolean): RefreshStateAssumptionsRequest.AsObject;
  static toObject(includeInstance: boolean, msg: RefreshStateAssumptionsRequest): RefreshStateAssumptionsRequest.AsObject;
  static serializeBinaryToWriter(message: RefreshStateAssumptionsRequest, writer: jspb.BinaryWriter): void;
  static deserializeBinary(bytes: Uint8Array): RefreshStateAssumptionsRequest;
  static deserializeBinaryFromReader(message: RefreshStateAssumptionsRequest, reader: jspb.BinaryReader): RefreshStateAssumptionsRequest;
}

export namespace RefreshStateAssumptionsRequest {
  export type AsObject = {
    dryRun: boolean,
    dataYear: number,
  }
}

export class RefreshStateAssumptionsResponse extends jspb.Message {
  getOk(): boolean;
  setOk(value: boolean): RefreshStateAssumptionsResponse;

  getDataYear(): number;
  setDataYear(value: number): RefreshStateAssumptionsResponse;

  getStatesUpdated(): number;
  setStatesUpdated(value: number): RefreshStateAssumptionsResponse;

  getStatesRejected(): number;
  setStatesRejected(value: number): RefreshStateAssumptionsResponse;

  getDataSource(): string;
  setDataSource(value: string): RefreshStateAssumptionsResponse;

  getError(): string;
  setError(value: string): RefreshStateAssumptionsResponse;

  serializeBinary(): Uint8Array;
  toObject(includeInstance?: boolean): RefreshStateAssumptionsResponse.AsObject;
  static toObject(includeInstance: boolean, msg: RefreshStateAssumptionsResponse): RefreshStateAssumptionsResponse.AsObject;
  static serializeBinaryToWriter(message: RefreshStateAssumptionsResponse, writer: jspb.BinaryWriter): void;
  static deserializeBinary(bytes: Uint8Array): RefreshStateAssumptionsResponse;
  static deserializeBinaryFromReader(message: RefreshStateAssumptionsResponse, reader: jspb.BinaryReader): RefreshStateAssumptionsResponse;
}

export namespace RefreshStateAssumptionsResponse {
  export type AsObject = {
    ok: boolean,
    dataYear: number,
    statesUpdated: number,
    statesRejected: number,
    dataSource: string,
    error: string,
  }
}

export class GetStateAssumptionsRequest extends jspb.Message {
  getSlug(): string;
  setSlug(value: string): GetStateAssumptionsRequest;

  serializeBinary(): Uint8Array;
  toObject(includeInstance?: boolean): GetStateAssumptionsRequest.AsObject;
  static toObject(includeInstance: boolean, msg: GetStateAssumptionsRequest): GetStateAssumptionsRequest.AsObject;
  static serializeBinaryToWriter(message: GetStateAssumptionsRequest, writer: jspb.BinaryWriter): void;
  static deserializeBinary(bytes: Uint8Array): GetStateAssumptionsRequest;
  static deserializeBinaryFromReader(message: GetStateAssumptionsRequest, reader: jspb.BinaryReader): GetStateAssumptionsRequest;
}

export namespace GetStateAssumptionsRequest {
  export type AsObject = {
    slug: string,
  }
}

export class StateAssumption extends jspb.Message {
  getSlug(): string;
  setSlug(value: string): StateAssumption;

  getName(): string;
  setName(value: string): StateAssumption;

  getAbbr(): string;
  setAbbr(value: string): StateAssumption;

  getMedianPrice(): string;
  setMedianPrice(value: string): StateAssumption;

  getPropertyTaxRate(): string;
  setPropertyTaxRate(value: string): StateAssumption;

  getInsuranceAnnual(): string;
  setInsuranceAnnual(value: string): StateAssumption;

  getStateIncomeTax(): string;
  setStateIncomeTax(value: string): StateAssumption;

  getMedianRent(): string;
  setMedianRent(value: string): StateAssumption;

  getNote(): string;
  setNote(value: string): StateAssumption;

  getDataSource(): string;
  setDataSource(value: string): StateAssumption;

  getDataYear(): number;
  setDataYear(value: number): StateAssumption;

  getRefreshedAt(): string;
  setRefreshedAt(value: string): StateAssumption;

  serializeBinary(): Uint8Array;
  toObject(includeInstance?: boolean): StateAssumption.AsObject;
  static toObject(includeInstance: boolean, msg: StateAssumption): StateAssumption.AsObject;
  static serializeBinaryToWriter(message: StateAssumption, writer: jspb.BinaryWriter): void;
  static deserializeBinary(bytes: Uint8Array): StateAssumption;
  static deserializeBinaryFromReader(message: StateAssumption, reader: jspb.BinaryReader): StateAssumption;
}

export namespace StateAssumption {
  export type AsObject = {
    slug: string,
    name: string,
    abbr: string,
    medianPrice: string,
    propertyTaxRate: string,
    insuranceAnnual: string,
    stateIncomeTax: string,
    medianRent: string,
    note: string,
    dataSource: string,
    dataYear: number,
    refreshedAt: string,
  }
}

export class GetStateAssumptionsResponse extends jspb.Message {
  getStatesList(): Array<StateAssumption>;
  setStatesList(value: Array<StateAssumption>): GetStateAssumptionsResponse;
  clearStatesList(): GetStateAssumptionsResponse;
  addStates(value?: StateAssumption, index?: number): StateAssumption;

  serializeBinary(): Uint8Array;
  toObject(includeInstance?: boolean): GetStateAssumptionsResponse.AsObject;
  static toObject(includeInstance: boolean, msg: GetStateAssumptionsResponse): GetStateAssumptionsResponse.AsObject;
  static serializeBinaryToWriter(message: GetStateAssumptionsResponse, writer: jspb.BinaryWriter): void;
  static deserializeBinary(bytes: Uint8Array): GetStateAssumptionsResponse;
  static deserializeBinaryFromReader(message: GetStateAssumptionsResponse, reader: jspb.BinaryReader): GetStateAssumptionsResponse;
}

export namespace GetStateAssumptionsResponse {
  export type AsObject = {
    statesList: Array<StateAssumption.AsObject>,
  }
}

export enum AnnuityTiming { 
  END_OF_PERIOD = 0,
  BEGINNING_OF_PERIOD = 1,
}
export enum OptionType { 
  CALL = 0,
  PUT = 1,
}
export enum ExerciseType { 
  EUROPEAN = 0,
  AMERICAN = 1,
  BERMUDAN = 2,
}
export enum AsianType { 
  NOT_ASIAN = 0,
  AVERAGE_PRICE = 1,
  AVERAGE_STRIKE = 2,
}
