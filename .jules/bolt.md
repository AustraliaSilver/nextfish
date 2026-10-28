## 2026-10-28 - [Precompile Regex in Loop/Callbacks]
**Learning:** Compiling regex patterns (e.g. `re.compile(...)`) inside functions that are called frequently (like callbacks that parse stdout from the engine line-by-line) introduces unnecessary overhead and hurts performance.
**Action:** When a regular expression is static or can use capture groups to validate dynamic parts, compile it once globally at the module level (e.g., `REGEX_NAME = re.compile(...)`) instead of re-compiling it locally on every function invocation.
