// Minuvex-authored morph fixtures. No build-time Node dependency.
import {readFileSync,writeFileSync} from 'node:fs';
import {fileURLToPath} from 'node:url';
import {dirname,resolve} from 'node:path';
const dir=dirname(fileURLToPath(import.meta.url));
const model=JSON.parse(readFileSync(resolve(dir,'AnimatedRibbon.gltf'),'utf8'));
let data=Buffer.from(model.buffers[0].uri.split(',')[1],'base64');
function accessor(values,type) {
  const padding=(4-data.length%4)%4;data=Buffer.concat([data,Buffer.alloc(padding)]);
  const bytes=Buffer.alloc(values.length*4);values.forEach((v,i)=>bytes.writeFloatLE(v,i*4));
  model.bufferViews.push({buffer:0,byteOffset:data.length,byteLength:bytes.length});data=Buffer.concat([data,bytes]);
  model.accessors.push({bufferView:model.bufferViews.length-1,componentType:5126,count:values.length/({SCALAR:1,VEC3:3,VEC4:4}[type]),type});return model.accessors.length-1;
}
const primitive=model.meshes[0].primitives[0],vertices=model.accessors[primitive.attributes.POSITION].count;
primitive.attributes.TANGENT=accessor(Array.from({length:vertices},()=>[1,0,0,1]).flat(),'VEC4');
const delta=(v)=>accessor(Array.from({length:vertices},()=>v).flat(),'VEC3');
primitive.targets=[{POSITION:delta([.5,0,0]),NORMAL:delta([.3,0,-.1]),TANGENT:delta([0,.3,0])},{POSITION:delta([0,.5,0])}];
model.meshes[0].weights=[.2,0];model.meshes[0].extras={targetNames:['Smile','Lift']};model.nodes[3].weights=[.6,0];
model.nodes.push({name:'SharedMorphNode',mesh:0,translation:[-1,0,0]});model.nodes[0].children.push(4);
const times=accessor([0,1,2],'SCALAR'),values=accessor([0,0,1,0,0,1],'SCALAR');
model.animations.push({name:'Morph LINEAR',samplers:[{input:times,output:values,interpolation:'LINEAR'}],channels:[{sampler:0,target:{node:3,path:'weights'}}]});
model.animations.push({name:'Morph STEP',samplers:[{input:times,output:values,interpolation:'STEP'}],channels:[{sampler:0,target:{node:3,path:'weights'}}]});
model.buffers[0]={byteLength:data.length,uri:`data:application/octet-stream;base64,${data.toString('base64')}`};
const save=(name,value)=>writeFileSync(resolve(dir,name),JSON.stringify(value,null,2)+'\n');save('MorphRibbon.gltf',model);
writeFileSync(resolve(dir,'../../examples/assets/MorphRibbon.gltf'),JSON.stringify(model,null,2)+'\n');
const rigid=structuredClone(model);delete rigid.skins;delete rigid.nodes[3].skin;rigid.nodes[3].translation=[0,0,0];save('MorphRigid.gltf',rigid);
const sparse=structuredClone(rigid);const padding=(4-data.length%4)%4;const indexOffset=data.length+padding;data=Buffer.concat([data,Buffer.alloc(padding),Buffer.from([1,0,0,0])]);
sparse.bufferViews.push({buffer:0,byteOffset:indexOffset,byteLength:1});const indexView=sparse.bufferViews.length-1;
const valueOffset=data.length,bytes=Buffer.alloc(12);bytes.writeFloatLE(1,0);data=Buffer.concat([data,bytes]);sparse.bufferViews.push({buffer:0,byteOffset:valueOffset,byteLength:12});
sparse.accessors.push({componentType:5126,count:vertices,type:'VEC3',sparse:{count:1,indices:{bufferView:indexView,componentType:5121},values:{bufferView:sparse.bufferViews.length-1}}});
sparse.meshes[0].primitives[0].targets[0]={POSITION:sparse.accessors.length-1};sparse.buffers[0]={byteLength:data.length,uri:`data:application/octet-stream;base64,${data.toString('base64')}`};save('MorphSparse.gltf',sparse);
let fbx=readFileSync(resolve(dir,'Compatibility.fbx'),'utf8');
const objects=`
    Deformer: 1300, "Deformer::Face", "BlendShape" { Version: 100 }
    Deformer: 1301, "SubDeformer::Smile", "BlendShapeChannel" {
        Version: 100
        DeformPercent: 25
        FullWeights: *2 { a: 50,100 }
    }
    Geometry: 1302, "Geometry::Half", "Shape" {
        Version: 100
        Indexes: *2 { a: 2,3 }
        Vertices: *6 { a: 0.5,0,0,0.5,0,0 }
        Normals: *6 { a: 0.2,0,0,0.2,0,0 }
    }
    Geometry: 1303, "Geometry::Full", "Shape" {
        Version: 100
        Indexes: *2 { a: 2,3 }
        Vertices: *6 { a: 1,0,0,1,0,0 }
        Normals: *6 { a: 0.4,0,0,0.4,0,0 }
    }
    AnimationCurveNode: 1304, "AnimCurveNode::Morph", "" {
        Properties70: { P: "d|DeformPercent", "Number", "", "A",0 }
    }
    AnimationCurve: 1305, "AnimCurve::Morph", "" {
        Default: 0
        KeyVer: 4008
        KeyTime: *3 { a: 0,23093079000,46186158000 }
        KeyValueFloat: *3 { a: 0,50,100 }
        KeyAttrFlags: *3 { a: 24836,24836,24836 }
        KeyAttrDataFloat: *12 { a: 0,0,0,0,0,0,0,0,0,0,0,0 }
        KeyAttrRefCount: *3 { a: 1,1,1 }
    }
`;
fbx=fbx.replace('Objects: {','Objects: {\n'+objects).replace('Connections: {',`Connections: {
    C: "OO",1300,100
    C: "OO",1301,1300
    C: "OO",1302,1301
    C: "OO",1303,1301
    C: "OO",1304,901
    C: "OP",1304,1301,"DeformPercent"
    C: "OP",1305,1304,"d|DeformPercent"`);
writeFileSync(resolve(dir,'MorphProgressive.fbx'),fbx);
