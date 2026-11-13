import {createServer} from 'node:http';
import {readFile} from 'node:fs/promises';
import {resolve, join} from 'node:path';
import {pathToFileURL} from 'node:url';
import assert from 'node:assert/strict';

const root = resolve(new URL('../../', import.meta.url).pathname.replace(/^\/([A-Za-z]:)/, '$1'));
const artifact = resolve(process.env.MINVEX_WEB_DIR ?? join(root, 'out/build/wasm-release/web'));
const {chromium, firefox} = await import(process.env.MINVEX_PLAYWRIGHT_MODULE ? pathToFileURL(process.env.MINVEX_PLAYWRIGHT_MODULE).href : 'playwright');
const ribbon = JSON.parse(await readFile(join(root, 'examples/assets/MorphRibbon.gltf'), 'utf8'));
ribbon.scenes[0].nodes = ribbon.scenes[0].nodes.filter(node => node !== 4);
const external = structuredClone(ribbon);
const binary = Buffer.from(external.buffers[0].uri.split(',')[1], 'base64');
external.buffers[0].uri = '../shared/ribbon%20data.bin';
const virtual = new Map([
  ['/fixtures/morph.gltf', Buffer.from(JSON.stringify(ribbon))],
  ['/fixtures/nested/model.gltf', Buffer.from(JSON.stringify(external))],
  ['/fixtures/shared/ribbon data.bin', binary],
]);
const server = createServer(async (request, response) => {
  try {
    const path = decodeURIComponent(new URL(request.url, 'http://local').pathname);
    if (path === '/harness') {response.setHeader('Content-Type', 'text/html');response.end('<canvas id="test" style="width:256px;height:256px" width="256" height="256"></canvas>');return;}
    if (path === '/slow.glb') {await new Promise(done => setTimeout(done, 200));}
    let data = virtual.get(path), file;
    if (!data) {
      const base = path.startsWith('/assets/') || path === '/slow.glb' ? join(root, 'examples/assets') : artifact;
      file = resolve(base, path === '/slow.glb' ? 'SharedBox.glb' : path.replace(/^\/assets\//, '').replace(/^\//, ''));
      if (!file.startsWith(base + '/') && !file.startsWith(base + '\\')) throw Error('outside root');
      data = await readFile(file);
    }
    response.setHeader('Content-Type', path.endsWith('.wasm') ? 'application/wasm' : path.endsWith('.js') ? 'text/javascript' : path.endsWith('.html') ? 'text/html' : 'application/octet-stream');
    response.end(data);
  } catch {response.writeHead(404);response.end('missing');}
});
await new Promise(done => server.listen(0, '127.0.0.1', done));
const url = `http://127.0.0.1:${server.address().port}`;
const requested = process.env.MINVEX_BROWSERS?.split(',') ?? ['chromium'];
try {
  for (const name of requested) {
    const browser = name === 'firefox' ? await firefox.launch({headless: true, firefoxUserPrefs: {'webgl.force-enabled': true, 'gfx.webrender.software': true}}) : await chromium.launch({headless: true, channel: name === 'edge' ? 'msedge' : name === 'chromium' ? undefined : 'chrome', args: ['--use-angle=swiftshader', '--enable-unsafe-swiftshader', '--disable-gpu-sandbox']});
    try {
      const page = await browser.newPage();const errors = [];
      page.on('pageerror', error => errors.push(error.message));
      page.on('console', message => {if ((message.type()==='error' || message.text().includes('INVALID_')) && !message.text().includes('404')) console.error(message.text());});
      await page.goto(`${url}/harness`);
      await page.evaluate(async () => {
        const {createViewer, MinvexError} = await import('/minvex.js');
        const check = (condition, message) => {if (!condition) throw Error(message);};
        const expect = async (code, work) => {try {await work();} catch (error) {check(error instanceof MinvexError && error.code === code, `Expected ${code}: ${error.code} ${error.message}`);return;}throw Error(`Missing ${code}`);};
        const canvas = document.querySelector('canvas');
        window.viewer = await createViewer({canvas, autoStart: false, quality: {preset:'medium', shadow:'off', targetFps:0, pbr:true}});
        window.check = check;window.expect = expect;
        const gl = canvas.getContext('webgl2');check(gl, 'WebGL2 context');
        window.pixels = () => {viewer.render();const data=new Uint8Array(canvas.width*canvas.height*4);gl.readPixels(0,0,canvas.width,canvas.height,gl.RGBA,gl.UNSIGNED_BYTE,data);const code=gl.getError();check(code===gl.NO_ERROR,`WebGL readback error ${code.toString(16)}`);return data;};
        window.changed = (a,b) => {let n=0;for(let i=0;i<a.length;i+=4) if(Math.abs(a[i]-b[i])+Math.abs(a[i+1]-b[i+1])+Math.abs(a[i+2]-b[i+2])>12) n++;return n;};
        check(!crossOriginIsolated, 'Single-thread build must work without COOP/COEP');
        await expect('InvalidState',()=>createViewer({canvas}));
      });
      const core = await page.evaluate(async () => {
        const base = pixels();
        const box = await viewer.loadModel('/assets/SharedBox.glb');
        const image = pixels();check(changed(base,image)>100,'GLB did not produce Canvas pixels');
        const quality = viewer.quality;check(quality.backend==='WebGL2' && quality.pbr && quality.maxSkinJoints===48,'Web capability contract');
        box.setMaterial({metallic:.2,roughness:.3,baseColor:[.9,.2,.1,1]});check(changed(image,pixels())>50,'PBR override did not affect pixels');
        const materialPixels=pixels();
        for (const index of [.5,4294967296,-4294967296]) await expect('InvalidArgument',()=>box.setMaterial({metallic:1},index));
        check(changed(materialPixels,pixels())===0,'Rejected material index mutated another material');
        const beforeCamera=pixels();viewer.setCamera({position:[3,2,4],target:[0,0,0]});check(changed(beforeCamera,pixels())>50,'Camera edit did not affect pixels');
        const beforeWheel=pixels();document.querySelector('canvas').dispatchEvent(new WheelEvent('wheel',{deltaY:100,cancelable:true}));check(changed(beforeWheel,pixels())>10,'Canvas wheel control');
        await expect('UnsupportedFeature',()=>viewer.setQuality({preset:'medium',shadow:'pcss',allowFallback:false}));
        check(viewer.quality.shadow===0,'Rejected quality modified applied state');
        await expect('InvalidArgument',()=>viewer.setQuality({unknown:true}));
        for (const value of [{targetFps:1.5},{msaaSamples:1.9},{targetFps:4294967296}]) await expect('InvalidArgument',()=>viewer.setQuality(value));
        viewer.setQuality({preset:'high',allowFallback:true});check(viewer.quality.shadow===0 && !viewer.quality.postProcessing && viewer.quality.adjustments.length>=2,'Web High fallback did not report disabled native features');
        viewer.setQuality({preset:'medium',shadow:'off',targetFps:0,pbr:true});box.remove();
        await expect('InvalidHandle',()=>box.info);
        window.model = await viewer.loadModel('/fixtures/nested/model.gltf');
        model.playAnimation('Bend');model.seekAnimation(0);const start=pixels();model.seekAnimation(.5);check(changed(start,pixels())>15,'GPU Skinning did not animate');
        model.pauseAnimation();model.stopAnimation();const noMorph=pixels();model.setMorphWeights(3,[.8,.2]);check(changed(noMorph,pixels())>15,'CPU Morph did not affect WebGL geometry');
        for (const node of [3.5,4294967299,-4294967293]) {
          await expect('InvalidArgument',()=>model.setMorphWeights(node,[0,0]));
          await expect('InvalidArgument',()=>model.clearMorphWeights(node));
        }
        check(Math.abs(model.info.morphNodes[0].weights[0]-.8)<.001,'Rejected node index mutated a valid Morph node');
        for (const clip of [.5,4294967296,-4294967296]) await expect('InvalidArgument',()=>model.playAnimation(clip));
        const clone=model.clone();model.setTransform({position:[-1.3,0,0]});clone.setTransform({position:[1.3,0,0]});clone.setMorphWeights(3,[0,0]);viewer.setCamera({position:[0,1,6],target:[0,1,0],orthographicHeight:4});const paired=pixels();
        model.setMorphWeights(3,[.1,0]);const isolated=pixels();check(changed(paired,isolated)>10,'Shared model Morph update had no effect');check(clone.info.morphNodes[0].weights[0]===0,'Instance Morph state leaked');
        let independent=true;for(let y=0;y<256;y++)for(let x=128;x<256;x++)for(let c=0;c<4;c++)independent&&=paired[(y*256+x)*4+c]===isolated[(y*256+x)*4+c];check(independent,'Independent instance pixels changed');
        clone.remove();model.setTransform({position:[0,0,0]});
        model.playBlendTree({samples:[{clip:0,x:0},{clip:1,x:1}]});model.setBlendParameter('Speed',.25);model.seekBlendTree(.5);check(Math.abs(model.info.blendWeights[0]-.75)<.001,'Blend Tree weights');pixels();
        model.setAdditiveAnimation(1,.5);model.seekAdditive(.5);pixels();model.clearAdditiveAnimation();
        model.playAnimation(0);model.setAnimationEvents(0,[{time:.2,name:'step',payload:'왼발'}]);model.seekAnimation(.4,true);viewer.render();const events=viewer.pollAnimationEvents();check(events.length===1 && events[0].payload==='왼발','Animation polling event');
        await expect('InvalidArgument',()=>model.setMorphWeights(3,[NaN,0]));
        await expect('AssetLoad',()=>viewer.loadModel('/assets/missing.glb'));
        await expect('InvalidData',()=>viewer.loadBuffer(new Uint8Array([0,1,2]),{name:'bad.glb'}));
        await expect('InsufficientResource',()=>viewer.loadBuffer(new ArrayBuffer(64*1024*1024+1)));
        const gltfDocument=await (await fetch('/fixtures/nested/model.gltf')).json();await expect('AssetLoad',()=>viewer.loadBuffer(new TextEncoder().encode(JSON.stringify(gltfDocument)),{name:'missing.gltf'}));
        return {pixelChecks:true,events:events.length};
      });
      const lifecycle = await page.evaluate(async () => {
        const json=await (await fetch('/fixtures/morph.gltf')).arrayBuffer();const file=new File([json],'사용자.gltf');const input=document.createElement('input');input.type='file';input.multiple=true;const transfer=new DataTransfer();transfer.items.add(file);input.files=transfer.files;
        const fromFile=await viewer.loadFiles(input.files);check(fromFile.info.clips.length>0,'Browser File Input');fromFile.remove();
        class Unreadable extends File {async arrayBuffer(){throw new DOMException('read failed','NotReadableError');}}
        await expect('AssetLoad',()=>viewer.loadFiles([new Unreadable([''],'unreadable.gltf')]));
        model.seekAnimation(0);viewer.start();await new Promise(done=>setTimeout(done,100));viewer.stop();check(model.info.time>0,'Browser frame loop did not advance animation');const stoppedTime=model.info.time;await new Promise(done=>setTimeout(done,40));check(model.info.time===stoppedTime,'stop did not cancel animation frames');
        viewer.start();
        const extension=document.querySelector('canvas').getContext('webgl2').getExtension('WEBGL_lose_context');check(extension,'Context loss extension');
        await new Promise(done=>{document.querySelector('canvas').addEventListener('webglcontextlost',()=>setTimeout(done,20),{once:true});extension.loseContext();});
        await expect('InvalidState',()=>viewer.render());
        viewer.stop();const lostTime=model.info.time;
        const lostError=document.querySelector('canvas').getContext('webgl2').getError();check(lostError===0x9242 || lostError===0,'Unexpected context-loss error');
        await new Promise(done=>{document.querySelector('canvas').addEventListener('webglcontextrestored',()=>setTimeout(done,20),{once:true});extension.restoreContext();});
        check(model.info.clips.length>0 && pixels().length>0,'Context restoration lost CPU state or rendering');
        await new Promise(done=>setTimeout(done,40));check(model.info.time===lostTime,'Explicit stop during context loss was not preserved');
        check(viewer.lastError===null,'Expected context loss became an asynchronous rendering error');
        const deferred=await (await fetch('/fixtures/nested/model.gltf')).json();deferred.images=[...(deferred.images??[]),{uri:'must-not-resolve.png'}];
        let release,resolveCalls=0;
        const resolving=viewer.loadBuffer(new TextEncoder().encode(JSON.stringify(deferred)),{name:'cancel.gltf',resolve:()=>++resolveCalls===1 ? new Promise(done=>{release=done;}):new Uint8Array()});
        let releaseFile;class Deferred extends File {arrayBuffer(){return new Promise(done=>{releaseFile=done;});}}
        const reading=viewer.loadFiles([new Deferred([''],'pending.gltf')]);
        const pending=viewer.loadModel('/slow.glb');viewer.dispose();release(new Uint8Array());releaseFile(json);
        await expect('InvalidState',()=>resolving);check(resolveCalls===1,'Disposed load started another dependency resolver');
        await expect('InvalidState',()=>reading);
        await expect('InvalidState',()=>pending);await expect('InvalidState',()=>model.info);viewer.dispose();
        const {createViewer}=await import('/minvex.js');
        let callbacks=0;window.next=await createViewer({canvas:document.querySelector('canvas'),autoStart:false,onEvent:()=>{if(callbacks++===0)throw new Error('callback detail');throw 0;}});
        const animated=await next.loadModel('/fixtures/morph.gltf');animated.playAnimation(0);animated.setAnimationEvents(0,[{time:.2,name:'step'}]);animated.seekAnimation(.4,true);
        try {next.render();throw Error('Missing callback failure');}catch(error){check(error.code==='CallbackFailure' && error.cause.message==='callback detail','JS callback cause');}
        animated.seekAnimation(0);animated.seekAnimation(.4,true);try {next.render();throw Error('Missing primitive callback failure');}catch(error){check(error.code==='CallbackFailure' && error.cause===0,'Primitive callback cause');}
        next.dispose();return {fileInput:true,contextRestored:true,disposed:true};
      });
      assert.equal(errors.length,0,errors.join('\n'));
      console.log(JSON.stringify({browser:name,...core,...lifecycle}));
    } finally {await browser.close();}
  }
} finally {await new Promise(done=>server.close(done));}
