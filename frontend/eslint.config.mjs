import { defineConfig, globalIgnores } from "eslint/config";
import nextVitals from "eslint-config-next/core-web-vitals";
import nextTs from "eslint-config-next/typescript";

const eslintConfig = defineConfig([
  ...nextVitals,
  ...nextTs,
  // Override default ignores of eslint-config-next.
  globalIgnores([
    // Default ignores of eslint-config-next:
    ".next/**",
    "out/**",
    "build/**",
    "next-env.d.ts",
    // protoc output, committed because the CDN build has no protoc
    // (scripts/gen_proto.sh). Linting generated code reports defects nobody can
    // fix without editing a file the next `gen_proto.sh` overwrites -- and this
    // project has already paid once for reformatting a generated file.
    "src/grpc/**",
  ]),
]);

export default eslintConfig;
