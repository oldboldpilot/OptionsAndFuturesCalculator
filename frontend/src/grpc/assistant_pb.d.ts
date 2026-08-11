import * as jspb from 'google-protobuf'

import * as finance_pb from './finance_pb'; // proto import: "finance.proto"


export class ParseRequest extends jspb.Message {
  getUtterance(): string;
  setUtterance(value: string): ParseRequest;

  getPriorClarification(): string;
  setPriorClarification(value: string): ParseRequest;

  serializeBinary(): Uint8Array;
  toObject(includeInstance?: boolean): ParseRequest.AsObject;
  static toObject(includeInstance: boolean, msg: ParseRequest): ParseRequest.AsObject;
  static serializeBinaryToWriter(message: ParseRequest, writer: jspb.BinaryWriter): void;
  static deserializeBinary(bytes: Uint8Array): ParseRequest;
  static deserializeBinaryFromReader(message: ParseRequest, reader: jspb.BinaryReader): ParseRequest;
}

export namespace ParseRequest {
  export type AsObject = {
    utterance: string,
    priorClarification: string,
  }
}

export class ParseResponse extends jspb.Message {
  getParams(): StrategyParams | undefined;
  setParams(value?: StrategyParams): ParseResponse;
  hasParams(): boolean;
  clearParams(): ParseResponse;

  getClarification(): Clarification | undefined;
  setClarification(value?: Clarification): ParseResponse;
  hasClarification(): boolean;
  clearClarification(): ParseResponse;

  getRefusal(): Refusal | undefined;
  setRefusal(value?: Refusal): ParseResponse;
  hasRefusal(): boolean;
  clearRefusal(): ParseResponse;

  getOutcomeCase(): ParseResponse.OutcomeCase;

  serializeBinary(): Uint8Array;
  toObject(includeInstance?: boolean): ParseResponse.AsObject;
  static toObject(includeInstance: boolean, msg: ParseResponse): ParseResponse.AsObject;
  static serializeBinaryToWriter(message: ParseResponse, writer: jspb.BinaryWriter): void;
  static deserializeBinary(bytes: Uint8Array): ParseResponse;
  static deserializeBinaryFromReader(message: ParseResponse, reader: jspb.BinaryReader): ParseResponse;
}

export namespace ParseResponse {
  export type AsObject = {
    params?: StrategyParams.AsObject,
    clarification?: Clarification.AsObject,
    refusal?: Refusal.AsObject,
  }

  export enum OutcomeCase { 
    OUTCOME_NOT_SET = 0,
    PARAMS = 1,
    CLARIFICATION = 2,
    REFUSAL = 3,
  }
}

export class StrategyParams extends jspb.Message {
  getSymbol(): string;
  setSymbol(value: string): StrategyParams;

  getAssetClass(): string;
  setAssetClass(value: string): StrategyParams;

  getStrategy(): string;
  setStrategy(value: string): StrategyParams;

  getExpirationDays(): number;
  setExpirationDays(value: number): StrategyParams;

  getQuantity(): number;
  setQuantity(value: number): StrategyParams;

  getFarExpirationDays(): number;
  setFarExpirationDays(value: number): StrategyParams;

  getExerciseType(): finance_pb.ExerciseType;
  setExerciseType(value: finance_pb.ExerciseType): StrategyParams;

  getAsianType(): finance_pb.AsianType;
  setAsianType(value: finance_pb.AsianType): StrategyParams;

  serializeBinary(): Uint8Array;
  toObject(includeInstance?: boolean): StrategyParams.AsObject;
  static toObject(includeInstance: boolean, msg: StrategyParams): StrategyParams.AsObject;
  static serializeBinaryToWriter(message: StrategyParams, writer: jspb.BinaryWriter): void;
  static deserializeBinary(bytes: Uint8Array): StrategyParams;
  static deserializeBinaryFromReader(message: StrategyParams, reader: jspb.BinaryReader): StrategyParams;
}

export namespace StrategyParams {
  export type AsObject = {
    symbol: string,
    assetClass: string,
    strategy: string,
    expirationDays: number,
    quantity: number,
    farExpirationDays: number,
    exerciseType: finance_pb.ExerciseType,
    asianType: finance_pb.AsianType,
  }
}

export class Clarification extends jspb.Message {
  getQuestion(): string;
  setQuestion(value: string): Clarification;

  serializeBinary(): Uint8Array;
  toObject(includeInstance?: boolean): Clarification.AsObject;
  static toObject(includeInstance: boolean, msg: Clarification): Clarification.AsObject;
  static serializeBinaryToWriter(message: Clarification, writer: jspb.BinaryWriter): void;
  static deserializeBinary(bytes: Uint8Array): Clarification;
  static deserializeBinaryFromReader(message: Clarification, reader: jspb.BinaryReader): Clarification;
}

export namespace Clarification {
  export type AsObject = {
    question: string,
  }
}

export class Refusal extends jspb.Message {
  getReason(): Refusal.Reason;
  setReason(value: Refusal.Reason): Refusal;

  getMessage(): string;
  setMessage(value: string): Refusal;

  serializeBinary(): Uint8Array;
  toObject(includeInstance?: boolean): Refusal.AsObject;
  static toObject(includeInstance: boolean, msg: Refusal): Refusal.AsObject;
  static serializeBinaryToWriter(message: Refusal, writer: jspb.BinaryWriter): void;
  static deserializeBinary(bytes: Uint8Array): Refusal;
  static deserializeBinaryFromReader(message: Refusal, reader: jspb.BinaryReader): Refusal;
}

export namespace Refusal {
  export type AsObject = {
    reason: Refusal.Reason,
    message: string,
  }

  export enum Reason { 
    REASON_UNSPECIFIED = 0,
    UNSUPPORTED_STRATEGY = 1,
    UNKNOWN_SYMBOL = 2,
    OUT_OF_SCOPE = 3,
    MODEL_UNAVAILABLE = 4,
    DATA_UNAVAILABLE = 5,
  }
}

