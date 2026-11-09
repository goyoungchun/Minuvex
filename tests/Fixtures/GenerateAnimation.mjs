// Original two-joint glTF fixture. Regenerate with node; no build-time Node dependency.
import {writeFileSync, mkdirSync} from 'node:fs';
import {fileURLToPath} from 'node:url';
import {dirname,resolve} from 'node:path';
const directory=dirname(fileURLToPath(import.meta.url));
const chunks=[], views=[], accessors=[]; let length=0;
function accessor(values,type,component=5126,normalized=false,limits=false) {
  const bytes=component===5126?4:component===5123?2:1;
  const n={SCALAR:1,VEC2:2,VEC3:3,VEC4:4,MAT4:16}[type];
  const padding=(4-length%4)%4; chunks.push(Buffer.alloc(padding)); length+=padding;
  const data=Buffer.alloc(values.length*bytes);
  values.forEach((v,i)=>component===5126?data.writeFloatLE(v,i*bytes):component===5123?data.writeUInt16LE(v,i*bytes):data.writeUInt8(v,i));
  views.push({buffer:0,byteOffset:length,byteLength:data.length}); chunks.push(data); length+=data.length;
  const a={bufferView:views.length-1,componentType:component,count:values.length/n,type};
  if(normalized)a.normalized=true;
  if(limits){a.min=Array.from({length:n},(_,i)=>Math.min(...values.filter((_,j)=>j%n===i)));a.max=Array.from({length:n},(_,i)=>Math.max(...values.filter((_,j)=>j%n===i)));}
  accessors.push(a); return accessors.length-1;
}
const positions=accessor([-.15,0,0,.15,0,0,-.15,1,0,.15,1,0,-.15,2,0,.15,2,0],'VEC3',5126,false,true);
const normals=accessor(Array.from({length:6},()=>[0,0,1]).flat(),'VEC3');
const joints=accessor(Array.from({length:6},()=>[0,1,0,0]).flat(),'VEC4',5123);
const weights=accessor([255,0,0,0,255,0,0,0,128,127,0,0,128,127,0,0,0,255,0,0,0,255,0,0],'VEC4',5121,true);
const indices=accessor([0,1,2,1,3,2,2,3,4,3,5,4],'SCALAR',5123);
const matrix=(x,y)=>[1,0,0,0,0,1,0,0,0,0,1,0,x,y,0,1];
const inverse=accessor([...matrix(-.2,0),...matrix(-.2,-1)],'MAT4');
const times=accessor([0,1,2],'SCALAR',5126,false,true);
const rotation=accessor([0,0,0,1,0,0,Math.SQRT1_2,Math.SQRT1_2,0,0,0,1],'VEC4');
const movement=accessor([0,0,0,.5,0,0,0,0,0],'VEC3');
const step=accessor([0,0,0,1,0,0,0,0,0],'VEC3');
const scale=accessor([1,1,1,1.5,1,1,2,1,1],'VEC3');
const ancestor=accessor([.2,0,0,.7,0,0,1.2,0,0],'VEC3');
const track=(node,path,output,interpolation='LINEAR')=>({samplers:[{input:times,output,interpolation}],channels:[{sampler:0,target:{node,path}}]});
const bend=track(2,'rotation',rotation);bend.name='Bend';bend.samplers.push({input:times,output:movement,interpolation:'LINEAR'});bend.channels.push({sampler:1,target:{node:1,path:'translation'}});
const clips=[bend,{name:'Step Move',...track(1,'translation',step,'STEP')},{name:'Scale',...track(2,'scale',scale)},{name:'Ancestor',...track(0,'translation',ancestor)}];
const data=Buffer.concat(chunks);
const model={asset:{version:'2.0',generator:'Minuvex original animation fixture'},buffers:[{byteLength:data.length,uri:`data:application/octet-stream;base64,${data.toString('base64')}`}],bufferViews:views,accessors,
  materials:[{name:'Orange',doubleSided:true,pbrMetallicRoughness:{baseColorFactor:[1,.4,.05,1]},extensions:{KHR_materials_unlit:{}}}],extensionsUsed:['KHR_materials_unlit'],
  meshes:[{name:'Ribbon',primitives:[{attributes:{POSITION:positions,NORMAL:normals,JOINTS_0:joints,WEIGHTS_0:weights},indices,material:0}]}],
  nodes:[{name:'RigRoot',translation:[.2,0,0],children:[1,3]},{name:'Hip',children:[2]},{name:'Tip',translation:[0,1,0]},{name:'SkinnedRibbon',mesh:0,skin:0,translation:[4,0,0]}],
  skins:[{name:'TwoJointSkin',joints:[1,2],inverseBindMatrices:inverse,skeleton:1}],animations:clips,scenes:[{nodes:[0]}],scene:0};
writeFileSync(resolve(directory,'AnimatedRibbon.gltf'),JSON.stringify(model,null,2)+'\n');
const rigid=structuredClone(model);delete rigid.nodes[3].skin;rigid.nodes[3].translation=[0,0,0];delete rigid.skins;
rigid.animations=[{name:'Rigid Move',...track(3,'translation',step)}];
writeFileSync(resolve(directory,'AnimatedRigid.gltf'),JSON.stringify(rigid,null,2)+'\n');
const example=resolve(directory,'../../examples/assets');mkdirSync(example,{recursive:true});
writeFileSync(resolve(example,'AnimatedRibbon.gltf'),JSON.stringify(model,null,2)+'\n');
