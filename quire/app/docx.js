/* Quire documents: Word (.docx) export and import, Markdown and plain-text import.
 *
 * A .docx is a zip of XML files. This file carries a small zip writer (stored, no compression)
 * and reader (stored or deflate, inflated with the browser's DecompressionStream), so Quire
 * needs no libraries and keeps working offline.
 *
 * Imports produce { title, chapters: [{ title, scenes: [{ title, html }] }] } where html uses
 * Quire's subset: <p>, <em>, <strong>.
 */
(function (root, factory) {
  const api = factory();
  if (typeof module === 'object' && module.exports) module.exports = api;
  else root.QuireDocs = api;
})(typeof self !== 'undefined' ? self : this, () => {
  'use strict';

  /* ---------------- zip ---------------- */
  const CRC = (() => {
    const t = new Uint32Array(256);
    for (let n = 0; n < 256; n++) { let c = n; for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1; t[n] = c >>> 0; }
    return t;
  })();
  function crc32(bytes) {
    let c = 0xffffffff;
    for (let i = 0; i < bytes.length; i++) c = CRC[(c ^ bytes[i]) & 0xff] ^ (c >>> 8);
    return (c ^ 0xffffffff) >>> 0;
  }
  const enc = new TextEncoder();

  /** files: [{ name, data: string | Uint8Array }] -> Uint8Array (zip, stored) */
  function zip(files) {
    const d = new Date();
    const time = (d.getHours() << 11) | (d.getMinutes() << 5) | (d.getSeconds() >> 1);
    const date = ((d.getFullYear() - 1980) << 9) | ((d.getMonth() + 1) << 5) | d.getDate();
    const locals = [], centrals = [];
    let offset = 0;
    for (const f of files) {
      const name = enc.encode(f.name);
      const data = typeof f.data === 'string' ? enc.encode(f.data) : f.data;
      const crc = crc32(data);
      const lh = new DataView(new ArrayBuffer(30));
      lh.setUint32(0, 0x04034b50, true); lh.setUint16(4, 20, true); lh.setUint16(6, 0x0800, true); lh.setUint16(8, 0, true);
      lh.setUint16(10, time, true); lh.setUint16(12, date, true); lh.setUint32(14, crc, true);
      lh.setUint32(18, data.length, true); lh.setUint32(22, data.length, true); lh.setUint16(26, name.length, true); lh.setUint16(28, 0, true);
      locals.push(new Uint8Array(lh.buffer), name, data);
      const ch = new DataView(new ArrayBuffer(46));
      ch.setUint32(0, 0x02014b50, true); ch.setUint16(4, 20, true); ch.setUint16(6, 20, true); ch.setUint16(8, 0x0800, true); ch.setUint16(10, 0, true);
      ch.setUint16(12, time, true); ch.setUint16(14, date, true); ch.setUint32(16, crc, true);
      ch.setUint32(20, data.length, true); ch.setUint32(24, data.length, true); ch.setUint16(28, name.length, true);
      ch.setUint32(42, offset, true);
      centrals.push(new Uint8Array(ch.buffer), name);
      offset += 30 + name.length + data.length;
    }
    const cdSize = centrals.reduce((a, b) => a + b.length, 0);
    const end = new DataView(new ArrayBuffer(22));
    end.setUint32(0, 0x06054b50, true); end.setUint16(8, files.length, true); end.setUint16(10, files.length, true);
    end.setUint32(12, cdSize, true); end.setUint32(16, offset, true);
    const parts = [...locals, ...centrals, new Uint8Array(end.buffer)];
    const out = new Uint8Array(parts.reduce((a, b) => a + b.length, 0));
    let p = 0;
    for (const part of parts) { out.set(part, p); p += part.length; }
    return out;
  }

  async function inflateRaw(bytes) {
    const stream = new Blob([bytes]).stream().pipeThrough(new DecompressionStream('deflate-raw'));
    return new Uint8Array(await new Response(stream).arrayBuffer());
  }

  /** Uint8Array (zip) -> Map name -> Uint8Array */
  async function unzip(bytes) {
    const v = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
    let eocd = -1;
    for (let i = bytes.length - 22; i >= Math.max(0, bytes.length - 65557); i--) if (v.getUint32(i, true) === 0x06054b50) { eocd = i; break; }
    if (eocd < 0) throw new Error('Not a zip file');
    const count = v.getUint16(eocd + 10, true);
    let p = v.getUint32(eocd + 16, true);
    const out = new Map();
    const dec = new TextDecoder();
    for (let i = 0; i < count; i++) {
      if (v.getUint32(p, true) !== 0x02014b50) throw new Error('Damaged zip file');
      const method = v.getUint16(p + 10, true);
      const csize = v.getUint32(p + 20, true);
      const nlen = v.getUint16(p + 28, true), xlen = v.getUint16(p + 30, true), clen = v.getUint16(p + 32, true);
      const lho = v.getUint32(p + 42, true);
      const name = dec.decode(bytes.subarray(p + 46, p + 46 + nlen));
      const start = lho + 30 + v.getUint16(lho + 26, true) + v.getUint16(lho + 28, true);
      const raw = bytes.subarray(start, start + csize);
      if (method === 0) out.set(name, raw);
      else if (method === 8) out.set(name, await inflateRaw(raw));
      p += 46 + nlen + xlen + clen;
    }
    return out;
  }

  /* ---------------- shared ---------------- */
  const escXML = (s) => String(s).replace(/[&<>"]/g, (c) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' }[c]))
    .replace(/[\u0000-\u0008\u000b\u000c\u000e-\u001f]/g, '');
  const escHTML = (s) => String(s).replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;');
  const BREAK_RE = /^\s*(#|\*\s*\*\s*\*|\* \* \*|-\s*-\s*-|~+|⁂|◆|§)\s*$/;

  /** Collects paragraphs into chapters and scenes while importing. */
  function builder(defaultTitle) {
    const chapters = [];
    let ch = null, sc = null;
    const api = {
      chapter(title) { ch = { title: title || 'Untitled chapter', scenes: [] }; chapters.push(ch); sc = null; },
      scene(title) { if (!ch) api.chapter(defaultTitle); sc = { title: title || '', paras: [] }; ch.scenes.push(sc); },
      brk() { if (sc && sc.paras.length) sc = null; },
      para(html) {
        if (!html.trim()) return;
        if (!ch) api.chapter(defaultTitle);
        if (!sc) api.scene('');
        sc.paras.push(`<p>${html}</p>`);
      },
      done() {
        return chapters.map((c) => ({
          title: c.title,
          scenes: c.scenes.filter((s) => s.paras.length || s.title).map((s) => ({ title: s.title, html: s.paras.join('') })),
        })).filter((c) => c.scenes.length || c.title);
      },
    };
    return api;
  }

  /* ---------------- Markdown and plain text ---------------- */
  function mdInline(s) {
    return escHTML(s)
      .replace(/\*\*(.+?)\*\*|__(.+?)__/g, (_, a, b) => `<strong>${a || b}</strong>`)
      .replace(/(^|[^\w*])\*(?!\s)(.+?)\*(?!\w)/g, (_, pre, t) => `${pre}<em>${t}</em>`)
      .replace(/(^|[^\w])_(?!\s)(.+?)_(?!\w)/g, (_, pre, t) => `${pre}<em>${t}</em>`)
      .replace(/\\([*_#\\])/g, '$1');
  }
  function importText(text, name, markdown) {
    const b = builder(name || 'Imported');
    const lines = text.replace(/\r\n?/g, '\n').split('\n');
    // Hard-wrapped text (blank lines between paragraphs) joins its lines; otherwise one line = one paragraph.
    const blankSeparated = markdown || /\n[ \t]*\n/.test(text) && lines.filter((l) => l.trim()).length > 2 * (text.match(/\n[ \t]*\n/g) || []).length;
    let buf = [];
    const flush = () => { if (buf.length) { const t = buf.join(' ').trim(); b.para(markdown ? mdInline(t) : escHTML(t)); buf = []; } };
    let title = null;
    // One "# Title" at the top with "##" sections below: # is the book title, ## chapters, ### scenes.
    // Otherwise: # chapters, ## and deeper scenes.
    const h1 = lines.filter((l) => /^#\s+/.test(l)).length;
    const titleMode = markdown && h1 === 1 && /^#\s+/.test(lines.find((l) => l.trim()) || '') && lines.some((l) => /^##\s+/.test(l));
    for (const raw of lines) {
      const line = raw.replace(/\s+$/, '');
      const hm = markdown && line.match(/^(#{1,6})\s+(.*)$/);
      if (hm) {
        flush();
        const level = hm[1].length - (titleMode ? 1 : 0), t = hm[2].replace(/\s+#+$/, '').trim();
        if (level === 0) title = t;
        else if (level === 1) b.chapter(t);
        else b.scene(t);
        continue;
      }
      if (!markdown && /^\s*(chapter|part|prologue|epilogue)\b.{0,60}$/i.test(line) && line.trim().length < 70) { flush(); b.chapter(line.trim()); continue; }
      if (BREAK_RE.test(line)) { flush(); b.brk(); continue; }
      if (!line.trim()) { flush(); continue; }
      if (blankSeparated) buf.push(line.trim()); else { buf.push(line.trim()); flush(); }
    }
    flush();
    return { title, chapters: b.done() };
  }

  /* ---------------- Word import ---------------- */
  const W = 'http://schemas.openxmlformats.org/wordprocessingml/2006/main';
  const on = (el) => { if (!el) return false; const val = el.getAttributeNS(W, 'val') || el.getAttribute('w:val'); return !val || !/^(0|false|off|none)$/i.test(val); };
  const kid = (el, name) => { for (const c of el.childNodes) if (c.localName === name) return c; return null; };

  async function importDocx(bytes, name) {
    const files = await unzip(bytes);
    const docXml = files.get('word/document.xml');
    if (!docXml) throw new Error('This doesn’t look like a Word document.');
    const dec = new TextDecoder();
    const doc = new DOMParser().parseFromString(dec.decode(docXml), 'application/xml');
    // Map style ids to names so "Heading 1" is recognised whatever the id is.
    const styleNames = {};
    if (files.get('word/styles.xml')) {
      const st = new DOMParser().parseFromString(dec.decode(files.get('word/styles.xml')), 'application/xml');
      for (const s of st.getElementsByTagNameNS(W, 'style')) {
        const nm = kid(s, 'name');
        styleNames[s.getAttributeNS(W, 'styleId') || s.getAttribute('w:styleId')] = nm ? (nm.getAttributeNS(W, 'val') || nm.getAttribute('w:val')) : '';
      }
    }
    let title = null;
    const core = files.get('docProps/core.xml');
    if (core) { const m = dec.decode(core).match(/<dc:title>([^<]*)<\/dc:title>/); if (m && m[1].trim()) title = m[1].trim(); }

    const body = doc.getElementsByTagNameNS(W, 'body')[0];
    const paras = [...(body ? body.getElementsByTagNameNS(W, 'p') : [])].map((p) => {
      const pPr = kid(p, 'pPr');
      const sid = pPr && kid(pPr, 'pStyle') ? (kid(pPr, 'pStyle').getAttributeNS(W, 'val') || kid(pPr, 'pStyle').getAttribute('w:val')) : '';
      const lvlEl = pPr && kid(pPr, 'outlineLvl');
      return { p, style: `${sid} ${styleNames[sid] || ''}`.toLowerCase(), lvl: lvlEl ? +(lvlEl.getAttributeNS(W, 'val') || lvlEl.getAttribute('w:val')) : -1 };
    });
    const isChapter = (x) => /heading ?1|heading1/.test(x.style) || x.lvl === 0;
    // Text before the first chapter heading (a title page, a dedication) is kept apart as front matter.
    const b = builder(paras.some(isChapter) ? 'Front matter' : name || 'Imported');
    for (const x of paras) {
      const { p, style, lvl } = x;
      if (/contact|byline|subtitle/.test(style)) continue;
      let html = '', text = '';
      for (const r of p.getElementsByTagNameNS(W, 'r')) {
        const rPr = kid(r, 'rPr');
        const em = rPr && on(kid(rPr, 'i')), strong = rPr && on(kid(rPr, 'b'));
        let t = '';
        for (const c of r.childNodes) {
          if (c.localName === 't') t += c.textContent;
          else if (c.localName === 'tab') t += ' ';
          else if (c.localName === 'noBreakHyphen') t += '‑';
        }
        if (!t) continue;
        text += t;
        let s = escHTML(t);
        if (em) s = `<em>${s}</em>`;
        if (strong) s = `<strong>${s}</strong>`;
        html += s;
      }
      html = html.replace(/<\/em><em>/g, '').replace(/<\/strong><strong>/g, '');
      if (/(^|\s)title(\s|$)/.test(style) && text.trim()) { if (title == null) title = text.trim(); continue; }
      if (isChapter(x)) { if (text.trim()) b.chapter(text.trim()); continue; }
      if (/heading ?[2-3]|heading[2-3]/.test(style) || lvl === 1 || lvl === 2) { if (text.trim()) b.scene(text.trim()); continue; }
      if (BREAK_RE.test(text)) { b.brk(); continue; }
      if (!b.done().length && /^\s*(chapter|part|prologue|epilogue)\b/i.test(text) && text.length < 70) { b.chapter(text.trim()); continue; }
      b.para(html);
    }
    return { title, chapters: b.done() };
  }

  /* ---------------- Word export ---------------- */
  /** Paragraph list from Quire HTML: [[{ text, em, strong }]] */
  function runsOf(html) {
    const doc = new DOMParser().parseFromString(`<body>${html}</body>`, 'text/html');
    const paras = [];
    for (const p of doc.body.children) {
      const runs = [];
      const walk = (n, m) => {
        for (const c of n.childNodes) {
          if (c.nodeType === 3) { if (c.data) runs.push({ text: c.data, ...m }); } else if (c.nodeType === 1) {
            walk(c, { em: m.em || c.tagName === 'EM', strong: m.strong || c.tagName === 'STRONG' });
          }
        }
      };
      walk(p, { em: false, strong: false });
      if (runs.some((r) => r.text.trim())) paras.push(runs);
    }
    return paras;
  }
  const run = (r) => `<w:r>${r.em || r.strong ? `<w:rPr>${r.strong ? '<w:b/>' : ''}${r.em ? '<w:i/>' : ''}</w:rPr>` : ''}<w:t xml:space="preserve">${escXML(r.text)}</w:t></w:r>`;
  const para = (style, runs) => `<w:p><w:pPr><w:pStyle w:val="${style}"/></w:pPr>${runs.map(run).join('')}</w:p>`;

  /** Standard manuscript format: Times New Roman 12, double spaced, 1" margins, chapters on new pages. */
  function exportDocx(project, { countWords } = {}) {
    const m = project.meta || {};
    const body = [];
    const words = countWords ? countWords() : 0;
    const approx = words >= 1000 ? `about ${(Math.round(words / 1000) * 1000).toLocaleString()} words` : words ? `about ${(Math.round(words / 100) * 100 || words).toLocaleString()} words` : '';
    body.push(para('Contact', [{ text: m.author || '' }]));
    if (approx) body.push(para('Contact', [{ text: approx }]));
    body.push(para('Title', [{ text: m.title || 'Untitled' }]));
    if (m.author) body.push(para('Byline', [{ text: `by ${m.author}` }]));
    for (const ch of project.manuscript || []) {
      body.push(para('Heading1', [{ text: ch.title || '' }]));
      let first = true;
      (ch.children || []).forEach((s, i) => {
        const ps = runsOf(s.content || '');
        if (!ps.length) return;
        if (i > 0 && !first) body.push(para('SceneBreak', [{ text: '#' }]));
        ps.forEach((runs, j) => body.push(para(j === 0 ? 'FirstPara' : 'Body', runs)));
        first = false;
      });
    }
    const document = `<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<w:document xmlns:w="${W}"><w:body>${body.join('')}<w:sectPr><w:pgSz w:w="12240" w:h="15840"/><w:pgMar w:top="1440" w:right="1440" w:bottom="1440" w:left="1440" w:header="720" w:footer="720" w:gutter="0"/></w:sectPr></w:body></w:document>`;
    const style = (id, name, ppr, rpr = '', extra = '') => `<w:style w:type="paragraph" w:styleId="${id}"><w:name w:val="${name}"/><w:basedOn w:val="Normal"/>${extra}<w:qFormat/><w:pPr>${ppr}</w:pPr>${rpr ? `<w:rPr>${rpr}</w:rPr>` : ''}</w:style>`;
    const styles = `<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<w:styles xmlns:w="${W}"><w:docDefaults><w:rPrDefault><w:rPr><w:rFonts w:ascii="Times New Roman" w:hAnsi="Times New Roman" w:eastAsia="Times New Roman" w:cs="Times New Roman"/><w:sz w:val="24"/><w:szCs w:val="24"/><w:lang w:val="en-US"/></w:rPr></w:rPrDefault><w:pPrDefault><w:pPr><w:spacing w:after="0" w:line="480" w:lineRule="auto"/></w:pPr></w:pPrDefault></w:docDefaults>
<w:style w:type="paragraph" w:default="1" w:styleId="Normal"><w:name w:val="Normal"/><w:qFormat/></w:style>
${style('Body', 'Body Text', '<w:ind w:firstLine="720"/>')}
${style('FirstPara', 'First Paragraph', '<w:ind w:firstLine="0"/>')}
${style('Contact', 'Contact', '<w:spacing w:line="240" w:lineRule="auto"/>')}
${style('Title', 'Title', '<w:spacing w:before="4320" w:after="240"/><w:jc w:val="center"/>', '<w:sz w:val="36"/><w:szCs w:val="36"/>')}
${style('Byline', 'Byline', '<w:jc w:val="center"/>')}
${style('Heading1', 'heading 1', '<w:keepNext/><w:pageBreakBefore/><w:spacing w:before="2880" w:after="480"/><w:jc w:val="center"/><w:outlineLvl w:val="0"/>', '<w:sz w:val="28"/><w:szCs w:val="28"/>', '<w:next w:val="FirstPara"/>')}
${style('SceneBreak', 'Scene Break', '<w:jc w:val="center"/>', '', '<w:next w:val="FirstPara"/>')}
</w:styles>`;
    const now = new Date().toISOString().replace(/\.\d+Z$/, 'Z');
    return zip([
      { name: '[Content_Types].xml', data: '<?xml version="1.0" encoding="UTF-8" standalone="yes"?><Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types"><Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/><Default Extension="xml" ContentType="application/xml"/><Override PartName="/word/document.xml" ContentType="application/vnd.openxmlformats-officedocument.wordprocessingml.document.main+xml"/><Override PartName="/word/styles.xml" ContentType="application/vnd.openxmlformats-officedocument.wordprocessingml.styles+xml"/><Override PartName="/docProps/core.xml" ContentType="application/vnd.openxmlformats-package.core-properties+xml"/></Types>' },
      { name: '_rels/.rels', data: '<?xml version="1.0" encoding="UTF-8" standalone="yes"?><Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships"><Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument" Target="word/document.xml"/><Relationship Id="rId2" Type="http://schemas.openxmlformats.org/package/2006/relationships/metadata/core-properties" Target="docProps/core.xml"/></Relationships>' },
      { name: 'word/_rels/document.xml.rels', data: '<?xml version="1.0" encoding="UTF-8" standalone="yes"?><Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships"><Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/styles" Target="styles.xml"/></Relationships>' },
      { name: 'word/document.xml', data: document },
      { name: 'word/styles.xml', data: styles },
      { name: 'docProps/core.xml', data: `<?xml version="1.0" encoding="UTF-8" standalone="yes"?><cp:coreProperties xmlns:cp="http://schemas.openxmlformats.org/package/2006/metadata/core-properties" xmlns:dc="http://purl.org/dc/elements/1.1/" xmlns:dcterms="http://purl.org/dc/terms/" xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance"><dc:title>${escXML(m.title || '')}</dc:title><dc:creator>${escXML(m.author || '')}</dc:creator><dcterms:created xsi:type="dcterms:W3CDTF">${now}</dcterms:created><dcterms:modified xsi:type="dcterms:W3CDTF">${now}</dcterms:modified></cp:coreProperties>` },
    ]);
  }

  return { zip, unzip, crc32, importText, importDocx, exportDocx };
});
