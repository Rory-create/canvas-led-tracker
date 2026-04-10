// Generates two PNG files of the gradient DL monogram logo.
// No dependencies — uses only built-in Node.js (zlib, fs).
//
// Usage: node design/generate-logo.js
// Output:
//   design/logo-64.png   — 64×64  (favicon / icon size)
//   design/logo-512.png  — 512×512 (Stripe branding / general use)

'use strict';
const zlib = require('zlib');
const fs   = require('fs');
const path = require('path');

// ── PNG encoder ──────────────────────────────────────────────────────────────

function crc32(buf) {
  let c = 0xffffffff;
  for (let i = 0; i < buf.length; i++) {
    c ^= buf[i];
    for (let j = 0; j < 8; j++) c = (c & 1) ? (0xedb88320 ^ (c >>> 1)) : (c >>> 1);
  }
  return (c ^ 0xffffffff) >>> 0;
}

function pngChunk(type, data) {
  const len  = Buffer.alloc(4); len.writeUInt32BE(data.length, 0);
  const t    = Buffer.from(type, 'ascii');
  const crcV = Buffer.alloc(4); crcV.writeUInt32BE(crc32(Buffer.concat([t, data])), 0);
  return Buffer.concat([len, t, data, crcV]);
}

function writePNG(filename, size, pixels) {
  // IHDR
  const ihdr = Buffer.alloc(13);
  ihdr.writeUInt32BE(size, 0); ihdr.writeUInt32BE(size, 4);
  ihdr[8]=8; ihdr[9]=6; // 8-bit RGBA

  // Raw scanlines: 1 filter byte + 4 bytes/pixel per row
  const raw = Buffer.alloc((size * 4 + 1) * size);
  for (let y = 0; y < size; y++) {
    raw[y * (size * 4 + 1)] = 0; // filter: None
    for (let x = 0; x < size; x++) {
      const s = (y * size + x) * 4;
      const d = y * (size * 4 + 1) + 1 + x * 4;
      raw[d]=pixels[s]; raw[d+1]=pixels[s+1]; raw[d+2]=pixels[s+2]; raw[d+3]=pixels[s+3];
    }
  }

  const out = Buffer.concat([
    Buffer.from([137,80,78,71,13,10,26,10]), // PNG signature
    pngChunk('IHDR', ihdr),
    pngChunk('IDAT', zlib.deflateSync(raw, { level: 9 })),
    pngChunk('IEND', Buffer.alloc(0)),
  ]);
  fs.writeFileSync(filename, out);
  console.log(`wrote ${filename} (${size}×${size})`);
}

// ── Pixel buffer primitives ───────────────────────────────────────────────────

function makeBuffer(size) { return new Uint8Array(size * size * 4); }

function parseColor(c) {
  if (c[0] === '#') {
    return [parseInt(c.slice(1,3),16), parseInt(c.slice(3,5),16), parseInt(c.slice(5,7),16), 255];
  }
  const m = c.match(/rgba?\((\d+),\s*(\d+),\s*(\d+)(?:,\s*([\d.]+))?\)/);
  return [+m[1], +m[2], +m[3], m[4] !== undefined ? Math.round(+m[4]*255) : 255];
}

function blend(buf, size, x, y, r, g, b, a) {
  if (x < 0 || x >= size || y < 0 || y >= size) return;
  const i = (y * size + x) * 4;
  if (a === 255) { buf[i]=r; buf[i+1]=g; buf[i+2]=b; buf[i+3]=255; return; }
  const sa = a/255, da = buf[i+3]/255, oa = sa + da*(1-sa);
  if (oa === 0) return;
  buf[i]   = Math.round((r*sa + buf[i]  *da*(1-sa))/oa);
  buf[i+1] = Math.round((g*sa + buf[i+1]*da*(1-sa))/oa);
  buf[i+2] = Math.round((b*sa + buf[i+2]*da*(1-sa))/oa);
  buf[i+3] = Math.round(oa*255);
}

function px(buf, size, x, y, color) {
  const [r,g,b,a] = parseColor(color);
  blend(buf, size, Math.floor(x), Math.floor(y), r, g, b, a);
}

function circle(buf, size, cx, cy, r, color) {
  const [R,G,B,A] = parseColor(color);
  for (let y = Math.ceil(cy-r); y <= Math.floor(cy+r); y++)
    for (let x = Math.ceil(cx-r); x <= Math.floor(cx+r); x++) {
      const dx=x-cx+0.5, dy=y-cy+0.5;
      if (dx*dx+dy*dy <= r*r) blend(buf, size, x, y, R, G, B, A);
    }
}

