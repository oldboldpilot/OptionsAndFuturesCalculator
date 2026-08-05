#!/usr/bin/env node
//
// Minimal, runnable proof that the vendored gRPC-Web stub reaches the live
// service and gets back an exact answer. Run with:
//
//   npm install
//   node example/compute-payment.js
//
// This calls the SAME generated client (src/grpc/finance_pb.js +
// FinanceServiceClientPb.ts) a browser page would use -- it is not a
// shortcut. The only Node-specific piece is the XMLHttpRequest polyfill
// below: grpc-web's runtime assumes a browser's XHR, which Node does not
// have natively. A real browser page does NOT need the xhr2 line or
// dependency at all -- delete it when you copy this into page code.
'use strict';

global.XMLHttpRequest = require('xhr2');

const grpcWeb = require('grpc-web');
const finance_pb = require('../src/grpc/finance_pb.js');

const ENDPOINT = 'https://api.optionsandfuturescalculator.com';

// The generated FinanceServiceClientPb.ts wraps exactly this pattern per
// method (MethodDescriptor + client_.rpcCall). Building it inline here keeps
// this example runnable as plain JS with no TypeScript build step; a
// TypeScript consumer should import { FinanceClient } from
// '../src/grpc/FinanceServiceClientPb' instead and call
// `client.computePayment(req, metadata)` directly -- see README.md.
const client = new grpcWeb.GrpcWebClientBase({ format: 'text' });

const computePaymentDescriptor = new grpcWeb.MethodDescriptor(
  '/sensen.finance.Finance/ComputePayment',
  grpcWeb.MethodType.UNARY,
  finance_pb.PaymentRequest,
  finance_pb.DecimalResponse,
  (request) => request.serializeBinary(),
  finance_pb.DecimalResponse.deserializeBinary
);

// Monthly payment on a $300,000 loan at 6% nominal annual, 30 years.
// rate is PER PERIOD (0.06 / 12), not the annual rate.
const req = new finance_pb.PaymentRequest();
req.setRate('0.005');
req.setPeriods(360);
req.setPresentValue('300000');
// req.setFutureValue('0');   // omit for 0
// timing defaults to END_OF_PERIOD (ordinary annuity)

// x-api-key goes here as call metadata once a key is provisioned (see
// README.md "Headers"). Uncomment when mortgagefv-web has one:
// const metadata = { 'x-api-key': process.env.MORTGAGEFV_API_KEY };
const metadata = {};

client.rpcCall(
  `${ENDPOINT}/sensen.finance.Finance/ComputePayment`,
  req,
  metadata,
  computePaymentDescriptor,
  (err, res) => {
    if (err) {
      // err.code is the gRPC status (see README.md "Failure modes").
      console.error(`ComputePayment failed: grpc-status=${err.code} message=${err.message}`);
      process.exitCode = 1;
      return;
    }
    // res.getValue() is a decimal STRING, not a JS number -- see README.md
    // "The numeric contract" before doing any arithmetic with it.
    console.log('ComputePayment ->', res.getValue());
    console.log('(negative = cash outflow; this is correct, not a bug)');
  }
);
