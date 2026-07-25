#!/bin/bash
set -e

echo "Starting OptionsCalculatorEngine backend..."
/app/build/calculator_engine &
BACKEND_PID=$!

echo "Starting Envoy proxy..."
envoy -c /etc/envoy/envoy.yaml &
ENVOY_PID=$!

# Wait for any process to exit
wait -n $BACKEND_PID $ENVOY_PID
exit $?