function fillRect(buf, size, x, y, w, h, color) {
  const [R,G,B,A] = parseColor(color);
  for (let dy=0; dy<h; dy++)
    for (let dx=0; dx<w; dx++)
      blend(buf, size, x+dx, y+dy, R, G, B, A);
}

// ── Logo drawing ──────────────────────────────────────────────────────────────

function lerpHex(a, b, t) {
  const p = (s,i) => parseInt(s.slice(i,i+2),16);
  const r=Math.round(p(a,1)+(p(b,1)-p(a,1))*t);
  const g=Math.round(p(a,3)+(p(b,3)-p(a,3))*t);
  const bl=Math.round(p(a,5)+(p(b,5)-p(a,5))*t);
  return `rgb(${r},${g},${bl})`;
}
function ledColor(row, maxRow) {
  const t = row / maxRow;
  return t < 0.5 ? lerpHex('#22c55e','#eab308',t*2) : lerpHex('#eab308','#ef4444',(t-0.5)*2);
}

const GLYPH_D = [[1,1,1,1,0],[1,0,0,0,1],[1,0,0,0,1],[1,0,0,0,1],[1,0,0,0,1],[1,0,0,0,1],[1,1,1,1,0]];
const GLYPH_L = [[1,0,0,0,0],[1,0,0,0,0],[1,0,0,0,0],[1,0,0,0,0],[1,0,0,0,0],[1,0,0,0,0],[1,1,1,1,1]];

function drawLogo(buf, vsize) {
  const ps=3, gW=5*ps, gH=7*ps, gapX=6;
  const ox = Math.floor((vsize - (gW+gapX+gW)) / 2);
  const oy = Math.floor((vsize - gH) / 2) + 1;
  const lx = ox + gW + gapX;
  const dotCx = ox + gW + gapX/2 - 0.5;
  const dotCy = oy + gH/2 - 0.5;
  const rows  = GLYPH_D.length - 1;

  // Gradient letters
  for (let gy=0; gy<GLYPH_D.length; gy++) {
    const col = ledColor(gy, rows);
    for (let gx=0; gx<GLYPH_D[gy].length; gx++)
      if (GLYPH_D[gy][gx]) fillRect(buf, vsize, ox+gx*ps, oy+gy*ps, ps, ps, col);
    for (let gx=0; gx<GLYPH_L[gy].length; gx++)
      if (GLYPH_L[gy][gx]) fillRect(buf, vsize, lx+gx*ps, oy+gy*ps, ps, ps, col);
  }

  // 3 stacked LED dots: green · yellow · red
  const spacing = 7;
  const leds = [
    ['#22c55e', 'rgba(34,197,94,0.35)',  -spacing],
    ['#eab308', 'rgba(234,179,8,0.35)',   0],
    ['#ef4444', 'rgba(239,68,68,0.35)',   spacing],
  ];
  leds.forEach(([col, glow, dy]) => {
    circle(buf, vsize, dotCx, dotCy+dy, 2.5, glow);
    circle(buf, vsize, dotCx, dotCy+dy, 1.5, col);
    px(buf, vsize, Math.floor(dotCx), Math.floor(dotCy+dy)-1, '#ffffff');
  });
}

// Scale a vsize×vsize buffer up to (vsize*scale)×(vsize*scale)
function scaleUp(src, vsize, scale) {
  const dst = new Uint8Array(vsize*scale * vsize*scale * 4);
  for (let y=0; y<vsize; y++)
    for (let x=0; x<vsize; x++) {
      const si = (y*vsize+x)*4;
      const [r,g,b,a] = [src[si],src[si+1],src[si+2],src[si+3]];
      for (let dy=0; dy<scale; dy++)
        for (let dx=0; dx<scale; dx++) {
          const di = ((y*scale+dy)*(vsize*scale)+(x*scale+dx))*4;
          dst[di]=r; dst[di+1]=g; dst[di+2]=b; dst[di+3]=a;
        }
    }
  return dst;
}

// ── Generate ──────────────────────────────────────────────────────────────────

const VSIZE = 64;
const dir   = path.join(__dirname);

// 64×64 — draw at native virtual resolution (1:1)
const buf64 = makeBuffer(VSIZE);
drawLogo(buf64, VSIZE);
writePNG(path.join(dir, 'logo-64.png'), VSIZE, buf64);

// 512×512 — scale up 8×
const buf512 = scaleUp(buf64, VSIZE, 8);
writePNG(path.join(dir, 'logo-512.png'), VSIZE*8, buf512);
