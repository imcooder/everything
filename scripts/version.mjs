import { describeVersion } from "./lib/version.mjs";

const { base, resolved, source } = describeVersion();
console.log(resolved);
console.log(`  package.json: ${base}`);
console.log(`  patch from:   ${source}`);
