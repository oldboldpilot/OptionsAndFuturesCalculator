// source: finance.proto
/**
 * @fileoverview
 * @enhanceable
 * @suppress {missingRequire} reports error on implicit type usages.
 * @suppress {messageConventions} JS Compiler reports an error if a variable or
 *     field starts with 'MSG_' and isn't a translatable message.
 * @public
 */
// GENERATED CODE -- DO NOT EDIT!
/* eslint-disable */
// @ts-nocheck

var jspb = require('google-protobuf');
var goog = jspb;
var global = (function() {
  if (this) { return this; }
  if (typeof window !== 'undefined') { return window; }
  if (typeof global !== 'undefined') { return global; }
  if (typeof self !== 'undefined') { return self; }
  return Function('return this')();
}.call(null));

goog.exportSymbol('proto.sensen.finance.AmortizationBatchRequest', null, global);
goog.exportSymbol('proto.sensen.finance.AmortizationBatchResponse', null, global);
goog.exportSymbol('proto.sensen.finance.AmortizationRequest', null, global);
goog.exportSymbol('proto.sensen.finance.AmortizationResponse', null, global);
goog.exportSymbol('proto.sensen.finance.AmortizationRow', null, global);
goog.exportSymbol('proto.sensen.finance.AnnuityTiming', null, global);
goog.exportSymbol('proto.sensen.finance.AsianType', null, global);
goog.exportSymbol('proto.sensen.finance.BlackScholesRequest', null, global);
goog.exportSymbol('proto.sensen.finance.BlackScholesResponse', null, global);
goog.exportSymbol('proto.sensen.finance.BondRequest', null, global);
goog.exportSymbol('proto.sensen.finance.BondRequest.KnownCase', null, global);
goog.exportSymbol('proto.sensen.finance.BondResponse', null, global);
goog.exportSymbol('proto.sensen.finance.ClosingCostsRequest', null, global);
goog.exportSymbol('proto.sensen.finance.ClosingCostsResponse', null, global);
goog.exportSymbol('proto.sensen.finance.CommoditySpreadRequest', null, global);
goog.exportSymbol('proto.sensen.finance.CommoditySpreadRequest.Spread', null, global);
goog.exportSymbol('proto.sensen.finance.CumulativeRequest', null, global);
goog.exportSymbol('proto.sensen.finance.CumulativeRequest.Component', null, global);
goog.exportSymbol('proto.sensen.finance.DatedCashFlowRequest', null, global);
goog.exportSymbol('proto.sensen.finance.DecimalResponse', null, global);
goog.exportSymbol('proto.sensen.finance.DepreciationRequest', null, global);
goog.exportSymbol('proto.sensen.finance.DepreciationRequest.Method', null, global);
goog.exportSymbol('proto.sensen.finance.DetailedAmortizationRequest', null, global);
goog.exportSymbol('proto.sensen.finance.DetailedAmortizationResponse', null, global);
goog.exportSymbol('proto.sensen.finance.DetailedAmortizationRow', null, global);
goog.exportSymbol('proto.sensen.finance.DetailedMortgageSummary', null, global);
goog.exportSymbol('proto.sensen.finance.DoubleResponse', null, global);
goog.exportSymbol('proto.sensen.finance.ExerciseType', null, global);
goog.exportSymbol('proto.sensen.finance.FisherRequest', null, global);
goog.exportSymbol('proto.sensen.finance.FisherRequest.Direction', null, global);
goog.exportSymbol('proto.sensen.finance.FutureValueDetailedRequest', null, global);
goog.exportSymbol('proto.sensen.finance.FutureValueDetailedResponse', null, global);
goog.exportSymbol('proto.sensen.finance.FutureValueRequest', null, global);
goog.exportSymbol('proto.sensen.finance.FuturesPricingRequest', null, global);
goog.exportSymbol('proto.sensen.finance.FuturesValuationRequest', null, global);
goog.exportSymbol('proto.sensen.finance.HedgeRequest', null, global);
goog.exportSymbol('proto.sensen.finance.HedgeResponse', null, global);
goog.exportSymbol('proto.sensen.finance.HelocRequest', null, global);
goog.exportSymbol('proto.sensen.finance.HelocResponse', null, global);
goog.exportSymbol('proto.sensen.finance.HomeFutureValueRequest', null, global);
goog.exportSymbol('proto.sensen.finance.HomeFutureValueResponse', null, global);
goog.exportSymbol('proto.sensen.finance.HomeNpvRequest', null, global);
goog.exportSymbol('proto.sensen.finance.HomeNpvResponse', null, global);
goog.exportSymbol('proto.sensen.finance.IrrRequest', null, global);
goog.exportSymbol('proto.sensen.finance.MarginSimulationRequest', null, global);
goog.exportSymbol('proto.sensen.finance.MarginSimulationResponse', null, global);
goog.exportSymbol('proto.sensen.finance.MonteCarloRequest', null, global);
goog.exportSymbol('proto.sensen.finance.MortgageRecastRequest', null, global);
goog.exportSymbol('proto.sensen.finance.MortgageRecastResponse', null, global);
goog.exportSymbol('proto.sensen.finance.MortgageSummary', null, global);
goog.exportSymbol('proto.sensen.finance.NpvRequest', null, global);
goog.exportSymbol('proto.sensen.finance.OptionPricingResponse', null, global);
goog.exportSymbol('proto.sensen.finance.OptionTreeRequest', null, global);
goog.exportSymbol('proto.sensen.finance.OptionType', null, global);
goog.exportSymbol('proto.sensen.finance.PaybackRequest', null, global);
goog.exportSymbol('proto.sensen.finance.PaymentRequest', null, global);
goog.exportSymbol('proto.sensen.finance.PayoffTimingRequest', null, global);
goog.exportSymbol('proto.sensen.finance.PayoffTimingResponse', null, global);
goog.exportSymbol('proto.sensen.finance.PeriodPaymentRequest', null, global);
goog.exportSymbol('proto.sensen.finance.PeriodsRequest', null, global);
goog.exportSymbol('proto.sensen.finance.PortfolioOptimizeRequest', null, global);
goog.exportSymbol('proto.sensen.finance.PortfolioOptimizeResponse', null, global);
goog.exportSymbol('proto.sensen.finance.PortfolioStatsRequest', null, global);
goog.exportSymbol('proto.sensen.finance.PortfolioStatsResponse', null, global);
goog.exportSymbol('proto.sensen.finance.PresentValueRequest', null, global);
goog.exportSymbol('proto.sensen.finance.ProbabilityTreeRequest', null, global);
goog.exportSymbol('proto.sensen.finance.ProbabilityTreeResponse', null, global);
goog.exportSymbol('proto.sensen.finance.RateConversionRequest', null, global);
goog.exportSymbol('proto.sensen.finance.RateConversionRequest.Direction', null, global);
goog.exportSymbol('proto.sensen.finance.RateRequest', null, global);
goog.exportSymbol('proto.sensen.finance.RefinanceRequest', null, global);
goog.exportSymbol('proto.sensen.finance.RefinanceRequest.ClosingCostType', null, global);
goog.exportSymbol('proto.sensen.finance.RefinanceResponse', null, global);
goog.exportSymbol('proto.sensen.finance.RentVsBuyRequest', null, global);
goog.exportSymbol('proto.sensen.finance.RentVsBuyResponse', null, global);
goog.exportSymbol('proto.sensen.finance.RentalRoiRequest', null, global);
goog.exportSymbol('proto.sensen.finance.RentalRoiResponse', null, global);
goog.exportSymbol('proto.sensen.finance.RiskContributionRequest', null, global);
goog.exportSymbol('proto.sensen.finance.RiskContributionResponse', null, global);
goog.exportSymbol('proto.sensen.finance.TreasuryBillRequest', null, global);
goog.exportSymbol('proto.sensen.finance.TreasuryBillRequest.KnownCase', null, global);
goog.exportSymbol('proto.sensen.finance.TreasuryBillResponse', null, global);
/**
 * Generated by JsPbCodeGenerator.
 * @param {Array=} opt_data Optional initial data array, typically from a
 * server response, or constructed directly in Javascript. The array is used
 * in place and becomes part of the constructed object. It is not cloned.
 * If no data is provided, the constructed object will be empty, but still
 * valid.
 * @extends {jspb.Message}
 * @constructor
 */
proto.sensen.finance.DecimalResponse = function(opt_data) {
  jspb.Message.initialize(this, opt_data, 0, -1, null, null);
};
goog.inherits(proto.sensen.finance.DecimalResponse, jspb.Message);
if (goog.DEBUG && !COMPILED) {
  /**
   * @public
   * @override
   */
  proto.sensen.finance.DecimalResponse.displayName = 'proto.sensen.finance.DecimalResponse';
}
/**
 * Generated by JsPbCodeGenerator.
 * @param {Array=} opt_data Optional initial data array, typically from a
 * server response, or constructed directly in Javascript. The array is used
 * in place and becomes part of the constructed object. It is not cloned.
 * If no data is provided, the constructed object will be empty, but still
 * valid.
 * @extends {jspb.Message}
 * @constructor
 */
proto.sensen.finance.DoubleResponse = function(opt_data) {
  jspb.Message.initialize(this, opt_data, 0, -1, null, null);
};
goog.inherits(proto.sensen.finance.DoubleResponse, jspb.Message);
if (goog.DEBUG && !COMPILED) {
  /**
   * @public
   * @override
   */
  proto.sensen.finance.DoubleResponse.displayName = 'proto.sensen.finance.DoubleResponse';
}
/**
 * Generated by JsPbCodeGenerator.
 * @param {Array=} opt_data Optional initial data array, typically from a
 * server response, or constructed directly in Javascript. The array is used
 * in place and becomes part of the constructed object. It is not cloned.
 * If no data is provided, the constructed object will be empty, but still
 * valid.
 * @extends {jspb.Message}
 * @constructor
 */
proto.sensen.finance.PaymentRequest = function(opt_data) {
  jspb.Message.initialize(this, opt_data, 0, -1, null, null);
};
goog.inherits(proto.sensen.finance.PaymentRequest, jspb.Message);
if (goog.DEBUG && !COMPILED) {
  /**
   * @public
   * @override
   */
  proto.sensen.finance.PaymentRequest.displayName = 'proto.sensen.finance.PaymentRequest';
}
/**
 * Generated by JsPbCodeGenerator.
 * @param {Array=} opt_data Optional initial data array, typically from a
 * server response, or constructed directly in Javascript. The array is used
 * in place and becomes part of the constructed object. It is not cloned.
 * If no data is provided, the constructed object will be empty, but still
 * valid.
 * @extends {jspb.Message}
 * @constructor
 */
proto.sensen.finance.PresentValueRequest = function(opt_data) {
  jspb.Message.initialize(this, opt_data, 0, -1, null, null);
};
goog.inherits(proto.sensen.finance.PresentValueRequest, jspb.Message);
if (goog.DEBUG && !COMPILED) {
  /**
   * @public
   * @override
   */
  proto.sensen.finance.PresentValueRequest.displayName = 'proto.sensen.finance.PresentValueRequest';
}
/**
 * Generated by JsPbCodeGenerator.
 * @param {Array=} opt_data Optional initial data array, typically from a
 * server response, or constructed directly in Javascript. The array is used
 * in place and becomes part of the constructed object. It is not cloned.
 * If no data is provided, the constructed object will be empty, but still
 * valid.
 * @extends {jspb.Message}
 * @constructor
 */
proto.sensen.finance.FutureValueRequest = function(opt_data) {
  jspb.Message.initialize(this, opt_data, 0, -1, null, null);
};
goog.inherits(proto.sensen.finance.FutureValueRequest, jspb.Message);
if (goog.DEBUG && !COMPILED) {
  /**
   * @public
   * @override
   */
  proto.sensen.finance.FutureValueRequest.displayName = 'proto.sensen.finance.FutureValueRequest';
}
/**
 * Generated by JsPbCodeGenerator.
 * @param {Array=} opt_data Optional initial data array, typically from a
 * server response, or constructed directly in Javascript. The array is used
 * in place and becomes part of the constructed object. It is not cloned.
 * If no data is provided, the constructed object will be empty, but still
 * valid.
 * @extends {jspb.Message}
 * @constructor
 */
proto.sensen.finance.FutureValueDetailedRequest = function(opt_data) {
  jspb.Message.initialize(this, opt_data, 0, -1, null, null);
};
goog.inherits(proto.sensen.finance.FutureValueDetailedRequest, jspb.Message);
if (goog.DEBUG && !COMPILED) {
  /**
   * @public
   * @override
   */
  proto.sensen.finance.FutureValueDetailedRequest.displayName = 'proto.sensen.finance.FutureValueDetailedRequest';
}
/**
 * Generated by JsPbCodeGenerator.
 * @param {Array=} opt_data Optional initial data array, typically from a
 * server response, or constructed directly in Javascript. The array is used
 * in place and becomes part of the constructed object. It is not cloned.
 * If no data is provided, the constructed object will be empty, but still
 * valid.
 * @extends {jspb.Message}
 * @constructor
 */
proto.sensen.finance.FutureValueDetailedResponse = function(opt_data) {
  jspb.Message.initialize(this, opt_data, 0, -1, null, null);
};
goog.inherits(proto.sensen.finance.FutureValueDetailedResponse, jspb.Message);
if (goog.DEBUG && !COMPILED) {
  /**
   * @public
   * @override
   */
  proto.sensen.finance.FutureValueDetailedResponse.displayName = 'proto.sensen.finance.FutureValueDetailedResponse';
}
/**
 * Generated by JsPbCodeGenerator.
 * @param {Array=} opt_data Optional initial data array, typically from a
 * server response, or constructed directly in Javascript. The array is used
 * in place and becomes part of the constructed object. It is not cloned.
 * If no data is provided, the constructed object will be empty, but still
 * valid.
 * @extends {jspb.Message}
 * @constructor
 */
proto.sensen.finance.PeriodPaymentRequest = function(opt_data) {
  jspb.Message.initialize(this, opt_data, 0, -1, null, null);
};
goog.inherits(proto.sensen.finance.PeriodPaymentRequest, jspb.Message);
if (goog.DEBUG && !COMPILED) {
  /**
   * @public
   * @override
   */
  proto.sensen.finance.PeriodPaymentRequest.displayName = 'proto.sensen.finance.PeriodPaymentRequest';
}
/**
 * Generated by JsPbCodeGenerator.
 * @param {Array=} opt_data Optional initial data array, typically from a
 * server response, or constructed directly in Javascript. The array is used
 * in place and becomes part of the constructed object. It is not cloned.
 * If no data is provided, the constructed object will be empty, but still
 * valid.
 * @extends {jspb.Message}
 * @constructor
 */
proto.sensen.finance.RateRequest = function(opt_data) {
  jspb.Message.initialize(this, opt_data, 0, -1, null, null);
};
goog.inherits(proto.sensen.finance.RateRequest, jspb.Message);
if (goog.DEBUG && !COMPILED) {
  /**
   * @public
   * @override
   */
  proto.sensen.finance.RateRequest.displayName = 'proto.sensen.finance.RateRequest';
}
/**
 * Generated by JsPbCodeGenerator.
 * @param {Array=} opt_data Optional initial data array, typically from a
 * server response, or constructed directly in Javascript. The array is used
 * in place and becomes part of the constructed object. It is not cloned.
 * If no data is provided, the constructed object will be empty, but still
 * valid.
 * @extends {jspb.Message}
 * @constructor
 */
proto.sensen.finance.PeriodsRequest = function(opt_data) {
  jspb.Message.initialize(this, opt_data, 0, -1, null, null);
};
goog.inherits(proto.sensen.finance.PeriodsRequest, jspb.Message);
if (goog.DEBUG && !COMPILED) {
  /**
   * @public
   * @override
   */
  proto.sensen.finance.PeriodsRequest.displayName = 'proto.sensen.finance.PeriodsRequest';
}
/**
 * Generated by JsPbCodeGenerator.
 * @param {Array=} opt_data Optional initial data array, typically from a
 * server response, or constructed directly in Javascript. The array is used
 * in place and becomes part of the constructed object. It is not cloned.
 * If no data is provided, the constructed object will be empty, but still
 * valid.
 * @extends {jspb.Message}
 * @constructor
 */
proto.sensen.finance.RateConversionRequest = function(opt_data) {
  jspb.Message.initialize(this, opt_data, 0, -1, null, null);
};
goog.inherits(proto.sensen.finance.RateConversionRequest, jspb.Message);
if (goog.DEBUG && !COMPILED) {
  /**
   * @public
   * @override
   */
  proto.sensen.finance.RateConversionRequest.displayName = 'proto.sensen.finance.RateConversionRequest';
}
/**
 * Generated by JsPbCodeGenerator.
 * @param {Array=} opt_data Optional initial data array, typically from a
 * server response, or constructed directly in Javascript. The array is used
 * in place and becomes part of the constructed object. It is not cloned.
 * If no data is provided, the constructed object will be empty, but still
 * valid.
 * @extends {jspb.Message}
 * @constructor
 */
proto.sensen.finance.FisherRequest = function(opt_data) {
  jspb.Message.initialize(this, opt_data, 0, -1, null, null);
};
goog.inherits(proto.sensen.finance.FisherRequest, jspb.Message);
if (goog.DEBUG && !COMPILED) {
  /**
   * @public
   * @override
   */
  proto.sensen.finance.FisherRequest.displayName = 'proto.sensen.finance.FisherRequest';
}
/**
 * Generated by JsPbCodeGenerator.
 * @param {Array=} opt_data Optional initial data array, typically from a
 * server response, or constructed directly in Javascript. The array is used
 * in place and becomes part of the constructed object. It is not cloned.
 * If no data is provided, the constructed object will be empty, but still
 * valid.
 * @extends {jspb.Message}
 * @constructor
 */
proto.sensen.finance.AmortizationRequest = function(opt_data) {
  jspb.Message.initialize(this, opt_data, 0, -1, null, null);
};
goog.inherits(proto.sensen.finance.AmortizationRequest, jspb.Message);
if (goog.DEBUG && !COMPILED) {
  /**
   * @public
   * @override
   */
  proto.sensen.finance.AmortizationRequest.displayName = 'proto.sensen.finance.AmortizationRequest';
}
/**
 * Generated by JsPbCodeGenerator.
 * @param {Array=} opt_data Optional initial data array, typically from a
 * server response, or constructed directly in Javascript. The array is used
 * in place and becomes part of the constructed object. It is not cloned.
 * If no data is provided, the constructed object will be empty, but still
 * valid.
 * @extends {jspb.Message}
 * @constructor
 */
proto.sensen.finance.AmortizationRow = function(opt_data) {
  jspb.Message.initialize(this, opt_data, 0, -1, null, null);
};
goog.inherits(proto.sensen.finance.AmortizationRow, jspb.Message);
if (goog.DEBUG && !COMPILED) {
  /**
   * @public
   * @override
   */
  proto.sensen.finance.AmortizationRow.displayName = 'proto.sensen.finance.AmortizationRow';
}
/**
 * Generated by JsPbCodeGenerator.
 * @param {Array=} opt_data Optional initial data array, typically from a
 * server response, or constructed directly in Javascript. The array is used
 * in place and becomes part of the constructed object. It is not cloned.
 * If no data is provided, the constructed object will be empty, but still
 * valid.
 * @extends {jspb.Message}
 * @constructor
 */
proto.sensen.finance.MortgageSummary = function(opt_data) {
  jspb.Message.initialize(this, opt_data, 0, -1, null, null);
};
goog.inherits(proto.sensen.finance.MortgageSummary, jspb.Message);
if (goog.DEBUG && !COMPILED) {
  /**
   * @public
   * @override
   */
  proto.sensen.finance.MortgageSummary.displayName = 'proto.sensen.finance.MortgageSummary';
}
/**
 * Generated by JsPbCodeGenerator.
 * @param {Array=} opt_data Optional initial data array, typically from a
 * server response, or constructed directly in Javascript. The array is used
 * in place and becomes part of the constructed object. It is not cloned.
 * If no data is provided, the constructed object will be empty, but still
 * valid.
 * @extends {jspb.Message}
 * @constructor
 */
proto.sensen.finance.AmortizationResponse = function(opt_data) {
  jspb.Message.initialize(this, opt_data, 0, -1, proto.sensen.finance.AmortizationResponse.repeatedFields_, null);
};
goog.inherits(proto.sensen.finance.AmortizationResponse, jspb.Message);
if (goog.DEBUG && !COMPILED) {
  /**
   * @public
   * @override
   */
  proto.sensen.finance.AmortizationResponse.displayName = 'proto.sensen.finance.AmortizationResponse';
}
/**
 * Generated by JsPbCodeGenerator.
 * @param {Array=} opt_data Optional initial data array, typically from a
 * server response, or constructed directly in Javascript. The array is used
 * in place and becomes part of the constructed object. It is not cloned.
 * If no data is provided, the constructed object will be empty, but still
 * valid.
 * @extends {jspb.Message}
 * @constructor
 */
proto.sensen.finance.DetailedAmortizationRequest = function(opt_data) {
  jspb.Message.initialize(this, opt_data, 0, -1, null, null);
};
goog.inherits(proto.sensen.finance.DetailedAmortizationRequest, jspb.Message);
if (goog.DEBUG && !COMPILED) {
  /**
   * @public
   * @override
   */
  proto.sensen.finance.DetailedAmortizationRequest.displayName = 'proto.sensen.finance.DetailedAmortizationRequest';
}
/**
 * Generated by JsPbCodeGenerator.
 * @param {Array=} opt_data Optional initial data array, typically from a
 * server response, or constructed directly in Javascript. The array is used
 * in place and becomes part of the constructed object. It is not cloned.
 * If no data is provided, the constructed object will be empty, but still
 * valid.
 * @extends {jspb.Message}
 * @constructor
 */
proto.sensen.finance.DetailedAmortizationRow = function(opt_data) {
  jspb.Message.initialize(this, opt_data, 0, -1, null, null);
};
goog.inherits(proto.sensen.finance.DetailedAmortizationRow, jspb.Message);
if (goog.DEBUG && !COMPILED) {
  /**
   * @public
   * @override
   */
  proto.sensen.finance.DetailedAmortizationRow.displayName = 'proto.sensen.finance.DetailedAmortizationRow';
}
/**
 * Generated by JsPbCodeGenerator.
 * @param {Array=} opt_data Optional initial data array, typically from a
 * server response, or constructed directly in Javascript. The array is used
 * in place and becomes part of the constructed object. It is not cloned.
 * If no data is provided, the constructed object will be empty, but still
 * valid.
 * @extends {jspb.Message}
 * @constructor
 */
proto.sensen.finance.DetailedMortgageSummary = function(opt_data) {
  jspb.Message.initialize(this, opt_data, 0, -1, null, null);
};
goog.inherits(proto.sensen.finance.DetailedMortgageSummary, jspb.Message);
if (goog.DEBUG && !COMPILED) {
  /**
   * @public
   * @override
   */
  proto.sensen.finance.DetailedMortgageSummary.displayName = 'proto.sensen.finance.DetailedMortgageSummary';
}
/**
 * Generated by JsPbCodeGenerator.
 * @param {Array=} opt_data Optional initial data array, typically from a
 * server response, or constructed directly in Javascript. The array is used
 * in place and becomes part of the constructed object. It is not cloned.
 * If no data is provided, the constructed object will be empty, but still
 * valid.
 * @extends {jspb.Message}
 * @constructor
 */
proto.sensen.finance.DetailedAmortizationResponse = function(opt_data) {
  jspb.Message.initialize(this, opt_data, 0, -1, proto.sensen.finance.DetailedAmortizationResponse.repeatedFields_, null);
};
goog.inherits(proto.sensen.finance.DetailedAmortizationResponse, jspb.Message);
if (goog.DEBUG && !COMPILED) {
  /**
   * @public
   * @override
   */
  proto.sensen.finance.DetailedAmortizationResponse.displayName = 'proto.sensen.finance.DetailedAmortizationResponse';
}
/**
 * Generated by JsPbCodeGenerator.
 * @param {Array=} opt_data Optional initial data array, typically from a
 * server response, or constructed directly in Javascript. The array is used
 * in place and becomes part of the constructed object. It is not cloned.
 * If no data is provided, the constructed object will be empty, but still
 * valid.
 * @extends {jspb.Message}
 * @constructor
 */
proto.sensen.finance.AmortizationBatchRequest = function(opt_data) {
  jspb.Message.initialize(this, opt_data, 0, -1, proto.sensen.finance.AmortizationBatchRequest.repeatedFields_, null);
};
goog.inherits(proto.sensen.finance.AmortizationBatchRequest, jspb.Message);
if (goog.DEBUG && !COMPILED) {
  /**
   * @public
   * @override
   */
  proto.sensen.finance.AmortizationBatchRequest.displayName = 'proto.sensen.finance.AmortizationBatchRequest';
}
/**
 * Generated by JsPbCodeGenerator.
 * @param {Array=} opt_data Optional initial data array, typically from a
 * server response, or constructed directly in Javascript. The array is used
 * in place and becomes part of the constructed object. It is not cloned.
 * If no data is provided, the constructed object will be empty, but still
 * valid.
 * @extends {jspb.Message}
 * @constructor
 */
proto.sensen.finance.AmortizationBatchResponse = function(opt_data) {
  jspb.Message.initialize(this, opt_data, 0, -1, proto.sensen.finance.AmortizationBatchResponse.repeatedFields_, null);
};
goog.inherits(proto.sensen.finance.AmortizationBatchResponse, jspb.Message);
if (goog.DEBUG && !COMPILED) {
  /**
   * @public
   * @override
   */
  proto.sensen.finance.AmortizationBatchResponse.displayName = 'proto.sensen.finance.AmortizationBatchResponse';
}
/**
 * Generated by JsPbCodeGenerator.
 * @param {Array=} opt_data Optional initial data array, typically from a
 * server response, or constructed directly in Javascript. The array is used
 * in place and becomes part of the constructed object. It is not cloned.
 * If no data is provided, the constructed object will be empty, but still
 * valid.
 * @extends {jspb.Message}
 * @constructor
 */
proto.sensen.finance.HelocRequest = function(opt_data) {
  jspb.Message.initialize(this, opt_data, 0, -1, null, null);
};
goog.inherits(proto.sensen.finance.HelocRequest, jspb.Message);
if (goog.DEBUG && !COMPILED) {
  /**
   * @public
   * @override
   */
  proto.sensen.finance.HelocRequest.displayName = 'proto.sensen.finance.HelocRequest';
}
/**
 * Generated by JsPbCodeGenerator.
 * @param {Array=} opt_data Optional initial data array, typically from a
 * server response, or constructed directly in Javascript. The array is used
 * in place and becomes part of the constructed object. It is not cloned.
 * If no data is provided, the constructed object will be empty, but still
 * valid.
 * @extends {jspb.Message}
 * @constructor
 */
proto.sensen.finance.HelocResponse = function(opt_data) {
  jspb.Message.initialize(this, opt_data, 0, -1, null, null);
};
goog.inherits(proto.sensen.finance.HelocResponse, jspb.Message);
if (goog.DEBUG && !COMPILED) {
  /**
   * @public
   * @override
   */
  proto.sensen.finance.HelocResponse.displayName = 'proto.sensen.finance.HelocResponse';
}
/**
 * Generated by JsPbCodeGenerator.
 * @param {Array=} opt_data Optional initial data array, typically from a
 * server response, or constructed directly in Javascript. The array is used
 * in place and becomes part of the constructed object. It is not cloned.
 * If no data is provided, the constructed object will be empty, but still
 * valid.
 * @extends {jspb.Message}
 * @constructor
 */
proto.sensen.finance.RefinanceRequest = function(opt_data) {
  jspb.Message.initialize(this, opt_data, 0, -1, null, null);
};
goog.inherits(proto.sensen.finance.RefinanceRequest, jspb.Message);
if (goog.DEBUG && !COMPILED) {
  /**
   * @public
   * @override
   */
  proto.sensen.finance.RefinanceRequest.displayName = 'proto.sensen.finance.RefinanceRequest';
}
/**
 * Generated by JsPbCodeGenerator.
 * @param {Array=} opt_data Optional initial data array, typically from a
 * server response, or constructed directly in Javascript. The array is used
 * in place and becomes part of the constructed object. It is not cloned.
 * If no data is provided, the constructed object will be empty, but still
 * valid.
 * @extends {jspb.Message}
 * @constructor
 */
proto.sensen.finance.RefinanceResponse = function(opt_data) {
  jspb.Message.initialize(this, opt_data, 0, -1, null, null);
};
goog.inherits(proto.sensen.finance.RefinanceResponse, jspb.Message);
if (goog.DEBUG && !COMPILED) {
  /**
   * @public
   * @override
   */
  proto.sensen.finance.RefinanceResponse.displayName = 'proto.sensen.finance.RefinanceResponse';
}
/**
 * Generated by JsPbCodeGenerator.
 * @param {Array=} opt_data Optional initial data array, typically from a
 * server response, or constructed directly in Javascript. The array is used
 * in place and becomes part of the constructed object. It is not cloned.
 * If no data is provided, the constructed object will be empty, but still
 * valid.
 * @extends {jspb.Message}
 * @constructor
 */
proto.sensen.finance.PayoffTimingRequest = function(opt_data) {
  jspb.Message.initialize(this, opt_data, 0, -1, null, null);
};
goog.inherits(proto.sensen.finance.PayoffTimingRequest, jspb.Message);
if (goog.DEBUG && !COMPILED) {
  /**
   * @public
   * @override
   */
  proto.sensen.finance.PayoffTimingRequest.displayName = 'proto.sensen.finance.PayoffTimingRequest';
}
/**
 * Generated by JsPbCodeGenerator.
 * @param {Array=} opt_data Optional initial data array, typically from a
 * server response, or constructed directly in Javascript. The array is used
 * in place and becomes part of the constructed object. It is not cloned.
 * If no data is provided, the constructed object will be empty, but still
 * valid.
 * @extends {jspb.Message}
 * @constructor
 */
proto.sensen.finance.PayoffTimingResponse = function(opt_data) {
  jspb.Message.initialize(this, opt_data, 0, -1, null, null);
};
goog.inherits(proto.sensen.finance.PayoffTimingResponse, jspb.Message);
if (goog.DEBUG && !COMPILED) {
  /**
   * @public
   * @override
   */
  proto.sensen.finance.PayoffTimingResponse.displayName = 'proto.sensen.finance.PayoffTimingResponse';
}
/**
 * Generated by JsPbCodeGenerator.
 * @param {Array=} opt_data Optional initial data array, typically from a
 * server response, or constructed directly in Javascript. The array is used
 * in place and becomes part of the constructed object. It is not cloned.
 * If no data is provided, the constructed object will be empty, but still
 * valid.
 * @extends {jspb.Message}
 * @constructor
 */
proto.sensen.finance.MortgageRecastRequest = function(opt_data) {
  jspb.Message.initialize(this, opt_data, 0, -1, null, null);
};
goog.inherits(proto.sensen.finance.MortgageRecastRequest, jspb.Message);
if (goog.DEBUG && !COMPILED) {
  /**
   * @public
   * @override
   */
  proto.sensen.finance.MortgageRecastRequest.displayName = 'proto.sensen.finance.MortgageRecastRequest';
}
/**
 * Generated by JsPbCodeGenerator.
 * @param {Array=} opt_data Optional initial data array, typically from a
 * server response, or constructed directly in Javascript. The array is used
 * in place and becomes part of the constructed object. It is not cloned.
 * If no data is provided, the constructed object will be empty, but still
 * valid.
 * @extends {jspb.Message}
 * @constructor
 */
proto.sensen.finance.MortgageRecastResponse = function(opt_data) {
  jspb.Message.initialize(this, opt_data, 0, -1, null, null);
};
goog.inherits(proto.sensen.finance.MortgageRecastResponse, jspb.Message);
if (goog.DEBUG && !COMPILED) {
  /**
   * @public
   * @override
   */
  proto.sensen.finance.MortgageRecastResponse.displayName = 'proto.sensen.finance.MortgageRecastResponse';
}
/**
 * Generated by JsPbCodeGenerator.
 * @param {Array=} opt_data Optional initial data array, typically from a
 * server response, or constructed directly in Javascript. The array is used
 * in place and becomes part of the constructed object. It is not cloned.
 * If no data is provided, the constructed object will be empty, but still
 * valid.
 * @extends {jspb.Message}
 * @constructor
 */
proto.sensen.finance.NpvRequest = function(opt_data) {
  jspb.Message.initialize(this, opt_data, 0, -1, proto.sensen.finance.NpvRequest.repeatedFields_, null);
};
goog.inherits(proto.sensen.finance.NpvRequest, jspb.Message);
if (goog.DEBUG && !COMPILED) {
  /**
   * @public
   * @override
   */
  proto.sensen.finance.NpvRequest.displayName = 'proto.sensen.finance.NpvRequest';
}
/**
 * Generated by JsPbCodeGenerator.
 * @param {Array=} opt_data Optional initial data array, typically from a
 * server response, or constructed directly in Javascript. The array is used
 * in place and becomes part of the constructed object. It is not cloned.
 * If no data is provided, the constructed object will be empty, but still
 * valid.
 * @extends {jspb.Message}
 * @constructor
 */
proto.sensen.finance.IrrRequest = function(opt_data) {
  jspb.Message.initialize(this, opt_data, 0, -1, proto.sensen.finance.IrrRequest.repeatedFields_, null);
};
goog.inherits(proto.sensen.finance.IrrRequest, jspb.Message);
if (goog.DEBUG && !COMPILED) {
  /**
   * @public
   * @override
   */
  proto.sensen.finance.IrrRequest.displayName = 'proto.sensen.finance.IrrRequest';
}
/**
 * Generated by JsPbCodeGenerator.
 * @param {Array=} opt_data Optional initial data array, typically from a
 * server response, or constructed directly in Javascript. The array is used
 * in place and becomes part of the constructed object. It is not cloned.
 * If no data is provided, the constructed object will be empty, but still
 * valid.
 * @extends {jspb.Message}
 * @constructor
 */
proto.sensen.finance.DatedCashFlowRequest = function(opt_data) {
  jspb.Message.initialize(this, opt_data, 0, -1, proto.sensen.finance.DatedCashFlowRequest.repeatedFields_, null);
};
goog.inherits(proto.sensen.finance.DatedCashFlowRequest, jspb.Message);
if (goog.DEBUG && !COMPILED) {
  /**
   * @public
   * @override
   */
  proto.sensen.finance.DatedCashFlowRequest.displayName = 'proto.sensen.finance.DatedCashFlowRequest';
}
/**
 * Generated by JsPbCodeGenerator.
 * @param {Array=} opt_data Optional initial data array, typically from a
 * server response, or constructed directly in Javascript. The array is used
 * in place and becomes part of the constructed object. It is not cloned.
 * If no data is provided, the constructed object will be empty, but still
 * valid.
 * @extends {jspb.Message}
 * @constructor
 */
proto.sensen.finance.PaybackRequest = function(opt_data) {
  jspb.Message.initialize(this, opt_data, 0, -1, proto.sensen.finance.PaybackRequest.repeatedFields_, null);
};
goog.inherits(proto.sensen.finance.PaybackRequest, jspb.Message);
if (goog.DEBUG && !COMPILED) {
  /**
   * @public
   * @override
   */
  proto.sensen.finance.PaybackRequest.displayName = 'proto.sensen.finance.PaybackRequest';
}
/**
 * Generated by JsPbCodeGenerator.
 * @param {Array=} opt_data Optional initial data array, typically from a
 * server response, or constructed directly in Javascript. The array is used
 * in place and becomes part of the constructed object. It is not cloned.
 * If no data is provided, the constructed object will be empty, but still
 * valid.
 * @extends {jspb.Message}
 * @constructor
 */
proto.sensen.finance.CumulativeRequest = function(opt_data) {
  jspb.Message.initialize(this, opt_data, 0, -1, null, null);
};
goog.inherits(proto.sensen.finance.CumulativeRequest, jspb.Message);
if (goog.DEBUG && !COMPILED) {
  /**
   * @public
   * @override
   */
  proto.sensen.finance.CumulativeRequest.displayName = 'proto.sensen.finance.CumulativeRequest';
}
/**
 * Generated by JsPbCodeGenerator.
 * @param {Array=} opt_data Optional initial data array, typically from a
 * server response, or constructed directly in Javascript. The array is used
 * in place and becomes part of the constructed object. It is not cloned.
 * If no data is provided, the constructed object will be empty, but still
 * valid.
 * @extends {jspb.Message}
 * @constructor
 */
proto.sensen.finance.DepreciationRequest = function(opt_data) {
  jspb.Message.initialize(this, opt_data, 0, -1, null, null);
};
goog.inherits(proto.sensen.finance.DepreciationRequest, jspb.Message);
if (goog.DEBUG && !COMPILED) {
  /**
   * @public
   * @override
   */
  proto.sensen.finance.DepreciationRequest.displayName = 'proto.sensen.finance.DepreciationRequest';
}
/**
 * Generated by JsPbCodeGenerator.
 * @param {Array=} opt_data Optional initial data array, typically from a
 * server response, or constructed directly in Javascript. The array is used
 * in place and becomes part of the constructed object. It is not cloned.
 * If no data is provided, the constructed object will be empty, but still
 * valid.
 * @extends {jspb.Message}
 * @constructor
 */
proto.sensen.finance.BondRequest = function(opt_data) {
  jspb.Message.initialize(this, opt_data, 0, -1, null, proto.sensen.finance.BondRequest.oneofGroups_);
};
goog.inherits(proto.sensen.finance.BondRequest, jspb.Message);
if (goog.DEBUG && !COMPILED) {
  /**
   * @public
   * @override
   */
  proto.sensen.finance.BondRequest.displayName = 'proto.sensen.finance.BondRequest';
}
/**
 * Generated by JsPbCodeGenerator.
 * @param {Array=} opt_data Optional initial data array, typically from a
 * server response, or constructed directly in Javascript. The array is used
 * in place and becomes part of the constructed object. It is not cloned.
 * If no data is provided, the constructed object will be empty, but still
 * valid.
 * @extends {jspb.Message}
 * @constructor
 */
proto.sensen.finance.BondResponse = function(opt_data) {
  jspb.Message.initialize(this, opt_data, 0, -1, null, null);
};
goog.inherits(proto.sensen.finance.BondResponse, jspb.Message);
if (goog.DEBUG && !COMPILED) {
  /**
   * @public
   * @override
   */
  proto.sensen.finance.BondResponse.displayName = 'proto.sensen.finance.BondResponse';
}
/**
 * Generated by JsPbCodeGenerator.
 * @param {Array=} opt_data Optional initial data array, typically from a
 * server response, or constructed directly in Javascript. The array is used
 * in place and becomes part of the constructed object. It is not cloned.
 * If no data is provided, the constructed object will be empty, but still
 * valid.
 * @extends {jspb.Message}
 * @constructor
 */
proto.sensen.finance.TreasuryBillRequest = function(opt_data) {
  jspb.Message.initialize(this, opt_data, 0, -1, null, proto.sensen.finance.TreasuryBillRequest.oneofGroups_);
};
goog.inherits(proto.sensen.finance.TreasuryBillRequest, jspb.Message);
if (goog.DEBUG && !COMPILED) {
  /**
   * @public
   * @override
   */
  proto.sensen.finance.TreasuryBillRequest.displayName = 'proto.sensen.finance.TreasuryBillRequest';
}
/**
 * Generated by JsPbCodeGenerator.
 * @param {Array=} opt_data Optional initial data array, typically from a
 * server response, or constructed directly in Javascript. The array is used
 * in place and becomes part of the constructed object. It is not cloned.
 * If no data is provided, the constructed object will be empty, but still
 * valid.
 * @extends {jspb.Message}
 * @constructor
 */
proto.sensen.finance.TreasuryBillResponse = function(opt_data) {
  jspb.Message.initialize(this, opt_data, 0, -1, null, null);
};
goog.inherits(proto.sensen.finance.TreasuryBillResponse, jspb.Message);
if (goog.DEBUG && !COMPILED) {
  /**
   * @public
   * @override
   */
  proto.sensen.finance.TreasuryBillResponse.displayName = 'proto.sensen.finance.TreasuryBillResponse';
}
/**
 * Generated by JsPbCodeGenerator.
 * @param {Array=} opt_data Optional initial data array, typically from a
 * server response, or constructed directly in Javascript. The array is used
 * in place and becomes part of the constructed object. It is not cloned.
 * If no data is provided, the constructed object will be empty, but still
 * valid.
 * @extends {jspb.Message}
 * @constructor
 */
proto.sensen.finance.FuturesPricingRequest = function(opt_data) {
  jspb.Message.initialize(this, opt_data, 0, -1, null, null);
};
goog.inherits(proto.sensen.finance.FuturesPricingRequest, jspb.Message);
if (goog.DEBUG && !COMPILED) {
  /**
   * @public
   * @override
   */
  proto.sensen.finance.FuturesPricingRequest.displayName = 'proto.sensen.finance.FuturesPricingRequest';
}
/**
 * Generated by JsPbCodeGenerator.
 * @param {Array=} opt_data Optional initial data array, typically from a
 * server response, or constructed directly in Javascript. The array is used
 * in place and becomes part of the constructed object. It is not cloned.
 * If no data is provided, the constructed object will be empty, but still
 * valid.
 * @extends {jspb.Message}
 * @constructor
 */
proto.sensen.finance.FuturesValuationRequest = function(opt_data) {
  jspb.Message.initialize(this, opt_data, 0, -1, null, null);
};
goog.inherits(proto.sensen.finance.FuturesValuationRequest, jspb.Message);
if (goog.DEBUG && !COMPILED) {
  /**
   * @public
   * @override
   */
  proto.sensen.finance.FuturesValuationRequest.displayName = 'proto.sensen.finance.FuturesValuationRequest';
}
/**
 * Generated by JsPbCodeGenerator.
 * @param {Array=} opt_data Optional initial data array, typically from a
 * server response, or constructed directly in Javascript. The array is used
 * in place and becomes part of the constructed object. It is not cloned.
 * If no data is provided, the constructed object will be empty, but still
 * valid.
 * @extends {jspb.Message}
 * @constructor
 */
proto.sensen.finance.MarginSimulationRequest = function(opt_data) {
  jspb.Message.initialize(this, opt_data, 0, -1, proto.sensen.finance.MarginSimulationRequest.repeatedFields_, null);
};
goog.inherits(proto.sensen.finance.MarginSimulationRequest, jspb.Message);
if (goog.DEBUG && !COMPILED) {
  /**
   * @public
   * @override
   */
  proto.sensen.finance.MarginSimulationRequest.displayName = 'proto.sensen.finance.MarginSimulationRequest';
}
/**
 * Generated by JsPbCodeGenerator.
 * @param {Array=} opt_data Optional initial data array, typically from a
 * server response, or constructed directly in Javascript. The array is used
 * in place and becomes part of the constructed object. It is not cloned.
 * If no data is provided, the constructed object will be empty, but still
 * valid.
 * @extends {jspb.Message}
 * @constructor
 */
proto.sensen.finance.MarginSimulationResponse = function(opt_data) {
  jspb.Message.initialize(this, opt_data, 0, -1, null, null);
};
goog.inherits(proto.sensen.finance.MarginSimulationResponse, jspb.Message);
if (goog.DEBUG && !COMPILED) {
  /**
   * @public
   * @override
   */
  proto.sensen.finance.MarginSimulationResponse.displayName = 'proto.sensen.finance.MarginSimulationResponse';
}
/**
 * Generated by JsPbCodeGenerator.
 * @param {Array=} opt_data Optional initial data array, typically from a
 * server response, or constructed directly in Javascript. The array is used
 * in place and becomes part of the constructed object. It is not cloned.
 * If no data is provided, the constructed object will be empty, but still
 * valid.
 * @extends {jspb.Message}
 * @constructor
 */
proto.sensen.finance.HedgeRequest = function(opt_data) {
  jspb.Message.initialize(this, opt_data, 0, -1, null, null);
};
goog.inherits(proto.sensen.finance.HedgeRequest, jspb.Message);
if (goog.DEBUG && !COMPILED) {
  /**
   * @public
   * @override
   */
  proto.sensen.finance.HedgeRequest.displayName = 'proto.sensen.finance.HedgeRequest';
}
/**
 * Generated by JsPbCodeGenerator.
 * @param {Array=} opt_data Optional initial data array, typically from a
 * server response, or constructed directly in Javascript. The array is used
 * in place and becomes part of the constructed object. It is not cloned.
 * If no data is provided, the constructed object will be empty, but still
 * valid.
 * @extends {jspb.Message}
 * @constructor
 */
proto.sensen.finance.HedgeResponse = function(opt_data) {
  jspb.Message.initialize(this, opt_data, 0, -1, null, null);
};
goog.inherits(proto.sensen.finance.HedgeResponse, jspb.Message);
if (goog.DEBUG && !COMPILED) {
  /**
   * @public
   * @override
   */
  proto.sensen.finance.HedgeResponse.displayName = 'proto.sensen.finance.HedgeResponse';
}
/**
 * Generated by JsPbCodeGenerator.
 * @param {Array=} opt_data Optional initial data array, typically from a
 * server response, or constructed directly in Javascript. The array is used
 * in place and becomes part of the constructed object. It is not cloned.
 * If no data is provided, the constructed object will be empty, but still
 * valid.
 * @extends {jspb.Message}
 * @constructor
 */
proto.sensen.finance.CommoditySpreadRequest = function(opt_data) {
  jspb.Message.initialize(this, opt_data, 0, -1, null, null);
};
goog.inherits(proto.sensen.finance.CommoditySpreadRequest, jspb.Message);
if (goog.DEBUG && !COMPILED) {
  /**
   * @public
   * @override
   */
  proto.sensen.finance.CommoditySpreadRequest.displayName = 'proto.sensen.finance.CommoditySpreadRequest';
}
/**
 * Generated by JsPbCodeGenerator.
 * @param {Array=} opt_data Optional initial data array, typically from a
 * server response, or constructed directly in Javascript. The array is used
 * in place and becomes part of the constructed object. It is not cloned.
 * If no data is provided, the constructed object will be empty, but still
 * valid.
 * @extends {jspb.Message}
 * @constructor
 */
proto.sensen.finance.RentalRoiRequest = function(opt_data) {
  jspb.Message.initialize(this, opt_data, 0, -1, null, null);
};
goog.inherits(proto.sensen.finance.RentalRoiRequest, jspb.Message);
if (goog.DEBUG && !COMPILED) {
  /**
   * @public
   * @override
   */
  proto.sensen.finance.RentalRoiRequest.displayName = 'proto.sensen.finance.RentalRoiRequest';
}
/**
 * Generated by JsPbCodeGenerator.
 * @param {Array=} opt_data Optional initial data array, typically from a
 * server response, or constructed directly in Javascript. The array is used
 * in place and becomes part of the constructed object. It is not cloned.
 * If no data is provided, the constructed object will be empty, but still
 * valid.
 * @extends {jspb.Message}
 * @constructor
 */
proto.sensen.finance.RentalRoiResponse = function(opt_data) {
  jspb.Message.initialize(this, opt_data, 0, -1, null, null);
};
goog.inherits(proto.sensen.finance.RentalRoiResponse, jspb.Message);
if (goog.DEBUG && !COMPILED) {
  /**
   * @public
   * @override
   */
  proto.sensen.finance.RentalRoiResponse.displayName = 'proto.sensen.finance.RentalRoiResponse';
}
/**
 * Generated by JsPbCodeGenerator.
 * @param {Array=} opt_data Optional initial data array, typically from a
 * server response, or constructed directly in Javascript. The array is used
 * in place and becomes part of the constructed object. It is not cloned.
 * If no data is provided, the constructed object will be empty, but still
 * valid.
 * @extends {jspb.Message}
 * @constructor
 */
proto.sensen.finance.HomeFutureValueRequest = function(opt_data) {
  jspb.Message.initialize(this, opt_data, 0, -1, null, null);
};
goog.inherits(proto.sensen.finance.HomeFutureValueRequest, jspb.Message);
if (goog.DEBUG && !COMPILED) {
  /**
   * @public
   * @override
   */
  proto.sensen.finance.HomeFutureValueRequest.displayName = 'proto.sensen.finance.HomeFutureValueRequest';
}
/**
 * Generated by JsPbCodeGenerator.
 * @param {Array=} opt_data Optional initial data array, typically from a
 * server response, or constructed directly in Javascript. The array is used
 * in place and becomes part of the constructed object. It is not cloned.
 * If no data is provided, the constructed object will be empty, but still
 * valid.
 * @extends {jspb.Message}
 * @constructor
 */
proto.sensen.finance.HomeFutureValueResponse = function(opt_data) {
  jspb.Message.initialize(this, opt_data, 0, -1, null, null);
};
goog.inherits(proto.sensen.finance.HomeFutureValueResponse, jspb.Message);
if (goog.DEBUG && !COMPILED) {
  /**
   * @public
   * @override
   */
  proto.sensen.finance.HomeFutureValueResponse.displayName = 'proto.sensen.finance.HomeFutureValueResponse';
}
/**
 * Generated by JsPbCodeGenerator.
 * @param {Array=} opt_data Optional initial data array, typically from a
 * server response, or constructed directly in Javascript. The array is used
 * in place and becomes part of the constructed object. It is not cloned.
 * If no data is provided, the constructed object will be empty, but still
 * valid.
 * @extends {jspb.Message}
 * @constructor
 */
proto.sensen.finance.RentVsBuyRequest = function(opt_data) {
  jspb.Message.initialize(this, opt_data, 0, -1, null, null);
};
goog.inherits(proto.sensen.finance.RentVsBuyRequest, jspb.Message);
if (goog.DEBUG && !COMPILED) {
  /**
   * @public
   * @override
   */
  proto.sensen.finance.RentVsBuyRequest.displayName = 'proto.sensen.finance.RentVsBuyRequest';
}
/**
 * Generated by JsPbCodeGenerator.
 * @param {Array=} opt_data Optional initial data array, typically from a
 * server response, or constructed directly in Javascript. The array is used
 * in place and becomes part of the constructed object. It is not cloned.
 * If no data is provided, the constructed object will be empty, but still
 * valid.
 * @extends {jspb.Message}
 * @constructor
 */
proto.sensen.finance.RentVsBuyResponse = function(opt_data) {
  jspb.Message.initialize(this, opt_data, 0, -1, null, null);
};
goog.inherits(proto.sensen.finance.RentVsBuyResponse, jspb.Message);
if (goog.DEBUG && !COMPILED) {
  /**
   * @public
   * @override
   */
  proto.sensen.finance.RentVsBuyResponse.displayName = 'proto.sensen.finance.RentVsBuyResponse';
}
/**
 * Generated by JsPbCodeGenerator.
 * @param {Array=} opt_data Optional initial data array, typically from a
 * server response, or constructed directly in Javascript. The array is used
 * in place and becomes part of the constructed object. It is not cloned.
 * If no data is provided, the constructed object will be empty, but still
 * valid.
 * @extends {jspb.Message}
 * @constructor
 */
proto.sensen.finance.HomeNpvRequest = function(opt_data) {
  jspb.Message.initialize(this, opt_data, 0, -1, null, null);
};
goog.inherits(proto.sensen.finance.HomeNpvRequest, jspb.Message);
if (goog.DEBUG && !COMPILED) {
  /**
   * @public
   * @override
   */
  proto.sensen.finance.HomeNpvRequest.displayName = 'proto.sensen.finance.HomeNpvRequest';
}
/**
 * Generated by JsPbCodeGenerator.
 * @param {Array=} opt_data Optional initial data array, typically from a
 * server response, or constructed directly in Javascript. The array is used
 * in place and becomes part of the constructed object. It is not cloned.
 * If no data is provided, the constructed object will be empty, but still
 * valid.
 * @extends {jspb.Message}
 * @constructor
 */
proto.sensen.finance.HomeNpvResponse = function(opt_data) {
  jspb.Message.initialize(this, opt_data, 0, -1, null, null);
};
goog.inherits(proto.sensen.finance.HomeNpvResponse, jspb.Message);
if (goog.DEBUG && !COMPILED) {
  /**
   * @public
   * @override
   */
  proto.sensen.finance.HomeNpvResponse.displayName = 'proto.sensen.finance.HomeNpvResponse';
}
/**
 * Generated by JsPbCodeGenerator.
 * @param {Array=} opt_data Optional initial data array, typically from a
 * server response, or constructed directly in Javascript. The array is used
 * in place and becomes part of the constructed object. It is not cloned.
 * If no data is provided, the constructed object will be empty, but still
 * valid.
 * @extends {jspb.Message}
 * @constructor
 */
proto.sensen.finance.OptionTreeRequest = function(opt_data) {
  jspb.Message.initialize(this, opt_data, 0, -1, proto.sensen.finance.OptionTreeRequest.repeatedFields_, null);
};
goog.inherits(proto.sensen.finance.OptionTreeRequest, jspb.Message);
if (goog.DEBUG && !COMPILED) {
  /**
   * @public
   * @override
   */
  proto.sensen.finance.OptionTreeRequest.displayName = 'proto.sensen.finance.OptionTreeRequest';
}
/**
 * Generated by JsPbCodeGenerator.
 * @param {Array=} opt_data Optional initial data array, typically from a
 * server response, or constructed directly in Javascript. The array is used
 * in place and becomes part of the constructed object. It is not cloned.
 * If no data is provided, the constructed object will be empty, but still
 * valid.
 * @extends {jspb.Message}
 * @constructor
 */
proto.sensen.finance.OptionPricingResponse = function(opt_data) {
  jspb.Message.initialize(this, opt_data, 0, -1, null, null);
};
goog.inherits(proto.sensen.finance.OptionPricingResponse, jspb.Message);
if (goog.DEBUG && !COMPILED) {
  /**
   * @public
   * @override
   */
  proto.sensen.finance.OptionPricingResponse.displayName = 'proto.sensen.finance.OptionPricingResponse';
}
/**
 * Generated by JsPbCodeGenerator.
 * @param {Array=} opt_data Optional initial data array, typically from a
 * server response, or constructed directly in Javascript. The array is used
 * in place and becomes part of the constructed object. It is not cloned.
 * If no data is provided, the constructed object will be empty, but still
 * valid.
 * @extends {jspb.Message}
 * @constructor
 */
proto.sensen.finance.BlackScholesRequest = function(opt_data) {
  jspb.Message.initialize(this, opt_data, 0, -1, null, null);
};
goog.inherits(proto.sensen.finance.BlackScholesRequest, jspb.Message);
if (goog.DEBUG && !COMPILED) {
  /**
   * @public
   * @override
   */
  proto.sensen.finance.BlackScholesRequest.displayName = 'proto.sensen.finance.BlackScholesRequest';
}
/**
 * Generated by JsPbCodeGenerator.
 * @param {Array=} opt_data Optional initial data array, typically from a
 * server response, or constructed directly in Javascript. The array is used
 * in place and becomes part of the constructed object. It is not cloned.
 * If no data is provided, the constructed object will be empty, but still
 * valid.
 * @extends {jspb.Message}
 * @constructor
 */
proto.sensen.finance.BlackScholesResponse = function(opt_data) {
  jspb.Message.initialize(this, opt_data, 0, -1, null, null);
};
goog.inherits(proto.sensen.finance.BlackScholesResponse, jspb.Message);
if (goog.DEBUG && !COMPILED) {
  /**
   * @public
   * @override
   */
  proto.sensen.finance.BlackScholesResponse.displayName = 'proto.sensen.finance.BlackScholesResponse';
}
/**
 * Generated by JsPbCodeGenerator.
 * @param {Array=} opt_data Optional initial data array, typically from a
 * server response, or constructed directly in Javascript. The array is used
 * in place and becomes part of the constructed object. It is not cloned.
 * If no data is provided, the constructed object will be empty, but still
 * valid.
 * @extends {jspb.Message}
 * @constructor
 */
proto.sensen.finance.MonteCarloRequest = function(opt_data) {
  jspb.Message.initialize(this, opt_data, 0, -1, null, null);
};
goog.inherits(proto.sensen.finance.MonteCarloRequest, jspb.Message);
if (goog.DEBUG && !COMPILED) {
  /**
   * @public
   * @override
   */
  proto.sensen.finance.MonteCarloRequest.displayName = 'proto.sensen.finance.MonteCarloRequest';
}
/**
 * Generated by JsPbCodeGenerator.
 * @param {Array=} opt_data Optional initial data array, typically from a
 * server response, or constructed directly in Javascript. The array is used
 * in place and becomes part of the constructed object. It is not cloned.
 * If no data is provided, the constructed object will be empty, but still
 * valid.
 * @extends {jspb.Message}
 * @constructor
 */
proto.sensen.finance.ProbabilityTreeRequest = function(opt_data) {
  jspb.Message.initialize(this, opt_data, 0, -1, null, null);
};
goog.inherits(proto.sensen.finance.ProbabilityTreeRequest, jspb.Message);
if (goog.DEBUG && !COMPILED) {
  /**
   * @public
   * @override
   */
  proto.sensen.finance.ProbabilityTreeRequest.displayName = 'proto.sensen.finance.ProbabilityTreeRequest';
}
/**
 * Generated by JsPbCodeGenerator.
 * @param {Array=} opt_data Optional initial data array, typically from a
 * server response, or constructed directly in Javascript. The array is used
 * in place and becomes part of the constructed object. It is not cloned.
 * If no data is provided, the constructed object will be empty, but still
 * valid.
 * @extends {jspb.Message}
 * @constructor
 */
proto.sensen.finance.ProbabilityTreeResponse = function(opt_data) {
  jspb.Message.initialize(this, opt_data, 0, -1, proto.sensen.finance.ProbabilityTreeResponse.repeatedFields_, null);
};
goog.inherits(proto.sensen.finance.ProbabilityTreeResponse, jspb.Message);
if (goog.DEBUG && !COMPILED) {
  /**
   * @public
   * @override
   */
  proto.sensen.finance.ProbabilityTreeResponse.displayName = 'proto.sensen.finance.ProbabilityTreeResponse';
}
/**
 * Generated by JsPbCodeGenerator.
 * @param {Array=} opt_data Optional initial data array, typically from a
 * server response, or constructed directly in Javascript. The array is used
 * in place and becomes part of the constructed object. It is not cloned.
 * If no data is provided, the constructed object will be empty, but still
 * valid.
 * @extends {jspb.Message}
 * @constructor
 */
proto.sensen.finance.PortfolioStatsRequest = function(opt_data) {
  jspb.Message.initialize(this, opt_data, 0, -1, proto.sensen.finance.PortfolioStatsRequest.repeatedFields_, null);
};
goog.inherits(proto.sensen.finance.PortfolioStatsRequest, jspb.Message);
if (goog.DEBUG && !COMPILED) {
  /**
   * @public
   * @override
   */
  proto.sensen.finance.PortfolioStatsRequest.displayName = 'proto.sensen.finance.PortfolioStatsRequest';
}
/**
 * Generated by JsPbCodeGenerator.
 * @param {Array=} opt_data Optional initial data array, typically from a
 * server response, or constructed directly in Javascript. The array is used
 * in place and becomes part of the constructed object. It is not cloned.
 * If no data is provided, the constructed object will be empty, but still
 * valid.
 * @extends {jspb.Message}
 * @constructor
 */
proto.sensen.finance.PortfolioStatsResponse = function(opt_data) {
  jspb.Message.initialize(this, opt_data, 0, -1, null, null);
};
goog.inherits(proto.sensen.finance.PortfolioStatsResponse, jspb.Message);
if (goog.DEBUG && !COMPILED) {
  /**
   * @public
   * @override
   */
  proto.sensen.finance.PortfolioStatsResponse.displayName = 'proto.sensen.finance.PortfolioStatsResponse';
}
/**
 * Generated by JsPbCodeGenerator.
 * @param {Array=} opt_data Optional initial data array, typically from a
 * server response, or constructed directly in Javascript. The array is used
 * in place and becomes part of the constructed object. It is not cloned.
 * If no data is provided, the constructed object will be empty, but still
 * valid.
 * @extends {jspb.Message}
 * @constructor
 */
proto.sensen.finance.PortfolioOptimizeRequest = function(opt_data) {
  jspb.Message.initialize(this, opt_data, 0, -1, proto.sensen.finance.PortfolioOptimizeRequest.repeatedFields_, null);
};
goog.inherits(proto.sensen.finance.PortfolioOptimizeRequest, jspb.Message);
if (goog.DEBUG && !COMPILED) {
  /**
   * @public
   * @override
   */
  proto.sensen.finance.PortfolioOptimizeRequest.displayName = 'proto.sensen.finance.PortfolioOptimizeRequest';
}
/**
 * Generated by JsPbCodeGenerator.
 * @param {Array=} opt_data Optional initial data array, typically from a
 * server response, or constructed directly in Javascript. The array is used
 * in place and becomes part of the constructed object. It is not cloned.
 * If no data is provided, the constructed object will be empty, but still
 * valid.
 * @extends {jspb.Message}
 * @constructor
 */
proto.sensen.finance.PortfolioOptimizeResponse = function(opt_data) {
  jspb.Message.initialize(this, opt_data, 0, -1, proto.sensen.finance.PortfolioOptimizeResponse.repeatedFields_, null);
};
goog.inherits(proto.sensen.finance.PortfolioOptimizeResponse, jspb.Message);
if (goog.DEBUG && !COMPILED) {
  /**
   * @public
   * @override
   */
  proto.sensen.finance.PortfolioOptimizeResponse.displayName = 'proto.sensen.finance.PortfolioOptimizeResponse';
}
/**
 * Generated by JsPbCodeGenerator.
 * @param {Array=} opt_data Optional initial data array, typically from a
 * server response, or constructed directly in Javascript. The array is used
 * in place and becomes part of the constructed object. It is not cloned.
 * If no data is provided, the constructed object will be empty, but still
 * valid.
 * @extends {jspb.Message}
 * @constructor
 */
proto.sensen.finance.RiskContributionRequest = function(opt_data) {
  jspb.Message.initialize(this, opt_data, 0, -1, proto.sensen.finance.RiskContributionRequest.repeatedFields_, null);
};
goog.inherits(proto.sensen.finance.RiskContributionRequest, jspb.Message);
if (goog.DEBUG && !COMPILED) {
  /**
   * @public
   * @override
   */
  proto.sensen.finance.RiskContributionRequest.displayName = 'proto.sensen.finance.RiskContributionRequest';
}
/**
 * Generated by JsPbCodeGenerator.
 * @param {Array=} opt_data Optional initial data array, typically from a
 * server response, or constructed directly in Javascript. The array is used
 * in place and becomes part of the constructed object. It is not cloned.
 * If no data is provided, the constructed object will be empty, but still
 * valid.
 * @extends {jspb.Message}
 * @constructor
 */
proto.sensen.finance.RiskContributionResponse = function(opt_data) {
  jspb.Message.initialize(this, opt_data, 0, -1, proto.sensen.finance.RiskContributionResponse.repeatedFields_, null);
};
goog.inherits(proto.sensen.finance.RiskContributionResponse, jspb.Message);
if (goog.DEBUG && !COMPILED) {
  /**
   * @public
   * @override
   */
  proto.sensen.finance.RiskContributionResponse.displayName = 'proto.sensen.finance.RiskContributionResponse';
}
/**
 * Generated by JsPbCodeGenerator.
 * @param {Array=} opt_data Optional initial data array, typically from a
 * server response, or constructed directly in Javascript. The array is used
 * in place and becomes part of the constructed object. It is not cloned.
 * If no data is provided, the constructed object will be empty, but still
 * valid.
 * @extends {jspb.Message}
 * @constructor
 */
proto.sensen.finance.ClosingCostsRequest = function(opt_data) {
  jspb.Message.initialize(this, opt_data, 0, -1, null, null);
};
goog.inherits(proto.sensen.finance.ClosingCostsRequest, jspb.Message);
if (goog.DEBUG && !COMPILED) {
  /**
   * @public
   * @override
   */
  proto.sensen.finance.ClosingCostsRequest.displayName = 'proto.sensen.finance.ClosingCostsRequest';
}
/**
 * Generated by JsPbCodeGenerator.
 * @param {Array=} opt_data Optional initial data array, typically from a
 * server response, or constructed directly in Javascript. The array is used
 * in place and becomes part of the constructed object. It is not cloned.
 * If no data is provided, the constructed object will be empty, but still
 * valid.
 * @extends {jspb.Message}
 * @constructor
 */
proto.sensen.finance.ClosingCostsResponse = function(opt_data) {
  jspb.Message.initialize(this, opt_data, 0, -1, null, null);
};
goog.inherits(proto.sensen.finance.ClosingCostsResponse, jspb.Message);
if (goog.DEBUG && !COMPILED) {
  /**
   * @public
   * @override
   */
  proto.sensen.finance.ClosingCostsResponse.displayName = 'proto.sensen.finance.ClosingCostsResponse';
}



if (jspb.Message.GENERATE_TO_OBJECT) {
/**
 * Creates an object representation of this proto.
 * Field names that are reserved in JavaScript and will be renamed to pb_name.
 * Optional fields that are not set will be set to undefined.
 * To access a reserved field use, foo.pb_<name>, eg, foo.pb_default.
 * For the list of reserved names please see:
 *     net/proto2/compiler/js/internal/generator.cc#kKeyword.
 * @param {boolean=} opt_includeInstance Deprecated. whether to include the
 *     JSPB instance for transitional soy proto support:
 *     http://goto/soy-param-migration
 * @return {!Object}
 */
proto.sensen.finance.DecimalResponse.prototype.toObject = function(opt_includeInstance) {
  return proto.sensen.finance.DecimalResponse.toObject(opt_includeInstance, this);
};


/**
 * Static version of the {@see toObject} method.
 * @param {boolean|undefined} includeInstance Deprecated. Whether to include
 *     the JSPB instance for transitional soy proto support:
 *     http://goto/soy-param-migration
 * @param {!proto.sensen.finance.DecimalResponse} msg The msg instance to transform.
 * @return {!Object}
 * @suppress {unusedLocalVariables} f is only used for nested messages
 */
proto.sensen.finance.DecimalResponse.toObject = function(includeInstance, msg) {
  var f, obj = {
    value: jspb.Message.getFieldWithDefault(msg, 1, "")
  };

  if (includeInstance) {
    obj.$jspbMessageInstance = msg;
  }
  return obj;
};
}


/**
 * Deserializes binary data (in protobuf wire format).
 * @param {jspb.ByteSource} bytes The bytes to deserialize.
 * @return {!proto.sensen.finance.DecimalResponse}
 */
proto.sensen.finance.DecimalResponse.deserializeBinary = function(bytes) {
  var reader = new jspb.BinaryReader(bytes);
  var msg = new proto.sensen.finance.DecimalResponse;
  return proto.sensen.finance.DecimalResponse.deserializeBinaryFromReader(msg, reader);
};


/**
 * Deserializes binary data (in protobuf wire format) from the
 * given reader into the given message object.
 * @param {!proto.sensen.finance.DecimalResponse} msg The message object to deserialize into.
 * @param {!jspb.BinaryReader} reader The BinaryReader to use.
 * @return {!proto.sensen.finance.DecimalResponse}
 */
proto.sensen.finance.DecimalResponse.deserializeBinaryFromReader = function(msg, reader) {
  while (reader.nextField()) {
    if (reader.isEndGroup()) {
      break;
    }
    var field = reader.getFieldNumber();
    switch (field) {
    case 1:
      var value = /** @type {string} */ (reader.readString());
      msg.setValue(value);
      break;
    default:
      reader.skipField();
      break;
    }
  }
  return msg;
};


/**
 * Serializes the message to binary data (in protobuf wire format).
 * @return {!Uint8Array}
 */
proto.sensen.finance.DecimalResponse.prototype.serializeBinary = function() {
  var writer = new jspb.BinaryWriter();
  proto.sensen.finance.DecimalResponse.serializeBinaryToWriter(this, writer);
  return writer.getResultBuffer();
};


/**
 * Serializes the given message to binary data (in protobuf wire
 * format), writing to the given BinaryWriter.
 * @param {!proto.sensen.finance.DecimalResponse} message
 * @param {!jspb.BinaryWriter} writer
 * @suppress {unusedLocalVariables} f is only used for nested messages
 */
proto.sensen.finance.DecimalResponse.serializeBinaryToWriter = function(message, writer) {
  var f = undefined;
  f = message.getValue();
  if (f.length > 0) {
    writer.writeString(
      1,
      f
    );
  }
};


/**
 * optional string value = 1;
 * @return {string}
 */
proto.sensen.finance.DecimalResponse.prototype.getValue = function() {
  return /** @type {string} */ (jspb.Message.getFieldWithDefault(this, 1, ""));
};


/**
 * @param {string} value
 * @return {!proto.sensen.finance.DecimalResponse} returns this
 */
proto.sensen.finance.DecimalResponse.prototype.setValue = function(value) {
  return jspb.Message.setProto3StringField(this, 1, value);
};





if (jspb.Message.GENERATE_TO_OBJECT) {
/**
 * Creates an object representation of this proto.
 * Field names that are reserved in JavaScript and will be renamed to pb_name.
 * Optional fields that are not set will be set to undefined.
 * To access a reserved field use, foo.pb_<name>, eg, foo.pb_default.
 * For the list of reserved names please see:
 *     net/proto2/compiler/js/internal/generator.cc#kKeyword.
 * @param {boolean=} opt_includeInstance Deprecated. whether to include the
 *     JSPB instance for transitional soy proto support:
 *     http://goto/soy-param-migration
 * @return {!Object}
 */
proto.sensen.finance.DoubleResponse.prototype.toObject = function(opt_includeInstance) {
  return proto.sensen.finance.DoubleResponse.toObject(opt_includeInstance, this);
};


/**
 * Static version of the {@see toObject} method.
 * @param {boolean|undefined} includeInstance Deprecated. Whether to include
 *     the JSPB instance for transitional soy proto support:
 *     http://goto/soy-param-migration
 * @param {!proto.sensen.finance.DoubleResponse} msg The msg instance to transform.
 * @return {!Object}
 * @suppress {unusedLocalVariables} f is only used for nested messages
 */
proto.sensen.finance.DoubleResponse.toObject = function(includeInstance, msg) {
  var f, obj = {
    value: jspb.Message.getFloatingPointFieldWithDefault(msg, 1, 0.0)
  };

  if (includeInstance) {
    obj.$jspbMessageInstance = msg;
  }
  return obj;
};
}


/**
 * Deserializes binary data (in protobuf wire format).
 * @param {jspb.ByteSource} bytes The bytes to deserialize.
 * @return {!proto.sensen.finance.DoubleResponse}
 */
proto.sensen.finance.DoubleResponse.deserializeBinary = function(bytes) {
  var reader = new jspb.BinaryReader(bytes);
  var msg = new proto.sensen.finance.DoubleResponse;
  return proto.sensen.finance.DoubleResponse.deserializeBinaryFromReader(msg, reader);
};


/**
 * Deserializes binary data (in protobuf wire format) from the
 * given reader into the given message object.
 * @param {!proto.sensen.finance.DoubleResponse} msg The message object to deserialize into.
 * @param {!jspb.BinaryReader} reader The BinaryReader to use.
 * @return {!proto.sensen.finance.DoubleResponse}
 */
proto.sensen.finance.DoubleResponse.deserializeBinaryFromReader = function(msg, reader) {
  while (reader.nextField()) {
    if (reader.isEndGroup()) {
      break;
    }
    var field = reader.getFieldNumber();
    switch (field) {
    case 1:
      var value = /** @type {number} */ (reader.readDouble());
      msg.setValue(value);
      break;
    default:
      reader.skipField();
      break;
    }
  }
  return msg;
};


/**
 * Serializes the message to binary data (in protobuf wire format).
 * @return {!Uint8Array}
 */
proto.sensen.finance.DoubleResponse.prototype.serializeBinary = function() {
  var writer = new jspb.BinaryWriter();
  proto.sensen.finance.DoubleResponse.serializeBinaryToWriter(this, writer);
  return writer.getResultBuffer();
};


/**
 * Serializes the given message to binary data (in protobuf wire
 * format), writing to the given BinaryWriter.
 * @param {!proto.sensen.finance.DoubleResponse} message
 * @param {!jspb.BinaryWriter} writer
 * @suppress {unusedLocalVariables} f is only used for nested messages
 */
proto.sensen.finance.DoubleResponse.serializeBinaryToWriter = function(message, writer) {
  var f = undefined;
  f = message.getValue();
  if (f !== 0.0) {
    writer.writeDouble(
      1,
      f
    );
  }
};


/**
 * optional double value = 1;
 * @return {number}
 */
proto.sensen.finance.DoubleResponse.prototype.getValue = function() {
  return /** @type {number} */ (jspb.Message.getFloatingPointFieldWithDefault(this, 1, 0.0));
};


/**
 * @param {number} value
 * @return {!proto.sensen.finance.DoubleResponse} returns this
 */
proto.sensen.finance.DoubleResponse.prototype.setValue = function(value) {
  return jspb.Message.setProto3FloatField(this, 1, value);
};





if (jspb.Message.GENERATE_TO_OBJECT) {
/**
 * Creates an object representation of this proto.
 * Field names that are reserved in JavaScript and will be renamed to pb_name.
 * Optional fields that are not set will be set to undefined.
 * To access a reserved field use, foo.pb_<name>, eg, foo.pb_default.
 * For the list of reserved names please see:
 *     net/proto2/compiler/js/internal/generator.cc#kKeyword.
 * @param {boolean=} opt_includeInstance Deprecated. whether to include the
 *     JSPB instance for transitional soy proto support:
 *     http://goto/soy-param-migration
 * @return {!Object}
 */
proto.sensen.finance.PaymentRequest.prototype.toObject = function(opt_includeInstance) {
  return proto.sensen.finance.PaymentRequest.toObject(opt_includeInstance, this);
};


/**
 * Static version of the {@see toObject} method.
 * @param {boolean|undefined} includeInstance Deprecated. Whether to include
 *     the JSPB instance for transitional soy proto support:
 *     http://goto/soy-param-migration
 * @param {!proto.sensen.finance.PaymentRequest} msg The msg instance to transform.
 * @return {!Object}
 * @suppress {unusedLocalVariables} f is only used for nested messages
 */
proto.sensen.finance.PaymentRequest.toObject = function(includeInstance, msg) {
  var f, obj = {
    rate: jspb.Message.getFieldWithDefault(msg, 1, ""),
    periods: jspb.Message.getFieldWithDefault(msg, 2, 0),
    presentValue: jspb.Message.getFieldWithDefault(msg, 3, ""),
    futureValue: jspb.Message.getFieldWithDefault(msg, 4, ""),
    timing: jspb.Message.getFieldWithDefault(msg, 5, 0)
  };

  if (includeInstance) {
    obj.$jspbMessageInstance = msg;
  }
  return obj;
};
}


/**
 * Deserializes binary data (in protobuf wire format).
 * @param {jspb.ByteSource} bytes The bytes to deserialize.
 * @return {!proto.sensen.finance.PaymentRequest}
 */
proto.sensen.finance.PaymentRequest.deserializeBinary = function(bytes) {
  var reader = new jspb.BinaryReader(bytes);
  var msg = new proto.sensen.finance.PaymentRequest;
  return proto.sensen.finance.PaymentRequest.deserializeBinaryFromReader(msg, reader);
};


/**
 * Deserializes binary data (in protobuf wire format) from the
 * given reader into the given message object.
 * @param {!proto.sensen.finance.PaymentRequest} msg The message object to deserialize into.
 * @param {!jspb.BinaryReader} reader The BinaryReader to use.
 * @return {!proto.sensen.finance.PaymentRequest}
 */
proto.sensen.finance.PaymentRequest.deserializeBinaryFromReader = function(msg, reader) {
  while (reader.nextField()) {
    if (reader.isEndGroup()) {
      break;
    }
    var field = reader.getFieldNumber();
    switch (field) {
    case 1:
      var value = /** @type {string} */ (reader.readString());
      msg.setRate(value);
      break;
    case 2:
      var value = /** @type {number} */ (reader.readInt32());
      msg.setPeriods(value);
      break;
    case 3:
      var value = /** @type {string} */ (reader.readString());
      msg.setPresentValue(value);
      break;
    case 4:
      var value = /** @type {string} */ (reader.readString());
      msg.setFutureValue(value);
      break;
    case 5:
      var value = /** @type {!proto.sensen.finance.AnnuityTiming} */ (reader.readEnum());
      msg.setTiming(value);
      break;
    default:
      reader.skipField();
      break;
    }
  }
  return msg;
};


/**
 * Serializes the message to binary data (in protobuf wire format).
 * @return {!Uint8Array}
 */
proto.sensen.finance.PaymentRequest.prototype.serializeBinary = function() {
  var writer = new jspb.BinaryWriter();
  proto.sensen.finance.PaymentRequest.serializeBinaryToWriter(this, writer);
  return writer.getResultBuffer();
};


/**
 * Serializes the given message to binary data (in protobuf wire
 * format), writing to the given BinaryWriter.
 * @param {!proto.sensen.finance.PaymentRequest} message
 * @param {!jspb.BinaryWriter} writer
 * @suppress {unusedLocalVariables} f is only used for nested messages
 */
proto.sensen.finance.PaymentRequest.serializeBinaryToWriter = function(message, writer) {
  var f = undefined;
  f = message.getRate();
  if (f.length > 0) {
    writer.writeString(
      1,
      f
    );
  }
  f = message.getPeriods();
  if (f !== 0) {
    writer.writeInt32(
      2,
      f
    );
  }
  f = message.getPresentValue();
  if (f.length > 0) {
    writer.writeString(
      3,
      f
    );
  }
  f = message.getFutureValue();
  if (f.length > 0) {
    writer.writeString(
      4,
      f
    );
  }
  f = message.getTiming();
  if (f !== 0.0) {
    writer.writeEnum(
      5,
      f
    );
  }
};


/**
 * optional string rate = 1;
 * @return {string}
 */
proto.sensen.finance.PaymentRequest.prototype.getRate = function() {
  return /** @type {string} */ (jspb.Message.getFieldWithDefault(this, 1, ""));
};


/**
 * @param {string} value
 * @return {!proto.sensen.finance.PaymentRequest} returns this
 */
proto.sensen.finance.PaymentRequest.prototype.setRate = function(value) {
  return jspb.Message.setProto3StringField(this, 1, value);
};


/**
 * optional int32 periods = 2;
 * @return {number}
 */
proto.sensen.finance.PaymentRequest.prototype.getPeriods = function() {
  return /** @type {number} */ (jspb.Message.getFieldWithDefault(this, 2, 0));
};


/**
 * @param {number} value
 * @return {!proto.sensen.finance.PaymentRequest} returns this
 */
proto.sensen.finance.PaymentRequest.prototype.setPeriods = function(value) {
  return jspb.Message.setProto3IntField(this, 2, value);
};


/**
 * optional string present_value = 3;
 * @return {string}
 */
proto.sensen.finance.PaymentRequest.prototype.getPresentValue = function() {
  return /** @type {string} */ (jspb.Message.getFieldWithDefault(this, 3, ""));
};


/**
 * @param {string} value
 * @return {!proto.sensen.finance.PaymentRequest} returns this
 */
proto.sensen.finance.PaymentRequest.prototype.setPresentValue = function(value) {
  return jspb.Message.setProto3StringField(this, 3, value);
};


/**
 * optional string future_value = 4;
 * @return {string}
 */
proto.sensen.finance.PaymentRequest.prototype.getFutureValue = function() {
  return /** @type {string} */ (jspb.Message.getFieldWithDefault(this, 4, ""));
};


/**
 * @param {string} value
 * @return {!proto.sensen.finance.PaymentRequest} returns this
 */
proto.sensen.finance.PaymentRequest.prototype.setFutureValue = function(value) {
  return jspb.Message.setProto3StringField(this, 4, value);
};


/**
 * optional AnnuityTiming timing = 5;
 * @return {!proto.sensen.finance.AnnuityTiming}
 */
proto.sensen.finance.PaymentRequest.prototype.getTiming = function() {
  return /** @type {!proto.sensen.finance.AnnuityTiming} */ (jspb.Message.getFieldWithDefault(this, 5, 0));
};


/**
 * @param {!proto.sensen.finance.AnnuityTiming} value
 * @return {!proto.sensen.finance.PaymentRequest} returns this
 */
proto.sensen.finance.PaymentRequest.prototype.setTiming = function(value) {
  return jspb.Message.setProto3EnumField(this, 5, value);
};





if (jspb.Message.GENERATE_TO_OBJECT) {
/**
 * Creates an object representation of this proto.
 * Field names that are reserved in JavaScript and will be renamed to pb_name.
 * Optional fields that are not set will be set to undefined.
 * To access a reserved field use, foo.pb_<name>, eg, foo.pb_default.
 * For the list of reserved names please see:
 *     net/proto2/compiler/js/internal/generator.cc#kKeyword.
 * @param {boolean=} opt_includeInstance Deprecated. whether to include the
 *     JSPB instance for transitional soy proto support:
 *     http://goto/soy-param-migration
 * @return {!Object}
 */
proto.sensen.finance.PresentValueRequest.prototype.toObject = function(opt_includeInstance) {
  return proto.sensen.finance.PresentValueRequest.toObject(opt_includeInstance, this);
};


/**
 * Static version of the {@see toObject} method.
 * @param {boolean|undefined} includeInstance Deprecated. Whether to include
 *     the JSPB instance for transitional soy proto support:
 *     http://goto/soy-param-migration
 * @param {!proto.sensen.finance.PresentValueRequest} msg The msg instance to transform.
 * @return {!Object}
 * @suppress {unusedLocalVariables} f is only used for nested messages
 */
proto.sensen.finance.PresentValueRequest.toObject = function(includeInstance, msg) {
  var f, obj = {
    rate: jspb.Message.getFieldWithDefault(msg, 1, ""),
    periods: jspb.Message.getFieldWithDefault(msg, 2, 0),
    payment: jspb.Message.getFieldWithDefault(msg, 3, ""),
    futureValue: jspb.Message.getFieldWithDefault(msg, 4, ""),
    timing: jspb.Message.getFieldWithDefault(msg, 5, 0)
  };

  if (includeInstance) {
    obj.$jspbMessageInstance = msg;
  }
  return obj;
};
}


/**
 * Deserializes binary data (in protobuf wire format).
 * @param {jspb.ByteSource} bytes The bytes to deserialize.
 * @return {!proto.sensen.finance.PresentValueRequest}
 */
proto.sensen.finance.PresentValueRequest.deserializeBinary = function(bytes) {
  var reader = new jspb.BinaryReader(bytes);
  var msg = new proto.sensen.finance.PresentValueRequest;
  return proto.sensen.finance.PresentValueRequest.deserializeBinaryFromReader(msg, reader);
};


/**
 * Deserializes binary data (in protobuf wire format) from the
 * given reader into the given message object.
 * @param {!proto.sensen.finance.PresentValueRequest} msg The message object to deserialize into.
 * @param {!jspb.BinaryReader} reader The BinaryReader to use.
 * @return {!proto.sensen.finance.PresentValueRequest}
 */
proto.sensen.finance.PresentValueRequest.deserializeBinaryFromReader = function(msg, reader) {
  while (reader.nextField()) {
    if (reader.isEndGroup()) {
      break;
    }
    var field = reader.getFieldNumber();
    switch (field) {
    case 1:
      var value = /** @type {string} */ (reader.readString());
      msg.setRate(value);
      break;
    case 2:
      var value = /** @type {number} */ (reader.readInt32());
      msg.setPeriods(value);
      break;
    case 3:
      var value = /** @type {string} */ (reader.readString());
      msg.setPayment(value);
      break;
    case 4:
      var value = /** @type {string} */ (reader.readString());
      msg.setFutureValue(value);
      break;
    case 5:
      var value = /** @type {!proto.sensen.finance.AnnuityTiming} */ (reader.readEnum());
      msg.setTiming(value);
      break;
    default:
      reader.skipField();
      break;
    }
  }
  return msg;
};


/**
 * Serializes the message to binary data (in protobuf wire format).
 * @return {!Uint8Array}
 */
proto.sensen.finance.PresentValueRequest.prototype.serializeBinary = function() {
  var writer = new jspb.BinaryWriter();
  proto.sensen.finance.PresentValueRequest.serializeBinaryToWriter(this, writer);
  return writer.getResultBuffer();
};


/**
 * Serializes the given message to binary data (in protobuf wire
 * format), writing to the given BinaryWriter.
 * @param {!proto.sensen.finance.PresentValueRequest} message
 * @param {!jspb.BinaryWriter} writer
 * @suppress {unusedLocalVariables} f is only used for nested messages
 */
proto.sensen.finance.PresentValueRequest.serializeBinaryToWriter = function(message, writer) {
  var f = undefined;
  f = message.getRate();
  if (f.length > 0) {
    writer.writeString(
      1,
      f
    );
  }
  f = message.getPeriods();
  if (f !== 0) {
    writer.writeInt32(
      2,
      f
    );
  }
  f = message.getPayment();
  if (f.length > 0) {
    writer.writeString(
      3,
      f
    );
  }
  f = message.getFutureValue();
  if (f.length > 0) {
    writer.writeString(
      4,
      f
    );
  }
  f = message.getTiming();
  if (f !== 0.0) {
    writer.writeEnum(
      5,
      f
    );
  }
};


/**
 * optional string rate = 1;
 * @return {string}
 */
proto.sensen.finance.PresentValueRequest.prototype.getRate = function() {
  return /** @type {string} */ (jspb.Message.getFieldWithDefault(this, 1, ""));
};


/**
 * @param {string} value
 * @return {!proto.sensen.finance.PresentValueRequest} returns this
 */
proto.sensen.finance.PresentValueRequest.prototype.setRate = function(value) {
  return jspb.Message.setProto3StringField(this, 1, value);
};


/**
 * optional int32 periods = 2;
 * @return {number}
 */
proto.sensen.finance.PresentValueRequest.prototype.getPeriods = function() {
  return /** @type {number} */ (jspb.Message.getFieldWithDefault(this, 2, 0));
};


/**
 * @param {number} value
 * @return {!proto.sensen.finance.PresentValueRequest} returns this
 */
proto.sensen.finance.PresentValueRequest.prototype.setPeriods = function(value) {
  return jspb.Message.setProto3IntField(this, 2, value);
};


/**
 * optional string payment = 3;
 * @return {string}
 */
proto.sensen.finance.PresentValueRequest.prototype.getPayment = function() {
  return /** @type {string} */ (jspb.Message.getFieldWithDefault(this, 3, ""));
};


/**
 * @param {string} value
 * @return {!proto.sensen.finance.PresentValueRequest} returns this
 */
proto.sensen.finance.PresentValueRequest.prototype.setPayment = function(value) {
  return jspb.Message.setProto3StringField(this, 3, value);
};


/**
 * optional string future_value = 4;
 * @return {string}
 */
proto.sensen.finance.PresentValueRequest.prototype.getFutureValue = function() {
  return /** @type {string} */ (jspb.Message.getFieldWithDefault(this, 4, ""));
};


/**
 * @param {string} value
 * @return {!proto.sensen.finance.PresentValueRequest} returns this
 */
proto.sensen.finance.PresentValueRequest.prototype.setFutureValue = function(value) {
  return jspb.Message.setProto3StringField(this, 4, value);
};


/**
 * optional AnnuityTiming timing = 5;
 * @return {!proto.sensen.finance.AnnuityTiming}
 */
proto.sensen.finance.PresentValueRequest.prototype.getTiming = function() {
  return /** @type {!proto.sensen.finance.AnnuityTiming} */ (jspb.Message.getFieldWithDefault(this, 5, 0));
};


/**
 * @param {!proto.sensen.finance.AnnuityTiming} value
 * @return {!proto.sensen.finance.PresentValueRequest} returns this
 */
proto.sensen.finance.PresentValueRequest.prototype.setTiming = function(value) {
  return jspb.Message.setProto3EnumField(this, 5, value);
};





if (jspb.Message.GENERATE_TO_OBJECT) {
/**
 * Creates an object representation of this proto.
 * Field names that are reserved in JavaScript and will be renamed to pb_name.
 * Optional fields that are not set will be set to undefined.
 * To access a reserved field use, foo.pb_<name>, eg, foo.pb_default.
 * For the list of reserved names please see:
 *     net/proto2/compiler/js/internal/generator.cc#kKeyword.
 * @param {boolean=} opt_includeInstance Deprecated. whether to include the
 *     JSPB instance for transitional soy proto support:
 *     http://goto/soy-param-migration
 * @return {!Object}
 */
proto.sensen.finance.FutureValueRequest.prototype.toObject = function(opt_includeInstance) {
  return proto.sensen.finance.FutureValueRequest.toObject(opt_includeInstance, this);
};


/**
 * Static version of the {@see toObject} method.
 * @param {boolean|undefined} includeInstance Deprecated. Whether to include
 *     the JSPB instance for transitional soy proto support:
 *     http://goto/soy-param-migration
 * @param {!proto.sensen.finance.FutureValueRequest} msg The msg instance to transform.
 * @return {!Object}
 * @suppress {unusedLocalVariables} f is only used for nested messages
 */
proto.sensen.finance.FutureValueRequest.toObject = function(includeInstance, msg) {
  var f, obj = {
    rate: jspb.Message.getFieldWithDefault(msg, 1, ""),
    periods: jspb.Message.getFieldWithDefault(msg, 2, 0),
    payment: jspb.Message.getFieldWithDefault(msg, 3, ""),
    presentValue: jspb.Message.getFieldWithDefault(msg, 4, ""),
    timing: jspb.Message.getFieldWithDefault(msg, 5, 0)
  };

  if (includeInstance) {
    obj.$jspbMessageInstance = msg;
  }
  return obj;
};
}


/**
 * Deserializes binary data (in protobuf wire format).
 * @param {jspb.ByteSource} bytes The bytes to deserialize.
 * @return {!proto.sensen.finance.FutureValueRequest}
 */
proto.sensen.finance.FutureValueRequest.deserializeBinary = function(bytes) {
  var reader = new jspb.BinaryReader(bytes);
  var msg = new proto.sensen.finance.FutureValueRequest;
  return proto.sensen.finance.FutureValueRequest.deserializeBinaryFromReader(msg, reader);
};


/**
 * Deserializes binary data (in protobuf wire format) from the
 * given reader into the given message object.
 * @param {!proto.sensen.finance.FutureValueRequest} msg The message object to deserialize into.
 * @param {!jspb.BinaryReader} reader The BinaryReader to use.
 * @return {!proto.sensen.finance.FutureValueRequest}
 */
proto.sensen.finance.FutureValueRequest.deserializeBinaryFromReader = function(msg, reader) {
  while (reader.nextField()) {
    if (reader.isEndGroup()) {
      break;
    }
    var field = reader.getFieldNumber();
    switch (field) {
    case 1:
      var value = /** @type {string} */ (reader.readString());
      msg.setRate(value);
      break;
    case 2:
      var value = /** @type {number} */ (reader.readInt32());
      msg.setPeriods(value);
      break;
    case 3:
      var value = /** @type {string} */ (reader.readString());
      msg.setPayment(value);
      break;
    case 4:
      var value = /** @type {string} */ (reader.readString());
      msg.setPresentValue(value);
      break;
    case 5:
      var value = /** @type {!proto.sensen.finance.AnnuityTiming} */ (reader.readEnum());
      msg.setTiming(value);
      break;
    default:
      reader.skipField();
      break;
    }
  }
  return msg;
};


/**
 * Serializes the message to binary data (in protobuf wire format).
 * @return {!Uint8Array}
 */
proto.sensen.finance.FutureValueRequest.prototype.serializeBinary = function() {
  var writer = new jspb.BinaryWriter();
  proto.sensen.finance.FutureValueRequest.serializeBinaryToWriter(this, writer);
  return writer.getResultBuffer();
};


/**
 * Serializes the given message to binary data (in protobuf wire
 * format), writing to the given BinaryWriter.
 * @param {!proto.sensen.finance.FutureValueRequest} message
 * @param {!jspb.BinaryWriter} writer
 * @suppress {unusedLocalVariables} f is only used for nested messages
 */
proto.sensen.finance.FutureValueRequest.serializeBinaryToWriter = function(message, writer) {
  var f = undefined;
  f = message.getRate();
  if (f.length > 0) {
    writer.writeString(
      1,
      f
    );
  }
  f = message.getPeriods();
  if (f !== 0) {
    writer.writeInt32(
      2,
      f
    );
  }
  f = message.getPayment();
  if (f.length > 0) {
    writer.writeString(
      3,
      f
    );
  }
  f = message.getPresentValue();
  if (f.length > 0) {
    writer.writeString(
      4,
      f
    );
  }
  f = message.getTiming();
  if (f !== 0.0) {
    writer.writeEnum(
      5,
      f
    );
  }
};


/**
 * optional string rate = 1;
 * @return {string}
 */
proto.sensen.finance.FutureValueRequest.prototype.getRate = function() {
  return /** @type {string} */ (jspb.Message.getFieldWithDefault(this, 1, ""));
};


/**
 * @param {string} value
 * @return {!proto.sensen.finance.FutureValueRequest} returns this
 */
proto.sensen.finance.FutureValueRequest.prototype.setRate = function(value) {
  return jspb.Message.setProto3StringField(this, 1, value);
};


/**
 * optional int32 periods = 2;
 * @return {number}
 */
proto.sensen.finance.FutureValueRequest.prototype.getPeriods = function() {
  return /** @type {number} */ (jspb.Message.getFieldWithDefault(this, 2, 0));
};


/**
 * @param {number} value
 * @return {!proto.sensen.finance.FutureValueRequest} returns this
 */
proto.sensen.finance.FutureValueRequest.prototype.setPeriods = function(value) {
  return jspb.Message.setProto3IntField(this, 2, value);
};


/**
 * optional string payment = 3;
 * @return {string}
 */
proto.sensen.finance.FutureValueRequest.prototype.getPayment = function() {
  return /** @type {string} */ (jspb.Message.getFieldWithDefault(this, 3, ""));
};


/**
 * @param {string} value
 * @return {!proto.sensen.finance.FutureValueRequest} returns this
 */
proto.sensen.finance.FutureValueRequest.prototype.setPayment = function(value) {
  return jspb.Message.setProto3StringField(this, 3, value);
};


/**
 * optional string present_value = 4;
 * @return {string}
 */
proto.sensen.finance.FutureValueRequest.prototype.getPresentValue = function() {
  return /** @type {string} */ (jspb.Message.getFieldWithDefault(this, 4, ""));
};


/**
 * @param {string} value
 * @return {!proto.sensen.finance.FutureValueRequest} returns this
 */
proto.sensen.finance.FutureValueRequest.prototype.setPresentValue = function(value) {
  return jspb.Message.setProto3StringField(this, 4, value);
};


/**
 * optional AnnuityTiming timing = 5;
 * @return {!proto.sensen.finance.AnnuityTiming}
 */
proto.sensen.finance.FutureValueRequest.prototype.getTiming = function() {
  return /** @type {!proto.sensen.finance.AnnuityTiming} */ (jspb.Message.getFieldWithDefault(this, 5, 0));
};


/**
 * @param {!proto.sensen.finance.AnnuityTiming} value
 * @return {!proto.sensen.finance.FutureValueRequest} returns this
 */
proto.sensen.finance.FutureValueRequest.prototype.setTiming = function(value) {
  return jspb.Message.setProto3EnumField(this, 5, value);
};





if (jspb.Message.GENERATE_TO_OBJECT) {
/**
 * Creates an object representation of this proto.
 * Field names that are reserved in JavaScript and will be renamed to pb_name.
 * Optional fields that are not set will be set to undefined.
 * To access a reserved field use, foo.pb_<name>, eg, foo.pb_default.
 * For the list of reserved names please see:
 *     net/proto2/compiler/js/internal/generator.cc#kKeyword.
 * @param {boolean=} opt_includeInstance Deprecated. whether to include the
 *     JSPB instance for transitional soy proto support:
 *     http://goto/soy-param-migration
 * @return {!Object}
 */
proto.sensen.finance.FutureValueDetailedRequest.prototype.toObject = function(opt_includeInstance) {
  return proto.sensen.finance.FutureValueDetailedRequest.toObject(opt_includeInstance, this);
};


/**
 * Static version of the {@see toObject} method.
 * @param {boolean|undefined} includeInstance Deprecated. Whether to include
 *     the JSPB instance for transitional soy proto support:
 *     http://goto/soy-param-migration
 * @param {!proto.sensen.finance.FutureValueDetailedRequest} msg The msg instance to transform.
 * @return {!Object}
 * @suppress {unusedLocalVariables} f is only used for nested messages
 */
proto.sensen.finance.FutureValueDetailedRequest.toObject = function(includeInstance, msg) {
  var f, obj = {
    annualRate: jspb.Message.getFieldWithDefault(msg, 1, ""),
    years: jspb.Message.getFieldWithDefault(msg, 2, 0),
    annualContribution: jspb.Message.getFieldWithDefault(msg, 3, ""),
    currentPrincipal: jspb.Message.getFieldWithDefault(msg, 4, ""),
    annualInflationRate: jspb.Message.getFieldWithDefault(msg, 5, ""),
    compoundFrequency: jspb.Message.getFieldWithDefault(msg, 6, 0)
  };

  if (includeInstance) {
    obj.$jspbMessageInstance = msg;
  }
  return obj;
};
}


/**
 * Deserializes binary data (in protobuf wire format).
 * @param {jspb.ByteSource} bytes The bytes to deserialize.
 * @return {!proto.sensen.finance.FutureValueDetailedRequest}
 */
proto.sensen.finance.FutureValueDetailedRequest.deserializeBinary = function(bytes) {
  var reader = new jspb.BinaryReader(bytes);
  var msg = new proto.sensen.finance.FutureValueDetailedRequest;
  return proto.sensen.finance.FutureValueDetailedRequest.deserializeBinaryFromReader(msg, reader);
};


/**
 * Deserializes binary data (in protobuf wire format) from the
 * given reader into the given message object.
 * @param {!proto.sensen.finance.FutureValueDetailedRequest} msg The message object to deserialize into.
 * @param {!jspb.BinaryReader} reader The BinaryReader to use.
 * @return {!proto.sensen.finance.FutureValueDetailedRequest}
 */
proto.sensen.finance.FutureValueDetailedRequest.deserializeBinaryFromReader = function(msg, reader) {
  while (reader.nextField()) {
    if (reader.isEndGroup()) {
      break;
    }
    var field = reader.getFieldNumber();
    switch (field) {
    case 1:
      var value = /** @type {string} */ (reader.readString());
      msg.setAnnualRate(value);
      break;
    case 2:
      var value = /** @type {number} */ (reader.readInt32());
      msg.setYears(value);
      break;
    case 3:
      var value = /** @type {string} */ (reader.readString());
      msg.setAnnualContribution(value);
      break;
    case 4:
      var value = /** @type {string} */ (reader.readString());
      msg.setCurrentPrincipal(value);
      break;
    case 5:
      var value = /** @type {string} */ (reader.readString());
      msg.setAnnualInflationRate(value);
      break;
    case 6:
      var value = /** @type {number} */ (reader.readInt32());
      msg.setCompoundFrequency(value);
      break;
    default:
      reader.skipField();
      break;
    }
  }
  return msg;
};


/**
 * Serializes the message to binary data (in protobuf wire format).
 * @return {!Uint8Array}
 */
proto.sensen.finance.FutureValueDetailedRequest.prototype.serializeBinary = function() {
  var writer = new jspb.BinaryWriter();
  proto.sensen.finance.FutureValueDetailedRequest.serializeBinaryToWriter(this, writer);
  return writer.getResultBuffer();
};


/**
 * Serializes the given message to binary data (in protobuf wire
 * format), writing to the given BinaryWriter.
 * @param {!proto.sensen.finance.FutureValueDetailedRequest} message
 * @param {!jspb.BinaryWriter} writer
 * @suppress {unusedLocalVariables} f is only used for nested messages
 */
proto.sensen.finance.FutureValueDetailedRequest.serializeBinaryToWriter = function(message, writer) {
  var f = undefined;
  f = message.getAnnualRate();
  if (f.length > 0) {
    writer.writeString(
      1,
      f
    );
  }
  f = message.getYears();
  if (f !== 0) {
    writer.writeInt32(
      2,
      f
    );
  }
  f = message.getAnnualContribution();
  if (f.length > 0) {
    writer.writeString(
      3,
      f
    );
  }
  f = message.getCurrentPrincipal();
  if (f.length > 0) {
    writer.writeString(
      4,
      f
    );
  }
  f = message.getAnnualInflationRate();
  if (f.length > 0) {
    writer.writeString(
      5,
      f
    );
  }
  f = message.getCompoundFrequency();
  if (f !== 0) {
    writer.writeInt32(
      6,
      f
    );
  }
};


/**
 * optional string annual_rate = 1;
 * @return {string}
 */
proto.sensen.finance.FutureValueDetailedRequest.prototype.getAnnualRate = function() {
  return /** @type {string} */ (jspb.Message.getFieldWithDefault(this, 1, ""));
};


/**
 * @param {string} value
 * @return {!proto.sensen.finance.FutureValueDetailedRequest} returns this
 */
proto.sensen.finance.FutureValueDetailedRequest.prototype.setAnnualRate = function(value) {
  return jspb.Message.setProto3StringField(this, 1, value);
};


/**
 * optional int32 years = 2;
 * @return {number}
 */
proto.sensen.finance.FutureValueDetailedRequest.prototype.getYears = function() {
  return /** @type {number} */ (jspb.Message.getFieldWithDefault(this, 2, 0));
};


/**
 * @param {number} value
 * @return {!proto.sensen.finance.FutureValueDetailedRequest} returns this
 */
proto.sensen.finance.FutureValueDetailedRequest.prototype.setYears = function(value) {
  return jspb.Message.setProto3IntField(this, 2, value);
};


/**
 * optional string annual_contribution = 3;
 * @return {string}
 */
proto.sensen.finance.FutureValueDetailedRequest.prototype.getAnnualContribution = function() {
  return /** @type {string} */ (jspb.Message.getFieldWithDefault(this, 3, ""));
};


/**
 * @param {string} value
 * @return {!proto.sensen.finance.FutureValueDetailedRequest} returns this
 */
proto.sensen.finance.FutureValueDetailedRequest.prototype.setAnnualContribution = function(value) {
  return jspb.Message.setProto3StringField(this, 3, value);
};


/**
 * optional string current_principal = 4;
 * @return {string}
 */
proto.sensen.finance.FutureValueDetailedRequest.prototype.getCurrentPrincipal = function() {
  return /** @type {string} */ (jspb.Message.getFieldWithDefault(this, 4, ""));
};


/**
 * @param {string} value
 * @return {!proto.sensen.finance.FutureValueDetailedRequest} returns this
 */
proto.sensen.finance.FutureValueDetailedRequest.prototype.setCurrentPrincipal = function(value) {
  return jspb.Message.setProto3StringField(this, 4, value);
};


/**
 * optional string annual_inflation_rate = 5;
 * @return {string}
 */
proto.sensen.finance.FutureValueDetailedRequest.prototype.getAnnualInflationRate = function() {
  return /** @type {string} */ (jspb.Message.getFieldWithDefault(this, 5, ""));
};


/**
 * @param {string} value
 * @return {!proto.sensen.finance.FutureValueDetailedRequest} returns this
 */
proto.sensen.finance.FutureValueDetailedRequest.prototype.setAnnualInflationRate = function(value) {
  return jspb.Message.setProto3StringField(this, 5, value);
};


/**
 * optional int32 compound_frequency = 6;
 * @return {number}
 */
proto.sensen.finance.FutureValueDetailedRequest.prototype.getCompoundFrequency = function() {
  return /** @type {number} */ (jspb.Message.getFieldWithDefault(this, 6, 0));
};


/**
 * @param {number} value
 * @return {!proto.sensen.finance.FutureValueDetailedRequest} returns this
 */
proto.sensen.finance.FutureValueDetailedRequest.prototype.setCompoundFrequency = function(value) {
  return jspb.Message.setProto3IntField(this, 6, value);
};





if (jspb.Message.GENERATE_TO_OBJECT) {
/**
 * Creates an object representation of this proto.
 * Field names that are reserved in JavaScript and will be renamed to pb_name.
 * Optional fields that are not set will be set to undefined.
 * To access a reserved field use, foo.pb_<name>, eg, foo.pb_default.
 * For the list of reserved names please see:
 *     net/proto2/compiler/js/internal/generator.cc#kKeyword.
 * @param {boolean=} opt_includeInstance Deprecated. whether to include the
 *     JSPB instance for transitional soy proto support:
 *     http://goto/soy-param-migration
 * @return {!Object}
 */
proto.sensen.finance.FutureValueDetailedResponse.prototype.toObject = function(opt_includeInstance) {
  return proto.sensen.finance.FutureValueDetailedResponse.toObject(opt_includeInstance, this);
};


/**
 * Static version of the {@see toObject} method.
 * @param {boolean|undefined} includeInstance Deprecated. Whether to include
 *     the JSPB instance for transitional soy proto support:
 *     http://goto/soy-param-migration
 * @param {!proto.sensen.finance.FutureValueDetailedResponse} msg The msg instance to transform.
 * @return {!Object}
 * @suppress {unusedLocalVariables} f is only used for nested messages
 */
proto.sensen.finance.FutureValueDetailedResponse.toObject = function(includeInstance, msg) {
  var f, obj = {
    nominalFv: jspb.Message.getFieldWithDefault(msg, 1, ""),
    inflationAdjustedFv: jspb.Message.getFieldWithDefault(msg, 2, ""),
    totalContributions: jspb.Message.getFieldWithDefault(msg, 3, ""),
    totalInterestEarned: jspb.Message.getFieldWithDefault(msg, 4, "")
  };

  if (includeInstance) {
    obj.$jspbMessageInstance = msg;
  }
  return obj;
};
}


/**
 * Deserializes binary data (in protobuf wire format).
 * @param {jspb.ByteSource} bytes The bytes to deserialize.
 * @return {!proto.sensen.finance.FutureValueDetailedResponse}
 */
proto.sensen.finance.FutureValueDetailedResponse.deserializeBinary = function(bytes) {
  var reader = new jspb.BinaryReader(bytes);
  var msg = new proto.sensen.finance.FutureValueDetailedResponse;
  return proto.sensen.finance.FutureValueDetailedResponse.deserializeBinaryFromReader(msg, reader);
};


/**
 * Deserializes binary data (in protobuf wire format) from the
 * given reader into the given message object.
 * @param {!proto.sensen.finance.FutureValueDetailedResponse} msg The message object to deserialize into.
 * @param {!jspb.BinaryReader} reader The BinaryReader to use.
 * @return {!proto.sensen.finance.FutureValueDetailedResponse}
 */
proto.sensen.finance.FutureValueDetailedResponse.deserializeBinaryFromReader = function(msg, reader) {
  while (reader.nextField()) {
    if (reader.isEndGroup()) {
      break;
    }
    var field = reader.getFieldNumber();
    switch (field) {
    case 1:
      var value = /** @type {string} */ (reader.readString());
      msg.setNominalFv(value);
      break;
    case 2:
      var value = /** @type {string} */ (reader.readString());
      msg.setInflationAdjustedFv(value);
      break;
    case 3:
      var value = /** @type {string} */ (reader.readString());
      msg.setTotalContributions(value);
      break;
    case 4:
      var value = /** @type {string} */ (reader.readString());
      msg.setTotalInterestEarned(value);
      break;
    default:
      reader.skipField();
      break;
    }
  }
  return msg;
};


/**
 * Serializes the message to binary data (in protobuf wire format).
 * @return {!Uint8Array}
 */
proto.sensen.finance.FutureValueDetailedResponse.prototype.serializeBinary = function() {
  var writer = new jspb.BinaryWriter();
  proto.sensen.finance.FutureValueDetailedResponse.serializeBinaryToWriter(this, writer);
  return writer.getResultBuffer();
};


/**
 * Serializes the given message to binary data (in protobuf wire
 * format), writing to the given BinaryWriter.
 * @param {!proto.sensen.finance.FutureValueDetailedResponse} message
 * @param {!jspb.BinaryWriter} writer
 * @suppress {unusedLocalVariables} f is only used for nested messages
 */
proto.sensen.finance.FutureValueDetailedResponse.serializeBinaryToWriter = function(message, writer) {
  var f = undefined;
  f = message.getNominalFv();
  if (f.length > 0) {
    writer.writeString(
      1,
      f
    );
  }
  f = message.getInflationAdjustedFv();
  if (f.length > 0) {
    writer.writeString(
      2,
      f
    );
  }
  f = message.getTotalContributions();
  if (f.length > 0) {
    writer.writeString(
      3,
      f
    );
  }
  f = message.getTotalInterestEarned();
  if (f.length > 0) {
    writer.writeString(
      4,
      f
    );
  }
};


/**
 * optional string nominal_fv = 1;
 * @return {string}
 */
proto.sensen.finance.FutureValueDetailedResponse.prototype.getNominalFv = function() {
  return /** @type {string} */ (jspb.Message.getFieldWithDefault(this, 1, ""));
};


/**
 * @param {string} value
 * @return {!proto.sensen.finance.FutureValueDetailedResponse} returns this
 */
proto.sensen.finance.FutureValueDetailedResponse.prototype.setNominalFv = function(value) {
  return jspb.Message.setProto3StringField(this, 1, value);
};


/**
 * optional string inflation_adjusted_fv = 2;
 * @return {string}
 */
proto.sensen.finance.FutureValueDetailedResponse.prototype.getInflationAdjustedFv = function() {
  return /** @type {string} */ (jspb.Message.getFieldWithDefault(this, 2, ""));
};


/**
 * @param {string} value
 * @return {!proto.sensen.finance.FutureValueDetailedResponse} returns this
 */
proto.sensen.finance.FutureValueDetailedResponse.prototype.setInflationAdjustedFv = function(value) {
  return jspb.Message.setProto3StringField(this, 2, value);
};


/**
 * optional string total_contributions = 3;
 * @return {string}
 */
proto.sensen.finance.FutureValueDetailedResponse.prototype.getTotalContributions = function() {
  return /** @type {string} */ (jspb.Message.getFieldWithDefault(this, 3, ""));
};


/**
 * @param {string} value
 * @return {!proto.sensen.finance.FutureValueDetailedResponse} returns this
 */
proto.sensen.finance.FutureValueDetailedResponse.prototype.setTotalContributions = function(value) {
  return jspb.Message.setProto3StringField(this, 3, value);
};


/**
 * optional string total_interest_earned = 4;
 * @return {string}
 */
proto.sensen.finance.FutureValueDetailedResponse.prototype.getTotalInterestEarned = function() {
  return /** @type {string} */ (jspb.Message.getFieldWithDefault(this, 4, ""));
};


/**
 * @param {string} value
 * @return {!proto.sensen.finance.FutureValueDetailedResponse} returns this
 */
proto.sensen.finance.FutureValueDetailedResponse.prototype.setTotalInterestEarned = function(value) {
  return jspb.Message.setProto3StringField(this, 4, value);
};





if (jspb.Message.GENERATE_TO_OBJECT) {
/**
 * Creates an object representation of this proto.
 * Field names that are reserved in JavaScript and will be renamed to pb_name.
 * Optional fields that are not set will be set to undefined.
 * To access a reserved field use, foo.pb_<name>, eg, foo.pb_default.
 * For the list of reserved names please see:
 *     net/proto2/compiler/js/internal/generator.cc#kKeyword.
 * @param {boolean=} opt_includeInstance Deprecated. whether to include the
 *     JSPB instance for transitional soy proto support:
 *     http://goto/soy-param-migration
 * @return {!Object}
 */
proto.sensen.finance.PeriodPaymentRequest.prototype.toObject = function(opt_includeInstance) {
  return proto.sensen.finance.PeriodPaymentRequest.toObject(opt_includeInstance, this);
};


/**
 * Static version of the {@see toObject} method.
 * @param {boolean|undefined} includeInstance Deprecated. Whether to include
 *     the JSPB instance for transitional soy proto support:
 *     http://goto/soy-param-migration
 * @param {!proto.sensen.finance.PeriodPaymentRequest} msg The msg instance to transform.
 * @return {!Object}
 * @suppress {unusedLocalVariables} f is only used for nested messages
 */
proto.sensen.finance.PeriodPaymentRequest.toObject = function(includeInstance, msg) {
  var f, obj = {
    rate: jspb.Message.getFieldWithDefault(msg, 1, ""),
    period: jspb.Message.getFieldWithDefault(msg, 2, 0),
    periods: jspb.Message.getFieldWithDefault(msg, 3, 0),
    presentValue: jspb.Message.getFieldWithDefault(msg, 4, ""),
    futureValue: jspb.Message.getFieldWithDefault(msg, 5, ""),
    timing: jspb.Message.getFieldWithDefault(msg, 6, 0)
  };

  if (includeInstance) {
    obj.$jspbMessageInstance = msg;
  }
  return obj;
};
}


/**
 * Deserializes binary data (in protobuf wire format).
 * @param {jspb.ByteSource} bytes The bytes to deserialize.
 * @return {!proto.sensen.finance.PeriodPaymentRequest}
 */
proto.sensen.finance.PeriodPaymentRequest.deserializeBinary = function(bytes) {
  var reader = new jspb.BinaryReader(bytes);
  var msg = new proto.sensen.finance.PeriodPaymentRequest;
  return proto.sensen.finance.PeriodPaymentRequest.deserializeBinaryFromReader(msg, reader);
};


/**
 * Deserializes binary data (in protobuf wire format) from the
 * given reader into the given message object.
 * @param {!proto.sensen.finance.PeriodPaymentRequest} msg The message object to deserialize into.
 * @param {!jspb.BinaryReader} reader The BinaryReader to use.
 * @return {!proto.sensen.finance.PeriodPaymentRequest}
 */
proto.sensen.finance.PeriodPaymentRequest.deserializeBinaryFromReader = function(msg, reader) {
  while (reader.nextField()) {
    if (reader.isEndGroup()) {
      break;
    }
    var field = reader.getFieldNumber();
    switch (field) {
    case 1:
      var value = /** @type {string} */ (reader.readString());
      msg.setRate(value);
      break;
    case 2:
      var value = /** @type {number} */ (reader.readInt32());
      msg.setPeriod(value);
      break;
    case 3:
      var value = /** @type {number} */ (reader.readInt32());
      msg.setPeriods(value);
      break;
    case 4:
      var value = /** @type {string} */ (reader.readString());
      msg.setPresentValue(value);
      break;
    case 5:
      var value = /** @type {string} */ (reader.readString());
      msg.setFutureValue(value);
      break;
    case 6:
      var value = /** @type {!proto.sensen.finance.AnnuityTiming} */ (reader.readEnum());
      msg.setTiming(value);
      break;
    default:
      reader.skipField();
      break;
    }
  }
  return msg;
};


/**
 * Serializes the message to binary data (in protobuf wire format).
 * @return {!Uint8Array}
 */
proto.sensen.finance.PeriodPaymentRequest.prototype.serializeBinary = function() {
  var writer = new jspb.BinaryWriter();
  proto.sensen.finance.PeriodPaymentRequest.serializeBinaryToWriter(this, writer);
  return writer.getResultBuffer();
};


/**
 * Serializes the given message to binary data (in protobuf wire
 * format), writing to the given BinaryWriter.
 * @param {!proto.sensen.finance.PeriodPaymentRequest} message
 * @param {!jspb.BinaryWriter} writer
 * @suppress {unusedLocalVariables} f is only used for nested messages
 */
proto.sensen.finance.PeriodPaymentRequest.serializeBinaryToWriter = function(message, writer) {
  var f = undefined;
  f = message.getRate();
  if (f.length > 0) {
    writer.writeString(
      1,
      f
    );
  }
  f = message.getPeriod();
  if (f !== 0) {
    writer.writeInt32(
      2,
      f
    );
  }
  f = message.getPeriods();
  if (f !== 0) {
    writer.writeInt32(
      3,
      f
    );
  }
  f = message.getPresentValue();
  if (f.length > 0) {
    writer.writeString(
      4,
      f
    );
  }
  f = message.getFutureValue();
  if (f.length > 0) {
    writer.writeString(
      5,
      f
    );
  }
  f = message.getTiming();
  if (f !== 0.0) {
    writer.writeEnum(
      6,
      f
    );
  }
};


/**
 * optional string rate = 1;
 * @return {string}
 */
proto.sensen.finance.PeriodPaymentRequest.prototype.getRate = function() {
  return /** @type {string} */ (jspb.Message.getFieldWithDefault(this, 1, ""));
};


/**
 * @param {string} value
 * @return {!proto.sensen.finance.PeriodPaymentRequest} returns this
 */
proto.sensen.finance.PeriodPaymentRequest.prototype.setRate = function(value) {
  return jspb.Message.setProto3StringField(this, 1, value);
};


/**
 * optional int32 period = 2;
 * @return {number}
 */
proto.sensen.finance.PeriodPaymentRequest.prototype.getPeriod = function() {
  return /** @type {number} */ (jspb.Message.getFieldWithDefault(this, 2, 0));
};


/**
 * @param {number} value
 * @return {!proto.sensen.finance.PeriodPaymentRequest} returns this
 */
proto.sensen.finance.PeriodPaymentRequest.prototype.setPeriod = function(value) {
  return jspb.Message.setProto3IntField(this, 2, value);
};


/**
 * optional int32 periods = 3;
 * @return {number}
 */
proto.sensen.finance.PeriodPaymentRequest.prototype.getPeriods = function() {
  return /** @type {number} */ (jspb.Message.getFieldWithDefault(this, 3, 0));
};


/**
 * @param {number} value
 * @return {!proto.sensen.finance.PeriodPaymentRequest} returns this
 */
proto.sensen.finance.PeriodPaymentRequest.prototype.setPeriods = function(value) {
  return jspb.Message.setProto3IntField(this, 3, value);
};


/**
 * optional string present_value = 4;
 * @return {string}
 */
proto.sensen.finance.PeriodPaymentRequest.prototype.getPresentValue = function() {
  return /** @type {string} */ (jspb.Message.getFieldWithDefault(this, 4, ""));
};


/**
 * @param {string} value
 * @return {!proto.sensen.finance.PeriodPaymentRequest} returns this
 */
proto.sensen.finance.PeriodPaymentRequest.prototype.setPresentValue = function(value) {
  return jspb.Message.setProto3StringField(this, 4, value);
};


/**
 * optional string future_value = 5;
 * @return {string}
 */
proto.sensen.finance.PeriodPaymentRequest.prototype.getFutureValue = function() {
  return /** @type {string} */ (jspb.Message.getFieldWithDefault(this, 5, ""));
};


/**
 * @param {string} value
 * @return {!proto.sensen.finance.PeriodPaymentRequest} returns this
 */
proto.sensen.finance.PeriodPaymentRequest.prototype.setFutureValue = function(value) {
  return jspb.Message.setProto3StringField(this, 5, value);
};


/**
 * optional AnnuityTiming timing = 6;
 * @return {!proto.sensen.finance.AnnuityTiming}
 */
proto.sensen.finance.PeriodPaymentRequest.prototype.getTiming = function() {
  return /** @type {!proto.sensen.finance.AnnuityTiming} */ (jspb.Message.getFieldWithDefault(this, 6, 0));
};


/**
 * @param {!proto.sensen.finance.AnnuityTiming} value
 * @return {!proto.sensen.finance.PeriodPaymentRequest} returns this
 */
proto.sensen.finance.PeriodPaymentRequest.prototype.setTiming = function(value) {
  return jspb.Message.setProto3EnumField(this, 6, value);
};





if (jspb.Message.GENERATE_TO_OBJECT) {
/**
 * Creates an object representation of this proto.
 * Field names that are reserved in JavaScript and will be renamed to pb_name.
 * Optional fields that are not set will be set to undefined.
 * To access a reserved field use, foo.pb_<name>, eg, foo.pb_default.
 * For the list of reserved names please see:
 *     net/proto2/compiler/js/internal/generator.cc#kKeyword.
 * @param {boolean=} opt_includeInstance Deprecated. whether to include the
 *     JSPB instance for transitional soy proto support:
 *     http://goto/soy-param-migration
 * @return {!Object}
 */
proto.sensen.finance.RateRequest.prototype.toObject = function(opt_includeInstance) {
  return proto.sensen.finance.RateRequest.toObject(opt_includeInstance, this);
};


/**
 * Static version of the {@see toObject} method.
 * @param {boolean|undefined} includeInstance Deprecated. Whether to include
 *     the JSPB instance for transitional soy proto support:
 *     http://goto/soy-param-migration
 * @param {!proto.sensen.finance.RateRequest} msg The msg instance to transform.
 * @return {!Object}
 * @suppress {unusedLocalVariables} f is only used for nested messages
 */
proto.sensen.finance.RateRequest.toObject = function(includeInstance, msg) {
  var f, obj = {
    periods: jspb.Message.getFieldWithDefault(msg, 1, 0),
    payment: jspb.Message.getFieldWithDefault(msg, 2, ""),
    presentValue: jspb.Message.getFieldWithDefault(msg, 3, ""),
    futureValue: jspb.Message.getFieldWithDefault(msg, 4, ""),
    timing: jspb.Message.getFieldWithDefault(msg, 5, 0),
    guess: jspb.Message.getFieldWithDefault(msg, 6, "")
  };

  if (includeInstance) {
    obj.$jspbMessageInstance = msg;
  }
  return obj;
};
}


/**
 * Deserializes binary data (in protobuf wire format).
 * @param {jspb.ByteSource} bytes The bytes to deserialize.
 * @return {!proto.sensen.finance.RateRequest}
 */
proto.sensen.finance.RateRequest.deserializeBinary = function(bytes) {
  var reader = new jspb.BinaryReader(bytes);
  var msg = new proto.sensen.finance.RateRequest;
  return proto.sensen.finance.RateRequest.deserializeBinaryFromReader(msg, reader);
};


/**
 * Deserializes binary data (in protobuf wire format) from the
 * given reader into the given message object.
 * @param {!proto.sensen.finance.RateRequest} msg The message object to deserialize into.
 * @param {!jspb.BinaryReader} reader The BinaryReader to use.
 * @return {!proto.sensen.finance.RateRequest}
 */
proto.sensen.finance.RateRequest.deserializeBinaryFromReader = function(msg, reader) {
  while (reader.nextField()) {
    if (reader.isEndGroup()) {
      break;
    }
    var field = reader.getFieldNumber();
    switch (field) {
    case 1:
      var value = /** @type {number} */ (reader.readInt32());
      msg.setPeriods(value);
      break;
    case 2:
      var value = /** @type {string} */ (reader.readString());
      msg.setPayment(value);
      break;
    case 3:
      var value = /** @type {string} */ (reader.readString());
      msg.setPresentValue(value);
      break;
    case 4:
      var value = /** @type {string} */ (reader.readString());
      msg.setFutureValue(value);
      break;
    case 5:
      var value = /** @type {!proto.sensen.finance.AnnuityTiming} */ (reader.readEnum());
      msg.setTiming(value);
      break;
    case 6:
      var value = /** @type {string} */ (reader.readString());
      msg.setGuess(value);
      break;
    default:
      reader.skipField();
      break;
    }
  }
  return msg;
};


/**
 * Serializes the message to binary data (in protobuf wire format).
 * @return {!Uint8Array}
 */
proto.sensen.finance.RateRequest.prototype.serializeBinary = function() {
  var writer = new jspb.BinaryWriter();
  proto.sensen.finance.RateRequest.serializeBinaryToWriter(this, writer);
  return writer.getResultBuffer();
};


/**
 * Serializes the given message to binary data (in protobuf wire
 * format), writing to the given BinaryWriter.
 * @param {!proto.sensen.finance.RateRequest} message
 * @param {!jspb.BinaryWriter} writer
 * @suppress {unusedLocalVariables} f is only used for nested messages
 */
proto.sensen.finance.RateRequest.serializeBinaryToWriter = function(message, writer) {
  var f = undefined;
  f = message.getPeriods();
  if (f !== 0) {
    writer.writeInt32(
      1,
      f
    );
  }
  f = message.getPayment();
  if (f.length > 0) {
    writer.writeString(
      2,
      f
    );
  }
  f = message.getPresentValue();
  if (f.length > 0) {
    writer.writeString(
      3,
      f
    );
  }
  f = message.getFutureValue();
  if (f.length > 0) {
    writer.writeString(
      4,
      f
    );
  }
  f = message.getTiming();
  if (f !== 0.0) {
    writer.writeEnum(
      5,
      f
    );
  }
  f = message.getGuess();
  if (f.length > 0) {
    writer.writeString(
      6,
      f
    );
  }
};


/**
 * optional int32 periods = 1;
 * @return {number}
 */
proto.sensen.finance.RateRequest.prototype.getPeriods = function() {
  return /** @type {number} */ (jspb.Message.getFieldWithDefault(this, 1, 0));
};


/**
 * @param {number} value
 * @return {!proto.sensen.finance.RateRequest} returns this
 */
proto.sensen.finance.RateRequest.prototype.setPeriods = function(value) {
  return jspb.Message.setProto3IntField(this, 1, value);
};


/**
 * optional string payment = 2;
 * @return {string}
 */
proto.sensen.finance.RateRequest.prototype.getPayment = function() {
  return /** @type {string} */ (jspb.Message.getFieldWithDefault(this, 2, ""));
};


/**
 * @param {string} value
 * @return {!proto.sensen.finance.RateRequest} returns this
 */
proto.sensen.finance.RateRequest.prototype.setPayment = function(value) {
  return jspb.Message.setProto3StringField(this, 2, value);
};


/**
 * optional string present_value = 3;
 * @return {string}
 */
proto.sensen.finance.RateRequest.prototype.getPresentValue = function() {
  return /** @type {string} */ (jspb.Message.getFieldWithDefault(this, 3, ""));
};


/**
 * @param {string} value
 * @return {!proto.sensen.finance.RateRequest} returns this
 */
proto.sensen.finance.RateRequest.prototype.setPresentValue = function(value) {
  return jspb.Message.setProto3StringField(this, 3, value);
};


/**
 * optional string future_value = 4;
 * @return {string}
 */
proto.sensen.finance.RateRequest.prototype.getFutureValue = function() {
  return /** @type {string} */ (jspb.Message.getFieldWithDefault(this, 4, ""));
};


/**
 * @param {string} value
 * @return {!proto.sensen.finance.RateRequest} returns this
 */
proto.sensen.finance.RateRequest.prototype.setFutureValue = function(value) {
  return jspb.Message.setProto3StringField(this, 4, value);
};


/**
 * optional AnnuityTiming timing = 5;
 * @return {!proto.sensen.finance.AnnuityTiming}
 */
proto.sensen.finance.RateRequest.prototype.getTiming = function() {
  return /** @type {!proto.sensen.finance.AnnuityTiming} */ (jspb.Message.getFieldWithDefault(this, 5, 0));
};


/**
 * @param {!proto.sensen.finance.AnnuityTiming} value
 * @return {!proto.sensen.finance.RateRequest} returns this
 */
proto.sensen.finance.RateRequest.prototype.setTiming = function(value) {
  return jspb.Message.setProto3EnumField(this, 5, value);
};


/**
 * optional string guess = 6;
 * @return {string}
 */
proto.sensen.finance.RateRequest.prototype.getGuess = function() {
  return /** @type {string} */ (jspb.Message.getFieldWithDefault(this, 6, ""));
};


/**
 * @param {string} value
 * @return {!proto.sensen.finance.RateRequest} returns this
 */
proto.sensen.finance.RateRequest.prototype.setGuess = function(value) {
  return jspb.Message.setProto3StringField(this, 6, value);
};





if (jspb.Message.GENERATE_TO_OBJECT) {
/**
 * Creates an object representation of this proto.
 * Field names that are reserved in JavaScript and will be renamed to pb_name.
 * Optional fields that are not set will be set to undefined.
 * To access a reserved field use, foo.pb_<name>, eg, foo.pb_default.
 * For the list of reserved names please see:
 *     net/proto2/compiler/js/internal/generator.cc#kKeyword.
 * @param {boolean=} opt_includeInstance Deprecated. whether to include the
 *     JSPB instance for transitional soy proto support:
 *     http://goto/soy-param-migration
 * @return {!Object}
 */
proto.sensen.finance.PeriodsRequest.prototype.toObject = function(opt_includeInstance) {
  return proto.sensen.finance.PeriodsRequest.toObject(opt_includeInstance, this);
};


/**
 * Static version of the {@see toObject} method.
 * @param {boolean|undefined} includeInstance Deprecated. Whether to include
 *     the JSPB instance for transitional soy proto support:
 *     http://goto/soy-param-migration
 * @param {!proto.sensen.finance.PeriodsRequest} msg The msg instance to transform.
 * @return {!Object}
 * @suppress {unusedLocalVariables} f is only used for nested messages
 */
proto.sensen.finance.PeriodsRequest.toObject = function(includeInstance, msg) {
  var f, obj = {
    rate: jspb.Message.getFieldWithDefault(msg, 1, ""),
    payment: jspb.Message.getFieldWithDefault(msg, 2, ""),
    presentValue: jspb.Message.getFieldWithDefault(msg, 3, ""),
    futureValue: jspb.Message.getFieldWithDefault(msg, 4, ""),
    timing: jspb.Message.getFieldWithDefault(msg, 5, 0)
  };

  if (includeInstance) {
    obj.$jspbMessageInstance = msg;
  }
  return obj;
};
}


/**
 * Deserializes binary data (in protobuf wire format).
 * @param {jspb.ByteSource} bytes The bytes to deserialize.
 * @return {!proto.sensen.finance.PeriodsRequest}
 */
proto.sensen.finance.PeriodsRequest.deserializeBinary = function(bytes) {
  var reader = new jspb.BinaryReader(bytes);
  var msg = new proto.sensen.finance.PeriodsRequest;
  return proto.sensen.finance.PeriodsRequest.deserializeBinaryFromReader(msg, reader);
};


/**
 * Deserializes binary data (in protobuf wire format) from the
 * given reader into the given message object.
 * @param {!proto.sensen.finance.PeriodsRequest} msg The message object to deserialize into.
 * @param {!jspb.BinaryReader} reader The BinaryReader to use.
 * @return {!proto.sensen.finance.PeriodsRequest}
 */
proto.sensen.finance.PeriodsRequest.deserializeBinaryFromReader = function(msg, reader) {
  while (reader.nextField()) {
    if (reader.isEndGroup()) {
      break;
    }
    var field = reader.getFieldNumber();
    switch (field) {
    case 1:
      var value = /** @type {string} */ (reader.readString());
      msg.setRate(value);
      break;
    case 2:
      var value = /** @type {string} */ (reader.readString());
      msg.setPayment(value);
      break;
    case 3:
      var value = /** @type {string} */ (reader.readString());
      msg.setPresentValue(value);
      break;
    case 4:
      var value = /** @type {string} */ (reader.readString());
      msg.setFutureValue(value);
      break;
    case 5:
      var value = /** @type {!proto.sensen.finance.AnnuityTiming} */ (reader.readEnum());
      msg.setTiming(value);
      break;
    default:
      reader.skipField();
      break;
    }
  }
  return msg;
};


/**
 * Serializes the message to binary data (in protobuf wire format).
 * @return {!Uint8Array}
 */
proto.sensen.finance.PeriodsRequest.prototype.serializeBinary = function() {
  var writer = new jspb.BinaryWriter();
  proto.sensen.finance.PeriodsRequest.serializeBinaryToWriter(this, writer);
  return writer.getResultBuffer();
};


/**
 * Serializes the given message to binary data (in protobuf wire
 * format), writing to the given BinaryWriter.
 * @param {!proto.sensen.finance.PeriodsRequest} message
 * @param {!jspb.BinaryWriter} writer
 * @suppress {unusedLocalVariables} f is only used for nested messages
 */
proto.sensen.finance.PeriodsRequest.serializeBinaryToWriter = function(message, writer) {
  var f = undefined;
  f = message.getRate();
  if (f.length > 0) {
    writer.writeString(
      1,
      f
    );
  }
  f = message.getPayment();
  if (f.length > 0) {
    writer.writeString(
      2,
      f
    );
  }
  f = message.getPresentValue();
  if (f.length > 0) {
    writer.writeString(
      3,
      f
    );
  }
  f = message.getFutureValue();
  if (f.length > 0) {
    writer.writeString(
      4,
      f
    );
  }
  f = message.getTiming();
  if (f !== 0.0) {
    writer.writeEnum(
      5,
      f
    );
  }
};


/**
 * optional string rate = 1;
 * @return {string}
 */
proto.sensen.finance.PeriodsRequest.prototype.getRate = function() {
  return /** @type {string} */ (jspb.Message.getFieldWithDefault(this, 1, ""));
};


/**
 * @param {string} value
 * @return {!proto.sensen.finance.PeriodsRequest} returns this
 */
proto.sensen.finance.PeriodsRequest.prototype.setRate = function(value) {
  return jspb.Message.setProto3StringField(this, 1, value);
};


/**
 * optional string payment = 2;
 * @return {string}
 */
proto.sensen.finance.PeriodsRequest.prototype.getPayment = function() {
  return /** @type {string} */ (jspb.Message.getFieldWithDefault(this, 2, ""));
};


/**
 * @param {string} value
 * @return {!proto.sensen.finance.PeriodsRequest} returns this
 */
proto.sensen.finance.PeriodsRequest.prototype.setPayment = function(value) {
  return jspb.Message.setProto3StringField(this, 2, value);
};


/**
 * optional string present_value = 3;
 * @return {string}
 */
proto.sensen.finance.PeriodsRequest.prototype.getPresentValue = function() {
  return /** @type {string} */ (jspb.Message.getFieldWithDefault(this, 3, ""));
};


/**
 * @param {string} value
 * @return {!proto.sensen.finance.PeriodsRequest} returns this
 */
proto.sensen.finance.PeriodsRequest.prototype.setPresentValue = function(value) {
  return jspb.Message.setProto3StringField(this, 3, value);
};


/**
 * optional string future_value = 4;
 * @return {string}
 */
proto.sensen.finance.PeriodsRequest.prototype.getFutureValue = function() {
  return /** @type {string} */ (jspb.Message.getFieldWithDefault(this, 4, ""));
};


/**
 * @param {string} value
 * @return {!proto.sensen.finance.PeriodsRequest} returns this
 */
proto.sensen.finance.PeriodsRequest.prototype.setFutureValue = function(value) {
  return jspb.Message.setProto3StringField(this, 4, value);
};


/**
 * optional AnnuityTiming timing = 5;
 * @return {!proto.sensen.finance.AnnuityTiming}
 */
proto.sensen.finance.PeriodsRequest.prototype.getTiming = function() {
  return /** @type {!proto.sensen.finance.AnnuityTiming} */ (jspb.Message.getFieldWithDefault(this, 5, 0));
};


/**
 * @param {!proto.sensen.finance.AnnuityTiming} value
 * @return {!proto.sensen.finance.PeriodsRequest} returns this
 */
proto.sensen.finance.PeriodsRequest.prototype.setTiming = function(value) {
  return jspb.Message.setProto3EnumField(this, 5, value);
};





if (jspb.Message.GENERATE_TO_OBJECT) {
/**
 * Creates an object representation of this proto.
 * Field names that are reserved in JavaScript and will be renamed to pb_name.
 * Optional fields that are not set will be set to undefined.
 * To access a reserved field use, foo.pb_<name>, eg, foo.pb_default.
 * For the list of reserved names please see:
 *     net/proto2/compiler/js/internal/generator.cc#kKeyword.
 * @param {boolean=} opt_includeInstance Deprecated. whether to include the
 *     JSPB instance for transitional soy proto support:
 *     http://goto/soy-param-migration
 * @return {!Object}
 */
proto.sensen.finance.RateConversionRequest.prototype.toObject = function(opt_includeInstance) {
  return proto.sensen.finance.RateConversionRequest.toObject(opt_includeInstance, this);
};


/**
 * Static version of the {@see toObject} method.
 * @param {boolean|undefined} includeInstance Deprecated. Whether to include
 *     the JSPB instance for transitional soy proto support:
 *     http://goto/soy-param-migration
 * @param {!proto.sensen.finance.RateConversionRequest} msg The msg instance to transform.
 * @return {!Object}
 * @suppress {unusedLocalVariables} f is only used for nested messages
 */
proto.sensen.finance.RateConversionRequest.toObject = function(includeInstance, msg) {
  var f, obj = {
    direction: jspb.Message.getFieldWithDefault(msg, 1, 0),
    rate: jspb.Message.getFloatingPointFieldWithDefault(msg, 2, 0.0),
    periodsPerYear: jspb.Message.getFloatingPointFieldWithDefault(msg, 3, 0.0)
  };

  if (includeInstance) {
    obj.$jspbMessageInstance = msg;
  }
  return obj;
};
}


/**
 * Deserializes binary data (in protobuf wire format).
 * @param {jspb.ByteSource} bytes The bytes to deserialize.
 * @return {!proto.sensen.finance.RateConversionRequest}
 */
proto.sensen.finance.RateConversionRequest.deserializeBinary = function(bytes) {
  var reader = new jspb.BinaryReader(bytes);
  var msg = new proto.sensen.finance.RateConversionRequest;
  return proto.sensen.finance.RateConversionRequest.deserializeBinaryFromReader(msg, reader);
};


/**
 * Deserializes binary data (in protobuf wire format) from the
 * given reader into the given message object.
 * @param {!proto.sensen.finance.RateConversionRequest} msg The message object to deserialize into.
 * @param {!jspb.BinaryReader} reader The BinaryReader to use.
 * @return {!proto.sensen.finance.RateConversionRequest}
 */
proto.sensen.finance.RateConversionRequest.deserializeBinaryFromReader = function(msg, reader) {
  while (reader.nextField()) {
    if (reader.isEndGroup()) {
      break;
    }
    var field = reader.getFieldNumber();
    switch (field) {
    case 1:
      var value = /** @type {!proto.sensen.finance.RateConversionRequest.Direction} */ (reader.readEnum());
      msg.setDirection(value);
      break;
    case 2:
      var value = /** @type {number} */ (reader.readDouble());
      msg.setRate(value);
      break;
    case 3:
      var value = /** @type {number} */ (reader.readDouble());
      msg.setPeriodsPerYear(value);
      break;
    default:
      reader.skipField();
      break;
    }
  }
  return msg;
};


/**
 * Serializes the message to binary data (in protobuf wire format).
 * @return {!Uint8Array}
 */
proto.sensen.finance.RateConversionRequest.prototype.serializeBinary = function() {
  var writer = new jspb.BinaryWriter();
  proto.sensen.finance.RateConversionRequest.serializeBinaryToWriter(this, writer);
  return writer.getResultBuffer();
};


/**
 * Serializes the given message to binary data (in protobuf wire
 * format), writing to the given BinaryWriter.
 * @param {!proto.sensen.finance.RateConversionRequest} message
 * @param {!jspb.BinaryWriter} writer
 * @suppress {unusedLocalVariables} f is only used for nested messages
 */
proto.sensen.finance.RateConversionRequest.serializeBinaryToWriter = function(message, writer) {
  var f = undefined;
  f = message.getDirection();
  if (f !== 0.0) {
    writer.writeEnum(
      1,
      f
    );
  }
  f = message.getRate();
  if (f !== 0.0) {
    writer.writeDouble(
      2,
      f
    );
  }
  f = message.getPeriodsPerYear();
  if (f !== 0.0) {
    writer.writeDouble(
      3,
      f
    );
  }
};


/**
 * @enum {number}
 */
proto.sensen.finance.RateConversionRequest.Direction = {
  NOMINAL_TO_EFFECTIVE: 0,
  EFFECTIVE_TO_NOMINAL: 1
};

/**
 * optional Direction direction = 1;
 * @return {!proto.sensen.finance.RateConversionRequest.Direction}
 */
proto.sensen.finance.RateConversionRequest.prototype.getDirection = function() {
  return /** @type {!proto.sensen.finance.RateConversionRequest.Direction} */ (jspb.Message.getFieldWithDefault(this, 1, 0));
};


/**
 * @param {!proto.sensen.finance.RateConversionRequest.Direction} value
 * @return {!proto.sensen.finance.RateConversionRequest} returns this
 */
proto.sensen.finance.RateConversionRequest.prototype.setDirection = function(value) {
  return jspb.Message.setProto3EnumField(this, 1, value);
};


/**
 * optional double rate = 2;
 * @return {number}
 */
proto.sensen.finance.RateConversionRequest.prototype.getRate = function() {
  return /** @type {number} */ (jspb.Message.getFloatingPointFieldWithDefault(this, 2, 0.0));
};


/**
 * @param {number} value
 * @return {!proto.sensen.finance.RateConversionRequest} returns this
 */
proto.sensen.finance.RateConversionRequest.prototype.setRate = function(value) {
  return jspb.Message.setProto3FloatField(this, 2, value);
};


/**
 * optional double periods_per_year = 3;
 * @return {number}
 */
proto.sensen.finance.RateConversionRequest.prototype.getPeriodsPerYear = function() {
  return /** @type {number} */ (jspb.Message.getFloatingPointFieldWithDefault(this, 3, 0.0));
};


/**
 * @param {number} value
 * @return {!proto.sensen.finance.RateConversionRequest} returns this
 */
proto.sensen.finance.RateConversionRequest.prototype.setPeriodsPerYear = function(value) {
  return jspb.Message.setProto3FloatField(this, 3, value);
};





if (jspb.Message.GENERATE_TO_OBJECT) {
/**
 * Creates an object representation of this proto.
 * Field names that are reserved in JavaScript and will be renamed to pb_name.
 * Optional fields that are not set will be set to undefined.
 * To access a reserved field use, foo.pb_<name>, eg, foo.pb_default.
 * For the list of reserved names please see:
 *     net/proto2/compiler/js/internal/generator.cc#kKeyword.
 * @param {boolean=} opt_includeInstance Deprecated. whether to include the
 *     JSPB instance for transitional soy proto support:
 *     http://goto/soy-param-migration
 * @return {!Object}
 */
proto.sensen.finance.FisherRequest.prototype.toObject = function(opt_includeInstance) {
  return proto.sensen.finance.FisherRequest.toObject(opt_includeInstance, this);
};


/**
 * Static version of the {@see toObject} method.
 * @param {boolean|undefined} includeInstance Deprecated. Whether to include
 *     the JSPB instance for transitional soy proto support:
 *     http://goto/soy-param-migration
 * @param {!proto.sensen.finance.FisherRequest} msg The msg instance to transform.
 * @return {!Object}
 * @suppress {unusedLocalVariables} f is only used for nested messages
 */
proto.sensen.finance.FisherRequest.toObject = function(includeInstance, msg) {
  var f, obj = {
    direction: jspb.Message.getFieldWithDefault(msg, 1, 0),
    rate: jspb.Message.getFloatingPointFieldWithDefault(msg, 2, 0.0),
    inflationRate: jspb.Message.getFloatingPointFieldWithDefault(msg, 3, 0.0)
  };

  if (includeInstance) {
    obj.$jspbMessageInstance = msg;
  }
  return obj;
};
}


/**
 * Deserializes binary data (in protobuf wire format).
 * @param {jspb.ByteSource} bytes The bytes to deserialize.
 * @return {!proto.sensen.finance.FisherRequest}
 */
proto.sensen.finance.FisherRequest.deserializeBinary = function(bytes) {
  var reader = new jspb.BinaryReader(bytes);
  var msg = new proto.sensen.finance.FisherRequest;
  return proto.sensen.finance.FisherRequest.deserializeBinaryFromReader(msg, reader);
};


/**
 * Deserializes binary data (in protobuf wire format) from the
 * given reader into the given message object.
 * @param {!proto.sensen.finance.FisherRequest} msg The message object to deserialize into.
 * @param {!jspb.BinaryReader} reader The BinaryReader to use.
 * @return {!proto.sensen.finance.FisherRequest}
 */
proto.sensen.finance.FisherRequest.deserializeBinaryFromReader = function(msg, reader) {
  while (reader.nextField()) {
    if (reader.isEndGroup()) {
      break;
    }
    var field = reader.getFieldNumber();
    switch (field) {
    case 1:
      var value = /** @type {!proto.sensen.finance.FisherRequest.Direction} */ (reader.readEnum());
      msg.setDirection(value);
      break;
    case 2:
      var value = /** @type {number} */ (reader.readDouble());
      msg.setRate(value);
      break;
    case 3:
      var value = /** @type {number} */ (reader.readDouble());
      msg.setInflationRate(value);
      break;
    default:
      reader.skipField();
      break;
    }
  }
  return msg;
};


/**
 * Serializes the message to binary data (in protobuf wire format).
 * @return {!Uint8Array}
 */
proto.sensen.finance.FisherRequest.prototype.serializeBinary = function() {
  var writer = new jspb.BinaryWriter();
  proto.sensen.finance.FisherRequest.serializeBinaryToWriter(this, writer);
  return writer.getResultBuffer();
};


/**
 * Serializes the given message to binary data (in protobuf wire
 * format), writing to the given BinaryWriter.
 * @param {!proto.sensen.finance.FisherRequest} message
 * @param {!jspb.BinaryWriter} writer
 * @suppress {unusedLocalVariables} f is only used for nested messages
 */
proto.sensen.finance.FisherRequest.serializeBinaryToWriter = function(message, writer) {
  var f = undefined;
  f = message.getDirection();
  if (f !== 0.0) {
    writer.writeEnum(
      1,
      f
    );
  }
  f = message.getRate();
  if (f !== 0.0) {
    writer.writeDouble(
      2,
      f
    );
  }
  f = message.getInflationRate();
  if (f !== 0.0) {
    writer.writeDouble(
      3,
      f
    );
  }
};


/**
 * @enum {number}
 */
proto.sensen.finance.FisherRequest.Direction = {
  NOMINAL_TO_REAL: 0,
  REAL_TO_NOMINAL: 1
};

/**
 * optional Direction direction = 1;
 * @return {!proto.sensen.finance.FisherRequest.Direction}
 */
proto.sensen.finance.FisherRequest.prototype.getDirection = function() {
  return /** @type {!proto.sensen.finance.FisherRequest.Direction} */ (jspb.Message.getFieldWithDefault(this, 1, 0));
};


/**
 * @param {!proto.sensen.finance.FisherRequest.Direction} value
 * @return {!proto.sensen.finance.FisherRequest} returns this
 */
proto.sensen.finance.FisherRequest.prototype.setDirection = function(value) {
  return jspb.Message.setProto3EnumField(this, 1, value);
};


/**
 * optional double rate = 2;
 * @return {number}
 */
proto.sensen.finance.FisherRequest.prototype.getRate = function() {
  return /** @type {number} */ (jspb.Message.getFloatingPointFieldWithDefault(this, 2, 0.0));
};


/**
 * @param {number} value
 * @return {!proto.sensen.finance.FisherRequest} returns this
 */
proto.sensen.finance.FisherRequest.prototype.setRate = function(value) {
  return jspb.Message.setProto3FloatField(this, 2, value);
};


/**
 * optional double inflation_rate = 3;
 * @return {number}
 */
proto.sensen.finance.FisherRequest.prototype.getInflationRate = function() {
  return /** @type {number} */ (jspb.Message.getFloatingPointFieldWithDefault(this, 3, 0.0));
};


/**
 * @param {number} value
 * @return {!proto.sensen.finance.FisherRequest} returns this
 */
proto.sensen.finance.FisherRequest.prototype.setInflationRate = function(value) {
  return jspb.Message.setProto3FloatField(this, 3, value);
};





if (jspb.Message.GENERATE_TO_OBJECT) {
/**
 * Creates an object representation of this proto.
 * Field names that are reserved in JavaScript and will be renamed to pb_name.
 * Optional fields that are not set will be set to undefined.
 * To access a reserved field use, foo.pb_<name>, eg, foo.pb_default.
 * For the list of reserved names please see:
 *     net/proto2/compiler/js/internal/generator.cc#kKeyword.
 * @param {boolean=} opt_includeInstance Deprecated. whether to include the
 *     JSPB instance for transitional soy proto support:
 *     http://goto/soy-param-migration
 * @return {!Object}
 */
proto.sensen.finance.AmortizationRequest.prototype.toObject = function(opt_includeInstance) {
  return proto.sensen.finance.AmortizationRequest.toObject(opt_includeInstance, this);
};


/**
 * Static version of the {@see toObject} method.
 * @param {boolean|undefined} includeInstance Deprecated. Whether to include
 *     the JSPB instance for transitional soy proto support:
 *     http://goto/soy-param-migration
 * @param {!proto.sensen.finance.AmortizationRequest} msg The msg instance to transform.
 * @return {!Object}
 * @suppress {unusedLocalVariables} f is only used for nested messages
 */
proto.sensen.finance.AmortizationRequest.toObject = function(includeInstance, msg) {
  var f, obj = {
    loanAmount: jspb.Message.getFieldWithDefault(msg, 1, ""),
    annualRate: jspb.Message.getFieldWithDefault(msg, 2, ""),
    termMonths: jspb.Message.getFieldWithDefault(msg, 3, 0),
    monthlyOverpayment: jspb.Message.getFieldWithDefault(msg, 4, ""),
    pmiAnnualRate: jspb.Message.getFieldWithDefault(msg, 5, ""),
    originalHomeValue: jspb.Message.getFieldWithDefault(msg, 6, "")
  };

  if (includeInstance) {
    obj.$jspbMessageInstance = msg;
  }
  return obj;
};
}


/**
 * Deserializes binary data (in protobuf wire format).
 * @param {jspb.ByteSource} bytes The bytes to deserialize.
 * @return {!proto.sensen.finance.AmortizationRequest}
 */
proto.sensen.finance.AmortizationRequest.deserializeBinary = function(bytes) {
  var reader = new jspb.BinaryReader(bytes);
  var msg = new proto.sensen.finance.AmortizationRequest;
  return proto.sensen.finance.AmortizationRequest.deserializeBinaryFromReader(msg, reader);
};


/**
 * Deserializes binary data (in protobuf wire format) from the
 * given reader into the given message object.
 * @param {!proto.sensen.finance.AmortizationRequest} msg The message object to deserialize into.
 * @param {!jspb.BinaryReader} reader The BinaryReader to use.
 * @return {!proto.sensen.finance.AmortizationRequest}
 */
proto.sensen.finance.AmortizationRequest.deserializeBinaryFromReader = function(msg, reader) {
  while (reader.nextField()) {
    if (reader.isEndGroup()) {
      break;
    }
    var field = reader.getFieldNumber();
    switch (field) {
    case 1:
      var value = /** @type {string} */ (reader.readString());
      msg.setLoanAmount(value);
      break;
    case 2:
      var value = /** @type {string} */ (reader.readString());
      msg.setAnnualRate(value);
      break;
    case 3:
      var value = /** @type {number} */ (reader.readInt32());
      msg.setTermMonths(value);
      break;
    case 4:
      var value = /** @type {string} */ (reader.readString());
      msg.setMonthlyOverpayment(value);
      break;
    case 5:
      var value = /** @type {string} */ (reader.readString());
      msg.setPmiAnnualRate(value);
      break;
    case 6:
      var value = /** @type {string} */ (reader.readString());
      msg.setOriginalHomeValue(value);
      break;
    default:
      reader.skipField();
      break;
    }
  }
  return msg;
};


/**
 * Serializes the message to binary data (in protobuf wire format).
 * @return {!Uint8Array}
 */
proto.sensen.finance.AmortizationRequest.prototype.serializeBinary = function() {
  var writer = new jspb.BinaryWriter();
  proto.sensen.finance.AmortizationRequest.serializeBinaryToWriter(this, writer);
  return writer.getResultBuffer();
};


/**
 * Serializes the given message to binary data (in protobuf wire
 * format), writing to the given BinaryWriter.
 * @param {!proto.sensen.finance.AmortizationRequest} message
 * @param {!jspb.BinaryWriter} writer
 * @suppress {unusedLocalVariables} f is only used for nested messages
 */
proto.sensen.finance.AmortizationRequest.serializeBinaryToWriter = function(message, writer) {
  var f = undefined;
  f = message.getLoanAmount();
  if (f.length > 0) {
    writer.writeString(
      1,
      f
    );
  }
  f = message.getAnnualRate();
  if (f.length > 0) {
    writer.writeString(
      2,
      f
    );
  }
  f = message.getTermMonths();
  if (f !== 0) {
    writer.writeInt32(
      3,
      f
    );
  }
  f = message.getMonthlyOverpayment();
  if (f.length > 0) {
    writer.writeString(
      4,
      f
    );
  }
  f = message.getPmiAnnualRate();
  if (f.length > 0) {
    writer.writeString(
      5,
      f
    );
  }
  f = message.getOriginalHomeValue();
  if (f.length > 0) {
    writer.writeString(
      6,
      f
    );
  }
};


/**
 * optional string loan_amount = 1;
 * @return {string}
 */
proto.sensen.finance.AmortizationRequest.prototype.getLoanAmount = function() {
  return /** @type {string} */ (jspb.Message.getFieldWithDefault(this, 1, ""));
};


/**
 * @param {string} value
 * @return {!proto.sensen.finance.AmortizationRequest} returns this
 */
proto.sensen.finance.AmortizationRequest.prototype.setLoanAmount = function(value) {
  return jspb.Message.setProto3StringField(this, 1, value);
};


/**
 * optional string annual_rate = 2;
 * @return {string}
 */
proto.sensen.finance.AmortizationRequest.prototype.getAnnualRate = function() {
  return /** @type {string} */ (jspb.Message.getFieldWithDefault(this, 2, ""));
};


/**
 * @param {string} value
 * @return {!proto.sensen.finance.AmortizationRequest} returns this
 */
proto.sensen.finance.AmortizationRequest.prototype.setAnnualRate = function(value) {
  return jspb.Message.setProto3StringField(this, 2, value);
};


/**
 * optional int32 term_months = 3;
 * @return {number}
 */
proto.sensen.finance.AmortizationRequest.prototype.getTermMonths = function() {
  return /** @type {number} */ (jspb.Message.getFieldWithDefault(this, 3, 0));
};


/**
 * @param {number} value
 * @return {!proto.sensen.finance.AmortizationRequest} returns this
 */
proto.sensen.finance.AmortizationRequest.prototype.setTermMonths = function(value) {
  return jspb.Message.setProto3IntField(this, 3, value);
};


/**
 * optional string monthly_overpayment = 4;
 * @return {string}
 */
proto.sensen.finance.AmortizationRequest.prototype.getMonthlyOverpayment = function() {
  return /** @type {string} */ (jspb.Message.getFieldWithDefault(this, 4, ""));
};


/**
 * @param {string} value
 * @return {!proto.sensen.finance.AmortizationRequest} returns this
 */
proto.sensen.finance.AmortizationRequest.prototype.setMonthlyOverpayment = function(value) {
  return jspb.Message.setProto3StringField(this, 4, value);
};


/**
 * optional string pmi_annual_rate = 5;
 * @return {string}
 */
proto.sensen.finance.AmortizationRequest.prototype.getPmiAnnualRate = function() {
  return /** @type {string} */ (jspb.Message.getFieldWithDefault(this, 5, ""));
};


/**
 * @param {string} value
 * @return {!proto.sensen.finance.AmortizationRequest} returns this
 */
proto.sensen.finance.AmortizationRequest.prototype.setPmiAnnualRate = function(value) {
  return jspb.Message.setProto3StringField(this, 5, value);
};


/**
 * optional string original_home_value = 6;
 * @return {string}
 */
proto.sensen.finance.AmortizationRequest.prototype.getOriginalHomeValue = function() {
  return /** @type {string} */ (jspb.Message.getFieldWithDefault(this, 6, ""));
};


/**
 * @param {string} value
 * @return {!proto.sensen.finance.AmortizationRequest} returns this
 */
proto.sensen.finance.AmortizationRequest.prototype.setOriginalHomeValue = function(value) {
  return jspb.Message.setProto3StringField(this, 6, value);
};





if (jspb.Message.GENERATE_TO_OBJECT) {
/**
 * Creates an object representation of this proto.
 * Field names that are reserved in JavaScript and will be renamed to pb_name.
 * Optional fields that are not set will be set to undefined.
 * To access a reserved field use, foo.pb_<name>, eg, foo.pb_default.
 * For the list of reserved names please see:
 *     net/proto2/compiler/js/internal/generator.cc#kKeyword.
 * @param {boolean=} opt_includeInstance Deprecated. whether to include the
 *     JSPB instance for transitional soy proto support:
 *     http://goto/soy-param-migration
 * @return {!Object}
 */
proto.sensen.finance.AmortizationRow.prototype.toObject = function(opt_includeInstance) {
  return proto.sensen.finance.AmortizationRow.toObject(opt_includeInstance, this);
};


/**
 * Static version of the {@see toObject} method.
 * @param {boolean|undefined} includeInstance Deprecated. Whether to include
 *     the JSPB instance for transitional soy proto support:
 *     http://goto/soy-param-migration
 * @param {!proto.sensen.finance.AmortizationRow} msg The msg instance to transform.
 * @return {!Object}
 * @suppress {unusedLocalVariables} f is only used for nested messages
 */
proto.sensen.finance.AmortizationRow.toObject = function(includeInstance, msg) {
  var f, obj = {
    period: jspb.Message.getFieldWithDefault(msg, 1, 0),
    startBalance: jspb.Message.getFieldWithDefault(msg, 2, ""),
    scheduledPayment: jspb.Message.getFieldWithDefault(msg, 3, ""),
    extraPayment: jspb.Message.getFieldWithDefault(msg, 4, ""),
    interestPaid: jspb.Message.getFieldWithDefault(msg, 5, ""),
    principalPaid: jspb.Message.getFieldWithDefault(msg, 6, ""),
    pmiPaid: jspb.Message.getFieldWithDefault(msg, 7, ""),
    endBalance: jspb.Message.getFieldWithDefault(msg, 8, "")
  };

  if (includeInstance) {
    obj.$jspbMessageInstance = msg;
  }
  return obj;
};
}


/**
 * Deserializes binary data (in protobuf wire format).
 * @param {jspb.ByteSource} bytes The bytes to deserialize.
 * @return {!proto.sensen.finance.AmortizationRow}
 */
proto.sensen.finance.AmortizationRow.deserializeBinary = function(bytes) {
  var reader = new jspb.BinaryReader(bytes);
  var msg = new proto.sensen.finance.AmortizationRow;
  return proto.sensen.finance.AmortizationRow.deserializeBinaryFromReader(msg, reader);
};


/**
 * Deserializes binary data (in protobuf wire format) from the
 * given reader into the given message object.
 * @param {!proto.sensen.finance.AmortizationRow} msg The message object to deserialize into.
 * @param {!jspb.BinaryReader} reader The BinaryReader to use.
 * @return {!proto.sensen.finance.AmortizationRow}
 */
proto.sensen.finance.AmortizationRow.deserializeBinaryFromReader = function(msg, reader) {
  while (reader.nextField()) {
    if (reader.isEndGroup()) {
      break;
    }
    var field = reader.getFieldNumber();
    switch (field) {
    case 1:
      var value = /** @type {number} */ (reader.readInt32());
      msg.setPeriod(value);
      break;
    case 2:
      var value = /** @type {string} */ (reader.readString());
      msg.setStartBalance(value);
      break;
    case 3:
      var value = /** @type {string} */ (reader.readString());
      msg.setScheduledPayment(value);
      break;
    case 4:
      var value = /** @type {string} */ (reader.readString());
      msg.setExtraPayment(value);
      break;
    case 5:
      var value = /** @type {string} */ (reader.readString());
      msg.setInterestPaid(value);
      break;
    case 6:
      var value = /** @type {string} */ (reader.readString());
      msg.setPrincipalPaid(value);
      break;
    case 7:
      var value = /** @type {string} */ (reader.readString());
      msg.setPmiPaid(value);
      break;
    case 8:
      var value = /** @type {string} */ (reader.readString());
      msg.setEndBalance(value);
      break;
    default:
      reader.skipField();
      break;
    }
  }
  return msg;
};


/**
 * Serializes the message to binary data (in protobuf wire format).
 * @return {!Uint8Array}
 */
proto.sensen.finance.AmortizationRow.prototype.serializeBinary = function() {
  var writer = new jspb.BinaryWriter();
  proto.sensen.finance.AmortizationRow.serializeBinaryToWriter(this, writer);
  return writer.getResultBuffer();
};


/**
 * Serializes the given message to binary data (in protobuf wire
 * format), writing to the given BinaryWriter.
 * @param {!proto.sensen.finance.AmortizationRow} message
 * @param {!jspb.BinaryWriter} writer
 * @suppress {unusedLocalVariables} f is only used for nested messages
 */
proto.sensen.finance.AmortizationRow.serializeBinaryToWriter = function(message, writer) {
  var f = undefined;
  f = message.getPeriod();
  if (f !== 0) {
    writer.writeInt32(
      1,
      f
    );
  }
  f = message.getStartBalance();
  if (f.length > 0) {
    writer.writeString(
      2,
      f
    );
  }
  f = message.getScheduledPayment();
  if (f.length > 0) {
    writer.writeString(
      3,
      f
    );
  }
  f = message.getExtraPayment();
  if (f.length > 0) {
    writer.writeString(
      4,
      f
    );
  }
  f = message.getInterestPaid();
  if (f.length > 0) {
    writer.writeString(
      5,
      f
    );
  }
  f = message.getPrincipalPaid();
  if (f.length > 0) {
    writer.writeString(
      6,
      f
    );
  }
  f = message.getPmiPaid();
  if (f.length > 0) {
    writer.writeString(
      7,
      f
    );
  }
  f = message.getEndBalance();
  if (f.length > 0) {
    writer.writeString(
      8,
      f
    );
  }
};


/**
 * optional int32 period = 1;
 * @return {number}
 */
proto.sensen.finance.AmortizationRow.prototype.getPeriod = function() {
  return /** @type {number} */ (jspb.Message.getFieldWithDefault(this, 1, 0));
};


/**
 * @param {number} value
 * @return {!proto.sensen.finance.AmortizationRow} returns this
 */
proto.sensen.finance.AmortizationRow.prototype.setPeriod = function(value) {
  return jspb.Message.setProto3IntField(this, 1, value);
};


/**
 * optional string start_balance = 2;
 * @return {string}
 */
proto.sensen.finance.AmortizationRow.prototype.getStartBalance = function() {
  return /** @type {string} */ (jspb.Message.getFieldWithDefault(this, 2, ""));
};


/**
 * @param {string} value
 * @return {!proto.sensen.finance.AmortizationRow} returns this
 */
proto.sensen.finance.AmortizationRow.prototype.setStartBalance = function(value) {
  return jspb.Message.setProto3StringField(this, 2, value);
};


/**
 * optional string scheduled_payment = 3;
 * @return {string}
 */
proto.sensen.finance.AmortizationRow.prototype.getScheduledPayment = function() {
  return /** @type {string} */ (jspb.Message.getFieldWithDefault(this, 3, ""));
};


/**
 * @param {string} value
 * @return {!proto.sensen.finance.AmortizationRow} returns this
 */
proto.sensen.finance.AmortizationRow.prototype.setScheduledPayment = function(value) {
  return jspb.Message.setProto3StringField(this, 3, value);
};


/**
 * optional string extra_payment = 4;
 * @return {string}
 */
proto.sensen.finance.AmortizationRow.prototype.getExtraPayment = function() {
  return /** @type {string} */ (jspb.Message.getFieldWithDefault(this, 4, ""));
};


/**
 * @param {string} value
 * @return {!proto.sensen.finance.AmortizationRow} returns this
 */
proto.sensen.finance.AmortizationRow.prototype.setExtraPayment = function(value) {
  return jspb.Message.setProto3StringField(this, 4, value);
};


/**
 * optional string interest_paid = 5;
 * @return {string}
 */
proto.sensen.finance.AmortizationRow.prototype.getInterestPaid = function() {
  return /** @type {string} */ (jspb.Message.getFieldWithDefault(this, 5, ""));
};


/**
 * @param {string} value
 * @return {!proto.sensen.finance.AmortizationRow} returns this
 */
proto.sensen.finance.AmortizationRow.prototype.setInterestPaid = function(value) {
  return jspb.Message.setProto3StringField(this, 5, value);
};


/**
 * optional string principal_paid = 6;
 * @return {string}
 */
proto.sensen.finance.AmortizationRow.prototype.getPrincipalPaid = function() {
  return /** @type {string} */ (jspb.Message.getFieldWithDefault(this, 6, ""));
};


/**
 * @param {string} value
 * @return {!proto.sensen.finance.AmortizationRow} returns this
 */
proto.sensen.finance.AmortizationRow.prototype.setPrincipalPaid = function(value) {
  return jspb.Message.setProto3StringField(this, 6, value);
};


/**
 * optional string pmi_paid = 7;
 * @return {string}
 */
proto.sensen.finance.AmortizationRow.prototype.getPmiPaid = function() {
  return /** @type {string} */ (jspb.Message.getFieldWithDefault(this, 7, ""));
};


/**
 * @param {string} value
 * @return {!proto.sensen.finance.AmortizationRow} returns this
 */
proto.sensen.finance.AmortizationRow.prototype.setPmiPaid = function(value) {
  return jspb.Message.setProto3StringField(this, 7, value);
};


/**
 * optional string end_balance = 8;
 * @return {string}
 */
proto.sensen.finance.AmortizationRow.prototype.getEndBalance = function() {
  return /** @type {string} */ (jspb.Message.getFieldWithDefault(this, 8, ""));
};


/**
 * @param {string} value
 * @return {!proto.sensen.finance.AmortizationRow} returns this
 */
proto.sensen.finance.AmortizationRow.prototype.setEndBalance = function(value) {
  return jspb.Message.setProto3StringField(this, 8, value);
};





if (jspb.Message.GENERATE_TO_OBJECT) {
/**
 * Creates an object representation of this proto.
 * Field names that are reserved in JavaScript and will be renamed to pb_name.
 * Optional fields that are not set will be set to undefined.
 * To access a reserved field use, foo.pb_<name>, eg, foo.pb_default.
 * For the list of reserved names please see:
 *     net/proto2/compiler/js/internal/generator.cc#kKeyword.
 * @param {boolean=} opt_includeInstance Deprecated. whether to include the
 *     JSPB instance for transitional soy proto support:
 *     http://goto/soy-param-migration
 * @return {!Object}
 */
proto.sensen.finance.MortgageSummary.prototype.toObject = function(opt_includeInstance) {
  return proto.sensen.finance.MortgageSummary.toObject(opt_includeInstance, this);
};


/**
 * Static version of the {@see toObject} method.
 * @param {boolean|undefined} includeInstance Deprecated. Whether to include
 *     the JSPB instance for transitional soy proto support:
 *     http://goto/soy-param-migration
 * @param {!proto.sensen.finance.MortgageSummary} msg The msg instance to transform.
 * @return {!Object}
 * @suppress {unusedLocalVariables} f is only used for nested messages
 */
proto.sensen.finance.MortgageSummary.toObject = function(includeInstance, msg) {
  var f, obj = {
    totalPrincipalPaid: jspb.Message.getFieldWithDefault(msg, 1, ""),
    totalInterestPaid: jspb.Message.getFieldWithDefault(msg, 2, ""),
    totalPmiPaid: jspb.Message.getFieldWithDefault(msg, 3, ""),
    totalPaymentsPaid: jspb.Message.getFieldWithDefault(msg, 4, ""),
    actualTermMonths: jspb.Message.getFieldWithDefault(msg, 5, 0)
  };

  if (includeInstance) {
    obj.$jspbMessageInstance = msg;
  }
  return obj;
};
}


/**
 * Deserializes binary data (in protobuf wire format).
 * @param {jspb.ByteSource} bytes The bytes to deserialize.
 * @return {!proto.sensen.finance.MortgageSummary}
 */
proto.sensen.finance.MortgageSummary.deserializeBinary = function(bytes) {
  var reader = new jspb.BinaryReader(bytes);
  var msg = new proto.sensen.finance.MortgageSummary;
  return proto.sensen.finance.MortgageSummary.deserializeBinaryFromReader(msg, reader);
};


/**
 * Deserializes binary data (in protobuf wire format) from the
 * given reader into the given message object.
 * @param {!proto.sensen.finance.MortgageSummary} msg The message object to deserialize into.
 * @param {!jspb.BinaryReader} reader The BinaryReader to use.
 * @return {!proto.sensen.finance.MortgageSummary}
 */
proto.sensen.finance.MortgageSummary.deserializeBinaryFromReader = function(msg, reader) {
  while (reader.nextField()) {
    if (reader.isEndGroup()) {
      break;
    }
    var field = reader.getFieldNumber();
    switch (field) {
    case 1:
      var value = /** @type {string} */ (reader.readString());
      msg.setTotalPrincipalPaid(value);
      break;
    case 2:
      var value = /** @type {string} */ (reader.readString());
      msg.setTotalInterestPaid(value);
      break;
    case 3:
      var value = /** @type {string} */ (reader.readString());
      msg.setTotalPmiPaid(value);
      break;
    case 4:
      var value = /** @type {string} */ (reader.readString());
      msg.setTotalPaymentsPaid(value);
      break;
    case 5:
      var value = /** @type {number} */ (reader.readInt32());
      msg.setActualTermMonths(value);
      break;
    default:
      reader.skipField();
      break;
    }
  }
  return msg;
};


/**
 * Serializes the message to binary data (in protobuf wire format).
 * @return {!Uint8Array}
 */
proto.sensen.finance.MortgageSummary.prototype.serializeBinary = function() {
  var writer = new jspb.BinaryWriter();
  proto.sensen.finance.MortgageSummary.serializeBinaryToWriter(this, writer);
  return writer.getResultBuffer();
};


/**
 * Serializes the given message to binary data (in protobuf wire
 * format), writing to the given BinaryWriter.
 * @param {!proto.sensen.finance.MortgageSummary} message
 * @param {!jspb.BinaryWriter} writer
 * @suppress {unusedLocalVariables} f is only used for nested messages
 */
proto.sensen.finance.MortgageSummary.serializeBinaryToWriter = function(message, writer) {
  var f = undefined;
  f = message.getTotalPrincipalPaid();
  if (f.length > 0) {
    writer.writeString(
      1,
      f
    );
  }
  f = message.getTotalInterestPaid();
  if (f.length > 0) {
    writer.writeString(
      2,
      f
    );
  }
  f = message.getTotalPmiPaid();
  if (f.length > 0) {
    writer.writeString(
      3,
      f
    );
  }
  f = message.getTotalPaymentsPaid();
  if (f.length > 0) {
    writer.writeString(
      4,
      f
    );
  }
  f = message.getActualTermMonths();
  if (f !== 0) {
    writer.writeInt32(
      5,
      f
    );
  }
};


/**
 * optional string total_principal_paid = 1;
 * @return {string}
 */
proto.sensen.finance.MortgageSummary.prototype.getTotalPrincipalPaid = function() {
  return /** @type {string} */ (jspb.Message.getFieldWithDefault(this, 1, ""));
};


/**
 * @param {string} value
 * @return {!proto.sensen.finance.MortgageSummary} returns this
 */
proto.sensen.finance.MortgageSummary.prototype.setTotalPrincipalPaid = function(value) {
  return jspb.Message.setProto3StringField(this, 1, value);
};


/**
 * optional string total_interest_paid = 2;
 * @return {string}
 */
proto.sensen.finance.MortgageSummary.prototype.getTotalInterestPaid = function() {
  return /** @type {string} */ (jspb.Message.getFieldWithDefault(this, 2, ""));
};


/**
 * @param {string} value
 * @return {!proto.sensen.finance.MortgageSummary} returns this
 */
proto.sensen.finance.MortgageSummary.prototype.setTotalInterestPaid = function(value) {
  return jspb.Message.setProto3StringField(this, 2, value);
};


/**
 * optional string total_pmi_paid = 3;
 * @return {string}
 */
proto.sensen.finance.MortgageSummary.prototype.getTotalPmiPaid = function() {
  return /** @type {string} */ (jspb.Message.getFieldWithDefault(this, 3, ""));
};


/**
 * @param {string} value
 * @return {!proto.sensen.finance.MortgageSummary} returns this
 */
proto.sensen.finance.MortgageSummary.prototype.setTotalPmiPaid = function(value) {
  return jspb.Message.setProto3StringField(this, 3, value);
};


/**
 * optional string total_payments_paid = 4;
 * @return {string}
 */
proto.sensen.finance.MortgageSummary.prototype.getTotalPaymentsPaid = function() {
  return /** @type {string} */ (jspb.Message.getFieldWithDefault(this, 4, ""));
};


/**
 * @param {string} value
 * @return {!proto.sensen.finance.MortgageSummary} returns this
 */
proto.sensen.finance.MortgageSummary.prototype.setTotalPaymentsPaid = function(value) {
  return jspb.Message.setProto3StringField(this, 4, value);
};


/**
 * optional int32 actual_term_months = 5;
 * @return {number}
 */
proto.sensen.finance.MortgageSummary.prototype.getActualTermMonths = function() {
  return /** @type {number} */ (jspb.Message.getFieldWithDefault(this, 5, 0));
};


/**
 * @param {number} value
 * @return {!proto.sensen.finance.MortgageSummary} returns this
 */
proto.sensen.finance.MortgageSummary.prototype.setActualTermMonths = function(value) {
  return jspb.Message.setProto3IntField(this, 5, value);
};



/**
 * List of repeated fields within this message type.
 * @private {!Array<number>}
 * @const
 */
proto.sensen.finance.AmortizationResponse.repeatedFields_ = [1];



if (jspb.Message.GENERATE_TO_OBJECT) {
/**
 * Creates an object representation of this proto.
 * Field names that are reserved in JavaScript and will be renamed to pb_name.
 * Optional fields that are not set will be set to undefined.
 * To access a reserved field use, foo.pb_<name>, eg, foo.pb_default.
 * For the list of reserved names please see:
 *     net/proto2/compiler/js/internal/generator.cc#kKeyword.
 * @param {boolean=} opt_includeInstance Deprecated. whether to include the
 *     JSPB instance for transitional soy proto support:
 *     http://goto/soy-param-migration
 * @return {!Object}
 */
proto.sensen.finance.AmortizationResponse.prototype.toObject = function(opt_includeInstance) {
  return proto.sensen.finance.AmortizationResponse.toObject(opt_includeInstance, this);
};


/**
 * Static version of the {@see toObject} method.
 * @param {boolean|undefined} includeInstance Deprecated. Whether to include
 *     the JSPB instance for transitional soy proto support:
 *     http://goto/soy-param-migration
 * @param {!proto.sensen.finance.AmortizationResponse} msg The msg instance to transform.
 * @return {!Object}
 * @suppress {unusedLocalVariables} f is only used for nested messages
 */
proto.sensen.finance.AmortizationResponse.toObject = function(includeInstance, msg) {
  var f, obj = {
    scheduleList: jspb.Message.toObjectList(msg.getScheduleList(),
    proto.sensen.finance.AmortizationRow.toObject, includeInstance),
    summary: (f = msg.getSummary()) && proto.sensen.finance.MortgageSummary.toObject(includeInstance, f)
  };

  if (includeInstance) {
    obj.$jspbMessageInstance = msg;
  }
  return obj;
};
}


/**
 * Deserializes binary data (in protobuf wire format).
 * @param {jspb.ByteSource} bytes The bytes to deserialize.
 * @return {!proto.sensen.finance.AmortizationResponse}
 */
proto.sensen.finance.AmortizationResponse.deserializeBinary = function(bytes) {
  var reader = new jspb.BinaryReader(bytes);
  var msg = new proto.sensen.finance.AmortizationResponse;
  return proto.sensen.finance.AmortizationResponse.deserializeBinaryFromReader(msg, reader);
};


/**
 * Deserializes binary data (in protobuf wire format) from the
 * given reader into the given message object.
 * @param {!proto.sensen.finance.AmortizationResponse} msg The message object to deserialize into.
 * @param {!jspb.BinaryReader} reader The BinaryReader to use.
 * @return {!proto.sensen.finance.AmortizationResponse}
 */
proto.sensen.finance.AmortizationResponse.deserializeBinaryFromReader = function(msg, reader) {
  while (reader.nextField()) {
    if (reader.isEndGroup()) {
      break;
    }
    var field = reader.getFieldNumber();
    switch (field) {
    case 1:
      var value = new proto.sensen.finance.AmortizationRow;
      reader.readMessage(value,proto.sensen.finance.AmortizationRow.deserializeBinaryFromReader);
      msg.addSchedule(value);
      break;
    case 2:
      var value = new proto.sensen.finance.MortgageSummary;
      reader.readMessage(value,proto.sensen.finance.MortgageSummary.deserializeBinaryFromReader);
      msg.setSummary(value);
      break;
    default:
      reader.skipField();
      break;
    }
  }
  return msg;
};


/**
 * Serializes the message to binary data (in protobuf wire format).
 * @return {!Uint8Array}
 */
proto.sensen.finance.AmortizationResponse.prototype.serializeBinary = function() {
  var writer = new jspb.BinaryWriter();
  proto.sensen.finance.AmortizationResponse.serializeBinaryToWriter(this, writer);
  return writer.getResultBuffer();
};


/**
 * Serializes the given message to binary data (in protobuf wire
 * format), writing to the given BinaryWriter.
 * @param {!proto.sensen.finance.AmortizationResponse} message
 * @param {!jspb.BinaryWriter} writer
 * @suppress {unusedLocalVariables} f is only used for nested messages
 */
proto.sensen.finance.AmortizationResponse.serializeBinaryToWriter = function(message, writer) {
  var f = undefined;
  f = message.getScheduleList();
  if (f.length > 0) {
    writer.writeRepeatedMessage(
      1,
      f,
      proto.sensen.finance.AmortizationRow.serializeBinaryToWriter
    );
  }
  f = message.getSummary();
  if (f != null) {
    writer.writeMessage(
      2,
      f,
      proto.sensen.finance.MortgageSummary.serializeBinaryToWriter
    );
  }
};


/**
 * repeated AmortizationRow schedule = 1;
 * @return {!Array<!proto.sensen.finance.AmortizationRow>}
 */
proto.sensen.finance.AmortizationResponse.prototype.getScheduleList = function() {
  return /** @type{!Array<!proto.sensen.finance.AmortizationRow>} */ (
    jspb.Message.getRepeatedWrapperField(this, proto.sensen.finance.AmortizationRow, 1));
};


/**
 * @param {!Array<!proto.sensen.finance.AmortizationRow>} value
 * @return {!proto.sensen.finance.AmortizationResponse} returns this
*/
proto.sensen.finance.AmortizationResponse.prototype.setScheduleList = function(value) {
  return jspb.Message.setRepeatedWrapperField(this, 1, value);
};


/**
 * @param {!proto.sensen.finance.AmortizationRow=} opt_value
 * @param {number=} opt_index
 * @return {!proto.sensen.finance.AmortizationRow}
 */
proto.sensen.finance.AmortizationResponse.prototype.addSchedule = function(opt_value, opt_index) {
  return jspb.Message.addToRepeatedWrapperField(this, 1, opt_value, proto.sensen.finance.AmortizationRow, opt_index);
};


/**
 * Clears the list making it empty but non-null.
 * @return {!proto.sensen.finance.AmortizationResponse} returns this
 */
proto.sensen.finance.AmortizationResponse.prototype.clearScheduleList = function() {
  return this.setScheduleList([]);
};


/**
 * optional MortgageSummary summary = 2;
 * @return {?proto.sensen.finance.MortgageSummary}
 */
proto.sensen.finance.AmortizationResponse.prototype.getSummary = function() {
  return /** @type{?proto.sensen.finance.MortgageSummary} */ (
    jspb.Message.getWrapperField(this, proto.sensen.finance.MortgageSummary, 2));
};


/**
 * @param {?proto.sensen.finance.MortgageSummary|undefined} value
 * @return {!proto.sensen.finance.AmortizationResponse} returns this
*/
proto.sensen.finance.AmortizationResponse.prototype.setSummary = function(value) {
  return jspb.Message.setWrapperField(this, 2, value);
};


/**
 * Clears the message field making it undefined.
 * @return {!proto.sensen.finance.AmortizationResponse} returns this
 */
proto.sensen.finance.AmortizationResponse.prototype.clearSummary = function() {
  return this.setSummary(undefined);
};


/**
 * Returns whether this field is set.
 * @return {boolean}
 */
proto.sensen.finance.AmortizationResponse.prototype.hasSummary = function() {
  return jspb.Message.getField(this, 2) != null;
};





if (jspb.Message.GENERATE_TO_OBJECT) {
/**
 * Creates an object representation of this proto.
 * Field names that are reserved in JavaScript and will be renamed to pb_name.
 * Optional fields that are not set will be set to undefined.
 * To access a reserved field use, foo.pb_<name>, eg, foo.pb_default.
 * For the list of reserved names please see:
 *     net/proto2/compiler/js/internal/generator.cc#kKeyword.
 * @param {boolean=} opt_includeInstance Deprecated. whether to include the
 *     JSPB instance for transitional soy proto support:
 *     http://goto/soy-param-migration
 * @return {!Object}
 */
proto.sensen.finance.DetailedAmortizationRequest.prototype.toObject = function(opt_includeInstance) {
  return proto.sensen.finance.DetailedAmortizationRequest.toObject(opt_includeInstance, this);
};


/**
 * Static version of the {@see toObject} method.
 * @param {boolean|undefined} includeInstance Deprecated. Whether to include
 *     the JSPB instance for transitional soy proto support:
 *     http://goto/soy-param-migration
 * @param {!proto.sensen.finance.DetailedAmortizationRequest} msg The msg instance to transform.
 * @return {!Object}
 * @suppress {unusedLocalVariables} f is only used for nested messages
 */
proto.sensen.finance.DetailedAmortizationRequest.toObject = function(includeInstance, msg) {
  var f, obj = {
    loanAmount: jspb.Message.getFieldWithDefault(msg, 1, ""),
    annualRate: jspb.Message.getFieldWithDefault(msg, 2, ""),
    termMonths: jspb.Message.getFieldWithDefault(msg, 3, 0),
    monthlyOverpayment: jspb.Message.getFieldWithDefault(msg, 4, ""),
    pmiAnnualRate: jspb.Message.getFieldWithDefault(msg, 5, ""),
    originalHomeValue: jspb.Message.getFieldWithDefault(msg, 6, ""),
    annualTaxRate: jspb.Message.getFieldWithDefault(msg, 7, "")
  };

  if (includeInstance) {
    obj.$jspbMessageInstance = msg;
  }
  return obj;
};
}


/**
 * Deserializes binary data (in protobuf wire format).
 * @param {jspb.ByteSource} bytes The bytes to deserialize.
 * @return {!proto.sensen.finance.DetailedAmortizationRequest}
 */
proto.sensen.finance.DetailedAmortizationRequest.deserializeBinary = function(bytes) {
  var reader = new jspb.BinaryReader(bytes);
  var msg = new proto.sensen.finance.DetailedAmortizationRequest;
  return proto.sensen.finance.DetailedAmortizationRequest.deserializeBinaryFromReader(msg, reader);
};


/**
 * Deserializes binary data (in protobuf wire format) from the
 * given reader into the given message object.
 * @param {!proto.sensen.finance.DetailedAmortizationRequest} msg The message object to deserialize into.
 * @param {!jspb.BinaryReader} reader The BinaryReader to use.
 * @return {!proto.sensen.finance.DetailedAmortizationRequest}
 */
proto.sensen.finance.DetailedAmortizationRequest.deserializeBinaryFromReader = function(msg, reader) {
  while (reader.nextField()) {
    if (reader.isEndGroup()) {
      break;
    }
    var field = reader.getFieldNumber();
    switch (field) {
    case 1:
      var value = /** @type {string} */ (reader.readString());
      msg.setLoanAmount(value);
      break;
    case 2:
      var value = /** @type {string} */ (reader.readString());
      msg.setAnnualRate(value);
      break;
    case 3:
      var value = /** @type {number} */ (reader.readInt32());
      msg.setTermMonths(value);
      break;
    case 4:
      var value = /** @type {string} */ (reader.readString());
      msg.setMonthlyOverpayment(value);
      break;
    case 5:
      var value = /** @type {string} */ (reader.readString());
      msg.setPmiAnnualRate(value);
      break;
    case 6:
      var value = /** @type {string} */ (reader.readString());
      msg.setOriginalHomeValue(value);
      break;
    case 7:
      var value = /** @type {string} */ (reader.readString());
      msg.setAnnualTaxRate(value);
      break;
    default:
      reader.skipField();
      break;
    }
  }
  return msg;
};


/**
 * Serializes the message to binary data (in protobuf wire format).
 * @return {!Uint8Array}
 */
proto.sensen.finance.DetailedAmortizationRequest.prototype.serializeBinary = function() {
  var writer = new jspb.BinaryWriter();
  proto.sensen.finance.DetailedAmortizationRequest.serializeBinaryToWriter(this, writer);
  return writer.getResultBuffer();
};


/**
 * Serializes the given message to binary data (in protobuf wire
 * format), writing to the given BinaryWriter.
 * @param {!proto.sensen.finance.DetailedAmortizationRequest} message
 * @param {!jspb.BinaryWriter} writer
 * @suppress {unusedLocalVariables} f is only used for nested messages
 */
proto.sensen.finance.DetailedAmortizationRequest.serializeBinaryToWriter = function(message, writer) {
  var f = undefined;
  f = message.getLoanAmount();
  if (f.length > 0) {
    writer.writeString(
      1,
      f
    );
  }
  f = message.getAnnualRate();
  if (f.length > 0) {
    writer.writeString(
      2,
      f
    );
  }
  f = message.getTermMonths();
  if (f !== 0) {
    writer.writeInt32(
      3,
      f
    );
  }
  f = message.getMonthlyOverpayment();
  if (f.length > 0) {
    writer.writeString(
      4,
      f
    );
  }
  f = message.getPmiAnnualRate();
  if (f.length > 0) {
    writer.writeString(
      5,
      f
    );
  }
  f = message.getOriginalHomeValue();
  if (f.length > 0) {
    writer.writeString(
      6,
      f
    );
  }
  f = message.getAnnualTaxRate();
  if (f.length > 0) {
    writer.writeString(
      7,
      f
    );
  }
};


/**
 * optional string loan_amount = 1;
 * @return {string}
 */
proto.sensen.finance.DetailedAmortizationRequest.prototype.getLoanAmount = function() {
  return /** @type {string} */ (jspb.Message.getFieldWithDefault(this, 1, ""));
};


/**
 * @param {string} value
 * @return {!proto.sensen.finance.DetailedAmortizationRequest} returns this
 */
proto.sensen.finance.DetailedAmortizationRequest.prototype.setLoanAmount = function(value) {
  return jspb.Message.setProto3StringField(this, 1, value);
};


/**
 * optional string annual_rate = 2;
 * @return {string}
 */
proto.sensen.finance.DetailedAmortizationRequest.prototype.getAnnualRate = function() {
  return /** @type {string} */ (jspb.Message.getFieldWithDefault(this, 2, ""));
};


/**
 * @param {string} value
 * @return {!proto.sensen.finance.DetailedAmortizationRequest} returns this
 */
proto.sensen.finance.DetailedAmortizationRequest.prototype.setAnnualRate = function(value) {
  return jspb.Message.setProto3StringField(this, 2, value);
};


/**
 * optional int32 term_months = 3;
 * @return {number}
 */
proto.sensen.finance.DetailedAmortizationRequest.prototype.getTermMonths = function() {
  return /** @type {number} */ (jspb.Message.getFieldWithDefault(this, 3, 0));
};


/**
 * @param {number} value
 * @return {!proto.sensen.finance.DetailedAmortizationRequest} returns this
 */
proto.sensen.finance.DetailedAmortizationRequest.prototype.setTermMonths = function(value) {
  return jspb.Message.setProto3IntField(this, 3, value);
};


/**
 * optional string monthly_overpayment = 4;
 * @return {string}
 */
proto.sensen.finance.DetailedAmortizationRequest.prototype.getMonthlyOverpayment = function() {
  return /** @type {string} */ (jspb.Message.getFieldWithDefault(this, 4, ""));
};


/**
 * @param {string} value
 * @return {!proto.sensen.finance.DetailedAmortizationRequest} returns this
 */
proto.sensen.finance.DetailedAmortizationRequest.prototype.setMonthlyOverpayment = function(value) {
  return jspb.Message.setProto3StringField(this, 4, value);
};


/**
 * optional string pmi_annual_rate = 5;
 * @return {string}
 */
proto.sensen.finance.DetailedAmortizationRequest.prototype.getPmiAnnualRate = function() {
  return /** @type {string} */ (jspb.Message.getFieldWithDefault(this, 5, ""));
};


/**
 * @param {string} value
 * @return {!proto.sensen.finance.DetailedAmortizationRequest} returns this
 */
proto.sensen.finance.DetailedAmortizationRequest.prototype.setPmiAnnualRate = function(value) {
  return jspb.Message.setProto3StringField(this, 5, value);
};


/**
 * optional string original_home_value = 6;
 * @return {string}
 */
proto.sensen.finance.DetailedAmortizationRequest.prototype.getOriginalHomeValue = function() {
  return /** @type {string} */ (jspb.Message.getFieldWithDefault(this, 6, ""));
};


/**
 * @param {string} value
 * @return {!proto.sensen.finance.DetailedAmortizationRequest} returns this
 */
proto.sensen.finance.DetailedAmortizationRequest.prototype.setOriginalHomeValue = function(value) {
  return jspb.Message.setProto3StringField(this, 6, value);
};


/**
 * optional string annual_tax_rate = 7;
 * @return {string}
 */
proto.sensen.finance.DetailedAmortizationRequest.prototype.getAnnualTaxRate = function() {
  return /** @type {string} */ (jspb.Message.getFieldWithDefault(this, 7, ""));
};


/**
 * @param {string} value
 * @return {!proto.sensen.finance.DetailedAmortizationRequest} returns this
 */
proto.sensen.finance.DetailedAmortizationRequest.prototype.setAnnualTaxRate = function(value) {
  return jspb.Message.setProto3StringField(this, 7, value);
};





if (jspb.Message.GENERATE_TO_OBJECT) {
/**
 * Creates an object representation of this proto.
 * Field names that are reserved in JavaScript and will be renamed to pb_name.
 * Optional fields that are not set will be set to undefined.
 * To access a reserved field use, foo.pb_<name>, eg, foo.pb_default.
 * For the list of reserved names please see:
 *     net/proto2/compiler/js/internal/generator.cc#kKeyword.
 * @param {boolean=} opt_includeInstance Deprecated. whether to include the
 *     JSPB instance for transitional soy proto support:
 *     http://goto/soy-param-migration
 * @return {!Object}
 */
proto.sensen.finance.DetailedAmortizationRow.prototype.toObject = function(opt_includeInstance) {
  return proto.sensen.finance.DetailedAmortizationRow.toObject(opt_includeInstance, this);
};


/**
 * Static version of the {@see toObject} method.
 * @param {boolean|undefined} includeInstance Deprecated. Whether to include
 *     the JSPB instance for transitional soy proto support:
 *     http://goto/soy-param-migration
 * @param {!proto.sensen.finance.DetailedAmortizationRow} msg The msg instance to transform.
 * @return {!Object}
 * @suppress {unusedLocalVariables} f is only used for nested messages
 */
proto.sensen.finance.DetailedAmortizationRow.toObject = function(includeInstance, msg) {
  var f, obj = {
    period: jspb.Message.getFieldWithDefault(msg, 1, 0),
    startBalance: jspb.Message.getFieldWithDefault(msg, 2, ""),
    scheduledPayment: jspb.Message.getFieldWithDefault(msg, 3, ""),
    extraPayment: jspb.Message.getFieldWithDefault(msg, 4, ""),
    interestPaid: jspb.Message.getFieldWithDefault(msg, 5, ""),
    principalPaid: jspb.Message.getFieldWithDefault(msg, 6, ""),
    pmiPaid: jspb.Message.getFieldWithDefault(msg, 7, ""),
    taxSavings: jspb.Message.getFieldWithDefault(msg, 8, ""),
    endBalance: jspb.Message.getFieldWithDefault(msg, 9, "")
  };

  if (includeInstance) {
    obj.$jspbMessageInstance = msg;
  }
  return obj;
};
}


/**
 * Deserializes binary data (in protobuf wire format).
 * @param {jspb.ByteSource} bytes The bytes to deserialize.
 * @return {!proto.sensen.finance.DetailedAmortizationRow}
 */
proto.sensen.finance.DetailedAmortizationRow.deserializeBinary = function(bytes) {
  var reader = new jspb.BinaryReader(bytes);
  var msg = new proto.sensen.finance.DetailedAmortizationRow;
  return proto.sensen.finance.DetailedAmortizationRow.deserializeBinaryFromReader(msg, reader);
};


/**
 * Deserializes binary data (in protobuf wire format) from the
 * given reader into the given message object.
 * @param {!proto.sensen.finance.DetailedAmortizationRow} msg The message object to deserialize into.
 * @param {!jspb.BinaryReader} reader The BinaryReader to use.
 * @return {!proto.sensen.finance.DetailedAmortizationRow}
 */
proto.sensen.finance.DetailedAmortizationRow.deserializeBinaryFromReader = function(msg, reader) {
  while (reader.nextField()) {
    if (reader.isEndGroup()) {
      break;
    }
    var field = reader.getFieldNumber();
    switch (field) {
    case 1:
      var value = /** @type {number} */ (reader.readInt32());
      msg.setPeriod(value);
      break;
    case 2:
      var value = /** @type {string} */ (reader.readString());
      msg.setStartBalance(value);
      break;
    case 3:
      var value = /** @type {string} */ (reader.readString());
      msg.setScheduledPayment(value);
      break;
    case 4:
      var value = /** @type {string} */ (reader.readString());
      msg.setExtraPayment(value);
      break;
    case 5:
      var value = /** @type {string} */ (reader.readString());
      msg.setInterestPaid(value);
      break;
    case 6:
      var value = /** @type {string} */ (reader.readString());
      msg.setPrincipalPaid(value);
      break;
    case 7:
      var value = /** @type {string} */ (reader.readString());
      msg.setPmiPaid(value);
      break;
    case 8:
      var value = /** @type {string} */ (reader.readString());
      msg.setTaxSavings(value);
      break;
    case 9:
      var value = /** @type {string} */ (reader.readString());
      msg.setEndBalance(value);
      break;
    default:
      reader.skipField();
      break;
    }
  }
  return msg;
};


/**
 * Serializes the message to binary data (in protobuf wire format).
 * @return {!Uint8Array}
 */
proto.sensen.finance.DetailedAmortizationRow.prototype.serializeBinary = function() {
  var writer = new jspb.BinaryWriter();
  proto.sensen.finance.DetailedAmortizationRow.serializeBinaryToWriter(this, writer);
  return writer.getResultBuffer();
};


/**
 * Serializes the given message to binary data (in protobuf wire
 * format), writing to the given BinaryWriter.
 * @param {!proto.sensen.finance.DetailedAmortizationRow} message
 * @param {!jspb.BinaryWriter} writer
 * @suppress {unusedLocalVariables} f is only used for nested messages
 */
proto.sensen.finance.DetailedAmortizationRow.serializeBinaryToWriter = function(message, writer) {
  var f = undefined;
  f = message.getPeriod();
  if (f !== 0) {
    writer.writeInt32(
      1,
      f
    );
  }
  f = message.getStartBalance();
  if (f.length > 0) {
    writer.writeString(
      2,
      f
    );
  }
  f = message.getScheduledPayment();
  if (f.length > 0) {
    writer.writeString(
      3,
      f
    );
  }
  f = message.getExtraPayment();
  if (f.length > 0) {
    writer.writeString(
      4,
      f
    );
  }
  f = message.getInterestPaid();
  if (f.length > 0) {
    writer.writeString(
      5,
      f
    );
  }
  f = message.getPrincipalPaid();
  if (f.length > 0) {
    writer.writeString(
      6,
      f
    );
  }
  f = message.getPmiPaid();
  if (f.length > 0) {
    writer.writeString(
      7,
      f
    );
  }
  f = message.getTaxSavings();
  if (f.length > 0) {
    writer.writeString(
      8,
      f
    );
  }
  f = message.getEndBalance();
  if (f.length > 0) {
    writer.writeString(
      9,
      f
    );
  }
};


/**
 * optional int32 period = 1;
 * @return {number}
 */
proto.sensen.finance.DetailedAmortizationRow.prototype.getPeriod = function() {
  return /** @type {number} */ (jspb.Message.getFieldWithDefault(this, 1, 0));
};


/**
 * @param {number} value
 * @return {!proto.sensen.finance.DetailedAmortizationRow} returns this
 */
proto.sensen.finance.DetailedAmortizationRow.prototype.setPeriod = function(value) {
  return jspb.Message.setProto3IntField(this, 1, value);
};


/**
 * optional string start_balance = 2;
 * @return {string}
 */
proto.sensen.finance.DetailedAmortizationRow.prototype.getStartBalance = function() {
  return /** @type {string} */ (jspb.Message.getFieldWithDefault(this, 2, ""));
};


/**
 * @param {string} value
 * @return {!proto.sensen.finance.DetailedAmortizationRow} returns this
 */
proto.sensen.finance.DetailedAmortizationRow.prototype.setStartBalance = function(value) {
  return jspb.Message.setProto3StringField(this, 2, value);
};


/**
 * optional string scheduled_payment = 3;
 * @return {string}
 */
proto.sensen.finance.DetailedAmortizationRow.prototype.getScheduledPayment = function() {
  return /** @type {string} */ (jspb.Message.getFieldWithDefault(this, 3, ""));
};


/**
 * @param {string} value
 * @return {!proto.sensen.finance.DetailedAmortizationRow} returns this
 */
proto.sensen.finance.DetailedAmortizationRow.prototype.setScheduledPayment = function(value) {
  return jspb.Message.setProto3StringField(this, 3, value);
};


/**
 * optional string extra_payment = 4;
 * @return {string}
 */
proto.sensen.finance.DetailedAmortizationRow.prototype.getExtraPayment = function() {
  return /** @type {string} */ (jspb.Message.getFieldWithDefault(this, 4, ""));
};


/**
 * @param {string} value
 * @return {!proto.sensen.finance.DetailedAmortizationRow} returns this
 */
proto.sensen.finance.DetailedAmortizationRow.prototype.setExtraPayment = function(value) {
  return jspb.Message.setProto3StringField(this, 4, value);
};


/**
 * optional string interest_paid = 5;
 * @return {string}
 */
proto.sensen.finance.DetailedAmortizationRow.prototype.getInterestPaid = function() {
  return /** @type {string} */ (jspb.Message.getFieldWithDefault(this, 5, ""));
};


/**
 * @param {string} value
 * @return {!proto.sensen.finance.DetailedAmortizationRow} returns this
 */
proto.sensen.finance.DetailedAmortizationRow.prototype.setInterestPaid = function(value) {
  return jspb.Message.setProto3StringField(this, 5, value);
};


/**
 * optional string principal_paid = 6;
 * @return {string}
 */
proto.sensen.finance.DetailedAmortizationRow.prototype.getPrincipalPaid = function() {
  return /** @type {string} */ (jspb.Message.getFieldWithDefault(this, 6, ""));
};


/**
 * @param {string} value
 * @return {!proto.sensen.finance.DetailedAmortizationRow} returns this
 */
proto.sensen.finance.DetailedAmortizationRow.prototype.setPrincipalPaid = function(value) {
  return jspb.Message.setProto3StringField(this, 6, value);
};


/**
 * optional string pmi_paid = 7;
 * @return {string}
 */
proto.sensen.finance.DetailedAmortizationRow.prototype.getPmiPaid = function() {
  return /** @type {string} */ (jspb.Message.getFieldWithDefault(this, 7, ""));
};


/**
 * @param {string} value
 * @return {!proto.sensen.finance.DetailedAmortizationRow} returns this
 */
proto.sensen.finance.DetailedAmortizationRow.prototype.setPmiPaid = function(value) {
  return jspb.Message.setProto3StringField(this, 7, value);
};


/**
 * optional string tax_savings = 8;
 * @return {string}
 */
proto.sensen.finance.DetailedAmortizationRow.prototype.getTaxSavings = function() {
  return /** @type {string} */ (jspb.Message.getFieldWithDefault(this, 8, ""));
};


/**
 * @param {string} value
 * @return {!proto.sensen.finance.DetailedAmortizationRow} returns this
 */
proto.sensen.finance.DetailedAmortizationRow.prototype.setTaxSavings = function(value) {
  return jspb.Message.setProto3StringField(this, 8, value);
};


/**
 * optional string end_balance = 9;
 * @return {string}
 */
proto.sensen.finance.DetailedAmortizationRow.prototype.getEndBalance = function() {
  return /** @type {string} */ (jspb.Message.getFieldWithDefault(this, 9, ""));
};


/**
 * @param {string} value
 * @return {!proto.sensen.finance.DetailedAmortizationRow} returns this
 */
proto.sensen.finance.DetailedAmortizationRow.prototype.setEndBalance = function(value) {
  return jspb.Message.setProto3StringField(this, 9, value);
};





if (jspb.Message.GENERATE_TO_OBJECT) {
/**
 * Creates an object representation of this proto.
 * Field names that are reserved in JavaScript and will be renamed to pb_name.
 * Optional fields that are not set will be set to undefined.
 * To access a reserved field use, foo.pb_<name>, eg, foo.pb_default.
 * For the list of reserved names please see:
 *     net/proto2/compiler/js/internal/generator.cc#kKeyword.
 * @param {boolean=} opt_includeInstance Deprecated. whether to include the
 *     JSPB instance for transitional soy proto support:
 *     http://goto/soy-param-migration
 * @return {!Object}
 */
proto.sensen.finance.DetailedMortgageSummary.prototype.toObject = function(opt_includeInstance) {
  return proto.sensen.finance.DetailedMortgageSummary.toObject(opt_includeInstance, this);
};


/**
 * Static version of the {@see toObject} method.
 * @param {boolean|undefined} includeInstance Deprecated. Whether to include
 *     the JSPB instance for transitional soy proto support:
 *     http://goto/soy-param-migration
 * @param {!proto.sensen.finance.DetailedMortgageSummary} msg The msg instance to transform.
 * @return {!Object}
 * @suppress {unusedLocalVariables} f is only used for nested messages
 */
proto.sensen.finance.DetailedMortgageSummary.toObject = function(includeInstance, msg) {
  var f, obj = {
    totalPrincipalPaid: jspb.Message.getFieldWithDefault(msg, 1, ""),
    totalInterestPaid: jspb.Message.getFieldWithDefault(msg, 2, ""),
    totalPmiPaid: jspb.Message.getFieldWithDefault(msg, 3, ""),
    totalPaymentsPaid: jspb.Message.getFieldWithDefault(msg, 4, ""),
    totalTaxSavings: jspb.Message.getFieldWithDefault(msg, 5, ""),
    actualTermMonths: jspb.Message.getFieldWithDefault(msg, 6, 0)
  };

  if (includeInstance) {
    obj.$jspbMessageInstance = msg;
  }
  return obj;
};
}


/**
 * Deserializes binary data (in protobuf wire format).
 * @param {jspb.ByteSource} bytes The bytes to deserialize.
 * @return {!proto.sensen.finance.DetailedMortgageSummary}
 */
proto.sensen.finance.DetailedMortgageSummary.deserializeBinary = function(bytes) {
  var reader = new jspb.BinaryReader(bytes);
  var msg = new proto.sensen.finance.DetailedMortgageSummary;
  return proto.sensen.finance.DetailedMortgageSummary.deserializeBinaryFromReader(msg, reader);
};


/**
 * Deserializes binary data (in protobuf wire format) from the
 * given reader into the given message object.
 * @param {!proto.sensen.finance.DetailedMortgageSummary} msg The message object to deserialize into.
 * @param {!jspb.BinaryReader} reader The BinaryReader to use.
 * @return {!proto.sensen.finance.DetailedMortgageSummary}
 */
proto.sensen.finance.DetailedMortgageSummary.deserializeBinaryFromReader = function(msg, reader) {
  while (reader.nextField()) {
    if (reader.isEndGroup()) {
      break;
    }
    var field = reader.getFieldNumber();
    switch (field) {
    case 1:
      var value = /** @type {string} */ (reader.readString());
      msg.setTotalPrincipalPaid(value);
      break;
    case 2:
      var value = /** @type {string} */ (reader.readString());
      msg.setTotalInterestPaid(value);
      break;
    case 3:
      var value = /** @type {string} */ (reader.readString());
      msg.setTotalPmiPaid(value);
      break;
    case 4:
      var value = /** @type {string} */ (reader.readString());
      msg.setTotalPaymentsPaid(value);
      break;
    case 5:
      var value = /** @type {string} */ (reader.readString());
      msg.setTotalTaxSavings(value);
      break;
    case 6:
      var value = /** @type {number} */ (reader.readInt32());
      msg.setActualTermMonths(value);
      break;
    default:
      reader.skipField();
      break;
    }
  }
  return msg;
};


/**
 * Serializes the message to binary data (in protobuf wire format).
 * @return {!Uint8Array}
 */
proto.sensen.finance.DetailedMortgageSummary.prototype.serializeBinary = function() {
  var writer = new jspb.BinaryWriter();
  proto.sensen.finance.DetailedMortgageSummary.serializeBinaryToWriter(this, writer);
  return writer.getResultBuffer();
};


/**
 * Serializes the given message to binary data (in protobuf wire
 * format), writing to the given BinaryWriter.
 * @param {!proto.sensen.finance.DetailedMortgageSummary} message
 * @param {!jspb.BinaryWriter} writer
 * @suppress {unusedLocalVariables} f is only used for nested messages
 */
proto.sensen.finance.DetailedMortgageSummary.serializeBinaryToWriter = function(message, writer) {
  var f = undefined;
  f = message.getTotalPrincipalPaid();
  if (f.length > 0) {
    writer.writeString(
      1,
      f
    );
  }
  f = message.getTotalInterestPaid();
  if (f.length > 0) {
    writer.writeString(
      2,
      f
    );
  }
  f = message.getTotalPmiPaid();
  if (f.length > 0) {
    writer.writeString(
      3,
      f
    );
  }
  f = message.getTotalPaymentsPaid();
  if (f.length > 0) {
    writer.writeString(
      4,
      f
    );
  }
  f = message.getTotalTaxSavings();
  if (f.length > 0) {
    writer.writeString(
      5,
      f
    );
  }
  f = message.getActualTermMonths();
  if (f !== 0) {
    writer.writeInt32(
      6,
      f
    );
  }
};


/**
 * optional string total_principal_paid = 1;
 * @return {string}
 */
proto.sensen.finance.DetailedMortgageSummary.prototype.getTotalPrincipalPaid = function() {
  return /** @type {string} */ (jspb.Message.getFieldWithDefault(this, 1, ""));
};


/**
 * @param {string} value
 * @return {!proto.sensen.finance.DetailedMortgageSummary} returns this
 */
proto.sensen.finance.DetailedMortgageSummary.prototype.setTotalPrincipalPaid = function(value) {
  return jspb.Message.setProto3StringField(this, 1, value);
};


/**
 * optional string total_interest_paid = 2;
 * @return {string}
 */
proto.sensen.finance.DetailedMortgageSummary.prototype.getTotalInterestPaid = function() {
  return /** @type {string} */ (jspb.Message.getFieldWithDefault(this, 2, ""));
};


/**
 * @param {string} value
 * @return {!proto.sensen.finance.DetailedMortgageSummary} returns this
 */
proto.sensen.finance.DetailedMortgageSummary.prototype.setTotalInterestPaid = function(value) {
  return jspb.Message.setProto3StringField(this, 2, value);
};


/**
 * optional string total_pmi_paid = 3;
 * @return {string}
 */
proto.sensen.finance.DetailedMortgageSummary.prototype.getTotalPmiPaid = function() {
  return /** @type {string} */ (jspb.Message.getFieldWithDefault(this, 3, ""));
};


/**
 * @param {string} value
 * @return {!proto.sensen.finance.DetailedMortgageSummary} returns this
 */
proto.sensen.finance.DetailedMortgageSummary.prototype.setTotalPmiPaid = function(value) {
  return jspb.Message.setProto3StringField(this, 3, value);
};


/**
 * optional string total_payments_paid = 4;
 * @return {string}
 */
proto.sensen.finance.DetailedMortgageSummary.prototype.getTotalPaymentsPaid = function() {
  return /** @type {string} */ (jspb.Message.getFieldWithDefault(this, 4, ""));
};


/**
 * @param {string} value
 * @return {!proto.sensen.finance.DetailedMortgageSummary} returns this
 */
proto.sensen.finance.DetailedMortgageSummary.prototype.setTotalPaymentsPaid = function(value) {
  return jspb.Message.setProto3StringField(this, 4, value);
};


/**
 * optional string total_tax_savings = 5;
 * @return {string}
 */
proto.sensen.finance.DetailedMortgageSummary.prototype.getTotalTaxSavings = function() {
  return /** @type {string} */ (jspb.Message.getFieldWithDefault(this, 5, ""));
};


/**
 * @param {string} value
 * @return {!proto.sensen.finance.DetailedMortgageSummary} returns this
 */
proto.sensen.finance.DetailedMortgageSummary.prototype.setTotalTaxSavings = function(value) {
  return jspb.Message.setProto3StringField(this, 5, value);
};


/**
 * optional int32 actual_term_months = 6;
 * @return {number}
 */
proto.sensen.finance.DetailedMortgageSummary.prototype.getActualTermMonths = function() {
  return /** @type {number} */ (jspb.Message.getFieldWithDefault(this, 6, 0));
};


/**
 * @param {number} value
 * @return {!proto.sensen.finance.DetailedMortgageSummary} returns this
 */
proto.sensen.finance.DetailedMortgageSummary.prototype.setActualTermMonths = function(value) {
  return jspb.Message.setProto3IntField(this, 6, value);
};



/**
 * List of repeated fields within this message type.
 * @private {!Array<number>}
 * @const
 */
proto.sensen.finance.DetailedAmortizationResponse.repeatedFields_ = [1];



if (jspb.Message.GENERATE_TO_OBJECT) {
/**
 * Creates an object representation of this proto.
 * Field names that are reserved in JavaScript and will be renamed to pb_name.
 * Optional fields that are not set will be set to undefined.
 * To access a reserved field use, foo.pb_<name>, eg, foo.pb_default.
 * For the list of reserved names please see:
 *     net/proto2/compiler/js/internal/generator.cc#kKeyword.
 * @param {boolean=} opt_includeInstance Deprecated. whether to include the
 *     JSPB instance for transitional soy proto support:
 *     http://goto/soy-param-migration
 * @return {!Object}
 */
proto.sensen.finance.DetailedAmortizationResponse.prototype.toObject = function(opt_includeInstance) {
  return proto.sensen.finance.DetailedAmortizationResponse.toObject(opt_includeInstance, this);
};


/**
 * Static version of the {@see toObject} method.
 * @param {boolean|undefined} includeInstance Deprecated. Whether to include
 *     the JSPB instance for transitional soy proto support:
 *     http://goto/soy-param-migration
 * @param {!proto.sensen.finance.DetailedAmortizationResponse} msg The msg instance to transform.
 * @return {!Object}
 * @suppress {unusedLocalVariables} f is only used for nested messages
 */
proto.sensen.finance.DetailedAmortizationResponse.toObject = function(includeInstance, msg) {
  var f, obj = {
    scheduleList: jspb.Message.toObjectList(msg.getScheduleList(),
    proto.sensen.finance.DetailedAmortizationRow.toObject, includeInstance),
    summary: (f = msg.getSummary()) && proto.sensen.finance.DetailedMortgageSummary.toObject(includeInstance, f)
  };

  if (includeInstance) {
    obj.$jspbMessageInstance = msg;
  }
  return obj;
};
}


/**
 * Deserializes binary data (in protobuf wire format).
 * @param {jspb.ByteSource} bytes The bytes to deserialize.
 * @return {!proto.sensen.finance.DetailedAmortizationResponse}
 */
proto.sensen.finance.DetailedAmortizationResponse.deserializeBinary = function(bytes) {
  var reader = new jspb.BinaryReader(bytes);
  var msg = new proto.sensen.finance.DetailedAmortizationResponse;
  return proto.sensen.finance.DetailedAmortizationResponse.deserializeBinaryFromReader(msg, reader);
};


/**
 * Deserializes binary data (in protobuf wire format) from the
 * given reader into the given message object.
 * @param {!proto.sensen.finance.DetailedAmortizationResponse} msg The message object to deserialize into.
 * @param {!jspb.BinaryReader} reader The BinaryReader to use.
 * @return {!proto.sensen.finance.DetailedAmortizationResponse}
 */
proto.sensen.finance.DetailedAmortizationResponse.deserializeBinaryFromReader = function(msg, reader) {
  while (reader.nextField()) {
    if (reader.isEndGroup()) {
      break;
    }
    var field = reader.getFieldNumber();
    switch (field) {
    case 1:
      var value = new proto.sensen.finance.DetailedAmortizationRow;
      reader.readMessage(value,proto.sensen.finance.DetailedAmortizationRow.deserializeBinaryFromReader);
      msg.addSchedule(value);
      break;
    case 2:
      var value = new proto.sensen.finance.DetailedMortgageSummary;
      reader.readMessage(value,proto.sensen.finance.DetailedMortgageSummary.deserializeBinaryFromReader);
      msg.setSummary(value);
      break;
    default:
      reader.skipField();
      break;
    }
  }
  return msg;
};


/**
 * Serializes the message to binary data (in protobuf wire format).
 * @return {!Uint8Array}
 */
proto.sensen.finance.DetailedAmortizationResponse.prototype.serializeBinary = function() {
  var writer = new jspb.BinaryWriter();
  proto.sensen.finance.DetailedAmortizationResponse.serializeBinaryToWriter(this, writer);
  return writer.getResultBuffer();
};


/**
 * Serializes the given message to binary data (in protobuf wire
 * format), writing to the given BinaryWriter.
 * @param {!proto.sensen.finance.DetailedAmortizationResponse} message
 * @param {!jspb.BinaryWriter} writer
 * @suppress {unusedLocalVariables} f is only used for nested messages
 */
proto.sensen.finance.DetailedAmortizationResponse.serializeBinaryToWriter = function(message, writer) {
  var f = undefined;
  f = message.getScheduleList();
  if (f.length > 0) {
    writer.writeRepeatedMessage(
      1,
      f,
      proto.sensen.finance.DetailedAmortizationRow.serializeBinaryToWriter
    );
  }
  f = message.getSummary();
  if (f != null) {
    writer.writeMessage(
      2,
      f,
      proto.sensen.finance.DetailedMortgageSummary.serializeBinaryToWriter
    );
  }
};


/**
 * repeated DetailedAmortizationRow schedule = 1;
 * @return {!Array<!proto.sensen.finance.DetailedAmortizationRow>}
 */
proto.sensen.finance.DetailedAmortizationResponse.prototype.getScheduleList = function() {
  return /** @type{!Array<!proto.sensen.finance.DetailedAmortizationRow>} */ (
    jspb.Message.getRepeatedWrapperField(this, proto.sensen.finance.DetailedAmortizationRow, 1));
};


/**
 * @param {!Array<!proto.sensen.finance.DetailedAmortizationRow>} value
 * @return {!proto.sensen.finance.DetailedAmortizationResponse} returns this
*/
proto.sensen.finance.DetailedAmortizationResponse.prototype.setScheduleList = function(value) {
  return jspb.Message.setRepeatedWrapperField(this, 1, value);
};


/**
 * @param {!proto.sensen.finance.DetailedAmortizationRow=} opt_value
 * @param {number=} opt_index
 * @return {!proto.sensen.finance.DetailedAmortizationRow}
 */
proto.sensen.finance.DetailedAmortizationResponse.prototype.addSchedule = function(opt_value, opt_index) {
  return jspb.Message.addToRepeatedWrapperField(this, 1, opt_value, proto.sensen.finance.DetailedAmortizationRow, opt_index);
};


/**
 * Clears the list making it empty but non-null.
 * @return {!proto.sensen.finance.DetailedAmortizationResponse} returns this
 */
proto.sensen.finance.DetailedAmortizationResponse.prototype.clearScheduleList = function() {
  return this.setScheduleList([]);
};


/**
 * optional DetailedMortgageSummary summary = 2;
 * @return {?proto.sensen.finance.DetailedMortgageSummary}
 */
proto.sensen.finance.DetailedAmortizationResponse.prototype.getSummary = function() {
  return /** @type{?proto.sensen.finance.DetailedMortgageSummary} */ (
    jspb.Message.getWrapperField(this, proto.sensen.finance.DetailedMortgageSummary, 2));
};


/**
 * @param {?proto.sensen.finance.DetailedMortgageSummary|undefined} value
 * @return {!proto.sensen.finance.DetailedAmortizationResponse} returns this
*/
proto.sensen.finance.DetailedAmortizationResponse.prototype.setSummary = function(value) {
  return jspb.Message.setWrapperField(this, 2, value);
};


/**
 * Clears the message field making it undefined.
 * @return {!proto.sensen.finance.DetailedAmortizationResponse} returns this
 */
proto.sensen.finance.DetailedAmortizationResponse.prototype.clearSummary = function() {
  return this.setSummary(undefined);
};


/**
 * Returns whether this field is set.
 * @return {boolean}
 */
proto.sensen.finance.DetailedAmortizationResponse.prototype.hasSummary = function() {
  return jspb.Message.getField(this, 2) != null;
};



/**
 * List of repeated fields within this message type.
 * @private {!Array<number>}
 * @const
 */
proto.sensen.finance.AmortizationBatchRequest.repeatedFields_ = [1,2,3,4,5,6];



if (jspb.Message.GENERATE_TO_OBJECT) {
/**
 * Creates an object representation of this proto.
 * Field names that are reserved in JavaScript and will be renamed to pb_name.
 * Optional fields that are not set will be set to undefined.
 * To access a reserved field use, foo.pb_<name>, eg, foo.pb_default.
 * For the list of reserved names please see:
 *     net/proto2/compiler/js/internal/generator.cc#kKeyword.
 * @param {boolean=} opt_includeInstance Deprecated. whether to include the
 *     JSPB instance for transitional soy proto support:
 *     http://goto/soy-param-migration
 * @return {!Object}
 */
proto.sensen.finance.AmortizationBatchRequest.prototype.toObject = function(opt_includeInstance) {
  return proto.sensen.finance.AmortizationBatchRequest.toObject(opt_includeInstance, this);
};


/**
 * Static version of the {@see toObject} method.
 * @param {boolean|undefined} includeInstance Deprecated. Whether to include
 *     the JSPB instance for transitional soy proto support:
 *     http://goto/soy-param-migration
 * @param {!proto.sensen.finance.AmortizationBatchRequest} msg The msg instance to transform.
 * @return {!Object}
 * @suppress {unusedLocalVariables} f is only used for nested messages
 */
proto.sensen.finance.AmortizationBatchRequest.toObject = function(includeInstance, msg) {
  var f, obj = {
    loanAmountsList: (f = jspb.Message.getRepeatedFloatingPointField(msg, 1)) == null ? undefined : f,
    annualRatesList: (f = jspb.Message.getRepeatedFloatingPointField(msg, 2)) == null ? undefined : f,
    termMonthsList: (f = jspb.Message.getRepeatedField(msg, 3)) == null ? undefined : f,
    extraPaymentsList: (f = jspb.Message.getRepeatedFloatingPointField(msg, 4)) == null ? undefined : f,
    pmiRatesList: (f = jspb.Message.getRepeatedFloatingPointField(msg, 5)) == null ? undefined : f,
    homeValuesList: (f = jspb.Message.getRepeatedFloatingPointField(msg, 6)) == null ? undefined : f
  };

  if (includeInstance) {
    obj.$jspbMessageInstance = msg;
  }
  return obj;
};
}


/**
 * Deserializes binary data (in protobuf wire format).
 * @param {jspb.ByteSource} bytes The bytes to deserialize.
 * @return {!proto.sensen.finance.AmortizationBatchRequest}
 */
proto.sensen.finance.AmortizationBatchRequest.deserializeBinary = function(bytes) {
  var reader = new jspb.BinaryReader(bytes);
  var msg = new proto.sensen.finance.AmortizationBatchRequest;
  return proto.sensen.finance.AmortizationBatchRequest.deserializeBinaryFromReader(msg, reader);
};


/**
 * Deserializes binary data (in protobuf wire format) from the
 * given reader into the given message object.
 * @param {!proto.sensen.finance.AmortizationBatchRequest} msg The message object to deserialize into.
 * @param {!jspb.BinaryReader} reader The BinaryReader to use.
 * @return {!proto.sensen.finance.AmortizationBatchRequest}
 */
proto.sensen.finance.AmortizationBatchRequest.deserializeBinaryFromReader = function(msg, reader) {
  while (reader.nextField()) {
    if (reader.isEndGroup()) {
      break;
    }
    var field = reader.getFieldNumber();
    switch (field) {
    case 1:
      var values = /** @type {!Array<number>} */ (reader.isDelimited() ? reader.readPackedDouble() : [reader.readDouble()]);
      for (var i = 0; i < values.length; i++) {
        msg.addLoanAmounts(values[i]);
      }
      break;
    case 2:
      var values = /** @type {!Array<number>} */ (reader.isDelimited() ? reader.readPackedDouble() : [reader.readDouble()]);
      for (var i = 0; i < values.length; i++) {
        msg.addAnnualRates(values[i]);
      }
      break;
    case 3:
      var values = /** @type {!Array<number>} */ (reader.isDelimited() ? reader.readPackedInt32() : [reader.readInt32()]);
      for (var i = 0; i < values.length; i++) {
        msg.addTermMonths(values[i]);
      }
      break;
    case 4:
      var values = /** @type {!Array<number>} */ (reader.isDelimited() ? reader.readPackedDouble() : [reader.readDouble()]);
      for (var i = 0; i < values.length; i++) {
        msg.addExtraPayments(values[i]);
      }
      break;
    case 5:
      var values = /** @type {!Array<number>} */ (reader.isDelimited() ? reader.readPackedDouble() : [reader.readDouble()]);
      for (var i = 0; i < values.length; i++) {
        msg.addPmiRates(values[i]);
      }
      break;
    case 6:
      var values = /** @type {!Array<number>} */ (reader.isDelimited() ? reader.readPackedDouble() : [reader.readDouble()]);
      for (var i = 0; i < values.length; i++) {
        msg.addHomeValues(values[i]);
      }
      break;
    default:
      reader.skipField();
      break;
    }
  }
  return msg;
};


/**
 * Serializes the message to binary data (in protobuf wire format).
 * @return {!Uint8Array}
 */
proto.sensen.finance.AmortizationBatchRequest.prototype.serializeBinary = function() {
  var writer = new jspb.BinaryWriter();
  proto.sensen.finance.AmortizationBatchRequest.serializeBinaryToWriter(this, writer);
  return writer.getResultBuffer();
};


/**
 * Serializes the given message to binary data (in protobuf wire
 * format), writing to the given BinaryWriter.
 * @param {!proto.sensen.finance.AmortizationBatchRequest} message
 * @param {!jspb.BinaryWriter} writer
 * @suppress {unusedLocalVariables} f is only used for nested messages
 */
proto.sensen.finance.AmortizationBatchRequest.serializeBinaryToWriter = function(message, writer) {
  var f = undefined;
  f = message.getLoanAmountsList();
  if (f.length > 0) {
    writer.writePackedDouble(
      1,
      f
    );
  }
  f = message.getAnnualRatesList();
  if (f.length > 0) {
    writer.writePackedDouble(
      2,
      f
    );
  }
  f = message.getTermMonthsList();
  if (f.length > 0) {
    writer.writePackedInt32(
      3,
      f
    );
  }
  f = message.getExtraPaymentsList();
  if (f.length > 0) {
    writer.writePackedDouble(
      4,
      f
    );
  }
  f = message.getPmiRatesList();
  if (f.length > 0) {
    writer.writePackedDouble(
      5,
      f
    );
  }
  f = message.getHomeValuesList();
  if (f.length > 0) {
    writer.writePackedDouble(
      6,
      f
    );
  }
};


/**
 * repeated double loan_amounts = 1;
 * @return {!Array<number>}
 */
proto.sensen.finance.AmortizationBatchRequest.prototype.getLoanAmountsList = function() {
  return /** @type {!Array<number>} */ (jspb.Message.getRepeatedFloatingPointField(this, 1));
};


/**
 * @param {!Array<number>} value
 * @return {!proto.sensen.finance.AmortizationBatchRequest} returns this
 */
proto.sensen.finance.AmortizationBatchRequest.prototype.setLoanAmountsList = function(value) {
  return jspb.Message.setField(this, 1, value || []);
};


/**
 * @param {number} value
 * @param {number=} opt_index
 * @return {!proto.sensen.finance.AmortizationBatchRequest} returns this
 */
proto.sensen.finance.AmortizationBatchRequest.prototype.addLoanAmounts = function(value, opt_index) {
  return jspb.Message.addToRepeatedField(this, 1, value, opt_index);
};


/**
 * Clears the list making it empty but non-null.
 * @return {!proto.sensen.finance.AmortizationBatchRequest} returns this
 */
proto.sensen.finance.AmortizationBatchRequest.prototype.clearLoanAmountsList = function() {
  return this.setLoanAmountsList([]);
};


/**
 * repeated double annual_rates = 2;
 * @return {!Array<number>}
 */
proto.sensen.finance.AmortizationBatchRequest.prototype.getAnnualRatesList = function() {
  return /** @type {!Array<number>} */ (jspb.Message.getRepeatedFloatingPointField(this, 2));
};


/**
 * @param {!Array<number>} value
 * @return {!proto.sensen.finance.AmortizationBatchRequest} returns this
 */
proto.sensen.finance.AmortizationBatchRequest.prototype.setAnnualRatesList = function(value) {
  return jspb.Message.setField(this, 2, value || []);
};


/**
 * @param {number} value
 * @param {number=} opt_index
 * @return {!proto.sensen.finance.AmortizationBatchRequest} returns this
 */
proto.sensen.finance.AmortizationBatchRequest.prototype.addAnnualRates = function(value, opt_index) {
  return jspb.Message.addToRepeatedField(this, 2, value, opt_index);
};


/**
 * Clears the list making it empty but non-null.
 * @return {!proto.sensen.finance.AmortizationBatchRequest} returns this
 */
proto.sensen.finance.AmortizationBatchRequest.prototype.clearAnnualRatesList = function() {
  return this.setAnnualRatesList([]);
};


/**
 * repeated int32 term_months = 3;
 * @return {!Array<number>}
 */
proto.sensen.finance.AmortizationBatchRequest.prototype.getTermMonthsList = function() {
  return /** @type {!Array<number>} */ (jspb.Message.getRepeatedField(this, 3));
};


/**
 * @param {!Array<number>} value
 * @return {!proto.sensen.finance.AmortizationBatchRequest} returns this
 */
proto.sensen.finance.AmortizationBatchRequest.prototype.setTermMonthsList = function(value) {
  return jspb.Message.setField(this, 3, value || []);
};


/**
 * @param {number} value
 * @param {number=} opt_index
 * @return {!proto.sensen.finance.AmortizationBatchRequest} returns this
 */
proto.sensen.finance.AmortizationBatchRequest.prototype.addTermMonths = function(value, opt_index) {
  return jspb.Message.addToRepeatedField(this, 3, value, opt_index);
};


/**
 * Clears the list making it empty but non-null.
 * @return {!proto.sensen.finance.AmortizationBatchRequest} returns this
 */
proto.sensen.finance.AmortizationBatchRequest.prototype.clearTermMonthsList = function() {
  return this.setTermMonthsList([]);
};


/**
 * repeated double extra_payments = 4;
 * @return {!Array<number>}
 */
proto.sensen.finance.AmortizationBatchRequest.prototype.getExtraPaymentsList = function() {
  return /** @type {!Array<number>} */ (jspb.Message.getRepeatedFloatingPointField(this, 4));
};


/**
 * @param {!Array<number>} value
 * @return {!proto.sensen.finance.AmortizationBatchRequest} returns this
 */
proto.sensen.finance.AmortizationBatchRequest.prototype.setExtraPaymentsList = function(value) {
  return jspb.Message.setField(this, 4, value || []);
};


/**
 * @param {number} value
 * @param {number=} opt_index
 * @return {!proto.sensen.finance.AmortizationBatchRequest} returns this
 */
proto.sensen.finance.AmortizationBatchRequest.prototype.addExtraPayments = function(value, opt_index) {
  return jspb.Message.addToRepeatedField(this, 4, value, opt_index);
};


/**
 * Clears the list making it empty but non-null.
 * @return {!proto.sensen.finance.AmortizationBatchRequest} returns this
 */
proto.sensen.finance.AmortizationBatchRequest.prototype.clearExtraPaymentsList = function() {
  return this.setExtraPaymentsList([]);
};


/**
 * repeated double pmi_rates = 5;
 * @return {!Array<number>}
 */
proto.sensen.finance.AmortizationBatchRequest.prototype.getPmiRatesList = function() {
  return /** @type {!Array<number>} */ (jspb.Message.getRepeatedFloatingPointField(this, 5));
};


/**
 * @param {!Array<number>} value
 * @return {!proto.sensen.finance.AmortizationBatchRequest} returns this
 */
proto.sensen.finance.AmortizationBatchRequest.prototype.setPmiRatesList = function(value) {
  return jspb.Message.setField(this, 5, value || []);
};


/**
 * @param {number} value
 * @param {number=} opt_index
 * @return {!proto.sensen.finance.AmortizationBatchRequest} returns this
 */
proto.sensen.finance.AmortizationBatchRequest.prototype.addPmiRates = function(value, opt_index) {
  return jspb.Message.addToRepeatedField(this, 5, value, opt_index);
};


/**
 * Clears the list making it empty but non-null.
 * @return {!proto.sensen.finance.AmortizationBatchRequest} returns this
 */
proto.sensen.finance.AmortizationBatchRequest.prototype.clearPmiRatesList = function() {
  return this.setPmiRatesList([]);
};


/**
 * repeated double home_values = 6;
 * @return {!Array<number>}
 */
proto.sensen.finance.AmortizationBatchRequest.prototype.getHomeValuesList = function() {
  return /** @type {!Array<number>} */ (jspb.Message.getRepeatedFloatingPointField(this, 6));
};


/**
 * @param {!Array<number>} value
 * @return {!proto.sensen.finance.AmortizationBatchRequest} returns this
 */
proto.sensen.finance.AmortizationBatchRequest.prototype.setHomeValuesList = function(value) {
  return jspb.Message.setField(this, 6, value || []);
};


/**
 * @param {number} value
 * @param {number=} opt_index
 * @return {!proto.sensen.finance.AmortizationBatchRequest} returns this
 */
proto.sensen.finance.AmortizationBatchRequest.prototype.addHomeValues = function(value, opt_index) {
  return jspb.Message.addToRepeatedField(this, 6, value, opt_index);
};


/**
 * Clears the list making it empty but non-null.
 * @return {!proto.sensen.finance.AmortizationBatchRequest} returns this
 */
proto.sensen.finance.AmortizationBatchRequest.prototype.clearHomeValuesList = function() {
  return this.setHomeValuesList([]);
};



/**
 * List of repeated fields within this message type.
 * @private {!Array<number>}
 * @const
 */
proto.sensen.finance.AmortizationBatchResponse.repeatedFields_ = [1];



if (jspb.Message.GENERATE_TO_OBJECT) {
/**
 * Creates an object representation of this proto.
 * Field names that are reserved in JavaScript and will be renamed to pb_name.
 * Optional fields that are not set will be set to undefined.
 * To access a reserved field use, foo.pb_<name>, eg, foo.pb_default.
 * For the list of reserved names please see:
 *     net/proto2/compiler/js/internal/generator.cc#kKeyword.
 * @param {boolean=} opt_includeInstance Deprecated. whether to include the
 *     JSPB instance for transitional soy proto support:
 *     http://goto/soy-param-migration
 * @return {!Object}
 */
proto.sensen.finance.AmortizationBatchResponse.prototype.toObject = function(opt_includeInstance) {
  return proto.sensen.finance.AmortizationBatchResponse.toObject(opt_includeInstance, this);
};


/**
 * Static version of the {@see toObject} method.
 * @param {boolean|undefined} includeInstance Deprecated. Whether to include
 *     the JSPB instance for transitional soy proto support:
 *     http://goto/soy-param-migration
 * @param {!proto.sensen.finance.AmortizationBatchResponse} msg The msg instance to transform.
 * @return {!Object}
 * @suppress {unusedLocalVariables} f is only used for nested messages
 */
proto.sensen.finance.AmortizationBatchResponse.toObject = function(includeInstance, msg) {
  var f, obj = {
    summariesList: jspb.Message.toObjectList(msg.getSummariesList(),
    proto.sensen.finance.MortgageSummary.toObject, includeInstance)
  };

  if (includeInstance) {
    obj.$jspbMessageInstance = msg;
  }
  return obj;
};
}


/**
 * Deserializes binary data (in protobuf wire format).
 * @param {jspb.ByteSource} bytes The bytes to deserialize.
 * @return {!proto.sensen.finance.AmortizationBatchResponse}
 */
proto.sensen.finance.AmortizationBatchResponse.deserializeBinary = function(bytes) {
  var reader = new jspb.BinaryReader(bytes);
  var msg = new proto.sensen.finance.AmortizationBatchResponse;
  return proto.sensen.finance.AmortizationBatchResponse.deserializeBinaryFromReader(msg, reader);
};


/**
 * Deserializes binary data (in protobuf wire format) from the
 * given reader into the given message object.
 * @param {!proto.sensen.finance.AmortizationBatchResponse} msg The message object to deserialize into.
 * @param {!jspb.BinaryReader} reader The BinaryReader to use.
 * @return {!proto.sensen.finance.AmortizationBatchResponse}
 */
proto.sensen.finance.AmortizationBatchResponse.deserializeBinaryFromReader = function(msg, reader) {
  while (reader.nextField()) {
    if (reader.isEndGroup()) {
      break;
    }
    var field = reader.getFieldNumber();
    switch (field) {
    case 1:
      var value = new proto.sensen.finance.MortgageSummary;
      reader.readMessage(value,proto.sensen.finance.MortgageSummary.deserializeBinaryFromReader);
      msg.addSummaries(value);
      break;
    default:
      reader.skipField();
      break;
    }
  }
  return msg;
};


/**
 * Serializes the message to binary data (in protobuf wire format).
 * @return {!Uint8Array}
 */
proto.sensen.finance.AmortizationBatchResponse.prototype.serializeBinary = function() {
  var writer = new jspb.BinaryWriter();
  proto.sensen.finance.AmortizationBatchResponse.serializeBinaryToWriter(this, writer);
  return writer.getResultBuffer();
};


/**
 * Serializes the given message to binary data (in protobuf wire
 * format), writing to the given BinaryWriter.
 * @param {!proto.sensen.finance.AmortizationBatchResponse} message
 * @param {!jspb.BinaryWriter} writer
 * @suppress {unusedLocalVariables} f is only used for nested messages
 */
proto.sensen.finance.AmortizationBatchResponse.serializeBinaryToWriter = function(message, writer) {
  var f = undefined;
  f = message.getSummariesList();
  if (f.length > 0) {
    writer.writeRepeatedMessage(
      1,
      f,
      proto.sensen.finance.MortgageSummary.serializeBinaryToWriter
    );
  }
};


/**
 * repeated MortgageSummary summaries = 1;
 * @return {!Array<!proto.sensen.finance.MortgageSummary>}
 */
proto.sensen.finance.AmortizationBatchResponse.prototype.getSummariesList = function() {
  return /** @type{!Array<!proto.sensen.finance.MortgageSummary>} */ (
    jspb.Message.getRepeatedWrapperField(this, proto.sensen.finance.MortgageSummary, 1));
};


/**
 * @param {!Array<!proto.sensen.finance.MortgageSummary>} value
 * @return {!proto.sensen.finance.AmortizationBatchResponse} returns this
*/
proto.sensen.finance.AmortizationBatchResponse.prototype.setSummariesList = function(value) {
  return jspb.Message.setRepeatedWrapperField(this, 1, value);
};


/**
 * @param {!proto.sensen.finance.MortgageSummary=} opt_value
 * @param {number=} opt_index
 * @return {!proto.sensen.finance.MortgageSummary}
 */
proto.sensen.finance.AmortizationBatchResponse.prototype.addSummaries = function(opt_value, opt_index) {
  return jspb.Message.addToRepeatedWrapperField(this, 1, opt_value, proto.sensen.finance.MortgageSummary, opt_index);
};


/**
 * Clears the list making it empty but non-null.
 * @return {!proto.sensen.finance.AmortizationBatchResponse} returns this
 */
proto.sensen.finance.AmortizationBatchResponse.prototype.clearSummariesList = function() {
  return this.setSummariesList([]);
};





if (jspb.Message.GENERATE_TO_OBJECT) {
/**
 * Creates an object representation of this proto.
 * Field names that are reserved in JavaScript and will be renamed to pb_name.
 * Optional fields that are not set will be set to undefined.
 * To access a reserved field use, foo.pb_<name>, eg, foo.pb_default.
 * For the list of reserved names please see:
 *     net/proto2/compiler/js/internal/generator.cc#kKeyword.
 * @param {boolean=} opt_includeInstance Deprecated. whether to include the
 *     JSPB instance for transitional soy proto support:
 *     http://goto/soy-param-migration
 * @return {!Object}
 */
proto.sensen.finance.HelocRequest.prototype.toObject = function(opt_includeInstance) {
  return proto.sensen.finance.HelocRequest.toObject(opt_includeInstance, this);
};


/**
 * Static version of the {@see toObject} method.
 * @param {boolean|undefined} includeInstance Deprecated. Whether to include
 *     the JSPB instance for transitional soy proto support:
 *     http://goto/soy-param-migration
 * @param {!proto.sensen.finance.HelocRequest} msg The msg instance to transform.
 * @return {!Object}
 * @suppress {unusedLocalVariables} f is only used for nested messages
 */
proto.sensen.finance.HelocRequest.toObject = function(includeInstance, msg) {
  var f, obj = {
    homeValue: jspb.Message.getFieldWithDefault(msg, 1, ""),
    currentMortgageBalance: jspb.Message.getFieldWithDefault(msg, 2, ""),
    maxLtvRate: jspb.Message.getFieldWithDefault(msg, 3, ""),
    drawnAmount: jspb.Message.getFieldWithDefault(msg, 4, ""),
    annualRate: jspb.Message.getFieldWithDefault(msg, 5, ""),
    repaymentTermYears: jspb.Message.getFieldWithDefault(msg, 6, 0),
    paymentsPerYear: jspb.Message.getFieldWithDefault(msg, 7, 0)
  };

  if (includeInstance) {
    obj.$jspbMessageInstance = msg;
  }
  return obj;
};
}


/**
 * Deserializes binary data (in protobuf wire format).
 * @param {jspb.ByteSource} bytes The bytes to deserialize.
 * @return {!proto.sensen.finance.HelocRequest}
 */
proto.sensen.finance.HelocRequest.deserializeBinary = function(bytes) {
  var reader = new jspb.BinaryReader(bytes);
  var msg = new proto.sensen.finance.HelocRequest;
  return proto.sensen.finance.HelocRequest.deserializeBinaryFromReader(msg, reader);
};


/**
 * Deserializes binary data (in protobuf wire format) from the
 * given reader into the given message object.
 * @param {!proto.sensen.finance.HelocRequest} msg The message object to deserialize into.
 * @param {!jspb.BinaryReader} reader The BinaryReader to use.
 * @return {!proto.sensen.finance.HelocRequest}
 */
proto.sensen.finance.HelocRequest.deserializeBinaryFromReader = function(msg, reader) {
  while (reader.nextField()) {
    if (reader.isEndGroup()) {
      break;
    }
    var field = reader.getFieldNumber();
    switch (field) {
    case 1:
      var value = /** @type {string} */ (reader.readString());
      msg.setHomeValue(value);
      break;
    case 2:
      var value = /** @type {string} */ (reader.readString());
      msg.setCurrentMortgageBalance(value);
      break;
    case 3:
      var value = /** @type {string} */ (reader.readString());
      msg.setMaxLtvRate(value);
      break;
    case 4:
      var value = /** @type {string} */ (reader.readString());
      msg.setDrawnAmount(value);
      break;
    case 5:
      var value = /** @type {string} */ (reader.readString());
      msg.setAnnualRate(value);
      break;
    case 6:
      var value = /** @type {number} */ (reader.readInt32());
      msg.setRepaymentTermYears(value);
      break;
    case 7:
      var value = /** @type {number} */ (reader.readInt32());
      msg.setPaymentsPerYear(value);
      break;
    default:
      reader.skipField();
      break;
    }
  }
  return msg;
};


/**
 * Serializes the message to binary data (in protobuf wire format).
 * @return {!Uint8Array}
 */
proto.sensen.finance.HelocRequest.prototype.serializeBinary = function() {
  var writer = new jspb.BinaryWriter();
  proto.sensen.finance.HelocRequest.serializeBinaryToWriter(this, writer);
  return writer.getResultBuffer();
};


/**
 * Serializes the given message to binary data (in protobuf wire
 * format), writing to the given BinaryWriter.
 * @param {!proto.sensen.finance.HelocRequest} message
 * @param {!jspb.BinaryWriter} writer
 * @suppress {unusedLocalVariables} f is only used for nested messages
 */
proto.sensen.finance.HelocRequest.serializeBinaryToWriter = function(message, writer) {
  var f = undefined;
  f = message.getHomeValue();
  if (f.length > 0) {
    writer.writeString(
      1,
      f
    );
  }
  f = message.getCurrentMortgageBalance();
  if (f.length > 0) {
    writer.writeString(
      2,
      f
    );
  }
  f = message.getMaxLtvRate();
  if (f.length > 0) {
    writer.writeString(
      3,
      f
    );
  }
  f = message.getDrawnAmount();
  if (f.length > 0) {
    writer.writeString(
      4,
      f
    );
  }
  f = message.getAnnualRate();
  if (f.length > 0) {
    writer.writeString(
      5,
      f
    );
  }
  f = message.getRepaymentTermYears();
  if (f !== 0) {
    writer.writeInt32(
      6,
      f
    );
  }
  f = message.getPaymentsPerYear();
  if (f !== 0) {
    writer.writeInt32(
      7,
      f
    );
  }
};


/**
 * optional string home_value = 1;
 * @return {string}
 */
proto.sensen.finance.HelocRequest.prototype.getHomeValue = function() {
  return /** @type {string} */ (jspb.Message.getFieldWithDefault(this, 1, ""));
};


/**
 * @param {string} value
 * @return {!proto.sensen.finance.HelocRequest} returns this
 */
proto.sensen.finance.HelocRequest.prototype.setHomeValue = function(value) {
  return jspb.Message.setProto3StringField(this, 1, value);
};


/**
 * optional string current_mortgage_balance = 2;
 * @return {string}
 */
proto.sensen.finance.HelocRequest.prototype.getCurrentMortgageBalance = function() {
  return /** @type {string} */ (jspb.Message.getFieldWithDefault(this, 2, ""));
};


/**
 * @param {string} value
 * @return {!proto.sensen.finance.HelocRequest} returns this
 */
proto.sensen.finance.HelocRequest.prototype.setCurrentMortgageBalance = function(value) {
  return jspb.Message.setProto3StringField(this, 2, value);
};


/**
 * optional string max_ltv_rate = 3;
 * @return {string}
 */
proto.sensen.finance.HelocRequest.prototype.getMaxLtvRate = function() {
  return /** @type {string} */ (jspb.Message.getFieldWithDefault(this, 3, ""));
};


/**
 * @param {string} value
 * @return {!proto.sensen.finance.HelocRequest} returns this
 */
proto.sensen.finance.HelocRequest.prototype.setMaxLtvRate = function(value) {
  return jspb.Message.setProto3StringField(this, 3, value);
};


/**
 * optional string drawn_amount = 4;
 * @return {string}
 */
proto.sensen.finance.HelocRequest.prototype.getDrawnAmount = function() {
  return /** @type {string} */ (jspb.Message.getFieldWithDefault(this, 4, ""));
};


/**
 * @param {string} value
 * @return {!proto.sensen.finance.HelocRequest} returns this
 */
proto.sensen.finance.HelocRequest.prototype.setDrawnAmount = function(value) {
  return jspb.Message.setProto3StringField(this, 4, value);
};


/**
 * optional string annual_rate = 5;
 * @return {string}
 */
proto.sensen.finance.HelocRequest.prototype.getAnnualRate = function() {
  return /** @type {string} */ (jspb.Message.getFieldWithDefault(this, 5, ""));
};


/**
 * @param {string} value
 * @return {!proto.sensen.finance.HelocRequest} returns this
 */
proto.sensen.finance.HelocRequest.prototype.setAnnualRate = function(value) {
  return jspb.Message.setProto3StringField(this, 5, value);
};


/**
 * optional int32 repayment_term_years = 6;
 * @return {number}
 */
proto.sensen.finance.HelocRequest.prototype.getRepaymentTermYears = function() {
  return /** @type {number} */ (jspb.Message.getFieldWithDefault(this, 6, 0));
};


/**
 * @param {number} value
 * @return {!proto.sensen.finance.HelocRequest} returns this
 */
proto.sensen.finance.HelocRequest.prototype.setRepaymentTermYears = function(value) {
  return jspb.Message.setProto3IntField(this, 6, value);
};


/**
 * optional int32 payments_per_year = 7;
 * @return {number}
 */
proto.sensen.finance.HelocRequest.prototype.getPaymentsPerYear = function() {
  return /** @type {number} */ (jspb.Message.getFieldWithDefault(this, 7, 0));
};


/**
 * @param {number} value
 * @return {!proto.sensen.finance.HelocRequest} returns this
 */
proto.sensen.finance.HelocRequest.prototype.setPaymentsPerYear = function(value) {
  return jspb.Message.setProto3IntField(this, 7, value);
};





if (jspb.Message.GENERATE_TO_OBJECT) {
/**
 * Creates an object representation of this proto.
 * Field names that are reserved in JavaScript and will be renamed to pb_name.
 * Optional fields that are not set will be set to undefined.
 * To access a reserved field use, foo.pb_<name>, eg, foo.pb_default.
 * For the list of reserved names please see:
 *     net/proto2/compiler/js/internal/generator.cc#kKeyword.
 * @param {boolean=} opt_includeInstance Deprecated. whether to include the
 *     JSPB instance for transitional soy proto support:
 *     http://goto/soy-param-migration
 * @return {!Object}
 */
proto.sensen.finance.HelocResponse.prototype.toObject = function(opt_includeInstance) {
  return proto.sensen.finance.HelocResponse.toObject(opt_includeInstance, this);
};


/**
 * Static version of the {@see toObject} method.
 * @param {boolean|undefined} includeInstance Deprecated. Whether to include
 *     the JSPB instance for transitional soy proto support:
 *     http://goto/soy-param-migration
 * @param {!proto.sensen.finance.HelocResponse} msg The msg instance to transform.
 * @return {!Object}
 * @suppress {unusedLocalVariables} f is only used for nested messages
 */
proto.sensen.finance.HelocResponse.toObject = function(includeInstance, msg) {
  var f, obj = {
    availableEquity: jspb.Message.getFieldWithDefault(msg, 1, ""),
    drawPeriodPayment: jspb.Message.getFieldWithDefault(msg, 2, ""),
    repaymentPeriodPayment: jspb.Message.getFieldWithDefault(msg, 3, "")
  };

  if (includeInstance) {
    obj.$jspbMessageInstance = msg;
  }
  return obj;
};
}


/**
 * Deserializes binary data (in protobuf wire format).
 * @param {jspb.ByteSource} bytes The bytes to deserialize.
 * @return {!proto.sensen.finance.HelocResponse}
 */
proto.sensen.finance.HelocResponse.deserializeBinary = function(bytes) {
  var reader = new jspb.BinaryReader(bytes);
  var msg = new proto.sensen.finance.HelocResponse;
  return proto.sensen.finance.HelocResponse.deserializeBinaryFromReader(msg, reader);
};


/**
 * Deserializes binary data (in protobuf wire format) from the
 * given reader into the given message object.
 * @param {!proto.sensen.finance.HelocResponse} msg The message object to deserialize into.
 * @param {!jspb.BinaryReader} reader The BinaryReader to use.
 * @return {!proto.sensen.finance.HelocResponse}
 */
proto.sensen.finance.HelocResponse.deserializeBinaryFromReader = function(msg, reader) {
  while (reader.nextField()) {
    if (reader.isEndGroup()) {
      break;
    }
    var field = reader.getFieldNumber();
    switch (field) {
    case 1:
      var value = /** @type {string} */ (reader.readString());
      msg.setAvailableEquity(value);
      break;
    case 2:
      var value = /** @type {string} */ (reader.readString());
      msg.setDrawPeriodPayment(value);
      break;
    case 3:
      var value = /** @type {string} */ (reader.readString());
      msg.setRepaymentPeriodPayment(value);
      break;
    default:
      reader.skipField();
      break;
    }
  }
  return msg;
};


/**
 * Serializes the message to binary data (in protobuf wire format).
 * @return {!Uint8Array}
 */
proto.sensen.finance.HelocResponse.prototype.serializeBinary = function() {
  var writer = new jspb.BinaryWriter();
  proto.sensen.finance.HelocResponse.serializeBinaryToWriter(this, writer);
  return writer.getResultBuffer();
};


/**
 * Serializes the given message to binary data (in protobuf wire
 * format), writing to the given BinaryWriter.
 * @param {!proto.sensen.finance.HelocResponse} message
 * @param {!jspb.BinaryWriter} writer
 * @suppress {unusedLocalVariables} f is only used for nested messages
 */
proto.sensen.finance.HelocResponse.serializeBinaryToWriter = function(message, writer) {
  var f = undefined;
  f = message.getAvailableEquity();
  if (f.length > 0) {
    writer.writeString(
      1,
      f
    );
  }
  f = message.getDrawPeriodPayment();
  if (f.length > 0) {
    writer.writeString(
      2,
      f
    );
  }
  f = message.getRepaymentPeriodPayment();
  if (f.length > 0) {
    writer.writeString(
      3,
      f
    );
  }
};


/**
 * optional string available_equity = 1;
 * @return {string}
 */
proto.sensen.finance.HelocResponse.prototype.getAvailableEquity = function() {
  return /** @type {string} */ (jspb.Message.getFieldWithDefault(this, 1, ""));
};


/**
 * @param {string} value
 * @return {!proto.sensen.finance.HelocResponse} returns this
 */
proto.sensen.finance.HelocResponse.prototype.setAvailableEquity = function(value) {
  return jspb.Message.setProto3StringField(this, 1, value);
};


/**
 * optional string draw_period_payment = 2;
 * @return {string}
 */
proto.sensen.finance.HelocResponse.prototype.getDrawPeriodPayment = function() {
  return /** @type {string} */ (jspb.Message.getFieldWithDefault(this, 2, ""));
};


/**
 * @param {string} value
 * @return {!proto.sensen.finance.HelocResponse} returns this
 */
proto.sensen.finance.HelocResponse.prototype.setDrawPeriodPayment = function(value) {
  return jspb.Message.setProto3StringField(this, 2, value);
};


/**
 * optional string repayment_period_payment = 3;
 * @return {string}
 */
proto.sensen.finance.HelocResponse.prototype.getRepaymentPeriodPayment = function() {
  return /** @type {string} */ (jspb.Message.getFieldWithDefault(this, 3, ""));
};


/**
 * @param {string} value
 * @return {!proto.sensen.finance.HelocResponse} returns this
 */
proto.sensen.finance.HelocResponse.prototype.setRepaymentPeriodPayment = function(value) {
  return jspb.Message.setProto3StringField(this, 3, value);
};





if (jspb.Message.GENERATE_TO_OBJECT) {
/**
 * Creates an object representation of this proto.
 * Field names that are reserved in JavaScript and will be renamed to pb_name.
 * Optional fields that are not set will be set to undefined.
 * To access a reserved field use, foo.pb_<name>, eg, foo.pb_default.
 * For the list of reserved names please see:
 *     net/proto2/compiler/js/internal/generator.cc#kKeyword.
 * @param {boolean=} opt_includeInstance Deprecated. whether to include the
 *     JSPB instance for transitional soy proto support:
 *     http://goto/soy-param-migration
 * @return {!Object}
 */
proto.sensen.finance.RefinanceRequest.prototype.toObject = function(opt_includeInstance) {
  return proto.sensen.finance.RefinanceRequest.toObject(opt_includeInstance, this);
};


/**
 * Static version of the {@see toObject} method.
 * @param {boolean|undefined} includeInstance Deprecated. Whether to include
 *     the JSPB instance for transitional soy proto support:
 *     http://goto/soy-param-migration
 * @param {!proto.sensen.finance.RefinanceRequest} msg The msg instance to transform.
 * @return {!Object}
 * @suppress {unusedLocalVariables} f is only used for nested messages
 */
proto.sensen.finance.RefinanceRequest.toObject = function(includeInstance, msg) {
  var f, obj = {
    currentLoanBalance: jspb.Message.getFieldWithDefault(msg, 1, ""),
    currentMonthlyPayment: jspb.Message.getFieldWithDefault(msg, 2, ""),
    currentAnnualRate: jspb.Message.getFieldWithDefault(msg, 3, ""),
    currentRemainingMonths: jspb.Message.getFieldWithDefault(msg, 4, 0),
    propertyValue: jspb.Message.getFieldWithDefault(msg, 5, ""),
    newAnnualRate: jspb.Message.getFieldWithDefault(msg, 6, ""),
    newTermYears: jspb.Message.getFieldWithDefault(msg, 7, 0),
    closingCosts: jspb.Message.getFieldWithDefault(msg, 8, ""),
    closingCostType: jspb.Message.getFieldWithDefault(msg, 9, 0),
    cashOutAmount: jspb.Message.getFieldWithDefault(msg, 10, ""),
    currentPmiMonthly: jspb.Message.getFieldWithDefault(msg, 11, ""),
    newPmiMonthly: jspb.Message.getFieldWithDefault(msg, 12, ""),
    pmiDropOffLtv: jspb.Message.getFieldWithDefault(msg, 13, ""),
    paymentsPerYear: jspb.Message.getFieldWithDefault(msg, 14, 0)
  };

  if (includeInstance) {
    obj.$jspbMessageInstance = msg;
  }
  return obj;
};
}


/**
 * Deserializes binary data (in protobuf wire format).
 * @param {jspb.ByteSource} bytes The bytes to deserialize.
 * @return {!proto.sensen.finance.RefinanceRequest}
 */
proto.sensen.finance.RefinanceRequest.deserializeBinary = function(bytes) {
  var reader = new jspb.BinaryReader(bytes);
  var msg = new proto.sensen.finance.RefinanceRequest;
  return proto.sensen.finance.RefinanceRequest.deserializeBinaryFromReader(msg, reader);
};


/**
 * Deserializes binary data (in protobuf wire format) from the
 * given reader into the given message object.
 * @param {!proto.sensen.finance.RefinanceRequest} msg The message object to deserialize into.
 * @param {!jspb.BinaryReader} reader The BinaryReader to use.
 * @return {!proto.sensen.finance.RefinanceRequest}
 */
proto.sensen.finance.RefinanceRequest.deserializeBinaryFromReader = function(msg, reader) {
  while (reader.nextField()) {
    if (reader.isEndGroup()) {
      break;
    }
    var field = reader.getFieldNumber();
    switch (field) {
    case 1:
      var value = /** @type {string} */ (reader.readString());
      msg.setCurrentLoanBalance(value);
      break;
    case 2:
      var value = /** @type {string} */ (reader.readString());
      msg.setCurrentMonthlyPayment(value);
      break;
    case 3:
      var value = /** @type {string} */ (reader.readString());
      msg.setCurrentAnnualRate(value);
      break;
    case 4:
      var value = /** @type {number} */ (reader.readInt32());
      msg.setCurrentRemainingMonths(value);
      break;
    case 5:
      var value = /** @type {string} */ (reader.readString());
      msg.setPropertyValue(value);
      break;
    case 6:
      var value = /** @type {string} */ (reader.readString());
      msg.setNewAnnualRate(value);
      break;
    case 7:
      var value = /** @type {number} */ (reader.readInt32());
      msg.setNewTermYears(value);
      break;
    case 8:
      var value = /** @type {string} */ (reader.readString());
      msg.setClosingCosts(value);
      break;
    case 9:
      var value = /** @type {!proto.sensen.finance.RefinanceRequest.ClosingCostType} */ (reader.readEnum());
      msg.setClosingCostType(value);
      break;
    case 10:
      var value = /** @type {string} */ (reader.readString());
      msg.setCashOutAmount(value);
      break;
    case 11:
      var value = /** @type {string} */ (reader.readString());
      msg.setCurrentPmiMonthly(value);
      break;
    case 12:
      var value = /** @type {string} */ (reader.readString());
      msg.setNewPmiMonthly(value);
      break;
    case 13:
      var value = /** @type {string} */ (reader.readString());
      msg.setPmiDropOffLtv(value);
      break;
    case 14:
      var value = /** @type {number} */ (reader.readInt32());
      msg.setPaymentsPerYear(value);
      break;
    default:
      reader.skipField();
      break;
    }
  }
  return msg;
};


/**
 * Serializes the message to binary data (in protobuf wire format).
 * @return {!Uint8Array}
 */
proto.sensen.finance.RefinanceRequest.prototype.serializeBinary = function() {
  var writer = new jspb.BinaryWriter();
  proto.sensen.finance.RefinanceRequest.serializeBinaryToWriter(this, writer);
  return writer.getResultBuffer();
};


/**
 * Serializes the given message to binary data (in protobuf wire
 * format), writing to the given BinaryWriter.
 * @param {!proto.sensen.finance.RefinanceRequest} message
 * @param {!jspb.BinaryWriter} writer
 * @suppress {unusedLocalVariables} f is only used for nested messages
 */
proto.sensen.finance.RefinanceRequest.serializeBinaryToWriter = function(message, writer) {
  var f = undefined;
  f = message.getCurrentLoanBalance();
  if (f.length > 0) {
    writer.writeString(
      1,
      f
    );
  }
  f = message.getCurrentMonthlyPayment();
  if (f.length > 0) {
    writer.writeString(
      2,
      f
    );
  }
  f = message.getCurrentAnnualRate();
  if (f.length > 0) {
    writer.writeString(
      3,
      f
    );
  }
  f = message.getCurrentRemainingMonths();
  if (f !== 0) {
    writer.writeInt32(
      4,
      f
    );
  }
  f = message.getPropertyValue();
  if (f.length > 0) {
    writer.writeString(
      5,
      f
    );
  }
  f = message.getNewAnnualRate();
  if (f.length > 0) {
    writer.writeString(
      6,
      f
    );
  }
  f = message.getNewTermYears();
  if (f !== 0) {
    writer.writeInt32(
      7,
      f
    );
  }
  f = message.getClosingCosts();
  if (f.length > 0) {
    writer.writeString(
      8,
      f
    );
  }
  f = message.getClosingCostType();
  if (f !== 0.0) {
    writer.writeEnum(
      9,
      f
    );
  }
  f = message.getCashOutAmount();
  if (f.length > 0) {
    writer.writeString(
      10,
      f
    );
  }
  f = message.getCurrentPmiMonthly();
  if (f.length > 0) {
    writer.writeString(
      11,
      f
    );
  }
  f = message.getNewPmiMonthly();
  if (f.length > 0) {
    writer.writeString(
      12,
      f
    );
  }
  f = message.getPmiDropOffLtv();
  if (f.length > 0) {
    writer.writeString(
      13,
      f
    );
  }
  f = message.getPaymentsPerYear();
  if (f !== 0) {
    writer.writeInt32(
      14,
      f
    );
  }
};


/**
 * @enum {number}
 */
proto.sensen.finance.RefinanceRequest.ClosingCostType = {
  PAID_IN_CASH: 0,
  ROLLED_INTO_LOAN: 1
};

/**
 * optional string current_loan_balance = 1;
 * @return {string}
 */
proto.sensen.finance.RefinanceRequest.prototype.getCurrentLoanBalance = function() {
  return /** @type {string} */ (jspb.Message.getFieldWithDefault(this, 1, ""));
};


/**
 * @param {string} value
 * @return {!proto.sensen.finance.RefinanceRequest} returns this
 */
proto.sensen.finance.RefinanceRequest.prototype.setCurrentLoanBalance = function(value) {
  return jspb.Message.setProto3StringField(this, 1, value);
};


/**
 * optional string current_monthly_payment = 2;
 * @return {string}
 */
proto.sensen.finance.RefinanceRequest.prototype.getCurrentMonthlyPayment = function() {
  return /** @type {string} */ (jspb.Message.getFieldWithDefault(this, 2, ""));
};


/**
 * @param {string} value
 * @return {!proto.sensen.finance.RefinanceRequest} returns this
 */
proto.sensen.finance.RefinanceRequest.prototype.setCurrentMonthlyPayment = function(value) {
  return jspb.Message.setProto3StringField(this, 2, value);
};


/**
 * optional string current_annual_rate = 3;
 * @return {string}
 */
proto.sensen.finance.RefinanceRequest.prototype.getCurrentAnnualRate = function() {
  return /** @type {string} */ (jspb.Message.getFieldWithDefault(this, 3, ""));
};


/**
 * @param {string} value
 * @return {!proto.sensen.finance.RefinanceRequest} returns this
 */
proto.sensen.finance.RefinanceRequest.prototype.setCurrentAnnualRate = function(value) {
  return jspb.Message.setProto3StringField(this, 3, value);
};


/**
 * optional int32 current_remaining_months = 4;
 * @return {number}
 */
proto.sensen.finance.RefinanceRequest.prototype.getCurrentRemainingMonths = function() {
  return /** @type {number} */ (jspb.Message.getFieldWithDefault(this, 4, 0));
};


/**
 * @param {number} value
 * @return {!proto.sensen.finance.RefinanceRequest} returns this
 */
proto.sensen.finance.RefinanceRequest.prototype.setCurrentRemainingMonths = function(value) {
  return jspb.Message.setProto3IntField(this, 4, value);
};


/**
 * optional string property_value = 5;
 * @return {string}
 */
proto.sensen.finance.RefinanceRequest.prototype.getPropertyValue = function() {
  return /** @type {string} */ (jspb.Message.getFieldWithDefault(this, 5, ""));
};


/**
 * @param {string} value
 * @return {!proto.sensen.finance.RefinanceRequest} returns this
 */
proto.sensen.finance.RefinanceRequest.prototype.setPropertyValue = function(value) {
  return jspb.Message.setProto3StringField(this, 5, value);
};


/**
 * optional string new_annual_rate = 6;
 * @return {string}
 */
proto.sensen.finance.RefinanceRequest.prototype.getNewAnnualRate = function() {
  return /** @type {string} */ (jspb.Message.getFieldWithDefault(this, 6, ""));
};


/**
 * @param {string} value
 * @return {!proto.sensen.finance.RefinanceRequest} returns this
 */
proto.sensen.finance.RefinanceRequest.prototype.setNewAnnualRate = function(value) {
  return jspb.Message.setProto3StringField(this, 6, value);
};


/**
 * optional int32 new_term_years = 7;
 * @return {number}
 */
proto.sensen.finance.RefinanceRequest.prototype.getNewTermYears = function() {
  return /** @type {number} */ (jspb.Message.getFieldWithDefault(this, 7, 0));
};


/**
 * @param {number} value
 * @return {!proto.sensen.finance.RefinanceRequest} returns this
 */
proto.sensen.finance.RefinanceRequest.prototype.setNewTermYears = function(value) {
  return jspb.Message.setProto3IntField(this, 7, value);
};


/**
 * optional string closing_costs = 8;
 * @return {string}
 */
proto.sensen.finance.RefinanceRequest.prototype.getClosingCosts = function() {
  return /** @type {string} */ (jspb.Message.getFieldWithDefault(this, 8, ""));
};


/**
 * @param {string} value
 * @return {!proto.sensen.finance.RefinanceRequest} returns this
 */
proto.sensen.finance.RefinanceRequest.prototype.setClosingCosts = function(value) {
  return jspb.Message.setProto3StringField(this, 8, value);
};


/**
 * optional ClosingCostType closing_cost_type = 9;
 * @return {!proto.sensen.finance.RefinanceRequest.ClosingCostType}
 */
proto.sensen.finance.RefinanceRequest.prototype.getClosingCostType = function() {
  return /** @type {!proto.sensen.finance.RefinanceRequest.ClosingCostType} */ (jspb.Message.getFieldWithDefault(this, 9, 0));
};


/**
 * @param {!proto.sensen.finance.RefinanceRequest.ClosingCostType} value
 * @return {!proto.sensen.finance.RefinanceRequest} returns this
 */
proto.sensen.finance.RefinanceRequest.prototype.setClosingCostType = function(value) {
  return jspb.Message.setProto3EnumField(this, 9, value);
};


/**
 * optional string cash_out_amount = 10;
 * @return {string}
 */
proto.sensen.finance.RefinanceRequest.prototype.getCashOutAmount = function() {
  return /** @type {string} */ (jspb.Message.getFieldWithDefault(this, 10, ""));
};


/**
 * @param {string} value
 * @return {!proto.sensen.finance.RefinanceRequest} returns this
 */
proto.sensen.finance.RefinanceRequest.prototype.setCashOutAmount = function(value) {
  return jspb.Message.setProto3StringField(this, 10, value);
};


/**
 * optional string current_pmi_monthly = 11;
 * @return {string}
 */
proto.sensen.finance.RefinanceRequest.prototype.getCurrentPmiMonthly = function() {
  return /** @type {string} */ (jspb.Message.getFieldWithDefault(this, 11, ""));
};


/**
 * @param {string} value
 * @return {!proto.sensen.finance.RefinanceRequest} returns this
 */
proto.sensen.finance.RefinanceRequest.prototype.setCurrentPmiMonthly = function(value) {
  return jspb.Message.setProto3StringField(this, 11, value);
};


/**
 * optional string new_pmi_monthly = 12;
 * @return {string}
 */
proto.sensen.finance.RefinanceRequest.prototype.getNewPmiMonthly = function() {
  return /** @type {string} */ (jspb.Message.getFieldWithDefault(this, 12, ""));
};


/**
 * @param {string} value
 * @return {!proto.sensen.finance.RefinanceRequest} returns this
 */
proto.sensen.finance.RefinanceRequest.prototype.setNewPmiMonthly = function(value) {
  return jspb.Message.setProto3StringField(this, 12, value);
};


/**
 * optional string pmi_drop_off_ltv = 13;
 * @return {string}
 */
proto.sensen.finance.RefinanceRequest.prototype.getPmiDropOffLtv = function() {
  return /** @type {string} */ (jspb.Message.getFieldWithDefault(this, 13, ""));
};


/**
 * @param {string} value
 * @return {!proto.sensen.finance.RefinanceRequest} returns this
 */
proto.sensen.finance.RefinanceRequest.prototype.setPmiDropOffLtv = function(value) {
  return jspb.Message.setProto3StringField(this, 13, value);
};


/**
 * optional int32 payments_per_year = 14;
 * @return {number}
 */
proto.sensen.finance.RefinanceRequest.prototype.getPaymentsPerYear = function() {
  return /** @type {number} */ (jspb.Message.getFieldWithDefault(this, 14, 0));
};


/**
 * @param {number} value
 * @return {!proto.sensen.finance.RefinanceRequest} returns this
 */
proto.sensen.finance.RefinanceRequest.prototype.setPaymentsPerYear = function(value) {
  return jspb.Message.setProto3IntField(this, 14, value);
};





if (jspb.Message.GENERATE_TO_OBJECT) {
/**
 * Creates an object representation of this proto.
 * Field names that are reserved in JavaScript and will be renamed to pb_name.
 * Optional fields that are not set will be set to undefined.
 * To access a reserved field use, foo.pb_<name>, eg, foo.pb_default.
 * For the list of reserved names please see:
 *     net/proto2/compiler/js/internal/generator.cc#kKeyword.
 * @param {boolean=} opt_includeInstance Deprecated. whether to include the
 *     JSPB instance for transitional soy proto support:
 *     http://goto/soy-param-migration
 * @return {!Object}
 */
proto.sensen.finance.RefinanceResponse.prototype.toObject = function(opt_includeInstance) {
  return proto.sensen.finance.RefinanceResponse.toObject(opt_includeInstance, this);
};


/**
 * Static version of the {@see toObject} method.
 * @param {boolean|undefined} includeInstance Deprecated. Whether to include
 *     the JSPB instance for transitional soy proto support:
 *     http://goto/soy-param-migration
 * @param {!proto.sensen.finance.RefinanceResponse} msg The msg instance to transform.
 * @return {!Object}
 * @suppress {unusedLocalVariables} f is only used for nested messages
 */
proto.sensen.finance.RefinanceResponse.toObject = function(includeInstance, msg) {
  var f, obj = {
    newLoanAmount: jspb.Message.getFieldWithDefault(msg, 1, ""),
    newMonthlyPayment: jspb.Message.getFieldWithDefault(msg, 2, ""),
    monthlySavingsInitial: jspb.Message.getFieldWithDefault(msg, 3, ""),
    currentLoanPmiDropOffMonths: jspb.Message.getFieldWithDefault(msg, 4, 0),
    newLoanPmiDropOffMonths: jspb.Message.getFieldWithDefault(msg, 5, 0),
    payoffDateShiftMonths: jspb.Message.getFieldWithDefault(msg, 6, 0),
    simpleBreakEvenMonths: jspb.Message.getFieldWithDefault(msg, 7, 0),
    cashFlowBreakEvenMonths: jspb.Message.getFieldWithDefault(msg, 8, 0),
    equityAdjustedBreakEvenMonths: jspb.Message.getFieldWithDefault(msg, 9, 0),
    totalSavingsOverLife: jspb.Message.getFloatingPointFieldWithDefault(msg, 10, 0.0)
  };

  if (includeInstance) {
    obj.$jspbMessageInstance = msg;
  }
  return obj;
};
}


/**
 * Deserializes binary data (in protobuf wire format).
 * @param {jspb.ByteSource} bytes The bytes to deserialize.
 * @return {!proto.sensen.finance.RefinanceResponse}
 */
proto.sensen.finance.RefinanceResponse.deserializeBinary = function(bytes) {
  var reader = new jspb.BinaryReader(bytes);
  var msg = new proto.sensen.finance.RefinanceResponse;
  return proto.sensen.finance.RefinanceResponse.deserializeBinaryFromReader(msg, reader);
};


/**
 * Deserializes binary data (in protobuf wire format) from the
 * given reader into the given message object.
 * @param {!proto.sensen.finance.RefinanceResponse} msg The message object to deserialize into.
 * @param {!jspb.BinaryReader} reader The BinaryReader to use.
 * @return {!proto.sensen.finance.RefinanceResponse}
 */
proto.sensen.finance.RefinanceResponse.deserializeBinaryFromReader = function(msg, reader) {
  while (reader.nextField()) {
    if (reader.isEndGroup()) {
      break;
    }
    var field = reader.getFieldNumber();
    switch (field) {
    case 1:
      var value = /** @type {string} */ (reader.readString());
      msg.setNewLoanAmount(value);
      break;
    case 2:
      var value = /** @type {string} */ (reader.readString());
      msg.setNewMonthlyPayment(value);
      break;
    case 3:
      var value = /** @type {string} */ (reader.readString());
      msg.setMonthlySavingsInitial(value);
      break;
    case 4:
      var value = /** @type {number} */ (reader.readInt32());
      msg.setCurrentLoanPmiDropOffMonths(value);
      break;
    case 5:
      var value = /** @type {number} */ (reader.readInt32());
      msg.setNewLoanPmiDropOffMonths(value);
      break;
    case 6:
      var value = /** @type {number} */ (reader.readInt32());
      msg.setPayoffDateShiftMonths(value);
      break;
    case 7:
      var value = /** @type {number} */ (reader.readInt32());
      msg.setSimpleBreakEvenMonths(value);
      break;
    case 8:
      var value = /** @type {number} */ (reader.readInt32());
      msg.setCashFlowBreakEvenMonths(value);
      break;
    case 9:
      var value = /** @type {number} */ (reader.readInt32());
      msg.setEquityAdjustedBreakEvenMonths(value);
      break;
    case 10:
      var value = /** @type {number} */ (reader.readDouble());
      msg.setTotalSavingsOverLife(value);
      break;
    default:
      reader.skipField();
      break;
    }
  }
  return msg;
};


/**
 * Serializes the message to binary data (in protobuf wire format).
 * @return {!Uint8Array}
 */
proto.sensen.finance.RefinanceResponse.prototype.serializeBinary = function() {
  var writer = new jspb.BinaryWriter();
  proto.sensen.finance.RefinanceResponse.serializeBinaryToWriter(this, writer);
  return writer.getResultBuffer();
};


/**
 * Serializes the given message to binary data (in protobuf wire
 * format), writing to the given BinaryWriter.
 * @param {!proto.sensen.finance.RefinanceResponse} message
 * @param {!jspb.BinaryWriter} writer
 * @suppress {unusedLocalVariables} f is only used for nested messages
 */
proto.sensen.finance.RefinanceResponse.serializeBinaryToWriter = function(message, writer) {
  var f = undefined;
  f = message.getNewLoanAmount();
  if (f.length > 0) {
    writer.writeString(
      1,
      f
    );
  }
  f = message.getNewMonthlyPayment();
  if (f.length > 0) {
    writer.writeString(
      2,
      f
    );
  }
  f = message.getMonthlySavingsInitial();
  if (f.length > 0) {
    writer.writeString(
      3,
      f
    );
  }
  f = message.getCurrentLoanPmiDropOffMonths();
  if (f !== 0) {
    writer.writeInt32(
      4,
      f
    );
  }
  f = message.getNewLoanPmiDropOffMonths();
  if (f !== 0) {
    writer.writeInt32(
      5,
      f
    );
  }
  f = message.getPayoffDateShiftMonths();
  if (f !== 0) {
    writer.writeInt32(
      6,
      f
    );
  }
  f = message.getSimpleBreakEvenMonths();
  if (f !== 0) {
    writer.writeInt32(
      7,
      f
    );
  }
  f = message.getCashFlowBreakEvenMonths();
  if (f !== 0) {
    writer.writeInt32(
      8,
      f
    );
  }
  f = message.getEquityAdjustedBreakEvenMonths();
  if (f !== 0) {
    writer.writeInt32(
      9,
      f
    );
  }
  f = message.getTotalSavingsOverLife();
  if (f !== 0.0) {
    writer.writeDouble(
      10,
      f
    );
  }
};


/**
 * optional string new_loan_amount = 1;
 * @return {string}
 */
proto.sensen.finance.RefinanceResponse.prototype.getNewLoanAmount = function() {
  return /** @type {string} */ (jspb.Message.getFieldWithDefault(this, 1, ""));
};


/**
 * @param {string} value
 * @return {!proto.sensen.finance.RefinanceResponse} returns this
 */
proto.sensen.finance.RefinanceResponse.prototype.setNewLoanAmount = function(value) {
  return jspb.Message.setProto3StringField(this, 1, value);
};


/**
 * optional string new_monthly_payment = 2;
 * @return {string}
 */
proto.sensen.finance.RefinanceResponse.prototype.getNewMonthlyPayment = function() {
  return /** @type {string} */ (jspb.Message.getFieldWithDefault(this, 2, ""));
};


/**
 * @param {string} value
 * @return {!proto.sensen.finance.RefinanceResponse} returns this
 */
proto.sensen.finance.RefinanceResponse.prototype.setNewMonthlyPayment = function(value) {
  return jspb.Message.setProto3StringField(this, 2, value);
};


/**
 * optional string monthly_savings_initial = 3;
 * @return {string}
 */
proto.sensen.finance.RefinanceResponse.prototype.getMonthlySavingsInitial = function() {
  return /** @type {string} */ (jspb.Message.getFieldWithDefault(this, 3, ""));
};


/**
 * @param {string} value
 * @return {!proto.sensen.finance.RefinanceResponse} returns this
 */
proto.sensen.finance.RefinanceResponse.prototype.setMonthlySavingsInitial = function(value) {
  return jspb.Message.setProto3StringField(this, 3, value);
};


/**
 * optional int32 current_loan_pmi_drop_off_months = 4;
 * @return {number}
 */
proto.sensen.finance.RefinanceResponse.prototype.getCurrentLoanPmiDropOffMonths = function() {
  return /** @type {number} */ (jspb.Message.getFieldWithDefault(this, 4, 0));
};


/**
 * @param {number} value
 * @return {!proto.sensen.finance.RefinanceResponse} returns this
 */
proto.sensen.finance.RefinanceResponse.prototype.setCurrentLoanPmiDropOffMonths = function(value) {
  return jspb.Message.setProto3IntField(this, 4, value);
};


/**
 * optional int32 new_loan_pmi_drop_off_months = 5;
 * @return {number}
 */
proto.sensen.finance.RefinanceResponse.prototype.getNewLoanPmiDropOffMonths = function() {
  return /** @type {number} */ (jspb.Message.getFieldWithDefault(this, 5, 0));
};


/**
 * @param {number} value
 * @return {!proto.sensen.finance.RefinanceResponse} returns this
 */
proto.sensen.finance.RefinanceResponse.prototype.setNewLoanPmiDropOffMonths = function(value) {
  return jspb.Message.setProto3IntField(this, 5, value);
};


/**
 * optional int32 payoff_date_shift_months = 6;
 * @return {number}
 */
proto.sensen.finance.RefinanceResponse.prototype.getPayoffDateShiftMonths = function() {
  return /** @type {number} */ (jspb.Message.getFieldWithDefault(this, 6, 0));
};


/**
 * @param {number} value
 * @return {!proto.sensen.finance.RefinanceResponse} returns this
 */
proto.sensen.finance.RefinanceResponse.prototype.setPayoffDateShiftMonths = function(value) {
  return jspb.Message.setProto3IntField(this, 6, value);
};


/**
 * optional int32 simple_break_even_months = 7;
 * @return {number}
 */
proto.sensen.finance.RefinanceResponse.prototype.getSimpleBreakEvenMonths = function() {
  return /** @type {number} */ (jspb.Message.getFieldWithDefault(this, 7, 0));
};


/**
 * @param {number} value
 * @return {!proto.sensen.finance.RefinanceResponse} returns this
 */
proto.sensen.finance.RefinanceResponse.prototype.setSimpleBreakEvenMonths = function(value) {
  return jspb.Message.setProto3IntField(this, 7, value);
};


/**
 * optional int32 cash_flow_break_even_months = 8;
 * @return {number}
 */
proto.sensen.finance.RefinanceResponse.prototype.getCashFlowBreakEvenMonths = function() {
  return /** @type {number} */ (jspb.Message.getFieldWithDefault(this, 8, 0));
};


/**
 * @param {number} value
 * @return {!proto.sensen.finance.RefinanceResponse} returns this
 */
proto.sensen.finance.RefinanceResponse.prototype.setCashFlowBreakEvenMonths = function(value) {
  return jspb.Message.setProto3IntField(this, 8, value);
};


/**
 * optional int32 equity_adjusted_break_even_months = 9;
 * @return {number}
 */
proto.sensen.finance.RefinanceResponse.prototype.getEquityAdjustedBreakEvenMonths = function() {
  return /** @type {number} */ (jspb.Message.getFieldWithDefault(this, 9, 0));
};


/**
 * @param {number} value
 * @return {!proto.sensen.finance.RefinanceResponse} returns this
 */
proto.sensen.finance.RefinanceResponse.prototype.setEquityAdjustedBreakEvenMonths = function(value) {
  return jspb.Message.setProto3IntField(this, 9, value);
};


/**
 * optional double total_savings_over_life = 10;
 * @return {number}
 */
proto.sensen.finance.RefinanceResponse.prototype.getTotalSavingsOverLife = function() {
  return /** @type {number} */ (jspb.Message.getFloatingPointFieldWithDefault(this, 10, 0.0));
};


/**
 * @param {number} value
 * @return {!proto.sensen.finance.RefinanceResponse} returns this
 */
proto.sensen.finance.RefinanceResponse.prototype.setTotalSavingsOverLife = function(value) {
  return jspb.Message.setProto3FloatField(this, 10, value);
};





if (jspb.Message.GENERATE_TO_OBJECT) {
/**
 * Creates an object representation of this proto.
 * Field names that are reserved in JavaScript and will be renamed to pb_name.
 * Optional fields that are not set will be set to undefined.
 * To access a reserved field use, foo.pb_<name>, eg, foo.pb_default.
 * For the list of reserved names please see:
 *     net/proto2/compiler/js/internal/generator.cc#kKeyword.
 * @param {boolean=} opt_includeInstance Deprecated. whether to include the
 *     JSPB instance for transitional soy proto support:
 *     http://goto/soy-param-migration
 * @return {!Object}
 */
proto.sensen.finance.PayoffTimingRequest.prototype.toObject = function(opt_includeInstance) {
  return proto.sensen.finance.PayoffTimingRequest.toObject(opt_includeInstance, this);
};


/**
 * Static version of the {@see toObject} method.
 * @param {boolean|undefined} includeInstance Deprecated. Whether to include
 *     the JSPB instance for transitional soy proto support:
 *     http://goto/soy-param-migration
 * @param {!proto.sensen.finance.PayoffTimingRequest} msg The msg instance to transform.
 * @return {!Object}
 * @suppress {unusedLocalVariables} f is only used for nested messages
 */
proto.sensen.finance.PayoffTimingRequest.toObject = function(includeInstance, msg) {
  var f, obj = {
    currentLoanBalance: jspb.Message.getFieldWithDefault(msg, 1, ""),
    annualRate: jspb.Message.getFieldWithDefault(msg, 2, ""),
    currentMonthlyPayment: jspb.Message.getFieldWithDefault(msg, 3, ""),
    extraMonthlyPayment: jspb.Message.getFieldWithDefault(msg, 4, ""),
    paymentsPerYear: jspb.Message.getFieldWithDefault(msg, 5, 0)
  };

  if (includeInstance) {
    obj.$jspbMessageInstance = msg;
  }
  return obj;
};
}


/**
 * Deserializes binary data (in protobuf wire format).
 * @param {jspb.ByteSource} bytes The bytes to deserialize.
 * @return {!proto.sensen.finance.PayoffTimingRequest}
 */
proto.sensen.finance.PayoffTimingRequest.deserializeBinary = function(bytes) {
  var reader = new jspb.BinaryReader(bytes);
  var msg = new proto.sensen.finance.PayoffTimingRequest;
  return proto.sensen.finance.PayoffTimingRequest.deserializeBinaryFromReader(msg, reader);
};


/**
 * Deserializes binary data (in protobuf wire format) from the
 * given reader into the given message object.
 * @param {!proto.sensen.finance.PayoffTimingRequest} msg The message object to deserialize into.
 * @param {!jspb.BinaryReader} reader The BinaryReader to use.
 * @return {!proto.sensen.finance.PayoffTimingRequest}
 */
proto.sensen.finance.PayoffTimingRequest.deserializeBinaryFromReader = function(msg, reader) {
  while (reader.nextField()) {
    if (reader.isEndGroup()) {
      break;
    }
    var field = reader.getFieldNumber();
    switch (field) {
    case 1:
      var value = /** @type {string} */ (reader.readString());
      msg.setCurrentLoanBalance(value);
      break;
    case 2:
      var value = /** @type {string} */ (reader.readString());
      msg.setAnnualRate(value);
      break;
    case 3:
      var value = /** @type {string} */ (reader.readString());
      msg.setCurrentMonthlyPayment(value);
      break;
    case 4:
      var value = /** @type {string} */ (reader.readString());
      msg.setExtraMonthlyPayment(value);
      break;
    case 5:
      var value = /** @type {number} */ (reader.readInt32());
      msg.setPaymentsPerYear(value);
      break;
    default:
      reader.skipField();
      break;
    }
  }
  return msg;
};


/**
 * Serializes the message to binary data (in protobuf wire format).
 * @return {!Uint8Array}
 */
proto.sensen.finance.PayoffTimingRequest.prototype.serializeBinary = function() {
  var writer = new jspb.BinaryWriter();
  proto.sensen.finance.PayoffTimingRequest.serializeBinaryToWriter(this, writer);
  return writer.getResultBuffer();
};


/**
 * Serializes the given message to binary data (in protobuf wire
 * format), writing to the given BinaryWriter.
 * @param {!proto.sensen.finance.PayoffTimingRequest} message
 * @param {!jspb.BinaryWriter} writer
 * @suppress {unusedLocalVariables} f is only used for nested messages
 */
proto.sensen.finance.PayoffTimingRequest.serializeBinaryToWriter = function(message, writer) {
  var f = undefined;
  f = message.getCurrentLoanBalance();
  if (f.length > 0) {
    writer.writeString(
      1,
      f
    );
  }
  f = message.getAnnualRate();
  if (f.length > 0) {
    writer.writeString(
      2,
      f
    );
  }
  f = message.getCurrentMonthlyPayment();
  if (f.length > 0) {
    writer.writeString(
      3,
      f
    );
  }
  f = message.getExtraMonthlyPayment();
  if (f.length > 0) {
    writer.writeString(
      4,
      f
    );
  }
  f = message.getPaymentsPerYear();
  if (f !== 0) {
    writer.writeInt32(
      5,
      f
    );
  }
};


/**
 * optional string current_loan_balance = 1;
 * @return {string}
 */
proto.sensen.finance.PayoffTimingRequest.prototype.getCurrentLoanBalance = function() {
  return /** @type {string} */ (jspb.Message.getFieldWithDefault(this, 1, ""));
};


/**
 * @param {string} value
 * @return {!proto.sensen.finance.PayoffTimingRequest} returns this
 */
proto.sensen.finance.PayoffTimingRequest.prototype.setCurrentLoanBalance = function(value) {
  return jspb.Message.setProto3StringField(this, 1, value);
};


/**
 * optional string annual_rate = 2;
 * @return {string}
 */
proto.sensen.finance.PayoffTimingRequest.prototype.getAnnualRate = function() {
  return /** @type {string} */ (jspb.Message.getFieldWithDefault(this, 2, ""));
};


/**
 * @param {string} value
 * @return {!proto.sensen.finance.PayoffTimingRequest} returns this
 */
proto.sensen.finance.PayoffTimingRequest.prototype.setAnnualRate = function(value) {
  return jspb.Message.setProto3StringField(this, 2, value);
};


/**
 * optional string current_monthly_payment = 3;
 * @return {string}
 */
proto.sensen.finance.PayoffTimingRequest.prototype.getCurrentMonthlyPayment = function() {
  return /** @type {string} */ (jspb.Message.getFieldWithDefault(this, 3, ""));
};


/**
 * @param {string} value
 * @return {!proto.sensen.finance.PayoffTimingRequest} returns this
 */
proto.sensen.finance.PayoffTimingRequest.prototype.setCurrentMonthlyPayment = function(value) {
  return jspb.Message.setProto3StringField(this, 3, value);
};


/**
 * optional string extra_monthly_payment = 4;
 * @return {string}
 */
proto.sensen.finance.PayoffTimingRequest.prototype.getExtraMonthlyPayment = function() {
  return /** @type {string} */ (jspb.Message.getFieldWithDefault(this, 4, ""));
};


/**
 * @param {string} value
 * @return {!proto.sensen.finance.PayoffTimingRequest} returns this
 */
proto.sensen.finance.PayoffTimingRequest.prototype.setExtraMonthlyPayment = function(value) {
  return jspb.Message.setProto3StringField(this, 4, value);
};


/**
 * optional int32 payments_per_year = 5;
 * @return {number}
 */
proto.sensen.finance.PayoffTimingRequest.prototype.getPaymentsPerYear = function() {
  return /** @type {number} */ (jspb.Message.getFieldWithDefault(this, 5, 0));
};


/**
 * @param {number} value
 * @return {!proto.sensen.finance.PayoffTimingRequest} returns this
 */
proto.sensen.finance.PayoffTimingRequest.prototype.setPaymentsPerYear = function(value) {
  return jspb.Message.setProto3IntField(this, 5, value);
};





if (jspb.Message.GENERATE_TO_OBJECT) {
/**
 * Creates an object representation of this proto.
 * Field names that are reserved in JavaScript and will be renamed to pb_name.
 * Optional fields that are not set will be set to undefined.
 * To access a reserved field use, foo.pb_<name>, eg, foo.pb_default.
 * For the list of reserved names please see:
 *     net/proto2/compiler/js/internal/generator.cc#kKeyword.
 * @param {boolean=} opt_includeInstance Deprecated. whether to include the
 *     JSPB instance for transitional soy proto support:
 *     http://goto/soy-param-migration
 * @return {!Object}
 */
proto.sensen.finance.PayoffTimingResponse.prototype.toObject = function(opt_includeInstance) {
  return proto.sensen.finance.PayoffTimingResponse.toObject(opt_includeInstance, this);
};


/**
 * Static version of the {@see toObject} method.
 * @param {boolean|undefined} includeInstance Deprecated. Whether to include
 *     the JSPB instance for transitional soy proto support:
 *     http://goto/soy-param-migration
 * @param {!proto.sensen.finance.PayoffTimingResponse} msg The msg instance to transform.
 * @return {!Object}
 * @suppress {unusedLocalVariables} f is only used for nested messages
 */
proto.sensen.finance.PayoffTimingResponse.toObject = function(includeInstance, msg) {
  var f, obj = {
    originalMonthsRemaining: jspb.Message.getFieldWithDefault(msg, 1, 0),
    newMonthsRemaining: jspb.Message.getFieldWithDefault(msg, 2, 0),
    monthsSaved: jspb.Message.getFieldWithDefault(msg, 3, 0),
    totalInterestSaved: jspb.Message.getFieldWithDefault(msg, 4, "")
  };

  if (includeInstance) {
    obj.$jspbMessageInstance = msg;
  }
  return obj;
};
}


/**
 * Deserializes binary data (in protobuf wire format).
 * @param {jspb.ByteSource} bytes The bytes to deserialize.
 * @return {!proto.sensen.finance.PayoffTimingResponse}
 */
proto.sensen.finance.PayoffTimingResponse.deserializeBinary = function(bytes) {
  var reader = new jspb.BinaryReader(bytes);
  var msg = new proto.sensen.finance.PayoffTimingResponse;
  return proto.sensen.finance.PayoffTimingResponse.deserializeBinaryFromReader(msg, reader);
};


/**
 * Deserializes binary data (in protobuf wire format) from the
 * given reader into the given message object.
 * @param {!proto.sensen.finance.PayoffTimingResponse} msg The message object to deserialize into.
 * @param {!jspb.BinaryReader} reader The BinaryReader to use.
 * @return {!proto.sensen.finance.PayoffTimingResponse}
 */
proto.sensen.finance.PayoffTimingResponse.deserializeBinaryFromReader = function(msg, reader) {
  while (reader.nextField()) {
    if (reader.isEndGroup()) {
      break;
    }
    var field = reader.getFieldNumber();
    switch (field) {
    case 1:
      var value = /** @type {number} */ (reader.readInt32());
      msg.setOriginalMonthsRemaining(value);
      break;
    case 2:
      var value = /** @type {number} */ (reader.readInt32());
      msg.setNewMonthsRemaining(value);
      break;
    case 3:
      var value = /** @type {number} */ (reader.readInt32());
      msg.setMonthsSaved(value);
      break;
    case 4:
      var value = /** @type {string} */ (reader.readString());
      msg.setTotalInterestSaved(value);
      break;
    default:
      reader.skipField();
      break;
    }
  }
  return msg;
};


/**
 * Serializes the message to binary data (in protobuf wire format).
 * @return {!Uint8Array}
 */
proto.sensen.finance.PayoffTimingResponse.prototype.serializeBinary = function() {
  var writer = new jspb.BinaryWriter();
  proto.sensen.finance.PayoffTimingResponse.serializeBinaryToWriter(this, writer);
  return writer.getResultBuffer();
};


/**
 * Serializes the given message to binary data (in protobuf wire
 * format), writing to the given BinaryWriter.
 * @param {!proto.sensen.finance.PayoffTimingResponse} message
 * @param {!jspb.BinaryWriter} writer
 * @suppress {unusedLocalVariables} f is only used for nested messages
 */
proto.sensen.finance.PayoffTimingResponse.serializeBinaryToWriter = function(message, writer) {
  var f = undefined;
  f = message.getOriginalMonthsRemaining();
  if (f !== 0) {
    writer.writeInt32(
      1,
      f
    );
  }
  f = message.getNewMonthsRemaining();
  if (f !== 0) {
    writer.writeInt32(
      2,
      f
    );
  }
  f = message.getMonthsSaved();
  if (f !== 0) {
    writer.writeInt32(
      3,
      f
    );
  }
  f = message.getTotalInterestSaved();
  if (f.length > 0) {
    writer.writeString(
      4,
      f
    );
  }
};


/**
 * optional int32 original_months_remaining = 1;
 * @return {number}
 */
proto.sensen.finance.PayoffTimingResponse.prototype.getOriginalMonthsRemaining = function() {
  return /** @type {number} */ (jspb.Message.getFieldWithDefault(this, 1, 0));
};


/**
 * @param {number} value
 * @return {!proto.sensen.finance.PayoffTimingResponse} returns this
 */
proto.sensen.finance.PayoffTimingResponse.prototype.setOriginalMonthsRemaining = function(value) {
  return jspb.Message.setProto3IntField(this, 1, value);
};


/**
 * optional int32 new_months_remaining = 2;
 * @return {number}
 */
proto.sensen.finance.PayoffTimingResponse.prototype.getNewMonthsRemaining = function() {
  return /** @type {number} */ (jspb.Message.getFieldWithDefault(this, 2, 0));
};


/**
 * @param {number} value
 * @return {!proto.sensen.finance.PayoffTimingResponse} returns this
 */
proto.sensen.finance.PayoffTimingResponse.prototype.setNewMonthsRemaining = function(value) {
  return jspb.Message.setProto3IntField(this, 2, value);
};


/**
 * optional int32 months_saved = 3;
 * @return {number}
 */
proto.sensen.finance.PayoffTimingResponse.prototype.getMonthsSaved = function() {
  return /** @type {number} */ (jspb.Message.getFieldWithDefault(this, 3, 0));
};


/**
 * @param {number} value
 * @return {!proto.sensen.finance.PayoffTimingResponse} returns this
 */
proto.sensen.finance.PayoffTimingResponse.prototype.setMonthsSaved = function(value) {
  return jspb.Message.setProto3IntField(this, 3, value);
};


/**
 * optional string total_interest_saved = 4;
 * @return {string}
 */
proto.sensen.finance.PayoffTimingResponse.prototype.getTotalInterestSaved = function() {
  return /** @type {string} */ (jspb.Message.getFieldWithDefault(this, 4, ""));
};


/**
 * @param {string} value
 * @return {!proto.sensen.finance.PayoffTimingResponse} returns this
 */
proto.sensen.finance.PayoffTimingResponse.prototype.setTotalInterestSaved = function(value) {
  return jspb.Message.setProto3StringField(this, 4, value);
};





if (jspb.Message.GENERATE_TO_OBJECT) {
/**
 * Creates an object representation of this proto.
 * Field names that are reserved in JavaScript and will be renamed to pb_name.
 * Optional fields that are not set will be set to undefined.
 * To access a reserved field use, foo.pb_<name>, eg, foo.pb_default.
 * For the list of reserved names please see:
 *     net/proto2/compiler/js/internal/generator.cc#kKeyword.
 * @param {boolean=} opt_includeInstance Deprecated. whether to include the
 *     JSPB instance for transitional soy proto support:
 *     http://goto/soy-param-migration
 * @return {!Object}
 */
proto.sensen.finance.MortgageRecastRequest.prototype.toObject = function(opt_includeInstance) {
  return proto.sensen.finance.MortgageRecastRequest.toObject(opt_includeInstance, this);
};


/**
 * Static version of the {@see toObject} method.
 * @param {boolean|undefined} includeInstance Deprecated. Whether to include
 *     the JSPB instance for transitional soy proto support:
 *     http://goto/soy-param-migration
 * @param {!proto.sensen.finance.MortgageRecastRequest} msg The msg instance to transform.
 * @return {!Object}
 * @suppress {unusedLocalVariables} f is only used for nested messages
 */
proto.sensen.finance.MortgageRecastRequest.toObject = function(includeInstance, msg) {
  var f, obj = {
    currentLoanBalance: jspb.Message.getFieldWithDefault(msg, 1, ""),
    currentMonthlyPayment: jspb.Message.getFieldWithDefault(msg, 2, ""),
    lumpSumPayment: jspb.Message.getFieldWithDefault(msg, 3, ""),
    annualRate: jspb.Message.getFieldWithDefault(msg, 4, ""),
    remainingMonths: jspb.Message.getFieldWithDefault(msg, 5, 0),
    paymentsPerYear: jspb.Message.getFieldWithDefault(msg, 6, 0)
  };

  if (includeInstance) {
    obj.$jspbMessageInstance = msg;
  }
  return obj;
};
}


/**
 * Deserializes binary data (in protobuf wire format).
 * @param {jspb.ByteSource} bytes The bytes to deserialize.
 * @return {!proto.sensen.finance.MortgageRecastRequest}
 */
proto.sensen.finance.MortgageRecastRequest.deserializeBinary = function(bytes) {
  var reader = new jspb.BinaryReader(bytes);
  var msg = new proto.sensen.finance.MortgageRecastRequest;
  return proto.sensen.finance.MortgageRecastRequest.deserializeBinaryFromReader(msg, reader);
};


/**
 * Deserializes binary data (in protobuf wire format) from the
 * given reader into the given message object.
 * @param {!proto.sensen.finance.MortgageRecastRequest} msg The message object to deserialize into.
 * @param {!jspb.BinaryReader} reader The BinaryReader to use.
 * @return {!proto.sensen.finance.MortgageRecastRequest}
 */
proto.sensen.finance.MortgageRecastRequest.deserializeBinaryFromReader = function(msg, reader) {
  while (reader.nextField()) {
    if (reader.isEndGroup()) {
      break;
    }
    var field = reader.getFieldNumber();
    switch (field) {
    case 1:
      var value = /** @type {string} */ (reader.readString());
      msg.setCurrentLoanBalance(value);
      break;
    case 2:
      var value = /** @type {string} */ (reader.readString());
      msg.setCurrentMonthlyPayment(value);
      break;
    case 3:
      var value = /** @type {string} */ (reader.readString());
      msg.setLumpSumPayment(value);
      break;
    case 4:
      var value = /** @type {string} */ (reader.readString());
      msg.setAnnualRate(value);
      break;
    case 5:
      var value = /** @type {number} */ (reader.readInt32());
      msg.setRemainingMonths(value);
      break;
    case 6:
      var value = /** @type {number} */ (reader.readInt32());
      msg.setPaymentsPerYear(value);
      break;
    default:
      reader.skipField();
      break;
    }
  }
  return msg;
};


/**
 * Serializes the message to binary data (in protobuf wire format).
 * @return {!Uint8Array}
 */
proto.sensen.finance.MortgageRecastRequest.prototype.serializeBinary = function() {
  var writer = new jspb.BinaryWriter();
  proto.sensen.finance.MortgageRecastRequest.serializeBinaryToWriter(this, writer);
  return writer.getResultBuffer();
};


/**
 * Serializes the given message to binary data (in protobuf wire
 * format), writing to the given BinaryWriter.
 * @param {!proto.sensen.finance.MortgageRecastRequest} message
 * @param {!jspb.BinaryWriter} writer
 * @suppress {unusedLocalVariables} f is only used for nested messages
 */
proto.sensen.finance.MortgageRecastRequest.serializeBinaryToWriter = function(message, writer) {
  var f = undefined;
  f = message.getCurrentLoanBalance();
  if (f.length > 0) {
    writer.writeString(
      1,
      f
    );
  }
  f = message.getCurrentMonthlyPayment();
  if (f.length > 0) {
    writer.writeString(
      2,
      f
    );
  }
  f = message.getLumpSumPayment();
  if (f.length > 0) {
    writer.writeString(
      3,
      f
    );
  }
  f = message.getAnnualRate();
  if (f.length > 0) {
    writer.writeString(
      4,
      f
    );
  }
  f = message.getRemainingMonths();
  if (f !== 0) {
    writer.writeInt32(
      5,
      f
    );
  }
  f = message.getPaymentsPerYear();
  if (f !== 0) {
    writer.writeInt32(
      6,
      f
    );
  }
};


/**
 * optional string current_loan_balance = 1;
 * @return {string}
 */
proto.sensen.finance.MortgageRecastRequest.prototype.getCurrentLoanBalance = function() {
  return /** @type {string} */ (jspb.Message.getFieldWithDefault(this, 1, ""));
};


/**
 * @param {string} value
 * @return {!proto.sensen.finance.MortgageRecastRequest} returns this
 */
proto.sensen.finance.MortgageRecastRequest.prototype.setCurrentLoanBalance = function(value) {
  return jspb.Message.setProto3StringField(this, 1, value);
};


/**
 * optional string current_monthly_payment = 2;
 * @return {string}
 */
proto.sensen.finance.MortgageRecastRequest.prototype.getCurrentMonthlyPayment = function() {
  return /** @type {string} */ (jspb.Message.getFieldWithDefault(this, 2, ""));
};


/**
 * @param {string} value
 * @return {!proto.sensen.finance.MortgageRecastRequest} returns this
 */
proto.sensen.finance.MortgageRecastRequest.prototype.setCurrentMonthlyPayment = function(value) {
  return jspb.Message.setProto3StringField(this, 2, value);
};


/**
 * optional string lump_sum_payment = 3;
 * @return {string}
 */
proto.sensen.finance.MortgageRecastRequest.prototype.getLumpSumPayment = function() {
  return /** @type {string} */ (jspb.Message.getFieldWithDefault(this, 3, ""));
};


/**
 * @param {string} value
 * @return {!proto.sensen.finance.MortgageRecastRequest} returns this
 */
proto.sensen.finance.MortgageRecastRequest.prototype.setLumpSumPayment = function(value) {
  return jspb.Message.setProto3StringField(this, 3, value);
};


/**
 * optional string annual_rate = 4;
 * @return {string}
 */
proto.sensen.finance.MortgageRecastRequest.prototype.getAnnualRate = function() {
  return /** @type {string} */ (jspb.Message.getFieldWithDefault(this, 4, ""));
};


/**
 * @param {string} value
 * @return {!proto.sensen.finance.MortgageRecastRequest} returns this
 */
proto.sensen.finance.MortgageRecastRequest.prototype.setAnnualRate = function(value) {
  return jspb.Message.setProto3StringField(this, 4, value);
};


/**
 * optional int32 remaining_months = 5;
 * @return {number}
 */
proto.sensen.finance.MortgageRecastRequest.prototype.getRemainingMonths = function() {
  return /** @type {number} */ (jspb.Message.getFieldWithDefault(this, 5, 0));
};


/**
 * @param {number} value
 * @return {!proto.sensen.finance.MortgageRecastRequest} returns this
 */
proto.sensen.finance.MortgageRecastRequest.prototype.setRemainingMonths = function(value) {
  return jspb.Message.setProto3IntField(this, 5, value);
};


/**
 * optional int32 payments_per_year = 6;
 * @return {number}
 */
proto.sensen.finance.MortgageRecastRequest.prototype.getPaymentsPerYear = function() {
  return /** @type {number} */ (jspb.Message.getFieldWithDefault(this, 6, 0));
};


/**
 * @param {number} value
 * @return {!proto.sensen.finance.MortgageRecastRequest} returns this
 */
proto.sensen.finance.MortgageRecastRequest.prototype.setPaymentsPerYear = function(value) {
  return jspb.Message.setProto3IntField(this, 6, value);
};





if (jspb.Message.GENERATE_TO_OBJECT) {
/**
 * Creates an object representation of this proto.
 * Field names that are reserved in JavaScript and will be renamed to pb_name.
 * Optional fields that are not set will be set to undefined.
 * To access a reserved field use, foo.pb_<name>, eg, foo.pb_default.
 * For the list of reserved names please see:
 *     net/proto2/compiler/js/internal/generator.cc#kKeyword.
 * @param {boolean=} opt_includeInstance Deprecated. whether to include the
 *     JSPB instance for transitional soy proto support:
 *     http://goto/soy-param-migration
 * @return {!Object}
 */
proto.sensen.finance.MortgageRecastResponse.prototype.toObject = function(opt_includeInstance) {
  return proto.sensen.finance.MortgageRecastResponse.toObject(opt_includeInstance, this);
};


/**
 * Static version of the {@see toObject} method.
 * @param {boolean|undefined} includeInstance Deprecated. Whether to include
 *     the JSPB instance for transitional soy proto support:
 *     http://goto/soy-param-migration
 * @param {!proto.sensen.finance.MortgageRecastResponse} msg The msg instance to transform.
 * @return {!Object}
 * @suppress {unusedLocalVariables} f is only used for nested messages
 */
proto.sensen.finance.MortgageRecastResponse.toObject = function(includeInstance, msg) {
  var f, obj = {
    newMonthlyPayment: jspb.Message.getFieldWithDefault(msg, 1, ""),
    monthlySavings: jspb.Message.getFieldWithDefault(msg, 2, "")
  };

  if (includeInstance) {
    obj.$jspbMessageInstance = msg;
  }
  return obj;
};
}


/**
 * Deserializes binary data (in protobuf wire format).
 * @param {jspb.ByteSource} bytes The bytes to deserialize.
 * @return {!proto.sensen.finance.MortgageRecastResponse}
 */
proto.sensen.finance.MortgageRecastResponse.deserializeBinary = function(bytes) {
  var reader = new jspb.BinaryReader(bytes);
  var msg = new proto.sensen.finance.MortgageRecastResponse;
  return proto.sensen.finance.MortgageRecastResponse.deserializeBinaryFromReader(msg, reader);
};


/**
 * Deserializes binary data (in protobuf wire format) from the
 * given reader into the given message object.
 * @param {!proto.sensen.finance.MortgageRecastResponse} msg The message object to deserialize into.
 * @param {!jspb.BinaryReader} reader The BinaryReader to use.
 * @return {!proto.sensen.finance.MortgageRecastResponse}
 */
proto.sensen.finance.MortgageRecastResponse.deserializeBinaryFromReader = function(msg, reader) {
  while (reader.nextField()) {
    if (reader.isEndGroup()) {
      break;
    }
    var field = reader.getFieldNumber();
    switch (field) {
    case 1:
      var value = /** @type {string} */ (reader.readString());
      msg.setNewMonthlyPayment(value);
      break;
    case 2:
      var value = /** @type {string} */ (reader.readString());
      msg.setMonthlySavings(value);
      break;
    default:
      reader.skipField();
      break;
    }
  }
  return msg;
};


/**
 * Serializes the message to binary data (in protobuf wire format).
 * @return {!Uint8Array}
 */
proto.sensen.finance.MortgageRecastResponse.prototype.serializeBinary = function() {
  var writer = new jspb.BinaryWriter();
  proto.sensen.finance.MortgageRecastResponse.serializeBinaryToWriter(this, writer);
  return writer.getResultBuffer();
};


/**
 * Serializes the given message to binary data (in protobuf wire
 * format), writing to the given BinaryWriter.
 * @param {!proto.sensen.finance.MortgageRecastResponse} message
 * @param {!jspb.BinaryWriter} writer
 * @suppress {unusedLocalVariables} f is only used for nested messages
 */
proto.sensen.finance.MortgageRecastResponse.serializeBinaryToWriter = function(message, writer) {
  var f = undefined;
  f = message.getNewMonthlyPayment();
  if (f.length > 0) {
    writer.writeString(
      1,
      f
    );
  }
  f = message.getMonthlySavings();
  if (f.length > 0) {
    writer.writeString(
      2,
      f
    );
  }
};


/**
 * optional string new_monthly_payment = 1;
 * @return {string}
 */
proto.sensen.finance.MortgageRecastResponse.prototype.getNewMonthlyPayment = function() {
  return /** @type {string} */ (jspb.Message.getFieldWithDefault(this, 1, ""));
};


/**
 * @param {string} value
 * @return {!proto.sensen.finance.MortgageRecastResponse} returns this
 */
proto.sensen.finance.MortgageRecastResponse.prototype.setNewMonthlyPayment = function(value) {
  return jspb.Message.setProto3StringField(this, 1, value);
};


/**
 * optional string monthly_savings = 2;
 * @return {string}
 */
proto.sensen.finance.MortgageRecastResponse.prototype.getMonthlySavings = function() {
  return /** @type {string} */ (jspb.Message.getFieldWithDefault(this, 2, ""));
};


/**
 * @param {string} value
 * @return {!proto.sensen.finance.MortgageRecastResponse} returns this
 */
proto.sensen.finance.MortgageRecastResponse.prototype.setMonthlySavings = function(value) {
  return jspb.Message.setProto3StringField(this, 2, value);
};



/**
 * List of repeated fields within this message type.
 * @private {!Array<number>}
 * @const
 */
proto.sensen.finance.NpvRequest.repeatedFields_ = [2];



if (jspb.Message.GENERATE_TO_OBJECT) {
/**
 * Creates an object representation of this proto.
 * Field names that are reserved in JavaScript and will be renamed to pb_name.
 * Optional fields that are not set will be set to undefined.
 * To access a reserved field use, foo.pb_<name>, eg, foo.pb_default.
 * For the list of reserved names please see:
 *     net/proto2/compiler/js/internal/generator.cc#kKeyword.
 * @param {boolean=} opt_includeInstance Deprecated. whether to include the
 *     JSPB instance for transitional soy proto support:
 *     http://goto/soy-param-migration
 * @return {!Object}
 */
proto.sensen.finance.NpvRequest.prototype.toObject = function(opt_includeInstance) {
  return proto.sensen.finance.NpvRequest.toObject(opt_includeInstance, this);
};


/**
 * Static version of the {@see toObject} method.
 * @param {boolean|undefined} includeInstance Deprecated. Whether to include
 *     the JSPB instance for transitional soy proto support:
 *     http://goto/soy-param-migration
 * @param {!proto.sensen.finance.NpvRequest} msg The msg instance to transform.
 * @return {!Object}
 * @suppress {unusedLocalVariables} f is only used for nested messages
 */
proto.sensen.finance.NpvRequest.toObject = function(includeInstance, msg) {
  var f, obj = {
    rate: jspb.Message.getFloatingPointFieldWithDefault(msg, 1, 0.0),
    valuesList: (f = jspb.Message.getRepeatedFloatingPointField(msg, 2)) == null ? undefined : f
  };

  if (includeInstance) {
    obj.$jspbMessageInstance = msg;
  }
  return obj;
};
}


/**
 * Deserializes binary data (in protobuf wire format).
 * @param {jspb.ByteSource} bytes The bytes to deserialize.
 * @return {!proto.sensen.finance.NpvRequest}
 */
proto.sensen.finance.NpvRequest.deserializeBinary = function(bytes) {
  var reader = new jspb.BinaryReader(bytes);
  var msg = new proto.sensen.finance.NpvRequest;
  return proto.sensen.finance.NpvRequest.deserializeBinaryFromReader(msg, reader);
};


/**
 * Deserializes binary data (in protobuf wire format) from the
 * given reader into the given message object.
 * @param {!proto.sensen.finance.NpvRequest} msg The message object to deserialize into.
 * @param {!jspb.BinaryReader} reader The BinaryReader to use.
 * @return {!proto.sensen.finance.NpvRequest}
 */
proto.sensen.finance.NpvRequest.deserializeBinaryFromReader = function(msg, reader) {
  while (reader.nextField()) {
    if (reader.isEndGroup()) {
      break;
    }
    var field = reader.getFieldNumber();
    switch (field) {
    case 1:
      var value = /** @type {number} */ (reader.readDouble());
      msg.setRate(value);
      break;
    case 2:
      var values = /** @type {!Array<number>} */ (reader.isDelimited() ? reader.readPackedDouble() : [reader.readDouble()]);
      for (var i = 0; i < values.length; i++) {
        msg.addValues(values[i]);
      }
      break;
    default:
      reader.skipField();
      break;
    }
  }
  return msg;
};


/**
 * Serializes the message to binary data (in protobuf wire format).
 * @return {!Uint8Array}
 */
proto.sensen.finance.NpvRequest.prototype.serializeBinary = function() {
  var writer = new jspb.BinaryWriter();
  proto.sensen.finance.NpvRequest.serializeBinaryToWriter(this, writer);
  return writer.getResultBuffer();
};


/**
 * Serializes the given message to binary data (in protobuf wire
 * format), writing to the given BinaryWriter.
 * @param {!proto.sensen.finance.NpvRequest} message
 * @param {!jspb.BinaryWriter} writer
 * @suppress {unusedLocalVariables} f is only used for nested messages
 */
proto.sensen.finance.NpvRequest.serializeBinaryToWriter = function(message, writer) {
  var f = undefined;
  f = message.getRate();
  if (f !== 0.0) {
    writer.writeDouble(
      1,
      f
    );
  }
  f = message.getValuesList();
  if (f.length > 0) {
    writer.writePackedDouble(
      2,
      f
    );
  }
};


/**
 * optional double rate = 1;
 * @return {number}
 */
proto.sensen.finance.NpvRequest.prototype.getRate = function() {
  return /** @type {number} */ (jspb.Message.getFloatingPointFieldWithDefault(this, 1, 0.0));
};


/**
 * @param {number} value
 * @return {!proto.sensen.finance.NpvRequest} returns this
 */
proto.sensen.finance.NpvRequest.prototype.setRate = function(value) {
  return jspb.Message.setProto3FloatField(this, 1, value);
};


/**
 * repeated double values = 2;
 * @return {!Array<number>}
 */
proto.sensen.finance.NpvRequest.prototype.getValuesList = function() {
  return /** @type {!Array<number>} */ (jspb.Message.getRepeatedFloatingPointField(this, 2));
};


/**
 * @param {!Array<number>} value
 * @return {!proto.sensen.finance.NpvRequest} returns this
 */
proto.sensen.finance.NpvRequest.prototype.setValuesList = function(value) {
  return jspb.Message.setField(this, 2, value || []);
};


/**
 * @param {number} value
 * @param {number=} opt_index
 * @return {!proto.sensen.finance.NpvRequest} returns this
 */
proto.sensen.finance.NpvRequest.prototype.addValues = function(value, opt_index) {
  return jspb.Message.addToRepeatedField(this, 2, value, opt_index);
};


/**
 * Clears the list making it empty but non-null.
 * @return {!proto.sensen.finance.NpvRequest} returns this
 */
proto.sensen.finance.NpvRequest.prototype.clearValuesList = function() {
  return this.setValuesList([]);
};



/**
 * List of repeated fields within this message type.
 * @private {!Array<number>}
 * @const
 */
proto.sensen.finance.IrrRequest.repeatedFields_ = [1];



if (jspb.Message.GENERATE_TO_OBJECT) {
/**
 * Creates an object representation of this proto.
 * Field names that are reserved in JavaScript and will be renamed to pb_name.
 * Optional fields that are not set will be set to undefined.
 * To access a reserved field use, foo.pb_<name>, eg, foo.pb_default.
 * For the list of reserved names please see:
 *     net/proto2/compiler/js/internal/generator.cc#kKeyword.
 * @param {boolean=} opt_includeInstance Deprecated. whether to include the
 *     JSPB instance for transitional soy proto support:
 *     http://goto/soy-param-migration
 * @return {!Object}
 */
proto.sensen.finance.IrrRequest.prototype.toObject = function(opt_includeInstance) {
  return proto.sensen.finance.IrrRequest.toObject(opt_includeInstance, this);
};


/**
 * Static version of the {@see toObject} method.
 * @param {boolean|undefined} includeInstance Deprecated. Whether to include
 *     the JSPB instance for transitional soy proto support:
 *     http://goto/soy-param-migration
 * @param {!proto.sensen.finance.IrrRequest} msg The msg instance to transform.
 * @return {!Object}
 * @suppress {unusedLocalVariables} f is only used for nested messages
 */
proto.sensen.finance.IrrRequest.toObject = function(includeInstance, msg) {
  var f, obj = {
    valuesList: (f = jspb.Message.getRepeatedFloatingPointField(msg, 1)) == null ? undefined : f,
    guess: jspb.Message.getFloatingPointFieldWithDefault(msg, 2, 0.0)
  };

  if (includeInstance) {
    obj.$jspbMessageInstance = msg;
  }
  return obj;
};
}


/**
 * Deserializes binary data (in protobuf wire format).
 * @param {jspb.ByteSource} bytes The bytes to deserialize.
 * @return {!proto.sensen.finance.IrrRequest}
 */
proto.sensen.finance.IrrRequest.deserializeBinary = function(bytes) {
  var reader = new jspb.BinaryReader(bytes);
  var msg = new proto.sensen.finance.IrrRequest;
  return proto.sensen.finance.IrrRequest.deserializeBinaryFromReader(msg, reader);
};


/**
 * Deserializes binary data (in protobuf wire format) from the
 * given reader into the given message object.
 * @param {!proto.sensen.finance.IrrRequest} msg The message object to deserialize into.
 * @param {!jspb.BinaryReader} reader The BinaryReader to use.
 * @return {!proto.sensen.finance.IrrRequest}
 */
proto.sensen.finance.IrrRequest.deserializeBinaryFromReader = function(msg, reader) {
  while (reader.nextField()) {
    if (reader.isEndGroup()) {
      break;
    }
    var field = reader.getFieldNumber();
    switch (field) {
    case 1:
      var values = /** @type {!Array<number>} */ (reader.isDelimited() ? reader.readPackedDouble() : [reader.readDouble()]);
      for (var i = 0; i < values.length; i++) {
        msg.addValues(values[i]);
      }
      break;
    case 2:
      var value = /** @type {number} */ (reader.readDouble());
      msg.setGuess(value);
      break;
    default:
      reader.skipField();
      break;
    }
  }
  return msg;
};


/**
 * Serializes the message to binary data (in protobuf wire format).
 * @return {!Uint8Array}
 */
proto.sensen.finance.IrrRequest.prototype.serializeBinary = function() {
  var writer = new jspb.BinaryWriter();
  proto.sensen.finance.IrrRequest.serializeBinaryToWriter(this, writer);
  return writer.getResultBuffer();
};


/**
 * Serializes the given message to binary data (in protobuf wire
 * format), writing to the given BinaryWriter.
 * @param {!proto.sensen.finance.IrrRequest} message
 * @param {!jspb.BinaryWriter} writer
 * @suppress {unusedLocalVariables} f is only used for nested messages
 */
proto.sensen.finance.IrrRequest.serializeBinaryToWriter = function(message, writer) {
  var f = undefined;
  f = message.getValuesList();
  if (f.length > 0) {
    writer.writePackedDouble(
      1,
      f
    );
  }
  f = message.getGuess();
  if (f !== 0.0) {
    writer.writeDouble(
      2,
      f
    );
  }
};


/**
 * repeated double values = 1;
 * @return {!Array<number>}
 */
proto.sensen.finance.IrrRequest.prototype.getValuesList = function() {
  return /** @type {!Array<number>} */ (jspb.Message.getRepeatedFloatingPointField(this, 1));
};


/**
 * @param {!Array<number>} value
 * @return {!proto.sensen.finance.IrrRequest} returns this
 */
proto.sensen.finance.IrrRequest.prototype.setValuesList = function(value) {
  return jspb.Message.setField(this, 1, value || []);
};


/**
 * @param {number} value
 * @param {number=} opt_index
 * @return {!proto.sensen.finance.IrrRequest} returns this
 */
proto.sensen.finance.IrrRequest.prototype.addValues = function(value, opt_index) {
  return jspb.Message.addToRepeatedField(this, 1, value, opt_index);
};


/**
 * Clears the list making it empty but non-null.
 * @return {!proto.sensen.finance.IrrRequest} returns this
 */
proto.sensen.finance.IrrRequest.prototype.clearValuesList = function() {
  return this.setValuesList([]);
};


/**
 * optional double guess = 2;
 * @return {number}
 */
proto.sensen.finance.IrrRequest.prototype.getGuess = function() {
  return /** @type {number} */ (jspb.Message.getFloatingPointFieldWithDefault(this, 2, 0.0));
};


/**
 * @param {number} value
 * @return {!proto.sensen.finance.IrrRequest} returns this
 */
proto.sensen.finance.IrrRequest.prototype.setGuess = function(value) {
  return jspb.Message.setProto3FloatField(this, 2, value);
};



/**
 * List of repeated fields within this message type.
 * @private {!Array<number>}
 * @const
 */
proto.sensen.finance.DatedCashFlowRequest.repeatedFields_ = [2,3];



if (jspb.Message.GENERATE_TO_OBJECT) {
/**
 * Creates an object representation of this proto.
 * Field names that are reserved in JavaScript and will be renamed to pb_name.
 * Optional fields that are not set will be set to undefined.
 * To access a reserved field use, foo.pb_<name>, eg, foo.pb_default.
 * For the list of reserved names please see:
 *     net/proto2/compiler/js/internal/generator.cc#kKeyword.
 * @param {boolean=} opt_includeInstance Deprecated. whether to include the
 *     JSPB instance for transitional soy proto support:
 *     http://goto/soy-param-migration
 * @return {!Object}
 */
proto.sensen.finance.DatedCashFlowRequest.prototype.toObject = function(opt_includeInstance) {
  return proto.sensen.finance.DatedCashFlowRequest.toObject(opt_includeInstance, this);
};


/**
 * Static version of the {@see toObject} method.
 * @param {boolean|undefined} includeInstance Deprecated. Whether to include
 *     the JSPB instance for transitional soy proto support:
 *     http://goto/soy-param-migration
 * @param {!proto.sensen.finance.DatedCashFlowRequest} msg The msg instance to transform.
 * @return {!Object}
 * @suppress {unusedLocalVariables} f is only used for nested messages
 */
proto.sensen.finance.DatedCashFlowRequest.toObject = function(includeInstance, msg) {
  var f, obj = {
    rate: jspb.Message.getFloatingPointFieldWithDefault(msg, 1, 0.0),
    valuesList: (f = jspb.Message.getRepeatedFloatingPointField(msg, 2)) == null ? undefined : f,
    datesList: (f = jspb.Message.getRepeatedFloatingPointField(msg, 3)) == null ? undefined : f,
    guess: jspb.Message.getFloatingPointFieldWithDefault(msg, 4, 0.0)
  };

  if (includeInstance) {
    obj.$jspbMessageInstance = msg;
  }
  return obj;
};
}


/**
 * Deserializes binary data (in protobuf wire format).
 * @param {jspb.ByteSource} bytes The bytes to deserialize.
 * @return {!proto.sensen.finance.DatedCashFlowRequest}
 */
proto.sensen.finance.DatedCashFlowRequest.deserializeBinary = function(bytes) {
  var reader = new jspb.BinaryReader(bytes);
  var msg = new proto.sensen.finance.DatedCashFlowRequest;
  return proto.sensen.finance.DatedCashFlowRequest.deserializeBinaryFromReader(msg, reader);
};


/**
 * Deserializes binary data (in protobuf wire format) from the
 * given reader into the given message object.
 * @param {!proto.sensen.finance.DatedCashFlowRequest} msg The message object to deserialize into.
 * @param {!jspb.BinaryReader} reader The BinaryReader to use.
 * @return {!proto.sensen.finance.DatedCashFlowRequest}
 */
proto.sensen.finance.DatedCashFlowRequest.deserializeBinaryFromReader = function(msg, reader) {
  while (reader.nextField()) {
    if (reader.isEndGroup()) {
      break;
    }
    var field = reader.getFieldNumber();
    switch (field) {
    case 1:
      var value = /** @type {number} */ (reader.readDouble());
      msg.setRate(value);
      break;
    case 2:
      var values = /** @type {!Array<number>} */ (reader.isDelimited() ? reader.readPackedDouble() : [reader.readDouble()]);
      for (var i = 0; i < values.length; i++) {
        msg.addValues(values[i]);
      }
      break;
    case 3:
      var values = /** @type {!Array<number>} */ (reader.isDelimited() ? reader.readPackedDouble() : [reader.readDouble()]);
      for (var i = 0; i < values.length; i++) {
        msg.addDates(values[i]);
      }
      break;
    case 4:
      var value = /** @type {number} */ (reader.readDouble());
      msg.setGuess(value);
      break;
    default:
      reader.skipField();
      break;
    }
  }
  return msg;
};


/**
 * Serializes the message to binary data (in protobuf wire format).
 * @return {!Uint8Array}
 */
proto.sensen.finance.DatedCashFlowRequest.prototype.serializeBinary = function() {
  var writer = new jspb.BinaryWriter();
  proto.sensen.finance.DatedCashFlowRequest.serializeBinaryToWriter(this, writer);
  return writer.getResultBuffer();
};


/**
 * Serializes the given message to binary data (in protobuf wire
 * format), writing to the given BinaryWriter.
 * @param {!proto.sensen.finance.DatedCashFlowRequest} message
 * @param {!jspb.BinaryWriter} writer
 * @suppress {unusedLocalVariables} f is only used for nested messages
 */
proto.sensen.finance.DatedCashFlowRequest.serializeBinaryToWriter = function(message, writer) {
  var f = undefined;
  f = message.getRate();
  if (f !== 0.0) {
    writer.writeDouble(
      1,
      f
    );
  }
  f = message.getValuesList();
  if (f.length > 0) {
    writer.writePackedDouble(
      2,
      f
    );
  }
  f = message.getDatesList();
  if (f.length > 0) {
    writer.writePackedDouble(
      3,
      f
    );
  }
  f = message.getGuess();
  if (f !== 0.0) {
    writer.writeDouble(
      4,
      f
    );
  }
};


/**
 * optional double rate = 1;
 * @return {number}
 */
proto.sensen.finance.DatedCashFlowRequest.prototype.getRate = function() {
  return /** @type {number} */ (jspb.Message.getFloatingPointFieldWithDefault(this, 1, 0.0));
};


/**
 * @param {number} value
 * @return {!proto.sensen.finance.DatedCashFlowRequest} returns this
 */
proto.sensen.finance.DatedCashFlowRequest.prototype.setRate = function(value) {
  return jspb.Message.setProto3FloatField(this, 1, value);
};


/**
 * repeated double values = 2;
 * @return {!Array<number>}
 */
proto.sensen.finance.DatedCashFlowRequest.prototype.getValuesList = function() {
  return /** @type {!Array<number>} */ (jspb.Message.getRepeatedFloatingPointField(this, 2));
};


/**
 * @param {!Array<number>} value
 * @return {!proto.sensen.finance.DatedCashFlowRequest} returns this
 */
proto.sensen.finance.DatedCashFlowRequest.prototype.setValuesList = function(value) {
  return jspb.Message.setField(this, 2, value || []);
};


/**
 * @param {number} value
 * @param {number=} opt_index
 * @return {!proto.sensen.finance.DatedCashFlowRequest} returns this
 */
proto.sensen.finance.DatedCashFlowRequest.prototype.addValues = function(value, opt_index) {
  return jspb.Message.addToRepeatedField(this, 2, value, opt_index);
};


/**
 * Clears the list making it empty but non-null.
 * @return {!proto.sensen.finance.DatedCashFlowRequest} returns this
 */
proto.sensen.finance.DatedCashFlowRequest.prototype.clearValuesList = function() {
  return this.setValuesList([]);
};


/**
 * repeated double dates = 3;
 * @return {!Array<number>}
 */
proto.sensen.finance.DatedCashFlowRequest.prototype.getDatesList = function() {
  return /** @type {!Array<number>} */ (jspb.Message.getRepeatedFloatingPointField(this, 3));
};


/**
 * @param {!Array<number>} value
 * @return {!proto.sensen.finance.DatedCashFlowRequest} returns this
 */
proto.sensen.finance.DatedCashFlowRequest.prototype.setDatesList = function(value) {
  return jspb.Message.setField(this, 3, value || []);
};


/**
 * @param {number} value
 * @param {number=} opt_index
 * @return {!proto.sensen.finance.DatedCashFlowRequest} returns this
 */
proto.sensen.finance.DatedCashFlowRequest.prototype.addDates = function(value, opt_index) {
  return jspb.Message.addToRepeatedField(this, 3, value, opt_index);
};


/**
 * Clears the list making it empty but non-null.
 * @return {!proto.sensen.finance.DatedCashFlowRequest} returns this
 */
proto.sensen.finance.DatedCashFlowRequest.prototype.clearDatesList = function() {
  return this.setDatesList([]);
};


/**
 * optional double guess = 4;
 * @return {number}
 */
proto.sensen.finance.DatedCashFlowRequest.prototype.getGuess = function() {
  return /** @type {number} */ (jspb.Message.getFloatingPointFieldWithDefault(this, 4, 0.0));
};


/**
 * @param {number} value
 * @return {!proto.sensen.finance.DatedCashFlowRequest} returns this
 */
proto.sensen.finance.DatedCashFlowRequest.prototype.setGuess = function(value) {
  return jspb.Message.setProto3FloatField(this, 4, value);
};



/**
 * List of repeated fields within this message type.
 * @private {!Array<number>}
 * @const
 */
proto.sensen.finance.PaybackRequest.repeatedFields_ = [1];



if (jspb.Message.GENERATE_TO_OBJECT) {
/**
 * Creates an object representation of this proto.
 * Field names that are reserved in JavaScript and will be renamed to pb_name.
 * Optional fields that are not set will be set to undefined.
 * To access a reserved field use, foo.pb_<name>, eg, foo.pb_default.
 * For the list of reserved names please see:
 *     net/proto2/compiler/js/internal/generator.cc#kKeyword.
 * @param {boolean=} opt_includeInstance Deprecated. whether to include the
 *     JSPB instance for transitional soy proto support:
 *     http://goto/soy-param-migration
 * @return {!Object}
 */
proto.sensen.finance.PaybackRequest.prototype.toObject = function(opt_includeInstance) {
  return proto.sensen.finance.PaybackRequest.toObject(opt_includeInstance, this);
};


/**
 * Static version of the {@see toObject} method.
 * @param {boolean|undefined} includeInstance Deprecated. Whether to include
 *     the JSPB instance for transitional soy proto support:
 *     http://goto/soy-param-migration
 * @param {!proto.sensen.finance.PaybackRequest} msg The msg instance to transform.
 * @return {!Object}
 * @suppress {unusedLocalVariables} f is only used for nested messages
 */
proto.sensen.finance.PaybackRequest.toObject = function(includeInstance, msg) {
  var f, obj = {
    valuesList: (f = jspb.Message.getRepeatedFloatingPointField(msg, 1)) == null ? undefined : f,
    discounted: jspb.Message.getBooleanFieldWithDefault(msg, 2, false),
    rate: jspb.Message.getFloatingPointFieldWithDefault(msg, 3, 0.0)
  };

  if (includeInstance) {
    obj.$jspbMessageInstance = msg;
  }
  return obj;
};
}


/**
 * Deserializes binary data (in protobuf wire format).
 * @param {jspb.ByteSource} bytes The bytes to deserialize.
 * @return {!proto.sensen.finance.PaybackRequest}
 */
proto.sensen.finance.PaybackRequest.deserializeBinary = function(bytes) {
  var reader = new jspb.BinaryReader(bytes);
  var msg = new proto.sensen.finance.PaybackRequest;
  return proto.sensen.finance.PaybackRequest.deserializeBinaryFromReader(msg, reader);
};


/**
 * Deserializes binary data (in protobuf wire format) from the
 * given reader into the given message object.
 * @param {!proto.sensen.finance.PaybackRequest} msg The message object to deserialize into.
 * @param {!jspb.BinaryReader} reader The BinaryReader to use.
 * @return {!proto.sensen.finance.PaybackRequest}
 */
proto.sensen.finance.PaybackRequest.deserializeBinaryFromReader = function(msg, reader) {
  while (reader.nextField()) {
    if (reader.isEndGroup()) {
      break;
    }
    var field = reader.getFieldNumber();
    switch (field) {
    case 1:
      var values = /** @type {!Array<number>} */ (reader.isDelimited() ? reader.readPackedDouble() : [reader.readDouble()]);
      for (var i = 0; i < values.length; i++) {
        msg.addValues(values[i]);
      }
      break;
    case 2:
      var value = /** @type {boolean} */ (reader.readBool());
      msg.setDiscounted(value);
      break;
    case 3:
      var value = /** @type {number} */ (reader.readDouble());
      msg.setRate(value);
      break;
    default:
      reader.skipField();
      break;
    }
  }
  return msg;
};


/**
 * Serializes the message to binary data (in protobuf wire format).
 * @return {!Uint8Array}
 */
proto.sensen.finance.PaybackRequest.prototype.serializeBinary = function() {
  var writer = new jspb.BinaryWriter();
  proto.sensen.finance.PaybackRequest.serializeBinaryToWriter(this, writer);
  return writer.getResultBuffer();
};


/**
 * Serializes the given message to binary data (in protobuf wire
 * format), writing to the given BinaryWriter.
 * @param {!proto.sensen.finance.PaybackRequest} message
 * @param {!jspb.BinaryWriter} writer
 * @suppress {unusedLocalVariables} f is only used for nested messages
 */
proto.sensen.finance.PaybackRequest.serializeBinaryToWriter = function(message, writer) {
  var f = undefined;
  f = message.getValuesList();
  if (f.length > 0) {
    writer.writePackedDouble(
      1,
      f
    );
  }
  f = message.getDiscounted();
  if (f) {
    writer.writeBool(
      2,
      f
    );
  }
  f = message.getRate();
  if (f !== 0.0) {
    writer.writeDouble(
      3,
      f
    );
  }
};


/**
 * repeated double values = 1;
 * @return {!Array<number>}
 */
proto.sensen.finance.PaybackRequest.prototype.getValuesList = function() {
  return /** @type {!Array<number>} */ (jspb.Message.getRepeatedFloatingPointField(this, 1));
};


/**
 * @param {!Array<number>} value
 * @return {!proto.sensen.finance.PaybackRequest} returns this
 */
proto.sensen.finance.PaybackRequest.prototype.setValuesList = function(value) {
  return jspb.Message.setField(this, 1, value || []);
};


/**
 * @param {number} value
 * @param {number=} opt_index
 * @return {!proto.sensen.finance.PaybackRequest} returns this
 */
proto.sensen.finance.PaybackRequest.prototype.addValues = function(value, opt_index) {
  return jspb.Message.addToRepeatedField(this, 1, value, opt_index);
};


/**
 * Clears the list making it empty but non-null.
 * @return {!proto.sensen.finance.PaybackRequest} returns this
 */
proto.sensen.finance.PaybackRequest.prototype.clearValuesList = function() {
  return this.setValuesList([]);
};


/**
 * optional bool discounted = 2;
 * @return {boolean}
 */
proto.sensen.finance.PaybackRequest.prototype.getDiscounted = function() {
  return /** @type {boolean} */ (jspb.Message.getBooleanFieldWithDefault(this, 2, false));
};


/**
 * @param {boolean} value
 * @return {!proto.sensen.finance.PaybackRequest} returns this
 */
proto.sensen.finance.PaybackRequest.prototype.setDiscounted = function(value) {
  return jspb.Message.setProto3BooleanField(this, 2, value);
};


/**
 * optional double rate = 3;
 * @return {number}
 */
proto.sensen.finance.PaybackRequest.prototype.getRate = function() {
  return /** @type {number} */ (jspb.Message.getFloatingPointFieldWithDefault(this, 3, 0.0));
};


/**
 * @param {number} value
 * @return {!proto.sensen.finance.PaybackRequest} returns this
 */
proto.sensen.finance.PaybackRequest.prototype.setRate = function(value) {
  return jspb.Message.setProto3FloatField(this, 3, value);
};





if (jspb.Message.GENERATE_TO_OBJECT) {
/**
 * Creates an object representation of this proto.
 * Field names that are reserved in JavaScript and will be renamed to pb_name.
 * Optional fields that are not set will be set to undefined.
 * To access a reserved field use, foo.pb_<name>, eg, foo.pb_default.
 * For the list of reserved names please see:
 *     net/proto2/compiler/js/internal/generator.cc#kKeyword.
 * @param {boolean=} opt_includeInstance Deprecated. whether to include the
 *     JSPB instance for transitional soy proto support:
 *     http://goto/soy-param-migration
 * @return {!Object}
 */
proto.sensen.finance.CumulativeRequest.prototype.toObject = function(opt_includeInstance) {
  return proto.sensen.finance.CumulativeRequest.toObject(opt_includeInstance, this);
};


/**
 * Static version of the {@see toObject} method.
 * @param {boolean|undefined} includeInstance Deprecated. Whether to include
 *     the JSPB instance for transitional soy proto support:
 *     http://goto/soy-param-migration
 * @param {!proto.sensen.finance.CumulativeRequest} msg The msg instance to transform.
 * @return {!Object}
 * @suppress {unusedLocalVariables} f is only used for nested messages
 */
proto.sensen.finance.CumulativeRequest.toObject = function(includeInstance, msg) {
  var f, obj = {
    component: jspb.Message.getFieldWithDefault(msg, 1, 0),
    rate: jspb.Message.getFloatingPointFieldWithDefault(msg, 2, 0.0),
    periods: jspb.Message.getFieldWithDefault(msg, 3, 0),
    presentValue: jspb.Message.getFloatingPointFieldWithDefault(msg, 4, 0.0),
    startPeriod: jspb.Message.getFieldWithDefault(msg, 5, 0),
    endPeriod: jspb.Message.getFieldWithDefault(msg, 6, 0),
    timing: jspb.Message.getFieldWithDefault(msg, 7, 0)
  };

  if (includeInstance) {
    obj.$jspbMessageInstance = msg;
  }
  return obj;
};
}


/**
 * Deserializes binary data (in protobuf wire format).
 * @param {jspb.ByteSource} bytes The bytes to deserialize.
 * @return {!proto.sensen.finance.CumulativeRequest}
 */
proto.sensen.finance.CumulativeRequest.deserializeBinary = function(bytes) {
  var reader = new jspb.BinaryReader(bytes);
  var msg = new proto.sensen.finance.CumulativeRequest;
  return proto.sensen.finance.CumulativeRequest.deserializeBinaryFromReader(msg, reader);
};


/**
 * Deserializes binary data (in protobuf wire format) from the
 * given reader into the given message object.
 * @param {!proto.sensen.finance.CumulativeRequest} msg The message object to deserialize into.
 * @param {!jspb.BinaryReader} reader The BinaryReader to use.
 * @return {!proto.sensen.finance.CumulativeRequest}
 */
proto.sensen.finance.CumulativeRequest.deserializeBinaryFromReader = function(msg, reader) {
  while (reader.nextField()) {
    if (reader.isEndGroup()) {
      break;
    }
    var field = reader.getFieldNumber();
    switch (field) {
    case 1:
      var value = /** @type {!proto.sensen.finance.CumulativeRequest.Component} */ (reader.readEnum());
      msg.setComponent(value);
      break;
    case 2:
      var value = /** @type {number} */ (reader.readDouble());
      msg.setRate(value);
      break;
    case 3:
      var value = /** @type {number} */ (reader.readInt32());
      msg.setPeriods(value);
      break;
    case 4:
      var value = /** @type {number} */ (reader.readDouble());
      msg.setPresentValue(value);
      break;
    case 5:
      var value = /** @type {number} */ (reader.readInt32());
      msg.setStartPeriod(value);
      break;
    case 6:
      var value = /** @type {number} */ (reader.readInt32());
      msg.setEndPeriod(value);
      break;
    case 7:
      var value = /** @type {!proto.sensen.finance.AnnuityTiming} */ (reader.readEnum());
      msg.setTiming(value);
      break;
    default:
      reader.skipField();
      break;
    }
  }
  return msg;
};


/**
 * Serializes the message to binary data (in protobuf wire format).
 * @return {!Uint8Array}
 */
proto.sensen.finance.CumulativeRequest.prototype.serializeBinary = function() {
  var writer = new jspb.BinaryWriter();
  proto.sensen.finance.CumulativeRequest.serializeBinaryToWriter(this, writer);
  return writer.getResultBuffer();
};


/**
 * Serializes the given message to binary data (in protobuf wire
 * format), writing to the given BinaryWriter.
 * @param {!proto.sensen.finance.CumulativeRequest} message
 * @param {!jspb.BinaryWriter} writer
 * @suppress {unusedLocalVariables} f is only used for nested messages
 */
proto.sensen.finance.CumulativeRequest.serializeBinaryToWriter = function(message, writer) {
  var f = undefined;
  f = message.getComponent();
  if (f !== 0.0) {
    writer.writeEnum(
      1,
      f
    );
  }
  f = message.getRate();
  if (f !== 0.0) {
    writer.writeDouble(
      2,
      f
    );
  }
  f = message.getPeriods();
  if (f !== 0) {
    writer.writeInt32(
      3,
      f
    );
  }
  f = message.getPresentValue();
  if (f !== 0.0) {
    writer.writeDouble(
      4,
      f
    );
  }
  f = message.getStartPeriod();
  if (f !== 0) {
    writer.writeInt32(
      5,
      f
    );
  }
  f = message.getEndPeriod();
  if (f !== 0) {
    writer.writeInt32(
      6,
      f
    );
  }
  f = message.getTiming();
  if (f !== 0.0) {
    writer.writeEnum(
      7,
      f
    );
  }
};


/**
 * @enum {number}
 */
proto.sensen.finance.CumulativeRequest.Component = {
  INTEREST: 0,
  PRINCIPAL: 1
};

/**
 * optional Component component = 1;
 * @return {!proto.sensen.finance.CumulativeRequest.Component}
 */
proto.sensen.finance.CumulativeRequest.prototype.getComponent = function() {
  return /** @type {!proto.sensen.finance.CumulativeRequest.Component} */ (jspb.Message.getFieldWithDefault(this, 1, 0));
};


/**
 * @param {!proto.sensen.finance.CumulativeRequest.Component} value
 * @return {!proto.sensen.finance.CumulativeRequest} returns this
 */
proto.sensen.finance.CumulativeRequest.prototype.setComponent = function(value) {
  return jspb.Message.setProto3EnumField(this, 1, value);
};


/**
 * optional double rate = 2;
 * @return {number}
 */
proto.sensen.finance.CumulativeRequest.prototype.getRate = function() {
  return /** @type {number} */ (jspb.Message.getFloatingPointFieldWithDefault(this, 2, 0.0));
};


/**
 * @param {number} value
 * @return {!proto.sensen.finance.CumulativeRequest} returns this
 */
proto.sensen.finance.CumulativeRequest.prototype.setRate = function(value) {
  return jspb.Message.setProto3FloatField(this, 2, value);
};


/**
 * optional int32 periods = 3;
 * @return {number}
 */
proto.sensen.finance.CumulativeRequest.prototype.getPeriods = function() {
  return /** @type {number} */ (jspb.Message.getFieldWithDefault(this, 3, 0));
};


/**
 * @param {number} value
 * @return {!proto.sensen.finance.CumulativeRequest} returns this
 */
proto.sensen.finance.CumulativeRequest.prototype.setPeriods = function(value) {
  return jspb.Message.setProto3IntField(this, 3, value);
};


/**
 * optional double present_value = 4;
 * @return {number}
 */
proto.sensen.finance.CumulativeRequest.prototype.getPresentValue = function() {
  return /** @type {number} */ (jspb.Message.getFloatingPointFieldWithDefault(this, 4, 0.0));
};


/**
 * @param {number} value
 * @return {!proto.sensen.finance.CumulativeRequest} returns this
 */
proto.sensen.finance.CumulativeRequest.prototype.setPresentValue = function(value) {
  return jspb.Message.setProto3FloatField(this, 4, value);
};


/**
 * optional int32 start_period = 5;
 * @return {number}
 */
proto.sensen.finance.CumulativeRequest.prototype.getStartPeriod = function() {
  return /** @type {number} */ (jspb.Message.getFieldWithDefault(this, 5, 0));
};


/**
 * @param {number} value
 * @return {!proto.sensen.finance.CumulativeRequest} returns this
 */
proto.sensen.finance.CumulativeRequest.prototype.setStartPeriod = function(value) {
  return jspb.Message.setProto3IntField(this, 5, value);
};


/**
 * optional int32 end_period = 6;
 * @return {number}
 */
proto.sensen.finance.CumulativeRequest.prototype.getEndPeriod = function() {
  return /** @type {number} */ (jspb.Message.getFieldWithDefault(this, 6, 0));
};


/**
 * @param {number} value
 * @return {!proto.sensen.finance.CumulativeRequest} returns this
 */
proto.sensen.finance.CumulativeRequest.prototype.setEndPeriod = function(value) {
  return jspb.Message.setProto3IntField(this, 6, value);
};


/**
 * optional AnnuityTiming timing = 7;
 * @return {!proto.sensen.finance.AnnuityTiming}
 */
proto.sensen.finance.CumulativeRequest.prototype.getTiming = function() {
  return /** @type {!proto.sensen.finance.AnnuityTiming} */ (jspb.Message.getFieldWithDefault(this, 7, 0));
};


/**
 * @param {!proto.sensen.finance.AnnuityTiming} value
 * @return {!proto.sensen.finance.CumulativeRequest} returns this
 */
proto.sensen.finance.CumulativeRequest.prototype.setTiming = function(value) {
  return jspb.Message.setProto3EnumField(this, 7, value);
};





if (jspb.Message.GENERATE_TO_OBJECT) {
/**
 * Creates an object representation of this proto.
 * Field names that are reserved in JavaScript and will be renamed to pb_name.
 * Optional fields that are not set will be set to undefined.
 * To access a reserved field use, foo.pb_<name>, eg, foo.pb_default.
 * For the list of reserved names please see:
 *     net/proto2/compiler/js/internal/generator.cc#kKeyword.
 * @param {boolean=} opt_includeInstance Deprecated. whether to include the
 *     JSPB instance for transitional soy proto support:
 *     http://goto/soy-param-migration
 * @return {!Object}
 */
proto.sensen.finance.DepreciationRequest.prototype.toObject = function(opt_includeInstance) {
  return proto.sensen.finance.DepreciationRequest.toObject(opt_includeInstance, this);
};


/**
 * Static version of the {@see toObject} method.
 * @param {boolean|undefined} includeInstance Deprecated. Whether to include
 *     the JSPB instance for transitional soy proto support:
 *     http://goto/soy-param-migration
 * @param {!proto.sensen.finance.DepreciationRequest} msg The msg instance to transform.
 * @return {!Object}
 * @suppress {unusedLocalVariables} f is only used for nested messages
 */
proto.sensen.finance.DepreciationRequest.toObject = function(includeInstance, msg) {
  var f, obj = {
    method: jspb.Message.getFieldWithDefault(msg, 1, 0),
    cost: jspb.Message.getFloatingPointFieldWithDefault(msg, 2, 0.0),
    salvage: jspb.Message.getFloatingPointFieldWithDefault(msg, 3, 0.0),
    life: jspb.Message.getFloatingPointFieldWithDefault(msg, 4, 0.0),
    period: jspb.Message.getFloatingPointFieldWithDefault(msg, 5, 0.0),
    factor: jspb.Message.getFloatingPointFieldWithDefault(msg, 6, 0.0),
    recoveryPeriod: jspb.Message.getFieldWithDefault(msg, 7, 0),
    year: jspb.Message.getFieldWithDefault(msg, 8, 0)
  };

  if (includeInstance) {
    obj.$jspbMessageInstance = msg;
  }
  return obj;
};
}


/**
 * Deserializes binary data (in protobuf wire format).
 * @param {jspb.ByteSource} bytes The bytes to deserialize.
 * @return {!proto.sensen.finance.DepreciationRequest}
 */
proto.sensen.finance.DepreciationRequest.deserializeBinary = function(bytes) {
  var reader = new jspb.BinaryReader(bytes);
  var msg = new proto.sensen.finance.DepreciationRequest;
  return proto.sensen.finance.DepreciationRequest.deserializeBinaryFromReader(msg, reader);
};


/**
 * Deserializes binary data (in protobuf wire format) from the
 * given reader into the given message object.
 * @param {!proto.sensen.finance.DepreciationRequest} msg The message object to deserialize into.
 * @param {!jspb.BinaryReader} reader The BinaryReader to use.
 * @return {!proto.sensen.finance.DepreciationRequest}
 */
proto.sensen.finance.DepreciationRequest.deserializeBinaryFromReader = function(msg, reader) {
  while (reader.nextField()) {
    if (reader.isEndGroup()) {
      break;
    }
    var field = reader.getFieldNumber();
    switch (field) {
    case 1:
      var value = /** @type {!proto.sensen.finance.DepreciationRequest.Method} */ (reader.readEnum());
      msg.setMethod(value);
      break;
    case 2:
      var value = /** @type {number} */ (reader.readDouble());
      msg.setCost(value);
      break;
    case 3:
      var value = /** @type {number} */ (reader.readDouble());
      msg.setSalvage(value);
      break;
    case 4:
      var value = /** @type {number} */ (reader.readDouble());
      msg.setLife(value);
      break;
    case 5:
      var value = /** @type {number} */ (reader.readDouble());
      msg.setPeriod(value);
      break;
    case 6:
      var value = /** @type {number} */ (reader.readDouble());
      msg.setFactor(value);
      break;
    case 7:
      var value = /** @type {number} */ (reader.readInt32());
      msg.setRecoveryPeriod(value);
      break;
    case 8:
      var value = /** @type {number} */ (reader.readInt32());
      msg.setYear(value);
      break;
    default:
      reader.skipField();
      break;
    }
  }
  return msg;
};


/**
 * Serializes the message to binary data (in protobuf wire format).
 * @return {!Uint8Array}
 */
proto.sensen.finance.DepreciationRequest.prototype.serializeBinary = function() {
  var writer = new jspb.BinaryWriter();
  proto.sensen.finance.DepreciationRequest.serializeBinaryToWriter(this, writer);
  return writer.getResultBuffer();
};


/**
 * Serializes the given message to binary data (in protobuf wire
 * format), writing to the given BinaryWriter.
 * @param {!proto.sensen.finance.DepreciationRequest} message
 * @param {!jspb.BinaryWriter} writer
 * @suppress {unusedLocalVariables} f is only used for nested messages
 */
proto.sensen.finance.DepreciationRequest.serializeBinaryToWriter = function(message, writer) {
  var f = undefined;
  f = message.getMethod();
  if (f !== 0.0) {
    writer.writeEnum(
      1,
      f
    );
  }
  f = message.getCost();
  if (f !== 0.0) {
    writer.writeDouble(
      2,
      f
    );
  }
  f = message.getSalvage();
  if (f !== 0.0) {
    writer.writeDouble(
      3,
      f
    );
  }
  f = message.getLife();
  if (f !== 0.0) {
    writer.writeDouble(
      4,
      f
    );
  }
  f = message.getPeriod();
  if (f !== 0.0) {
    writer.writeDouble(
      5,
      f
    );
  }
  f = message.getFactor();
  if (f !== 0.0) {
    writer.writeDouble(
      6,
      f
    );
  }
  f = message.getRecoveryPeriod();
  if (f !== 0) {
    writer.writeInt32(
      7,
      f
    );
  }
  f = message.getYear();
  if (f !== 0) {
    writer.writeInt32(
      8,
      f
    );
  }
};


/**
 * @enum {number}
 */
proto.sensen.finance.DepreciationRequest.Method = {
  STRAIGHT_LINE: 0,
  SUM_OF_YEARS_DIGITS: 1,
  DECLINING_BALANCE: 2,
  MACRS: 3
};

/**
 * optional Method method = 1;
 * @return {!proto.sensen.finance.DepreciationRequest.Method}
 */
proto.sensen.finance.DepreciationRequest.prototype.getMethod = function() {
  return /** @type {!proto.sensen.finance.DepreciationRequest.Method} */ (jspb.Message.getFieldWithDefault(this, 1, 0));
};


/**
 * @param {!proto.sensen.finance.DepreciationRequest.Method} value
 * @return {!proto.sensen.finance.DepreciationRequest} returns this
 */
proto.sensen.finance.DepreciationRequest.prototype.setMethod = function(value) {
  return jspb.Message.setProto3EnumField(this, 1, value);
};


/**
 * optional double cost = 2;
 * @return {number}
 */
proto.sensen.finance.DepreciationRequest.prototype.getCost = function() {
  return /** @type {number} */ (jspb.Message.getFloatingPointFieldWithDefault(this, 2, 0.0));
};


/**
 * @param {number} value
 * @return {!proto.sensen.finance.DepreciationRequest} returns this
 */
proto.sensen.finance.DepreciationRequest.prototype.setCost = function(value) {
  return jspb.Message.setProto3FloatField(this, 2, value);
};


/**
 * optional double salvage = 3;
 * @return {number}
 */
proto.sensen.finance.DepreciationRequest.prototype.getSalvage = function() {
  return /** @type {number} */ (jspb.Message.getFloatingPointFieldWithDefault(this, 3, 0.0));
};


/**
 * @param {number} value
 * @return {!proto.sensen.finance.DepreciationRequest} returns this
 */
proto.sensen.finance.DepreciationRequest.prototype.setSalvage = function(value) {
  return jspb.Message.setProto3FloatField(this, 3, value);
};


/**
 * optional double life = 4;
 * @return {number}
 */
proto.sensen.finance.DepreciationRequest.prototype.getLife = function() {
  return /** @type {number} */ (jspb.Message.getFloatingPointFieldWithDefault(this, 4, 0.0));
};


/**
 * @param {number} value
 * @return {!proto.sensen.finance.DepreciationRequest} returns this
 */
proto.sensen.finance.DepreciationRequest.prototype.setLife = function(value) {
  return jspb.Message.setProto3FloatField(this, 4, value);
};


/**
 * optional double period = 5;
 * @return {number}
 */
proto.sensen.finance.DepreciationRequest.prototype.getPeriod = function() {
  return /** @type {number} */ (jspb.Message.getFloatingPointFieldWithDefault(this, 5, 0.0));
};


/**
 * @param {number} value
 * @return {!proto.sensen.finance.DepreciationRequest} returns this
 */
proto.sensen.finance.DepreciationRequest.prototype.setPeriod = function(value) {
  return jspb.Message.setProto3FloatField(this, 5, value);
};


/**
 * optional double factor = 6;
 * @return {number}
 */
proto.sensen.finance.DepreciationRequest.prototype.getFactor = function() {
  return /** @type {number} */ (jspb.Message.getFloatingPointFieldWithDefault(this, 6, 0.0));
};


/**
 * @param {number} value
 * @return {!proto.sensen.finance.DepreciationRequest} returns this
 */
proto.sensen.finance.DepreciationRequest.prototype.setFactor = function(value) {
  return jspb.Message.setProto3FloatField(this, 6, value);
};


/**
 * optional int32 recovery_period = 7;
 * @return {number}
 */
proto.sensen.finance.DepreciationRequest.prototype.getRecoveryPeriod = function() {
  return /** @type {number} */ (jspb.Message.getFieldWithDefault(this, 7, 0));
};


/**
 * @param {number} value
 * @return {!proto.sensen.finance.DepreciationRequest} returns this
 */
proto.sensen.finance.DepreciationRequest.prototype.setRecoveryPeriod = function(value) {
  return jspb.Message.setProto3IntField(this, 7, value);
};


/**
 * optional int32 year = 8;
 * @return {number}
 */
proto.sensen.finance.DepreciationRequest.prototype.getYear = function() {
  return /** @type {number} */ (jspb.Message.getFieldWithDefault(this, 8, 0));
};


/**
 * @param {number} value
 * @return {!proto.sensen.finance.DepreciationRequest} returns this
 */
proto.sensen.finance.DepreciationRequest.prototype.setYear = function(value) {
  return jspb.Message.setProto3IntField(this, 8, value);
};



/**
 * Oneof group definitions for this message. Each group defines the field
 * numbers belonging to that group. When of these fields' value is set, all
 * other fields in the group are cleared. During deserialization, if multiple
 * fields are encountered for a group, only the last value seen will be kept.
 * @private {!Array<!Array<number>>}
 * @const
 */
proto.sensen.finance.BondRequest.oneofGroups_ = [[6,7]];

/**
 * @enum {number}
 */
proto.sensen.finance.BondRequest.KnownCase = {
  KNOWN_NOT_SET: 0,
  YIELD: 6,
  PRICE: 7
};

/**
 * @return {proto.sensen.finance.BondRequest.KnownCase}
 */
proto.sensen.finance.BondRequest.prototype.getKnownCase = function() {
  return /** @type {proto.sensen.finance.BondRequest.KnownCase} */(jspb.Message.computeOneofCase(this, proto.sensen.finance.BondRequest.oneofGroups_[0]));
};



if (jspb.Message.GENERATE_TO_OBJECT) {
/**
 * Creates an object representation of this proto.
 * Field names that are reserved in JavaScript and will be renamed to pb_name.
 * Optional fields that are not set will be set to undefined.
 * To access a reserved field use, foo.pb_<name>, eg, foo.pb_default.
 * For the list of reserved names please see:
 *     net/proto2/compiler/js/internal/generator.cc#kKeyword.
 * @param {boolean=} opt_includeInstance Deprecated. whether to include the
 *     JSPB instance for transitional soy proto support:
 *     http://goto/soy-param-migration
 * @return {!Object}
 */
proto.sensen.finance.BondRequest.prototype.toObject = function(opt_includeInstance) {
  return proto.sensen.finance.BondRequest.toObject(opt_includeInstance, this);
};


/**
 * Static version of the {@see toObject} method.
 * @param {boolean|undefined} includeInstance Deprecated. Whether to include
 *     the JSPB instance for transitional soy proto support:
 *     http://goto/soy-param-migration
 * @param {!proto.sensen.finance.BondRequest} msg The msg instance to transform.
 * @return {!Object}
 * @suppress {unusedLocalVariables} f is only used for nested messages
 */
proto.sensen.finance.BondRequest.toObject = function(includeInstance, msg) {
  var f, obj = {
    par: jspb.Message.getFloatingPointFieldWithDefault(msg, 1, 0.0),
    couponRate: jspb.Message.getFloatingPointFieldWithDefault(msg, 2, 0.0),
    frequency: jspb.Message.getFieldWithDefault(msg, 3, 0),
    yearsToMaturity: jspb.Message.getFloatingPointFieldWithDefault(msg, 4, 0.0),
    redemption: jspb.Message.getFloatingPointFieldWithDefault(msg, 5, 0.0),
    yield: jspb.Message.getFloatingPointFieldWithDefault(msg, 6, 0.0),
    price: jspb.Message.getFloatingPointFieldWithDefault(msg, 7, 0.0),
    yieldGuess: jspb.Message.getFloatingPointFieldWithDefault(msg, 8, 0.0)
  };

  if (includeInstance) {
    obj.$jspbMessageInstance = msg;
  }
  return obj;
};
}


/**
 * Deserializes binary data (in protobuf wire format).
 * @param {jspb.ByteSource} bytes The bytes to deserialize.
 * @return {!proto.sensen.finance.BondRequest}
 */
proto.sensen.finance.BondRequest.deserializeBinary = function(bytes) {
  var reader = new jspb.BinaryReader(bytes);
  var msg = new proto.sensen.finance.BondRequest;
  return proto.sensen.finance.BondRequest.deserializeBinaryFromReader(msg, reader);
};


/**
 * Deserializes binary data (in protobuf wire format) from the
 * given reader into the given message object.
 * @param {!proto.sensen.finance.BondRequest} msg The message object to deserialize into.
 * @param {!jspb.BinaryReader} reader The BinaryReader to use.
 * @return {!proto.sensen.finance.BondRequest}
 */
proto.sensen.finance.BondRequest.deserializeBinaryFromReader = function(msg, reader) {
  while (reader.nextField()) {
    if (reader.isEndGroup()) {
      break;
    }
    var field = reader.getFieldNumber();
    switch (field) {
    case 1:
      var value = /** @type {number} */ (reader.readDouble());
      msg.setPar(value);
      break;
    case 2:
      var value = /** @type {number} */ (reader.readDouble());
      msg.setCouponRate(value);
      break;
    case 3:
      var value = /** @type {number} */ (reader.readInt32());
      msg.setFrequency(value);
      break;
    case 4:
      var value = /** @type {number} */ (reader.readDouble());
      msg.setYearsToMaturity(value);
      break;
    case 5:
      var value = /** @type {number} */ (reader.readDouble());
      msg.setRedemption(value);
      break;
    case 6:
      var value = /** @type {number} */ (reader.readDouble());
      msg.setYield(value);
      break;
    case 7:
      var value = /** @type {number} */ (reader.readDouble());
      msg.setPrice(value);
      break;
    case 8:
      var value = /** @type {number} */ (reader.readDouble());
      msg.setYieldGuess(value);
      break;
    default:
      reader.skipField();
      break;
    }
  }
  return msg;
};


/**
 * Serializes the message to binary data (in protobuf wire format).
 * @return {!Uint8Array}
 */
proto.sensen.finance.BondRequest.prototype.serializeBinary = function() {
  var writer = new jspb.BinaryWriter();
  proto.sensen.finance.BondRequest.serializeBinaryToWriter(this, writer);
  return writer.getResultBuffer();
};


/**
 * Serializes the given message to binary data (in protobuf wire
 * format), writing to the given BinaryWriter.
 * @param {!proto.sensen.finance.BondRequest} message
 * @param {!jspb.BinaryWriter} writer
 * @suppress {unusedLocalVariables} f is only used for nested messages
 */
proto.sensen.finance.BondRequest.serializeBinaryToWriter = function(message, writer) {
  var f = undefined;
  f = message.getPar();
  if (f !== 0.0) {
    writer.writeDouble(
      1,
      f
    );
  }
  f = message.getCouponRate();
  if (f !== 0.0) {
    writer.writeDouble(
      2,
      f
    );
  }
  f = message.getFrequency();
  if (f !== 0) {
    writer.writeInt32(
      3,
      f
    );
  }
  f = message.getYearsToMaturity();
  if (f !== 0.0) {
    writer.writeDouble(
      4,
      f
    );
  }
  f = message.getRedemption();
  if (f !== 0.0) {
    writer.writeDouble(
      5,
      f
    );
  }
  f = /** @type {number} */ (jspb.Message.getField(message, 6));
  if (f != null) {
    writer.writeDouble(
      6,
      f
    );
  }
  f = /** @type {number} */ (jspb.Message.getField(message, 7));
  if (f != null) {
    writer.writeDouble(
      7,
      f
    );
  }
  f = message.getYieldGuess();
  if (f !== 0.0) {
    writer.writeDouble(
      8,
      f
    );
  }
};


/**
 * optional double par = 1;
 * @return {number}
 */
proto.sensen.finance.BondRequest.prototype.getPar = function() {
  return /** @type {number} */ (jspb.Message.getFloatingPointFieldWithDefault(this, 1, 0.0));
};


/**
 * @param {number} value
 * @return {!proto.sensen.finance.BondRequest} returns this
 */
proto.sensen.finance.BondRequest.prototype.setPar = function(value) {
  return jspb.Message.setProto3FloatField(this, 1, value);
};


/**
 * optional double coupon_rate = 2;
 * @return {number}
 */
proto.sensen.finance.BondRequest.prototype.getCouponRate = function() {
  return /** @type {number} */ (jspb.Message.getFloatingPointFieldWithDefault(this, 2, 0.0));
};


/**
 * @param {number} value
 * @return {!proto.sensen.finance.BondRequest} returns this
 */
proto.sensen.finance.BondRequest.prototype.setCouponRate = function(value) {
  return jspb.Message.setProto3FloatField(this, 2, value);
};


/**
 * optional int32 frequency = 3;
 * @return {number}
 */
proto.sensen.finance.BondRequest.prototype.getFrequency = function() {
  return /** @type {number} */ (jspb.Message.getFieldWithDefault(this, 3, 0));
};


/**
 * @param {number} value
 * @return {!proto.sensen.finance.BondRequest} returns this
 */
proto.sensen.finance.BondRequest.prototype.setFrequency = function(value) {
  return jspb.Message.setProto3IntField(this, 3, value);
};


/**
 * optional double years_to_maturity = 4;
 * @return {number}
 */
proto.sensen.finance.BondRequest.prototype.getYearsToMaturity = function() {
  return /** @type {number} */ (jspb.Message.getFloatingPointFieldWithDefault(this, 4, 0.0));
};


/**
 * @param {number} value
 * @return {!proto.sensen.finance.BondRequest} returns this
 */
proto.sensen.finance.BondRequest.prototype.setYearsToMaturity = function(value) {
  return jspb.Message.setProto3FloatField(this, 4, value);
};


/**
 * optional double redemption = 5;
 * @return {number}
 */
proto.sensen.finance.BondRequest.prototype.getRedemption = function() {
  return /** @type {number} */ (jspb.Message.getFloatingPointFieldWithDefault(this, 5, 0.0));
};


/**
 * @param {number} value
 * @return {!proto.sensen.finance.BondRequest} returns this
 */
proto.sensen.finance.BondRequest.prototype.setRedemption = function(value) {
  return jspb.Message.setProto3FloatField(this, 5, value);
};


/**
 * optional double yield = 6;
 * @return {number}
 */
proto.sensen.finance.BondRequest.prototype.getYield = function() {
  return /** @type {number} */ (jspb.Message.getFloatingPointFieldWithDefault(this, 6, 0.0));
};


/**
 * @param {number} value
 * @return {!proto.sensen.finance.BondRequest} returns this
 */
proto.sensen.finance.BondRequest.prototype.setYield = function(value) {
  return jspb.Message.setOneofField(this, 6, proto.sensen.finance.BondRequest.oneofGroups_[0], value);
};


/**
 * Clears the field making it undefined.
 * @return {!proto.sensen.finance.BondRequest} returns this
 */
proto.sensen.finance.BondRequest.prototype.clearYield = function() {
  return jspb.Message.setOneofField(this, 6, proto.sensen.finance.BondRequest.oneofGroups_[0], undefined);
};


/**
 * Returns whether this field is set.
 * @return {boolean}
 */
proto.sensen.finance.BondRequest.prototype.hasYield = function() {
  return jspb.Message.getField(this, 6) != null;
};


/**
 * optional double price = 7;
 * @return {number}
 */
proto.sensen.finance.BondRequest.prototype.getPrice = function() {
  return /** @type {number} */ (jspb.Message.getFloatingPointFieldWithDefault(this, 7, 0.0));
};


/**
 * @param {number} value
 * @return {!proto.sensen.finance.BondRequest} returns this
 */
proto.sensen.finance.BondRequest.prototype.setPrice = function(value) {
  return jspb.Message.setOneofField(this, 7, proto.sensen.finance.BondRequest.oneofGroups_[0], value);
};


/**
 * Clears the field making it undefined.
 * @return {!proto.sensen.finance.BondRequest} returns this
 */
proto.sensen.finance.BondRequest.prototype.clearPrice = function() {
  return jspb.Message.setOneofField(this, 7, proto.sensen.finance.BondRequest.oneofGroups_[0], undefined);
};


/**
 * Returns whether this field is set.
 * @return {boolean}
 */
proto.sensen.finance.BondRequest.prototype.hasPrice = function() {
  return jspb.Message.getField(this, 7) != null;
};


/**
 * optional double yield_guess = 8;
 * @return {number}
 */
proto.sensen.finance.BondRequest.prototype.getYieldGuess = function() {
  return /** @type {number} */ (jspb.Message.getFloatingPointFieldWithDefault(this, 8, 0.0));
};


/**
 * @param {number} value
 * @return {!proto.sensen.finance.BondRequest} returns this
 */
proto.sensen.finance.BondRequest.prototype.setYieldGuess = function(value) {
  return jspb.Message.setProto3FloatField(this, 8, value);
};





if (jspb.Message.GENERATE_TO_OBJECT) {
/**
 * Creates an object representation of this proto.
 * Field names that are reserved in JavaScript and will be renamed to pb_name.
 * Optional fields that are not set will be set to undefined.
 * To access a reserved field use, foo.pb_<name>, eg, foo.pb_default.
 * For the list of reserved names please see:
 *     net/proto2/compiler/js/internal/generator.cc#kKeyword.
 * @param {boolean=} opt_includeInstance Deprecated. whether to include the
 *     JSPB instance for transitional soy proto support:
 *     http://goto/soy-param-migration
 * @return {!Object}
 */
proto.sensen.finance.BondResponse.prototype.toObject = function(opt_includeInstance) {
  return proto.sensen.finance.BondResponse.toObject(opt_includeInstance, this);
};


/**
 * Static version of the {@see toObject} method.
 * @param {boolean|undefined} includeInstance Deprecated. Whether to include
 *     the JSPB instance for transitional soy proto support:
 *     http://goto/soy-param-migration
 * @param {!proto.sensen.finance.BondResponse} msg The msg instance to transform.
 * @return {!Object}
 * @suppress {unusedLocalVariables} f is only used for nested messages
 */
proto.sensen.finance.BondResponse.toObject = function(includeInstance, msg) {
  var f, obj = {
    price: jspb.Message.getFloatingPointFieldWithDefault(msg, 1, 0.0),
    yield: jspb.Message.getFloatingPointFieldWithDefault(msg, 2, 0.0),
    macaulayDuration: jspb.Message.getFloatingPointFieldWithDefault(msg, 3, 0.0),
    convexity: jspb.Message.getFloatingPointFieldWithDefault(msg, 4, 0.0)
  };

  if (includeInstance) {
    obj.$jspbMessageInstance = msg;
  }
  return obj;
};
}


/**
 * Deserializes binary data (in protobuf wire format).
 * @param {jspb.ByteSource} bytes The bytes to deserialize.
 * @return {!proto.sensen.finance.BondResponse}
 */
proto.sensen.finance.BondResponse.deserializeBinary = function(bytes) {
  var reader = new jspb.BinaryReader(bytes);
  var msg = new proto.sensen.finance.BondResponse;
  return proto.sensen.finance.BondResponse.deserializeBinaryFromReader(msg, reader);
};


/**
 * Deserializes binary data (in protobuf wire format) from the
 * given reader into the given message object.
 * @param {!proto.sensen.finance.BondResponse} msg The message object to deserialize into.
 * @param {!jspb.BinaryReader} reader The BinaryReader to use.
 * @return {!proto.sensen.finance.BondResponse}
 */
proto.sensen.finance.BondResponse.deserializeBinaryFromReader = function(msg, reader) {
  while (reader.nextField()) {
    if (reader.isEndGroup()) {
      break;
    }
    var field = reader.getFieldNumber();
    switch (field) {
    case 1:
      var value = /** @type {number} */ (reader.readDouble());
      msg.setPrice(value);
      break;
    case 2:
      var value = /** @type {number} */ (reader.readDouble());
      msg.setYield(value);
      break;
    case 3:
      var value = /** @type {number} */ (reader.readDouble());
      msg.setMacaulayDuration(value);
      break;
    case 4:
      var value = /** @type {number} */ (reader.readDouble());
      msg.setConvexity(value);
      break;
    default:
      reader.skipField();
      break;
    }
  }
  return msg;
};


/**
 * Serializes the message to binary data (in protobuf wire format).
 * @return {!Uint8Array}
 */
proto.sensen.finance.BondResponse.prototype.serializeBinary = function() {
  var writer = new jspb.BinaryWriter();
  proto.sensen.finance.BondResponse.serializeBinaryToWriter(this, writer);
  return writer.getResultBuffer();
};


/**
 * Serializes the given message to binary data (in protobuf wire
 * format), writing to the given BinaryWriter.
 * @param {!proto.sensen.finance.BondResponse} message
 * @param {!jspb.BinaryWriter} writer
 * @suppress {unusedLocalVariables} f is only used for nested messages
 */
proto.sensen.finance.BondResponse.serializeBinaryToWriter = function(message, writer) {
  var f = undefined;
  f = message.getPrice();
  if (f !== 0.0) {
    writer.writeDouble(
      1,
      f
    );
  }
  f = message.getYield();
  if (f !== 0.0) {
    writer.writeDouble(
      2,
      f
    );
  }
  f = message.getMacaulayDuration();
  if (f !== 0.0) {
    writer.writeDouble(
      3,
      f
    );
  }
  f = message.getConvexity();
  if (f !== 0.0) {
    writer.writeDouble(
      4,
      f
    );
  }
};


/**
 * optional double price = 1;
 * @return {number}
 */
proto.sensen.finance.BondResponse.prototype.getPrice = function() {
  return /** @type {number} */ (jspb.Message.getFloatingPointFieldWithDefault(this, 1, 0.0));
};


/**
 * @param {number} value
 * @return {!proto.sensen.finance.BondResponse} returns this
 */
proto.sensen.finance.BondResponse.prototype.setPrice = function(value) {
  return jspb.Message.setProto3FloatField(this, 1, value);
};


/**
 * optional double yield = 2;
 * @return {number}
 */
proto.sensen.finance.BondResponse.prototype.getYield = function() {
  return /** @type {number} */ (jspb.Message.getFloatingPointFieldWithDefault(this, 2, 0.0));
};


/**
 * @param {number} value
 * @return {!proto.sensen.finance.BondResponse} returns this
 */
proto.sensen.finance.BondResponse.prototype.setYield = function(value) {
  return jspb.Message.setProto3FloatField(this, 2, value);
};


/**
 * optional double macaulay_duration = 3;
 * @return {number}
 */
proto.sensen.finance.BondResponse.prototype.getMacaulayDuration = function() {
  return /** @type {number} */ (jspb.Message.getFloatingPointFieldWithDefault(this, 3, 0.0));
};


/**
 * @param {number} value
 * @return {!proto.sensen.finance.BondResponse} returns this
 */
proto.sensen.finance.BondResponse.prototype.setMacaulayDuration = function(value) {
  return jspb.Message.setProto3FloatField(this, 3, value);
};


/**
 * optional double convexity = 4;
 * @return {number}
 */
proto.sensen.finance.BondResponse.prototype.getConvexity = function() {
  return /** @type {number} */ (jspb.Message.getFloatingPointFieldWithDefault(this, 4, 0.0));
};


/**
 * @param {number} value
 * @return {!proto.sensen.finance.BondResponse} returns this
 */
proto.sensen.finance.BondResponse.prototype.setConvexity = function(value) {
  return jspb.Message.setProto3FloatField(this, 4, value);
};



/**
 * Oneof group definitions for this message. Each group defines the field
 * numbers belonging to that group. When of these fields' value is set, all
 * other fields in the group are cleared. During deserialization, if multiple
 * fields are encountered for a group, only the last value seen will be kept.
 * @private {!Array<!Array<number>>}
 * @const
 */
proto.sensen.finance.TreasuryBillRequest.oneofGroups_ = [[3,4]];

/**
 * @enum {number}
 */
proto.sensen.finance.TreasuryBillRequest.KnownCase = {
  KNOWN_NOT_SET: 0,
  DISCOUNT_RATE: 3,
  PRICE: 4
};

/**
 * @return {proto.sensen.finance.TreasuryBillRequest.KnownCase}
 */
proto.sensen.finance.TreasuryBillRequest.prototype.getKnownCase = function() {
  return /** @type {proto.sensen.finance.TreasuryBillRequest.KnownCase} */(jspb.Message.computeOneofCase(this, proto.sensen.finance.TreasuryBillRequest.oneofGroups_[0]));
};



if (jspb.Message.GENERATE_TO_OBJECT) {
/**
 * Creates an object representation of this proto.
 * Field names that are reserved in JavaScript and will be renamed to pb_name.
 * Optional fields that are not set will be set to undefined.
 * To access a reserved field use, foo.pb_<name>, eg, foo.pb_default.
 * For the list of reserved names please see:
 *     net/proto2/compiler/js/internal/generator.cc#kKeyword.
 * @param {boolean=} opt_includeInstance Deprecated. whether to include the
 *     JSPB instance for transitional soy proto support:
 *     http://goto/soy-param-migration
 * @return {!Object}
 */
proto.sensen.finance.TreasuryBillRequest.prototype.toObject = function(opt_includeInstance) {
  return proto.sensen.finance.TreasuryBillRequest.toObject(opt_includeInstance, this);
};


/**
 * Static version of the {@see toObject} method.
 * @param {boolean|undefined} includeInstance Deprecated. Whether to include
 *     the JSPB instance for transitional soy proto support:
 *     http://goto/soy-param-migration
 * @param {!proto.sensen.finance.TreasuryBillRequest} msg The msg instance to transform.
 * @return {!Object}
 * @suppress {unusedLocalVariables} f is only used for nested messages
 */
proto.sensen.finance.TreasuryBillRequest.toObject = function(includeInstance, msg) {
  var f, obj = {
    faceValue: jspb.Message.getFloatingPointFieldWithDefault(msg, 1, 0.0),
    daysToMaturity: jspb.Message.getFieldWithDefault(msg, 2, 0),
    discountRate: jspb.Message.getFloatingPointFieldWithDefault(msg, 3, 0.0),
    price: jspb.Message.getFloatingPointFieldWithDefault(msg, 4, 0.0)
  };

  if (includeInstance) {
    obj.$jspbMessageInstance = msg;
  }
  return obj;
};
}


/**
 * Deserializes binary data (in protobuf wire format).
 * @param {jspb.ByteSource} bytes The bytes to deserialize.
 * @return {!proto.sensen.finance.TreasuryBillRequest}
 */
proto.sensen.finance.TreasuryBillRequest.deserializeBinary = function(bytes) {
  var reader = new jspb.BinaryReader(bytes);
  var msg = new proto.sensen.finance.TreasuryBillRequest;
  return proto.sensen.finance.TreasuryBillRequest.deserializeBinaryFromReader(msg, reader);
};


/**
 * Deserializes binary data (in protobuf wire format) from the
 * given reader into the given message object.
 * @param {!proto.sensen.finance.TreasuryBillRequest} msg The message object to deserialize into.
 * @param {!jspb.BinaryReader} reader The BinaryReader to use.
 * @return {!proto.sensen.finance.TreasuryBillRequest}
 */
proto.sensen.finance.TreasuryBillRequest.deserializeBinaryFromReader = function(msg, reader) {
  while (reader.nextField()) {
    if (reader.isEndGroup()) {
      break;
    }
    var field = reader.getFieldNumber();
    switch (field) {
    case 1:
      var value = /** @type {number} */ (reader.readDouble());
      msg.setFaceValue(value);
      break;
    case 2:
      var value = /** @type {number} */ (reader.readInt32());
      msg.setDaysToMaturity(value);
      break;
    case 3:
      var value = /** @type {number} */ (reader.readDouble());
      msg.setDiscountRate(value);
      break;
    case 4:
      var value = /** @type {number} */ (reader.readDouble());
      msg.setPrice(value);
      break;
    default:
      reader.skipField();
      break;
    }
  }
  return msg;
};


/**
 * Serializes the message to binary data (in protobuf wire format).
 * @return {!Uint8Array}
 */
proto.sensen.finance.TreasuryBillRequest.prototype.serializeBinary = function() {
  var writer = new jspb.BinaryWriter();
  proto.sensen.finance.TreasuryBillRequest.serializeBinaryToWriter(this, writer);
  return writer.getResultBuffer();
};


/**
 * Serializes the given message to binary data (in protobuf wire
 * format), writing to the given BinaryWriter.
 * @param {!proto.sensen.finance.TreasuryBillRequest} message
 * @param {!jspb.BinaryWriter} writer
 * @suppress {unusedLocalVariables} f is only used for nested messages
 */
proto.sensen.finance.TreasuryBillRequest.serializeBinaryToWriter = function(message, writer) {
  var f = undefined;
  f = message.getFaceValue();
  if (f !== 0.0) {
    writer.writeDouble(
      1,
      f
    );
  }
  f = message.getDaysToMaturity();
  if (f !== 0) {
    writer.writeInt32(
      2,
      f
    );
  }
  f = /** @type {number} */ (jspb.Message.getField(message, 3));
  if (f != null) {
    writer.writeDouble(
      3,
      f
    );
  }
  f = /** @type {number} */ (jspb.Message.getField(message, 4));
  if (f != null) {
    writer.writeDouble(
      4,
      f
    );
  }
};


/**
 * optional double face_value = 1;
 * @return {number}
 */
proto.sensen.finance.TreasuryBillRequest.prototype.getFaceValue = function() {
  return /** @type {number} */ (jspb.Message.getFloatingPointFieldWithDefault(this, 1, 0.0));
};


/**
 * @param {number} value
 * @return {!proto.sensen.finance.TreasuryBillRequest} returns this
 */
proto.sensen.finance.TreasuryBillRequest.prototype.setFaceValue = function(value) {
  return jspb.Message.setProto3FloatField(this, 1, value);
};


/**
 * optional int32 days_to_maturity = 2;
 * @return {number}
 */
proto.sensen.finance.TreasuryBillRequest.prototype.getDaysToMaturity = function() {
  return /** @type {number} */ (jspb.Message.getFieldWithDefault(this, 2, 0));
};


/**
 * @param {number} value
 * @return {!proto.sensen.finance.TreasuryBillRequest} returns this
 */
proto.sensen.finance.TreasuryBillRequest.prototype.setDaysToMaturity = function(value) {
  return jspb.Message.setProto3IntField(this, 2, value);
};


/**
 * optional double discount_rate = 3;
 * @return {number}
 */
proto.sensen.finance.TreasuryBillRequest.prototype.getDiscountRate = function() {
  return /** @type {number} */ (jspb.Message.getFloatingPointFieldWithDefault(this, 3, 0.0));
};


/**
 * @param {number} value
 * @return {!proto.sensen.finance.TreasuryBillRequest} returns this
 */
proto.sensen.finance.TreasuryBillRequest.prototype.setDiscountRate = function(value) {
  return jspb.Message.setOneofField(this, 3, proto.sensen.finance.TreasuryBillRequest.oneofGroups_[0], value);
};


/**
 * Clears the field making it undefined.
 * @return {!proto.sensen.finance.TreasuryBillRequest} returns this
 */
proto.sensen.finance.TreasuryBillRequest.prototype.clearDiscountRate = function() {
  return jspb.Message.setOneofField(this, 3, proto.sensen.finance.TreasuryBillRequest.oneofGroups_[0], undefined);
};


/**
 * Returns whether this field is set.
 * @return {boolean}
 */
proto.sensen.finance.TreasuryBillRequest.prototype.hasDiscountRate = function() {
  return jspb.Message.getField(this, 3) != null;
};


/**
 * optional double price = 4;
 * @return {number}
 */
proto.sensen.finance.TreasuryBillRequest.prototype.getPrice = function() {
  return /** @type {number} */ (jspb.Message.getFloatingPointFieldWithDefault(this, 4, 0.0));
};


/**
 * @param {number} value
 * @return {!proto.sensen.finance.TreasuryBillRequest} returns this
 */
proto.sensen.finance.TreasuryBillRequest.prototype.setPrice = function(value) {
  return jspb.Message.setOneofField(this, 4, proto.sensen.finance.TreasuryBillRequest.oneofGroups_[0], value);
};


/**
 * Clears the field making it undefined.
 * @return {!proto.sensen.finance.TreasuryBillRequest} returns this
 */
proto.sensen.finance.TreasuryBillRequest.prototype.clearPrice = function() {
  return jspb.Message.setOneofField(this, 4, proto.sensen.finance.TreasuryBillRequest.oneofGroups_[0], undefined);
};


/**
 * Returns whether this field is set.
 * @return {boolean}
 */
proto.sensen.finance.TreasuryBillRequest.prototype.hasPrice = function() {
  return jspb.Message.getField(this, 4) != null;
};





if (jspb.Message.GENERATE_TO_OBJECT) {
/**
 * Creates an object representation of this proto.
 * Field names that are reserved in JavaScript and will be renamed to pb_name.
 * Optional fields that are not set will be set to undefined.
 * To access a reserved field use, foo.pb_<name>, eg, foo.pb_default.
 * For the list of reserved names please see:
 *     net/proto2/compiler/js/internal/generator.cc#kKeyword.
 * @param {boolean=} opt_includeInstance Deprecated. whether to include the
 *     JSPB instance for transitional soy proto support:
 *     http://goto/soy-param-migration
 * @return {!Object}
 */
proto.sensen.finance.TreasuryBillResponse.prototype.toObject = function(opt_includeInstance) {
  return proto.sensen.finance.TreasuryBillResponse.toObject(opt_includeInstance, this);
};


/**
 * Static version of the {@see toObject} method.
 * @param {boolean|undefined} includeInstance Deprecated. Whether to include
 *     the JSPB instance for transitional soy proto support:
 *     http://goto/soy-param-migration
 * @param {!proto.sensen.finance.TreasuryBillResponse} msg The msg instance to transform.
 * @return {!Object}
 * @suppress {unusedLocalVariables} f is only used for nested messages
 */
proto.sensen.finance.TreasuryBillResponse.toObject = function(includeInstance, msg) {
  var f, obj = {
    price: jspb.Message.getFloatingPointFieldWithDefault(msg, 1, 0.0),
    bondEquivalentYield: jspb.Message.getFloatingPointFieldWithDefault(msg, 2, 0.0),
    moneyMarketYield: jspb.Message.getFloatingPointFieldWithDefault(msg, 3, 0.0),
    bankDiscountYield: jspb.Message.getFloatingPointFieldWithDefault(msg, 4, 0.0)
  };

  if (includeInstance) {
    obj.$jspbMessageInstance = msg;
  }
  return obj;
};
}


/**
 * Deserializes binary data (in protobuf wire format).
 * @param {jspb.ByteSource} bytes The bytes to deserialize.
 * @return {!proto.sensen.finance.TreasuryBillResponse}
 */
proto.sensen.finance.TreasuryBillResponse.deserializeBinary = function(bytes) {
  var reader = new jspb.BinaryReader(bytes);
  var msg = new proto.sensen.finance.TreasuryBillResponse;
  return proto.sensen.finance.TreasuryBillResponse.deserializeBinaryFromReader(msg, reader);
};


/**
 * Deserializes binary data (in protobuf wire format) from the
 * given reader into the given message object.
 * @param {!proto.sensen.finance.TreasuryBillResponse} msg The message object to deserialize into.
 * @param {!jspb.BinaryReader} reader The BinaryReader to use.
 * @return {!proto.sensen.finance.TreasuryBillResponse}
 */
proto.sensen.finance.TreasuryBillResponse.deserializeBinaryFromReader = function(msg, reader) {
  while (reader.nextField()) {
    if (reader.isEndGroup()) {
      break;
    }
    var field = reader.getFieldNumber();
    switch (field) {
    case 1:
      var value = /** @type {number} */ (reader.readDouble());
      msg.setPrice(value);
      break;
    case 2:
      var value = /** @type {number} */ (reader.readDouble());
      msg.setBondEquivalentYield(value);
      break;
    case 3:
      var value = /** @type {number} */ (reader.readDouble());
      msg.setMoneyMarketYield(value);
      break;
    case 4:
      var value = /** @type {number} */ (reader.readDouble());
      msg.setBankDiscountYield(value);
      break;
    default:
      reader.skipField();
      break;
    }
  }
  return msg;
};


/**
 * Serializes the message to binary data (in protobuf wire format).
 * @return {!Uint8Array}
 */
proto.sensen.finance.TreasuryBillResponse.prototype.serializeBinary = function() {
  var writer = new jspb.BinaryWriter();
  proto.sensen.finance.TreasuryBillResponse.serializeBinaryToWriter(this, writer);
  return writer.getResultBuffer();
};


/**
 * Serializes the given message to binary data (in protobuf wire
 * format), writing to the given BinaryWriter.
 * @param {!proto.sensen.finance.TreasuryBillResponse} message
 * @param {!jspb.BinaryWriter} writer
 * @suppress {unusedLocalVariables} f is only used for nested messages
 */
proto.sensen.finance.TreasuryBillResponse.serializeBinaryToWriter = function(message, writer) {
  var f = undefined;
  f = message.getPrice();
  if (f !== 0.0) {
    writer.writeDouble(
      1,
      f
    );
  }
  f = message.getBondEquivalentYield();
  if (f !== 0.0) {
    writer.writeDouble(
      2,
      f
    );
  }
  f = message.getMoneyMarketYield();
  if (f !== 0.0) {
    writer.writeDouble(
      3,
      f
    );
  }
  f = message.getBankDiscountYield();
  if (f !== 0.0) {
    writer.writeDouble(
      4,
      f
    );
  }
};


/**
 * optional double price = 1;
 * @return {number}
 */
proto.sensen.finance.TreasuryBillResponse.prototype.getPrice = function() {
  return /** @type {number} */ (jspb.Message.getFloatingPointFieldWithDefault(this, 1, 0.0));
};


/**
 * @param {number} value
 * @return {!proto.sensen.finance.TreasuryBillResponse} returns this
 */
proto.sensen.finance.TreasuryBillResponse.prototype.setPrice = function(value) {
  return jspb.Message.setProto3FloatField(this, 1, value);
};


/**
 * optional double bond_equivalent_yield = 2;
 * @return {number}
 */
proto.sensen.finance.TreasuryBillResponse.prototype.getBondEquivalentYield = function() {
  return /** @type {number} */ (jspb.Message.getFloatingPointFieldWithDefault(this, 2, 0.0));
};


/**
 * @param {number} value
 * @return {!proto.sensen.finance.TreasuryBillResponse} returns this
 */
proto.sensen.finance.TreasuryBillResponse.prototype.setBondEquivalentYield = function(value) {
  return jspb.Message.setProto3FloatField(this, 2, value);
};


/**
 * optional double money_market_yield = 3;
 * @return {number}
 */
proto.sensen.finance.TreasuryBillResponse.prototype.getMoneyMarketYield = function() {
  return /** @type {number} */ (jspb.Message.getFloatingPointFieldWithDefault(this, 3, 0.0));
};


/**
 * @param {number} value
 * @return {!proto.sensen.finance.TreasuryBillResponse} returns this
 */
proto.sensen.finance.TreasuryBillResponse.prototype.setMoneyMarketYield = function(value) {
  return jspb.Message.setProto3FloatField(this, 3, value);
};


/**
 * optional double bank_discount_yield = 4;
 * @return {number}
 */
proto.sensen.finance.TreasuryBillResponse.prototype.getBankDiscountYield = function() {
  return /** @type {number} */ (jspb.Message.getFloatingPointFieldWithDefault(this, 4, 0.0));
};


/**
 * @param {number} value
 * @return {!proto.sensen.finance.TreasuryBillResponse} returns this
 */
proto.sensen.finance.TreasuryBillResponse.prototype.setBankDiscountYield = function(value) {
  return jspb.Message.setProto3FloatField(this, 4, value);
};





if (jspb.Message.GENERATE_TO_OBJECT) {
/**
 * Creates an object representation of this proto.
 * Field names that are reserved in JavaScript and will be renamed to pb_name.
 * Optional fields that are not set will be set to undefined.
 * To access a reserved field use, foo.pb_<name>, eg, foo.pb_default.
 * For the list of reserved names please see:
 *     net/proto2/compiler/js/internal/generator.cc#kKeyword.
 * @param {boolean=} opt_includeInstance Deprecated. whether to include the
 *     JSPB instance for transitional soy proto support:
 *     http://goto/soy-param-migration
 * @return {!Object}
 */
proto.sensen.finance.FuturesPricingRequest.prototype.toObject = function(opt_includeInstance) {
  return proto.sensen.finance.FuturesPricingRequest.toObject(opt_includeInstance, this);
};


/**
 * Static version of the {@see toObject} method.
 * @param {boolean|undefined} includeInstance Deprecated. Whether to include
 *     the JSPB instance for transitional soy proto support:
 *     http://goto/soy-param-migration
 * @param {!proto.sensen.finance.FuturesPricingRequest} msg The msg instance to transform.
 * @return {!Object}
 * @suppress {unusedLocalVariables} f is only used for nested messages
 */
proto.sensen.finance.FuturesPricingRequest.toObject = function(includeInstance, msg) {
  var f, obj = {
    spot: jspb.Message.getFloatingPointFieldWithDefault(msg, 1, 0.0),
    rate: jspb.Message.getFloatingPointFieldWithDefault(msg, 2, 0.0),
    costOfCarry: jspb.Message.getFloatingPointFieldWithDefault(msg, 3, 0.0),
    yearsToMaturity: jspb.Message.getFloatingPointFieldWithDefault(msg, 4, 0.0),
    continuous: jspb.Message.getBooleanFieldWithDefault(msg, 5, false)
  };

  if (includeInstance) {
    obj.$jspbMessageInstance = msg;
  }
  return obj;
};
}


/**
 * Deserializes binary data (in protobuf wire format).
 * @param {jspb.ByteSource} bytes The bytes to deserialize.
 * @return {!proto.sensen.finance.FuturesPricingRequest}
 */
proto.sensen.finance.FuturesPricingRequest.deserializeBinary = function(bytes) {
  var reader = new jspb.BinaryReader(bytes);
  var msg = new proto.sensen.finance.FuturesPricingRequest;
  return proto.sensen.finance.FuturesPricingRequest.deserializeBinaryFromReader(msg, reader);
};


/**
 * Deserializes binary data (in protobuf wire format) from the
 * given reader into the given message object.
 * @param {!proto.sensen.finance.FuturesPricingRequest} msg The message object to deserialize into.
 * @param {!jspb.BinaryReader} reader The BinaryReader to use.
 * @return {!proto.sensen.finance.FuturesPricingRequest}
 */
proto.sensen.finance.FuturesPricingRequest.deserializeBinaryFromReader = function(msg, reader) {
  while (reader.nextField()) {
    if (reader.isEndGroup()) {
      break;
    }
    var field = reader.getFieldNumber();
    switch (field) {
    case 1:
      var value = /** @type {number} */ (reader.readDouble());
      msg.setSpot(value);
      break;
    case 2:
      var value = /** @type {number} */ (reader.readDouble());
      msg.setRate(value);
      break;
    case 3:
      var value = /** @type {number} */ (reader.readDouble());
      msg.setCostOfCarry(value);
      break;
    case 4:
      var value = /** @type {number} */ (reader.readDouble());
      msg.setYearsToMaturity(value);
      break;
    case 5:
      var value = /** @type {boolean} */ (reader.readBool());
      msg.setContinuous(value);
      break;
    default:
      reader.skipField();
      break;
    }
  }
  return msg;
};


/**
 * Serializes the message to binary data (in protobuf wire format).
 * @return {!Uint8Array}
 */
proto.sensen.finance.FuturesPricingRequest.prototype.serializeBinary = function() {
  var writer = new jspb.BinaryWriter();
  proto.sensen.finance.FuturesPricingRequest.serializeBinaryToWriter(this, writer);
  return writer.getResultBuffer();
};


/**
 * Serializes the given message to binary data (in protobuf wire
 * format), writing to the given BinaryWriter.
 * @param {!proto.sensen.finance.FuturesPricingRequest} message
 * @param {!jspb.BinaryWriter} writer
 * @suppress {unusedLocalVariables} f is only used for nested messages
 */
proto.sensen.finance.FuturesPricingRequest.serializeBinaryToWriter = function(message, writer) {
  var f = undefined;
  f = message.getSpot();
  if (f !== 0.0) {
    writer.writeDouble(
      1,
      f
    );
  }
  f = message.getRate();
  if (f !== 0.0) {
    writer.writeDouble(
      2,
      f
    );
  }
  f = message.getCostOfCarry();
  if (f !== 0.0) {
    writer.writeDouble(
      3,
      f
    );
  }
  f = message.getYearsToMaturity();
  if (f !== 0.0) {
    writer.writeDouble(
      4,
      f
    );
  }
  f = message.getContinuous();
  if (f) {
    writer.writeBool(
      5,
      f
    );
  }
};


/**
 * optional double spot = 1;
 * @return {number}
 */
proto.sensen.finance.FuturesPricingRequest.prototype.getSpot = function() {
  return /** @type {number} */ (jspb.Message.getFloatingPointFieldWithDefault(this, 1, 0.0));
};


/**
 * @param {number} value
 * @return {!proto.sensen.finance.FuturesPricingRequest} returns this
 */
proto.sensen.finance.FuturesPricingRequest.prototype.setSpot = function(value) {
  return jspb.Message.setProto3FloatField(this, 1, value);
};


/**
 * optional double rate = 2;
 * @return {number}
 */
proto.sensen.finance.FuturesPricingRequest.prototype.getRate = function() {
  return /** @type {number} */ (jspb.Message.getFloatingPointFieldWithDefault(this, 2, 0.0));
};


/**
 * @param {number} value
 * @return {!proto.sensen.finance.FuturesPricingRequest} returns this
 */
proto.sensen.finance.FuturesPricingRequest.prototype.setRate = function(value) {
  return jspb.Message.setProto3FloatField(this, 2, value);
};


/**
 * optional double cost_of_carry = 3;
 * @return {number}
 */
proto.sensen.finance.FuturesPricingRequest.prototype.getCostOfCarry = function() {
  return /** @type {number} */ (jspb.Message.getFloatingPointFieldWithDefault(this, 3, 0.0));
};


/**
 * @param {number} value
 * @return {!proto.sensen.finance.FuturesPricingRequest} returns this
 */
proto.sensen.finance.FuturesPricingRequest.prototype.setCostOfCarry = function(value) {
  return jspb.Message.setProto3FloatField(this, 3, value);
};


/**
 * optional double years_to_maturity = 4;
 * @return {number}
 */
proto.sensen.finance.FuturesPricingRequest.prototype.getYearsToMaturity = function() {
  return /** @type {number} */ (jspb.Message.getFloatingPointFieldWithDefault(this, 4, 0.0));
};


/**
 * @param {number} value
 * @return {!proto.sensen.finance.FuturesPricingRequest} returns this
 */
proto.sensen.finance.FuturesPricingRequest.prototype.setYearsToMaturity = function(value) {
  return jspb.Message.setProto3FloatField(this, 4, value);
};


/**
 * optional bool continuous = 5;
 * @return {boolean}
 */
proto.sensen.finance.FuturesPricingRequest.prototype.getContinuous = function() {
  return /** @type {boolean} */ (jspb.Message.getBooleanFieldWithDefault(this, 5, false));
};


/**
 * @param {boolean} value
 * @return {!proto.sensen.finance.FuturesPricingRequest} returns this
 */
proto.sensen.finance.FuturesPricingRequest.prototype.setContinuous = function(value) {
  return jspb.Message.setProto3BooleanField(this, 5, value);
};





if (jspb.Message.GENERATE_TO_OBJECT) {
/**
 * Creates an object representation of this proto.
 * Field names that are reserved in JavaScript and will be renamed to pb_name.
 * Optional fields that are not set will be set to undefined.
 * To access a reserved field use, foo.pb_<name>, eg, foo.pb_default.
 * For the list of reserved names please see:
 *     net/proto2/compiler/js/internal/generator.cc#kKeyword.
 * @param {boolean=} opt_includeInstance Deprecated. whether to include the
 *     JSPB instance for transitional soy proto support:
 *     http://goto/soy-param-migration
 * @return {!Object}
 */
proto.sensen.finance.FuturesValuationRequest.prototype.toObject = function(opt_includeInstance) {
  return proto.sensen.finance.FuturesValuationRequest.toObject(opt_includeInstance, this);
};


/**
 * Static version of the {@see toObject} method.
 * @param {boolean|undefined} includeInstance Deprecated. Whether to include
 *     the JSPB instance for transitional soy proto support:
 *     http://goto/soy-param-migration
 * @param {!proto.sensen.finance.FuturesValuationRequest} msg The msg instance to transform.
 * @return {!Object}
 * @suppress {unusedLocalVariables} f is only used for nested messages
 */
proto.sensen.finance.FuturesValuationRequest.toObject = function(includeInstance, msg) {
  var f, obj = {
    currentSpot: jspb.Message.getFloatingPointFieldWithDefault(msg, 1, 0.0),
    deliveryPrice: jspb.Message.getFloatingPointFieldWithDefault(msg, 2, 0.0),
    rate: jspb.Message.getFloatingPointFieldWithDefault(msg, 3, 0.0),
    yearsToMaturity: jspb.Message.getFloatingPointFieldWithDefault(msg, 4, 0.0),
    isLong: jspb.Message.getBooleanFieldWithDefault(msg, 5, false)
  };

  if (includeInstance) {
    obj.$jspbMessageInstance = msg;
  }
  return obj;
};
}


/**
 * Deserializes binary data (in protobuf wire format).
 * @param {jspb.ByteSource} bytes The bytes to deserialize.
 * @return {!proto.sensen.finance.FuturesValuationRequest}
 */
proto.sensen.finance.FuturesValuationRequest.deserializeBinary = function(bytes) {
  var reader = new jspb.BinaryReader(bytes);
  var msg = new proto.sensen.finance.FuturesValuationRequest;
  return proto.sensen.finance.FuturesValuationRequest.deserializeBinaryFromReader(msg, reader);
};


/**
 * Deserializes binary data (in protobuf wire format) from the
 * given reader into the given message object.
 * @param {!proto.sensen.finance.FuturesValuationRequest} msg The message object to deserialize into.
 * @param {!jspb.BinaryReader} reader The BinaryReader to use.
 * @return {!proto.sensen.finance.FuturesValuationRequest}
 */
proto.sensen.finance.FuturesValuationRequest.deserializeBinaryFromReader = function(msg, reader) {
  while (reader.nextField()) {
    if (reader.isEndGroup()) {
      break;
    }
    var field = reader.getFieldNumber();
    switch (field) {
    case 1:
      var value = /** @type {number} */ (reader.readDouble());
      msg.setCurrentSpot(value);
      break;
    case 2:
      var value = /** @type {number} */ (reader.readDouble());
      msg.setDeliveryPrice(value);
      break;
    case 3:
      var value = /** @type {number} */ (reader.readDouble());
      msg.setRate(value);
      break;
    case 4:
      var value = /** @type {number} */ (reader.readDouble());
      msg.setYearsToMaturity(value);
      break;
    case 5:
      var value = /** @type {boolean} */ (reader.readBool());
      msg.setIsLong(value);
      break;
    default:
      reader.skipField();
      break;
    }
  }
  return msg;
};


/**
 * Serializes the message to binary data (in protobuf wire format).
 * @return {!Uint8Array}
 */
proto.sensen.finance.FuturesValuationRequest.prototype.serializeBinary = function() {
  var writer = new jspb.BinaryWriter();
  proto.sensen.finance.FuturesValuationRequest.serializeBinaryToWriter(this, writer);
  return writer.getResultBuffer();
};


/**
 * Serializes the given message to binary data (in protobuf wire
 * format), writing to the given BinaryWriter.
 * @param {!proto.sensen.finance.FuturesValuationRequest} message
 * @param {!jspb.BinaryWriter} writer
 * @suppress {unusedLocalVariables} f is only used for nested messages
 */
proto.sensen.finance.FuturesValuationRequest.serializeBinaryToWriter = function(message, writer) {
  var f = undefined;
  f = message.getCurrentSpot();
  if (f !== 0.0) {
    writer.writeDouble(
      1,
      f
    );
  }
  f = message.getDeliveryPrice();
  if (f !== 0.0) {
    writer.writeDouble(
      2,
      f
    );
  }
  f = message.getRate();
  if (f !== 0.0) {
    writer.writeDouble(
      3,
      f
    );
  }
  f = message.getYearsToMaturity();
  if (f !== 0.0) {
    writer.writeDouble(
      4,
      f
    );
  }
  f = message.getIsLong();
  if (f) {
    writer.writeBool(
      5,
      f
    );
  }
};


/**
 * optional double current_spot = 1;
 * @return {number}
 */
proto.sensen.finance.FuturesValuationRequest.prototype.getCurrentSpot = function() {
  return /** @type {number} */ (jspb.Message.getFloatingPointFieldWithDefault(this, 1, 0.0));
};


/**
 * @param {number} value
 * @return {!proto.sensen.finance.FuturesValuationRequest} returns this
 */
proto.sensen.finance.FuturesValuationRequest.prototype.setCurrentSpot = function(value) {
  return jspb.Message.setProto3FloatField(this, 1, value);
};


/**
 * optional double delivery_price = 2;
 * @return {number}
 */
proto.sensen.finance.FuturesValuationRequest.prototype.getDeliveryPrice = function() {
  return /** @type {number} */ (jspb.Message.getFloatingPointFieldWithDefault(this, 2, 0.0));
};


/**
 * @param {number} value
 * @return {!proto.sensen.finance.FuturesValuationRequest} returns this
 */
proto.sensen.finance.FuturesValuationRequest.prototype.setDeliveryPrice = function(value) {
  return jspb.Message.setProto3FloatField(this, 2, value);
};


/**
 * optional double rate = 3;
 * @return {number}
 */
proto.sensen.finance.FuturesValuationRequest.prototype.getRate = function() {
  return /** @type {number} */ (jspb.Message.getFloatingPointFieldWithDefault(this, 3, 0.0));
};


/**
 * @param {number} value
 * @return {!proto.sensen.finance.FuturesValuationRequest} returns this
 */
proto.sensen.finance.FuturesValuationRequest.prototype.setRate = function(value) {
  return jspb.Message.setProto3FloatField(this, 3, value);
};


/**
 * optional double years_to_maturity = 4;
 * @return {number}
 */
proto.sensen.finance.FuturesValuationRequest.prototype.getYearsToMaturity = function() {
  return /** @type {number} */ (jspb.Message.getFloatingPointFieldWithDefault(this, 4, 0.0));
};


/**
 * @param {number} value
 * @return {!proto.sensen.finance.FuturesValuationRequest} returns this
 */
proto.sensen.finance.FuturesValuationRequest.prototype.setYearsToMaturity = function(value) {
  return jspb.Message.setProto3FloatField(this, 4, value);
};


/**
 * optional bool is_long = 5;
 * @return {boolean}
 */
proto.sensen.finance.FuturesValuationRequest.prototype.getIsLong = function() {
  return /** @type {boolean} */ (jspb.Message.getBooleanFieldWithDefault(this, 5, false));
};


/**
 * @param {boolean} value
 * @return {!proto.sensen.finance.FuturesValuationRequest} returns this
 */
proto.sensen.finance.FuturesValuationRequest.prototype.setIsLong = function(value) {
  return jspb.Message.setProto3BooleanField(this, 5, value);
};



/**
 * List of repeated fields within this message type.
 * @private {!Array<number>}
 * @const
 */
proto.sensen.finance.MarginSimulationRequest.repeatedFields_ = [6];



if (jspb.Message.GENERATE_TO_OBJECT) {
/**
 * Creates an object representation of this proto.
 * Field names that are reserved in JavaScript and will be renamed to pb_name.
 * Optional fields that are not set will be set to undefined.
 * To access a reserved field use, foo.pb_<name>, eg, foo.pb_default.
 * For the list of reserved names please see:
 *     net/proto2/compiler/js/internal/generator.cc#kKeyword.
 * @param {boolean=} opt_includeInstance Deprecated. whether to include the
 *     JSPB instance for transitional soy proto support:
 *     http://goto/soy-param-migration
 * @return {!Object}
 */
proto.sensen.finance.MarginSimulationRequest.prototype.toObject = function(opt_includeInstance) {
  return proto.sensen.finance.MarginSimulationRequest.toObject(opt_includeInstance, this);
};


/**
 * Static version of the {@see toObject} method.
 * @param {boolean|undefined} includeInstance Deprecated. Whether to include
 *     the JSPB instance for transitional soy proto support:
 *     http://goto/soy-param-migration
 * @param {!proto.sensen.finance.MarginSimulationRequest} msg The msg instance to transform.
 * @return {!Object}
 * @suppress {unusedLocalVariables} f is only used for nested messages
 */
proto.sensen.finance.MarginSimulationRequest.toObject = function(includeInstance, msg) {
  var f, obj = {
    initialDeposit: jspb.Message.getFloatingPointFieldWithDefault(msg, 1, 0.0),
    initialMarginRequirement: jspb.Message.getFloatingPointFieldWithDefault(msg, 2, 0.0),
    maintenanceMarginRequirement: jspb.Message.getFloatingPointFieldWithDefault(msg, 3, 0.0),
    contractSize: jspb.Message.getFieldWithDefault(msg, 4, 0),
    entryPrice: jspb.Message.getFloatingPointFieldWithDefault(msg, 5, 0.0),
    dailyPricesList: (f = jspb.Message.getRepeatedFloatingPointField(msg, 6)) == null ? undefined : f,
    isLong: jspb.Message.getBooleanFieldWithDefault(msg, 7, false)
  };

  if (includeInstance) {
    obj.$jspbMessageInstance = msg;
  }
  return obj;
};
}


/**
 * Deserializes binary data (in protobuf wire format).
 * @param {jspb.ByteSource} bytes The bytes to deserialize.
 * @return {!proto.sensen.finance.MarginSimulationRequest}
 */
proto.sensen.finance.MarginSimulationRequest.deserializeBinary = function(bytes) {
  var reader = new jspb.BinaryReader(bytes);
  var msg = new proto.sensen.finance.MarginSimulationRequest;
  return proto.sensen.finance.MarginSimulationRequest.deserializeBinaryFromReader(msg, reader);
};


/**
 * Deserializes binary data (in protobuf wire format) from the
 * given reader into the given message object.
 * @param {!proto.sensen.finance.MarginSimulationRequest} msg The message object to deserialize into.
 * @param {!jspb.BinaryReader} reader The BinaryReader to use.
 * @return {!proto.sensen.finance.MarginSimulationRequest}
 */
proto.sensen.finance.MarginSimulationRequest.deserializeBinaryFromReader = function(msg, reader) {
  while (reader.nextField()) {
    if (reader.isEndGroup()) {
      break;
    }
    var field = reader.getFieldNumber();
    switch (field) {
    case 1:
      var value = /** @type {number} */ (reader.readDouble());
      msg.setInitialDeposit(value);
      break;
    case 2:
      var value = /** @type {number} */ (reader.readDouble());
      msg.setInitialMarginRequirement(value);
      break;
    case 3:
      var value = /** @type {number} */ (reader.readDouble());
      msg.setMaintenanceMarginRequirement(value);
      break;
    case 4:
      var value = /** @type {number} */ (reader.readInt32());
      msg.setContractSize(value);
      break;
    case 5:
      var value = /** @type {number} */ (reader.readDouble());
      msg.setEntryPrice(value);
      break;
    case 6:
      var values = /** @type {!Array<number>} */ (reader.isDelimited() ? reader.readPackedDouble() : [reader.readDouble()]);
      for (var i = 0; i < values.length; i++) {
        msg.addDailyPrices(values[i]);
      }
      break;
    case 7:
      var value = /** @type {boolean} */ (reader.readBool());
      msg.setIsLong(value);
      break;
    default:
      reader.skipField();
      break;
    }
  }
  return msg;
};


/**
 * Serializes the message to binary data (in protobuf wire format).
 * @return {!Uint8Array}
 */
proto.sensen.finance.MarginSimulationRequest.prototype.serializeBinary = function() {
  var writer = new jspb.BinaryWriter();
  proto.sensen.finance.MarginSimulationRequest.serializeBinaryToWriter(this, writer);
  return writer.getResultBuffer();
};


/**
 * Serializes the given message to binary data (in protobuf wire
 * format), writing to the given BinaryWriter.
 * @param {!proto.sensen.finance.MarginSimulationRequest} message
 * @param {!jspb.BinaryWriter} writer
 * @suppress {unusedLocalVariables} f is only used for nested messages
 */
proto.sensen.finance.MarginSimulationRequest.serializeBinaryToWriter = function(message, writer) {
  var f = undefined;
  f = message.getInitialDeposit();
  if (f !== 0.0) {
    writer.writeDouble(
      1,
      f
    );
  }
  f = message.getInitialMarginRequirement();
  if (f !== 0.0) {
    writer.writeDouble(
      2,
      f
    );
  }
  f = message.getMaintenanceMarginRequirement();
  if (f !== 0.0) {
    writer.writeDouble(
      3,
      f
    );
  }
  f = message.getContractSize();
  if (f !== 0) {
    writer.writeInt32(
      4,
      f
    );
  }
  f = message.getEntryPrice();
  if (f !== 0.0) {
    writer.writeDouble(
      5,
      f
    );
  }
  f = message.getDailyPricesList();
  if (f.length > 0) {
    writer.writePackedDouble(
      6,
      f
    );
  }
  f = message.getIsLong();
  if (f) {
    writer.writeBool(
      7,
      f
    );
  }
};


/**
 * optional double initial_deposit = 1;
 * @return {number}
 */
proto.sensen.finance.MarginSimulationRequest.prototype.getInitialDeposit = function() {
  return /** @type {number} */ (jspb.Message.getFloatingPointFieldWithDefault(this, 1, 0.0));
};


/**
 * @param {number} value
 * @return {!proto.sensen.finance.MarginSimulationRequest} returns this
 */
proto.sensen.finance.MarginSimulationRequest.prototype.setInitialDeposit = function(value) {
  return jspb.Message.setProto3FloatField(this, 1, value);
};


/**
 * optional double initial_margin_requirement = 2;
 * @return {number}
 */
proto.sensen.finance.MarginSimulationRequest.prototype.getInitialMarginRequirement = function() {
  return /** @type {number} */ (jspb.Message.getFloatingPointFieldWithDefault(this, 2, 0.0));
};


/**
 * @param {number} value
 * @return {!proto.sensen.finance.MarginSimulationRequest} returns this
 */
proto.sensen.finance.MarginSimulationRequest.prototype.setInitialMarginRequirement = function(value) {
  return jspb.Message.setProto3FloatField(this, 2, value);
};


/**
 * optional double maintenance_margin_requirement = 3;
 * @return {number}
 */
proto.sensen.finance.MarginSimulationRequest.prototype.getMaintenanceMarginRequirement = function() {
  return /** @type {number} */ (jspb.Message.getFloatingPointFieldWithDefault(this, 3, 0.0));
};


/**
 * @param {number} value
 * @return {!proto.sensen.finance.MarginSimulationRequest} returns this
 */
proto.sensen.finance.MarginSimulationRequest.prototype.setMaintenanceMarginRequirement = function(value) {
  return jspb.Message.setProto3FloatField(this, 3, value);
};


/**
 * optional int32 contract_size = 4;
 * @return {number}
 */
proto.sensen.finance.MarginSimulationRequest.prototype.getContractSize = function() {
  return /** @type {number} */ (jspb.Message.getFieldWithDefault(this, 4, 0));
};


/**
 * @param {number} value
 * @return {!proto.sensen.finance.MarginSimulationRequest} returns this
 */
proto.sensen.finance.MarginSimulationRequest.prototype.setContractSize = function(value) {
  return jspb.Message.setProto3IntField(this, 4, value);
};


/**
 * optional double entry_price = 5;
 * @return {number}
 */
proto.sensen.finance.MarginSimulationRequest.prototype.getEntryPrice = function() {
  return /** @type {number} */ (jspb.Message.getFloatingPointFieldWithDefault(this, 5, 0.0));
};


/**
 * @param {number} value
 * @return {!proto.sensen.finance.MarginSimulationRequest} returns this
 */
proto.sensen.finance.MarginSimulationRequest.prototype.setEntryPrice = function(value) {
  return jspb.Message.setProto3FloatField(this, 5, value);
};


/**
 * repeated double daily_prices = 6;
 * @return {!Array<number>}
 */
proto.sensen.finance.MarginSimulationRequest.prototype.getDailyPricesList = function() {
  return /** @type {!Array<number>} */ (jspb.Message.getRepeatedFloatingPointField(this, 6));
};


/**
 * @param {!Array<number>} value
 * @return {!proto.sensen.finance.MarginSimulationRequest} returns this
 */
proto.sensen.finance.MarginSimulationRequest.prototype.setDailyPricesList = function(value) {
  return jspb.Message.setField(this, 6, value || []);
};


/**
 * @param {number} value
 * @param {number=} opt_index
 * @return {!proto.sensen.finance.MarginSimulationRequest} returns this
 */
proto.sensen.finance.MarginSimulationRequest.prototype.addDailyPrices = function(value, opt_index) {
  return jspb.Message.addToRepeatedField(this, 6, value, opt_index);
};


/**
 * Clears the list making it empty but non-null.
 * @return {!proto.sensen.finance.MarginSimulationRequest} returns this
 */
proto.sensen.finance.MarginSimulationRequest.prototype.clearDailyPricesList = function() {
  return this.setDailyPricesList([]);
};


/**
 * optional bool is_long = 7;
 * @return {boolean}
 */
proto.sensen.finance.MarginSimulationRequest.prototype.getIsLong = function() {
  return /** @type {boolean} */ (jspb.Message.getBooleanFieldWithDefault(this, 7, false));
};


/**
 * @param {boolean} value
 * @return {!proto.sensen.finance.MarginSimulationRequest} returns this
 */
proto.sensen.finance.MarginSimulationRequest.prototype.setIsLong = function(value) {
  return jspb.Message.setProto3BooleanField(this, 7, value);
};





if (jspb.Message.GENERATE_TO_OBJECT) {
/**
 * Creates an object representation of this proto.
 * Field names that are reserved in JavaScript and will be renamed to pb_name.
 * Optional fields that are not set will be set to undefined.
 * To access a reserved field use, foo.pb_<name>, eg, foo.pb_default.
 * For the list of reserved names please see:
 *     net/proto2/compiler/js/internal/generator.cc#kKeyword.
 * @param {boolean=} opt_includeInstance Deprecated. whether to include the
 *     JSPB instance for transitional soy proto support:
 *     http://goto/soy-param-migration
 * @return {!Object}
 */
proto.sensen.finance.MarginSimulationResponse.prototype.toObject = function(opt_includeInstance) {
  return proto.sensen.finance.MarginSimulationResponse.toObject(opt_includeInstance, this);
};


/**
 * Static version of the {@see toObject} method.
 * @param {boolean|undefined} includeInstance Deprecated. Whether to include
 *     the JSPB instance for transitional soy proto support:
 *     http://goto/soy-param-migration
 * @param {!proto.sensen.finance.MarginSimulationResponse} msg The msg instance to transform.
 * @return {!Object}
 * @suppress {unusedLocalVariables} f is only used for nested messages
 */
proto.sensen.finance.MarginSimulationResponse.toObject = function(includeInstance, msg) {
  var f, obj = {
    balance: jspb.Message.getFloatingPointFieldWithDefault(msg, 1, 0.0),
    initialMargin: jspb.Message.getFloatingPointFieldWithDefault(msg, 2, 0.0),
    maintenanceMargin: jspb.Message.getFloatingPointFieldWithDefault(msg, 3, 0.0),
    contractSize: jspb.Message.getFieldWithDefault(msg, 4, 0),
    marginCall: jspb.Message.getBooleanFieldWithDefault(msg, 5, false)
  };

  if (includeInstance) {
    obj.$jspbMessageInstance = msg;
  }
  return obj;
};
}


/**
 * Deserializes binary data (in protobuf wire format).
 * @param {jspb.ByteSource} bytes The bytes to deserialize.
 * @return {!proto.sensen.finance.MarginSimulationResponse}
 */
proto.sensen.finance.MarginSimulationResponse.deserializeBinary = function(bytes) {
  var reader = new jspb.BinaryReader(bytes);
  var msg = new proto.sensen.finance.MarginSimulationResponse;
  return proto.sensen.finance.MarginSimulationResponse.deserializeBinaryFromReader(msg, reader);
};


/**
 * Deserializes binary data (in protobuf wire format) from the
 * given reader into the given message object.
 * @param {!proto.sensen.finance.MarginSimulationResponse} msg The message object to deserialize into.
 * @param {!jspb.BinaryReader} reader The BinaryReader to use.
 * @return {!proto.sensen.finance.MarginSimulationResponse}
 */
proto.sensen.finance.MarginSimulationResponse.deserializeBinaryFromReader = function(msg, reader) {
  while (reader.nextField()) {
    if (reader.isEndGroup()) {
      break;
    }
    var field = reader.getFieldNumber();
    switch (field) {
    case 1:
      var value = /** @type {number} */ (reader.readDouble());
      msg.setBalance(value);
      break;
    case 2:
      var value = /** @type {number} */ (reader.readDouble());
      msg.setInitialMargin(value);
      break;
    case 3:
      var value = /** @type {number} */ (reader.readDouble());
      msg.setMaintenanceMargin(value);
      break;
    case 4:
      var value = /** @type {number} */ (reader.readInt32());
      msg.setContractSize(value);
      break;
    case 5:
      var value = /** @type {boolean} */ (reader.readBool());
      msg.setMarginCall(value);
      break;
    default:
      reader.skipField();
      break;
    }
  }
  return msg;
};


/**
 * Serializes the message to binary data (in protobuf wire format).
 * @return {!Uint8Array}
 */
proto.sensen.finance.MarginSimulationResponse.prototype.serializeBinary = function() {
  var writer = new jspb.BinaryWriter();
  proto.sensen.finance.MarginSimulationResponse.serializeBinaryToWriter(this, writer);
  return writer.getResultBuffer();
};


/**
 * Serializes the given message to binary data (in protobuf wire
 * format), writing to the given BinaryWriter.
 * @param {!proto.sensen.finance.MarginSimulationResponse} message
 * @param {!jspb.BinaryWriter} writer
 * @suppress {unusedLocalVariables} f is only used for nested messages
 */
proto.sensen.finance.MarginSimulationResponse.serializeBinaryToWriter = function(message, writer) {
  var f = undefined;
  f = message.getBalance();
  if (f !== 0.0) {
    writer.writeDouble(
      1,
      f
    );
  }
  f = message.getInitialMargin();
  if (f !== 0.0) {
    writer.writeDouble(
      2,
      f
    );
  }
  f = message.getMaintenanceMargin();
  if (f !== 0.0) {
    writer.writeDouble(
      3,
      f
    );
  }
  f = message.getContractSize();
  if (f !== 0) {
    writer.writeInt32(
      4,
      f
    );
  }
  f = message.getMarginCall();
  if (f) {
    writer.writeBool(
      5,
      f
    );
  }
};


/**
 * optional double balance = 1;
 * @return {number}
 */
proto.sensen.finance.MarginSimulationResponse.prototype.getBalance = function() {
  return /** @type {number} */ (jspb.Message.getFloatingPointFieldWithDefault(this, 1, 0.0));
};


/**
 * @param {number} value
 * @return {!proto.sensen.finance.MarginSimulationResponse} returns this
 */
proto.sensen.finance.MarginSimulationResponse.prototype.setBalance = function(value) {
  return jspb.Message.setProto3FloatField(this, 1, value);
};


/**
 * optional double initial_margin = 2;
 * @return {number}
 */
proto.sensen.finance.MarginSimulationResponse.prototype.getInitialMargin = function() {
  return /** @type {number} */ (jspb.Message.getFloatingPointFieldWithDefault(this, 2, 0.0));
};


/**
 * @param {number} value
 * @return {!proto.sensen.finance.MarginSimulationResponse} returns this
 */
proto.sensen.finance.MarginSimulationResponse.prototype.setInitialMargin = function(value) {
  return jspb.Message.setProto3FloatField(this, 2, value);
};


/**
 * optional double maintenance_margin = 3;
 * @return {number}
 */
proto.sensen.finance.MarginSimulationResponse.prototype.getMaintenanceMargin = function() {
  return /** @type {number} */ (jspb.Message.getFloatingPointFieldWithDefault(this, 3, 0.0));
};


/**
 * @param {number} value
 * @return {!proto.sensen.finance.MarginSimulationResponse} returns this
 */
proto.sensen.finance.MarginSimulationResponse.prototype.setMaintenanceMargin = function(value) {
  return jspb.Message.setProto3FloatField(this, 3, value);
};


/**
 * optional int32 contract_size = 4;
 * @return {number}
 */
proto.sensen.finance.MarginSimulationResponse.prototype.getContractSize = function() {
  return /** @type {number} */ (jspb.Message.getFieldWithDefault(this, 4, 0));
};


/**
 * @param {number} value
 * @return {!proto.sensen.finance.MarginSimulationResponse} returns this
 */
proto.sensen.finance.MarginSimulationResponse.prototype.setContractSize = function(value) {
  return jspb.Message.setProto3IntField(this, 4, value);
};


/**
 * optional bool margin_call = 5;
 * @return {boolean}
 */
proto.sensen.finance.MarginSimulationResponse.prototype.getMarginCall = function() {
  return /** @type {boolean} */ (jspb.Message.getBooleanFieldWithDefault(this, 5, false));
};


/**
 * @param {boolean} value
 * @return {!proto.sensen.finance.MarginSimulationResponse} returns this
 */
proto.sensen.finance.MarginSimulationResponse.prototype.setMarginCall = function(value) {
  return jspb.Message.setProto3BooleanField(this, 5, value);
};





if (jspb.Message.GENERATE_TO_OBJECT) {
/**
 * Creates an object representation of this proto.
 * Field names that are reserved in JavaScript and will be renamed to pb_name.
 * Optional fields that are not set will be set to undefined.
 * To access a reserved field use, foo.pb_<name>, eg, foo.pb_default.
 * For the list of reserved names please see:
 *     net/proto2/compiler/js/internal/generator.cc#kKeyword.
 * @param {boolean=} opt_includeInstance Deprecated. whether to include the
 *     JSPB instance for transitional soy proto support:
 *     http://goto/soy-param-migration
 * @return {!Object}
 */
proto.sensen.finance.HedgeRequest.prototype.toObject = function(opt_includeInstance) {
  return proto.sensen.finance.HedgeRequest.toObject(opt_includeInstance, this);
};


/**
 * Static version of the {@see toObject} method.
 * @param {boolean|undefined} includeInstance Deprecated. Whether to include
 *     the JSPB instance for transitional soy proto support:
 *     http://goto/soy-param-migration
 * @param {!proto.sensen.finance.HedgeRequest} msg The msg instance to transform.
 * @return {!Object}
 * @suppress {unusedLocalVariables} f is only used for nested messages
 */
proto.sensen.finance.HedgeRequest.toObject = function(includeInstance, msg) {
  var f, obj = {
    assetVolatility: jspb.Message.getFloatingPointFieldWithDefault(msg, 1, 0.0),
    futuresVolatility: jspb.Message.getFloatingPointFieldWithDefault(msg, 2, 0.0),
    correlation: jspb.Message.getFloatingPointFieldWithDefault(msg, 3, 0.0),
    spotValue: jspb.Message.getFloatingPointFieldWithDefault(msg, 4, 0.0),
    contractMultiplier: jspb.Message.getFloatingPointFieldWithDefault(msg, 5, 0.0),
    futuresPrice: jspb.Message.getFloatingPointFieldWithDefault(msg, 6, 0.0)
  };

  if (includeInstance) {
    obj.$jspbMessageInstance = msg;
  }
  return obj;
};
}


/**
 * Deserializes binary data (in protobuf wire format).
 * @param {jspb.ByteSource} bytes The bytes to deserialize.
 * @return {!proto.sensen.finance.HedgeRequest}
 */
proto.sensen.finance.HedgeRequest.deserializeBinary = function(bytes) {
  var reader = new jspb.BinaryReader(bytes);
  var msg = new proto.sensen.finance.HedgeRequest;
  return proto.sensen.finance.HedgeRequest.deserializeBinaryFromReader(msg, reader);
};


/**
 * Deserializes binary data (in protobuf wire format) from the
 * given reader into the given message object.
 * @param {!proto.sensen.finance.HedgeRequest} msg The message object to deserialize into.
 * @param {!jspb.BinaryReader} reader The BinaryReader to use.
 * @return {!proto.sensen.finance.HedgeRequest}
 */
proto.sensen.finance.HedgeRequest.deserializeBinaryFromReader = function(msg, reader) {
  while (reader.nextField()) {
    if (reader.isEndGroup()) {
      break;
    }
    var field = reader.getFieldNumber();
    switch (field) {
    case 1:
      var value = /** @type {number} */ (reader.readDouble());
      msg.setAssetVolatility(value);
      break;
    case 2:
      var value = /** @type {number} */ (reader.readDouble());
      msg.setFuturesVolatility(value);
      break;
    case 3:
      var value = /** @type {number} */ (reader.readDouble());
      msg.setCorrelation(value);
      break;
    case 4:
      var value = /** @type {number} */ (reader.readDouble());
      msg.setSpotValue(value);
      break;
    case 5:
      var value = /** @type {number} */ (reader.readDouble());
      msg.setContractMultiplier(value);
      break;
    case 6:
      var value = /** @type {number} */ (reader.readDouble());
      msg.setFuturesPrice(value);
      break;
    default:
      reader.skipField();
      break;
    }
  }
  return msg;
};


/**
 * Serializes the message to binary data (in protobuf wire format).
 * @return {!Uint8Array}
 */
proto.sensen.finance.HedgeRequest.prototype.serializeBinary = function() {
  var writer = new jspb.BinaryWriter();
  proto.sensen.finance.HedgeRequest.serializeBinaryToWriter(this, writer);
  return writer.getResultBuffer();
};


/**
 * Serializes the given message to binary data (in protobuf wire
 * format), writing to the given BinaryWriter.
 * @param {!proto.sensen.finance.HedgeRequest} message
 * @param {!jspb.BinaryWriter} writer
 * @suppress {unusedLocalVariables} f is only used for nested messages
 */
proto.sensen.finance.HedgeRequest.serializeBinaryToWriter = function(message, writer) {
  var f = undefined;
  f = message.getAssetVolatility();
  if (f !== 0.0) {
    writer.writeDouble(
      1,
      f
    );
  }
  f = message.getFuturesVolatility();
  if (f !== 0.0) {
    writer.writeDouble(
      2,
      f
    );
  }
  f = message.getCorrelation();
  if (f !== 0.0) {
    writer.writeDouble(
      3,
      f
    );
  }
  f = message.getSpotValue();
  if (f !== 0.0) {
    writer.writeDouble(
      4,
      f
    );
  }
  f = message.getContractMultiplier();
  if (f !== 0.0) {
    writer.writeDouble(
      5,
      f
    );
  }
  f = message.getFuturesPrice();
  if (f !== 0.0) {
    writer.writeDouble(
      6,
      f
    );
  }
};


/**
 * optional double asset_volatility = 1;
 * @return {number}
 */
proto.sensen.finance.HedgeRequest.prototype.getAssetVolatility = function() {
  return /** @type {number} */ (jspb.Message.getFloatingPointFieldWithDefault(this, 1, 0.0));
};


/**
 * @param {number} value
 * @return {!proto.sensen.finance.HedgeRequest} returns this
 */
proto.sensen.finance.HedgeRequest.prototype.setAssetVolatility = function(value) {
  return jspb.Message.setProto3FloatField(this, 1, value);
};


/**
 * optional double futures_volatility = 2;
 * @return {number}
 */
proto.sensen.finance.HedgeRequest.prototype.getFuturesVolatility = function() {
  return /** @type {number} */ (jspb.Message.getFloatingPointFieldWithDefault(this, 2, 0.0));
};


/**
 * @param {number} value
 * @return {!proto.sensen.finance.HedgeRequest} returns this
 */
proto.sensen.finance.HedgeRequest.prototype.setFuturesVolatility = function(value) {
  return jspb.Message.setProto3FloatField(this, 2, value);
};


/**
 * optional double correlation = 3;
 * @return {number}
 */
proto.sensen.finance.HedgeRequest.prototype.getCorrelation = function() {
  return /** @type {number} */ (jspb.Message.getFloatingPointFieldWithDefault(this, 3, 0.0));
};


/**
 * @param {number} value
 * @return {!proto.sensen.finance.HedgeRequest} returns this
 */
proto.sensen.finance.HedgeRequest.prototype.setCorrelation = function(value) {
  return jspb.Message.setProto3FloatField(this, 3, value);
};


/**
 * optional double spot_value = 4;
 * @return {number}
 */
proto.sensen.finance.HedgeRequest.prototype.getSpotValue = function() {
  return /** @type {number} */ (jspb.Message.getFloatingPointFieldWithDefault(this, 4, 0.0));
};


/**
 * @param {number} value
 * @return {!proto.sensen.finance.HedgeRequest} returns this
 */
proto.sensen.finance.HedgeRequest.prototype.setSpotValue = function(value) {
  return jspb.Message.setProto3FloatField(this, 4, value);
};


/**
 * optional double contract_multiplier = 5;
 * @return {number}
 */
proto.sensen.finance.HedgeRequest.prototype.getContractMultiplier = function() {
  return /** @type {number} */ (jspb.Message.getFloatingPointFieldWithDefault(this, 5, 0.0));
};


/**
 * @param {number} value
 * @return {!proto.sensen.finance.HedgeRequest} returns this
 */
proto.sensen.finance.HedgeRequest.prototype.setContractMultiplier = function(value) {
  return jspb.Message.setProto3FloatField(this, 5, value);
};


/**
 * optional double futures_price = 6;
 * @return {number}
 */
proto.sensen.finance.HedgeRequest.prototype.getFuturesPrice = function() {
  return /** @type {number} */ (jspb.Message.getFloatingPointFieldWithDefault(this, 6, 0.0));
};


/**
 * @param {number} value
 * @return {!proto.sensen.finance.HedgeRequest} returns this
 */
proto.sensen.finance.HedgeRequest.prototype.setFuturesPrice = function(value) {
  return jspb.Message.setProto3FloatField(this, 6, value);
};





if (jspb.Message.GENERATE_TO_OBJECT) {
/**
 * Creates an object representation of this proto.
 * Field names that are reserved in JavaScript and will be renamed to pb_name.
 * Optional fields that are not set will be set to undefined.
 * To access a reserved field use, foo.pb_<name>, eg, foo.pb_default.
 * For the list of reserved names please see:
 *     net/proto2/compiler/js/internal/generator.cc#kKeyword.
 * @param {boolean=} opt_includeInstance Deprecated. whether to include the
 *     JSPB instance for transitional soy proto support:
 *     http://goto/soy-param-migration
 * @return {!Object}
 */
proto.sensen.finance.HedgeResponse.prototype.toObject = function(opt_includeInstance) {
  return proto.sensen.finance.HedgeResponse.toObject(opt_includeInstance, this);
};


/**
 * Static version of the {@see toObject} method.
 * @param {boolean|undefined} includeInstance Deprecated. Whether to include
 *     the JSPB instance for transitional soy proto support:
 *     http://goto/soy-param-migration
 * @param {!proto.sensen.finance.HedgeResponse} msg The msg instance to transform.
 * @return {!Object}
 * @suppress {unusedLocalVariables} f is only used for nested messages
 */
proto.sensen.finance.HedgeResponse.toObject = function(includeInstance, msg) {
  var f, obj = {
    hedgeRatio: jspb.Message.getFloatingPointFieldWithDefault(msg, 1, 0.0),
    contracts: jspb.Message.getFloatingPointFieldWithDefault(msg, 2, 0.0),
    contractsComputed: jspb.Message.getBooleanFieldWithDefault(msg, 3, false)
  };

  if (includeInstance) {
    obj.$jspbMessageInstance = msg;
  }
  return obj;
};
}


/**
 * Deserializes binary data (in protobuf wire format).
 * @param {jspb.ByteSource} bytes The bytes to deserialize.
 * @return {!proto.sensen.finance.HedgeResponse}
 */
proto.sensen.finance.HedgeResponse.deserializeBinary = function(bytes) {
  var reader = new jspb.BinaryReader(bytes);
  var msg = new proto.sensen.finance.HedgeResponse;
  return proto.sensen.finance.HedgeResponse.deserializeBinaryFromReader(msg, reader);
};


/**
 * Deserializes binary data (in protobuf wire format) from the
 * given reader into the given message object.
 * @param {!proto.sensen.finance.HedgeResponse} msg The message object to deserialize into.
 * @param {!jspb.BinaryReader} reader The BinaryReader to use.
 * @return {!proto.sensen.finance.HedgeResponse}
 */
proto.sensen.finance.HedgeResponse.deserializeBinaryFromReader = function(msg, reader) {
  while (reader.nextField()) {
    if (reader.isEndGroup()) {
      break;
    }
    var field = reader.getFieldNumber();
    switch (field) {
    case 1:
      var value = /** @type {number} */ (reader.readDouble());
      msg.setHedgeRatio(value);
      break;
    case 2:
      var value = /** @type {number} */ (reader.readDouble());
      msg.setContracts(value);
      break;
    case 3:
      var value = /** @type {boolean} */ (reader.readBool());
      msg.setContractsComputed(value);
      break;
    default:
      reader.skipField();
      break;
    }
  }
  return msg;
};


/**
 * Serializes the message to binary data (in protobuf wire format).
 * @return {!Uint8Array}
 */
proto.sensen.finance.HedgeResponse.prototype.serializeBinary = function() {
  var writer = new jspb.BinaryWriter();
  proto.sensen.finance.HedgeResponse.serializeBinaryToWriter(this, writer);
  return writer.getResultBuffer();
};


/**
 * Serializes the given message to binary data (in protobuf wire
 * format), writing to the given BinaryWriter.
 * @param {!proto.sensen.finance.HedgeResponse} message
 * @param {!jspb.BinaryWriter} writer
 * @suppress {unusedLocalVariables} f is only used for nested messages
 */
proto.sensen.finance.HedgeResponse.serializeBinaryToWriter = function(message, writer) {
  var f = undefined;
  f = message.getHedgeRatio();
  if (f !== 0.0) {
    writer.writeDouble(
      1,
      f
    );
  }
  f = message.getContracts();
  if (f !== 0.0) {
    writer.writeDouble(
      2,
      f
    );
  }
  f = message.getContractsComputed();
  if (f) {
    writer.writeBool(
      3,
      f
    );
  }
};


/**
 * optional double hedge_ratio = 1;
 * @return {number}
 */
proto.sensen.finance.HedgeResponse.prototype.getHedgeRatio = function() {
  return /** @type {number} */ (jspb.Message.getFloatingPointFieldWithDefault(this, 1, 0.0));
};


/**
 * @param {number} value
 * @return {!proto.sensen.finance.HedgeResponse} returns this
 */
proto.sensen.finance.HedgeResponse.prototype.setHedgeRatio = function(value) {
  return jspb.Message.setProto3FloatField(this, 1, value);
};


/**
 * optional double contracts = 2;
 * @return {number}
 */
proto.sensen.finance.HedgeResponse.prototype.getContracts = function() {
  return /** @type {number} */ (jspb.Message.getFloatingPointFieldWithDefault(this, 2, 0.0));
};


/**
 * @param {number} value
 * @return {!proto.sensen.finance.HedgeResponse} returns this
 */
proto.sensen.finance.HedgeResponse.prototype.setContracts = function(value) {
  return jspb.Message.setProto3FloatField(this, 2, value);
};


/**
 * optional bool contracts_computed = 3;
 * @return {boolean}
 */
proto.sensen.finance.HedgeResponse.prototype.getContractsComputed = function() {
  return /** @type {boolean} */ (jspb.Message.getBooleanFieldWithDefault(this, 3, false));
};


/**
 * @param {boolean} value
 * @return {!proto.sensen.finance.HedgeResponse} returns this
 */
proto.sensen.finance.HedgeResponse.prototype.setContractsComputed = function(value) {
  return jspb.Message.setProto3BooleanField(this, 3, value);
};





if (jspb.Message.GENERATE_TO_OBJECT) {
/**
 * Creates an object representation of this proto.
 * Field names that are reserved in JavaScript and will be renamed to pb_name.
 * Optional fields that are not set will be set to undefined.
 * To access a reserved field use, foo.pb_<name>, eg, foo.pb_default.
 * For the list of reserved names please see:
 *     net/proto2/compiler/js/internal/generator.cc#kKeyword.
 * @param {boolean=} opt_includeInstance Deprecated. whether to include the
 *     JSPB instance for transitional soy proto support:
 *     http://goto/soy-param-migration
 * @return {!Object}
 */
proto.sensen.finance.CommoditySpreadRequest.prototype.toObject = function(opt_includeInstance) {
  return proto.sensen.finance.CommoditySpreadRequest.toObject(opt_includeInstance, this);
};


/**
 * Static version of the {@see toObject} method.
 * @param {boolean|undefined} includeInstance Deprecated. Whether to include
 *     the JSPB instance for transitional soy proto support:
 *     http://goto/soy-param-migration
 * @param {!proto.sensen.finance.CommoditySpreadRequest} msg The msg instance to transform.
 * @return {!Object}
 * @suppress {unusedLocalVariables} f is only used for nested messages
 */
proto.sensen.finance.CommoditySpreadRequest.toObject = function(includeInstance, msg) {
  var f, obj = {
    spread: jspb.Message.getFieldWithDefault(msg, 1, 0),
    a: jspb.Message.getFloatingPointFieldWithDefault(msg, 2, 0.0),
    b: jspb.Message.getFloatingPointFieldWithDefault(msg, 3, 0.0),
    c: jspb.Message.getFloatingPointFieldWithDefault(msg, 4, 0.0)
  };

  if (includeInstance) {
    obj.$jspbMessageInstance = msg;
  }
  return obj;
};
}


/**
 * Deserializes binary data (in protobuf wire format).
 * @param {jspb.ByteSource} bytes The bytes to deserialize.
 * @return {!proto.sensen.finance.CommoditySpreadRequest}
 */
proto.sensen.finance.CommoditySpreadRequest.deserializeBinary = function(bytes) {
  var reader = new jspb.BinaryReader(bytes);
  var msg = new proto.sensen.finance.CommoditySpreadRequest;
  return proto.sensen.finance.CommoditySpreadRequest.deserializeBinaryFromReader(msg, reader);
};


/**
 * Deserializes binary data (in protobuf wire format) from the
 * given reader into the given message object.
 * @param {!proto.sensen.finance.CommoditySpreadRequest} msg The message object to deserialize into.
 * @param {!jspb.BinaryReader} reader The BinaryReader to use.
 * @return {!proto.sensen.finance.CommoditySpreadRequest}
 */
proto.sensen.finance.CommoditySpreadRequest.deserializeBinaryFromReader = function(msg, reader) {
  while (reader.nextField()) {
    if (reader.isEndGroup()) {
      break;
    }
    var field = reader.getFieldNumber();
    switch (field) {
    case 1:
      var value = /** @type {!proto.sensen.finance.CommoditySpreadRequest.Spread} */ (reader.readEnum());
      msg.setSpread(value);
      break;
    case 2:
      var value = /** @type {number} */ (reader.readDouble());
      msg.setA(value);
      break;
    case 3:
      var value = /** @type {number} */ (reader.readDouble());
      msg.setB(value);
      break;
    case 4:
      var value = /** @type {number} */ (reader.readDouble());
      msg.setC(value);
      break;
    default:
      reader.skipField();
      break;
    }
  }
  return msg;
};


/**
 * Serializes the message to binary data (in protobuf wire format).
 * @return {!Uint8Array}
 */
proto.sensen.finance.CommoditySpreadRequest.prototype.serializeBinary = function() {
  var writer = new jspb.BinaryWriter();
  proto.sensen.finance.CommoditySpreadRequest.serializeBinaryToWriter(this, writer);
  return writer.getResultBuffer();
};


/**
 * Serializes the given message to binary data (in protobuf wire
 * format), writing to the given BinaryWriter.
 * @param {!proto.sensen.finance.CommoditySpreadRequest} message
 * @param {!jspb.BinaryWriter} writer
 * @suppress {unusedLocalVariables} f is only used for nested messages
 */
proto.sensen.finance.CommoditySpreadRequest.serializeBinaryToWriter = function(message, writer) {
  var f = undefined;
  f = message.getSpread();
  if (f !== 0.0) {
    writer.writeEnum(
      1,
      f
    );
  }
  f = message.getA();
  if (f !== 0.0) {
    writer.writeDouble(
      2,
      f
    );
  }
  f = message.getB();
  if (f !== 0.0) {
    writer.writeDouble(
      3,
      f
    );
  }
  f = message.getC();
  if (f !== 0.0) {
    writer.writeDouble(
      4,
      f
    );
  }
};


/**
 * @enum {number}
 */
proto.sensen.finance.CommoditySpreadRequest.Spread = {
  CRACK_321: 0,
  SPARK: 1,
  CRUSH: 2
};

/**
 * optional Spread spread = 1;
 * @return {!proto.sensen.finance.CommoditySpreadRequest.Spread}
 */
proto.sensen.finance.CommoditySpreadRequest.prototype.getSpread = function() {
  return /** @type {!proto.sensen.finance.CommoditySpreadRequest.Spread} */ (jspb.Message.getFieldWithDefault(this, 1, 0));
};


/**
 * @param {!proto.sensen.finance.CommoditySpreadRequest.Spread} value
 * @return {!proto.sensen.finance.CommoditySpreadRequest} returns this
 */
proto.sensen.finance.CommoditySpreadRequest.prototype.setSpread = function(value) {
  return jspb.Message.setProto3EnumField(this, 1, value);
};


/**
 * optional double a = 2;
 * @return {number}
 */
proto.sensen.finance.CommoditySpreadRequest.prototype.getA = function() {
  return /** @type {number} */ (jspb.Message.getFloatingPointFieldWithDefault(this, 2, 0.0));
};


/**
 * @param {number} value
 * @return {!proto.sensen.finance.CommoditySpreadRequest} returns this
 */
proto.sensen.finance.CommoditySpreadRequest.prototype.setA = function(value) {
  return jspb.Message.setProto3FloatField(this, 2, value);
};


/**
 * optional double b = 3;
 * @return {number}
 */
proto.sensen.finance.CommoditySpreadRequest.prototype.getB = function() {
  return /** @type {number} */ (jspb.Message.getFloatingPointFieldWithDefault(this, 3, 0.0));
};


/**
 * @param {number} value
 * @return {!proto.sensen.finance.CommoditySpreadRequest} returns this
 */
proto.sensen.finance.CommoditySpreadRequest.prototype.setB = function(value) {
  return jspb.Message.setProto3FloatField(this, 3, value);
};


/**
 * optional double c = 4;
 * @return {number}
 */
proto.sensen.finance.CommoditySpreadRequest.prototype.getC = function() {
  return /** @type {number} */ (jspb.Message.getFloatingPointFieldWithDefault(this, 4, 0.0));
};


/**
 * @param {number} value
 * @return {!proto.sensen.finance.CommoditySpreadRequest} returns this
 */
proto.sensen.finance.CommoditySpreadRequest.prototype.setC = function(value) {
  return jspb.Message.setProto3FloatField(this, 4, value);
};





if (jspb.Message.GENERATE_TO_OBJECT) {
/**
 * Creates an object representation of this proto.
 * Field names that are reserved in JavaScript and will be renamed to pb_name.
 * Optional fields that are not set will be set to undefined.
 * To access a reserved field use, foo.pb_<name>, eg, foo.pb_default.
 * For the list of reserved names please see:
 *     net/proto2/compiler/js/internal/generator.cc#kKeyword.
 * @param {boolean=} opt_includeInstance Deprecated. whether to include the
 *     JSPB instance for transitional soy proto support:
 *     http://goto/soy-param-migration
 * @return {!Object}
 */
proto.sensen.finance.RentalRoiRequest.prototype.toObject = function(opt_includeInstance) {
  return proto.sensen.finance.RentalRoiRequest.toObject(opt_includeInstance, this);
};


/**
 * Static version of the {@see toObject} method.
 * @param {boolean|undefined} includeInstance Deprecated. Whether to include
 *     the JSPB instance for transitional soy proto support:
 *     http://goto/soy-param-migration
 * @param {!proto.sensen.finance.RentalRoiRequest} msg The msg instance to transform.
 * @return {!Object}
 * @suppress {unusedLocalVariables} f is only used for nested messages
 */
proto.sensen.finance.RentalRoiRequest.toObject = function(includeInstance, msg) {
  var f, obj = {
    propertyValue: jspb.Message.getFieldWithDefault(msg, 1, ""),
    totalCashInvested: jspb.Message.getFieldWithDefault(msg, 2, ""),
    periodicGrossRent: jspb.Message.getFieldWithDefault(msg, 3, ""),
    periodicOperatingExpenses: jspb.Message.getFieldWithDefault(msg, 4, ""),
    periodicMortgagePayment: jspb.Message.getFieldWithDefault(msg, 5, ""),
    periodsPerYear: jspb.Message.getFieldWithDefault(msg, 6, 0)
  };

  if (includeInstance) {
    obj.$jspbMessageInstance = msg;
  }
  return obj;
};
}


/**
 * Deserializes binary data (in protobuf wire format).
 * @param {jspb.ByteSource} bytes The bytes to deserialize.
 * @return {!proto.sensen.finance.RentalRoiRequest}
 */
proto.sensen.finance.RentalRoiRequest.deserializeBinary = function(bytes) {
  var reader = new jspb.BinaryReader(bytes);
  var msg = new proto.sensen.finance.RentalRoiRequest;
  return proto.sensen.finance.RentalRoiRequest.deserializeBinaryFromReader(msg, reader);
};


/**
 * Deserializes binary data (in protobuf wire format) from the
 * given reader into the given message object.
 * @param {!proto.sensen.finance.RentalRoiRequest} msg The message object to deserialize into.
 * @param {!jspb.BinaryReader} reader The BinaryReader to use.
 * @return {!proto.sensen.finance.RentalRoiRequest}
 */
proto.sensen.finance.RentalRoiRequest.deserializeBinaryFromReader = function(msg, reader) {
  while (reader.nextField()) {
    if (reader.isEndGroup()) {
      break;
    }
    var field = reader.getFieldNumber();
    switch (field) {
    case 1:
      var value = /** @type {string} */ (reader.readString());
      msg.setPropertyValue(value);
      break;
    case 2:
      var value = /** @type {string} */ (reader.readString());
      msg.setTotalCashInvested(value);
      break;
    case 3:
      var value = /** @type {string} */ (reader.readString());
      msg.setPeriodicGrossRent(value);
      break;
    case 4:
      var value = /** @type {string} */ (reader.readString());
      msg.setPeriodicOperatingExpenses(value);
      break;
    case 5:
      var value = /** @type {string} */ (reader.readString());
      msg.setPeriodicMortgagePayment(value);
      break;
    case 6:
      var value = /** @type {number} */ (reader.readInt32());
      msg.setPeriodsPerYear(value);
      break;
    default:
      reader.skipField();
      break;
    }
  }
  return msg;
};


/**
 * Serializes the message to binary data (in protobuf wire format).
 * @return {!Uint8Array}
 */
proto.sensen.finance.RentalRoiRequest.prototype.serializeBinary = function() {
  var writer = new jspb.BinaryWriter();
  proto.sensen.finance.RentalRoiRequest.serializeBinaryToWriter(this, writer);
  return writer.getResultBuffer();
};


/**
 * Serializes the given message to binary data (in protobuf wire
 * format), writing to the given BinaryWriter.
 * @param {!proto.sensen.finance.RentalRoiRequest} message
 * @param {!jspb.BinaryWriter} writer
 * @suppress {unusedLocalVariables} f is only used for nested messages
 */
proto.sensen.finance.RentalRoiRequest.serializeBinaryToWriter = function(message, writer) {
  var f = undefined;
  f = message.getPropertyValue();
  if (f.length > 0) {
    writer.writeString(
      1,
      f
    );
  }
  f = message.getTotalCashInvested();
  if (f.length > 0) {
    writer.writeString(
      2,
      f
    );
  }
  f = message.getPeriodicGrossRent();
  if (f.length > 0) {
    writer.writeString(
      3,
      f
    );
  }
  f = message.getPeriodicOperatingExpenses();
  if (f.length > 0) {
    writer.writeString(
      4,
      f
    );
  }
  f = message.getPeriodicMortgagePayment();
  if (f.length > 0) {
    writer.writeString(
      5,
      f
    );
  }
  f = message.getPeriodsPerYear();
  if (f !== 0) {
    writer.writeInt32(
      6,
      f
    );
  }
};


/**
 * optional string property_value = 1;
 * @return {string}
 */
proto.sensen.finance.RentalRoiRequest.prototype.getPropertyValue = function() {
  return /** @type {string} */ (jspb.Message.getFieldWithDefault(this, 1, ""));
};


/**
 * @param {string} value
 * @return {!proto.sensen.finance.RentalRoiRequest} returns this
 */
proto.sensen.finance.RentalRoiRequest.prototype.setPropertyValue = function(value) {
  return jspb.Message.setProto3StringField(this, 1, value);
};


/**
 * optional string total_cash_invested = 2;
 * @return {string}
 */
proto.sensen.finance.RentalRoiRequest.prototype.getTotalCashInvested = function() {
  return /** @type {string} */ (jspb.Message.getFieldWithDefault(this, 2, ""));
};


/**
 * @param {string} value
 * @return {!proto.sensen.finance.RentalRoiRequest} returns this
 */
proto.sensen.finance.RentalRoiRequest.prototype.setTotalCashInvested = function(value) {
  return jspb.Message.setProto3StringField(this, 2, value);
};


/**
 * optional string periodic_gross_rent = 3;
 * @return {string}
 */
proto.sensen.finance.RentalRoiRequest.prototype.getPeriodicGrossRent = function() {
  return /** @type {string} */ (jspb.Message.getFieldWithDefault(this, 3, ""));
};


/**
 * @param {string} value
 * @return {!proto.sensen.finance.RentalRoiRequest} returns this
 */
proto.sensen.finance.RentalRoiRequest.prototype.setPeriodicGrossRent = function(value) {
  return jspb.Message.setProto3StringField(this, 3, value);
};


/**
 * optional string periodic_operating_expenses = 4;
 * @return {string}
 */
proto.sensen.finance.RentalRoiRequest.prototype.getPeriodicOperatingExpenses = function() {
  return /** @type {string} */ (jspb.Message.getFieldWithDefault(this, 4, ""));
};


/**
 * @param {string} value
 * @return {!proto.sensen.finance.RentalRoiRequest} returns this
 */
proto.sensen.finance.RentalRoiRequest.prototype.setPeriodicOperatingExpenses = function(value) {
  return jspb.Message.setProto3StringField(this, 4, value);
};


/**
 * optional string periodic_mortgage_payment = 5;
 * @return {string}
 */
proto.sensen.finance.RentalRoiRequest.prototype.getPeriodicMortgagePayment = function() {
  return /** @type {string} */ (jspb.Message.getFieldWithDefault(this, 5, ""));
};


/**
 * @param {string} value
 * @return {!proto.sensen.finance.RentalRoiRequest} returns this
 */
proto.sensen.finance.RentalRoiRequest.prototype.setPeriodicMortgagePayment = function(value) {
  return jspb.Message.setProto3StringField(this, 5, value);
};


/**
 * optional int32 periods_per_year = 6;
 * @return {number}
 */
proto.sensen.finance.RentalRoiRequest.prototype.getPeriodsPerYear = function() {
  return /** @type {number} */ (jspb.Message.getFieldWithDefault(this, 6, 0));
};


/**
 * @param {number} value
 * @return {!proto.sensen.finance.RentalRoiRequest} returns this
 */
proto.sensen.finance.RentalRoiRequest.prototype.setPeriodsPerYear = function(value) {
  return jspb.Message.setProto3IntField(this, 6, value);
};





if (jspb.Message.GENERATE_TO_OBJECT) {
/**
 * Creates an object representation of this proto.
 * Field names that are reserved in JavaScript and will be renamed to pb_name.
 * Optional fields that are not set will be set to undefined.
 * To access a reserved field use, foo.pb_<name>, eg, foo.pb_default.
 * For the list of reserved names please see:
 *     net/proto2/compiler/js/internal/generator.cc#kKeyword.
 * @param {boolean=} opt_includeInstance Deprecated. whether to include the
 *     JSPB instance for transitional soy proto support:
 *     http://goto/soy-param-migration
 * @return {!Object}
 */
proto.sensen.finance.RentalRoiResponse.prototype.toObject = function(opt_includeInstance) {
  return proto.sensen.finance.RentalRoiResponse.toObject(opt_includeInstance, this);
};


/**
 * Static version of the {@see toObject} method.
 * @param {boolean|undefined} includeInstance Deprecated. Whether to include
 *     the JSPB instance for transitional soy proto support:
 *     http://goto/soy-param-migration
 * @param {!proto.sensen.finance.RentalRoiResponse} msg The msg instance to transform.
 * @return {!Object}
 * @suppress {unusedLocalVariables} f is only used for nested messages
 */
proto.sensen.finance.RentalRoiResponse.toObject = function(includeInstance, msg) {
  var f, obj = {
    netOperatingIncome: jspb.Message.getFieldWithDefault(msg, 1, ""),
    annualCashFlow: jspb.Message.getFieldWithDefault(msg, 2, ""),
    cashOnCashReturn: jspb.Message.getFieldWithDefault(msg, 3, ""),
    capRate: jspb.Message.getFieldWithDefault(msg, 4, ""),
    grossRentMultiplier: jspb.Message.getFieldWithDefault(msg, 5, "")
  };

  if (includeInstance) {
    obj.$jspbMessageInstance = msg;
  }
  return obj;
};
}


/**
 * Deserializes binary data (in protobuf wire format).
 * @param {jspb.ByteSource} bytes The bytes to deserialize.
 * @return {!proto.sensen.finance.RentalRoiResponse}
 */
proto.sensen.finance.RentalRoiResponse.deserializeBinary = function(bytes) {
  var reader = new jspb.BinaryReader(bytes);
  var msg = new proto.sensen.finance.RentalRoiResponse;
  return proto.sensen.finance.RentalRoiResponse.deserializeBinaryFromReader(msg, reader);
};


/**
 * Deserializes binary data (in protobuf wire format) from the
 * given reader into the given message object.
 * @param {!proto.sensen.finance.RentalRoiResponse} msg The message object to deserialize into.
 * @param {!jspb.BinaryReader} reader The BinaryReader to use.
 * @return {!proto.sensen.finance.RentalRoiResponse}
 */
proto.sensen.finance.RentalRoiResponse.deserializeBinaryFromReader = function(msg, reader) {
  while (reader.nextField()) {
    if (reader.isEndGroup()) {
      break;
    }
    var field = reader.getFieldNumber();
    switch (field) {
    case 1:
      var value = /** @type {string} */ (reader.readString());
      msg.setNetOperatingIncome(value);
      break;
    case 2:
      var value = /** @type {string} */ (reader.readString());
      msg.setAnnualCashFlow(value);
      break;
    case 3:
      var value = /** @type {string} */ (reader.readString());
      msg.setCashOnCashReturn(value);
      break;
    case 4:
      var value = /** @type {string} */ (reader.readString());
      msg.setCapRate(value);
      break;
    case 5:
      var value = /** @type {string} */ (reader.readString());
      msg.setGrossRentMultiplier(value);
      break;
    default:
      reader.skipField();
      break;
    }
  }
  return msg;
};


/**
 * Serializes the message to binary data (in protobuf wire format).
 * @return {!Uint8Array}
 */
proto.sensen.finance.RentalRoiResponse.prototype.serializeBinary = function() {
  var writer = new jspb.BinaryWriter();
  proto.sensen.finance.RentalRoiResponse.serializeBinaryToWriter(this, writer);
  return writer.getResultBuffer();
};


/**
 * Serializes the given message to binary data (in protobuf wire
 * format), writing to the given BinaryWriter.
 * @param {!proto.sensen.finance.RentalRoiResponse} message
 * @param {!jspb.BinaryWriter} writer
 * @suppress {unusedLocalVariables} f is only used for nested messages
 */
proto.sensen.finance.RentalRoiResponse.serializeBinaryToWriter = function(message, writer) {
  var f = undefined;
  f = message.getNetOperatingIncome();
  if (f.length > 0) {
    writer.writeString(
      1,
      f
    );
  }
  f = message.getAnnualCashFlow();
  if (f.length > 0) {
    writer.writeString(
      2,
      f
    );
  }
  f = message.getCashOnCashReturn();
  if (f.length > 0) {
    writer.writeString(
      3,
      f
    );
  }
  f = message.getCapRate();
  if (f.length > 0) {
    writer.writeString(
      4,
      f
    );
  }
  f = message.getGrossRentMultiplier();
  if (f.length > 0) {
    writer.writeString(
      5,
      f
    );
  }
};


/**
 * optional string net_operating_income = 1;
 * @return {string}
 */
proto.sensen.finance.RentalRoiResponse.prototype.getNetOperatingIncome = function() {
  return /** @type {string} */ (jspb.Message.getFieldWithDefault(this, 1, ""));
};


/**
 * @param {string} value
 * @return {!proto.sensen.finance.RentalRoiResponse} returns this
 */
proto.sensen.finance.RentalRoiResponse.prototype.setNetOperatingIncome = function(value) {
  return jspb.Message.setProto3StringField(this, 1, value);
};


/**
 * optional string annual_cash_flow = 2;
 * @return {string}
 */
proto.sensen.finance.RentalRoiResponse.prototype.getAnnualCashFlow = function() {
  return /** @type {string} */ (jspb.Message.getFieldWithDefault(this, 2, ""));
};


/**
 * @param {string} value
 * @return {!proto.sensen.finance.RentalRoiResponse} returns this
 */
proto.sensen.finance.RentalRoiResponse.prototype.setAnnualCashFlow = function(value) {
  return jspb.Message.setProto3StringField(this, 2, value);
};


/**
 * optional string cash_on_cash_return = 3;
 * @return {string}
 */
proto.sensen.finance.RentalRoiResponse.prototype.getCashOnCashReturn = function() {
  return /** @type {string} */ (jspb.Message.getFieldWithDefault(this, 3, ""));
};


/**
 * @param {string} value
 * @return {!proto.sensen.finance.RentalRoiResponse} returns this
 */
proto.sensen.finance.RentalRoiResponse.prototype.setCashOnCashReturn = function(value) {
  return jspb.Message.setProto3StringField(this, 3, value);
};


/**
 * optional string cap_rate = 4;
 * @return {string}
 */
proto.sensen.finance.RentalRoiResponse.prototype.getCapRate = function() {
  return /** @type {string} */ (jspb.Message.getFieldWithDefault(this, 4, ""));
};


/**
 * @param {string} value
 * @return {!proto.sensen.finance.RentalRoiResponse} returns this
 */
proto.sensen.finance.RentalRoiResponse.prototype.setCapRate = function(value) {
  return jspb.Message.setProto3StringField(this, 4, value);
};


/**
 * optional string gross_rent_multiplier = 5;
 * @return {string}
 */
proto.sensen.finance.RentalRoiResponse.prototype.getGrossRentMultiplier = function() {
  return /** @type {string} */ (jspb.Message.getFieldWithDefault(this, 5, ""));
};


/**
 * @param {string} value
 * @return {!proto.sensen.finance.RentalRoiResponse} returns this
 */
proto.sensen.finance.RentalRoiResponse.prototype.setGrossRentMultiplier = function(value) {
  return jspb.Message.setProto3StringField(this, 5, value);
};





if (jspb.Message.GENERATE_TO_OBJECT) {
/**
 * Creates an object representation of this proto.
 * Field names that are reserved in JavaScript and will be renamed to pb_name.
 * Optional fields that are not set will be set to undefined.
 * To access a reserved field use, foo.pb_<name>, eg, foo.pb_default.
 * For the list of reserved names please see:
 *     net/proto2/compiler/js/internal/generator.cc#kKeyword.
 * @param {boolean=} opt_includeInstance Deprecated. whether to include the
 *     JSPB instance for transitional soy proto support:
 *     http://goto/soy-param-migration
 * @return {!Object}
 */
proto.sensen.finance.HomeFutureValueRequest.prototype.toObject = function(opt_includeInstance) {
  return proto.sensen.finance.HomeFutureValueRequest.toObject(opt_includeInstance, this);
};


/**
 * Static version of the {@see toObject} method.
 * @param {boolean|undefined} includeInstance Deprecated. Whether to include
 *     the JSPB instance for transitional soy proto support:
 *     http://goto/soy-param-migration
 * @param {!proto.sensen.finance.HomeFutureValueRequest} msg The msg instance to transform.
 * @return {!Object}
 * @suppress {unusedLocalVariables} f is only used for nested messages
 */
proto.sensen.finance.HomeFutureValueRequest.toObject = function(includeInstance, msg) {
  var f, obj = {
    currentPropertyValue: jspb.Message.getFieldWithDefault(msg, 1, ""),
    annualAppreciationRate: jspb.Message.getFieldWithDefault(msg, 2, ""),
    currentLoanBalance: jspb.Message.getFieldWithDefault(msg, 3, ""),
    annualMortgageRate: jspb.Message.getFieldWithDefault(msg, 4, ""),
    currentMonthlyPayment: jspb.Message.getFieldWithDefault(msg, 5, ""),
    targetYears: jspb.Message.getFieldWithDefault(msg, 6, 0),
    paymentsPerYear: jspb.Message.getFieldWithDefault(msg, 7, 0)
  };

  if (includeInstance) {
    obj.$jspbMessageInstance = msg;
  }
  return obj;
};
}


/**
 * Deserializes binary data (in protobuf wire format).
 * @param {jspb.ByteSource} bytes The bytes to deserialize.
 * @return {!proto.sensen.finance.HomeFutureValueRequest}
 */
proto.sensen.finance.HomeFutureValueRequest.deserializeBinary = function(bytes) {
  var reader = new jspb.BinaryReader(bytes);
  var msg = new proto.sensen.finance.HomeFutureValueRequest;
  return proto.sensen.finance.HomeFutureValueRequest.deserializeBinaryFromReader(msg, reader);
};


/**
 * Deserializes binary data (in protobuf wire format) from the
 * given reader into the given message object.
 * @param {!proto.sensen.finance.HomeFutureValueRequest} msg The message object to deserialize into.
 * @param {!jspb.BinaryReader} reader The BinaryReader to use.
 * @return {!proto.sensen.finance.HomeFutureValueRequest}
 */
proto.sensen.finance.HomeFutureValueRequest.deserializeBinaryFromReader = function(msg, reader) {
  while (reader.nextField()) {
    if (reader.isEndGroup()) {
      break;
    }
    var field = reader.getFieldNumber();
    switch (field) {
    case 1:
      var value = /** @type {string} */ (reader.readString());
      msg.setCurrentPropertyValue(value);
      break;
    case 2:
      var value = /** @type {string} */ (reader.readString());
      msg.setAnnualAppreciationRate(value);
      break;
    case 3:
      var value = /** @type {string} */ (reader.readString());
      msg.setCurrentLoanBalance(value);
      break;
    case 4:
      var value = /** @type {string} */ (reader.readString());
      msg.setAnnualMortgageRate(value);
      break;
    case 5:
      var value = /** @type {string} */ (reader.readString());
      msg.setCurrentMonthlyPayment(value);
      break;
    case 6:
      var value = /** @type {number} */ (reader.readInt32());
      msg.setTargetYears(value);
      break;
    case 7:
      var value = /** @type {number} */ (reader.readInt32());
      msg.setPaymentsPerYear(value);
      break;
    default:
      reader.skipField();
      break;
    }
  }
  return msg;
};


/**
 * Serializes the message to binary data (in protobuf wire format).
 * @return {!Uint8Array}
 */
proto.sensen.finance.HomeFutureValueRequest.prototype.serializeBinary = function() {
  var writer = new jspb.BinaryWriter();
  proto.sensen.finance.HomeFutureValueRequest.serializeBinaryToWriter(this, writer);
  return writer.getResultBuffer();
};


/**
 * Serializes the given message to binary data (in protobuf wire
 * format), writing to the given BinaryWriter.
 * @param {!proto.sensen.finance.HomeFutureValueRequest} message
 * @param {!jspb.BinaryWriter} writer
 * @suppress {unusedLocalVariables} f is only used for nested messages
 */
proto.sensen.finance.HomeFutureValueRequest.serializeBinaryToWriter = function(message, writer) {
  var f = undefined;
  f = message.getCurrentPropertyValue();
  if (f.length > 0) {
    writer.writeString(
      1,
      f
    );
  }
  f = message.getAnnualAppreciationRate();
  if (f.length > 0) {
    writer.writeString(
      2,
      f
    );
  }
  f = message.getCurrentLoanBalance();
  if (f.length > 0) {
    writer.writeString(
      3,
      f
    );
  }
  f = message.getAnnualMortgageRate();
  if (f.length > 0) {
    writer.writeString(
      4,
      f
    );
  }
  f = message.getCurrentMonthlyPayment();
  if (f.length > 0) {
    writer.writeString(
      5,
      f
    );
  }
  f = message.getTargetYears();
  if (f !== 0) {
    writer.writeInt32(
      6,
      f
    );
  }
  f = message.getPaymentsPerYear();
  if (f !== 0) {
    writer.writeInt32(
      7,
      f
    );
  }
};


/**
 * optional string current_property_value = 1;
 * @return {string}
 */
proto.sensen.finance.HomeFutureValueRequest.prototype.getCurrentPropertyValue = function() {
  return /** @type {string} */ (jspb.Message.getFieldWithDefault(this, 1, ""));
};


/**
 * @param {string} value
 * @return {!proto.sensen.finance.HomeFutureValueRequest} returns this
 */
proto.sensen.finance.HomeFutureValueRequest.prototype.setCurrentPropertyValue = function(value) {
  return jspb.Message.setProto3StringField(this, 1, value);
};


/**
 * optional string annual_appreciation_rate = 2;
 * @return {string}
 */
proto.sensen.finance.HomeFutureValueRequest.prototype.getAnnualAppreciationRate = function() {
  return /** @type {string} */ (jspb.Message.getFieldWithDefault(this, 2, ""));
};


/**
 * @param {string} value
 * @return {!proto.sensen.finance.HomeFutureValueRequest} returns this
 */
proto.sensen.finance.HomeFutureValueRequest.prototype.setAnnualAppreciationRate = function(value) {
  return jspb.Message.setProto3StringField(this, 2, value);
};


/**
 * optional string current_loan_balance = 3;
 * @return {string}
 */
proto.sensen.finance.HomeFutureValueRequest.prototype.getCurrentLoanBalance = function() {
  return /** @type {string} */ (jspb.Message.getFieldWithDefault(this, 3, ""));
};


/**
 * @param {string} value
 * @return {!proto.sensen.finance.HomeFutureValueRequest} returns this
 */
proto.sensen.finance.HomeFutureValueRequest.prototype.setCurrentLoanBalance = function(value) {
  return jspb.Message.setProto3StringField(this, 3, value);
};


/**
 * optional string annual_mortgage_rate = 4;
 * @return {string}
 */
proto.sensen.finance.HomeFutureValueRequest.prototype.getAnnualMortgageRate = function() {
  return /** @type {string} */ (jspb.Message.getFieldWithDefault(this, 4, ""));
};


/**
 * @param {string} value
 * @return {!proto.sensen.finance.HomeFutureValueRequest} returns this
 */
proto.sensen.finance.HomeFutureValueRequest.prototype.setAnnualMortgageRate = function(value) {
  return jspb.Message.setProto3StringField(this, 4, value);
};


/**
 * optional string current_monthly_payment = 5;
 * @return {string}
 */
proto.sensen.finance.HomeFutureValueRequest.prototype.getCurrentMonthlyPayment = function() {
  return /** @type {string} */ (jspb.Message.getFieldWithDefault(this, 5, ""));
};


/**
 * @param {string} value
 * @return {!proto.sensen.finance.HomeFutureValueRequest} returns this
 */
proto.sensen.finance.HomeFutureValueRequest.prototype.setCurrentMonthlyPayment = function(value) {
  return jspb.Message.setProto3StringField(this, 5, value);
};


/**
 * optional int32 target_years = 6;
 * @return {number}
 */
proto.sensen.finance.HomeFutureValueRequest.prototype.getTargetYears = function() {
  return /** @type {number} */ (jspb.Message.getFieldWithDefault(this, 6, 0));
};


/**
 * @param {number} value
 * @return {!proto.sensen.finance.HomeFutureValueRequest} returns this
 */
proto.sensen.finance.HomeFutureValueRequest.prototype.setTargetYears = function(value) {
  return jspb.Message.setProto3IntField(this, 6, value);
};


/**
 * optional int32 payments_per_year = 7;
 * @return {number}
 */
proto.sensen.finance.HomeFutureValueRequest.prototype.getPaymentsPerYear = function() {
  return /** @type {number} */ (jspb.Message.getFieldWithDefault(this, 7, 0));
};


/**
 * @param {number} value
 * @return {!proto.sensen.finance.HomeFutureValueRequest} returns this
 */
proto.sensen.finance.HomeFutureValueRequest.prototype.setPaymentsPerYear = function(value) {
  return jspb.Message.setProto3IntField(this, 7, value);
};





if (jspb.Message.GENERATE_TO_OBJECT) {
/**
 * Creates an object representation of this proto.
 * Field names that are reserved in JavaScript and will be renamed to pb_name.
 * Optional fields that are not set will be set to undefined.
 * To access a reserved field use, foo.pb_<name>, eg, foo.pb_default.
 * For the list of reserved names please see:
 *     net/proto2/compiler/js/internal/generator.cc#kKeyword.
 * @param {boolean=} opt_includeInstance Deprecated. whether to include the
 *     JSPB instance for transitional soy proto support:
 *     http://goto/soy-param-migration
 * @return {!Object}
 */
proto.sensen.finance.HomeFutureValueResponse.prototype.toObject = function(opt_includeInstance) {
  return proto.sensen.finance.HomeFutureValueResponse.toObject(opt_includeInstance, this);
};


/**
 * Static version of the {@see toObject} method.
 * @param {boolean|undefined} includeInstance Deprecated. Whether to include
 *     the JSPB instance for transitional soy proto support:
 *     http://goto/soy-param-migration
 * @param {!proto.sensen.finance.HomeFutureValueResponse} msg The msg instance to transform.
 * @return {!Object}
 * @suppress {unusedLocalVariables} f is only used for nested messages
 */
proto.sensen.finance.HomeFutureValueResponse.toObject = function(includeInstance, msg) {
  var f, obj = {
    futurePropertyValue: jspb.Message.getFloatingPointFieldWithDefault(msg, 1, 0.0),
    futureLoanBalance: jspb.Message.getFieldWithDefault(msg, 2, ""),
    futureEquity: jspb.Message.getFieldWithDefault(msg, 3, "")
  };

  if (includeInstance) {
    obj.$jspbMessageInstance = msg;
  }
  return obj;
};
}


/**
 * Deserializes binary data (in protobuf wire format).
 * @param {jspb.ByteSource} bytes The bytes to deserialize.
 * @return {!proto.sensen.finance.HomeFutureValueResponse}
 */
proto.sensen.finance.HomeFutureValueResponse.deserializeBinary = function(bytes) {
  var reader = new jspb.BinaryReader(bytes);
  var msg = new proto.sensen.finance.HomeFutureValueResponse;
  return proto.sensen.finance.HomeFutureValueResponse.deserializeBinaryFromReader(msg, reader);
};


/**
 * Deserializes binary data (in protobuf wire format) from the
 * given reader into the given message object.
 * @param {!proto.sensen.finance.HomeFutureValueResponse} msg The message object to deserialize into.
 * @param {!jspb.BinaryReader} reader The BinaryReader to use.
 * @return {!proto.sensen.finance.HomeFutureValueResponse}
 */
proto.sensen.finance.HomeFutureValueResponse.deserializeBinaryFromReader = function(msg, reader) {
  while (reader.nextField()) {
    if (reader.isEndGroup()) {
      break;
    }
    var field = reader.getFieldNumber();
    switch (field) {
    case 1:
      var value = /** @type {number} */ (reader.readDouble());
      msg.setFuturePropertyValue(value);
      break;
    case 2:
      var value = /** @type {string} */ (reader.readString());
      msg.setFutureLoanBalance(value);
      break;
    case 3:
      var value = /** @type {string} */ (reader.readString());
      msg.setFutureEquity(value);
      break;
    default:
      reader.skipField();
      break;
    }
  }
  return msg;
};


/**
 * Serializes the message to binary data (in protobuf wire format).
 * @return {!Uint8Array}
 */
proto.sensen.finance.HomeFutureValueResponse.prototype.serializeBinary = function() {
  var writer = new jspb.BinaryWriter();
  proto.sensen.finance.HomeFutureValueResponse.serializeBinaryToWriter(this, writer);
  return writer.getResultBuffer();
};


/**
 * Serializes the given message to binary data (in protobuf wire
 * format), writing to the given BinaryWriter.
 * @param {!proto.sensen.finance.HomeFutureValueResponse} message
 * @param {!jspb.BinaryWriter} writer
 * @suppress {unusedLocalVariables} f is only used for nested messages
 */
proto.sensen.finance.HomeFutureValueResponse.serializeBinaryToWriter = function(message, writer) {
  var f = undefined;
  f = message.getFuturePropertyValue();
  if (f !== 0.0) {
    writer.writeDouble(
      1,
      f
    );
  }
  f = message.getFutureLoanBalance();
  if (f.length > 0) {
    writer.writeString(
      2,
      f
    );
  }
  f = message.getFutureEquity();
  if (f.length > 0) {
    writer.writeString(
      3,
      f
    );
  }
};


/**
 * optional double future_property_value = 1;
 * @return {number}
 */
proto.sensen.finance.HomeFutureValueResponse.prototype.getFuturePropertyValue = function() {
  return /** @type {number} */ (jspb.Message.getFloatingPointFieldWithDefault(this, 1, 0.0));
};


/**
 * @param {number} value
 * @return {!proto.sensen.finance.HomeFutureValueResponse} returns this
 */
proto.sensen.finance.HomeFutureValueResponse.prototype.setFuturePropertyValue = function(value) {
  return jspb.Message.setProto3FloatField(this, 1, value);
};


/**
 * optional string future_loan_balance = 2;
 * @return {string}
 */
proto.sensen.finance.HomeFutureValueResponse.prototype.getFutureLoanBalance = function() {
  return /** @type {string} */ (jspb.Message.getFieldWithDefault(this, 2, ""));
};


/**
 * @param {string} value
 * @return {!proto.sensen.finance.HomeFutureValueResponse} returns this
 */
proto.sensen.finance.HomeFutureValueResponse.prototype.setFutureLoanBalance = function(value) {
  return jspb.Message.setProto3StringField(this, 2, value);
};


/**
 * optional string future_equity = 3;
 * @return {string}
 */
proto.sensen.finance.HomeFutureValueResponse.prototype.getFutureEquity = function() {
  return /** @type {string} */ (jspb.Message.getFieldWithDefault(this, 3, ""));
};


/**
 * @param {string} value
 * @return {!proto.sensen.finance.HomeFutureValueResponse} returns this
 */
proto.sensen.finance.HomeFutureValueResponse.prototype.setFutureEquity = function(value) {
  return jspb.Message.setProto3StringField(this, 3, value);
};





if (jspb.Message.GENERATE_TO_OBJECT) {
/**
 * Creates an object representation of this proto.
 * Field names that are reserved in JavaScript and will be renamed to pb_name.
 * Optional fields that are not set will be set to undefined.
 * To access a reserved field use, foo.pb_<name>, eg, foo.pb_default.
 * For the list of reserved names please see:
 *     net/proto2/compiler/js/internal/generator.cc#kKeyword.
 * @param {boolean=} opt_includeInstance Deprecated. whether to include the
 *     JSPB instance for transitional soy proto support:
 *     http://goto/soy-param-migration
 * @return {!Object}
 */
proto.sensen.finance.RentVsBuyRequest.prototype.toObject = function(opt_includeInstance) {
  return proto.sensen.finance.RentVsBuyRequest.toObject(opt_includeInstance, this);
};


/**
 * Static version of the {@see toObject} method.
 * @param {boolean|undefined} includeInstance Deprecated. Whether to include
 *     the JSPB instance for transitional soy proto support:
 *     http://goto/soy-param-migration
 * @param {!proto.sensen.finance.RentVsBuyRequest} msg The msg instance to transform.
 * @return {!Object}
 * @suppress {unusedLocalVariables} f is only used for nested messages
 */
proto.sensen.finance.RentVsBuyRequest.toObject = function(includeInstance, msg) {
  var f, obj = {
    propertyPrice: jspb.Message.getFieldWithDefault(msg, 1, ""),
    downPayment: jspb.Message.getFieldWithDefault(msg, 2, ""),
    monthlyPitiAndMaintenance: jspb.Message.getFieldWithDefault(msg, 3, ""),
    annualHomeAppreciation: jspb.Message.getFieldWithDefault(msg, 4, ""),
    currentMonthlyRent: jspb.Message.getFieldWithDefault(msg, 5, ""),
    annualRentIncrease: jspb.Message.getFieldWithDefault(msg, 6, ""),
    annualInvestmentReturn: jspb.Message.getFieldWithDefault(msg, 7, ""),
    years: jspb.Message.getFieldWithDefault(msg, 8, 0)
  };

  if (includeInstance) {
    obj.$jspbMessageInstance = msg;
  }
  return obj;
};
}


/**
 * Deserializes binary data (in protobuf wire format).
 * @param {jspb.ByteSource} bytes The bytes to deserialize.
 * @return {!proto.sensen.finance.RentVsBuyRequest}
 */
proto.sensen.finance.RentVsBuyRequest.deserializeBinary = function(bytes) {
  var reader = new jspb.BinaryReader(bytes);
  var msg = new proto.sensen.finance.RentVsBuyRequest;
  return proto.sensen.finance.RentVsBuyRequest.deserializeBinaryFromReader(msg, reader);
};


/**
 * Deserializes binary data (in protobuf wire format) from the
 * given reader into the given message object.
 * @param {!proto.sensen.finance.RentVsBuyRequest} msg The message object to deserialize into.
 * @param {!jspb.BinaryReader} reader The BinaryReader to use.
 * @return {!proto.sensen.finance.RentVsBuyRequest}
 */
proto.sensen.finance.RentVsBuyRequest.deserializeBinaryFromReader = function(msg, reader) {
  while (reader.nextField()) {
    if (reader.isEndGroup()) {
      break;
    }
    var field = reader.getFieldNumber();
    switch (field) {
    case 1:
      var value = /** @type {string} */ (reader.readString());
      msg.setPropertyPrice(value);
      break;
    case 2:
      var value = /** @type {string} */ (reader.readString());
      msg.setDownPayment(value);
      break;
    case 3:
      var value = /** @type {string} */ (reader.readString());
      msg.setMonthlyPitiAndMaintenance(value);
      break;
    case 4:
      var value = /** @type {string} */ (reader.readString());
      msg.setAnnualHomeAppreciation(value);
      break;
    case 5:
      var value = /** @type {string} */ (reader.readString());
      msg.setCurrentMonthlyRent(value);
      break;
    case 6:
      var value = /** @type {string} */ (reader.readString());
      msg.setAnnualRentIncrease(value);
      break;
    case 7:
      var value = /** @type {string} */ (reader.readString());
      msg.setAnnualInvestmentReturn(value);
      break;
    case 8:
      var value = /** @type {number} */ (reader.readInt32());
      msg.setYears(value);
      break;
    default:
      reader.skipField();
      break;
    }
  }
  return msg;
};


/**
 * Serializes the message to binary data (in protobuf wire format).
 * @return {!Uint8Array}
 */
proto.sensen.finance.RentVsBuyRequest.prototype.serializeBinary = function() {
  var writer = new jspb.BinaryWriter();
  proto.sensen.finance.RentVsBuyRequest.serializeBinaryToWriter(this, writer);
  return writer.getResultBuffer();
};


/**
 * Serializes the given message to binary data (in protobuf wire
 * format), writing to the given BinaryWriter.
 * @param {!proto.sensen.finance.RentVsBuyRequest} message
 * @param {!jspb.BinaryWriter} writer
 * @suppress {unusedLocalVariables} f is only used for nested messages
 */
proto.sensen.finance.RentVsBuyRequest.serializeBinaryToWriter = function(message, writer) {
  var f = undefined;
  f = message.getPropertyPrice();
  if (f.length > 0) {
    writer.writeString(
      1,
      f
    );
  }
  f = message.getDownPayment();
  if (f.length > 0) {
    writer.writeString(
      2,
      f
    );
  }
  f = message.getMonthlyPitiAndMaintenance();
  if (f.length > 0) {
    writer.writeString(
      3,
      f
    );
  }
  f = message.getAnnualHomeAppreciation();
  if (f.length > 0) {
    writer.writeString(
      4,
      f
    );
  }
  f = message.getCurrentMonthlyRent();
  if (f.length > 0) {
    writer.writeString(
      5,
      f
    );
  }
  f = message.getAnnualRentIncrease();
  if (f.length > 0) {
    writer.writeString(
      6,
      f
    );
  }
  f = message.getAnnualInvestmentReturn();
  if (f.length > 0) {
    writer.writeString(
      7,
      f
    );
  }
  f = message.getYears();
  if (f !== 0) {
    writer.writeInt32(
      8,
      f
    );
  }
};


/**
 * optional string property_price = 1;
 * @return {string}
 */
proto.sensen.finance.RentVsBuyRequest.prototype.getPropertyPrice = function() {
  return /** @type {string} */ (jspb.Message.getFieldWithDefault(this, 1, ""));
};


/**
 * @param {string} value
 * @return {!proto.sensen.finance.RentVsBuyRequest} returns this
 */
proto.sensen.finance.RentVsBuyRequest.prototype.setPropertyPrice = function(value) {
  return jspb.Message.setProto3StringField(this, 1, value);
};


/**
 * optional string down_payment = 2;
 * @return {string}
 */
proto.sensen.finance.RentVsBuyRequest.prototype.getDownPayment = function() {
  return /** @type {string} */ (jspb.Message.getFieldWithDefault(this, 2, ""));
};


/**
 * @param {string} value
 * @return {!proto.sensen.finance.RentVsBuyRequest} returns this
 */
proto.sensen.finance.RentVsBuyRequest.prototype.setDownPayment = function(value) {
  return jspb.Message.setProto3StringField(this, 2, value);
};


/**
 * optional string monthly_piti_and_maintenance = 3;
 * @return {string}
 */
proto.sensen.finance.RentVsBuyRequest.prototype.getMonthlyPitiAndMaintenance = function() {
  return /** @type {string} */ (jspb.Message.getFieldWithDefault(this, 3, ""));
};


/**
 * @param {string} value
 * @return {!proto.sensen.finance.RentVsBuyRequest} returns this
 */
proto.sensen.finance.RentVsBuyRequest.prototype.setMonthlyPitiAndMaintenance = function(value) {
  return jspb.Message.setProto3StringField(this, 3, value);
};


/**
 * optional string annual_home_appreciation = 4;
 * @return {string}
 */
proto.sensen.finance.RentVsBuyRequest.prototype.getAnnualHomeAppreciation = function() {
  return /** @type {string} */ (jspb.Message.getFieldWithDefault(this, 4, ""));
};


/**
 * @param {string} value
 * @return {!proto.sensen.finance.RentVsBuyRequest} returns this
 */
proto.sensen.finance.RentVsBuyRequest.prototype.setAnnualHomeAppreciation = function(value) {
  return jspb.Message.setProto3StringField(this, 4, value);
};


/**
 * optional string current_monthly_rent = 5;
 * @return {string}
 */
proto.sensen.finance.RentVsBuyRequest.prototype.getCurrentMonthlyRent = function() {
  return /** @type {string} */ (jspb.Message.getFieldWithDefault(this, 5, ""));
};


/**
 * @param {string} value
 * @return {!proto.sensen.finance.RentVsBuyRequest} returns this
 */
proto.sensen.finance.RentVsBuyRequest.prototype.setCurrentMonthlyRent = function(value) {
  return jspb.Message.setProto3StringField(this, 5, value);
};


/**
 * optional string annual_rent_increase = 6;
 * @return {string}
 */
proto.sensen.finance.RentVsBuyRequest.prototype.getAnnualRentIncrease = function() {
  return /** @type {string} */ (jspb.Message.getFieldWithDefault(this, 6, ""));
};


/**
 * @param {string} value
 * @return {!proto.sensen.finance.RentVsBuyRequest} returns this
 */
proto.sensen.finance.RentVsBuyRequest.prototype.setAnnualRentIncrease = function(value) {
  return jspb.Message.setProto3StringField(this, 6, value);
};


/**
 * optional string annual_investment_return = 7;
 * @return {string}
 */
proto.sensen.finance.RentVsBuyRequest.prototype.getAnnualInvestmentReturn = function() {
  return /** @type {string} */ (jspb.Message.getFieldWithDefault(this, 7, ""));
};


/**
 * @param {string} value
 * @return {!proto.sensen.finance.RentVsBuyRequest} returns this
 */
proto.sensen.finance.RentVsBuyRequest.prototype.setAnnualInvestmentReturn = function(value) {
  return jspb.Message.setProto3StringField(this, 7, value);
};


/**
 * optional int32 years = 8;
 * @return {number}
 */
proto.sensen.finance.RentVsBuyRequest.prototype.getYears = function() {
  return /** @type {number} */ (jspb.Message.getFieldWithDefault(this, 8, 0));
};


/**
 * @param {number} value
 * @return {!proto.sensen.finance.RentVsBuyRequest} returns this
 */
proto.sensen.finance.RentVsBuyRequest.prototype.setYears = function(value) {
  return jspb.Message.setProto3IntField(this, 8, value);
};





if (jspb.Message.GENERATE_TO_OBJECT) {
/**
 * Creates an object representation of this proto.
 * Field names that are reserved in JavaScript and will be renamed to pb_name.
 * Optional fields that are not set will be set to undefined.
 * To access a reserved field use, foo.pb_<name>, eg, foo.pb_default.
 * For the list of reserved names please see:
 *     net/proto2/compiler/js/internal/generator.cc#kKeyword.
 * @param {boolean=} opt_includeInstance Deprecated. whether to include the
 *     JSPB instance for transitional soy proto support:
 *     http://goto/soy-param-migration
 * @return {!Object}
 */
proto.sensen.finance.RentVsBuyResponse.prototype.toObject = function(opt_includeInstance) {
  return proto.sensen.finance.RentVsBuyResponse.toObject(opt_includeInstance, this);
};


/**
 * Static version of the {@see toObject} method.
 * @param {boolean|undefined} includeInstance Deprecated. Whether to include
 *     the JSPB instance for transitional soy proto support:
 *     http://goto/soy-param-migration
 * @param {!proto.sensen.finance.RentVsBuyResponse} msg The msg instance to transform.
 * @return {!Object}
 * @suppress {unusedLocalVariables} f is only used for nested messages
 */
proto.sensen.finance.RentVsBuyResponse.toObject = function(includeInstance, msg) {
  var f, obj = {
    totalCostOfBuying: jspb.Message.getFloatingPointFieldWithDefault(msg, 1, 0.0),
    totalCostOfRenting: jspb.Message.getFloatingPointFieldWithDefault(msg, 2, 0.0),
    isBuyingBetter: jspb.Message.getBooleanFieldWithDefault(msg, 3, false),
    buyingAdvantage: jspb.Message.getFloatingPointFieldWithDefault(msg, 4, 0.0)
  };

  if (includeInstance) {
    obj.$jspbMessageInstance = msg;
  }
  return obj;
};
}


/**
 * Deserializes binary data (in protobuf wire format).
 * @param {jspb.ByteSource} bytes The bytes to deserialize.
 * @return {!proto.sensen.finance.RentVsBuyResponse}
 */
proto.sensen.finance.RentVsBuyResponse.deserializeBinary = function(bytes) {
  var reader = new jspb.BinaryReader(bytes);
  var msg = new proto.sensen.finance.RentVsBuyResponse;
  return proto.sensen.finance.RentVsBuyResponse.deserializeBinaryFromReader(msg, reader);
};


/**
 * Deserializes binary data (in protobuf wire format) from the
 * given reader into the given message object.
 * @param {!proto.sensen.finance.RentVsBuyResponse} msg The message object to deserialize into.
 * @param {!jspb.BinaryReader} reader The BinaryReader to use.
 * @return {!proto.sensen.finance.RentVsBuyResponse}
 */
proto.sensen.finance.RentVsBuyResponse.deserializeBinaryFromReader = function(msg, reader) {
  while (reader.nextField()) {
    if (reader.isEndGroup()) {
      break;
    }
    var field = reader.getFieldNumber();
    switch (field) {
    case 1:
      var value = /** @type {number} */ (reader.readDouble());
      msg.setTotalCostOfBuying(value);
      break;
    case 2:
      var value = /** @type {number} */ (reader.readDouble());
      msg.setTotalCostOfRenting(value);
      break;
    case 3:
      var value = /** @type {boolean} */ (reader.readBool());
      msg.setIsBuyingBetter(value);
      break;
    case 4:
      var value = /** @type {number} */ (reader.readDouble());
      msg.setBuyingAdvantage(value);
      break;
    default:
      reader.skipField();
      break;
    }
  }
  return msg;
};


/**
 * Serializes the message to binary data (in protobuf wire format).
 * @return {!Uint8Array}
 */
proto.sensen.finance.RentVsBuyResponse.prototype.serializeBinary = function() {
  var writer = new jspb.BinaryWriter();
  proto.sensen.finance.RentVsBuyResponse.serializeBinaryToWriter(this, writer);
  return writer.getResultBuffer();
};


/**
 * Serializes the given message to binary data (in protobuf wire
 * format), writing to the given BinaryWriter.
 * @param {!proto.sensen.finance.RentVsBuyResponse} message
 * @param {!jspb.BinaryWriter} writer
 * @suppress {unusedLocalVariables} f is only used for nested messages
 */
proto.sensen.finance.RentVsBuyResponse.serializeBinaryToWriter = function(message, writer) {
  var f = undefined;
  f = message.getTotalCostOfBuying();
  if (f !== 0.0) {
    writer.writeDouble(
      1,
      f
    );
  }
  f = message.getTotalCostOfRenting();
  if (f !== 0.0) {
    writer.writeDouble(
      2,
      f
    );
  }
  f = message.getIsBuyingBetter();
  if (f) {
    writer.writeBool(
      3,
      f
    );
  }
  f = message.getBuyingAdvantage();
  if (f !== 0.0) {
    writer.writeDouble(
      4,
      f
    );
  }
};


/**
 * optional double total_cost_of_buying = 1;
 * @return {number}
 */
proto.sensen.finance.RentVsBuyResponse.prototype.getTotalCostOfBuying = function() {
  return /** @type {number} */ (jspb.Message.getFloatingPointFieldWithDefault(this, 1, 0.0));
};


/**
 * @param {number} value
 * @return {!proto.sensen.finance.RentVsBuyResponse} returns this
 */
proto.sensen.finance.RentVsBuyResponse.prototype.setTotalCostOfBuying = function(value) {
  return jspb.Message.setProto3FloatField(this, 1, value);
};


/**
 * optional double total_cost_of_renting = 2;
 * @return {number}
 */
proto.sensen.finance.RentVsBuyResponse.prototype.getTotalCostOfRenting = function() {
  return /** @type {number} */ (jspb.Message.getFloatingPointFieldWithDefault(this, 2, 0.0));
};


/**
 * @param {number} value
 * @return {!proto.sensen.finance.RentVsBuyResponse} returns this
 */
proto.sensen.finance.RentVsBuyResponse.prototype.setTotalCostOfRenting = function(value) {
  return jspb.Message.setProto3FloatField(this, 2, value);
};


/**
 * optional bool is_buying_better = 3;
 * @return {boolean}
 */
proto.sensen.finance.RentVsBuyResponse.prototype.getIsBuyingBetter = function() {
  return /** @type {boolean} */ (jspb.Message.getBooleanFieldWithDefault(this, 3, false));
};


/**
 * @param {boolean} value
 * @return {!proto.sensen.finance.RentVsBuyResponse} returns this
 */
proto.sensen.finance.RentVsBuyResponse.prototype.setIsBuyingBetter = function(value) {
  return jspb.Message.setProto3BooleanField(this, 3, value);
};


/**
 * optional double buying_advantage = 4;
 * @return {number}
 */
proto.sensen.finance.RentVsBuyResponse.prototype.getBuyingAdvantage = function() {
  return /** @type {number} */ (jspb.Message.getFloatingPointFieldWithDefault(this, 4, 0.0));
};


/**
 * @param {number} value
 * @return {!proto.sensen.finance.RentVsBuyResponse} returns this
 */
proto.sensen.finance.RentVsBuyResponse.prototype.setBuyingAdvantage = function(value) {
  return jspb.Message.setProto3FloatField(this, 4, value);
};





if (jspb.Message.GENERATE_TO_OBJECT) {
/**
 * Creates an object representation of this proto.
 * Field names that are reserved in JavaScript and will be renamed to pb_name.
 * Optional fields that are not set will be set to undefined.
 * To access a reserved field use, foo.pb_<name>, eg, foo.pb_default.
 * For the list of reserved names please see:
 *     net/proto2/compiler/js/internal/generator.cc#kKeyword.
 * @param {boolean=} opt_includeInstance Deprecated. whether to include the
 *     JSPB instance for transitional soy proto support:
 *     http://goto/soy-param-migration
 * @return {!Object}
 */
proto.sensen.finance.HomeNpvRequest.prototype.toObject = function(opt_includeInstance) {
  return proto.sensen.finance.HomeNpvRequest.toObject(opt_includeInstance, this);
};


/**
 * Static version of the {@see toObject} method.
 * @param {boolean|undefined} includeInstance Deprecated. Whether to include
 *     the JSPB instance for transitional soy proto support:
 *     http://goto/soy-param-migration
 * @param {!proto.sensen.finance.HomeNpvRequest} msg The msg instance to transform.
 * @return {!Object}
 * @suppress {unusedLocalVariables} f is only used for nested messages
 */
proto.sensen.finance.HomeNpvRequest.toObject = function(includeInstance, msg) {
  var f, obj = {
    propertyPrice: jspb.Message.getFieldWithDefault(msg, 1, ""),
    downPayment: jspb.Message.getFieldWithDefault(msg, 2, ""),
    closingCostsBuy: jspb.Message.getFieldWithDefault(msg, 3, ""),
    loanAmount: jspb.Message.getFieldWithDefault(msg, 4, ""),
    loanAnnualRate: jspb.Message.getFieldWithDefault(msg, 5, ""),
    loanTermYears: jspb.Message.getFieldWithDefault(msg, 6, 0),
    monthlyTaxesInsHoa: jspb.Message.getFieldWithDefault(msg, 7, ""),
    monthlyMaintenance: jspb.Message.getFieldWithDefault(msg, 8, ""),
    annualAppreciationRate: jspb.Message.getFieldWithDefault(msg, 9, ""),
    sellingClosingCostPercent: jspb.Message.getFieldWithDefault(msg, 10, ""),
    monthlyRentSaved: jspb.Message.getFieldWithDefault(msg, 11, ""),
    annualRentIncrease: jspb.Message.getFieldWithDefault(msg, 12, ""),
    annualDiscountRate: jspb.Message.getFieldWithDefault(msg, 13, ""),
    holdingPeriodYears: jspb.Message.getFieldWithDefault(msg, 14, 0)
  };

  if (includeInstance) {
    obj.$jspbMessageInstance = msg;
  }
  return obj;
};
}


/**
 * Deserializes binary data (in protobuf wire format).
 * @param {jspb.ByteSource} bytes The bytes to deserialize.
 * @return {!proto.sensen.finance.HomeNpvRequest}
 */
proto.sensen.finance.HomeNpvRequest.deserializeBinary = function(bytes) {
  var reader = new jspb.BinaryReader(bytes);
  var msg = new proto.sensen.finance.HomeNpvRequest;
  return proto.sensen.finance.HomeNpvRequest.deserializeBinaryFromReader(msg, reader);
};


/**
 * Deserializes binary data (in protobuf wire format) from the
 * given reader into the given message object.
 * @param {!proto.sensen.finance.HomeNpvRequest} msg The message object to deserialize into.
 * @param {!jspb.BinaryReader} reader The BinaryReader to use.
 * @return {!proto.sensen.finance.HomeNpvRequest}
 */
proto.sensen.finance.HomeNpvRequest.deserializeBinaryFromReader = function(msg, reader) {
  while (reader.nextField()) {
    if (reader.isEndGroup()) {
      break;
    }
    var field = reader.getFieldNumber();
    switch (field) {
    case 1:
      var value = /** @type {string} */ (reader.readString());
      msg.setPropertyPrice(value);
      break;
    case 2:
      var value = /** @type {string} */ (reader.readString());
      msg.setDownPayment(value);
      break;
    case 3:
      var value = /** @type {string} */ (reader.readString());
      msg.setClosingCostsBuy(value);
      break;
    case 4:
      var value = /** @type {string} */ (reader.readString());
      msg.setLoanAmount(value);
      break;
    case 5:
      var value = /** @type {string} */ (reader.readString());
      msg.setLoanAnnualRate(value);
      break;
    case 6:
      var value = /** @type {number} */ (reader.readInt32());
      msg.setLoanTermYears(value);
      break;
    case 7:
      var value = /** @type {string} */ (reader.readString());
      msg.setMonthlyTaxesInsHoa(value);
      break;
    case 8:
      var value = /** @type {string} */ (reader.readString());
      msg.setMonthlyMaintenance(value);
      break;
    case 9:
      var value = /** @type {string} */ (reader.readString());
      msg.setAnnualAppreciationRate(value);
      break;
    case 10:
      var value = /** @type {string} */ (reader.readString());
      msg.setSellingClosingCostPercent(value);
      break;
    case 11:
      var value = /** @type {string} */ (reader.readString());
      msg.setMonthlyRentSaved(value);
      break;
    case 12:
      var value = /** @type {string} */ (reader.readString());
      msg.setAnnualRentIncrease(value);
      break;
    case 13:
      var value = /** @type {string} */ (reader.readString());
      msg.setAnnualDiscountRate(value);
      break;
    case 14:
      var value = /** @type {number} */ (reader.readInt32());
      msg.setHoldingPeriodYears(value);
      break;
    default:
      reader.skipField();
      break;
    }
  }
  return msg;
};


/**
 * Serializes the message to binary data (in protobuf wire format).
 * @return {!Uint8Array}
 */
proto.sensen.finance.HomeNpvRequest.prototype.serializeBinary = function() {
  var writer = new jspb.BinaryWriter();
  proto.sensen.finance.HomeNpvRequest.serializeBinaryToWriter(this, writer);
  return writer.getResultBuffer();
};


/**
 * Serializes the given message to binary data (in protobuf wire
 * format), writing to the given BinaryWriter.
 * @param {!proto.sensen.finance.HomeNpvRequest} message
 * @param {!jspb.BinaryWriter} writer
 * @suppress {unusedLocalVariables} f is only used for nested messages
 */
proto.sensen.finance.HomeNpvRequest.serializeBinaryToWriter = function(message, writer) {
  var f = undefined;
  f = message.getPropertyPrice();
  if (f.length > 0) {
    writer.writeString(
      1,
      f
    );
  }
  f = message.getDownPayment();
  if (f.length > 0) {
    writer.writeString(
      2,
      f
    );
  }
  f = message.getClosingCostsBuy();
  if (f.length > 0) {
    writer.writeString(
      3,
      f
    );
  }
  f = message.getLoanAmount();
  if (f.length > 0) {
    writer.writeString(
      4,
      f
    );
  }
  f = message.getLoanAnnualRate();
  if (f.length > 0) {
    writer.writeString(
      5,
      f
    );
  }
  f = message.getLoanTermYears();
  if (f !== 0) {
    writer.writeInt32(
      6,
      f
    );
  }
  f = message.getMonthlyTaxesInsHoa();
  if (f.length > 0) {
    writer.writeString(
      7,
      f
    );
  }
  f = message.getMonthlyMaintenance();
  if (f.length > 0) {
    writer.writeString(
      8,
      f
    );
  }
  f = message.getAnnualAppreciationRate();
  if (f.length > 0) {
    writer.writeString(
      9,
      f
    );
  }
  f = message.getSellingClosingCostPercent();
  if (f.length > 0) {
    writer.writeString(
      10,
      f
    );
  }
  f = message.getMonthlyRentSaved();
  if (f.length > 0) {
    writer.writeString(
      11,
      f
    );
  }
  f = message.getAnnualRentIncrease();
  if (f.length > 0) {
    writer.writeString(
      12,
      f
    );
  }
  f = message.getAnnualDiscountRate();
  if (f.length > 0) {
    writer.writeString(
      13,
      f
    );
  }
  f = message.getHoldingPeriodYears();
  if (f !== 0) {
    writer.writeInt32(
      14,
      f
    );
  }
};


/**
 * optional string property_price = 1;
 * @return {string}
 */
proto.sensen.finance.HomeNpvRequest.prototype.getPropertyPrice = function() {
  return /** @type {string} */ (jspb.Message.getFieldWithDefault(this, 1, ""));
};


/**
 * @param {string} value
 * @return {!proto.sensen.finance.HomeNpvRequest} returns this
 */
proto.sensen.finance.HomeNpvRequest.prototype.setPropertyPrice = function(value) {
  return jspb.Message.setProto3StringField(this, 1, value);
};


/**
 * optional string down_payment = 2;
 * @return {string}
 */
proto.sensen.finance.HomeNpvRequest.prototype.getDownPayment = function() {
  return /** @type {string} */ (jspb.Message.getFieldWithDefault(this, 2, ""));
};


/**
 * @param {string} value
 * @return {!proto.sensen.finance.HomeNpvRequest} returns this
 */
proto.sensen.finance.HomeNpvRequest.prototype.setDownPayment = function(value) {
  return jspb.Message.setProto3StringField(this, 2, value);
};


/**
 * optional string closing_costs_buy = 3;
 * @return {string}
 */
proto.sensen.finance.HomeNpvRequest.prototype.getClosingCostsBuy = function() {
  return /** @type {string} */ (jspb.Message.getFieldWithDefault(this, 3, ""));
};


/**
 * @param {string} value
 * @return {!proto.sensen.finance.HomeNpvRequest} returns this
 */
proto.sensen.finance.HomeNpvRequest.prototype.setClosingCostsBuy = function(value) {
  return jspb.Message.setProto3StringField(this, 3, value);
};


/**
 * optional string loan_amount = 4;
 * @return {string}
 */
proto.sensen.finance.HomeNpvRequest.prototype.getLoanAmount = function() {
  return /** @type {string} */ (jspb.Message.getFieldWithDefault(this, 4, ""));
};


/**
 * @param {string} value
 * @return {!proto.sensen.finance.HomeNpvRequest} returns this
 */
proto.sensen.finance.HomeNpvRequest.prototype.setLoanAmount = function(value) {
  return jspb.Message.setProto3StringField(this, 4, value);
};


/**
 * optional string loan_annual_rate = 5;
 * @return {string}
 */
proto.sensen.finance.HomeNpvRequest.prototype.getLoanAnnualRate = function() {
  return /** @type {string} */ (jspb.Message.getFieldWithDefault(this, 5, ""));
};


/**
 * @param {string} value
 * @return {!proto.sensen.finance.HomeNpvRequest} returns this
 */
proto.sensen.finance.HomeNpvRequest.prototype.setLoanAnnualRate = function(value) {
  return jspb.Message.setProto3StringField(this, 5, value);
};


/**
 * optional int32 loan_term_years = 6;
 * @return {number}
 */
proto.sensen.finance.HomeNpvRequest.prototype.getLoanTermYears = function() {
  return /** @type {number} */ (jspb.Message.getFieldWithDefault(this, 6, 0));
};


/**
 * @param {number} value
 * @return {!proto.sensen.finance.HomeNpvRequest} returns this
 */
proto.sensen.finance.HomeNpvRequest.prototype.setLoanTermYears = function(value) {
  return jspb.Message.setProto3IntField(this, 6, value);
};


/**
 * optional string monthly_taxes_ins_hoa = 7;
 * @return {string}
 */
proto.sensen.finance.HomeNpvRequest.prototype.getMonthlyTaxesInsHoa = function() {
  return /** @type {string} */ (jspb.Message.getFieldWithDefault(this, 7, ""));
};


/**
 * @param {string} value
 * @return {!proto.sensen.finance.HomeNpvRequest} returns this
 */
proto.sensen.finance.HomeNpvRequest.prototype.setMonthlyTaxesInsHoa = function(value) {
  return jspb.Message.setProto3StringField(this, 7, value);
};


/**
 * optional string monthly_maintenance = 8;
 * @return {string}
 */
proto.sensen.finance.HomeNpvRequest.prototype.getMonthlyMaintenance = function() {
  return /** @type {string} */ (jspb.Message.getFieldWithDefault(this, 8, ""));
};


/**
 * @param {string} value
 * @return {!proto.sensen.finance.HomeNpvRequest} returns this
 */
proto.sensen.finance.HomeNpvRequest.prototype.setMonthlyMaintenance = function(value) {
  return jspb.Message.setProto3StringField(this, 8, value);
};


/**
 * optional string annual_appreciation_rate = 9;
 * @return {string}
 */
proto.sensen.finance.HomeNpvRequest.prototype.getAnnualAppreciationRate = function() {
  return /** @type {string} */ (jspb.Message.getFieldWithDefault(this, 9, ""));
};


/**
 * @param {string} value
 * @return {!proto.sensen.finance.HomeNpvRequest} returns this
 */
proto.sensen.finance.HomeNpvRequest.prototype.setAnnualAppreciationRate = function(value) {
  return jspb.Message.setProto3StringField(this, 9, value);
};


/**
 * optional string selling_closing_cost_percent = 10;
 * @return {string}
 */
proto.sensen.finance.HomeNpvRequest.prototype.getSellingClosingCostPercent = function() {
  return /** @type {string} */ (jspb.Message.getFieldWithDefault(this, 10, ""));
};


/**
 * @param {string} value
 * @return {!proto.sensen.finance.HomeNpvRequest} returns this
 */
proto.sensen.finance.HomeNpvRequest.prototype.setSellingClosingCostPercent = function(value) {
  return jspb.Message.setProto3StringField(this, 10, value);
};


/**
 * optional string monthly_rent_saved = 11;
 * @return {string}
 */
proto.sensen.finance.HomeNpvRequest.prototype.getMonthlyRentSaved = function() {
  return /** @type {string} */ (jspb.Message.getFieldWithDefault(this, 11, ""));
};


/**
 * @param {string} value
 * @return {!proto.sensen.finance.HomeNpvRequest} returns this
 */
proto.sensen.finance.HomeNpvRequest.prototype.setMonthlyRentSaved = function(value) {
  return jspb.Message.setProto3StringField(this, 11, value);
};


/**
 * optional string annual_rent_increase = 12;
 * @return {string}
 */
proto.sensen.finance.HomeNpvRequest.prototype.getAnnualRentIncrease = function() {
  return /** @type {string} */ (jspb.Message.getFieldWithDefault(this, 12, ""));
};


/**
 * @param {string} value
 * @return {!proto.sensen.finance.HomeNpvRequest} returns this
 */
proto.sensen.finance.HomeNpvRequest.prototype.setAnnualRentIncrease = function(value) {
  return jspb.Message.setProto3StringField(this, 12, value);
};


/**
 * optional string annual_discount_rate = 13;
 * @return {string}
 */
proto.sensen.finance.HomeNpvRequest.prototype.getAnnualDiscountRate = function() {
  return /** @type {string} */ (jspb.Message.getFieldWithDefault(this, 13, ""));
};


/**
 * @param {string} value
 * @return {!proto.sensen.finance.HomeNpvRequest} returns this
 */
proto.sensen.finance.HomeNpvRequest.prototype.setAnnualDiscountRate = function(value) {
  return jspb.Message.setProto3StringField(this, 13, value);
};


/**
 * optional int32 holding_period_years = 14;
 * @return {number}
 */
proto.sensen.finance.HomeNpvRequest.prototype.getHoldingPeriodYears = function() {
  return /** @type {number} */ (jspb.Message.getFieldWithDefault(this, 14, 0));
};


/**
 * @param {number} value
 * @return {!proto.sensen.finance.HomeNpvRequest} returns this
 */
proto.sensen.finance.HomeNpvRequest.prototype.setHoldingPeriodYears = function(value) {
  return jspb.Message.setProto3IntField(this, 14, value);
};





if (jspb.Message.GENERATE_TO_OBJECT) {
/**
 * Creates an object representation of this proto.
 * Field names that are reserved in JavaScript and will be renamed to pb_name.
 * Optional fields that are not set will be set to undefined.
 * To access a reserved field use, foo.pb_<name>, eg, foo.pb_default.
 * For the list of reserved names please see:
 *     net/proto2/compiler/js/internal/generator.cc#kKeyword.
 * @param {boolean=} opt_includeInstance Deprecated. whether to include the
 *     JSPB instance for transitional soy proto support:
 *     http://goto/soy-param-migration
 * @return {!Object}
 */
proto.sensen.finance.HomeNpvResponse.prototype.toObject = function(opt_includeInstance) {
  return proto.sensen.finance.HomeNpvResponse.toObject(opt_includeInstance, this);
};


/**
 * Static version of the {@see toObject} method.
 * @param {boolean|undefined} includeInstance Deprecated. Whether to include
 *     the JSPB instance for transitional soy proto support:
 *     http://goto/soy-param-migration
 * @param {!proto.sensen.finance.HomeNpvResponse} msg The msg instance to transform.
 * @return {!Object}
 * @suppress {unusedLocalVariables} f is only used for nested messages
 */
proto.sensen.finance.HomeNpvResponse.toObject = function(includeInstance, msg) {
  var f, obj = {
    netPresentValue: jspb.Message.getFloatingPointFieldWithDefault(msg, 1, 0.0),
    internalRateOfReturn: jspb.Message.getFloatingPointFieldWithDefault(msg, 2, 0.0),
    futureSalePrice: jspb.Message.getFloatingPointFieldWithDefault(msg, 3, 0.0),
    futureEquity: jspb.Message.getFloatingPointFieldWithDefault(msg, 4, 0.0)
  };

  if (includeInstance) {
    obj.$jspbMessageInstance = msg;
  }
  return obj;
};
}


/**
 * Deserializes binary data (in protobuf wire format).
 * @param {jspb.ByteSource} bytes The bytes to deserialize.
 * @return {!proto.sensen.finance.HomeNpvResponse}
 */
proto.sensen.finance.HomeNpvResponse.deserializeBinary = function(bytes) {
  var reader = new jspb.BinaryReader(bytes);
  var msg = new proto.sensen.finance.HomeNpvResponse;
  return proto.sensen.finance.HomeNpvResponse.deserializeBinaryFromReader(msg, reader);
};


/**
 * Deserializes binary data (in protobuf wire format) from the
 * given reader into the given message object.
 * @param {!proto.sensen.finance.HomeNpvResponse} msg The message object to deserialize into.
 * @param {!jspb.BinaryReader} reader The BinaryReader to use.
 * @return {!proto.sensen.finance.HomeNpvResponse}
 */
proto.sensen.finance.HomeNpvResponse.deserializeBinaryFromReader = function(msg, reader) {
  while (reader.nextField()) {
    if (reader.isEndGroup()) {
      break;
    }
    var field = reader.getFieldNumber();
    switch (field) {
    case 1:
      var value = /** @type {number} */ (reader.readDouble());
      msg.setNetPresentValue(value);
      break;
    case 2:
      var value = /** @type {number} */ (reader.readDouble());
      msg.setInternalRateOfReturn(value);
      break;
    case 3:
      var value = /** @type {number} */ (reader.readDouble());
      msg.setFutureSalePrice(value);
      break;
    case 4:
      var value = /** @type {number} */ (reader.readDouble());
      msg.setFutureEquity(value);
      break;
    default:
      reader.skipField();
      break;
    }
  }
  return msg;
};


/**
 * Serializes the message to binary data (in protobuf wire format).
 * @return {!Uint8Array}
 */
proto.sensen.finance.HomeNpvResponse.prototype.serializeBinary = function() {
  var writer = new jspb.BinaryWriter();
  proto.sensen.finance.HomeNpvResponse.serializeBinaryToWriter(this, writer);
  return writer.getResultBuffer();
};


/**
 * Serializes the given message to binary data (in protobuf wire
 * format), writing to the given BinaryWriter.
 * @param {!proto.sensen.finance.HomeNpvResponse} message
 * @param {!jspb.BinaryWriter} writer
 * @suppress {unusedLocalVariables} f is only used for nested messages
 */
proto.sensen.finance.HomeNpvResponse.serializeBinaryToWriter = function(message, writer) {
  var f = undefined;
  f = message.getNetPresentValue();
  if (f !== 0.0) {
    writer.writeDouble(
      1,
      f
    );
  }
  f = message.getInternalRateOfReturn();
  if (f !== 0.0) {
    writer.writeDouble(
      2,
      f
    );
  }
  f = message.getFutureSalePrice();
  if (f !== 0.0) {
    writer.writeDouble(
      3,
      f
    );
  }
  f = message.getFutureEquity();
  if (f !== 0.0) {
    writer.writeDouble(
      4,
      f
    );
  }
};


/**
 * optional double net_present_value = 1;
 * @return {number}
 */
proto.sensen.finance.HomeNpvResponse.prototype.getNetPresentValue = function() {
  return /** @type {number} */ (jspb.Message.getFloatingPointFieldWithDefault(this, 1, 0.0));
};


/**
 * @param {number} value
 * @return {!proto.sensen.finance.HomeNpvResponse} returns this
 */
proto.sensen.finance.HomeNpvResponse.prototype.setNetPresentValue = function(value) {
  return jspb.Message.setProto3FloatField(this, 1, value);
};


/**
 * optional double internal_rate_of_return = 2;
 * @return {number}
 */
proto.sensen.finance.HomeNpvResponse.prototype.getInternalRateOfReturn = function() {
  return /** @type {number} */ (jspb.Message.getFloatingPointFieldWithDefault(this, 2, 0.0));
};


/**
 * @param {number} value
 * @return {!proto.sensen.finance.HomeNpvResponse} returns this
 */
proto.sensen.finance.HomeNpvResponse.prototype.setInternalRateOfReturn = function(value) {
  return jspb.Message.setProto3FloatField(this, 2, value);
};


/**
 * optional double future_sale_price = 3;
 * @return {number}
 */
proto.sensen.finance.HomeNpvResponse.prototype.getFutureSalePrice = function() {
  return /** @type {number} */ (jspb.Message.getFloatingPointFieldWithDefault(this, 3, 0.0));
};


/**
 * @param {number} value
 * @return {!proto.sensen.finance.HomeNpvResponse} returns this
 */
proto.sensen.finance.HomeNpvResponse.prototype.setFutureSalePrice = function(value) {
  return jspb.Message.setProto3FloatField(this, 3, value);
};


/**
 * optional double future_equity = 4;
 * @return {number}
 */
proto.sensen.finance.HomeNpvResponse.prototype.getFutureEquity = function() {
  return /** @type {number} */ (jspb.Message.getFloatingPointFieldWithDefault(this, 4, 0.0));
};


/**
 * @param {number} value
 * @return {!proto.sensen.finance.HomeNpvResponse} returns this
 */
proto.sensen.finance.HomeNpvResponse.prototype.setFutureEquity = function(value) {
  return jspb.Message.setProto3FloatField(this, 4, value);
};



/**
 * List of repeated fields within this message type.
 * @private {!Array<number>}
 * @const
 */
proto.sensen.finance.OptionTreeRequest.repeatedFields_ = [9];



if (jspb.Message.GENERATE_TO_OBJECT) {
/**
 * Creates an object representation of this proto.
 * Field names that are reserved in JavaScript and will be renamed to pb_name.
 * Optional fields that are not set will be set to undefined.
 * To access a reserved field use, foo.pb_<name>, eg, foo.pb_default.
 * For the list of reserved names please see:
 *     net/proto2/compiler/js/internal/generator.cc#kKeyword.
 * @param {boolean=} opt_includeInstance Deprecated. whether to include the
 *     JSPB instance for transitional soy proto support:
 *     http://goto/soy-param-migration
 * @return {!Object}
 */
proto.sensen.finance.OptionTreeRequest.prototype.toObject = function(opt_includeInstance) {
  return proto.sensen.finance.OptionTreeRequest.toObject(opt_includeInstance, this);
};


/**
 * Static version of the {@see toObject} method.
 * @param {boolean|undefined} includeInstance Deprecated. Whether to include
 *     the JSPB instance for transitional soy proto support:
 *     http://goto/soy-param-migration
 * @param {!proto.sensen.finance.OptionTreeRequest} msg The msg instance to transform.
 * @return {!Object}
 * @suppress {unusedLocalVariables} f is only used for nested messages
 */
proto.sensen.finance.OptionTreeRequest.toObject = function(includeInstance, msg) {
  var f, obj = {
    spot: jspb.Message.getFloatingPointFieldWithDefault(msg, 1, 0.0),
    strike: jspb.Message.getFloatingPointFieldWithDefault(msg, 2, 0.0),
    rate: jspb.Message.getFloatingPointFieldWithDefault(msg, 3, 0.0),
    volatility: jspb.Message.getFloatingPointFieldWithDefault(msg, 4, 0.0),
    yearsToExpiry: jspb.Message.getFloatingPointFieldWithDefault(msg, 5, 0.0),
    steps: jspb.Message.getFieldWithDefault(msg, 6, 0),
    optionType: jspb.Message.getFieldWithDefault(msg, 7, 0),
    exerciseType: jspb.Message.getFieldWithDefault(msg, 8, 0),
    bermudanDatesList: (f = jspb.Message.getRepeatedFloatingPointField(msg, 9)) == null ? undefined : f,
    asianType: jspb.Message.getFieldWithDefault(msg, 10, 0),
    averagingStates: jspb.Message.getFieldWithDefault(msg, 11, 0),
    lambda: jspb.Message.getFloatingPointFieldWithDefault(msg, 12, 0.0)
  };

  if (includeInstance) {
    obj.$jspbMessageInstance = msg;
  }
  return obj;
};
}


/**
 * Deserializes binary data (in protobuf wire format).
 * @param {jspb.ByteSource} bytes The bytes to deserialize.
 * @return {!proto.sensen.finance.OptionTreeRequest}
 */
proto.sensen.finance.OptionTreeRequest.deserializeBinary = function(bytes) {
  var reader = new jspb.BinaryReader(bytes);
  var msg = new proto.sensen.finance.OptionTreeRequest;
  return proto.sensen.finance.OptionTreeRequest.deserializeBinaryFromReader(msg, reader);
};


/**
 * Deserializes binary data (in protobuf wire format) from the
 * given reader into the given message object.
 * @param {!proto.sensen.finance.OptionTreeRequest} msg The message object to deserialize into.
 * @param {!jspb.BinaryReader} reader The BinaryReader to use.
 * @return {!proto.sensen.finance.OptionTreeRequest}
 */
proto.sensen.finance.OptionTreeRequest.deserializeBinaryFromReader = function(msg, reader) {
  while (reader.nextField()) {
    if (reader.isEndGroup()) {
      break;
    }
    var field = reader.getFieldNumber();
    switch (field) {
    case 1:
      var value = /** @type {number} */ (reader.readDouble());
      msg.setSpot(value);
      break;
    case 2:
      var value = /** @type {number} */ (reader.readDouble());
      msg.setStrike(value);
      break;
    case 3:
      var value = /** @type {number} */ (reader.readDouble());
      msg.setRate(value);
      break;
    case 4:
      var value = /** @type {number} */ (reader.readDouble());
      msg.setVolatility(value);
      break;
    case 5:
      var value = /** @type {number} */ (reader.readDouble());
      msg.setYearsToExpiry(value);
      break;
    case 6:
      var value = /** @type {number} */ (reader.readInt32());
      msg.setSteps(value);
      break;
    case 7:
      var value = /** @type {!proto.sensen.finance.OptionType} */ (reader.readEnum());
      msg.setOptionType(value);
      break;
    case 8:
      var value = /** @type {!proto.sensen.finance.ExerciseType} */ (reader.readEnum());
      msg.setExerciseType(value);
      break;
    case 9:
      var values = /** @type {!Array<number>} */ (reader.isDelimited() ? reader.readPackedDouble() : [reader.readDouble()]);
      for (var i = 0; i < values.length; i++) {
        msg.addBermudanDates(values[i]);
      }
      break;
    case 10:
      var value = /** @type {!proto.sensen.finance.AsianType} */ (reader.readEnum());
      msg.setAsianType(value);
      break;
    case 11:
      var value = /** @type {number} */ (reader.readInt32());
      msg.setAveragingStates(value);
      break;
    case 12:
      var value = /** @type {number} */ (reader.readDouble());
      msg.setLambda(value);
      break;
    default:
      reader.skipField();
      break;
    }
  }
  return msg;
};


/**
 * Serializes the message to binary data (in protobuf wire format).
 * @return {!Uint8Array}
 */
proto.sensen.finance.OptionTreeRequest.prototype.serializeBinary = function() {
  var writer = new jspb.BinaryWriter();
  proto.sensen.finance.OptionTreeRequest.serializeBinaryToWriter(this, writer);
  return writer.getResultBuffer();
};


/**
 * Serializes the given message to binary data (in protobuf wire
 * format), writing to the given BinaryWriter.
 * @param {!proto.sensen.finance.OptionTreeRequest} message
 * @param {!jspb.BinaryWriter} writer
 * @suppress {unusedLocalVariables} f is only used for nested messages
 */
proto.sensen.finance.OptionTreeRequest.serializeBinaryToWriter = function(message, writer) {
  var f = undefined;
  f = message.getSpot();
  if (f !== 0.0) {
    writer.writeDouble(
      1,
      f
    );
  }
  f = message.getStrike();
  if (f !== 0.0) {
    writer.writeDouble(
      2,
      f
    );
  }
  f = message.getRate();
  if (f !== 0.0) {
    writer.writeDouble(
      3,
      f
    );
  }
  f = message.getVolatility();
  if (f !== 0.0) {
    writer.writeDouble(
      4,
      f
    );
  }
  f = message.getYearsToExpiry();
  if (f !== 0.0) {
    writer.writeDouble(
      5,
      f
    );
  }
  f = message.getSteps();
  if (f !== 0) {
    writer.writeInt32(
      6,
      f
    );
  }
  f = message.getOptionType();
  if (f !== 0.0) {
    writer.writeEnum(
      7,
      f
    );
  }
  f = message.getExerciseType();
  if (f !== 0.0) {
    writer.writeEnum(
      8,
      f
    );
  }
  f = message.getBermudanDatesList();
  if (f.length > 0) {
    writer.writePackedDouble(
      9,
      f
    );
  }
  f = message.getAsianType();
  if (f !== 0.0) {
    writer.writeEnum(
      10,
      f
    );
  }
  f = message.getAveragingStates();
  if (f !== 0) {
    writer.writeInt32(
      11,
      f
    );
  }
  f = message.getLambda();
  if (f !== 0.0) {
    writer.writeDouble(
      12,
      f
    );
  }
};


/**
 * optional double spot = 1;
 * @return {number}
 */
proto.sensen.finance.OptionTreeRequest.prototype.getSpot = function() {
  return /** @type {number} */ (jspb.Message.getFloatingPointFieldWithDefault(this, 1, 0.0));
};


/**
 * @param {number} value
 * @return {!proto.sensen.finance.OptionTreeRequest} returns this
 */
proto.sensen.finance.OptionTreeRequest.prototype.setSpot = function(value) {
  return jspb.Message.setProto3FloatField(this, 1, value);
};


/**
 * optional double strike = 2;
 * @return {number}
 */
proto.sensen.finance.OptionTreeRequest.prototype.getStrike = function() {
  return /** @type {number} */ (jspb.Message.getFloatingPointFieldWithDefault(this, 2, 0.0));
};


/**
 * @param {number} value
 * @return {!proto.sensen.finance.OptionTreeRequest} returns this
 */
proto.sensen.finance.OptionTreeRequest.prototype.setStrike = function(value) {
  return jspb.Message.setProto3FloatField(this, 2, value);
};


/**
 * optional double rate = 3;
 * @return {number}
 */
proto.sensen.finance.OptionTreeRequest.prototype.getRate = function() {
  return /** @type {number} */ (jspb.Message.getFloatingPointFieldWithDefault(this, 3, 0.0));
};


/**
 * @param {number} value
 * @return {!proto.sensen.finance.OptionTreeRequest} returns this
 */
proto.sensen.finance.OptionTreeRequest.prototype.setRate = function(value) {
  return jspb.Message.setProto3FloatField(this, 3, value);
};


/**
 * optional double volatility = 4;
 * @return {number}
 */
proto.sensen.finance.OptionTreeRequest.prototype.getVolatility = function() {
  return /** @type {number} */ (jspb.Message.getFloatingPointFieldWithDefault(this, 4, 0.0));
};


/**
 * @param {number} value
 * @return {!proto.sensen.finance.OptionTreeRequest} returns this
 */
proto.sensen.finance.OptionTreeRequest.prototype.setVolatility = function(value) {
  return jspb.Message.setProto3FloatField(this, 4, value);
};


/**
 * optional double years_to_expiry = 5;
 * @return {number}
 */
proto.sensen.finance.OptionTreeRequest.prototype.getYearsToExpiry = function() {
  return /** @type {number} */ (jspb.Message.getFloatingPointFieldWithDefault(this, 5, 0.0));
};


/**
 * @param {number} value
 * @return {!proto.sensen.finance.OptionTreeRequest} returns this
 */
proto.sensen.finance.OptionTreeRequest.prototype.setYearsToExpiry = function(value) {
  return jspb.Message.setProto3FloatField(this, 5, value);
};


/**
 * optional int32 steps = 6;
 * @return {number}
 */
proto.sensen.finance.OptionTreeRequest.prototype.getSteps = function() {
  return /** @type {number} */ (jspb.Message.getFieldWithDefault(this, 6, 0));
};


/**
 * @param {number} value
 * @return {!proto.sensen.finance.OptionTreeRequest} returns this
 */
proto.sensen.finance.OptionTreeRequest.prototype.setSteps = function(value) {
  return jspb.Message.setProto3IntField(this, 6, value);
};


/**
 * optional OptionType option_type = 7;
 * @return {!proto.sensen.finance.OptionType}
 */
proto.sensen.finance.OptionTreeRequest.prototype.getOptionType = function() {
  return /** @type {!proto.sensen.finance.OptionType} */ (jspb.Message.getFieldWithDefault(this, 7, 0));
};


/**
 * @param {!proto.sensen.finance.OptionType} value
 * @return {!proto.sensen.finance.OptionTreeRequest} returns this
 */
proto.sensen.finance.OptionTreeRequest.prototype.setOptionType = function(value) {
  return jspb.Message.setProto3EnumField(this, 7, value);
};


/**
 * optional ExerciseType exercise_type = 8;
 * @return {!proto.sensen.finance.ExerciseType}
 */
proto.sensen.finance.OptionTreeRequest.prototype.getExerciseType = function() {
  return /** @type {!proto.sensen.finance.ExerciseType} */ (jspb.Message.getFieldWithDefault(this, 8, 0));
};


/**
 * @param {!proto.sensen.finance.ExerciseType} value
 * @return {!proto.sensen.finance.OptionTreeRequest} returns this
 */
proto.sensen.finance.OptionTreeRequest.prototype.setExerciseType = function(value) {
  return jspb.Message.setProto3EnumField(this, 8, value);
};


/**
 * repeated double bermudan_dates = 9;
 * @return {!Array<number>}
 */
proto.sensen.finance.OptionTreeRequest.prototype.getBermudanDatesList = function() {
  return /** @type {!Array<number>} */ (jspb.Message.getRepeatedFloatingPointField(this, 9));
};


/**
 * @param {!Array<number>} value
 * @return {!proto.sensen.finance.OptionTreeRequest} returns this
 */
proto.sensen.finance.OptionTreeRequest.prototype.setBermudanDatesList = function(value) {
  return jspb.Message.setField(this, 9, value || []);
};


/**
 * @param {number} value
 * @param {number=} opt_index
 * @return {!proto.sensen.finance.OptionTreeRequest} returns this
 */
proto.sensen.finance.OptionTreeRequest.prototype.addBermudanDates = function(value, opt_index) {
  return jspb.Message.addToRepeatedField(this, 9, value, opt_index);
};


/**
 * Clears the list making it empty but non-null.
 * @return {!proto.sensen.finance.OptionTreeRequest} returns this
 */
proto.sensen.finance.OptionTreeRequest.prototype.clearBermudanDatesList = function() {
  return this.setBermudanDatesList([]);
};


/**
 * optional AsianType asian_type = 10;
 * @return {!proto.sensen.finance.AsianType}
 */
proto.sensen.finance.OptionTreeRequest.prototype.getAsianType = function() {
  return /** @type {!proto.sensen.finance.AsianType} */ (jspb.Message.getFieldWithDefault(this, 10, 0));
};


/**
 * @param {!proto.sensen.finance.AsianType} value
 * @return {!proto.sensen.finance.OptionTreeRequest} returns this
 */
proto.sensen.finance.OptionTreeRequest.prototype.setAsianType = function(value) {
  return jspb.Message.setProto3EnumField(this, 10, value);
};


/**
 * optional int32 averaging_states = 11;
 * @return {number}
 */
proto.sensen.finance.OptionTreeRequest.prototype.getAveragingStates = function() {
  return /** @type {number} */ (jspb.Message.getFieldWithDefault(this, 11, 0));
};


/**
 * @param {number} value
 * @return {!proto.sensen.finance.OptionTreeRequest} returns this
 */
proto.sensen.finance.OptionTreeRequest.prototype.setAveragingStates = function(value) {
  return jspb.Message.setProto3IntField(this, 11, value);
};


/**
 * optional double lambda = 12;
 * @return {number}
 */
proto.sensen.finance.OptionTreeRequest.prototype.getLambda = function() {
  return /** @type {number} */ (jspb.Message.getFloatingPointFieldWithDefault(this, 12, 0.0));
};


/**
 * @param {number} value
 * @return {!proto.sensen.finance.OptionTreeRequest} returns this
 */
proto.sensen.finance.OptionTreeRequest.prototype.setLambda = function(value) {
  return jspb.Message.setProto3FloatField(this, 12, value);
};





if (jspb.Message.GENERATE_TO_OBJECT) {
/**
 * Creates an object representation of this proto.
 * Field names that are reserved in JavaScript and will be renamed to pb_name.
 * Optional fields that are not set will be set to undefined.
 * To access a reserved field use, foo.pb_<name>, eg, foo.pb_default.
 * For the list of reserved names please see:
 *     net/proto2/compiler/js/internal/generator.cc#kKeyword.
 * @param {boolean=} opt_includeInstance Deprecated. whether to include the
 *     JSPB instance for transitional soy proto support:
 *     http://goto/soy-param-migration
 * @return {!Object}
 */
proto.sensen.finance.OptionPricingResponse.prototype.toObject = function(opt_includeInstance) {
  return proto.sensen.finance.OptionPricingResponse.toObject(opt_includeInstance, this);
};


/**
 * Static version of the {@see toObject} method.
 * @param {boolean|undefined} includeInstance Deprecated. Whether to include
 *     the JSPB instance for transitional soy proto support:
 *     http://goto/soy-param-migration
 * @param {!proto.sensen.finance.OptionPricingResponse} msg The msg instance to transform.
 * @return {!Object}
 * @suppress {unusedLocalVariables} f is only used for nested messages
 */
proto.sensen.finance.OptionPricingResponse.toObject = function(includeInstance, msg) {
  var f, obj = {
    value: jspb.Message.getFloatingPointFieldWithDefault(msg, 1, 0.0),
    delta: jspb.Message.getFloatingPointFieldWithDefault(msg, 2, 0.0),
    gamma: jspb.Message.getFloatingPointFieldWithDefault(msg, 3, 0.0),
    theta: jspb.Message.getFloatingPointFieldWithDefault(msg, 4, 0.0)
  };

  if (includeInstance) {
    obj.$jspbMessageInstance = msg;
  }
  return obj;
};
}


/**
 * Deserializes binary data (in protobuf wire format).
 * @param {jspb.ByteSource} bytes The bytes to deserialize.
 * @return {!proto.sensen.finance.OptionPricingResponse}
 */
proto.sensen.finance.OptionPricingResponse.deserializeBinary = function(bytes) {
  var reader = new jspb.BinaryReader(bytes);
  var msg = new proto.sensen.finance.OptionPricingResponse;
  return proto.sensen.finance.OptionPricingResponse.deserializeBinaryFromReader(msg, reader);
};


/**
 * Deserializes binary data (in protobuf wire format) from the
 * given reader into the given message object.
 * @param {!proto.sensen.finance.OptionPricingResponse} msg The message object to deserialize into.
 * @param {!jspb.BinaryReader} reader The BinaryReader to use.
 * @return {!proto.sensen.finance.OptionPricingResponse}
 */
proto.sensen.finance.OptionPricingResponse.deserializeBinaryFromReader = function(msg, reader) {
  while (reader.nextField()) {
    if (reader.isEndGroup()) {
      break;
    }
    var field = reader.getFieldNumber();
    switch (field) {
    case 1:
      var value = /** @type {number} */ (reader.readDouble());
      msg.setValue(value);
      break;
    case 2:
      var value = /** @type {number} */ (reader.readDouble());
      msg.setDelta(value);
      break;
    case 3:
      var value = /** @type {number} */ (reader.readDouble());
      msg.setGamma(value);
      break;
    case 4:
      var value = /** @type {number} */ (reader.readDouble());
      msg.setTheta(value);
      break;
    default:
      reader.skipField();
      break;
    }
  }
  return msg;
};


/**
 * Serializes the message to binary data (in protobuf wire format).
 * @return {!Uint8Array}
 */
proto.sensen.finance.OptionPricingResponse.prototype.serializeBinary = function() {
  var writer = new jspb.BinaryWriter();
  proto.sensen.finance.OptionPricingResponse.serializeBinaryToWriter(this, writer);
  return writer.getResultBuffer();
};


/**
 * Serializes the given message to binary data (in protobuf wire
 * format), writing to the given BinaryWriter.
 * @param {!proto.sensen.finance.OptionPricingResponse} message
 * @param {!jspb.BinaryWriter} writer
 * @suppress {unusedLocalVariables} f is only used for nested messages
 */
proto.sensen.finance.OptionPricingResponse.serializeBinaryToWriter = function(message, writer) {
  var f = undefined;
  f = message.getValue();
  if (f !== 0.0) {
    writer.writeDouble(
      1,
      f
    );
  }
  f = message.getDelta();
  if (f !== 0.0) {
    writer.writeDouble(
      2,
      f
    );
  }
  f = message.getGamma();
  if (f !== 0.0) {
    writer.writeDouble(
      3,
      f
    );
  }
  f = message.getTheta();
  if (f !== 0.0) {
    writer.writeDouble(
      4,
      f
    );
  }
};


/**
 * optional double value = 1;
 * @return {number}
 */
proto.sensen.finance.OptionPricingResponse.prototype.getValue = function() {
  return /** @type {number} */ (jspb.Message.getFloatingPointFieldWithDefault(this, 1, 0.0));
};


/**
 * @param {number} value
 * @return {!proto.sensen.finance.OptionPricingResponse} returns this
 */
proto.sensen.finance.OptionPricingResponse.prototype.setValue = function(value) {
  return jspb.Message.setProto3FloatField(this, 1, value);
};


/**
 * optional double delta = 2;
 * @return {number}
 */
proto.sensen.finance.OptionPricingResponse.prototype.getDelta = function() {
  return /** @type {number} */ (jspb.Message.getFloatingPointFieldWithDefault(this, 2, 0.0));
};


/**
 * @param {number} value
 * @return {!proto.sensen.finance.OptionPricingResponse} returns this
 */
proto.sensen.finance.OptionPricingResponse.prototype.setDelta = function(value) {
  return jspb.Message.setProto3FloatField(this, 2, value);
};


/**
 * optional double gamma = 3;
 * @return {number}
 */
proto.sensen.finance.OptionPricingResponse.prototype.getGamma = function() {
  return /** @type {number} */ (jspb.Message.getFloatingPointFieldWithDefault(this, 3, 0.0));
};


/**
 * @param {number} value
 * @return {!proto.sensen.finance.OptionPricingResponse} returns this
 */
proto.sensen.finance.OptionPricingResponse.prototype.setGamma = function(value) {
  return jspb.Message.setProto3FloatField(this, 3, value);
};


/**
 * optional double theta = 4;
 * @return {number}
 */
proto.sensen.finance.OptionPricingResponse.prototype.getTheta = function() {
  return /** @type {number} */ (jspb.Message.getFloatingPointFieldWithDefault(this, 4, 0.0));
};


/**
 * @param {number} value
 * @return {!proto.sensen.finance.OptionPricingResponse} returns this
 */
proto.sensen.finance.OptionPricingResponse.prototype.setTheta = function(value) {
  return jspb.Message.setProto3FloatField(this, 4, value);
};





if (jspb.Message.GENERATE_TO_OBJECT) {
/**
 * Creates an object representation of this proto.
 * Field names that are reserved in JavaScript and will be renamed to pb_name.
 * Optional fields that are not set will be set to undefined.
 * To access a reserved field use, foo.pb_<name>, eg, foo.pb_default.
 * For the list of reserved names please see:
 *     net/proto2/compiler/js/internal/generator.cc#kKeyword.
 * @param {boolean=} opt_includeInstance Deprecated. whether to include the
 *     JSPB instance for transitional soy proto support:
 *     http://goto/soy-param-migration
 * @return {!Object}
 */
proto.sensen.finance.BlackScholesRequest.prototype.toObject = function(opt_includeInstance) {
  return proto.sensen.finance.BlackScholesRequest.toObject(opt_includeInstance, this);
};


/**
 * Static version of the {@see toObject} method.
 * @param {boolean|undefined} includeInstance Deprecated. Whether to include
 *     the JSPB instance for transitional soy proto support:
 *     http://goto/soy-param-migration
 * @param {!proto.sensen.finance.BlackScholesRequest} msg The msg instance to transform.
 * @return {!Object}
 * @suppress {unusedLocalVariables} f is only used for nested messages
 */
proto.sensen.finance.BlackScholesRequest.toObject = function(includeInstance, msg) {
  var f, obj = {
    spot: jspb.Message.getFloatingPointFieldWithDefault(msg, 1, 0.0),
    strike: jspb.Message.getFloatingPointFieldWithDefault(msg, 2, 0.0),
    rate: jspb.Message.getFloatingPointFieldWithDefault(msg, 3, 0.0),
    volatility: jspb.Message.getFloatingPointFieldWithDefault(msg, 4, 0.0),
    yearsToExpiry: jspb.Message.getFloatingPointFieldWithDefault(msg, 5, 0.0),
    optionType: jspb.Message.getFieldWithDefault(msg, 6, 0)
  };

  if (includeInstance) {
    obj.$jspbMessageInstance = msg;
  }
  return obj;
};
}


/**
 * Deserializes binary data (in protobuf wire format).
 * @param {jspb.ByteSource} bytes The bytes to deserialize.
 * @return {!proto.sensen.finance.BlackScholesRequest}
 */
proto.sensen.finance.BlackScholesRequest.deserializeBinary = function(bytes) {
  var reader = new jspb.BinaryReader(bytes);
  var msg = new proto.sensen.finance.BlackScholesRequest;
  return proto.sensen.finance.BlackScholesRequest.deserializeBinaryFromReader(msg, reader);
};


/**
 * Deserializes binary data (in protobuf wire format) from the
 * given reader into the given message object.
 * @param {!proto.sensen.finance.BlackScholesRequest} msg The message object to deserialize into.
 * @param {!jspb.BinaryReader} reader The BinaryReader to use.
 * @return {!proto.sensen.finance.BlackScholesRequest}
 */
proto.sensen.finance.BlackScholesRequest.deserializeBinaryFromReader = function(msg, reader) {
  while (reader.nextField()) {
    if (reader.isEndGroup()) {
      break;
    }
    var field = reader.getFieldNumber();
    switch (field) {
    case 1:
      var value = /** @type {number} */ (reader.readDouble());
      msg.setSpot(value);
      break;
    case 2:
      var value = /** @type {number} */ (reader.readDouble());
      msg.setStrike(value);
      break;
    case 3:
      var value = /** @type {number} */ (reader.readDouble());
      msg.setRate(value);
      break;
    case 4:
      var value = /** @type {number} */ (reader.readDouble());
      msg.setVolatility(value);
      break;
    case 5:
      var value = /** @type {number} */ (reader.readDouble());
      msg.setYearsToExpiry(value);
      break;
    case 6:
      var value = /** @type {!proto.sensen.finance.OptionType} */ (reader.readEnum());
      msg.setOptionType(value);
      break;
    default:
      reader.skipField();
      break;
    }
  }
  return msg;
};


/**
 * Serializes the message to binary data (in protobuf wire format).
 * @return {!Uint8Array}
 */
proto.sensen.finance.BlackScholesRequest.prototype.serializeBinary = function() {
  var writer = new jspb.BinaryWriter();
  proto.sensen.finance.BlackScholesRequest.serializeBinaryToWriter(this, writer);
  return writer.getResultBuffer();
};


/**
 * Serializes the given message to binary data (in protobuf wire
 * format), writing to the given BinaryWriter.
 * @param {!proto.sensen.finance.BlackScholesRequest} message
 * @param {!jspb.BinaryWriter} writer
 * @suppress {unusedLocalVariables} f is only used for nested messages
 */
proto.sensen.finance.BlackScholesRequest.serializeBinaryToWriter = function(message, writer) {
  var f = undefined;
  f = message.getSpot();
  if (f !== 0.0) {
    writer.writeDouble(
      1,
      f
    );
  }
  f = message.getStrike();
  if (f !== 0.0) {
    writer.writeDouble(
      2,
      f
    );
  }
  f = message.getRate();
  if (f !== 0.0) {
    writer.writeDouble(
      3,
      f
    );
  }
  f = message.getVolatility();
  if (f !== 0.0) {
    writer.writeDouble(
      4,
      f
    );
  }
  f = message.getYearsToExpiry();
  if (f !== 0.0) {
    writer.writeDouble(
      5,
      f
    );
  }
  f = message.getOptionType();
  if (f !== 0.0) {
    writer.writeEnum(
      6,
      f
    );
  }
};


/**
 * optional double spot = 1;
 * @return {number}
 */
proto.sensen.finance.BlackScholesRequest.prototype.getSpot = function() {
  return /** @type {number} */ (jspb.Message.getFloatingPointFieldWithDefault(this, 1, 0.0));
};


/**
 * @param {number} value
 * @return {!proto.sensen.finance.BlackScholesRequest} returns this
 */
proto.sensen.finance.BlackScholesRequest.prototype.setSpot = function(value) {
  return jspb.Message.setProto3FloatField(this, 1, value);
};


/**
 * optional double strike = 2;
 * @return {number}
 */
proto.sensen.finance.BlackScholesRequest.prototype.getStrike = function() {
  return /** @type {number} */ (jspb.Message.getFloatingPointFieldWithDefault(this, 2, 0.0));
};


/**
 * @param {number} value
 * @return {!proto.sensen.finance.BlackScholesRequest} returns this
 */
proto.sensen.finance.BlackScholesRequest.prototype.setStrike = function(value) {
  return jspb.Message.setProto3FloatField(this, 2, value);
};


/**
 * optional double rate = 3;
 * @return {number}
 */
proto.sensen.finance.BlackScholesRequest.prototype.getRate = function() {
  return /** @type {number} */ (jspb.Message.getFloatingPointFieldWithDefault(this, 3, 0.0));
};


/**
 * @param {number} value
 * @return {!proto.sensen.finance.BlackScholesRequest} returns this
 */
proto.sensen.finance.BlackScholesRequest.prototype.setRate = function(value) {
  return jspb.Message.setProto3FloatField(this, 3, value);
};


/**
 * optional double volatility = 4;
 * @return {number}
 */
proto.sensen.finance.BlackScholesRequest.prototype.getVolatility = function() {
  return /** @type {number} */ (jspb.Message.getFloatingPointFieldWithDefault(this, 4, 0.0));
};


/**
 * @param {number} value
 * @return {!proto.sensen.finance.BlackScholesRequest} returns this
 */
proto.sensen.finance.BlackScholesRequest.prototype.setVolatility = function(value) {
  return jspb.Message.setProto3FloatField(this, 4, value);
};


/**
 * optional double years_to_expiry = 5;
 * @return {number}
 */
proto.sensen.finance.BlackScholesRequest.prototype.getYearsToExpiry = function() {
  return /** @type {number} */ (jspb.Message.getFloatingPointFieldWithDefault(this, 5, 0.0));
};


/**
 * @param {number} value
 * @return {!proto.sensen.finance.BlackScholesRequest} returns this
 */
proto.sensen.finance.BlackScholesRequest.prototype.setYearsToExpiry = function(value) {
  return jspb.Message.setProto3FloatField(this, 5, value);
};


/**
 * optional OptionType option_type = 6;
 * @return {!proto.sensen.finance.OptionType}
 */
proto.sensen.finance.BlackScholesRequest.prototype.getOptionType = function() {
  return /** @type {!proto.sensen.finance.OptionType} */ (jspb.Message.getFieldWithDefault(this, 6, 0));
};


/**
 * @param {!proto.sensen.finance.OptionType} value
 * @return {!proto.sensen.finance.BlackScholesRequest} returns this
 */
proto.sensen.finance.BlackScholesRequest.prototype.setOptionType = function(value) {
  return jspb.Message.setProto3EnumField(this, 6, value);
};





if (jspb.Message.GENERATE_TO_OBJECT) {
/**
 * Creates an object representation of this proto.
 * Field names that are reserved in JavaScript and will be renamed to pb_name.
 * Optional fields that are not set will be set to undefined.
 * To access a reserved field use, foo.pb_<name>, eg, foo.pb_default.
 * For the list of reserved names please see:
 *     net/proto2/compiler/js/internal/generator.cc#kKeyword.
 * @param {boolean=} opt_includeInstance Deprecated. whether to include the
 *     JSPB instance for transitional soy proto support:
 *     http://goto/soy-param-migration
 * @return {!Object}
 */
proto.sensen.finance.BlackScholesResponse.prototype.toObject = function(opt_includeInstance) {
  return proto.sensen.finance.BlackScholesResponse.toObject(opt_includeInstance, this);
};


/**
 * Static version of the {@see toObject} method.
 * @param {boolean|undefined} includeInstance Deprecated. Whether to include
 *     the JSPB instance for transitional soy proto support:
 *     http://goto/soy-param-migration
 * @param {!proto.sensen.finance.BlackScholesResponse} msg The msg instance to transform.
 * @return {!Object}
 * @suppress {unusedLocalVariables} f is only used for nested messages
 */
proto.sensen.finance.BlackScholesResponse.toObject = function(includeInstance, msg) {
  var f, obj = {
    value: jspb.Message.getFloatingPointFieldWithDefault(msg, 1, 0.0),
    delta: jspb.Message.getFloatingPointFieldWithDefault(msg, 2, 0.0),
    gamma: jspb.Message.getFloatingPointFieldWithDefault(msg, 3, 0.0),
    theta: jspb.Message.getFloatingPointFieldWithDefault(msg, 4, 0.0),
    vega: jspb.Message.getFloatingPointFieldWithDefault(msg, 5, 0.0),
    rho: jspb.Message.getFloatingPointFieldWithDefault(msg, 6, 0.0),
    vanna: jspb.Message.getFloatingPointFieldWithDefault(msg, 7, 0.0),
    volga: jspb.Message.getFloatingPointFieldWithDefault(msg, 8, 0.0),
    charm: jspb.Message.getFloatingPointFieldWithDefault(msg, 9, 0.0),
    color: jspb.Message.getFloatingPointFieldWithDefault(msg, 10, 0.0),
    speed: jspb.Message.getFloatingPointFieldWithDefault(msg, 11, 0.0)
  };

  if (includeInstance) {
    obj.$jspbMessageInstance = msg;
  }
  return obj;
};
}


/**
 * Deserializes binary data (in protobuf wire format).
 * @param {jspb.ByteSource} bytes The bytes to deserialize.
 * @return {!proto.sensen.finance.BlackScholesResponse}
 */
proto.sensen.finance.BlackScholesResponse.deserializeBinary = function(bytes) {
  var reader = new jspb.BinaryReader(bytes);
  var msg = new proto.sensen.finance.BlackScholesResponse;
  return proto.sensen.finance.BlackScholesResponse.deserializeBinaryFromReader(msg, reader);
};


/**
 * Deserializes binary data (in protobuf wire format) from the
 * given reader into the given message object.
 * @param {!proto.sensen.finance.BlackScholesResponse} msg The message object to deserialize into.
 * @param {!jspb.BinaryReader} reader The BinaryReader to use.
 * @return {!proto.sensen.finance.BlackScholesResponse}
 */
proto.sensen.finance.BlackScholesResponse.deserializeBinaryFromReader = function(msg, reader) {
  while (reader.nextField()) {
    if (reader.isEndGroup()) {
      break;
    }
    var field = reader.getFieldNumber();
    switch (field) {
    case 1:
      var value = /** @type {number} */ (reader.readDouble());
      msg.setValue(value);
      break;
    case 2:
      var value = /** @type {number} */ (reader.readDouble());
      msg.setDelta(value);
      break;
    case 3:
      var value = /** @type {number} */ (reader.readDouble());
      msg.setGamma(value);
      break;
    case 4:
      var value = /** @type {number} */ (reader.readDouble());
      msg.setTheta(value);
      break;
    case 5:
      var value = /** @type {number} */ (reader.readDouble());
      msg.setVega(value);
      break;
    case 6:
      var value = /** @type {number} */ (reader.readDouble());
      msg.setRho(value);
      break;
    case 7:
      var value = /** @type {number} */ (reader.readDouble());
      msg.setVanna(value);
      break;
    case 8:
      var value = /** @type {number} */ (reader.readDouble());
      msg.setVolga(value);
      break;
    case 9:
      var value = /** @type {number} */ (reader.readDouble());
      msg.setCharm(value);
      break;
    case 10:
      var value = /** @type {number} */ (reader.readDouble());
      msg.setColor(value);
      break;
    case 11:
      var value = /** @type {number} */ (reader.readDouble());
      msg.setSpeed(value);
      break;
    default:
      reader.skipField();
      break;
    }
  }
  return msg;
};


/**
 * Serializes the message to binary data (in protobuf wire format).
 * @return {!Uint8Array}
 */
proto.sensen.finance.BlackScholesResponse.prototype.serializeBinary = function() {
  var writer = new jspb.BinaryWriter();
  proto.sensen.finance.BlackScholesResponse.serializeBinaryToWriter(this, writer);
  return writer.getResultBuffer();
};


/**
 * Serializes the given message to binary data (in protobuf wire
 * format), writing to the given BinaryWriter.
 * @param {!proto.sensen.finance.BlackScholesResponse} message
 * @param {!jspb.BinaryWriter} writer
 * @suppress {unusedLocalVariables} f is only used for nested messages
 */
proto.sensen.finance.BlackScholesResponse.serializeBinaryToWriter = function(message, writer) {
  var f = undefined;
  f = message.getValue();
  if (f !== 0.0) {
    writer.writeDouble(
      1,
      f
    );
  }
  f = message.getDelta();
  if (f !== 0.0) {
    writer.writeDouble(
      2,
      f
    );
  }
  f = message.getGamma();
  if (f !== 0.0) {
    writer.writeDouble(
      3,
      f
    );
  }
  f = message.getTheta();
  if (f !== 0.0) {
    writer.writeDouble(
      4,
      f
    );
  }
  f = message.getVega();
  if (f !== 0.0) {
    writer.writeDouble(
      5,
      f
    );
  }
  f = message.getRho();
  if (f !== 0.0) {
    writer.writeDouble(
      6,
      f
    );
  }
  f = message.getVanna();
  if (f !== 0.0) {
    writer.writeDouble(
      7,
      f
    );
  }
  f = message.getVolga();
  if (f !== 0.0) {
    writer.writeDouble(
      8,
      f
    );
  }
  f = message.getCharm();
  if (f !== 0.0) {
    writer.writeDouble(
      9,
      f
    );
  }
  f = message.getColor();
  if (f !== 0.0) {
    writer.writeDouble(
      10,
      f
    );
  }
  f = message.getSpeed();
  if (f !== 0.0) {
    writer.writeDouble(
      11,
      f
    );
  }
};


/**
 * optional double value = 1;
 * @return {number}
 */
proto.sensen.finance.BlackScholesResponse.prototype.getValue = function() {
  return /** @type {number} */ (jspb.Message.getFloatingPointFieldWithDefault(this, 1, 0.0));
};


/**
 * @param {number} value
 * @return {!proto.sensen.finance.BlackScholesResponse} returns this
 */
proto.sensen.finance.BlackScholesResponse.prototype.setValue = function(value) {
  return jspb.Message.setProto3FloatField(this, 1, value);
};


/**
 * optional double delta = 2;
 * @return {number}
 */
proto.sensen.finance.BlackScholesResponse.prototype.getDelta = function() {
  return /** @type {number} */ (jspb.Message.getFloatingPointFieldWithDefault(this, 2, 0.0));
};


/**
 * @param {number} value
 * @return {!proto.sensen.finance.BlackScholesResponse} returns this
 */
proto.sensen.finance.BlackScholesResponse.prototype.setDelta = function(value) {
  return jspb.Message.setProto3FloatField(this, 2, value);
};


/**
 * optional double gamma = 3;
 * @return {number}
 */
proto.sensen.finance.BlackScholesResponse.prototype.getGamma = function() {
  return /** @type {number} */ (jspb.Message.getFloatingPointFieldWithDefault(this, 3, 0.0));
};


/**
 * @param {number} value
 * @return {!proto.sensen.finance.BlackScholesResponse} returns this
 */
proto.sensen.finance.BlackScholesResponse.prototype.setGamma = function(value) {
  return jspb.Message.setProto3FloatField(this, 3, value);
};


/**
 * optional double theta = 4;
 * @return {number}
 */
proto.sensen.finance.BlackScholesResponse.prototype.getTheta = function() {
  return /** @type {number} */ (jspb.Message.getFloatingPointFieldWithDefault(this, 4, 0.0));
};


/**
 * @param {number} value
 * @return {!proto.sensen.finance.BlackScholesResponse} returns this
 */
proto.sensen.finance.BlackScholesResponse.prototype.setTheta = function(value) {
  return jspb.Message.setProto3FloatField(this, 4, value);
};


/**
 * optional double vega = 5;
 * @return {number}
 */
proto.sensen.finance.BlackScholesResponse.prototype.getVega = function() {
  return /** @type {number} */ (jspb.Message.getFloatingPointFieldWithDefault(this, 5, 0.0));
};


/**
 * @param {number} value
 * @return {!proto.sensen.finance.BlackScholesResponse} returns this
 */
proto.sensen.finance.BlackScholesResponse.prototype.setVega = function(value) {
  return jspb.Message.setProto3FloatField(this, 5, value);
};


/**
 * optional double rho = 6;
 * @return {number}
 */
proto.sensen.finance.BlackScholesResponse.prototype.getRho = function() {
  return /** @type {number} */ (jspb.Message.getFloatingPointFieldWithDefault(this, 6, 0.0));
};


/**
 * @param {number} value
 * @return {!proto.sensen.finance.BlackScholesResponse} returns this
 */
proto.sensen.finance.BlackScholesResponse.prototype.setRho = function(value) {
  return jspb.Message.setProto3FloatField(this, 6, value);
};


/**
 * optional double vanna = 7;
 * @return {number}
 */
proto.sensen.finance.BlackScholesResponse.prototype.getVanna = function() {
  return /** @type {number} */ (jspb.Message.getFloatingPointFieldWithDefault(this, 7, 0.0));
};


/**
 * @param {number} value
 * @return {!proto.sensen.finance.BlackScholesResponse} returns this
 */
proto.sensen.finance.BlackScholesResponse.prototype.setVanna = function(value) {
  return jspb.Message.setProto3FloatField(this, 7, value);
};


/**
 * optional double volga = 8;
 * @return {number}
 */
proto.sensen.finance.BlackScholesResponse.prototype.getVolga = function() {
  return /** @type {number} */ (jspb.Message.getFloatingPointFieldWithDefault(this, 8, 0.0));
};


/**
 * @param {number} value
 * @return {!proto.sensen.finance.BlackScholesResponse} returns this
 */
proto.sensen.finance.BlackScholesResponse.prototype.setVolga = function(value) {
  return jspb.Message.setProto3FloatField(this, 8, value);
};


/**
 * optional double charm = 9;
 * @return {number}
 */
proto.sensen.finance.BlackScholesResponse.prototype.getCharm = function() {
  return /** @type {number} */ (jspb.Message.getFloatingPointFieldWithDefault(this, 9, 0.0));
};


/**
 * @param {number} value
 * @return {!proto.sensen.finance.BlackScholesResponse} returns this
 */
proto.sensen.finance.BlackScholesResponse.prototype.setCharm = function(value) {
  return jspb.Message.setProto3FloatField(this, 9, value);
};


/**
 * optional double color = 10;
 * @return {number}
 */
proto.sensen.finance.BlackScholesResponse.prototype.getColor = function() {
  return /** @type {number} */ (jspb.Message.getFloatingPointFieldWithDefault(this, 10, 0.0));
};


/**
 * @param {number} value
 * @return {!proto.sensen.finance.BlackScholesResponse} returns this
 */
proto.sensen.finance.BlackScholesResponse.prototype.setColor = function(value) {
  return jspb.Message.setProto3FloatField(this, 10, value);
};


/**
 * optional double speed = 11;
 * @return {number}
 */
proto.sensen.finance.BlackScholesResponse.prototype.getSpeed = function() {
  return /** @type {number} */ (jspb.Message.getFloatingPointFieldWithDefault(this, 11, 0.0));
};


/**
 * @param {number} value
 * @return {!proto.sensen.finance.BlackScholesResponse} returns this
 */
proto.sensen.finance.BlackScholesResponse.prototype.setSpeed = function(value) {
  return jspb.Message.setProto3FloatField(this, 11, value);
};





if (jspb.Message.GENERATE_TO_OBJECT) {
/**
 * Creates an object representation of this proto.
 * Field names that are reserved in JavaScript and will be renamed to pb_name.
 * Optional fields that are not set will be set to undefined.
 * To access a reserved field use, foo.pb_<name>, eg, foo.pb_default.
 * For the list of reserved names please see:
 *     net/proto2/compiler/js/internal/generator.cc#kKeyword.
 * @param {boolean=} opt_includeInstance Deprecated. whether to include the
 *     JSPB instance for transitional soy proto support:
 *     http://goto/soy-param-migration
 * @return {!Object}
 */
proto.sensen.finance.MonteCarloRequest.prototype.toObject = function(opt_includeInstance) {
  return proto.sensen.finance.MonteCarloRequest.toObject(opt_includeInstance, this);
};


/**
 * Static version of the {@see toObject} method.
 * @param {boolean|undefined} includeInstance Deprecated. Whether to include
 *     the JSPB instance for transitional soy proto support:
 *     http://goto/soy-param-migration
 * @param {!proto.sensen.finance.MonteCarloRequest} msg The msg instance to transform.
 * @return {!Object}
 * @suppress {unusedLocalVariables} f is only used for nested messages
 */
proto.sensen.finance.MonteCarloRequest.toObject = function(includeInstance, msg) {
  var f, obj = {
    spot: jspb.Message.getFloatingPointFieldWithDefault(msg, 1, 0.0),
    strike: jspb.Message.getFloatingPointFieldWithDefault(msg, 2, 0.0),
    rate: jspb.Message.getFloatingPointFieldWithDefault(msg, 3, 0.0),
    volatility: jspb.Message.getFloatingPointFieldWithDefault(msg, 4, 0.0),
    yearsToExpiry: jspb.Message.getFloatingPointFieldWithDefault(msg, 5, 0.0),
    paths: jspb.Message.getFieldWithDefault(msg, 6, 0),
    steps: jspb.Message.getFieldWithDefault(msg, 7, 0),
    optionType: jspb.Message.getFieldWithDefault(msg, 8, 0),
    asianType: jspb.Message.getFieldWithDefault(msg, 9, 0),
    numThreads: jspb.Message.getFieldWithDefault(msg, 10, 0)
  };

  if (includeInstance) {
    obj.$jspbMessageInstance = msg;
  }
  return obj;
};
}


/**
 * Deserializes binary data (in protobuf wire format).
 * @param {jspb.ByteSource} bytes The bytes to deserialize.
 * @return {!proto.sensen.finance.MonteCarloRequest}
 */
proto.sensen.finance.MonteCarloRequest.deserializeBinary = function(bytes) {
  var reader = new jspb.BinaryReader(bytes);
  var msg = new proto.sensen.finance.MonteCarloRequest;
  return proto.sensen.finance.MonteCarloRequest.deserializeBinaryFromReader(msg, reader);
};


/**
 * Deserializes binary data (in protobuf wire format) from the
 * given reader into the given message object.
 * @param {!proto.sensen.finance.MonteCarloRequest} msg The message object to deserialize into.
 * @param {!jspb.BinaryReader} reader The BinaryReader to use.
 * @return {!proto.sensen.finance.MonteCarloRequest}
 */
proto.sensen.finance.MonteCarloRequest.deserializeBinaryFromReader = function(msg, reader) {
  while (reader.nextField()) {
    if (reader.isEndGroup()) {
      break;
    }
    var field = reader.getFieldNumber();
    switch (field) {
    case 1:
      var value = /** @type {number} */ (reader.readDouble());
      msg.setSpot(value);
      break;
    case 2:
      var value = /** @type {number} */ (reader.readDouble());
      msg.setStrike(value);
      break;
    case 3:
      var value = /** @type {number} */ (reader.readDouble());
      msg.setRate(value);
      break;
    case 4:
      var value = /** @type {number} */ (reader.readDouble());
      msg.setVolatility(value);
      break;
    case 5:
      var value = /** @type {number} */ (reader.readDouble());
      msg.setYearsToExpiry(value);
      break;
    case 6:
      var value = /** @type {number} */ (reader.readInt32());
      msg.setPaths(value);
      break;
    case 7:
      var value = /** @type {number} */ (reader.readInt32());
      msg.setSteps(value);
      break;
    case 8:
      var value = /** @type {!proto.sensen.finance.OptionType} */ (reader.readEnum());
      msg.setOptionType(value);
      break;
    case 9:
      var value = /** @type {!proto.sensen.finance.AsianType} */ (reader.readEnum());
      msg.setAsianType(value);
      break;
    case 10:
      var value = /** @type {number} */ (reader.readInt32());
      msg.setNumThreads(value);
      break;
    default:
      reader.skipField();
      break;
    }
  }
  return msg;
};


/**
 * Serializes the message to binary data (in protobuf wire format).
 * @return {!Uint8Array}
 */
proto.sensen.finance.MonteCarloRequest.prototype.serializeBinary = function() {
  var writer = new jspb.BinaryWriter();
  proto.sensen.finance.MonteCarloRequest.serializeBinaryToWriter(this, writer);
  return writer.getResultBuffer();
};


/**
 * Serializes the given message to binary data (in protobuf wire
 * format), writing to the given BinaryWriter.
 * @param {!proto.sensen.finance.MonteCarloRequest} message
 * @param {!jspb.BinaryWriter} writer
 * @suppress {unusedLocalVariables} f is only used for nested messages
 */
proto.sensen.finance.MonteCarloRequest.serializeBinaryToWriter = function(message, writer) {
  var f = undefined;
  f = message.getSpot();
  if (f !== 0.0) {
    writer.writeDouble(
      1,
      f
    );
  }
  f = message.getStrike();
  if (f !== 0.0) {
    writer.writeDouble(
      2,
      f
    );
  }
  f = message.getRate();
  if (f !== 0.0) {
    writer.writeDouble(
      3,
      f
    );
  }
  f = message.getVolatility();
  if (f !== 0.0) {
    writer.writeDouble(
      4,
      f
    );
  }
  f = message.getYearsToExpiry();
  if (f !== 0.0) {
    writer.writeDouble(
      5,
      f
    );
  }
  f = message.getPaths();
  if (f !== 0) {
    writer.writeInt32(
      6,
      f
    );
  }
  f = message.getSteps();
  if (f !== 0) {
    writer.writeInt32(
      7,
      f
    );
  }
  f = message.getOptionType();
  if (f !== 0.0) {
    writer.writeEnum(
      8,
      f
    );
  }
  f = message.getAsianType();
  if (f !== 0.0) {
    writer.writeEnum(
      9,
      f
    );
  }
  f = message.getNumThreads();
  if (f !== 0) {
    writer.writeInt32(
      10,
      f
    );
  }
};


/**
 * optional double spot = 1;
 * @return {number}
 */
proto.sensen.finance.MonteCarloRequest.prototype.getSpot = function() {
  return /** @type {number} */ (jspb.Message.getFloatingPointFieldWithDefault(this, 1, 0.0));
};


/**
 * @param {number} value
 * @return {!proto.sensen.finance.MonteCarloRequest} returns this
 */
proto.sensen.finance.MonteCarloRequest.prototype.setSpot = function(value) {
  return jspb.Message.setProto3FloatField(this, 1, value);
};


/**
 * optional double strike = 2;
 * @return {number}
 */
proto.sensen.finance.MonteCarloRequest.prototype.getStrike = function() {
  return /** @type {number} */ (jspb.Message.getFloatingPointFieldWithDefault(this, 2, 0.0));
};


/**
 * @param {number} value
 * @return {!proto.sensen.finance.MonteCarloRequest} returns this
 */
proto.sensen.finance.MonteCarloRequest.prototype.setStrike = function(value) {
  return jspb.Message.setProto3FloatField(this, 2, value);
};


/**
 * optional double rate = 3;
 * @return {number}
 */
proto.sensen.finance.MonteCarloRequest.prototype.getRate = function() {
  return /** @type {number} */ (jspb.Message.getFloatingPointFieldWithDefault(this, 3, 0.0));
};


/**
 * @param {number} value
 * @return {!proto.sensen.finance.MonteCarloRequest} returns this
 */
proto.sensen.finance.MonteCarloRequest.prototype.setRate = function(value) {
  return jspb.Message.setProto3FloatField(this, 3, value);
};


/**
 * optional double volatility = 4;
 * @return {number}
 */
proto.sensen.finance.MonteCarloRequest.prototype.getVolatility = function() {
  return /** @type {number} */ (jspb.Message.getFloatingPointFieldWithDefault(this, 4, 0.0));
};


/**
 * @param {number} value
 * @return {!proto.sensen.finance.MonteCarloRequest} returns this
 */
proto.sensen.finance.MonteCarloRequest.prototype.setVolatility = function(value) {
  return jspb.Message.setProto3FloatField(this, 4, value);
};


/**
 * optional double years_to_expiry = 5;
 * @return {number}
 */
proto.sensen.finance.MonteCarloRequest.prototype.getYearsToExpiry = function() {
  return /** @type {number} */ (jspb.Message.getFloatingPointFieldWithDefault(this, 5, 0.0));
};


/**
 * @param {number} value
 * @return {!proto.sensen.finance.MonteCarloRequest} returns this
 */
proto.sensen.finance.MonteCarloRequest.prototype.setYearsToExpiry = function(value) {
  return jspb.Message.setProto3FloatField(this, 5, value);
};


/**
 * optional int32 paths = 6;
 * @return {number}
 */
proto.sensen.finance.MonteCarloRequest.prototype.getPaths = function() {
  return /** @type {number} */ (jspb.Message.getFieldWithDefault(this, 6, 0));
};


/**
 * @param {number} value
 * @return {!proto.sensen.finance.MonteCarloRequest} returns this
 */
proto.sensen.finance.MonteCarloRequest.prototype.setPaths = function(value) {
  return jspb.Message.setProto3IntField(this, 6, value);
};


/**
 * optional int32 steps = 7;
 * @return {number}
 */
proto.sensen.finance.MonteCarloRequest.prototype.getSteps = function() {
  return /** @type {number} */ (jspb.Message.getFieldWithDefault(this, 7, 0));
};


/**
 * @param {number} value
 * @return {!proto.sensen.finance.MonteCarloRequest} returns this
 */
proto.sensen.finance.MonteCarloRequest.prototype.setSteps = function(value) {
  return jspb.Message.setProto3IntField(this, 7, value);
};


/**
 * optional OptionType option_type = 8;
 * @return {!proto.sensen.finance.OptionType}
 */
proto.sensen.finance.MonteCarloRequest.prototype.getOptionType = function() {
  return /** @type {!proto.sensen.finance.OptionType} */ (jspb.Message.getFieldWithDefault(this, 8, 0));
};


/**
 * @param {!proto.sensen.finance.OptionType} value
 * @return {!proto.sensen.finance.MonteCarloRequest} returns this
 */
proto.sensen.finance.MonteCarloRequest.prototype.setOptionType = function(value) {
  return jspb.Message.setProto3EnumField(this, 8, value);
};


/**
 * optional AsianType asian_type = 9;
 * @return {!proto.sensen.finance.AsianType}
 */
proto.sensen.finance.MonteCarloRequest.prototype.getAsianType = function() {
  return /** @type {!proto.sensen.finance.AsianType} */ (jspb.Message.getFieldWithDefault(this, 9, 0));
};


/**
 * @param {!proto.sensen.finance.AsianType} value
 * @return {!proto.sensen.finance.MonteCarloRequest} returns this
 */
proto.sensen.finance.MonteCarloRequest.prototype.setAsianType = function(value) {
  return jspb.Message.setProto3EnumField(this, 9, value);
};


/**
 * optional int32 num_threads = 10;
 * @return {number}
 */
proto.sensen.finance.MonteCarloRequest.prototype.getNumThreads = function() {
  return /** @type {number} */ (jspb.Message.getFieldWithDefault(this, 10, 0));
};


/**
 * @param {number} value
 * @return {!proto.sensen.finance.MonteCarloRequest} returns this
 */
proto.sensen.finance.MonteCarloRequest.prototype.setNumThreads = function(value) {
  return jspb.Message.setProto3IntField(this, 10, value);
};





if (jspb.Message.GENERATE_TO_OBJECT) {
/**
 * Creates an object representation of this proto.
 * Field names that are reserved in JavaScript and will be renamed to pb_name.
 * Optional fields that are not set will be set to undefined.
 * To access a reserved field use, foo.pb_<name>, eg, foo.pb_default.
 * For the list of reserved names please see:
 *     net/proto2/compiler/js/internal/generator.cc#kKeyword.
 * @param {boolean=} opt_includeInstance Deprecated. whether to include the
 *     JSPB instance for transitional soy proto support:
 *     http://goto/soy-param-migration
 * @return {!Object}
 */
proto.sensen.finance.ProbabilityTreeRequest.prototype.toObject = function(opt_includeInstance) {
  return proto.sensen.finance.ProbabilityTreeRequest.toObject(opt_includeInstance, this);
};


/**
 * Static version of the {@see toObject} method.
 * @param {boolean|undefined} includeInstance Deprecated. Whether to include
 *     the JSPB instance for transitional soy proto support:
 *     http://goto/soy-param-migration
 * @param {!proto.sensen.finance.ProbabilityTreeRequest} msg The msg instance to transform.
 * @return {!Object}
 * @suppress {unusedLocalVariables} f is only used for nested messages
 */
proto.sensen.finance.ProbabilityTreeRequest.toObject = function(includeInstance, msg) {
  var f, obj = {
    rate: jspb.Message.getFloatingPointFieldWithDefault(msg, 1, 0.0),
    volatility: jspb.Message.getFloatingPointFieldWithDefault(msg, 2, 0.0),
    yearsToExpiry: jspb.Message.getFloatingPointFieldWithDefault(msg, 3, 0.0),
    steps: jspb.Message.getFieldWithDefault(msg, 4, 0),
    lambda: jspb.Message.getFloatingPointFieldWithDefault(msg, 5, 0.0)
  };

  if (includeInstance) {
    obj.$jspbMessageInstance = msg;
  }
  return obj;
};
}


/**
 * Deserializes binary data (in protobuf wire format).
 * @param {jspb.ByteSource} bytes The bytes to deserialize.
 * @return {!proto.sensen.finance.ProbabilityTreeRequest}
 */
proto.sensen.finance.ProbabilityTreeRequest.deserializeBinary = function(bytes) {
  var reader = new jspb.BinaryReader(bytes);
  var msg = new proto.sensen.finance.ProbabilityTreeRequest;
  return proto.sensen.finance.ProbabilityTreeRequest.deserializeBinaryFromReader(msg, reader);
};


/**
 * Deserializes binary data (in protobuf wire format) from the
 * given reader into the given message object.
 * @param {!proto.sensen.finance.ProbabilityTreeRequest} msg The message object to deserialize into.
 * @param {!jspb.BinaryReader} reader The BinaryReader to use.
 * @return {!proto.sensen.finance.ProbabilityTreeRequest}
 */
proto.sensen.finance.ProbabilityTreeRequest.deserializeBinaryFromReader = function(msg, reader) {
  while (reader.nextField()) {
    if (reader.isEndGroup()) {
      break;
    }
    var field = reader.getFieldNumber();
    switch (field) {
    case 1:
      var value = /** @type {number} */ (reader.readDouble());
      msg.setRate(value);
      break;
    case 2:
      var value = /** @type {number} */ (reader.readDouble());
      msg.setVolatility(value);
      break;
    case 3:
      var value = /** @type {number} */ (reader.readDouble());
      msg.setYearsToExpiry(value);
      break;
    case 4:
      var value = /** @type {number} */ (reader.readInt32());
      msg.setSteps(value);
      break;
    case 5:
      var value = /** @type {number} */ (reader.readDouble());
      msg.setLambda(value);
      break;
    default:
      reader.skipField();
      break;
    }
  }
  return msg;
};


/**
 * Serializes the message to binary data (in protobuf wire format).
 * @return {!Uint8Array}
 */
proto.sensen.finance.ProbabilityTreeRequest.prototype.serializeBinary = function() {
  var writer = new jspb.BinaryWriter();
  proto.sensen.finance.ProbabilityTreeRequest.serializeBinaryToWriter(this, writer);
  return writer.getResultBuffer();
};


/**
 * Serializes the given message to binary data (in protobuf wire
 * format), writing to the given BinaryWriter.
 * @param {!proto.sensen.finance.ProbabilityTreeRequest} message
 * @param {!jspb.BinaryWriter} writer
 * @suppress {unusedLocalVariables} f is only used for nested messages
 */
proto.sensen.finance.ProbabilityTreeRequest.serializeBinaryToWriter = function(message, writer) {
  var f = undefined;
  f = message.getRate();
  if (f !== 0.0) {
    writer.writeDouble(
      1,
      f
    );
  }
  f = message.getVolatility();
  if (f !== 0.0) {
    writer.writeDouble(
      2,
      f
    );
  }
  f = message.getYearsToExpiry();
  if (f !== 0.0) {
    writer.writeDouble(
      3,
      f
    );
  }
  f = message.getSteps();
  if (f !== 0) {
    writer.writeInt32(
      4,
      f
    );
  }
  f = message.getLambda();
  if (f !== 0.0) {
    writer.writeDouble(
      5,
      f
    );
  }
};


/**
 * optional double rate = 1;
 * @return {number}
 */
proto.sensen.finance.ProbabilityTreeRequest.prototype.getRate = function() {
  return /** @type {number} */ (jspb.Message.getFloatingPointFieldWithDefault(this, 1, 0.0));
};


/**
 * @param {number} value
 * @return {!proto.sensen.finance.ProbabilityTreeRequest} returns this
 */
proto.sensen.finance.ProbabilityTreeRequest.prototype.setRate = function(value) {
  return jspb.Message.setProto3FloatField(this, 1, value);
};


/**
 * optional double volatility = 2;
 * @return {number}
 */
proto.sensen.finance.ProbabilityTreeRequest.prototype.getVolatility = function() {
  return /** @type {number} */ (jspb.Message.getFloatingPointFieldWithDefault(this, 2, 0.0));
};


/**
 * @param {number} value
 * @return {!proto.sensen.finance.ProbabilityTreeRequest} returns this
 */
proto.sensen.finance.ProbabilityTreeRequest.prototype.setVolatility = function(value) {
  return jspb.Message.setProto3FloatField(this, 2, value);
};


/**
 * optional double years_to_expiry = 3;
 * @return {number}
 */
proto.sensen.finance.ProbabilityTreeRequest.prototype.getYearsToExpiry = function() {
  return /** @type {number} */ (jspb.Message.getFloatingPointFieldWithDefault(this, 3, 0.0));
};


/**
 * @param {number} value
 * @return {!proto.sensen.finance.ProbabilityTreeRequest} returns this
 */
proto.sensen.finance.ProbabilityTreeRequest.prototype.setYearsToExpiry = function(value) {
  return jspb.Message.setProto3FloatField(this, 3, value);
};


/**
 * optional int32 steps = 4;
 * @return {number}
 */
proto.sensen.finance.ProbabilityTreeRequest.prototype.getSteps = function() {
  return /** @type {number} */ (jspb.Message.getFieldWithDefault(this, 4, 0));
};


/**
 * @param {number} value
 * @return {!proto.sensen.finance.ProbabilityTreeRequest} returns this
 */
proto.sensen.finance.ProbabilityTreeRequest.prototype.setSteps = function(value) {
  return jspb.Message.setProto3IntField(this, 4, value);
};


/**
 * optional double lambda = 5;
 * @return {number}
 */
proto.sensen.finance.ProbabilityTreeRequest.prototype.getLambda = function() {
  return /** @type {number} */ (jspb.Message.getFloatingPointFieldWithDefault(this, 5, 0.0));
};


/**
 * @param {number} value
 * @return {!proto.sensen.finance.ProbabilityTreeRequest} returns this
 */
proto.sensen.finance.ProbabilityTreeRequest.prototype.setLambda = function(value) {
  return jspb.Message.setProto3FloatField(this, 5, value);
};



/**
 * List of repeated fields within this message type.
 * @private {!Array<number>}
 * @const
 */
proto.sensen.finance.ProbabilityTreeResponse.repeatedFields_ = [1,2];



if (jspb.Message.GENERATE_TO_OBJECT) {
/**
 * Creates an object representation of this proto.
 * Field names that are reserved in JavaScript and will be renamed to pb_name.
 * Optional fields that are not set will be set to undefined.
 * To access a reserved field use, foo.pb_<name>, eg, foo.pb_default.
 * For the list of reserved names please see:
 *     net/proto2/compiler/js/internal/generator.cc#kKeyword.
 * @param {boolean=} opt_includeInstance Deprecated. whether to include the
 *     JSPB instance for transitional soy proto support:
 *     http://goto/soy-param-migration
 * @return {!Object}
 */
proto.sensen.finance.ProbabilityTreeResponse.prototype.toObject = function(opt_includeInstance) {
  return proto.sensen.finance.ProbabilityTreeResponse.toObject(opt_includeInstance, this);
};


/**
 * Static version of the {@see toObject} method.
 * @param {boolean|undefined} includeInstance Deprecated. Whether to include
 *     the JSPB instance for transitional soy proto support:
 *     http://goto/soy-param-migration
 * @param {!proto.sensen.finance.ProbabilityTreeResponse} msg The msg instance to transform.
 * @return {!Object}
 * @suppress {unusedLocalVariables} f is only used for nested messages
 */
proto.sensen.finance.ProbabilityTreeResponse.toObject = function(includeInstance, msg) {
  var f, obj = {
    stockPricesList: (f = jspb.Message.getRepeatedFloatingPointField(msg, 1)) == null ? undefined : f,
    stateProbabilitiesList: (f = jspb.Message.getRepeatedFloatingPointField(msg, 2)) == null ? undefined : f,
    steps: jspb.Message.getFieldWithDefault(msg, 3, 0)
  };

  if (includeInstance) {
    obj.$jspbMessageInstance = msg;
  }
  return obj;
};
}


/**
 * Deserializes binary data (in protobuf wire format).
 * @param {jspb.ByteSource} bytes The bytes to deserialize.
 * @return {!proto.sensen.finance.ProbabilityTreeResponse}
 */
proto.sensen.finance.ProbabilityTreeResponse.deserializeBinary = function(bytes) {
  var reader = new jspb.BinaryReader(bytes);
  var msg = new proto.sensen.finance.ProbabilityTreeResponse;
  return proto.sensen.finance.ProbabilityTreeResponse.deserializeBinaryFromReader(msg, reader);
};


/**
 * Deserializes binary data (in protobuf wire format) from the
 * given reader into the given message object.
 * @param {!proto.sensen.finance.ProbabilityTreeResponse} msg The message object to deserialize into.
 * @param {!jspb.BinaryReader} reader The BinaryReader to use.
 * @return {!proto.sensen.finance.ProbabilityTreeResponse}
 */
proto.sensen.finance.ProbabilityTreeResponse.deserializeBinaryFromReader = function(msg, reader) {
  while (reader.nextField()) {
    if (reader.isEndGroup()) {
      break;
    }
    var field = reader.getFieldNumber();
    switch (field) {
    case 1:
      var values = /** @type {!Array<number>} */ (reader.isDelimited() ? reader.readPackedDouble() : [reader.readDouble()]);
      for (var i = 0; i < values.length; i++) {
        msg.addStockPrices(values[i]);
      }
      break;
    case 2:
      var values = /** @type {!Array<number>} */ (reader.isDelimited() ? reader.readPackedDouble() : [reader.readDouble()]);
      for (var i = 0; i < values.length; i++) {
        msg.addStateProbabilities(values[i]);
      }
      break;
    case 3:
      var value = /** @type {number} */ (reader.readInt32());
      msg.setSteps(value);
      break;
    default:
      reader.skipField();
      break;
    }
  }
  return msg;
};


/**
 * Serializes the message to binary data (in protobuf wire format).
 * @return {!Uint8Array}
 */
proto.sensen.finance.ProbabilityTreeResponse.prototype.serializeBinary = function() {
  var writer = new jspb.BinaryWriter();
  proto.sensen.finance.ProbabilityTreeResponse.serializeBinaryToWriter(this, writer);
  return writer.getResultBuffer();
};


/**
 * Serializes the given message to binary data (in protobuf wire
 * format), writing to the given BinaryWriter.
 * @param {!proto.sensen.finance.ProbabilityTreeResponse} message
 * @param {!jspb.BinaryWriter} writer
 * @suppress {unusedLocalVariables} f is only used for nested messages
 */
proto.sensen.finance.ProbabilityTreeResponse.serializeBinaryToWriter = function(message, writer) {
  var f = undefined;
  f = message.getStockPricesList();
  if (f.length > 0) {
    writer.writePackedDouble(
      1,
      f
    );
  }
  f = message.getStateProbabilitiesList();
  if (f.length > 0) {
    writer.writePackedDouble(
      2,
      f
    );
  }
  f = message.getSteps();
  if (f !== 0) {
    writer.writeInt32(
      3,
      f
    );
  }
};


/**
 * repeated double stock_prices = 1;
 * @return {!Array<number>}
 */
proto.sensen.finance.ProbabilityTreeResponse.prototype.getStockPricesList = function() {
  return /** @type {!Array<number>} */ (jspb.Message.getRepeatedFloatingPointField(this, 1));
};


/**
 * @param {!Array<number>} value
 * @return {!proto.sensen.finance.ProbabilityTreeResponse} returns this
 */
proto.sensen.finance.ProbabilityTreeResponse.prototype.setStockPricesList = function(value) {
  return jspb.Message.setField(this, 1, value || []);
};


/**
 * @param {number} value
 * @param {number=} opt_index
 * @return {!proto.sensen.finance.ProbabilityTreeResponse} returns this
 */
proto.sensen.finance.ProbabilityTreeResponse.prototype.addStockPrices = function(value, opt_index) {
  return jspb.Message.addToRepeatedField(this, 1, value, opt_index);
};


/**
 * Clears the list making it empty but non-null.
 * @return {!proto.sensen.finance.ProbabilityTreeResponse} returns this
 */
proto.sensen.finance.ProbabilityTreeResponse.prototype.clearStockPricesList = function() {
  return this.setStockPricesList([]);
};


/**
 * repeated double state_probabilities = 2;
 * @return {!Array<number>}
 */
proto.sensen.finance.ProbabilityTreeResponse.prototype.getStateProbabilitiesList = function() {
  return /** @type {!Array<number>} */ (jspb.Message.getRepeatedFloatingPointField(this, 2));
};


/**
 * @param {!Array<number>} value
 * @return {!proto.sensen.finance.ProbabilityTreeResponse} returns this
 */
proto.sensen.finance.ProbabilityTreeResponse.prototype.setStateProbabilitiesList = function(value) {
  return jspb.Message.setField(this, 2, value || []);
};


/**
 * @param {number} value
 * @param {number=} opt_index
 * @return {!proto.sensen.finance.ProbabilityTreeResponse} returns this
 */
proto.sensen.finance.ProbabilityTreeResponse.prototype.addStateProbabilities = function(value, opt_index) {
  return jspb.Message.addToRepeatedField(this, 2, value, opt_index);
};


/**
 * Clears the list making it empty but non-null.
 * @return {!proto.sensen.finance.ProbabilityTreeResponse} returns this
 */
proto.sensen.finance.ProbabilityTreeResponse.prototype.clearStateProbabilitiesList = function() {
  return this.setStateProbabilitiesList([]);
};


/**
 * optional int32 steps = 3;
 * @return {number}
 */
proto.sensen.finance.ProbabilityTreeResponse.prototype.getSteps = function() {
  return /** @type {number} */ (jspb.Message.getFieldWithDefault(this, 3, 0));
};


/**
 * @param {number} value
 * @return {!proto.sensen.finance.ProbabilityTreeResponse} returns this
 */
proto.sensen.finance.ProbabilityTreeResponse.prototype.setSteps = function(value) {
  return jspb.Message.setProto3IntField(this, 3, value);
};



/**
 * List of repeated fields within this message type.
 * @private {!Array<number>}
 * @const
 */
proto.sensen.finance.PortfolioStatsRequest.repeatedFields_ = [1,2];



if (jspb.Message.GENERATE_TO_OBJECT) {
/**
 * Creates an object representation of this proto.
 * Field names that are reserved in JavaScript and will be renamed to pb_name.
 * Optional fields that are not set will be set to undefined.
 * To access a reserved field use, foo.pb_<name>, eg, foo.pb_default.
 * For the list of reserved names please see:
 *     net/proto2/compiler/js/internal/generator.cc#kKeyword.
 * @param {boolean=} opt_includeInstance Deprecated. whether to include the
 *     JSPB instance for transitional soy proto support:
 *     http://goto/soy-param-migration
 * @return {!Object}
 */
proto.sensen.finance.PortfolioStatsRequest.prototype.toObject = function(opt_includeInstance) {
  return proto.sensen.finance.PortfolioStatsRequest.toObject(opt_includeInstance, this);
};


/**
 * Static version of the {@see toObject} method.
 * @param {boolean|undefined} includeInstance Deprecated. Whether to include
 *     the JSPB instance for transitional soy proto support:
 *     http://goto/soy-param-migration
 * @param {!proto.sensen.finance.PortfolioStatsRequest} msg The msg instance to transform.
 * @return {!Object}
 * @suppress {unusedLocalVariables} f is only used for nested messages
 */
proto.sensen.finance.PortfolioStatsRequest.toObject = function(includeInstance, msg) {
  var f, obj = {
    portfolioReturnsList: (f = jspb.Message.getRepeatedFloatingPointField(msg, 1)) == null ? undefined : f,
    marketReturnsList: (f = jspb.Message.getRepeatedFloatingPointField(msg, 2)) == null ? undefined : f,
    riskFreeRate: jspb.Message.getFloatingPointFieldWithDefault(msg, 3, 0.0)
  };

  if (includeInstance) {
    obj.$jspbMessageInstance = msg;
  }
  return obj;
};
}


/**
 * Deserializes binary data (in protobuf wire format).
 * @param {jspb.ByteSource} bytes The bytes to deserialize.
 * @return {!proto.sensen.finance.PortfolioStatsRequest}
 */
proto.sensen.finance.PortfolioStatsRequest.deserializeBinary = function(bytes) {
  var reader = new jspb.BinaryReader(bytes);
  var msg = new proto.sensen.finance.PortfolioStatsRequest;
  return proto.sensen.finance.PortfolioStatsRequest.deserializeBinaryFromReader(msg, reader);
};


/**
 * Deserializes binary data (in protobuf wire format) from the
 * given reader into the given message object.
 * @param {!proto.sensen.finance.PortfolioStatsRequest} msg The message object to deserialize into.
 * @param {!jspb.BinaryReader} reader The BinaryReader to use.
 * @return {!proto.sensen.finance.PortfolioStatsRequest}
 */
proto.sensen.finance.PortfolioStatsRequest.deserializeBinaryFromReader = function(msg, reader) {
  while (reader.nextField()) {
    if (reader.isEndGroup()) {
      break;
    }
    var field = reader.getFieldNumber();
    switch (field) {
    case 1:
      var values = /** @type {!Array<number>} */ (reader.isDelimited() ? reader.readPackedDouble() : [reader.readDouble()]);
      for (var i = 0; i < values.length; i++) {
        msg.addPortfolioReturns(values[i]);
      }
      break;
    case 2:
      var values = /** @type {!Array<number>} */ (reader.isDelimited() ? reader.readPackedDouble() : [reader.readDouble()]);
      for (var i = 0; i < values.length; i++) {
        msg.addMarketReturns(values[i]);
      }
      break;
    case 3:
      var value = /** @type {number} */ (reader.readDouble());
      msg.setRiskFreeRate(value);
      break;
    default:
      reader.skipField();
      break;
    }
  }
  return msg;
};


/**
 * Serializes the message to binary data (in protobuf wire format).
 * @return {!Uint8Array}
 */
proto.sensen.finance.PortfolioStatsRequest.prototype.serializeBinary = function() {
  var writer = new jspb.BinaryWriter();
  proto.sensen.finance.PortfolioStatsRequest.serializeBinaryToWriter(this, writer);
  return writer.getResultBuffer();
};


/**
 * Serializes the given message to binary data (in protobuf wire
 * format), writing to the given BinaryWriter.
 * @param {!proto.sensen.finance.PortfolioStatsRequest} message
 * @param {!jspb.BinaryWriter} writer
 * @suppress {unusedLocalVariables} f is only used for nested messages
 */
proto.sensen.finance.PortfolioStatsRequest.serializeBinaryToWriter = function(message, writer) {
  var f = undefined;
  f = message.getPortfolioReturnsList();
  if (f.length > 0) {
    writer.writePackedDouble(
      1,
      f
    );
  }
  f = message.getMarketReturnsList();
  if (f.length > 0) {
    writer.writePackedDouble(
      2,
      f
    );
  }
  f = message.getRiskFreeRate();
  if (f !== 0.0) {
    writer.writeDouble(
      3,
      f
    );
  }
};


/**
 * repeated double portfolio_returns = 1;
 * @return {!Array<number>}
 */
proto.sensen.finance.PortfolioStatsRequest.prototype.getPortfolioReturnsList = function() {
  return /** @type {!Array<number>} */ (jspb.Message.getRepeatedFloatingPointField(this, 1));
};


/**
 * @param {!Array<number>} value
 * @return {!proto.sensen.finance.PortfolioStatsRequest} returns this
 */
proto.sensen.finance.PortfolioStatsRequest.prototype.setPortfolioReturnsList = function(value) {
  return jspb.Message.setField(this, 1, value || []);
};


/**
 * @param {number} value
 * @param {number=} opt_index
 * @return {!proto.sensen.finance.PortfolioStatsRequest} returns this
 */
proto.sensen.finance.PortfolioStatsRequest.prototype.addPortfolioReturns = function(value, opt_index) {
  return jspb.Message.addToRepeatedField(this, 1, value, opt_index);
};


/**
 * Clears the list making it empty but non-null.
 * @return {!proto.sensen.finance.PortfolioStatsRequest} returns this
 */
proto.sensen.finance.PortfolioStatsRequest.prototype.clearPortfolioReturnsList = function() {
  return this.setPortfolioReturnsList([]);
};


/**
 * repeated double market_returns = 2;
 * @return {!Array<number>}
 */
proto.sensen.finance.PortfolioStatsRequest.prototype.getMarketReturnsList = function() {
  return /** @type {!Array<number>} */ (jspb.Message.getRepeatedFloatingPointField(this, 2));
};


/**
 * @param {!Array<number>} value
 * @return {!proto.sensen.finance.PortfolioStatsRequest} returns this
 */
proto.sensen.finance.PortfolioStatsRequest.prototype.setMarketReturnsList = function(value) {
  return jspb.Message.setField(this, 2, value || []);
};


/**
 * @param {number} value
 * @param {number=} opt_index
 * @return {!proto.sensen.finance.PortfolioStatsRequest} returns this
 */
proto.sensen.finance.PortfolioStatsRequest.prototype.addMarketReturns = function(value, opt_index) {
  return jspb.Message.addToRepeatedField(this, 2, value, opt_index);
};


/**
 * Clears the list making it empty but non-null.
 * @return {!proto.sensen.finance.PortfolioStatsRequest} returns this
 */
proto.sensen.finance.PortfolioStatsRequest.prototype.clearMarketReturnsList = function() {
  return this.setMarketReturnsList([]);
};


/**
 * optional double risk_free_rate = 3;
 * @return {number}
 */
proto.sensen.finance.PortfolioStatsRequest.prototype.getRiskFreeRate = function() {
  return /** @type {number} */ (jspb.Message.getFloatingPointFieldWithDefault(this, 3, 0.0));
};


/**
 * @param {number} value
 * @return {!proto.sensen.finance.PortfolioStatsRequest} returns this
 */
proto.sensen.finance.PortfolioStatsRequest.prototype.setRiskFreeRate = function(value) {
  return jspb.Message.setProto3FloatField(this, 3, value);
};





if (jspb.Message.GENERATE_TO_OBJECT) {
/**
 * Creates an object representation of this proto.
 * Field names that are reserved in JavaScript and will be renamed to pb_name.
 * Optional fields that are not set will be set to undefined.
 * To access a reserved field use, foo.pb_<name>, eg, foo.pb_default.
 * For the list of reserved names please see:
 *     net/proto2/compiler/js/internal/generator.cc#kKeyword.
 * @param {boolean=} opt_includeInstance Deprecated. whether to include the
 *     JSPB instance for transitional soy proto support:
 *     http://goto/soy-param-migration
 * @return {!Object}
 */
proto.sensen.finance.PortfolioStatsResponse.prototype.toObject = function(opt_includeInstance) {
  return proto.sensen.finance.PortfolioStatsResponse.toObject(opt_includeInstance, this);
};


/**
 * Static version of the {@see toObject} method.
 * @param {boolean|undefined} includeInstance Deprecated. Whether to include
 *     the JSPB instance for transitional soy proto support:
 *     http://goto/soy-param-migration
 * @param {!proto.sensen.finance.PortfolioStatsResponse} msg The msg instance to transform.
 * @return {!Object}
 * @suppress {unusedLocalVariables} f is only used for nested messages
 */
proto.sensen.finance.PortfolioStatsResponse.toObject = function(includeInstance, msg) {
  var f, obj = {
    sharpeRatio: jspb.Message.getFloatingPointFieldWithDefault(msg, 1, 0.0),
    sortinoRatio: jspb.Message.getFloatingPointFieldWithDefault(msg, 2, 0.0),
    treynorRatio: jspb.Message.getFloatingPointFieldWithDefault(msg, 3, 0.0),
    beta: jspb.Message.getFloatingPointFieldWithDefault(msg, 4, 0.0),
    alpha: jspb.Message.getFloatingPointFieldWithDefault(msg, 5, 0.0),
    maxDrawdown: jspb.Message.getFloatingPointFieldWithDefault(msg, 6, 0.0),
    varHistorical95: jspb.Message.getFloatingPointFieldWithDefault(msg, 7, 0.0),
    varHistorical99: jspb.Message.getFloatingPointFieldWithDefault(msg, 8, 0.0),
    cvarHistorical95: jspb.Message.getFloatingPointFieldWithDefault(msg, 9, 0.0),
    cvarHistorical99: jspb.Message.getFloatingPointFieldWithDefault(msg, 10, 0.0),
    varParametric95: jspb.Message.getFloatingPointFieldWithDefault(msg, 11, 0.0),
    varParametric99: jspb.Message.getFloatingPointFieldWithDefault(msg, 12, 0.0),
    cvarParametric95: jspb.Message.getFloatingPointFieldWithDefault(msg, 13, 0.0),
    cvarParametric99: jspb.Message.getFloatingPointFieldWithDefault(msg, 14, 0.0),
    omegaRatio: jspb.Message.getFloatingPointFieldWithDefault(msg, 15, 0.0),
    calmarRatio: jspb.Message.getFloatingPointFieldWithDefault(msg, 16, 0.0),
    informationRatio: jspb.Message.getFloatingPointFieldWithDefault(msg, 17, 0.0),
    trackingError: jspb.Message.getFloatingPointFieldWithDefault(msg, 18, 0.0),
    benchmarkSupplied: jspb.Message.getBooleanFieldWithDefault(msg, 19, false)
  };

  if (includeInstance) {
    obj.$jspbMessageInstance = msg;
  }
  return obj;
};
}


/**
 * Deserializes binary data (in protobuf wire format).
 * @param {jspb.ByteSource} bytes The bytes to deserialize.
 * @return {!proto.sensen.finance.PortfolioStatsResponse}
 */
proto.sensen.finance.PortfolioStatsResponse.deserializeBinary = function(bytes) {
  var reader = new jspb.BinaryReader(bytes);
  var msg = new proto.sensen.finance.PortfolioStatsResponse;
  return proto.sensen.finance.PortfolioStatsResponse.deserializeBinaryFromReader(msg, reader);
};


/**
 * Deserializes binary data (in protobuf wire format) from the
 * given reader into the given message object.
 * @param {!proto.sensen.finance.PortfolioStatsResponse} msg The message object to deserialize into.
 * @param {!jspb.BinaryReader} reader The BinaryReader to use.
 * @return {!proto.sensen.finance.PortfolioStatsResponse}
 */
proto.sensen.finance.PortfolioStatsResponse.deserializeBinaryFromReader = function(msg, reader) {
  while (reader.nextField()) {
    if (reader.isEndGroup()) {
      break;
    }
    var field = reader.getFieldNumber();
    switch (field) {
    case 1:
      var value = /** @type {number} */ (reader.readDouble());
      msg.setSharpeRatio(value);
      break;
    case 2:
      var value = /** @type {number} */ (reader.readDouble());
      msg.setSortinoRatio(value);
      break;
    case 3:
      var value = /** @type {number} */ (reader.readDouble());
      msg.setTreynorRatio(value);
      break;
    case 4:
      var value = /** @type {number} */ (reader.readDouble());
      msg.setBeta(value);
      break;
    case 5:
      var value = /** @type {number} */ (reader.readDouble());
      msg.setAlpha(value);
      break;
    case 6:
      var value = /** @type {number} */ (reader.readDouble());
      msg.setMaxDrawdown(value);
      break;
    case 7:
      var value = /** @type {number} */ (reader.readDouble());
      msg.setVarHistorical95(value);
      break;
    case 8:
      var value = /** @type {number} */ (reader.readDouble());
      msg.setVarHistorical99(value);
      break;
    case 9:
      var value = /** @type {number} */ (reader.readDouble());
      msg.setCvarHistorical95(value);
      break;
    case 10:
      var value = /** @type {number} */ (reader.readDouble());
      msg.setCvarHistorical99(value);
      break;
    case 11:
      var value = /** @type {number} */ (reader.readDouble());
      msg.setVarParametric95(value);
      break;
    case 12:
      var value = /** @type {number} */ (reader.readDouble());
      msg.setVarParametric99(value);
      break;
    case 13:
      var value = /** @type {number} */ (reader.readDouble());
      msg.setCvarParametric95(value);
      break;
    case 14:
      var value = /** @type {number} */ (reader.readDouble());
      msg.setCvarParametric99(value);
      break;
    case 15:
      var value = /** @type {number} */ (reader.readDouble());
      msg.setOmegaRatio(value);
      break;
    case 16:
      var value = /** @type {number} */ (reader.readDouble());
      msg.setCalmarRatio(value);
      break;
    case 17:
      var value = /** @type {number} */ (reader.readDouble());
      msg.setInformationRatio(value);
      break;
    case 18:
      var value = /** @type {number} */ (reader.readDouble());
      msg.setTrackingError(value);
      break;
    case 19:
      var value = /** @type {boolean} */ (reader.readBool());
      msg.setBenchmarkSupplied(value);
      break;
    default:
      reader.skipField();
      break;
    }
  }
  return msg;
};


/**
 * Serializes the message to binary data (in protobuf wire format).
 * @return {!Uint8Array}
 */
proto.sensen.finance.PortfolioStatsResponse.prototype.serializeBinary = function() {
  var writer = new jspb.BinaryWriter();
  proto.sensen.finance.PortfolioStatsResponse.serializeBinaryToWriter(this, writer);
  return writer.getResultBuffer();
};


/**
 * Serializes the given message to binary data (in protobuf wire
 * format), writing to the given BinaryWriter.
 * @param {!proto.sensen.finance.PortfolioStatsResponse} message
 * @param {!jspb.BinaryWriter} writer
 * @suppress {unusedLocalVariables} f is only used for nested messages
 */
proto.sensen.finance.PortfolioStatsResponse.serializeBinaryToWriter = function(message, writer) {
  var f = undefined;
  f = message.getSharpeRatio();
  if (f !== 0.0) {
    writer.writeDouble(
      1,
      f
    );
  }
  f = message.getSortinoRatio();
  if (f !== 0.0) {
    writer.writeDouble(
      2,
      f
    );
  }
  f = message.getTreynorRatio();
  if (f !== 0.0) {
    writer.writeDouble(
      3,
      f
    );
  }
  f = message.getBeta();
  if (f !== 0.0) {
    writer.writeDouble(
      4,
      f
    );
  }
  f = message.getAlpha();
  if (f !== 0.0) {
    writer.writeDouble(
      5,
      f
    );
  }
  f = message.getMaxDrawdown();
  if (f !== 0.0) {
    writer.writeDouble(
      6,
      f
    );
  }
  f = message.getVarHistorical95();
  if (f !== 0.0) {
    writer.writeDouble(
      7,
      f
    );
  }
  f = message.getVarHistorical99();
  if (f !== 0.0) {
    writer.writeDouble(
      8,
      f
    );
  }
  f = message.getCvarHistorical95();
  if (f !== 0.0) {
    writer.writeDouble(
      9,
      f
    );
  }
  f = message.getCvarHistorical99();
  if (f !== 0.0) {
    writer.writeDouble(
      10,
      f
    );
  }
  f = message.getVarParametric95();
  if (f !== 0.0) {
    writer.writeDouble(
      11,
      f
    );
  }
  f = message.getVarParametric99();
  if (f !== 0.0) {
    writer.writeDouble(
      12,
      f
    );
  }
  f = message.getCvarParametric95();
  if (f !== 0.0) {
    writer.writeDouble(
      13,
      f
    );
  }
  f = message.getCvarParametric99();
  if (f !== 0.0) {
    writer.writeDouble(
      14,
      f
    );
  }
  f = message.getOmegaRatio();
  if (f !== 0.0) {
    writer.writeDouble(
      15,
      f
    );
  }
  f = message.getCalmarRatio();
  if (f !== 0.0) {
    writer.writeDouble(
      16,
      f
    );
  }
  f = message.getInformationRatio();
  if (f !== 0.0) {
    writer.writeDouble(
      17,
      f
    );
  }
  f = message.getTrackingError();
  if (f !== 0.0) {
    writer.writeDouble(
      18,
      f
    );
  }
  f = message.getBenchmarkSupplied();
  if (f) {
    writer.writeBool(
      19,
      f
    );
  }
};


/**
 * optional double sharpe_ratio = 1;
 * @return {number}
 */
proto.sensen.finance.PortfolioStatsResponse.prototype.getSharpeRatio = function() {
  return /** @type {number} */ (jspb.Message.getFloatingPointFieldWithDefault(this, 1, 0.0));
};


/**
 * @param {number} value
 * @return {!proto.sensen.finance.PortfolioStatsResponse} returns this
 */
proto.sensen.finance.PortfolioStatsResponse.prototype.setSharpeRatio = function(value) {
  return jspb.Message.setProto3FloatField(this, 1, value);
};


/**
 * optional double sortino_ratio = 2;
 * @return {number}
 */
proto.sensen.finance.PortfolioStatsResponse.prototype.getSortinoRatio = function() {
  return /** @type {number} */ (jspb.Message.getFloatingPointFieldWithDefault(this, 2, 0.0));
};


/**
 * @param {number} value
 * @return {!proto.sensen.finance.PortfolioStatsResponse} returns this
 */
proto.sensen.finance.PortfolioStatsResponse.prototype.setSortinoRatio = function(value) {
  return jspb.Message.setProto3FloatField(this, 2, value);
};


/**
 * optional double treynor_ratio = 3;
 * @return {number}
 */
proto.sensen.finance.PortfolioStatsResponse.prototype.getTreynorRatio = function() {
  return /** @type {number} */ (jspb.Message.getFloatingPointFieldWithDefault(this, 3, 0.0));
};


/**
 * @param {number} value
 * @return {!proto.sensen.finance.PortfolioStatsResponse} returns this
 */
proto.sensen.finance.PortfolioStatsResponse.prototype.setTreynorRatio = function(value) {
  return jspb.Message.setProto3FloatField(this, 3, value);
};


/**
 * optional double beta = 4;
 * @return {number}
 */
proto.sensen.finance.PortfolioStatsResponse.prototype.getBeta = function() {
  return /** @type {number} */ (jspb.Message.getFloatingPointFieldWithDefault(this, 4, 0.0));
};


/**
 * @param {number} value
 * @return {!proto.sensen.finance.PortfolioStatsResponse} returns this
 */
proto.sensen.finance.PortfolioStatsResponse.prototype.setBeta = function(value) {
  return jspb.Message.setProto3FloatField(this, 4, value);
};


/**
 * optional double alpha = 5;
 * @return {number}
 */
proto.sensen.finance.PortfolioStatsResponse.prototype.getAlpha = function() {
  return /** @type {number} */ (jspb.Message.getFloatingPointFieldWithDefault(this, 5, 0.0));
};


/**
 * @param {number} value
 * @return {!proto.sensen.finance.PortfolioStatsResponse} returns this
 */
proto.sensen.finance.PortfolioStatsResponse.prototype.setAlpha = function(value) {
  return jspb.Message.setProto3FloatField(this, 5, value);
};


/**
 * optional double max_drawdown = 6;
 * @return {number}
 */
proto.sensen.finance.PortfolioStatsResponse.prototype.getMaxDrawdown = function() {
  return /** @type {number} */ (jspb.Message.getFloatingPointFieldWithDefault(this, 6, 0.0));
};


/**
 * @param {number} value
 * @return {!proto.sensen.finance.PortfolioStatsResponse} returns this
 */
proto.sensen.finance.PortfolioStatsResponse.prototype.setMaxDrawdown = function(value) {
  return jspb.Message.setProto3FloatField(this, 6, value);
};


/**
 * optional double var_historical_95 = 7;
 * @return {number}
 */
proto.sensen.finance.PortfolioStatsResponse.prototype.getVarHistorical95 = function() {
  return /** @type {number} */ (jspb.Message.getFloatingPointFieldWithDefault(this, 7, 0.0));
};


/**
 * @param {number} value
 * @return {!proto.sensen.finance.PortfolioStatsResponse} returns this
 */
proto.sensen.finance.PortfolioStatsResponse.prototype.setVarHistorical95 = function(value) {
  return jspb.Message.setProto3FloatField(this, 7, value);
};


/**
 * optional double var_historical_99 = 8;
 * @return {number}
 */
proto.sensen.finance.PortfolioStatsResponse.prototype.getVarHistorical99 = function() {
  return /** @type {number} */ (jspb.Message.getFloatingPointFieldWithDefault(this, 8, 0.0));
};


/**
 * @param {number} value
 * @return {!proto.sensen.finance.PortfolioStatsResponse} returns this
 */
proto.sensen.finance.PortfolioStatsResponse.prototype.setVarHistorical99 = function(value) {
  return jspb.Message.setProto3FloatField(this, 8, value);
};


/**
 * optional double cvar_historical_95 = 9;
 * @return {number}
 */
proto.sensen.finance.PortfolioStatsResponse.prototype.getCvarHistorical95 = function() {
  return /** @type {number} */ (jspb.Message.getFloatingPointFieldWithDefault(this, 9, 0.0));
};


/**
 * @param {number} value
 * @return {!proto.sensen.finance.PortfolioStatsResponse} returns this
 */
proto.sensen.finance.PortfolioStatsResponse.prototype.setCvarHistorical95 = function(value) {
  return jspb.Message.setProto3FloatField(this, 9, value);
};


/**
 * optional double cvar_historical_99 = 10;
 * @return {number}
 */
proto.sensen.finance.PortfolioStatsResponse.prototype.getCvarHistorical99 = function() {
  return /** @type {number} */ (jspb.Message.getFloatingPointFieldWithDefault(this, 10, 0.0));
};


/**
 * @param {number} value
 * @return {!proto.sensen.finance.PortfolioStatsResponse} returns this
 */
proto.sensen.finance.PortfolioStatsResponse.prototype.setCvarHistorical99 = function(value) {
  return jspb.Message.setProto3FloatField(this, 10, value);
};


/**
 * optional double var_parametric_95 = 11;
 * @return {number}
 */
proto.sensen.finance.PortfolioStatsResponse.prototype.getVarParametric95 = function() {
  return /** @type {number} */ (jspb.Message.getFloatingPointFieldWithDefault(this, 11, 0.0));
};


/**
 * @param {number} value
 * @return {!proto.sensen.finance.PortfolioStatsResponse} returns this
 */
proto.sensen.finance.PortfolioStatsResponse.prototype.setVarParametric95 = function(value) {
  return jspb.Message.setProto3FloatField(this, 11, value);
};


/**
 * optional double var_parametric_99 = 12;
 * @return {number}
 */
proto.sensen.finance.PortfolioStatsResponse.prototype.getVarParametric99 = function() {
  return /** @type {number} */ (jspb.Message.getFloatingPointFieldWithDefault(this, 12, 0.0));
};


/**
 * @param {number} value
 * @return {!proto.sensen.finance.PortfolioStatsResponse} returns this
 */
proto.sensen.finance.PortfolioStatsResponse.prototype.setVarParametric99 = function(value) {
  return jspb.Message.setProto3FloatField(this, 12, value);
};


/**
 * optional double cvar_parametric_95 = 13;
 * @return {number}
 */
proto.sensen.finance.PortfolioStatsResponse.prototype.getCvarParametric95 = function() {
  return /** @type {number} */ (jspb.Message.getFloatingPointFieldWithDefault(this, 13, 0.0));
};


/**
 * @param {number} value
 * @return {!proto.sensen.finance.PortfolioStatsResponse} returns this
 */
proto.sensen.finance.PortfolioStatsResponse.prototype.setCvarParametric95 = function(value) {
  return jspb.Message.setProto3FloatField(this, 13, value);
};


/**
 * optional double cvar_parametric_99 = 14;
 * @return {number}
 */
proto.sensen.finance.PortfolioStatsResponse.prototype.getCvarParametric99 = function() {
  return /** @type {number} */ (jspb.Message.getFloatingPointFieldWithDefault(this, 14, 0.0));
};


/**
 * @param {number} value
 * @return {!proto.sensen.finance.PortfolioStatsResponse} returns this
 */
proto.sensen.finance.PortfolioStatsResponse.prototype.setCvarParametric99 = function(value) {
  return jspb.Message.setProto3FloatField(this, 14, value);
};


/**
 * optional double omega_ratio = 15;
 * @return {number}
 */
proto.sensen.finance.PortfolioStatsResponse.prototype.getOmegaRatio = function() {
  return /** @type {number} */ (jspb.Message.getFloatingPointFieldWithDefault(this, 15, 0.0));
};


/**
 * @param {number} value
 * @return {!proto.sensen.finance.PortfolioStatsResponse} returns this
 */
proto.sensen.finance.PortfolioStatsResponse.prototype.setOmegaRatio = function(value) {
  return jspb.Message.setProto3FloatField(this, 15, value);
};


/**
 * optional double calmar_ratio = 16;
 * @return {number}
 */
proto.sensen.finance.PortfolioStatsResponse.prototype.getCalmarRatio = function() {
  return /** @type {number} */ (jspb.Message.getFloatingPointFieldWithDefault(this, 16, 0.0));
};


/**
 * @param {number} value
 * @return {!proto.sensen.finance.PortfolioStatsResponse} returns this
 */
proto.sensen.finance.PortfolioStatsResponse.prototype.setCalmarRatio = function(value) {
  return jspb.Message.setProto3FloatField(this, 16, value);
};


/**
 * optional double information_ratio = 17;
 * @return {number}
 */
proto.sensen.finance.PortfolioStatsResponse.prototype.getInformationRatio = function() {
  return /** @type {number} */ (jspb.Message.getFloatingPointFieldWithDefault(this, 17, 0.0));
};


/**
 * @param {number} value
 * @return {!proto.sensen.finance.PortfolioStatsResponse} returns this
 */
proto.sensen.finance.PortfolioStatsResponse.prototype.setInformationRatio = function(value) {
  return jspb.Message.setProto3FloatField(this, 17, value);
};


/**
 * optional double tracking_error = 18;
 * @return {number}
 */
proto.sensen.finance.PortfolioStatsResponse.prototype.getTrackingError = function() {
  return /** @type {number} */ (jspb.Message.getFloatingPointFieldWithDefault(this, 18, 0.0));
};


/**
 * @param {number} value
 * @return {!proto.sensen.finance.PortfolioStatsResponse} returns this
 */
proto.sensen.finance.PortfolioStatsResponse.prototype.setTrackingError = function(value) {
  return jspb.Message.setProto3FloatField(this, 18, value);
};


/**
 * optional bool benchmark_supplied = 19;
 * @return {boolean}
 */
proto.sensen.finance.PortfolioStatsResponse.prototype.getBenchmarkSupplied = function() {
  return /** @type {boolean} */ (jspb.Message.getBooleanFieldWithDefault(this, 19, false));
};


/**
 * @param {boolean} value
 * @return {!proto.sensen.finance.PortfolioStatsResponse} returns this
 */
proto.sensen.finance.PortfolioStatsResponse.prototype.setBenchmarkSupplied = function(value) {
  return jspb.Message.setProto3BooleanField(this, 19, value);
};



/**
 * List of repeated fields within this message type.
 * @private {!Array<number>}
 * @const
 */
proto.sensen.finance.PortfolioOptimizeRequest.repeatedFields_ = [1,2];



if (jspb.Message.GENERATE_TO_OBJECT) {
/**
 * Creates an object representation of this proto.
 * Field names that are reserved in JavaScript and will be renamed to pb_name.
 * Optional fields that are not set will be set to undefined.
 * To access a reserved field use, foo.pb_<name>, eg, foo.pb_default.
 * For the list of reserved names please see:
 *     net/proto2/compiler/js/internal/generator.cc#kKeyword.
 * @param {boolean=} opt_includeInstance Deprecated. whether to include the
 *     JSPB instance for transitional soy proto support:
 *     http://goto/soy-param-migration
 * @return {!Object}
 */
proto.sensen.finance.PortfolioOptimizeRequest.prototype.toObject = function(opt_includeInstance) {
  return proto.sensen.finance.PortfolioOptimizeRequest.toObject(opt_includeInstance, this);
};


/**
 * Static version of the {@see toObject} method.
 * @param {boolean|undefined} includeInstance Deprecated. Whether to include
 *     the JSPB instance for transitional soy proto support:
 *     http://goto/soy-param-migration
 * @param {!proto.sensen.finance.PortfolioOptimizeRequest} msg The msg instance to transform.
 * @return {!Object}
 * @suppress {unusedLocalVariables} f is only used for nested messages
 */
proto.sensen.finance.PortfolioOptimizeRequest.toObject = function(includeInstance, msg) {
  var f, obj = {
    expectedReturnsList: (f = jspb.Message.getRepeatedFloatingPointField(msg, 1)) == null ? undefined : f,
    covarianceList: (f = jspb.Message.getRepeatedFloatingPointField(msg, 2)) == null ? undefined : f,
    size: jspb.Message.getFieldWithDefault(msg, 3, 0),
    riskFreeRate: jspb.Message.getFloatingPointFieldWithDefault(msg, 4, 0.0),
    maxSharpe: jspb.Message.getBooleanFieldWithDefault(msg, 5, false)
  };

  if (includeInstance) {
    obj.$jspbMessageInstance = msg;
  }
  return obj;
};
}


/**
 * Deserializes binary data (in protobuf wire format).
 * @param {jspb.ByteSource} bytes The bytes to deserialize.
 * @return {!proto.sensen.finance.PortfolioOptimizeRequest}
 */
proto.sensen.finance.PortfolioOptimizeRequest.deserializeBinary = function(bytes) {
  var reader = new jspb.BinaryReader(bytes);
  var msg = new proto.sensen.finance.PortfolioOptimizeRequest;
  return proto.sensen.finance.PortfolioOptimizeRequest.deserializeBinaryFromReader(msg, reader);
};


/**
 * Deserializes binary data (in protobuf wire format) from the
 * given reader into the given message object.
 * @param {!proto.sensen.finance.PortfolioOptimizeRequest} msg The message object to deserialize into.
 * @param {!jspb.BinaryReader} reader The BinaryReader to use.
 * @return {!proto.sensen.finance.PortfolioOptimizeRequest}
 */
proto.sensen.finance.PortfolioOptimizeRequest.deserializeBinaryFromReader = function(msg, reader) {
  while (reader.nextField()) {
    if (reader.isEndGroup()) {
      break;
    }
    var field = reader.getFieldNumber();
    switch (field) {
    case 1:
      var values = /** @type {!Array<number>} */ (reader.isDelimited() ? reader.readPackedDouble() : [reader.readDouble()]);
      for (var i = 0; i < values.length; i++) {
        msg.addExpectedReturns(values[i]);
      }
      break;
    case 2:
      var values = /** @type {!Array<number>} */ (reader.isDelimited() ? reader.readPackedDouble() : [reader.readDouble()]);
      for (var i = 0; i < values.length; i++) {
        msg.addCovariance(values[i]);
      }
      break;
    case 3:
      var value = /** @type {number} */ (reader.readInt32());
      msg.setSize(value);
      break;
    case 4:
      var value = /** @type {number} */ (reader.readDouble());
      msg.setRiskFreeRate(value);
      break;
    case 5:
      var value = /** @type {boolean} */ (reader.readBool());
      msg.setMaxSharpe(value);
      break;
    default:
      reader.skipField();
      break;
    }
  }
  return msg;
};


/**
 * Serializes the message to binary data (in protobuf wire format).
 * @return {!Uint8Array}
 */
proto.sensen.finance.PortfolioOptimizeRequest.prototype.serializeBinary = function() {
  var writer = new jspb.BinaryWriter();
  proto.sensen.finance.PortfolioOptimizeRequest.serializeBinaryToWriter(this, writer);
  return writer.getResultBuffer();
};


/**
 * Serializes the given message to binary data (in protobuf wire
 * format), writing to the given BinaryWriter.
 * @param {!proto.sensen.finance.PortfolioOptimizeRequest} message
 * @param {!jspb.BinaryWriter} writer
 * @suppress {unusedLocalVariables} f is only used for nested messages
 */
proto.sensen.finance.PortfolioOptimizeRequest.serializeBinaryToWriter = function(message, writer) {
  var f = undefined;
  f = message.getExpectedReturnsList();
  if (f.length > 0) {
    writer.writePackedDouble(
      1,
      f
    );
  }
  f = message.getCovarianceList();
  if (f.length > 0) {
    writer.writePackedDouble(
      2,
      f
    );
  }
  f = message.getSize();
  if (f !== 0) {
    writer.writeInt32(
      3,
      f
    );
  }
  f = message.getRiskFreeRate();
  if (f !== 0.0) {
    writer.writeDouble(
      4,
      f
    );
  }
  f = message.getMaxSharpe();
  if (f) {
    writer.writeBool(
      5,
      f
    );
  }
};


/**
 * repeated double expected_returns = 1;
 * @return {!Array<number>}
 */
proto.sensen.finance.PortfolioOptimizeRequest.prototype.getExpectedReturnsList = function() {
  return /** @type {!Array<number>} */ (jspb.Message.getRepeatedFloatingPointField(this, 1));
};


/**
 * @param {!Array<number>} value
 * @return {!proto.sensen.finance.PortfolioOptimizeRequest} returns this
 */
proto.sensen.finance.PortfolioOptimizeRequest.prototype.setExpectedReturnsList = function(value) {
  return jspb.Message.setField(this, 1, value || []);
};


/**
 * @param {number} value
 * @param {number=} opt_index
 * @return {!proto.sensen.finance.PortfolioOptimizeRequest} returns this
 */
proto.sensen.finance.PortfolioOptimizeRequest.prototype.addExpectedReturns = function(value, opt_index) {
  return jspb.Message.addToRepeatedField(this, 1, value, opt_index);
};


/**
 * Clears the list making it empty but non-null.
 * @return {!proto.sensen.finance.PortfolioOptimizeRequest} returns this
 */
proto.sensen.finance.PortfolioOptimizeRequest.prototype.clearExpectedReturnsList = function() {
  return this.setExpectedReturnsList([]);
};


/**
 * repeated double covariance = 2;
 * @return {!Array<number>}
 */
proto.sensen.finance.PortfolioOptimizeRequest.prototype.getCovarianceList = function() {
  return /** @type {!Array<number>} */ (jspb.Message.getRepeatedFloatingPointField(this, 2));
};


/**
 * @param {!Array<number>} value
 * @return {!proto.sensen.finance.PortfolioOptimizeRequest} returns this
 */
proto.sensen.finance.PortfolioOptimizeRequest.prototype.setCovarianceList = function(value) {
  return jspb.Message.setField(this, 2, value || []);
};


/**
 * @param {number} value
 * @param {number=} opt_index
 * @return {!proto.sensen.finance.PortfolioOptimizeRequest} returns this
 */
proto.sensen.finance.PortfolioOptimizeRequest.prototype.addCovariance = function(value, opt_index) {
  return jspb.Message.addToRepeatedField(this, 2, value, opt_index);
};


/**
 * Clears the list making it empty but non-null.
 * @return {!proto.sensen.finance.PortfolioOptimizeRequest} returns this
 */
proto.sensen.finance.PortfolioOptimizeRequest.prototype.clearCovarianceList = function() {
  return this.setCovarianceList([]);
};


/**
 * optional int32 size = 3;
 * @return {number}
 */
proto.sensen.finance.PortfolioOptimizeRequest.prototype.getSize = function() {
  return /** @type {number} */ (jspb.Message.getFieldWithDefault(this, 3, 0));
};


/**
 * @param {number} value
 * @return {!proto.sensen.finance.PortfolioOptimizeRequest} returns this
 */
proto.sensen.finance.PortfolioOptimizeRequest.prototype.setSize = function(value) {
  return jspb.Message.setProto3IntField(this, 3, value);
};


/**
 * optional double risk_free_rate = 4;
 * @return {number}
 */
proto.sensen.finance.PortfolioOptimizeRequest.prototype.getRiskFreeRate = function() {
  return /** @type {number} */ (jspb.Message.getFloatingPointFieldWithDefault(this, 4, 0.0));
};


/**
 * @param {number} value
 * @return {!proto.sensen.finance.PortfolioOptimizeRequest} returns this
 */
proto.sensen.finance.PortfolioOptimizeRequest.prototype.setRiskFreeRate = function(value) {
  return jspb.Message.setProto3FloatField(this, 4, value);
};


/**
 * optional bool max_sharpe = 5;
 * @return {boolean}
 */
proto.sensen.finance.PortfolioOptimizeRequest.prototype.getMaxSharpe = function() {
  return /** @type {boolean} */ (jspb.Message.getBooleanFieldWithDefault(this, 5, false));
};


/**
 * @param {boolean} value
 * @return {!proto.sensen.finance.PortfolioOptimizeRequest} returns this
 */
proto.sensen.finance.PortfolioOptimizeRequest.prototype.setMaxSharpe = function(value) {
  return jspb.Message.setProto3BooleanField(this, 5, value);
};



/**
 * List of repeated fields within this message type.
 * @private {!Array<number>}
 * @const
 */
proto.sensen.finance.PortfolioOptimizeResponse.repeatedFields_ = [1];



if (jspb.Message.GENERATE_TO_OBJECT) {
/**
 * Creates an object representation of this proto.
 * Field names that are reserved in JavaScript and will be renamed to pb_name.
 * Optional fields that are not set will be set to undefined.
 * To access a reserved field use, foo.pb_<name>, eg, foo.pb_default.
 * For the list of reserved names please see:
 *     net/proto2/compiler/js/internal/generator.cc#kKeyword.
 * @param {boolean=} opt_includeInstance Deprecated. whether to include the
 *     JSPB instance for transitional soy proto support:
 *     http://goto/soy-param-migration
 * @return {!Object}
 */
proto.sensen.finance.PortfolioOptimizeResponse.prototype.toObject = function(opt_includeInstance) {
  return proto.sensen.finance.PortfolioOptimizeResponse.toObject(opt_includeInstance, this);
};


/**
 * Static version of the {@see toObject} method.
 * @param {boolean|undefined} includeInstance Deprecated. Whether to include
 *     the JSPB instance for transitional soy proto support:
 *     http://goto/soy-param-migration
 * @param {!proto.sensen.finance.PortfolioOptimizeResponse} msg The msg instance to transform.
 * @return {!Object}
 * @suppress {unusedLocalVariables} f is only used for nested messages
 */
proto.sensen.finance.PortfolioOptimizeResponse.toObject = function(includeInstance, msg) {
  var f, obj = {
    weightsList: (f = jspb.Message.getRepeatedFloatingPointField(msg, 1)) == null ? undefined : f,
    expectedReturn: jspb.Message.getFloatingPointFieldWithDefault(msg, 2, 0.0),
    volatility: jspb.Message.getFloatingPointFieldWithDefault(msg, 3, 0.0),
    sharpeRatio: jspb.Message.getFloatingPointFieldWithDefault(msg, 4, 0.0)
  };

  if (includeInstance) {
    obj.$jspbMessageInstance = msg;
  }
  return obj;
};
}


/**
 * Deserializes binary data (in protobuf wire format).
 * @param {jspb.ByteSource} bytes The bytes to deserialize.
 * @return {!proto.sensen.finance.PortfolioOptimizeResponse}
 */
proto.sensen.finance.PortfolioOptimizeResponse.deserializeBinary = function(bytes) {
  var reader = new jspb.BinaryReader(bytes);
  var msg = new proto.sensen.finance.PortfolioOptimizeResponse;
  return proto.sensen.finance.PortfolioOptimizeResponse.deserializeBinaryFromReader(msg, reader);
};


/**
 * Deserializes binary data (in protobuf wire format) from the
 * given reader into the given message object.
 * @param {!proto.sensen.finance.PortfolioOptimizeResponse} msg The message object to deserialize into.
 * @param {!jspb.BinaryReader} reader The BinaryReader to use.
 * @return {!proto.sensen.finance.PortfolioOptimizeResponse}
 */
proto.sensen.finance.PortfolioOptimizeResponse.deserializeBinaryFromReader = function(msg, reader) {
  while (reader.nextField()) {
    if (reader.isEndGroup()) {
      break;
    }
    var field = reader.getFieldNumber();
    switch (field) {
    case 1:
      var values = /** @type {!Array<number>} */ (reader.isDelimited() ? reader.readPackedDouble() : [reader.readDouble()]);
      for (var i = 0; i < values.length; i++) {
        msg.addWeights(values[i]);
      }
      break;
    case 2:
      var value = /** @type {number} */ (reader.readDouble());
      msg.setExpectedReturn(value);
      break;
    case 3:
      var value = /** @type {number} */ (reader.readDouble());
      msg.setVolatility(value);
      break;
    case 4:
      var value = /** @type {number} */ (reader.readDouble());
      msg.setSharpeRatio(value);
      break;
    default:
      reader.skipField();
      break;
    }
  }
  return msg;
};


/**
 * Serializes the message to binary data (in protobuf wire format).
 * @return {!Uint8Array}
 */
proto.sensen.finance.PortfolioOptimizeResponse.prototype.serializeBinary = function() {
  var writer = new jspb.BinaryWriter();
  proto.sensen.finance.PortfolioOptimizeResponse.serializeBinaryToWriter(this, writer);
  return writer.getResultBuffer();
};


/**
 * Serializes the given message to binary data (in protobuf wire
 * format), writing to the given BinaryWriter.
 * @param {!proto.sensen.finance.PortfolioOptimizeResponse} message
 * @param {!jspb.BinaryWriter} writer
 * @suppress {unusedLocalVariables} f is only used for nested messages
 */
proto.sensen.finance.PortfolioOptimizeResponse.serializeBinaryToWriter = function(message, writer) {
  var f = undefined;
  f = message.getWeightsList();
  if (f.length > 0) {
    writer.writePackedDouble(
      1,
      f
    );
  }
  f = message.getExpectedReturn();
  if (f !== 0.0) {
    writer.writeDouble(
      2,
      f
    );
  }
  f = message.getVolatility();
  if (f !== 0.0) {
    writer.writeDouble(
      3,
      f
    );
  }
  f = message.getSharpeRatio();
  if (f !== 0.0) {
    writer.writeDouble(
      4,
      f
    );
  }
};


/**
 * repeated double weights = 1;
 * @return {!Array<number>}
 */
proto.sensen.finance.PortfolioOptimizeResponse.prototype.getWeightsList = function() {
  return /** @type {!Array<number>} */ (jspb.Message.getRepeatedFloatingPointField(this, 1));
};


/**
 * @param {!Array<number>} value
 * @return {!proto.sensen.finance.PortfolioOptimizeResponse} returns this
 */
proto.sensen.finance.PortfolioOptimizeResponse.prototype.setWeightsList = function(value) {
  return jspb.Message.setField(this, 1, value || []);
};


/**
 * @param {number} value
 * @param {number=} opt_index
 * @return {!proto.sensen.finance.PortfolioOptimizeResponse} returns this
 */
proto.sensen.finance.PortfolioOptimizeResponse.prototype.addWeights = function(value, opt_index) {
  return jspb.Message.addToRepeatedField(this, 1, value, opt_index);
};


/**
 * Clears the list making it empty but non-null.
 * @return {!proto.sensen.finance.PortfolioOptimizeResponse} returns this
 */
proto.sensen.finance.PortfolioOptimizeResponse.prototype.clearWeightsList = function() {
  return this.setWeightsList([]);
};


/**
 * optional double expected_return = 2;
 * @return {number}
 */
proto.sensen.finance.PortfolioOptimizeResponse.prototype.getExpectedReturn = function() {
  return /** @type {number} */ (jspb.Message.getFloatingPointFieldWithDefault(this, 2, 0.0));
};


/**
 * @param {number} value
 * @return {!proto.sensen.finance.PortfolioOptimizeResponse} returns this
 */
proto.sensen.finance.PortfolioOptimizeResponse.prototype.setExpectedReturn = function(value) {
  return jspb.Message.setProto3FloatField(this, 2, value);
};


/**
 * optional double volatility = 3;
 * @return {number}
 */
proto.sensen.finance.PortfolioOptimizeResponse.prototype.getVolatility = function() {
  return /** @type {number} */ (jspb.Message.getFloatingPointFieldWithDefault(this, 3, 0.0));
};


/**
 * @param {number} value
 * @return {!proto.sensen.finance.PortfolioOptimizeResponse} returns this
 */
proto.sensen.finance.PortfolioOptimizeResponse.prototype.setVolatility = function(value) {
  return jspb.Message.setProto3FloatField(this, 3, value);
};


/**
 * optional double sharpe_ratio = 4;
 * @return {number}
 */
proto.sensen.finance.PortfolioOptimizeResponse.prototype.getSharpeRatio = function() {
  return /** @type {number} */ (jspb.Message.getFloatingPointFieldWithDefault(this, 4, 0.0));
};


/**
 * @param {number} value
 * @return {!proto.sensen.finance.PortfolioOptimizeResponse} returns this
 */
proto.sensen.finance.PortfolioOptimizeResponse.prototype.setSharpeRatio = function(value) {
  return jspb.Message.setProto3FloatField(this, 4, value);
};



/**
 * List of repeated fields within this message type.
 * @private {!Array<number>}
 * @const
 */
proto.sensen.finance.RiskContributionRequest.repeatedFields_ = [1,2];



if (jspb.Message.GENERATE_TO_OBJECT) {
/**
 * Creates an object representation of this proto.
 * Field names that are reserved in JavaScript and will be renamed to pb_name.
 * Optional fields that are not set will be set to undefined.
 * To access a reserved field use, foo.pb_<name>, eg, foo.pb_default.
 * For the list of reserved names please see:
 *     net/proto2/compiler/js/internal/generator.cc#kKeyword.
 * @param {boolean=} opt_includeInstance Deprecated. whether to include the
 *     JSPB instance for transitional soy proto support:
 *     http://goto/soy-param-migration
 * @return {!Object}
 */
proto.sensen.finance.RiskContributionRequest.prototype.toObject = function(opt_includeInstance) {
  return proto.sensen.finance.RiskContributionRequest.toObject(opt_includeInstance, this);
};


/**
 * Static version of the {@see toObject} method.
 * @param {boolean|undefined} includeInstance Deprecated. Whether to include
 *     the JSPB instance for transitional soy proto support:
 *     http://goto/soy-param-migration
 * @param {!proto.sensen.finance.RiskContributionRequest} msg The msg instance to transform.
 * @return {!Object}
 * @suppress {unusedLocalVariables} f is only used for nested messages
 */
proto.sensen.finance.RiskContributionRequest.toObject = function(includeInstance, msg) {
  var f, obj = {
    weightsList: (f = jspb.Message.getRepeatedFloatingPointField(msg, 1)) == null ? undefined : f,
    covarianceList: (f = jspb.Message.getRepeatedFloatingPointField(msg, 2)) == null ? undefined : f,
    size: jspb.Message.getFieldWithDefault(msg, 3, 0)
  };

  if (includeInstance) {
    obj.$jspbMessageInstance = msg;
  }
  return obj;
};
}


/**
 * Deserializes binary data (in protobuf wire format).
 * @param {jspb.ByteSource} bytes The bytes to deserialize.
 * @return {!proto.sensen.finance.RiskContributionRequest}
 */
proto.sensen.finance.RiskContributionRequest.deserializeBinary = function(bytes) {
  var reader = new jspb.BinaryReader(bytes);
  var msg = new proto.sensen.finance.RiskContributionRequest;
  return proto.sensen.finance.RiskContributionRequest.deserializeBinaryFromReader(msg, reader);
};


/**
 * Deserializes binary data (in protobuf wire format) from the
 * given reader into the given message object.
 * @param {!proto.sensen.finance.RiskContributionRequest} msg The message object to deserialize into.
 * @param {!jspb.BinaryReader} reader The BinaryReader to use.
 * @return {!proto.sensen.finance.RiskContributionRequest}
 */
proto.sensen.finance.RiskContributionRequest.deserializeBinaryFromReader = function(msg, reader) {
  while (reader.nextField()) {
    if (reader.isEndGroup()) {
      break;
    }
    var field = reader.getFieldNumber();
    switch (field) {
    case 1:
      var values = /** @type {!Array<number>} */ (reader.isDelimited() ? reader.readPackedDouble() : [reader.readDouble()]);
      for (var i = 0; i < values.length; i++) {
        msg.addWeights(values[i]);
      }
      break;
    case 2:
      var values = /** @type {!Array<number>} */ (reader.isDelimited() ? reader.readPackedDouble() : [reader.readDouble()]);
      for (var i = 0; i < values.length; i++) {
        msg.addCovariance(values[i]);
      }
      break;
    case 3:
      var value = /** @type {number} */ (reader.readInt32());
      msg.setSize(value);
      break;
    default:
      reader.skipField();
      break;
    }
  }
  return msg;
};


/**
 * Serializes the message to binary data (in protobuf wire format).
 * @return {!Uint8Array}
 */
proto.sensen.finance.RiskContributionRequest.prototype.serializeBinary = function() {
  var writer = new jspb.BinaryWriter();
  proto.sensen.finance.RiskContributionRequest.serializeBinaryToWriter(this, writer);
  return writer.getResultBuffer();
};


/**
 * Serializes the given message to binary data (in protobuf wire
 * format), writing to the given BinaryWriter.
 * @param {!proto.sensen.finance.RiskContributionRequest} message
 * @param {!jspb.BinaryWriter} writer
 * @suppress {unusedLocalVariables} f is only used for nested messages
 */
proto.sensen.finance.RiskContributionRequest.serializeBinaryToWriter = function(message, writer) {
  var f = undefined;
  f = message.getWeightsList();
  if (f.length > 0) {
    writer.writePackedDouble(
      1,
      f
    );
  }
  f = message.getCovarianceList();
  if (f.length > 0) {
    writer.writePackedDouble(
      2,
      f
    );
  }
  f = message.getSize();
  if (f !== 0) {
    writer.writeInt32(
      3,
      f
    );
  }
};


/**
 * repeated double weights = 1;
 * @return {!Array<number>}
 */
proto.sensen.finance.RiskContributionRequest.prototype.getWeightsList = function() {
  return /** @type {!Array<number>} */ (jspb.Message.getRepeatedFloatingPointField(this, 1));
};


/**
 * @param {!Array<number>} value
 * @return {!proto.sensen.finance.RiskContributionRequest} returns this
 */
proto.sensen.finance.RiskContributionRequest.prototype.setWeightsList = function(value) {
  return jspb.Message.setField(this, 1, value || []);
};


/**
 * @param {number} value
 * @param {number=} opt_index
 * @return {!proto.sensen.finance.RiskContributionRequest} returns this
 */
proto.sensen.finance.RiskContributionRequest.prototype.addWeights = function(value, opt_index) {
  return jspb.Message.addToRepeatedField(this, 1, value, opt_index);
};


/**
 * Clears the list making it empty but non-null.
 * @return {!proto.sensen.finance.RiskContributionRequest} returns this
 */
proto.sensen.finance.RiskContributionRequest.prototype.clearWeightsList = function() {
  return this.setWeightsList([]);
};


/**
 * repeated double covariance = 2;
 * @return {!Array<number>}
 */
proto.sensen.finance.RiskContributionRequest.prototype.getCovarianceList = function() {
  return /** @type {!Array<number>} */ (jspb.Message.getRepeatedFloatingPointField(this, 2));
};


/**
 * @param {!Array<number>} value
 * @return {!proto.sensen.finance.RiskContributionRequest} returns this
 */
proto.sensen.finance.RiskContributionRequest.prototype.setCovarianceList = function(value) {
  return jspb.Message.setField(this, 2, value || []);
};


/**
 * @param {number} value
 * @param {number=} opt_index
 * @return {!proto.sensen.finance.RiskContributionRequest} returns this
 */
proto.sensen.finance.RiskContributionRequest.prototype.addCovariance = function(value, opt_index) {
  return jspb.Message.addToRepeatedField(this, 2, value, opt_index);
};


/**
 * Clears the list making it empty but non-null.
 * @return {!proto.sensen.finance.RiskContributionRequest} returns this
 */
proto.sensen.finance.RiskContributionRequest.prototype.clearCovarianceList = function() {
  return this.setCovarianceList([]);
};


/**
 * optional int32 size = 3;
 * @return {number}
 */
proto.sensen.finance.RiskContributionRequest.prototype.getSize = function() {
  return /** @type {number} */ (jspb.Message.getFieldWithDefault(this, 3, 0));
};


/**
 * @param {number} value
 * @return {!proto.sensen.finance.RiskContributionRequest} returns this
 */
proto.sensen.finance.RiskContributionRequest.prototype.setSize = function(value) {
  return jspb.Message.setProto3IntField(this, 3, value);
};



/**
 * List of repeated fields within this message type.
 * @private {!Array<number>}
 * @const
 */
proto.sensen.finance.RiskContributionResponse.repeatedFields_ = [1];



if (jspb.Message.GENERATE_TO_OBJECT) {
/**
 * Creates an object representation of this proto.
 * Field names that are reserved in JavaScript and will be renamed to pb_name.
 * Optional fields that are not set will be set to undefined.
 * To access a reserved field use, foo.pb_<name>, eg, foo.pb_default.
 * For the list of reserved names please see:
 *     net/proto2/compiler/js/internal/generator.cc#kKeyword.
 * @param {boolean=} opt_includeInstance Deprecated. whether to include the
 *     JSPB instance for transitional soy proto support:
 *     http://goto/soy-param-migration
 * @return {!Object}
 */
proto.sensen.finance.RiskContributionResponse.prototype.toObject = function(opt_includeInstance) {
  return proto.sensen.finance.RiskContributionResponse.toObject(opt_includeInstance, this);
};


/**
 * Static version of the {@see toObject} method.
 * @param {boolean|undefined} includeInstance Deprecated. Whether to include
 *     the JSPB instance for transitional soy proto support:
 *     http://goto/soy-param-migration
 * @param {!proto.sensen.finance.RiskContributionResponse} msg The msg instance to transform.
 * @return {!Object}
 * @suppress {unusedLocalVariables} f is only used for nested messages
 */
proto.sensen.finance.RiskContributionResponse.toObject = function(includeInstance, msg) {
  var f, obj = {
    contributionsList: (f = jspb.Message.getRepeatedFloatingPointField(msg, 1)) == null ? undefined : f
  };

  if (includeInstance) {
    obj.$jspbMessageInstance = msg;
  }
  return obj;
};
}


/**
 * Deserializes binary data (in protobuf wire format).
 * @param {jspb.ByteSource} bytes The bytes to deserialize.
 * @return {!proto.sensen.finance.RiskContributionResponse}
 */
proto.sensen.finance.RiskContributionResponse.deserializeBinary = function(bytes) {
  var reader = new jspb.BinaryReader(bytes);
  var msg = new proto.sensen.finance.RiskContributionResponse;
  return proto.sensen.finance.RiskContributionResponse.deserializeBinaryFromReader(msg, reader);
};


/**
 * Deserializes binary data (in protobuf wire format) from the
 * given reader into the given message object.
 * @param {!proto.sensen.finance.RiskContributionResponse} msg The message object to deserialize into.
 * @param {!jspb.BinaryReader} reader The BinaryReader to use.
 * @return {!proto.sensen.finance.RiskContributionResponse}
 */
proto.sensen.finance.RiskContributionResponse.deserializeBinaryFromReader = function(msg, reader) {
  while (reader.nextField()) {
    if (reader.isEndGroup()) {
      break;
    }
    var field = reader.getFieldNumber();
    switch (field) {
    case 1:
      var values = /** @type {!Array<number>} */ (reader.isDelimited() ? reader.readPackedDouble() : [reader.readDouble()]);
      for (var i = 0; i < values.length; i++) {
        msg.addContributions(values[i]);
      }
      break;
    default:
      reader.skipField();
      break;
    }
  }
  return msg;
};


/**
 * Serializes the message to binary data (in protobuf wire format).
 * @return {!Uint8Array}
 */
proto.sensen.finance.RiskContributionResponse.prototype.serializeBinary = function() {
  var writer = new jspb.BinaryWriter();
  proto.sensen.finance.RiskContributionResponse.serializeBinaryToWriter(this, writer);
  return writer.getResultBuffer();
};


/**
 * Serializes the given message to binary data (in protobuf wire
 * format), writing to the given BinaryWriter.
 * @param {!proto.sensen.finance.RiskContributionResponse} message
 * @param {!jspb.BinaryWriter} writer
 * @suppress {unusedLocalVariables} f is only used for nested messages
 */
proto.sensen.finance.RiskContributionResponse.serializeBinaryToWriter = function(message, writer) {
  var f = undefined;
  f = message.getContributionsList();
  if (f.length > 0) {
    writer.writePackedDouble(
      1,
      f
    );
  }
};


/**
 * repeated double contributions = 1;
 * @return {!Array<number>}
 */
proto.sensen.finance.RiskContributionResponse.prototype.getContributionsList = function() {
  return /** @type {!Array<number>} */ (jspb.Message.getRepeatedFloatingPointField(this, 1));
};


/**
 * @param {!Array<number>} value
 * @return {!proto.sensen.finance.RiskContributionResponse} returns this
 */
proto.sensen.finance.RiskContributionResponse.prototype.setContributionsList = function(value) {
  return jspb.Message.setField(this, 1, value || []);
};


/**
 * @param {number} value
 * @param {number=} opt_index
 * @return {!proto.sensen.finance.RiskContributionResponse} returns this
 */
proto.sensen.finance.RiskContributionResponse.prototype.addContributions = function(value, opt_index) {
  return jspb.Message.addToRepeatedField(this, 1, value, opt_index);
};


/**
 * Clears the list making it empty but non-null.
 * @return {!proto.sensen.finance.RiskContributionResponse} returns this
 */
proto.sensen.finance.RiskContributionResponse.prototype.clearContributionsList = function() {
  return this.setContributionsList([]);
};





if (jspb.Message.GENERATE_TO_OBJECT) {
/**
 * Creates an object representation of this proto.
 * Field names that are reserved in JavaScript and will be renamed to pb_name.
 * Optional fields that are not set will be set to undefined.
 * To access a reserved field use, foo.pb_<name>, eg, foo.pb_default.
 * For the list of reserved names please see:
 *     net/proto2/compiler/js/internal/generator.cc#kKeyword.
 * @param {boolean=} opt_includeInstance Deprecated. whether to include the
 *     JSPB instance for transitional soy proto support:
 *     http://goto/soy-param-migration
 * @return {!Object}
 */
proto.sensen.finance.ClosingCostsRequest.prototype.toObject = function(opt_includeInstance) {
  return proto.sensen.finance.ClosingCostsRequest.toObject(opt_includeInstance, this);
};


/**
 * Static version of the {@see toObject} method.
 * @param {boolean|undefined} includeInstance Deprecated. Whether to include
 *     the JSPB instance for transitional soy proto support:
 *     http://goto/soy-param-migration
 * @param {!proto.sensen.finance.ClosingCostsRequest} msg The msg instance to transform.
 * @return {!Object}
 * @suppress {unusedLocalVariables} f is only used for nested messages
 */
proto.sensen.finance.ClosingCostsRequest.toObject = function(includeInstance, msg) {
  var f, obj = {
    homePrice: jspb.Message.getFieldWithDefault(msg, 1, ""),
    downPaymentPercent: jspb.Message.getFieldWithDefault(msg, 2, ""),
    annualRate: jspb.Message.getFieldWithDefault(msg, 3, ""),
    originationFeePercent: jspb.Message.getFieldWithDefault(msg, 4, ""),
    discountPointsPercent: jspb.Message.getFieldWithDefault(msg, 5, ""),
    otherLenderFees: jspb.Message.getFieldWithDefault(msg, 6, ""),
    titleSettlementPercent: jspb.Message.getFieldWithDefault(msg, 7, ""),
    appraisalFee: jspb.Message.getFieldWithDefault(msg, 8, ""),
    inspectionFee: jspb.Message.getFieldWithDefault(msg, 9, ""),
    recordingFees: jspb.Message.getFieldWithDefault(msg, 10, ""),
    transferTaxPercent: jspb.Message.getFieldWithDefault(msg, 11, ""),
    homeownersInsuranceAnnual: jspb.Message.getFieldWithDefault(msg, 12, ""),
    propertyTaxAnnual: jspb.Message.getFieldWithDefault(msg, 13, ""),
    taxEscrowMonths: jspb.Message.getFieldWithDefault(msg, 14, 0),
    sellerLenderCredits: jspb.Message.getFieldWithDefault(msg, 15, ""),
    prepaidInterestDays: jspb.Message.getFieldWithDefault(msg, 16, 0)
  };

  if (includeInstance) {
    obj.$jspbMessageInstance = msg;
  }
  return obj;
};
}


/**
 * Deserializes binary data (in protobuf wire format).
 * @param {jspb.ByteSource} bytes The bytes to deserialize.
 * @return {!proto.sensen.finance.ClosingCostsRequest}
 */
proto.sensen.finance.ClosingCostsRequest.deserializeBinary = function(bytes) {
  var reader = new jspb.BinaryReader(bytes);
  var msg = new proto.sensen.finance.ClosingCostsRequest;
  return proto.sensen.finance.ClosingCostsRequest.deserializeBinaryFromReader(msg, reader);
};


/**
 * Deserializes binary data (in protobuf wire format) from the
 * given reader into the given message object.
 * @param {!proto.sensen.finance.ClosingCostsRequest} msg The message object to deserialize into.
 * @param {!jspb.BinaryReader} reader The BinaryReader to use.
 * @return {!proto.sensen.finance.ClosingCostsRequest}
 */
proto.sensen.finance.ClosingCostsRequest.deserializeBinaryFromReader = function(msg, reader) {
  while (reader.nextField()) {
    if (reader.isEndGroup()) {
      break;
    }
    var field = reader.getFieldNumber();
    switch (field) {
    case 1:
      var value = /** @type {string} */ (reader.readString());
      msg.setHomePrice(value);
      break;
    case 2:
      var value = /** @type {string} */ (reader.readString());
      msg.setDownPaymentPercent(value);
      break;
    case 3:
      var value = /** @type {string} */ (reader.readString());
      msg.setAnnualRate(value);
      break;
    case 4:
      var value = /** @type {string} */ (reader.readString());
      msg.setOriginationFeePercent(value);
      break;
    case 5:
      var value = /** @type {string} */ (reader.readString());
      msg.setDiscountPointsPercent(value);
      break;
    case 6:
      var value = /** @type {string} */ (reader.readString());
      msg.setOtherLenderFees(value);
      break;
    case 7:
      var value = /** @type {string} */ (reader.readString());
      msg.setTitleSettlementPercent(value);
      break;
    case 8:
      var value = /** @type {string} */ (reader.readString());
      msg.setAppraisalFee(value);
      break;
    case 9:
      var value = /** @type {string} */ (reader.readString());
      msg.setInspectionFee(value);
      break;
    case 10:
      var value = /** @type {string} */ (reader.readString());
      msg.setRecordingFees(value);
      break;
    case 11:
      var value = /** @type {string} */ (reader.readString());
      msg.setTransferTaxPercent(value);
      break;
    case 12:
      var value = /** @type {string} */ (reader.readString());
      msg.setHomeownersInsuranceAnnual(value);
      break;
    case 13:
      var value = /** @type {string} */ (reader.readString());
      msg.setPropertyTaxAnnual(value);
      break;
    case 14:
      var value = /** @type {number} */ (reader.readInt32());
      msg.setTaxEscrowMonths(value);
      break;
    case 15:
      var value = /** @type {string} */ (reader.readString());
      msg.setSellerLenderCredits(value);
      break;
    case 16:
      var value = /** @type {number} */ (reader.readInt32());
      msg.setPrepaidInterestDays(value);
      break;
    default:
      reader.skipField();
      break;
    }
  }
  return msg;
};


/**
 * Serializes the message to binary data (in protobuf wire format).
 * @return {!Uint8Array}
 */
proto.sensen.finance.ClosingCostsRequest.prototype.serializeBinary = function() {
  var writer = new jspb.BinaryWriter();
  proto.sensen.finance.ClosingCostsRequest.serializeBinaryToWriter(this, writer);
  return writer.getResultBuffer();
};


/**
 * Serializes the given message to binary data (in protobuf wire
 * format), writing to the given BinaryWriter.
 * @param {!proto.sensen.finance.ClosingCostsRequest} message
 * @param {!jspb.BinaryWriter} writer
 * @suppress {unusedLocalVariables} f is only used for nested messages
 */
proto.sensen.finance.ClosingCostsRequest.serializeBinaryToWriter = function(message, writer) {
  var f = undefined;
  f = message.getHomePrice();
  if (f.length > 0) {
    writer.writeString(
      1,
      f
    );
  }
  f = message.getDownPaymentPercent();
  if (f.length > 0) {
    writer.writeString(
      2,
      f
    );
  }
  f = message.getAnnualRate();
  if (f.length > 0) {
    writer.writeString(
      3,
      f
    );
  }
  f = message.getOriginationFeePercent();
  if (f.length > 0) {
    writer.writeString(
      4,
      f
    );
  }
  f = message.getDiscountPointsPercent();
  if (f.length > 0) {
    writer.writeString(
      5,
      f
    );
  }
  f = message.getOtherLenderFees();
  if (f.length > 0) {
    writer.writeString(
      6,
      f
    );
  }
  f = message.getTitleSettlementPercent();
  if (f.length > 0) {
    writer.writeString(
      7,
      f
    );
  }
  f = message.getAppraisalFee();
  if (f.length > 0) {
    writer.writeString(
      8,
      f
    );
  }
  f = message.getInspectionFee();
  if (f.length > 0) {
    writer.writeString(
      9,
      f
    );
  }
  f = message.getRecordingFees();
  if (f.length > 0) {
    writer.writeString(
      10,
      f
    );
  }
  f = message.getTransferTaxPercent();
  if (f.length > 0) {
    writer.writeString(
      11,
      f
    );
  }
  f = message.getHomeownersInsuranceAnnual();
  if (f.length > 0) {
    writer.writeString(
      12,
      f
    );
  }
  f = message.getPropertyTaxAnnual();
  if (f.length > 0) {
    writer.writeString(
      13,
      f
    );
  }
  f = message.getTaxEscrowMonths();
  if (f !== 0) {
    writer.writeInt32(
      14,
      f
    );
  }
  f = message.getSellerLenderCredits();
  if (f.length > 0) {
    writer.writeString(
      15,
      f
    );
  }
  f = /** @type {number} */ (jspb.Message.getField(message, 16));
  if (f != null) {
    writer.writeInt32(
      16,
      f
    );
  }
};


/**
 * optional string home_price = 1;
 * @return {string}
 */
proto.sensen.finance.ClosingCostsRequest.prototype.getHomePrice = function() {
  return /** @type {string} */ (jspb.Message.getFieldWithDefault(this, 1, ""));
};


/**
 * @param {string} value
 * @return {!proto.sensen.finance.ClosingCostsRequest} returns this
 */
proto.sensen.finance.ClosingCostsRequest.prototype.setHomePrice = function(value) {
  return jspb.Message.setProto3StringField(this, 1, value);
};


/**
 * optional string down_payment_percent = 2;
 * @return {string}
 */
proto.sensen.finance.ClosingCostsRequest.prototype.getDownPaymentPercent = function() {
  return /** @type {string} */ (jspb.Message.getFieldWithDefault(this, 2, ""));
};


/**
 * @param {string} value
 * @return {!proto.sensen.finance.ClosingCostsRequest} returns this
 */
proto.sensen.finance.ClosingCostsRequest.prototype.setDownPaymentPercent = function(value) {
  return jspb.Message.setProto3StringField(this, 2, value);
};


/**
 * optional string annual_rate = 3;
 * @return {string}
 */
proto.sensen.finance.ClosingCostsRequest.prototype.getAnnualRate = function() {
  return /** @type {string} */ (jspb.Message.getFieldWithDefault(this, 3, ""));
};


/**
 * @param {string} value
 * @return {!proto.sensen.finance.ClosingCostsRequest} returns this
 */
proto.sensen.finance.ClosingCostsRequest.prototype.setAnnualRate = function(value) {
  return jspb.Message.setProto3StringField(this, 3, value);
};


/**
 * optional string origination_fee_percent = 4;
 * @return {string}
 */
proto.sensen.finance.ClosingCostsRequest.prototype.getOriginationFeePercent = function() {
  return /** @type {string} */ (jspb.Message.getFieldWithDefault(this, 4, ""));
};


/**
 * @param {string} value
 * @return {!proto.sensen.finance.ClosingCostsRequest} returns this
 */
proto.sensen.finance.ClosingCostsRequest.prototype.setOriginationFeePercent = function(value) {
  return jspb.Message.setProto3StringField(this, 4, value);
};


/**
 * optional string discount_points_percent = 5;
 * @return {string}
 */
proto.sensen.finance.ClosingCostsRequest.prototype.getDiscountPointsPercent = function() {
  return /** @type {string} */ (jspb.Message.getFieldWithDefault(this, 5, ""));
};


/**
 * @param {string} value
 * @return {!proto.sensen.finance.ClosingCostsRequest} returns this
 */
proto.sensen.finance.ClosingCostsRequest.prototype.setDiscountPointsPercent = function(value) {
  return jspb.Message.setProto3StringField(this, 5, value);
};


/**
 * optional string other_lender_fees = 6;
 * @return {string}
 */
proto.sensen.finance.ClosingCostsRequest.prototype.getOtherLenderFees = function() {
  return /** @type {string} */ (jspb.Message.getFieldWithDefault(this, 6, ""));
};


/**
 * @param {string} value
 * @return {!proto.sensen.finance.ClosingCostsRequest} returns this
 */
proto.sensen.finance.ClosingCostsRequest.prototype.setOtherLenderFees = function(value) {
  return jspb.Message.setProto3StringField(this, 6, value);
};


/**
 * optional string title_settlement_percent = 7;
 * @return {string}
 */
proto.sensen.finance.ClosingCostsRequest.prototype.getTitleSettlementPercent = function() {
  return /** @type {string} */ (jspb.Message.getFieldWithDefault(this, 7, ""));
};


/**
 * @param {string} value
 * @return {!proto.sensen.finance.ClosingCostsRequest} returns this
 */
proto.sensen.finance.ClosingCostsRequest.prototype.setTitleSettlementPercent = function(value) {
  return jspb.Message.setProto3StringField(this, 7, value);
};


/**
 * optional string appraisal_fee = 8;
 * @return {string}
 */
proto.sensen.finance.ClosingCostsRequest.prototype.getAppraisalFee = function() {
  return /** @type {string} */ (jspb.Message.getFieldWithDefault(this, 8, ""));
};


/**
 * @param {string} value
 * @return {!proto.sensen.finance.ClosingCostsRequest} returns this
 */
proto.sensen.finance.ClosingCostsRequest.prototype.setAppraisalFee = function(value) {
  return jspb.Message.setProto3StringField(this, 8, value);
};


/**
 * optional string inspection_fee = 9;
 * @return {string}
 */
proto.sensen.finance.ClosingCostsRequest.prototype.getInspectionFee = function() {
  return /** @type {string} */ (jspb.Message.getFieldWithDefault(this, 9, ""));
};


/**
 * @param {string} value
 * @return {!proto.sensen.finance.ClosingCostsRequest} returns this
 */
proto.sensen.finance.ClosingCostsRequest.prototype.setInspectionFee = function(value) {
  return jspb.Message.setProto3StringField(this, 9, value);
};


/**
 * optional string recording_fees = 10;
 * @return {string}
 */
proto.sensen.finance.ClosingCostsRequest.prototype.getRecordingFees = function() {
  return /** @type {string} */ (jspb.Message.getFieldWithDefault(this, 10, ""));
};


/**
 * @param {string} value
 * @return {!proto.sensen.finance.ClosingCostsRequest} returns this
 */
proto.sensen.finance.ClosingCostsRequest.prototype.setRecordingFees = function(value) {
  return jspb.Message.setProto3StringField(this, 10, value);
};


/**
 * optional string transfer_tax_percent = 11;
 * @return {string}
 */
proto.sensen.finance.ClosingCostsRequest.prototype.getTransferTaxPercent = function() {
  return /** @type {string} */ (jspb.Message.getFieldWithDefault(this, 11, ""));
};


/**
 * @param {string} value
 * @return {!proto.sensen.finance.ClosingCostsRequest} returns this
 */
proto.sensen.finance.ClosingCostsRequest.prototype.setTransferTaxPercent = function(value) {
  return jspb.Message.setProto3StringField(this, 11, value);
};


/**
 * optional string homeowners_insurance_annual = 12;
 * @return {string}
 */
proto.sensen.finance.ClosingCostsRequest.prototype.getHomeownersInsuranceAnnual = function() {
  return /** @type {string} */ (jspb.Message.getFieldWithDefault(this, 12, ""));
};


/**
 * @param {string} value
 * @return {!proto.sensen.finance.ClosingCostsRequest} returns this
 */
proto.sensen.finance.ClosingCostsRequest.prototype.setHomeownersInsuranceAnnual = function(value) {
  return jspb.Message.setProto3StringField(this, 12, value);
};


/**
 * optional string property_tax_annual = 13;
 * @return {string}
 */
proto.sensen.finance.ClosingCostsRequest.prototype.getPropertyTaxAnnual = function() {
  return /** @type {string} */ (jspb.Message.getFieldWithDefault(this, 13, ""));
};


/**
 * @param {string} value
 * @return {!proto.sensen.finance.ClosingCostsRequest} returns this
 */
proto.sensen.finance.ClosingCostsRequest.prototype.setPropertyTaxAnnual = function(value) {
  return jspb.Message.setProto3StringField(this, 13, value);
};


/**
 * optional int32 tax_escrow_months = 14;
 * @return {number}
 */
proto.sensen.finance.ClosingCostsRequest.prototype.getTaxEscrowMonths = function() {
  return /** @type {number} */ (jspb.Message.getFieldWithDefault(this, 14, 0));
};


/**
 * @param {number} value
 * @return {!proto.sensen.finance.ClosingCostsRequest} returns this
 */
proto.sensen.finance.ClosingCostsRequest.prototype.setTaxEscrowMonths = function(value) {
  return jspb.Message.setProto3IntField(this, 14, value);
};


/**
 * optional string seller_lender_credits = 15;
 * @return {string}
 */
proto.sensen.finance.ClosingCostsRequest.prototype.getSellerLenderCredits = function() {
  return /** @type {string} */ (jspb.Message.getFieldWithDefault(this, 15, ""));
};


/**
 * @param {string} value
 * @return {!proto.sensen.finance.ClosingCostsRequest} returns this
 */
proto.sensen.finance.ClosingCostsRequest.prototype.setSellerLenderCredits = function(value) {
  return jspb.Message.setProto3StringField(this, 15, value);
};


/**
 * optional int32 prepaid_interest_days = 16;
 * @return {number}
 */
proto.sensen.finance.ClosingCostsRequest.prototype.getPrepaidInterestDays = function() {
  return /** @type {number} */ (jspb.Message.getFieldWithDefault(this, 16, 0));
};


/**
 * @param {number} value
 * @return {!proto.sensen.finance.ClosingCostsRequest} returns this
 */
proto.sensen.finance.ClosingCostsRequest.prototype.setPrepaidInterestDays = function(value) {
  return jspb.Message.setField(this, 16, value);
};


/**
 * Clears the field making it undefined.
 * @return {!proto.sensen.finance.ClosingCostsRequest} returns this
 */
proto.sensen.finance.ClosingCostsRequest.prototype.clearPrepaidInterestDays = function() {
  return jspb.Message.setField(this, 16, undefined);
};


/**
 * Returns whether this field is set.
 * @return {boolean}
 */
proto.sensen.finance.ClosingCostsRequest.prototype.hasPrepaidInterestDays = function() {
  return jspb.Message.getField(this, 16) != null;
};





if (jspb.Message.GENERATE_TO_OBJECT) {
/**
 * Creates an object representation of this proto.
 * Field names that are reserved in JavaScript and will be renamed to pb_name.
 * Optional fields that are not set will be set to undefined.
 * To access a reserved field use, foo.pb_<name>, eg, foo.pb_default.
 * For the list of reserved names please see:
 *     net/proto2/compiler/js/internal/generator.cc#kKeyword.
 * @param {boolean=} opt_includeInstance Deprecated. whether to include the
 *     JSPB instance for transitional soy proto support:
 *     http://goto/soy-param-migration
 * @return {!Object}
 */
proto.sensen.finance.ClosingCostsResponse.prototype.toObject = function(opt_includeInstance) {
  return proto.sensen.finance.ClosingCostsResponse.toObject(opt_includeInstance, this);
};


/**
 * Static version of the {@see toObject} method.
 * @param {boolean|undefined} includeInstance Deprecated. Whether to include
 *     the JSPB instance for transitional soy proto support:
 *     http://goto/soy-param-migration
 * @param {!proto.sensen.finance.ClosingCostsResponse} msg The msg instance to transform.
 * @return {!Object}
 * @suppress {unusedLocalVariables} f is only used for nested messages
 */
proto.sensen.finance.ClosingCostsResponse.toObject = function(includeInstance, msg) {
  var f, obj = {
    originationFee: jspb.Message.getFieldWithDefault(msg, 1, ""),
    discountPoints: jspb.Message.getFieldWithDefault(msg, 2, ""),
    otherLenderFees: jspb.Message.getFieldWithDefault(msg, 3, ""),
    titleSettlement: jspb.Message.getFieldWithDefault(msg, 4, ""),
    appraisalFee: jspb.Message.getFieldWithDefault(msg, 5, ""),
    inspectionFee: jspb.Message.getFieldWithDefault(msg, 6, ""),
    recordingFees: jspb.Message.getFieldWithDefault(msg, 7, ""),
    transferTax: jspb.Message.getFieldWithDefault(msg, 8, ""),
    homeownersInsurancePrepaid: jspb.Message.getFieldWithDefault(msg, 9, ""),
    propertyTaxEscrow: jspb.Message.getFieldWithDefault(msg, 10, ""),
    prepaidInterest: jspb.Message.getFieldWithDefault(msg, 11, ""),
    prepaidInterestDays: jspb.Message.getFieldWithDefault(msg, 12, 0),
    itemisedSubtotal: jspb.Message.getFieldWithDefault(msg, 13, ""),
    sellerLenderCredits: jspb.Message.getFieldWithDefault(msg, 14, ""),
    totalClosingCosts: jspb.Message.getFieldWithDefault(msg, 15, ""),
    loanAmount: jspb.Message.getFieldWithDefault(msg, 16, ""),
    downPayment: jspb.Message.getFieldWithDefault(msg, 17, ""),
    totalCashToClose: jspb.Message.getFieldWithDefault(msg, 18, ""),
    closingCostsPercentOfPrice: jspb.Message.getFloatingPointFieldWithDefault(msg, 19, 0.0)
  };

  if (includeInstance) {
    obj.$jspbMessageInstance = msg;
  }
  return obj;
};
}


/**
 * Deserializes binary data (in protobuf wire format).
 * @param {jspb.ByteSource} bytes The bytes to deserialize.
 * @return {!proto.sensen.finance.ClosingCostsResponse}
 */
proto.sensen.finance.ClosingCostsResponse.deserializeBinary = function(bytes) {
  var reader = new jspb.BinaryReader(bytes);
  var msg = new proto.sensen.finance.ClosingCostsResponse;
  return proto.sensen.finance.ClosingCostsResponse.deserializeBinaryFromReader(msg, reader);
};


/**
 * Deserializes binary data (in protobuf wire format) from the
 * given reader into the given message object.
 * @param {!proto.sensen.finance.ClosingCostsResponse} msg The message object to deserialize into.
 * @param {!jspb.BinaryReader} reader The BinaryReader to use.
 * @return {!proto.sensen.finance.ClosingCostsResponse}
 */
proto.sensen.finance.ClosingCostsResponse.deserializeBinaryFromReader = function(msg, reader) {
  while (reader.nextField()) {
    if (reader.isEndGroup()) {
      break;
    }
    var field = reader.getFieldNumber();
    switch (field) {
    case 1:
      var value = /** @type {string} */ (reader.readString());
      msg.setOriginationFee(value);
      break;
    case 2:
      var value = /** @type {string} */ (reader.readString());
      msg.setDiscountPoints(value);
      break;
    case 3:
      var value = /** @type {string} */ (reader.readString());
      msg.setOtherLenderFees(value);
      break;
    case 4:
      var value = /** @type {string} */ (reader.readString());
      msg.setTitleSettlement(value);
      break;
    case 5:
      var value = /** @type {string} */ (reader.readString());
      msg.setAppraisalFee(value);
      break;
    case 6:
      var value = /** @type {string} */ (reader.readString());
      msg.setInspectionFee(value);
      break;
    case 7:
      var value = /** @type {string} */ (reader.readString());
      msg.setRecordingFees(value);
      break;
    case 8:
      var value = /** @type {string} */ (reader.readString());
      msg.setTransferTax(value);
      break;
    case 9:
      var value = /** @type {string} */ (reader.readString());
      msg.setHomeownersInsurancePrepaid(value);
      break;
    case 10:
      var value = /** @type {string} */ (reader.readString());
      msg.setPropertyTaxEscrow(value);
      break;
    case 11:
      var value = /** @type {string} */ (reader.readString());
      msg.setPrepaidInterest(value);
      break;
    case 12:
      var value = /** @type {number} */ (reader.readInt32());
      msg.setPrepaidInterestDays(value);
      break;
    case 13:
      var value = /** @type {string} */ (reader.readString());
      msg.setItemisedSubtotal(value);
      break;
    case 14:
      var value = /** @type {string} */ (reader.readString());
      msg.setSellerLenderCredits(value);
      break;
    case 15:
      var value = /** @type {string} */ (reader.readString());
      msg.setTotalClosingCosts(value);
      break;
    case 16:
      var value = /** @type {string} */ (reader.readString());
      msg.setLoanAmount(value);
      break;
    case 17:
      var value = /** @type {string} */ (reader.readString());
      msg.setDownPayment(value);
      break;
    case 18:
      var value = /** @type {string} */ (reader.readString());
      msg.setTotalCashToClose(value);
      break;
    case 19:
      var value = /** @type {number} */ (reader.readDouble());
      msg.setClosingCostsPercentOfPrice(value);
      break;
    default:
      reader.skipField();
      break;
    }
  }
  return msg;
};


/**
 * Serializes the message to binary data (in protobuf wire format).
 * @return {!Uint8Array}
 */
proto.sensen.finance.ClosingCostsResponse.prototype.serializeBinary = function() {
  var writer = new jspb.BinaryWriter();
  proto.sensen.finance.ClosingCostsResponse.serializeBinaryToWriter(this, writer);
  return writer.getResultBuffer();
};


/**
 * Serializes the given message to binary data (in protobuf wire
 * format), writing to the given BinaryWriter.
 * @param {!proto.sensen.finance.ClosingCostsResponse} message
 * @param {!jspb.BinaryWriter} writer
 * @suppress {unusedLocalVariables} f is only used for nested messages
 */
proto.sensen.finance.ClosingCostsResponse.serializeBinaryToWriter = function(message, writer) {
  var f = undefined;
  f = message.getOriginationFee();
  if (f.length > 0) {
    writer.writeString(
      1,
      f
    );
  }
  f = message.getDiscountPoints();
  if (f.length > 0) {
    writer.writeString(
      2,
      f
    );
  }
  f = message.getOtherLenderFees();
  if (f.length > 0) {
    writer.writeString(
      3,
      f
    );
  }
  f = message.getTitleSettlement();
  if (f.length > 0) {
    writer.writeString(
      4,
      f
    );
  }
  f = message.getAppraisalFee();
  if (f.length > 0) {
    writer.writeString(
      5,
      f
    );
  }
  f = message.getInspectionFee();
  if (f.length > 0) {
    writer.writeString(
      6,
      f
    );
  }
  f = message.getRecordingFees();
  if (f.length > 0) {
    writer.writeString(
      7,
      f
    );
  }
  f = message.getTransferTax();
  if (f.length > 0) {
    writer.writeString(
      8,
      f
    );
  }
  f = message.getHomeownersInsurancePrepaid();
  if (f.length > 0) {
    writer.writeString(
      9,
      f
    );
  }
  f = message.getPropertyTaxEscrow();
  if (f.length > 0) {
    writer.writeString(
      10,
      f
    );
  }
  f = message.getPrepaidInterest();
  if (f.length > 0) {
    writer.writeString(
      11,
      f
    );
  }
  f = message.getPrepaidInterestDays();
  if (f !== 0) {
    writer.writeInt32(
      12,
      f
    );
  }
  f = message.getItemisedSubtotal();
  if (f.length > 0) {
    writer.writeString(
      13,
      f
    );
  }
  f = message.getSellerLenderCredits();
  if (f.length > 0) {
    writer.writeString(
      14,
      f
    );
  }
  f = message.getTotalClosingCosts();
  if (f.length > 0) {
    writer.writeString(
      15,
      f
    );
  }
  f = message.getLoanAmount();
  if (f.length > 0) {
    writer.writeString(
      16,
      f
    );
  }
  f = message.getDownPayment();
  if (f.length > 0) {
    writer.writeString(
      17,
      f
    );
  }
  f = message.getTotalCashToClose();
  if (f.length > 0) {
    writer.writeString(
      18,
      f
    );
  }
  f = message.getClosingCostsPercentOfPrice();
  if (f !== 0.0) {
    writer.writeDouble(
      19,
      f
    );
  }
};


/**
 * optional string origination_fee = 1;
 * @return {string}
 */
proto.sensen.finance.ClosingCostsResponse.prototype.getOriginationFee = function() {
  return /** @type {string} */ (jspb.Message.getFieldWithDefault(this, 1, ""));
};


/**
 * @param {string} value
 * @return {!proto.sensen.finance.ClosingCostsResponse} returns this
 */
proto.sensen.finance.ClosingCostsResponse.prototype.setOriginationFee = function(value) {
  return jspb.Message.setProto3StringField(this, 1, value);
};


/**
 * optional string discount_points = 2;
 * @return {string}
 */
proto.sensen.finance.ClosingCostsResponse.prototype.getDiscountPoints = function() {
  return /** @type {string} */ (jspb.Message.getFieldWithDefault(this, 2, ""));
};


/**
 * @param {string} value
 * @return {!proto.sensen.finance.ClosingCostsResponse} returns this
 */
proto.sensen.finance.ClosingCostsResponse.prototype.setDiscountPoints = function(value) {
  return jspb.Message.setProto3StringField(this, 2, value);
};


/**
 * optional string other_lender_fees = 3;
 * @return {string}
 */
proto.sensen.finance.ClosingCostsResponse.prototype.getOtherLenderFees = function() {
  return /** @type {string} */ (jspb.Message.getFieldWithDefault(this, 3, ""));
};


/**
 * @param {string} value
 * @return {!proto.sensen.finance.ClosingCostsResponse} returns this
 */
proto.sensen.finance.ClosingCostsResponse.prototype.setOtherLenderFees = function(value) {
  return jspb.Message.setProto3StringField(this, 3, value);
};


/**
 * optional string title_settlement = 4;
 * @return {string}
 */
proto.sensen.finance.ClosingCostsResponse.prototype.getTitleSettlement = function() {
  return /** @type {string} */ (jspb.Message.getFieldWithDefault(this, 4, ""));
};


/**
 * @param {string} value
 * @return {!proto.sensen.finance.ClosingCostsResponse} returns this
 */
proto.sensen.finance.ClosingCostsResponse.prototype.setTitleSettlement = function(value) {
  return jspb.Message.setProto3StringField(this, 4, value);
};


/**
 * optional string appraisal_fee = 5;
 * @return {string}
 */
proto.sensen.finance.ClosingCostsResponse.prototype.getAppraisalFee = function() {
  return /** @type {string} */ (jspb.Message.getFieldWithDefault(this, 5, ""));
};


/**
 * @param {string} value
 * @return {!proto.sensen.finance.ClosingCostsResponse} returns this
 */
proto.sensen.finance.ClosingCostsResponse.prototype.setAppraisalFee = function(value) {
  return jspb.Message.setProto3StringField(this, 5, value);
};


/**
 * optional string inspection_fee = 6;
 * @return {string}
 */
proto.sensen.finance.ClosingCostsResponse.prototype.getInspectionFee = function() {
  return /** @type {string} */ (jspb.Message.getFieldWithDefault(this, 6, ""));
};


/**
 * @param {string} value
 * @return {!proto.sensen.finance.ClosingCostsResponse} returns this
 */
proto.sensen.finance.ClosingCostsResponse.prototype.setInspectionFee = function(value) {
  return jspb.Message.setProto3StringField(this, 6, value);
};


/**
 * optional string recording_fees = 7;
 * @return {string}
 */
proto.sensen.finance.ClosingCostsResponse.prototype.getRecordingFees = function() {
  return /** @type {string} */ (jspb.Message.getFieldWithDefault(this, 7, ""));
};


/**
 * @param {string} value
 * @return {!proto.sensen.finance.ClosingCostsResponse} returns this
 */
proto.sensen.finance.ClosingCostsResponse.prototype.setRecordingFees = function(value) {
  return jspb.Message.setProto3StringField(this, 7, value);
};


/**
 * optional string transfer_tax = 8;
 * @return {string}
 */
proto.sensen.finance.ClosingCostsResponse.prototype.getTransferTax = function() {
  return /** @type {string} */ (jspb.Message.getFieldWithDefault(this, 8, ""));
};


/**
 * @param {string} value
 * @return {!proto.sensen.finance.ClosingCostsResponse} returns this
 */
proto.sensen.finance.ClosingCostsResponse.prototype.setTransferTax = function(value) {
  return jspb.Message.setProto3StringField(this, 8, value);
};


/**
 * optional string homeowners_insurance_prepaid = 9;
 * @return {string}
 */
proto.sensen.finance.ClosingCostsResponse.prototype.getHomeownersInsurancePrepaid = function() {
  return /** @type {string} */ (jspb.Message.getFieldWithDefault(this, 9, ""));
};


/**
 * @param {string} value
 * @return {!proto.sensen.finance.ClosingCostsResponse} returns this
 */
proto.sensen.finance.ClosingCostsResponse.prototype.setHomeownersInsurancePrepaid = function(value) {
  return jspb.Message.setProto3StringField(this, 9, value);
};


/**
 * optional string property_tax_escrow = 10;
 * @return {string}
 */
proto.sensen.finance.ClosingCostsResponse.prototype.getPropertyTaxEscrow = function() {
  return /** @type {string} */ (jspb.Message.getFieldWithDefault(this, 10, ""));
};


/**
 * @param {string} value
 * @return {!proto.sensen.finance.ClosingCostsResponse} returns this
 */
proto.sensen.finance.ClosingCostsResponse.prototype.setPropertyTaxEscrow = function(value) {
  return jspb.Message.setProto3StringField(this, 10, value);
};


/**
 * optional string prepaid_interest = 11;
 * @return {string}
 */
proto.sensen.finance.ClosingCostsResponse.prototype.getPrepaidInterest = function() {
  return /** @type {string} */ (jspb.Message.getFieldWithDefault(this, 11, ""));
};


/**
 * @param {string} value
 * @return {!proto.sensen.finance.ClosingCostsResponse} returns this
 */
proto.sensen.finance.ClosingCostsResponse.prototype.setPrepaidInterest = function(value) {
  return jspb.Message.setProto3StringField(this, 11, value);
};


/**
 * optional int32 prepaid_interest_days = 12;
 * @return {number}
 */
proto.sensen.finance.ClosingCostsResponse.prototype.getPrepaidInterestDays = function() {
  return /** @type {number} */ (jspb.Message.getFieldWithDefault(this, 12, 0));
};


/**
 * @param {number} value
 * @return {!proto.sensen.finance.ClosingCostsResponse} returns this
 */
proto.sensen.finance.ClosingCostsResponse.prototype.setPrepaidInterestDays = function(value) {
  return jspb.Message.setProto3IntField(this, 12, value);
};


/**
 * optional string itemised_subtotal = 13;
 * @return {string}
 */
proto.sensen.finance.ClosingCostsResponse.prototype.getItemisedSubtotal = function() {
  return /** @type {string} */ (jspb.Message.getFieldWithDefault(this, 13, ""));
};


/**
 * @param {string} value
 * @return {!proto.sensen.finance.ClosingCostsResponse} returns this
 */
proto.sensen.finance.ClosingCostsResponse.prototype.setItemisedSubtotal = function(value) {
  return jspb.Message.setProto3StringField(this, 13, value);
};


/**
 * optional string seller_lender_credits = 14;
 * @return {string}
 */
proto.sensen.finance.ClosingCostsResponse.prototype.getSellerLenderCredits = function() {
  return /** @type {string} */ (jspb.Message.getFieldWithDefault(this, 14, ""));
};


/**
 * @param {string} value
 * @return {!proto.sensen.finance.ClosingCostsResponse} returns this
 */
proto.sensen.finance.ClosingCostsResponse.prototype.setSellerLenderCredits = function(value) {
  return jspb.Message.setProto3StringField(this, 14, value);
};


/**
 * optional string total_closing_costs = 15;
 * @return {string}
 */
proto.sensen.finance.ClosingCostsResponse.prototype.getTotalClosingCosts = function() {
  return /** @type {string} */ (jspb.Message.getFieldWithDefault(this, 15, ""));
};


/**
 * @param {string} value
 * @return {!proto.sensen.finance.ClosingCostsResponse} returns this
 */
proto.sensen.finance.ClosingCostsResponse.prototype.setTotalClosingCosts = function(value) {
  return jspb.Message.setProto3StringField(this, 15, value);
};


/**
 * optional string loan_amount = 16;
 * @return {string}
 */
proto.sensen.finance.ClosingCostsResponse.prototype.getLoanAmount = function() {
  return /** @type {string} */ (jspb.Message.getFieldWithDefault(this, 16, ""));
};


/**
 * @param {string} value
 * @return {!proto.sensen.finance.ClosingCostsResponse} returns this
 */
proto.sensen.finance.ClosingCostsResponse.prototype.setLoanAmount = function(value) {
  return jspb.Message.setProto3StringField(this, 16, value);
};


/**
 * optional string down_payment = 17;
 * @return {string}
 */
proto.sensen.finance.ClosingCostsResponse.prototype.getDownPayment = function() {
  return /** @type {string} */ (jspb.Message.getFieldWithDefault(this, 17, ""));
};


/**
 * @param {string} value
 * @return {!proto.sensen.finance.ClosingCostsResponse} returns this
 */
proto.sensen.finance.ClosingCostsResponse.prototype.setDownPayment = function(value) {
  return jspb.Message.setProto3StringField(this, 17, value);
};


/**
 * optional string total_cash_to_close = 18;
 * @return {string}
 */
proto.sensen.finance.ClosingCostsResponse.prototype.getTotalCashToClose = function() {
  return /** @type {string} */ (jspb.Message.getFieldWithDefault(this, 18, ""));
};


/**
 * @param {string} value
 * @return {!proto.sensen.finance.ClosingCostsResponse} returns this
 */
proto.sensen.finance.ClosingCostsResponse.prototype.setTotalCashToClose = function(value) {
  return jspb.Message.setProto3StringField(this, 18, value);
};


/**
 * optional double closing_costs_percent_of_price = 19;
 * @return {number}
 */
proto.sensen.finance.ClosingCostsResponse.prototype.getClosingCostsPercentOfPrice = function() {
  return /** @type {number} */ (jspb.Message.getFloatingPointFieldWithDefault(this, 19, 0.0));
};


/**
 * @param {number} value
 * @return {!proto.sensen.finance.ClosingCostsResponse} returns this
 */
proto.sensen.finance.ClosingCostsResponse.prototype.setClosingCostsPercentOfPrice = function(value) {
  return jspb.Message.setProto3FloatField(this, 19, value);
};


/**
 * @enum {number}
 */
proto.sensen.finance.AnnuityTiming = {
  END_OF_PERIOD: 0,
  BEGINNING_OF_PERIOD: 1
};

/**
 * @enum {number}
 */
proto.sensen.finance.OptionType = {
  CALL: 0,
  PUT: 1
};

/**
 * @enum {number}
 */
proto.sensen.finance.ExerciseType = {
  EUROPEAN: 0,
  AMERICAN: 1,
  BERMUDAN: 2
};

/**
 * @enum {number}
 */
proto.sensen.finance.AsianType = {
  NOT_ASIAN: 0,
  AVERAGE_PRICE: 1,
  AVERAGE_STRIKE: 2
};

goog.object.extend(exports, proto.sensen.finance);
