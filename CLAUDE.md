# Options & Futures Profit Calculator - Claude Guide

This file provides context, build instructions, and strict coding policies for Claude.

## Project Overview
An enterprise-grade web application giving retail traders and institutions real-time, interactive P&L matrix visualizations, risk probability models, and option sensitivity breakdowns.
- **Backend:** C++23 Native Modular Engine (No Exceptions, ROP only).
- **Frontend:** Next.js with React Three Fiber (Vanilla CSS + Glassmorphism).
- **Database/Auth:** Supabase with strict RLS and Edge Functions.

## Build Commands
- **Backend Build & Test:** `./scripts/build_common.sh`
- **Frontend Build:** `cd frontend && npm run build`
- **Frontend Dev:** `cd frontend && npm run dev`
- **Sync & Adversarial CI:** `./scripts/code_update_sync.sh "Commit message"`

## Strict C++ Backend Policies
1. **Modules Over Headers:** USE `import std;` instead of `#include <iostream>`, etc.
2. **Error Handling (ROP):** NO EXCEPTIONS (`throw`, `try/catch`). You MUST use Railway Oriented Programming (ROP) via `std::expected` and monadic `.and_then()`, `.transform()`, `.or_else()`.
3. **Performance:** Do not use the fast-math compiler flag. Use Intel TBB and the internal SIMD library (`sensen`).
4. **Testing:** No external libraries (no Google Test, Catch2, etc.). Use the internal functional/fluent API test runner defined in `backend/src/modules/testing_framework.cppm`.
5. **No `cat` / `ls`:** When scripting, use specific native tooling.

## Strict Frontend Policies (Next.js)
1. **Vanilla CSS ONLY:** Do NOT use Tailwind CSS or any utility classes. All styling must be written in standard CSS using the established glassmorphic tokens in `frontend/src/app/globals.css`.
2. **State Management:** Use Zustand (`src/store/useCalculatorStore.ts`).
3. **Data Fetching/Networking:** Use gRPC-Web interacting with protobuf models.
4. **WebGL:** Use `@react-three/fiber` for 3D visualizations.

## Supabase & Edge Functions
- Edge Functions are built in Deno (`supabase/functions/`).
- Initial schemas are in `supabase/migrations/20260723000000_initial_schema.sql`.

## General Instructions
- **Do not bypass the adversarial CI:** If a commit is rejected, fix the issue described by the Tri-Agent consensus output.
- **Adhere to Code Policy:** Check `config/cpp_details.txt` and `config/update_policy.txt` if uncertain about architectural rules.
