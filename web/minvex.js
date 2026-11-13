import createModule from './minvex-native.js';

export class MinvexError extends Error {
  constructor(code, message, details = {}) {
    super(message, Object.hasOwn(details, 'cause') ? {cause: details.cause} : undefined);
    this.name = 'MinvexError'; this.code = code;
    this.hint = details.hint ?? ''; this.sourcePath = details.sourcePath ?? '';
  }
}
const failure = (code, message, details) => new MinvexError(code, message, details);
const finite = n => typeof n === 'number' && Number.isFinite(n);
let canvasSequence = 0;
const activeCanvases = new WeakSet();
const maximumFileBytes = 64 * 1024 * 1024, maximumImportBytes = 128 * 1024 * 1024;
function bytesOf(value) {
  if (!(value instanceof ArrayBuffer) && !ArrayBuffer.isView(value))
    throw failure('InvalidArgument', 'Asset data requires an ArrayBuffer or typed array.');
  if (value.byteLength > maximumFileBytes) throw failure('InsufficientResource', 'Asset file exceeds the size limit.');
  return value instanceof ArrayBuffer ? new Uint8Array(value).slice() : new Uint8Array(value.buffer, value.byteOffset, value.byteLength).slice();
}
function resourceUri(uri) {
  let decoded;
  try { decoded = decodeURIComponent(uri); } catch { throw failure('InvalidData', 'Invalid resource URI escape.'); }
  if (!decoded || /[\0:#?\\]/u.test(decoded) || decoded.startsWith('/'))
    throw failure('UnsupportedFeature', 'Resource URIs must be relative paths or base64 data URIs.');
  return decoded;
}
function joinPath(base, path) {
  const result = [...base];
  for (const part of path.split('/')) {
    if (!part || part === '.') continue;
    if (part === '..') { if (!result.length) throw failure('InvalidData', 'Resource path escapes the import sandbox.'); result.pop(); }
    else result.push(part);
  }
  return result.join('/');
}
function modelJson(bytes, name) {
  let jsonBytes = bytes;
  if (bytes.length >= 12 && new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength).getUint32(0, true) === 0x46546c67) {
    const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
    if (view.getUint32(4, true) !== 2 || view.getUint32(8, true) !== bytes.length || bytes.length < 20 || view.getUint32(16, true) !== 0x4e4f534a)
      throw failure('InvalidData', 'Invalid GLB header or JSON chunk.');
    const length = view.getUint32(12, true);
    if (length > bytes.length - 20) throw failure('InvalidData', 'GLB JSON chunk exceeds the input.');
    jsonBytes = bytes.subarray(20, 20 + length);
  } else if (!name.toLowerCase().endsWith('.gltf')) throw failure('InvalidData', 'Expected a GLB or glTF asset.');
  try { return JSON.parse(new TextDecoder('utf-8', {fatal: true}).decode(jsonBytes)); }
  catch (cause) { throw failure('InvalidData', 'Cannot parse the glTF JSON.', {cause}); }
}
function jsonString(value) {
  return JSON.stringify(value, (_key, item) => {
    if (typeof item === 'number' && !Number.isFinite(item)) throw failure('InvalidArgument', 'Numbers must be finite.');
    return item;
  });
}

class ModelObject {
  #owner;
  constructor(viewer, id) { this.#owner = new WeakRef(viewer); this.id = id; Object.freeze(this); }
  #viewer() {
    const viewer = this.#owner.deref();
    if (!viewer || viewer.disposed) throw failure('InvalidState', 'The owning Viewer is disposed.');
    return viewer;
  }
  #call(op, values = {}) { return this.#viewer().command({...values, op, id: this.id}); }
  get info() { return this.#call('info'); }
  clone() { const viewer = this.#viewer(); return new ModelObject(viewer, this.#call('clone').id); }
  remove() { return this.#call('remove'); }
  setTransform(value) { return this.#call('transform', value); }
  setMaterial(value, index = 0) { return this.#call('material', {value, index}); }
  playAnimation(clip) { return this.#call('play', {clip}); }
  pauseAnimation() { return this.#call('pause'); }
  stopAnimation() { return this.#call('stop'); }
  seekAnimation(time, collect = false) { return this.#call('seek', {time, collect}); }
  setAnimationSpeed(speed) { return this.#call('speed', {speed}); }
  setMorphWeights(node, weights) { return this.#call('morph', {node, weights: [...weights]}); }
  clearMorphWeights(node) { return this.#call('clearMorph', {node}); }
  playBlendTree(description) { return this.#call('tree', description); }
  setBlendParameter(name, value) { return this.#call('parameter', {name, value}); }
  seekBlendTree(phase) { return this.#call('seekTree', {phase}); }
  setAdditiveAnimation(clip, weight = 1, mask = []) { return this.#call('additive', {clip, weight, mask}); }
  setAdditiveWeight(weight) { return this.#call('additiveWeight', {weight}); }
  seekAdditive(time) { return this.#call('seekAdditive', {time}); }
  clearAdditiveAnimation() { return this.#call('clearAdditive'); }
  setAnimationEvents(clip, events) { return this.#call('events', {clip, events}); }
}

class Viewer {
  #module; #host; #canvas; #frame = 0; #last = 0; #running = false; #lost = false; #resume = false;
  #loads = new Set(); #loadSequence = 0; #listeners = []; #events = []; #observer; #onEvent; #onError;
  #disposed = false;
  get disposed() { return this.#disposed; }
  lastError = null;
  constructor(module, host, canvas, options) {
    this.#module = module; this.#host = host; this.#canvas = canvas;
    this.#onEvent = options.onEvent; this.#onError = options.onError;
    const listen = (name, handler, settings) => { canvas.addEventListener(name, handler, settings); this.#listeners.push(() => canvas.removeEventListener(name, handler, settings)); };
    listen('webglcontextlost', event => {
      event.preventDefault(); this.#handleLoss();
    });
    listen('webglcontextrestored', () => {
      try { this.command({op: 'restore'}); this.#lost = false; this.render(); if (this.#resume) this.start(); }
      catch (error) { this.#report(error); }
    });
    let previous = null;
    listen('contextmenu', event => event.preventDefault());
    listen('pointerdown', event => { previous = [event.clientX, event.clientY]; canvas.setPointerCapture(event.pointerId); this.command({op: 'input', type: 'down', button: event.button === 2 ? 1 : 0}); });
    listen('pointerup', event => { previous = null; this.command({op: 'input', type: 'up', button: event.button === 2 ? 1 : 0}); });
    listen('lostpointercapture', () => { previous = null; if (!this.disposed) this.command({op: 'input', type: 'reset'}); });
    listen('pointermove', event => { if (!previous) return; this.command({op: 'input', type: 'move', dx: event.clientX - previous[0], dy: event.clientY - previous[1]}); previous = [event.clientX, event.clientY]; });
    listen('wheel', event => { event.preventDefault(); this.command({op: 'input', type: 'wheel', scroll: -event.deltaY / 100}); }, {passive: false});
    this.#observer = new ResizeObserver(() => { if (!this.disposed && !this.#lost) {try {this.render();} catch (error) {this.#report(error);}} });
    this.#observer.observe(canvas);
  }
  command(request) {
    if (this.disposed) throw failure('InvalidState', 'Viewer is disposed.');
    const result = JSON.parse(this.#host.command(jsonString(request)));
    if (result.error) throw failure(result.error.code, result.error.message, result.error);
    return result.value;
  }
  #extent() {
    // CSS-pixel output keeps Low/Medium costs bounded and avoids intrinsic-size/DPR feedback.
    const style = getComputedStyle(this.#canvas);
    const contentWidth = this.#canvas.clientWidth - parseFloat(style.paddingLeft) - parseFloat(style.paddingRight);
    const contentHeight = this.#canvas.clientHeight - parseFloat(style.paddingTop) - parseFloat(style.paddingBottom);
    const width = Math.max(1, Math.min(8192, Math.floor(contentWidth || this.#canvas.width || 1)));
    const height = Math.max(1, Math.min(8192, Math.floor(contentHeight || this.#canvas.height || 1)));
    if (this.#canvas.width !== width) this.#canvas.width = width;
    if (this.#canvas.height !== height) this.#canvas.height = height;
    return {width, height};
  }
  get quality() { return this.command({op: 'getQuality'}); }
  setQuality(value) { return this.command({op: 'quality', value}); }
  setCamera(value) { return this.command({...value, op: 'camera'}); }
  frameScene() { return this.command({op: 'frame'}); }
  #handleLoss() {
    if (this.#lost || this.disposed) return;
    this.#resume = this.#running; this.#pauseLoop(); this.#lost = true;
    try { this.command({op: 'suspend'}); } catch (error) { this.#report(error); }
  }
  render(deltaSeconds = 0, force = true) {
    if (this.disposed) throw failure('InvalidState', 'Viewer is disposed.');
    if (this.#canvas.getContext('webgl2').isContextLost()) this.#handleLoss();
    if (this.#lost) throw failure('InvalidState', 'WebGL context is lost.');
    if (!finite(deltaSeconds) || deltaSeconds < 0 || deltaSeconds > 1) throw failure('InvalidArgument', 'Frame delta must be in [0,1].');
    const result = this.command({op: 'tick', dt: deltaSeconds, force, ...this.#extent()});
    if (this.#events.length + result.events.length > 1024) { this.stop(); throw failure('InsufficientResource', 'Animation event queue is full; poll events or provide onEvent.'); }
    this.#events.push(...result.events);
    if (this.#onEvent) for (const event of this.pollAnimationEvents()) {
      try { this.#onEvent(event); } catch (cause) { this.stop(); throw failure('CallbackFailure', 'Animation event callback failed.', {cause}); }
      if (this.disposed) break;
    }
    return result;
  }
  pollAnimationEvents() { if (this.disposed) throw failure('InvalidState', 'Viewer is disposed.'); return this.#events.splice(0); }
  #report(error) {
    this.lastError = error; this.stop();
    if (this.#onError) this.#onError(error);
    else this.#canvas.dispatchEvent(new CustomEvent('minvexerror', {detail: error}));
  }
  start() {
    if (this.disposed || this.#lost) throw failure('InvalidState', 'Viewer is disposed or its context is lost.');
    if (this.#running) return;
    this.#running = true; this.#last = performance.now();
    const frame = now => {
      if (!this.#running || this.disposed) return;
      const delta = Math.min(.25, Math.max(0, (now - this.#last) / 1000)); this.#last = now;
      try { this.render(delta, false); } catch (error) { if (this.#canvas.getContext('webgl2').isContextLost()) this.#handleLoss(); if (!this.#lost) this.#report(error); return; }
      if (this.#running && !this.disposed) this.#frame = requestAnimationFrame(frame);
    };
    this.#frame = requestAnimationFrame(frame);
  }
  #pauseLoop() { this.#running = false; cancelAnimationFrame(this.#frame); }
  stop() { this.#resume = false; this.#pauseLoop(); }
  async loadModel(url, options = {}) {
    return this.#loading(async signal => {
      const absolute = new URL(url, document.baseURI);
      const fetchBytes = async resource => {
        const response = await fetch(resource, {signal});
        if (!response.ok) throw failure('AssetLoad', `Asset request failed: ${response.status}`, {sourcePath: String(resource)});
        const size = Number(response.headers.get('content-length'));
        if (size > maximumFileBytes) throw failure('InsufficientResource', 'Asset file exceeds the size limit.');
        const reader = response.body.getReader(), chunks = []; let total = 0;
        try {
          while (true) { const {done, value} = await reader.read(); if (done) break; total += value.length;
            if (total > maximumFileBytes) { await reader.cancel(); throw failure('InsufficientResource', 'Asset file exceeds the size limit.'); } chunks.push(value); }
        } finally { reader.releaseLock(); }
        const bytes = new Uint8Array(total); let offset = 0;
        for (const chunk of chunks) { bytes.set(chunk, offset); offset += chunk.length; } return bytes;
      };
      const bytes = await fetchBytes(absolute);
      return this.#import(bytes, options.name ?? absolute.pathname.split('/').pop(), async uri => options.resolve ? options.resolve(uri, new URL(uri, absolute), signal) : fetchBytes(new URL(uri, absolute)), signal);
    });
  }
  async loadBuffer(data, options = {}) {
    return this.#loading(signal => this.#buffer(data, options, signal));
  }
  #buffer(data, options, signal) {
    if (this.disposed || signal.aborted) throw failure('InvalidState', 'Viewer is disposed.');
    const bytes = bytesOf(data), files = options.files ?? {};
    return this.#import(bytes, options.name ?? 'model.glb', async uri => {
      const decoded = resourceUri(uri);
      const item = files instanceof Map ? (files.get(uri) ?? files.get(decoded)) : (Object.hasOwn(files, uri) ? files[uri] : Object.hasOwn(files, decoded) ? files[decoded] : undefined);
      if (item !== undefined) return item;
      if (options.resolve) return options.resolve(uri, undefined, signal);
      throw failure('AssetLoad', `Missing dependency: ${uri}`, {sourcePath: uri});
    }, signal);
  }
  async loadFiles(fileList, options = {}) {
    return this.#loading(async signal => {
      const files = [...fileList], candidates = files.filter(file => /\.(glb|gltf)$/i.test(file.name));
      const entry = options.entry ? files.find(file => (file.webkitRelativePath || file.name) === options.entry || file.name === options.entry) : candidates.length === 1 ? candidates[0] : null;
      if (!entry) throw failure('InvalidArgument', 'Select one GLB/glTF entry or specify entry.');
      if (entry.size > maximumFileBytes) throw failure('InsufficientResource', 'Browser file exceeds the size limit.');
      const entryPath = entry.webkitRelativePath || entry.name;
      const data = await entry.arrayBuffer(), resources = new Map();
      if (this.disposed || signal.aborted) throw failure('InvalidState', 'Viewer is disposed.');
      const base = entryPath.split('/').slice(0, -1);
      for (const file of files) {
        if (file.size > maximumFileBytes) throw failure('InsufficientResource', 'Browser file exceeds the size limit.');
        resources.set(file.webkitRelativePath || file.name, file);
      }
      return this.#buffer(data, {...options, name: entry.name, resolve: async uri => {
        const decoded = resourceUri(uri);let key;
        try { key = joinPath(base, decoded); } catch (error) {if (error.code !== 'InvalidData') throw error;}
        let file = resources.get(key);
        if (!file) { const matches = files.filter(candidate => candidate.name === decoded.split('/').pop()); if (matches.length === 1) file = matches[0]; }
        if (!file) throw failure('AssetLoad', `Missing browser file: ${uri}`);
        return file.arrayBuffer();
      }}, signal);
    });
  }
  async #loading(work) {
    if (this.disposed) throw failure('InvalidState', 'Viewer is disposed.');
    const controller = new AbortController(); this.#loads.add(controller);
    try { return await work(controller.signal); }
    catch (cause) {
      if (this.disposed || controller.signal.aborted) throw failure('InvalidState', 'Load cancelled because the Viewer was disposed.');
      if (cause instanceof MinvexError) throw cause;
      throw failure('AssetLoad', cause?.message || 'Asset loading failed.', {cause});
    } finally { this.#loads.delete(controller); }
  }
  async #import(bytes, name, resolve, signal) {
    const live = () => {if (this.disposed || signal.aborted) throw failure('InvalidState', 'Viewer is disposed.');};
    live();
    if (bytes.length > maximumFileBytes) throw failure('InsufficientResource', 'Asset file exceeds the size limit.');
    if (!/\.(glb|gltf)$/i.test(name) || /[\\/\0]/u.test(name)) throw failure('InvalidArgument', 'Use a GLB/glTF file name without path separators.');
    const json = modelJson(bytes, name);
    const uris = [...new Set([...(json.buffers ?? []), ...(json.images ?? [])].map(item => item.uri).filter(uri => typeof uri === 'string' && !uri.startsWith('data:')))];
    let total = bytes.length;
    // A bounded private directory allows parent-relative glTF paths without escaping the sandbox.
    const base = Array(16).fill('r'), files = [[joinPath(base, name), bytes]];
    for (const uri of uris) {
      live();
      const decoded = resourceUri(uri), path = joinPath(base, decoded), resource = await resolve(uri);
      live();
      // Check the cumulative budget before allocating another owned copy.
      if (resource?.byteLength > maximumImportBytes - total) throw failure('InsufficientResource', 'Asset dependency budget exceeded.');
      const data = bytesOf(resource);
      total += data.length;
      if (data.length > maximumFileBytes || total > maximumImportBytes) throw failure('InsufficientResource', 'Asset dependency budget exceeded.');
      files.push([path, data]);
    }
    live();
    const root = `/minvex/load${++this.#loadSequence}`;
    const fs = this.#module.FS;
    const remove = path => { for (const child of fs.readdir(path).filter(n => n !== '.' && n !== '..')) { const p = `${path}/${child}`; if (fs.isDir(fs.stat(p).mode)) remove(p); else fs.unlink(p); } fs.rmdir(path); };
    try {
      for (const [path, data] of files) { const full = `${root}/${path}`; fs.mkdirTree(full.slice(0, full.lastIndexOf('/'))); fs.writeFile(full, data); }
      const result = this.command({op: 'load', path: `${root}/${joinPath(base, name)}`});
      return new ModelObject(this, result.id);
    } finally { if (fs.analyzePath(root).exists) remove(root); }
  }
  dispose() {
    if (this.disposed) return;
    this.stop(); this.#observer.disconnect();
    for (const remove of this.#listeners) remove(); this.#listeners = [];
    for (const controller of this.#loads) controller.abort(); this.#loads.clear();
    try { this.command({op: 'dispose'}); } finally { this.#host.delete(); this.#host = null; this.#module = null; this.#events = []; this.#disposed = true; activeCanvases.delete(this.#canvas); }
  }
}

export async function createViewer(options = {}) {
  const canvas = options.canvas;
  if (!(canvas instanceof HTMLCanvasElement)) throw failure('InvalidArgument', 'Provide an HTML Canvas.');
  if (activeCanvases.has(canvas)) throw failure('InvalidState', 'This Canvas already owns a Viewer.');
  if ((options.onEvent && typeof options.onEvent !== 'function') || (options.onError && typeof options.onError !== 'function')) throw failure('InvalidArgument', 'Callbacks must be functions.');
  if (!canvas.id) canvas.id = `minvex-canvas-${++canvasSequence}`;
  activeCanvases.add(canvas);
  // Each module owns one Sokol context, keeping multiple Canvas instances independent.
  let module, host, viewer;
  try {
    module = await createModule(options.moduleOptions ?? {}); host = new module.WebHost();
    const result = JSON.parse(host.command(jsonString({op: 'initialize', selector: `#${CSS.escape(canvas.id)}`, width: Math.max(1, canvas.width), height: Math.max(1, canvas.height), quality: options.quality ?? 'low'})));
    if (result.error) throw failure(result.error.code, result.error.message, result.error);
    viewer = new Viewer(module, host, canvas, options); viewer.render();
    if (options.autoStart !== false) viewer.start();
    return viewer;
  } catch (cause) {
    activeCanvases.delete(canvas);
    if (viewer) viewer.dispose(); else if (host) host.delete();
    if (cause instanceof MinvexError) throw cause;
    throw failure('UnsupportedFeature', 'Cannot initialize the WebAssembly/WebGL2 runtime.', {cause});
  }
}
