// Minuvex-authored FBX 7400 binary triangle; no third-party model data.
import {writeFileSync} from 'node:fs';
const u32=n=>{const b=Buffer.alloc(4);b.writeUInt32LE(n);return b;};
function prop(v) {
  if (typeof v==='string') {const b=Buffer.from(v);return Buffer.concat([Buffer.from('S'),u32(b.length),b]);}
  if (typeof v==='bigint') {const b=Buffer.alloc(9);b[0]=76;b.writeBigInt64LE(v,1);return b;}
  if (typeof v==='number') {const b=Buffer.alloc(9);b[0]=68;b.writeDoubleLE(v,1);return b;}
  const [kind,values]=v;const b=Buffer.alloc(values.length*(kind==='d'?8:4));values.forEach((n,i)=>kind==='d'?b.writeDoubleLE(n,i*8):b.writeInt32LE(n,i*4));
  return Buffer.concat([Buffer.from(kind),u32(values.length),u32(0),u32(b.length),b]);
}
const node=(name,values=[],children=[])=>({name,values,children});
function encode(n,offset) {
  const name=Buffer.from(n.name),properties=Buffer.concat(n.values.map(prop));const start=offset+13+name.length+properties.length;const chunks=[];let size=0;
  for(const child of n.children) {const b=encode(child,start+size);chunks.push(b);size+=b.length;}
  if(n.children.length) {chunks.push(Buffer.alloc(13));size+=13;}
  return Buffer.concat([u32(start+size),u32(n.values.length),u32(properties.length),Buffer.from([name.length]),name,properties,...chunks]);
}
const items=[
  node('FBXHeaderExtension',[],[node('FBXHeaderVersion',[1003n]),node('FBXVersion',[7400n])]),
  node('GlobalSettings',[],[node('Properties70',[],[node('P',['UnitScaleFactor','double','Number','',100])])]),
  node('Objects',[],[
    node('Geometry',[100n,'Triangle\0\x01Geometry','Mesh'],[
      node('Vertices',[['d',[-.5,-.5,0,.5,-.5,0,0,.5,0]]]),node('PolygonVertexIndex',[['i',[0,1,-3]]])]),
    node('Model',[200n,'Triangle\0\x01Model','Mesh'])]),
  node('Connections',[],[node('C',['OO',100n,200n]),node('C',['OO',200n,0n])]),
];
let result=Buffer.concat([Buffer.from('Kaydara FBX Binary  \0\x1a\0'),u32(7400)]);
for(const item of items) {result=Buffer.concat([result,encode(item,result.length)]);}
result=Buffer.concat([result,Buffer.alloc(13)]);
writeFileSync(new URL('CompatibilityBinary.fbx',import.meta.url),result);
