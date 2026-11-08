// ============================================================
// scripts/capture-screenshots.mjs
//
// Takes the README screenshots of the Shrtn frontend with Playwright,
// on a phone-sized viewport:
//
//   shrtn.png               a freshly shortened link: the result card with its
//                           copy button, above the list of links with clicks
//   shrtn-private.png       a private link created: it shows in the creator's
//                           list tagged "private"
//   shrtn-error.png         a URL the browser refuses (javascript:), with the
//                           inline error
//   shrtn-server-down.png   the links endpoint unreachable: the page says the
//                           server isn't responding
//
// Run by .github/workflows/screenshots.yml, which installs Playwright next to
// this script and starts the server and the frontend first. Config comes from
// the environment:
//
//   APP_URL    frontend to open                   (default http://localhost:4173)
//   API_BASE   backend the frontend talks to, used to register a few visits on
//              the example links so the click counts are not all zero
//              (default: skip the visits)
//   OUT_DIR    where PNGs go                      (default ./docs/assets)
//
// The script creates a handful of example links through the UI, so on a live
// deployment it adds those rows to its database. Nothing is changed or deleted.
// ============================================================

import { chromium } from "playwright";
import { mkdir } from "node:fs/promises";
import path from "node:path";

const APP_URL = process.env.APP_URL ?? "http://localhost:4173";
const API_BASE = (process.env.API_BASE ?? "").replace(/\/$/, "");
const OUT = process.env.OUT_DIR ?? "docs/assets";

await mkdir(OUT, { recursive: true });

const browser = await chromium.launch();
const context = await browser.newContext({
  viewport: { width: 390, height: 844 },
  deviceScaleFactor: 2,
  colorScheme: "dark",
  reducedMotion: "reduce",
});
const page = await context.newPage();

const shot = async (name) => {
  const file = path.join(OUT, name);
  await page.screenshot({ path: file, fullPage: true });
  console.log("saved", file);
};

// Types a URL into the form, optionally flips the private switch, submits, and
// returns the code the server answered with.
const shorten = async (url, { isPrivate = false } = {}) => {
  const input = page.getByPlaceholder("https://example.com/your-long-link");
  await input.fill(url);

  const toggle = page.locator("#private-toggle");
  if ((await toggle.isChecked()) !== isPrivate) {
    await page.locator('label[for="private-toggle"]').click();
  }

  const [response] = await Promise.all([
    page.waitForResponse(
      (r) => r.url().endsWith("/shorten") && r.request().method() === "POST",
      { timeout: 60_000 },
    ),
    page.getByRole("button", { name: "Shorten", exact: true }).click(),
  ]);
  const { code } = await response.json();
  await page.getByRole("button", { name: "Copy" }).waitFor({ timeout: 20_000 });
  return code;
};

// Registers a few visits on a code without following the redirect.
const visit = async (code, times) => {
  if (!API_BASE) return;
  for (let i = 0; i < times; i++) {
    await context.request.get(`${API_BASE}/${code}`, { maxRedirects: 0 });
  }
};

try {
  console.log("opening", APP_URL);
  await page.goto(APP_URL, { waitUntil: "domcontentloaded" });
  await page.getByPlaceholder("https://example.com/your-long-link").waitFor({ timeout: 120_000 });

  // 1. A few public links with some visits, then the one shown in the result card.
  const first = await shorten("https://en.wikipedia.org/wiki/URL_shortening");
  const second = await shorten("https://developer.mozilla.org/en-US/docs/Web/HTTP/Status/302");
  await visit(first, 5);
  await visit(second, 2);
  await shorten("https://github.com/privateMwb/Shrtn");

  // The list reloads after each creation; reload once more so the counts include the visits.
  await page.reload({ waitUntil: "domcontentloaded" });
  await page.getByText("wikipedia", { exact: false }).first().waitFor({ timeout: 20_000 });
  await shorten("https://example.com/articles/2026/08/a-very-long-path-that-needs-shortening");
  await page.waitForTimeout(1_000);
  await shot("shrtn.png");

  // 2. A private link: kept out of the shared list, remembered by this browser.
  await shorten("https://example.com/just-for-me", { isPrivate: true });
  await page.waitForTimeout(1_000);
  await shot("shrtn-private.png");

  // 3. A URL the browser refuses before it ever reaches the server.
  await page.getByPlaceholder("https://example.com/your-long-link").fill("javascript:alert(1)");
  await page.getByRole("button", { name: "Shorten", exact: true }).click();
  await page.getByRole("alert").waitFor({ timeout: 10_000 });
  await page.waitForTimeout(500);
  await shot("shrtn-error.png");

  // 4. Server unreachable: refuse the links request and reload.
  await page.route("**/links", (route) => route.abort());
  await page.route("**/api/links/*", (route) => route.abort());
  await page.reload({ waitUntil: "domcontentloaded" });
  await page.getByText("isn't responding").waitFor({ timeout: 20_000 });
  await page.waitForTimeout(500);
  await shot("shrtn-server-down.png");
} catch (error) {
  // Keep a picture of whatever the page showed, to debug from.
  await page.screenshot({ path: path.join(OUT, "_debug-failure.png"), fullPage: true });
  console.error(error);
  process.exitCode = 1;
} finally {
  await browser.close();
}
