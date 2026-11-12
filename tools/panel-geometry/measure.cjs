// Measures the panel mockup in Chrome: node measure.cjs <chrome.exe> [output folder]  (needs: npm i puppeteer-core)
const pp = require('puppeteer-core');
const path = require('path');
const fs = require('fs');
(async () => {
  const b = await pp.launch({ executablePath: process.argv[2], headless: 'new', args: ['--disable-gpu'] });
  const file = 'file:///' + path.resolve(__dirname, '..', '..', 'docs', 'panel-mockup.html').split(path.sep).join('/');
  const pg = await b.newPage();
  const errs = [];
  pg.on('pageerror', e => errs.push(e.message));
  await pg.setViewport({ width: 1500, height: 1100, deviceScaleFactor: 1 });
  await pg.goto(file, { waitUntil: 'networkidle0' });
  await pg.addStyleTag({ content: '.page{max-width:none!important}' });
  await pg.evaluate(() => window.dispatchEvent(new Event('resize')));
  await new Promise(r => setTimeout(r, 800));
  const geo = await pg.evaluate(() => {
    const panel = document.getElementById('panel');
    const pr = panel.getBoundingClientRect();
    const k = pr.width / 1426;
    const rd = n => Math.round(n * 100) / 100;
    const R = e => { const r = e.getBoundingClientRect(); return [rd((r.left - pr.left) / k), rd((r.top - pr.top) / k), rd(r.width / k), rd(r.height / k)]; };
    const vis = e => e && e.offsetParent !== null && e.getBoundingClientRect().width > 0;
    const out = { scale: k, width: 1426, height: panel.offsetHeight, sections: [], cells: [] };
    document.querySelectorAll('.sec').forEach(s => {
      const h = s.querySelector(':scope > h3');
      out.sections.push({ title: h.textContent, big: s.classList.contains('big'), frame: R(s), head: R(h) });
    });
    let envN = 0;
    document.querySelectorAll('.cell').forEach(c => {
      if (c.classList.contains('blank') || c.closest('.blank')) return;
      const ctl = c.querySelector(':scope > .ctl');
      if (!ctl) return;
      const env = c.querySelector('svg.env');
      let id = c.getAttribute('data-id');
      if (env) { id = ['env:fenv', 'env:vca'][envN++]; }
      if (!id) return;
      const rec = { id, cell: R(c), ctl: R(ctl) };
      const main = ctl.querySelector('svg.knob, svg.oct, svg.env, .win, .ledrow, .leds, .saw .btn, .btn');
      if (main) rec.main = R(main);
      const val = c.querySelector(':scope > .val');
      if (val && c.querySelector('svg.knob') && vis(val)) rec.val = R(val);
      const lab = c.querySelector(':scope > .lab');
      if (lab && vis(lab)) { rec.lab = R(lab); rec.labText = lab.textContent; }
      const items = [];
      c.querySelectorAll('.ledcol').forEach(i => items.push({ r: R(i), t: i.querySelector('span').textContent }));
      c.querySelectorAll('.led').forEach(i => items.push({ r: R(i), t: i.querySelector('span').textContent }));
      if (items.length) rec.items = items;
      if (c.querySelector('.anchor .dd')) rec.dd = true;
      if (c.closest('.blkgrid')) rec.blk = true;
      out.cells.push(rec);
    });
    return out;
  });
  geo.errors = errs;
  fs.writeFileSync(path.resolve(process.argv[3] || __dirname, 'geo.json'), JSON.stringify(geo, null, 1));
  const panel = await pg.$('#panel');
  await panel.screenshot({ path: path.resolve(process.argv[3] || __dirname, 'mockup.png') });
  console.log('sections', geo.sections.length, 'cells', geo.cells.length, 'height', geo.height, 'scale', geo.scale, 'errors', JSON.stringify(errs));
  await b.close();
})();
