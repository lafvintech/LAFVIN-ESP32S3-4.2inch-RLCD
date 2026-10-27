#pragma once
#include <Arduino.h>

// Browser page: single file, no external dependencies (the device AP has no internet, so no CDN resources).
// The page interacts with the device only through /api/* routes registered in gallery_app.cpp:
//   GET  /api/status            -> Device and album status
//   GET  /api/frame?id=N        -> 16-byte header + 15000-byte 1bpp bitmap
//   POST /api/upload            -> Multipart field name is fixed as frame
//   POST /api/select?id=N       -> Display photo N
//   POST /api/delete?id=N       -> Delete photo N
//   POST /api/slideshow?seconds= -> 0 / 10 / 30 / 60
//   POST /api/info              -> Return the display to the help page
// All pixel conversion (scaling, grayscale, dithering, packing, CRC) happens in the frontend; firmware transfers the bitmap,
// so the device needs neither JPEG decoding nor an additional image library.
static const char kGalleryHtml[] PROGMEM = R"GALLERY(<!DOCTYPE html>
<html lang="zh-CN">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,viewport-fit=cover">
<meta name="theme-color" content="#eeece7">
<title>RLCD 相册</title>
<style>
:root{color-scheme:light}
*{box-sizing:border-box}
body{margin:0;padding:12px 12px 30px;background:#eeece7;color:#111;
  font:15px/1.55 -apple-system,BlinkMacSystemFont,"Segoe UI","PingFang SC","Microsoft YaHei",sans-serif;
  -webkit-text-size-adjust:100%}
