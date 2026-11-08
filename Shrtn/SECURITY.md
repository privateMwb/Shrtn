# Security Policy

## Supported versions

Shrtn is a small, individually maintained project. Security fixes are made on the `main`
branch and shipped in the next release; older releases are not patched.

| Version | Supported |
|---|:-:|
| `main` and the latest release | ✅ |
| Anything older | ❌ |

## Reporting a vulnerability

**Please do not report a suspected vulnerability in a public issue, pull request, or
discussion.** Use GitHub's private reporting instead, so a fix can be prepared before the
details are public:

1. Open this repository's **Security** tab.
2. Choose **Report a vulnerability**.
3. Describe the problem.

A useful report includes:

- what is affected (the server, a specific route, the frontend, the Docker image) and the
  commit or release you tested;
- steps to reproduce, ideally the exact `curl` request or input bytes;
- what you expected and what happened (a crash, a sanitizer report, a redirect to somewhere
  it shouldn't);
- any suggested fix.

This is a spare-time project, so responses are best-effort. You will get an
acknowledgement and an honest assessment, and credit in the fix if you want it.

## Security model: read this before deploying

Shrtn is a portfolio-scale URL shortener. It has **no accounts, no authentication, and no
authorization**, and these limits are deliberate scope decisions, not oversights:

- **Private links are unlisted, not protected.** The `private` flag only keeps a link out of
  `GET /links`. Anyone who has or guesses the code can use `GET /:code`, and
  `GET /api/links/:code` never checks the flag. With 62⁶ possible codes, guessing is
  impractical, but a code is a bearer secret.
- **`GET /links` is one list shared by every visitor** to an instance.
- **`/api/metrics` is unauthenticated.** Anyone who can reach the port can read recent route
  patterns, methods, status codes and timings. Route *patterns* are recorded, never raw
  paths (so never short codes), query strings or bodies.
- **Redirects go wherever a link says.** The server only stores `http://` and `https://`
  URLs of at most 2048 characters, which blocks `javascript:` and `data:` targets. It does
  not check reputation, so a shortener can be used to hide where a link leads. Don't expose
  an open instance to people you don't trust.
- **Rate limiting trusts `X-Forwarded-For`.** The limiter keys on the first address in that
  header, which a caller can set to anything if they can reach the server directly rather
  than through the trusted proxy. When the header is missing, every caller shares one
  bucket.
- **No TLS.** Traffic is plaintext HTTP unless a proxy such as Render terminates it.
- **CORS allows every origin.** The server uses FalconHTTP's `Cors{}` with no argument, which
  sends `Access-Control-Allow-Origin: *`. CORS is a browser rule, not access control, either way.
- **Storage is one JSON file** (`data/shrtn.json`), rewritten on every change, with no
  schema migration path.

## Automated checks

CodeQL, clang-tidy, sanitizer runs (ASan, UBSan, TSan) and ClusterFuzzLite fuzzing of the
URL validation, short-code generation, and metrics serialization run in CI. They reduce
risk; they do not prove the absence of bugs.
