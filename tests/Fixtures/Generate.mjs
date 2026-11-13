// Regenerate the small, original test assets with `node tests/Fixtures/Generate.mjs`.
// Node is not needed to build or run Minvex; generated fixtures are committed.
import { writeFileSync, mkdirSync } from 'node:fs';
import { deflateSync } from 'node:zlib';
import { dirname, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const directory = dirname(fileURLToPath(import.meta.url));
const output = (name, data) => writeFileSync(resolve(directory, name), data);
const crc32 = (bytes) => {
  let crc = 0xffffffff;
  for (const b of bytes) {
    crc ^= b;
    for (let k = 0; k < 8; ++k) crc = (crc >>> 1) ^ (crc & 1 ? 0xedb88320 : 0);
  }
  return (crc ^ 0xffffffff) >>> 0;
};
const chunk = (name, data) => {
  const kind = Buffer.from(name);
  const length = Buffer.alloc(4); length.writeUInt32BE(data.length);
  const crc = Buffer.alloc(4); crc.writeUInt32BE(crc32(Buffer.concat([kind, data])));
  return Buffer.concat([length, kind, data, crc]);
};
const ihdr = Buffer.alloc(13);
ihdr.writeUInt32BE(2, 0); ihdr.writeUInt32BE(2, 4); ihdr[8] = 8; ihdr[9] = 6;
const png = Buffer.concat([
  Buffer.from([137,80,78,71,13,10,26,10]), chunk('IHDR', ihdr),
  chunk('IDAT', deflateSync(Buffer.from([0, 255,255,255,255, 30,160,220,255, 0, 250,140,40,255, 255,255,255,255]))),
  chunk('IEND', Buffer.alloc(0)),
]);
const faces = [
  [[[-.5,-.5,.5],[.5,-.5,.5],[.5,.5,.5],[-.5,.5,.5]],[0,0,1]],
  [[[.5,-.5,-.5],[-.5,-.5,-.5],[-.5,.5,-.5],[.5,.5,-.5]],[0,0,-1]],
  [[[.5,-.5,.5],[.5,-.5,-.5],[.5,.5,-.5],[.5,.5,.5]],[1,0,0]],
  [[[-.5,-.5,-.5],[-.5,-.5,.5],[-.5,.5,.5],[-.5,.5,-.5]],[-1,0,0]],
  [[[-.5,.5,.5],[.5,.5,.5],[.5,.5,-.5],[-.5,.5,-.5]],[0,1,0]],
  [[[-.5,-.5,-.5],[.5,-.5,-.5],[.5,-.5,.5],[-.5,-.5,.5]],[0,-1,0]],
];
const geometry = Buffer.alloc(24 * 36 + 36 * 2);
const uvs = [[0,1],[1,1],[1,0],[0,0]];
let v = 0, t = 0;
for (const [positions, normal] of faces) {
  for (let k = 0; k < 4; ++k) {
    const offset = v++ * 36;
    [...positions[k], ...normal, ...uvs[k]].forEach((value, j) => geometry.writeFloatLE(value, offset + j * 4));
    geometry.set([255, 255, 255, 255], offset + 32);
  }
  for (const index of [0,1,2,0,2,3]) geometry.writeUInt16LE(v - 4 + index, 864 + t++ * 2);
}
const model = {
  asset: { version: '2.0', generator: 'Minvex original fixture generator' },
  buffers: [{ byteLength: geometry.length, uri: `data:application/octet-stream;base64,${geometry.toString('base64')}` }],
  bufferViews: [{ buffer: 0, byteOffset: 0, byteLength: 864, byteStride: 36 }, { buffer: 0, byteOffset: 864, byteLength: 72 }],
  accessors: [
    { bufferView: 0, byteOffset: 0, componentType: 5126, count: 24, type: 'VEC3', min: [-.5,-.5,-.5], max: [.5,.5,.5] },
    { bufferView: 0, byteOffset: 12, componentType: 5126, count: 24, type: 'VEC3' },
    { bufferView: 0, byteOffset: 24, componentType: 5126, count: 24, type: 'VEC2' },
    { bufferView: 0, byteOffset: 32, componentType: 5121, normalized: true, count: 24, type: 'VEC4' },
    { bufferView: 1, componentType: 5123, count: 36, type: 'SCALAR' },
  ],
  images: [{ uri: `data:image/png;base64,${png.toString('base64')}` }],
  samplers: [{ magFilter: 9728, minFilter: 9987, wrapS: 10497, wrapT: 33071 }],
  textures: [{ source: 0, sampler: 0 }],
  materials: [{ name: 'Checker', pbrMetallicRoughness: { baseColorFactor: [1,.8,.6,1], baseColorTexture: { index: 0 }, metallicFactor: 0, roughnessFactor: 1 } }],
  meshes: [{ name: 'SharedBox', primitives: [{ attributes: { POSITION: 0, NORMAL: 1, TEXCOORD_0: 2, COLOR_0: 3 }, indices: 4, material: 0 }] }],
  nodes: [
    { name: 'Root', translation: [0,.25,0], children: [1,2] },
    { name: 'TallBox', mesh: 0, translation: [-.7,0,0], scale: [1,1.4,1] },
    { name: 'MirroredBox', mesh: 0, translation: [.7,0,0], scale: [-.75,.75,.75] },
    { name: 'OtherScene', mesh: 0, translation: [100,0,0] },
  ],
  scenes: [{ nodes: [0] }, { nodes: [3] }], scene: 0,
};
output('SharedBox.gltf', JSON.stringify(model, null, 2) + '\n');
const multi = structuredClone(model);
multi.materials.push(structuredClone(multi.materials[0]));
multi.materials[1].pbrMetallicRoughness.baseColorFactor = [.6,.8,1,1];
multi.meshes[0].primitives.push({ ...multi.meshes[0].primitives[0], material: 1 });
output('MultiPrimitive.gltf', JSON.stringify(multi, null, 2) + '\n');
const external = structuredClone(model);
external.buffers[0].uri = 'geometry%20data.bin'; external.images[0].uri = 'checker%20image.png';
output('External.gltf', JSON.stringify(external, null, 2) + '\n');
output('geometry data.bin', geometry); output('checker image.png', png);

const binary = structuredClone(model);
delete binary.buffers[0].uri;
binary.bufferViews.push({ buffer: 0, byteOffset: geometry.length, byteLength: png.length });
binary.images = [{ bufferView: 2, mimeType: 'image/png' }];
const bin = Buffer.concat([geometry, png]);
binary.buffers[0].byteLength = bin.length;
const pad = (bytes, byte) => Buffer.concat([bytes, Buffer.alloc((4 - bytes.length % 4) % 4, byte)]);
const json = pad(Buffer.from(JSON.stringify(binary)), 32), paddedBin = pad(bin, 0);
const header = Buffer.alloc(12); header.writeUInt32LE(0x46546c67); header.writeUInt32LE(2, 4); header.writeUInt32LE(28 + json.length + paddedBin.length, 8);
const jsonHeader = Buffer.alloc(8); jsonHeader.writeUInt32LE(json.length); jsonHeader.writeUInt32LE(0x4e4f534a, 4);
const binHeader = Buffer.alloc(8); binHeader.writeUInt32LE(paddedBin.length); binHeader.writeUInt32LE(0x004e4942, 4);
const glb = Buffer.concat([header, jsonHeader, json, binHeader, paddedBin]);
output('SharedBox.glb', glb);
const exampleDirectory = resolve(directory, '../../examples/assets');
mkdirSync(exampleDirectory, { recursive: true });
writeFileSync(resolve(exampleDirectory, 'SharedBox.glb'), glb);

const sparseData = Buffer.alloc(40);
sparseData.set([0,1,2]);
[-.5,-.5,0, .5,-.5,0, 0,.5,0].forEach((value, i) => sparseData.writeFloatLE(value, 4 + i * 4));
output('Sparse.gltf', JSON.stringify({
  asset: { version: '2.0' }, buffers: [{ byteLength: 40, uri: `data:application/octet-stream;base64,${sparseData.toString('base64')}` }],
  bufferViews: [{ buffer: 0, byteOffset: 0, byteLength: 3 }, { buffer: 0, byteOffset: 4, byteLength: 36 }],
  accessors: [{ componentType: 5126, count: 3, type: 'VEC3', min: [-.5,-.5,0], max: [.5,.5,0], sparse: { count: 3, indices: { bufferView: 0, componentType: 5121 }, values: { bufferView: 1 } } }],
  meshes: [{ primitives: [{ attributes: { POSITION: 0 } }] }], nodes: [{ mesh: 0 }], scenes: [{ nodes: [0] }],
}, null, 2) + '\n');