h1{margin:0 0 4px;font-size:19px;letter-spacing:.5px}
h2{margin:0 0 10px;font-size:16px}
code{font:13px/1 ui-monospace,Consolas,monospace;background:#fff;border:1px solid #ccc;padding:0 3px}
.card{margin:0 0 12px;padding:12px;background:#fbfaf7;border:1px solid #111;border-radius:2px}
.sub{color:#555;font-size:13px;margin:4px 0 0}
.chips{display:flex;flex-wrap:wrap;gap:6px;margin:0 0 10px}
.chip{padding:2px 9px;border:1px solid #999;border-radius:11px;font-size:12px;background:#fff}
.chip.on{border-color:#111;background:#111;color:#fff}
.row{display:flex;gap:8px;align-items:center;flex-wrap:wrap;margin:8px 0}
.row label.f{font-size:13px;color:#333;min-width:52px}
canvas#preview{display:block;width:100%;height:auto;background:#fff;border:1px solid #999}
.grid canvas{display:block;width:100%;height:auto;background:#fff;border:1px solid #bbb}
button{font:inherit;padding:9px 14px;min-height:42px;border:1px solid #111;background:#fff;color:#111;border-radius:2px;cursor:pointer}
button.primary{background:#111;color:#fff}
button:disabled{opacity:.4;cursor:default}
input[type=range]{flex:1;min-width:110px;height:32px}
input[type=file]{max-width:100%;font-size:13px}
select{font:inherit;padding:8px;min-height:42px;border:1px solid #999;background:#fff}
.grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(104px,1fr));gap:8px}
.grid>p.note{grid-column:1/-1;margin:0}
.item{border:1px solid #bbb;background:#fff;padding:4px;cursor:pointer}
.item.on{border:2px solid #111;padding:3px}
.item .cap{display:flex;justify-content:space-between;align-items:center;gap:4px;font-size:12px;margin-top:4px}
.item .cap button{min-height:28px;padding:1px 7px;font-size:12px}
.bar{height:8px;border:1px solid #999;background:#fff;margin-top:8px}
.bar>i{display:block;height:100%;width:0;background:#111}
#msg{display:none;margin:0 0 10px;padding:8px 10px;background:#fff;border:1px solid #111;
  font-size:13px;white-space:pre-wrap}
#msg.err{border-color:#a00;color:#a00}
.note{font-size:12px;color:#555}
</style>
</head>
<body>
<h1>RLCD 相册</h1>
<p class="sub" id="lead">正在读取设备状态…</p>
<div id="msg"></div>

<div class="card">
  <h2>① 上传照片</h2>
  <div class="row"><input type="file" id="file" accept="image/*"></div>
  <canvas id="preview" width="400" height="300"></canvas>
  <div class="row">
    <label class="f">效果</label>
    <select id="mode">
      <option value="dither">抖动（保留细节）</option>
      <option value="threshold">二值（对比强烈）</option>
    </select>
    <label class="f">适配</label>
    <select id="fit">
      <option value="cover">填充裁切</option>
      <option value="contain">完整留白</option>
    </select>
  </div>
  <div class="row"><label class="f">亮度</label><input type="range" id="bri" min="-100" max="100" value="0"><span class="sub" id="briv">0</span></div>
  <div class="row"><label class="f">对比度</label><input type="range" id="con" min="50" max="200" value="100"><span class="sub" id="conv">1.00</span></div>
  <div class="row">
    <button class="primary" id="submit" disabled>上传并显示</button>
    <span class="sub" id="previewInfo">未选择照片</span>
  </div>
  <div class="bar" id="barWrap" style="display:none"><i id="bar"></i></div>
  <p class="note">预览就是屏幕真正会显示的画面（400x300 黑白）。上传一次只发一张，成功后设备自动翻到新照片。</p>
</div>

<div class="card">
  <h2>② 屏幕与相册</h2>
  <div class="chips" id="chips"></div>
  <div class="row">
    <label class="f">幻灯片</label>
    <select id="slide">
      <option value="0">手动切换</option>
      <option value="10">每 10 秒</option>
      <option value="30">每 30 秒</option>
      <option value="60">每 60 秒</option>
    </select>
    <button id="info">显示帮助页</button>
    <button id="reload">重新读取</button>
  </div>
  <div class="grid" id="grid"></div>
  <p class="note" id="loadhint"></p>
</div>

<div class="card">
  <h2>使用说明</h2>
  <p class="sub">手机连上热点 <b id="ssid">RLCD-Gallery</b>，浏览器打开 <b id="url">http://192.168.4.1/</b>。
  这是设备自己的热点，没有外网，系统提示“无法访问互联网”时选择保持连接即可。</p>
  <p class="sub">设备按键：短按 BOOT 上一张，短按 KEY 下一张，长按 KEY 显示帮助页。</p>
  <p class="sub">照片存进 SD 卡的 <code>/gallery13</code>，文件名是 <code>P01.rlcd</code> 这样的 15016 字节数据包
  （16 字节头 + 15000 字节黑白位图），断电保留；
  没插卡或卡不可用时只剩 1 张临时照片，重启即丢失。</p>
</div>

<script src="/image.js"></script>
<script>
(() => {
  'use strict';
  const $ = (id) => document.getElementById(id);
  const msgBox = $('msg');
  function say(text, isError) {
    if (!text) { msgBox.style.display = 'none'; return; }
    msgBox.textContent = text;
    msgBox.className = isError ? 'err' : '';
    msgBox.style.display = 'block';
  }
  if (!globalThis.GalleryImage) { say('预览脚本 /image.js 加载失败，请刷新页面重试。', true); return; }

  const view = { status: null, listSig: '', viewSig: '' };
  const frames = new Map();               // "id:crc" -> 15000-byte bitmap
  const srcCanvas = document.createElement('canvas');
  srcCanvas.width = GalleryImage.width;
  srcCanvas.height = GalleryImage.height;
  const srcCtx = srcCanvas.getContext('2d');
  const scratchCanvas = document.createElement('canvas');
  scratchCanvas.width = GalleryImage.width;
  scratchCanvas.height = GalleryImage.height;
  const scratchCtx = scratchCanvas.getContext('2d');
  let sourceImage = null, previewFrame = null, previewPacket = null, previewQueued = false;
  let uploading = false, loadingThumbs = false, refreshing = false;

  const keyOf = (item) => item.id + ':' + item.crc;

  async function post(url) {
    const res = await fetch(url, { method: 'POST', cache: 'no-store' });
    let body = null;
    try { body = await res.json(); } catch (err) { body = null; }
    if (!res.ok || !body || body.ok !== true)
      throw new Error((body && body.error) || ('设备返回 HTTP ' + res.status));
    return body;
  }
  function chip(text, on) {
    const el = document.createElement('span');
    el.className = 'chip' + (on ? ' on' : '');
    el.textContent = text;
    return el;
  }
  function drawFrame(canvas, frame) {
    // pixels() returns RGBA bytes; putImageData requires an actual ImageData object.
    scratchCtx.putImageData(
      new ImageData(GalleryImage.pixels(frame), GalleryImage.width, GalleryImage.height), 0, 0);
    const ctx = canvas.getContext('2d');
    ctx.imageSmoothingEnabled = true;
    ctx.drawImage(scratchCanvas, 0, 0, canvas.width, canvas.height);
  }

  /* ---------------- Device status and album ---------------- */
  function renderStatus() {
    const s = view.status, items = s.items || [];
    $('ssid').textContent = s.ssid;
    $('url').textContent = 'http://' + s.ip + '/';
    $('lead').textContent = s.persistent
      ? 'SD 卡模式：最多 ' + s.capacity + ' 张照片，断电保留'
      : '临时模式：没有可用的 SD 卡，只能保存 1 张，断电丢失';
    const chips = $('chips');
    chips.innerHTML = '';
    chips.appendChild(chip('已存 ' + items.length + '/' + s.capacity));
    chips.appendChild(chip(s.helpVisible ? '屏幕：帮助页'
      : (s.selected ? '屏幕：第 ' + s.selected + ' 张' : '屏幕：空白')));
    chips.appendChild(chip('幻灯片：' + (s.slideshow ? s.slideshow + ' 秒' : '手动'), !!s.slideshow));
    if (!s.displayReady) chips.appendChild(chip('屏幕未就绪，请重启设备', true));
    $('slide').value = String(s.slideshow || 0);
  }
  function buildItem(item) {
    const box = document.createElement('div');
    box.className = 'item';
    box.dataset.id = item.id;
    const canvas = document.createElement('canvas');
    canvas.width = 200;
    canvas.height = 150;
    box.appendChild(canvas);
    const cap = document.createElement('div');
    cap.className = 'cap';
    const label = document.createElement('span');
    label.textContent = '#' + item.id + (item.valid ? '' : ' 损坏');
    cap.appendChild(label);
    const del = document.createElement('button');
    del.textContent = '删除';
    del.onclick = (ev) => { ev.stopPropagation(); removePhoto(item.id); };
    cap.appendChild(del);
    box.appendChild(cap);
    box.onclick = () => showPhoto(item.id);
    return box;
  }
  function thumbOf(id) {
    const box = $('grid').querySelector('.item[data-id="' + id + '"]');
    return box ? box.querySelector('canvas') : null;
  }
  function renderGallery() {
    const s = view.status, items = s.items || [];
    const listSig = items.map((i) => i.id + ':' + i.crc + ':' + (i.valid ? 1 : 0)).join(',');
    const viewSig = s.selected + '|' + (s.helpVisible ? 1 : 0);
    if (listSig !== view.listSig) {
      const grid = $('grid');
      grid.innerHTML = '';
      if (!items.length) {
        const empty = document.createElement('p');
        empty.className = 'note';
        empty.textContent = '还没有照片，用上面的按钮上传第一张。';
        grid.appendChild(empty);
      }
      for (const item of items) grid.appendChild(buildItem(item));
      view.listSig = listSig;
      view.viewSig = '';
      pruneFrames(items);
      for (const item of items) {
        const cached = frames.get(keyOf(item));
        const canvas = thumbOf(item.id);
        if (cached && canvas) drawFrame(canvas, cached);
      }
    }
    if (viewSig !== view.viewSig) {
      for (const item of items) {
        const box = $('grid').querySelector('.item[data-id="' + item.id + '"]');
        if (box) box.classList.toggle('on', !s.helpVisible && item.id === s.selected);
      }
      view.viewSig = viewSig;
    }
  }
  function pruneFrames(items) {
    const live = new Set(items.map(keyOf));
    for (const key of Array.from(frames.keys())) if (!live.has(key)) frames.delete(key);
  }
  async function loadThumbs() {
    if (loadingThumbs || !view.status) return;
    const items = view.status.items || [];
    const pending = items.filter((i) => i.valid && !frames.has(keyOf(i)));
    if (!pending.length) { $('loadhint').textContent = ''; return; }
    loadingThumbs = true;
    let done = 0;
    for (const item of pending) {
      $('loadhint').textContent = '正在读取缩略图 ' + (done + 1) + '/' + pending.length + '…';
      try {
        const res = await fetch('/api/frame?id=' + item.id, { cache: 'no-store' });
        if (!res.ok) throw new Error('HTTP ' + res.status);
        const frame = GalleryImage.decode(await res.arrayBuffer());
        frames.set(keyOf(item), frame);
        const canvas = thumbOf(item.id);
        if (canvas) drawFrame(canvas, frame);
      } catch (err) {
        const box = $('grid').querySelector('.item[data-id="' + item.id + '"]');
        if (box) box.querySelector('span').textContent = '#' + item.id + ' 读取失败';
      }
      done++;
    }
    loadingThumbs = false;
    $('loadhint').textContent = '';
  }
  async function refresh(quiet) {
    if (refreshing || uploading) return;
    refreshing = true;
    try {
      const res = await fetch('/api/status', { cache: 'no-store' });
      if (!res.ok) throw new Error('HTTP ' + res.status);
      const next = await res.json();
      const changed = !view.status ||
        JSON.stringify(next.items) !== JSON.stringify(view.status.items);
      view.status = next;
      renderStatus();
      renderGallery();
      if (!quiet) say('');
      if (changed || (next.items || []).some((i) => i.valid && !frames.has(keyOf(i)))) loadThumbs();
    } catch (err) {
      say('读取设备状态失败：' + err.message + '\n请确认手机仍连着热点，然后点“重新读取”。', true);
    }
    refreshing = false;
  }
  async function showPhoto(id) {
    if (uploading) return;
    try {
      await post('/api/select?id=' + id);
      await refresh(true);
      say('屏幕已切到第 ' + id + ' 张。');
    } catch (err) {
      say('切换失败：' + err.message, true);
    }
  }
  async function removePhoto(id) {
    if (uploading) return;
    if (!confirm('删除第 ' + id + ' 张照片？')) return;
    try {
      await post('/api/delete?id=' + id);
      view.listSig = '';
      await refresh(true);
      say('已删除第 ' + id + ' 张。');
    } catch (err) {
      say('删除失败：' + err.message, true);
    }
  }

  /* ---------------- Image selection and preview ---------------- */
  function loadSource(file) {
    if (typeof createImageBitmap === 'function') {
      // Downscale to 800 pixels wide first: phone images often exceed 4000 pixels; downsampling saves memory and improves clarity.
      return createImageBitmap(file, { resizeWidth: 800, resizeQuality: 'high' })
        .catch(() => createImageBitmap(file));
    }
    return new Promise((resolve, reject) => {
      const url = URL.createObjectURL(file);
      const img = new Image();
      img.onload = () => { URL.revokeObjectURL(url); resolve(img); };
      img.onerror = () => {
        URL.revokeObjectURL(url);
        reject(new Error('浏览器无法解码这张照片，请换 JPG 或 PNG 再试。'));
      };
      img.src = url;
    });
  }
  function updatePreview() {
    previewQueued = false;
    const ctx = $('preview').getContext('2d');
    if (!sourceImage) {
      ctx.fillStyle = '#ffffff';
      ctx.fillRect(0, 0, 400, 300);
      $('submit').disabled = true;
      $('previewInfo').textContent = '未选择照片';
      return;
    }
    srcCtx.fillStyle = '#ffffff';
    srcCtx.fillRect(0, 0, 400, 300);
    const cover = $('fit').value === 'cover';
    const scale = cover
      ? Math.max(400 / sourceImage.width, 300 / sourceImage.height)
      : Math.min(400 / sourceImage.width, 300 / sourceImage.height);
    const w = Math.max(1, Math.round(sourceImage.width * scale));
    const h = Math.max(1, Math.round(sourceImage.height * scale));
    srcCtx.imageSmoothingEnabled = true;
    srcCtx.imageSmoothingQuality = 'high';
    srcCtx.drawImage(sourceImage, Math.round((400 - w) / 2), Math.round((300 - h) / 2), w, h);
    const data = srcCtx.getImageData(0, 0, 400, 300);
    previewFrame = GalleryImage.monochrome(data.data, $('mode').value,
      Number($('bri').value), Number($('con').value) / 100);
    previewPacket = GalleryImage.packet(previewFrame);
    let ink = 0;
    for (let i = 0; i < previewFrame.length; i++) ink += bitCount(previewFrame[i]);
    ctx.putImageData(new ImageData(GalleryImage.pixels(previewFrame), 400, 300), 0, 0);
    $('submit').disabled = false;
    $('previewInfo').textContent = '400x300 黑白，黑色像素约 ' +
      Math.round(ink * 100 / (400 * 300)) + '%，CRC ' + GalleryImage.crc32(previewFrame).toString(16);
  }
  function bitCount(value) {
    let v = value, n = 0;
    while (v) { n += v & 1; v >>= 1; }
    return n;
  }
  function updatePreviewSoon() {
    if (previewQueued) return;
    previewQueued = true;
    requestAnimationFrame(updatePreview);
  }
  function setBar(ratio) {
    $('bar').style.width = Math.round(Math.min(1, Math.max(0, ratio)) * 100) + '%';
  }
  function upload() {
    if (!previewPacket || uploading) return;
    uploading = true;
    $('submit').disabled = true;
    $('barWrap').style.display = 'block';
    setBar(0);
    say('正在上传 15016 字节…');
    const form = new FormData();
    form.append('frame', new Blob([previewPacket], { type: 'application/octet-stream' }), 'frame.rlcd');
    const xhr = new XMLHttpRequest();
    xhr.open('POST', '/api/upload');
    xhr.upload.onprogress = (ev) => { if (ev.lengthComputable) setBar(ev.loaded / ev.total); };
    xhr.onload = async () => {
      let body = null;
      try { body = JSON.parse(xhr.responseText); } catch (err) { body = null; }
      uploading = false;
      $('submit').disabled = false;
      $('barWrap').style.display = 'none';
      if (xhr.status >= 200 && xhr.status < 300 && body && body.ok) {
        view.listSig = '';
        await refresh(true);
        say('上传成功，屏幕已经刷新。');
      } else {
        say('上传失败：' + ((body && body.error) || ('设备返回 HTTP ' + xhr.status)), true);
      }
    };
    xhr.onerror = () => {
      uploading = false;
      $('submit').disabled = false;
      $('barWrap').style.display = 'none';
      say('上传中断：手机与热点的连接可能断了，请重试。', true);
    };
    xhr.send(form);
  }

  /* ---------------- Event binding ---------------- */
  $('file').onchange = async (ev) => {
    const file = ev.target.files && ev.target.files[0];
    if (!file) return;
    say('正在读取照片…');
    try {
      sourceImage = await loadSource(file);
      say('');
      updatePreview();
    } catch (err) {
      sourceImage = null;
      updatePreview();
      say(err.message, true);
    }
  };
  $('mode').onchange = updatePreviewSoon;
  $('fit').onchange = updatePreviewSoon;
  $('bri').oninput = () => { $('briv').textContent = $('bri').value; updatePreviewSoon(); };
  $('con').oninput = () => {
    $('conv').textContent = (Number($('con').value) / 100).toFixed(2);
    updatePreviewSoon();
  };
  $('submit').onclick = upload;
  $('reload').onclick = () => { frames.clear(); view.listSig = ''; $('loadhint').textContent = ''; refresh(); };
  $('info').onclick = async () => {
    try { await post('/api/info'); await refresh(true); say('屏幕已回到帮助页。'); }
    catch (err) { say('操作失败：' + err.message, true); }
  };
  $('slide').onchange = async () => {
    try { await post('/api/slideshow?seconds=' + $('slide').value); await refresh(true); }
    catch (err) { say('设置幻灯片失败：' + err.message, true); }
  };
  document.addEventListener('visibilitychange', () => { if (!document.hidden) refresh(true); });
  setInterval(() => { if (!document.hidden) refresh(true); }, 4000);
  updatePreview();
  refresh();
})();
</script>
</body>
</html>
)GALLERY";
