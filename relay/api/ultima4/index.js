// Relay for ultima-assist's "Download it for me" button.
//
// Browsers can't fetch the Ultima IV zip directly because its host sends no
// CORS header.  This function fetches that one fixed URL server-side and
// returns it with a CORS header for the allowed sites.  It is deliberately
// narrow:
//   - it only ever requests UPSTREAM_URL (no URL comes from the caller);
//   - it only returns the bytes if their SHA-256 matches PINNED_SHA256, so a
//     changed or tampered upstream file is refused rather than passed on;
//   - it only adds CORS headers for ALLOWED_ORIGINS.
// Nothing is stored; a warm instance keeps the verified bytes in memory.

const crypto = require('crypto');

const UPSTREAM_URL = 'https://ultima.thatfleminggent.com/ultima4.zip';
const PINNED_SHA256 = '94aa748cfa1d0e7aa2e518abebb994f3c18acf7edb78c3bd37cd0a4404e6ba74';
const EXPECTED_BYTES = 529099;
const ALLOWED_ORIGINS = new Set([
  'https://pataltimore.github.io',
  'http://localhost:8090', 'http://localhost:8091', 'http://localhost:8092',
]);

let cached = null;     // verified bytes, kept while the instance is warm

async function load() {
  if (cached) return cached;
  const r = await fetch(UPSTREAM_URL, { redirect: 'follow' });
  if (!r.ok) throw new Error('upstream status ' + r.status);
  const buf = Buffer.from(await r.arrayBuffer());
  if (buf.length !== EXPECTED_BYTES) throw new Error('unexpected size ' + buf.length);
  const sum = crypto.createHash('sha256').update(buf).digest('hex');
  if (sum !== PINNED_SHA256) throw new Error('hash mismatch');
  cached = buf;
  return cached;
}

module.exports = async function (context, req) {
  const origin = req.headers && req.headers.origin;
  const headers = { 'Vary': 'Origin', 'X-Content-Type-Options': 'nosniff' };
  if (origin && ALLOWED_ORIGINS.has(origin)) headers['Access-Control-Allow-Origin'] = origin;

  try {
    const body = await load();
    context.res = {
      status: 200,
      isRaw: true,
      headers: Object.assign(headers, {
        'Content-Type': 'application/zip',
        'Content-Length': String(body.length),
        'Cache-Control': 'public, max-age=86400',
      }),
      body,
    };
  } catch (e) {
    context.log.error('relay failed: ' + e.message);
    context.res = {
      status: 502,
      headers: Object.assign(headers, { 'Content-Type': 'text/plain' }),
      body: 'The game file could not be fetched or did not verify. Please download it yourself instead.',
    };
  }
};
