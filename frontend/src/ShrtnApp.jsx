import { useState, useEffect, useCallback } from "react";

// Set via Vite env var, not guessed from window.location -- that
// guess only ever worked for local dev (port 8080) and would silently
// point a deployed frontend at the visitor's own localhost, where
// nothing is listening. Define VITE_API_BASE in frontend/.env.local
// for local overrides, or as a Vercel project environment variable
// for production (the Cloudflare Tunnel URL once that's live).
const API_BASE = import.meta.env.VITE_API_BASE || "http://localhost:8080";

function shortDisplay(code) {
  return API_BASE.replace(/^https?:\/\//, "") + "/" + code;
}

function fullUrl(code) {
  return API_BASE + "/" + code;
}

function formatDate(iso) {
  const d = new Date(iso);
  if (Number.isNaN(d.getTime())) return "";
  return d.toLocaleDateString(undefined, { month: "short", day: "numeric" });
}

// Mirrors the server's own check (http/https only) so a bad URL fails
// fast client-side, but the server is still the source of truth -- this
// is a courtesy, not a security boundary.
function looksAcceptable(url) {
  return /^https?:\/\//i.test(url.trim());
}

// Codes for private links this browser created -- the server excludes
// private rows from GET /links entirely, so this is the only record of
// them anywhere on the client. Real deployed code (not the in-chat
// artifact sandbox), so localStorage is fine here.
const PRIVATE_CODES_KEY = "shrtn:privateCodes";

function loadPrivateCodes() {
  try {
    const raw = localStorage.getItem(PRIVATE_CODES_KEY);
    return raw ? JSON.parse(raw) : [];
  } catch {
    return [];
  }
}

function savePrivateCode(code) {
  try {
    const codes = loadPrivateCodes();
    if (!codes.includes(code)) {
      localStorage.setItem(
        PRIVATE_CODES_KEY,
        JSON.stringify([...codes, code])
      );
    }
  } catch {
    // localStorage unavailable (private browsing, quota, etc.) -- the
    // link still exists on the server, it just won't be remembered
    // locally for the "Your links" merge below.
  }
}

export default function ShrtnApp() {
  const [urlInput, setUrlInput] = useState("");
  const [isPrivate, setIsPrivate] = useState(false);
  const [fieldError, setFieldError] = useState(null);
  const [submitting, setSubmitting] = useState(false);
  const [result, setResult] = useState(null); // { code, originalUrl }

  const [links, setLinks] = useState([]);
  const [linksStatus, setLinksStatus] = useState("loading"); // loading | loaded | empty | error

  const [toast, setToast] = useState(null);

  const loadLinks = useCallback(async () => {
    setLinksStatus("loading");
    try {
      const res = await fetch(`${API_BASE}/links`);
      if (!res.ok) throw new Error(`Server responded ${res.status}`);
      const publicLinks = await res.json();

      // Merge in this browser's own private links, fetched individually
      // since the server never returns them from the shared listing.
      const privateCodes = loadPrivateCodes();
      const privateLinks = (
        await Promise.all(
          privateCodes.map(async (code) => {
            try {
              const r = await fetch(`${API_BASE}/api/links/${code}`);
              if (!r.ok) return null; // e.g. deleted server-side
              return await r.json();
            } catch {
              return null;
            }
          })
        )
      ).filter(Boolean);

      const merged = [...publicLinks, ...privateLinks];
      setLinks(merged);
      setLinksStatus(merged.length === 0 ? "empty" : "loaded");
    } catch {
      setLinksStatus("error");
    }
  }, []);

  useEffect(() => {
    loadLinks();
  }, [loadLinks]);

  useEffect(() => {
    if (!toast) return;
    const t = setTimeout(() => setToast(null), 1800);
    return () => clearTimeout(t);
  }, [toast]);

  async function handleSubmit(e) {
    e.preventDefault();
    const url = urlInput.trim();

    if (!url) {
      setFieldError("Paste a link first.");
      return;
    }
    if (!looksAcceptable(url)) {
      setFieldError("Link needs to start with http:// or https://");
      return;
    }
    setFieldError(null);
    setSubmitting(true);

    try {
      const res = await fetch(`${API_BASE}/shorten`, {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ url, private: isPrivate }),
      });

      if (!res.ok) {
        setFieldError(
          res.status === 400
            ? "That link was rejected -- check it starts with http:// or https://"
            : `Something went wrong (${res.status}). Try again.`
        );
        return;
      }

      const data = await res.json();
      if (isPrivate) savePrivateCode(data.code);
      setResult({ code: data.code, originalUrl: url });
      setUrlInput("");
      loadLinks();
    } catch {
      setFieldError("Couldn't reach the server. Is it running?");
    } finally {
      setSubmitting(false);
    }
  }

  async function handleCopy() {
    if (!result) return;
    try {
      await navigator.clipboard.writeText(fullUrl(result.code));
      setToast("Copied to clipboard");
    } catch {
      setToast("Copy failed -- select and copy manually");
    }
  }

  // Newest first -- GET /links doesn't guarantee scan order, so sort
  // client-side rather than assume the backend already did. Capped at
  // 50 so the list stays scrollable inside a fixed-height card rather
  // than growing the page indefinitely.
  const sortedLinks = [...links]
    .sort((a, b) => (a.createdAt < b.createdAt ? 1 : -1))
    .slice(0, 50);

  return (
    <div style={styles.body}>
      <style>{`
        ${fontImport}
        html, body, #root {
          margin: 0;
          padding: 0;
          height: 100%;
          background: ${colors.bg};
        }
        @media (max-width: 420px) {
          .shrtn-link-date { display: none; }
          .shrtn-result { flex-wrap: wrap; }
          .shrtn-copy-btn { width: 100%; }
        }
        @media (min-width: 640px) {
          .shrtn-page { padding-top: 64px; }
        }
      `}</style>
      <div style={styles.page} className="shrtn-page">
        <div style={styles.brandRow}>
          <div style={styles.dot} />
          <h1 style={styles.wordmark}>
            shr<span style={{ color: colors.cyan }}>tn</span>
          </h1>
        </div>
        <p style={styles.tagline}>
          Paste a long link. Get a short one. Watch where it goes.
        </p>

        <form style={styles.inputCard} onSubmit={handleSubmit} noValidate>
          <label style={styles.inputLabel} htmlFor="url-input">
            Shorten a link
          </label>
          <div style={styles.inputRow}>
            <input
              id="url-input"
              style={{
                ...styles.urlInput,
                ...(fieldError ? styles.urlInputInvalid : {}),
              }}
              type="text"
              inputMode="url"
              placeholder="https://example.com/your-long-link"
              autoComplete="off"
              value={urlInput}
              onChange={(e) => {
                setUrlInput(e.target.value);
                if (fieldError) setFieldError(null);
              }}
            />
            <button
              type="submit"
              style={{
                ...styles.shrtnBtn,
                ...(submitting ? styles.shrtnBtnDisabled : {}),
              }}
              disabled={submitting}
            >
              {submitting ? "Shortening…" : "Shorten"}
            </button>
          </div>
          <label style={styles.privateToggleRow} htmlFor="private-toggle">
            <span
              style={{
                ...styles.toggleTrack,
                ...(isPrivate ? styles.toggleTrackActive : {}),
              }}
              role="switch"
              aria-checked={isPrivate}
            >
              <span
                style={{
                  ...styles.toggleThumb,
                  ...(isPrivate ? styles.toggleThumbActive : {}),
                }}
              />
            </span>
            <input
              id="private-toggle"
              type="checkbox"
              checked={isPrivate}
              onChange={(e) => setIsPrivate(e.target.checked)}
              style={styles.visuallyHidden}
            />
            <span style={styles.toggleLabel}>
              Private -- hide from the shared list
            </span>
          </label>
          {fieldError && (
            <p style={styles.fieldError} role="alert">
              {fieldError}
            </p>
          )}
        </form>

        {result && (
          <div style={styles.result} className="shrtn-result">
            <div style={styles.stamp}>
              <span style={styles.stampN}>0</span>
              <span style={styles.stampU}>clicks</span>
            </div>
            <div style={styles.resultBody}>
              <a
                style={styles.resultShort}
                href={fullUrl(result.code)}
                target="_blank"
                rel="noopener noreferrer"
              >
                {shortDisplay(result.code)}
              </a>
              <span style={styles.resultOriginal}>
                {result.originalUrl.replace(/^https?:\/\//, "")}
              </span>
            </div>
            <button style={styles.copyBtn} className="shrtn-copy-btn" onClick={handleCopy} type="button">
              Copy
            </button>
          </div>
        )}

        <div style={styles.sectionLabel}>
          <span>Your links</span>
          <span>{linksStatus === "loaded" ? links.length : ""}</span>
        </div>

        <div style={styles.linksCard}>
          {linksStatus === "loading" && (
            <div style={styles.listState}>
              <p style={styles.stateTitle}>Loading…</p>
            </div>
          )}

          {linksStatus === "error" && (
            <div style={styles.listState}>
              <p style={{ ...styles.stateTitle, color: colors.red }}>
                Couldn't load your links
              </p>
              <p style={styles.stateSub}>
                {API_BASE} isn't responding. Check that the server's running.
              </p>
            </div>
          )}

          {linksStatus === "empty" && (
            <div style={styles.listState}>
              <p style={styles.stateTitle}>No links yet</p>
              <p style={styles.stateSub}>
                Shorten one above to see it here.
              </p>
            </div>
          )}

          {linksStatus === "loaded" &&
            sortedLinks.map((link, i) => (
              <div
                key={link.code}
                style={{
                  ...styles.linkRow,
                  borderBottom:
                    i === sortedLinks.length - 1
                      ? "none"
                      : `1px solid ${colors.line}`,
                }}
              >
                <div
                  style={{
                    ...styles.miniStamp,
                    ...(link.clickCount > 0 ? styles.miniStampActive : {}),
                  }}
                >
                  <span style={styles.miniStampN}>{link.clickCount}</span>
                  <span style={styles.miniStampU}>clicks</span>
                </div>
                <div style={styles.linkInfo}>
                  <a
                    href={fullUrl(link.code)}
                    target="_blank"
                    rel="noopener noreferrer"
                    style={{ textDecoration: "none" }}
                  >
                    <span style={styles.linkShort}>
                      {shortDisplay(link.code)}
                      {link.isPrivate && (
                        <span style={styles.privateTag}> · private</span>
                      )}
                    </span>
                  </a>
                  <span style={styles.linkOriginal} title={link.originalUrl}>
                    {link.originalUrl.replace(/^https?:\/\//, "")}
                  </span>
                </div>
                <div style={styles.linkDate} className="shrtn-link-date">
                  {formatDate(link.createdAt)}
                </div>
              </div>
            ))}
        </div>
      </div>

      {toast && <div style={styles.toast}>{toast}</div>}
    </div>
  );
}

const colors = {
  bg: "#0B0E11",
  card: "#12161B",
  line: "#1A1F26",
  text: "#E5E7EB",
  textDim: "#6B7280",
  cyan: "#5EEAD4",
  cyanSoft: "rgba(94, 234, 212, 0.1)",
  amber: "#F5A623",
  red: "#F87171",
};

const fontDisplay = '"Inter", sans-serif';
const fontBody = '"Inter", sans-serif';
const fontMono = '"JetBrains Mono", ui-monospace, monospace';

const fontImport = `
  @import url('https://fonts.googleapis.com/css2?family=Inter:wght@400;500;600;700&family=JetBrains+Mono:wght@400;500;600&display=swap');
`;

const styles = {
  body: {
    margin: 0,
    background: colors.bg,
    color: colors.text,
    fontFamily: fontBody,
    minHeight: "100vh",
    WebkitFontSmoothing: "antialiased",
  },
  page: {
    maxWidth: 720,
    margin: "0 auto",
    padding: "48px 20px 80px",
  },
  brandRow: {
    display: "flex",
    alignItems: "center",
    gap: 10,
    marginBottom: 6,
  },
  dot: {
    width: 8,
    height: 8,
    borderRadius: "50%",
    background: colors.cyan,
    boxShadow: `0 0 8px ${colors.cyan}`,
    flexShrink: 0,
  },
  wordmark: {
    fontFamily: fontDisplay,
    fontWeight: 700,
    fontSize: "clamp(30px, 7vw, 40px)",
    letterSpacing: "-0.02em",
    margin: 0,
  },
  tagline: {
    color: colors.textDim,
    fontSize: 15,
    margin: "0 0 32px",
    maxWidth: "42ch",
  },
  inputCard: {
    background: colors.card,
    border: `1px solid ${colors.line}`,
    borderRadius: 10,
    padding: 18,
    marginBottom: 16,
  },
  inputLabel: {
    fontFamily: fontMono,
    fontSize: 10.5,
    letterSpacing: "0.08em",
    textTransform: "uppercase",
    color: colors.textDim,
    marginBottom: 10,
    display: "block",
  },
  inputRow: {
    display: "flex",
    gap: 10,
    flexWrap: "wrap",
  },
  urlInput: {
    flex: "1 1 260px",
    minWidth: 0,
    border: `1px solid ${colors.line}`,
    background: colors.bg,
    borderRadius: 6,
    fontFamily: fontMono,
    fontSize: 14,
    color: colors.text,
    padding: "11px 13px",
    outline: "none",
  },
  urlInputInvalid: {
    borderColor: colors.red,
  },
  shrtnBtn: {
    background: colors.cyan,
    color: colors.bg,
    border: "none",
    borderRadius: 6,
    padding: "11px 22px",
    fontFamily: fontDisplay,
    fontWeight: 700,
    fontSize: 14,
    cursor: "pointer",
  },
  shrtnBtnDisabled: {
    opacity: 0.5,
    cursor: "default",
  },
  fieldError: {
    fontFamily: fontMono,
    fontSize: 12,
    color: colors.red,
    marginTop: 10,
    marginBottom: 0,
  },
  privateToggleRow: {
    display: "flex",
    alignItems: "center",
    gap: 10,
    marginTop: 14,
    cursor: "pointer",
    userSelect: "none",
  },
  visuallyHidden: {
    position: "absolute",
    width: 1,
    height: 1,
    padding: 0,
    margin: -1,
    overflow: "hidden",
    clip: "rect(0, 0, 0, 0)",
    whiteSpace: "nowrap",
    border: 0,
  },
  toggleTrack: {
    flexShrink: 0,
    width: 34,
    height: 20,
    borderRadius: 999,
    background: colors.line,
    border: `1px solid ${colors.line}`,
    position: "relative",
    transition: "background 150ms ease, border-color 150ms ease",
  },
  toggleTrackActive: {
    background: colors.cyanSoft,
    borderColor: colors.cyan,
  },
  toggleThumb: {
    position: "absolute",
    top: 2,
    left: 2,
    width: 14,
    height: 14,
    borderRadius: "50%",
    background: colors.textDim,
    transition: "transform 150ms ease, background 150ms ease",
  },
  toggleThumbActive: {
    transform: "translateX(14px)",
    background: colors.cyan,
  },
  toggleLabel: {
    fontFamily: fontMono,
    fontSize: 11.5,
    color: colors.textDim,
  },
  result: {
    background: colors.card,
    border: `1px solid ${colors.cyan}`,
    borderRadius: 10,
    padding: "16px 18px",
    display: "flex",
    alignItems: "center",
    gap: 14,
    marginBottom: 40,
  },
  stamp: {
    flexShrink: 0,
    width: 50,
    height: 50,
    borderRadius: "50%",
    border: `1.5px solid ${colors.cyan}`,
    display: "flex",
    flexDirection: "column",
    alignItems: "center",
    justifyContent: "center",
    color: colors.cyan,
    fontFamily: fontMono,
    lineHeight: 1,
  },
  stampN: { fontSize: 15, fontWeight: 600 },
  stampU: {
    fontSize: 7,
    letterSpacing: "0.05em",
    marginTop: 2,
    color: colors.textDim,
  },
  resultBody: { minWidth: 0, flex: 1 },
  resultShort: {
    fontFamily: fontMono,
    fontSize: 16,
    fontWeight: 600,
    color: colors.cyan,
    display: "block",
    marginBottom: 3,
    textDecoration: "none",
  },
  resultOriginal: {
    fontSize: 12.5,
    color: colors.textDim,
    whiteSpace: "nowrap",
    overflow: "hidden",
    textOverflow: "ellipsis",
    display: "block",
  },
  copyBtn: {
    flexShrink: 0,
    background: colors.cyanSoft,
    color: colors.cyan,
    border: `1px solid ${colors.line}`,
    borderRadius: 6,
    padding: "8px 14px",
    fontFamily: fontBody,
    fontWeight: 600,
    fontSize: 12.5,
    cursor: "pointer",
    minWidth: 64,
  },
  sectionLabel: {
    fontFamily: fontMono,
    fontSize: 11,
    letterSpacing: "0.08em",
    textTransform: "uppercase",
    color: colors.textDim,
    marginBottom: 12,
    display: "flex",
    alignItems: "center",
    justifyContent: "space-between",
  },
  linksCard: {
    background: colors.card,
    border: `1px solid ${colors.line}`,
    borderRadius: 10,
    overflowY: "auto",
    maxHeight: 420,
  },
  linkRow: {
    display: "grid",
    gridTemplateColumns: "40px 1fr auto auto",
    alignItems: "center",
    gap: 14,
    padding: "14px 16px",
  },
  miniStamp: {
    width: 36,
    height: 36,
    borderRadius: "50%",
    border: `1px solid ${colors.line}`,
    display: "flex",
    flexDirection: "column",
    alignItems: "center",
    justifyContent: "center",
    fontFamily: fontMono,
    color: colors.textDim,
    lineHeight: 1,
  },
  miniStampActive: {
    borderColor: colors.amber,
    color: colors.amber,
  },
  miniStampN: { fontSize: 12, fontWeight: 600 },
  miniStampU: { fontSize: 5.5, letterSpacing: "0.04em", marginTop: 1 },
  linkInfo: { minWidth: 0 },
  linkShort: {
    fontFamily: fontMono,
    fontSize: 13.5,
    fontWeight: 600,
    color: colors.text,
    display: "block",
  },
  privateTag: {
    color: colors.amber,
    fontWeight: 500,
    fontSize: 11.5,
  },
  linkOriginal: {
    fontSize: 12,
    color: colors.textDim,
    whiteSpace: "nowrap",
    overflow: "hidden",
    textOverflow: "ellipsis",
    display: "block",
    maxWidth: "100%",
  },
  linkDate: {
    fontFamily: fontMono,
    fontSize: 11,
    color: colors.textDim,
    whiteSpace: "nowrap",
  },
  listState: {
    padding: "40px 16px",
    textAlign: "center",
  },
  stateTitle: {
    fontFamily: fontMono,
    fontSize: 13,
    color: colors.textDim,
    margin: "0 0 4px",
  },
  stateSub: {
    fontSize: 12.5,
    color: colors.textDim,
    opacity: 0.7,
    margin: 0,
  },
  toast: {
    position: "fixed",
    bottom: 24,
    left: "50%",
    transform: "translateX(-50%)",
    background: colors.card,
    border: `1px solid ${colors.cyan}`,
    color: colors.text,
    fontSize: 13,
    padding: "10px 18px",
    borderRadius: 8,
  },
};