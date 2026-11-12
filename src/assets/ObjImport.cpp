#include "ImportCommon.h"
#include <tiny_obj_loader.h>
#include <algorithm>
#include <cmath>
#include <map>
#include <sstream>
#include <tuple>

namespace lv::detail {
namespace {
class Materials final : public tinyobj::MaterialReader {
public:
    ImportContext& Context;
    std::vector<std::filesystem::path> Directories;
    explicit Materials(ImportContext& context):Context(context) {}
    bool operator()(const std::string& name,std::vector<tinyobj::material_t>* materials,std::map<std::string,int>* map,std::string* warn,std::string* error) override {
        if (!Context.Model.Settings.ImportMaterials) {return true;}
        const auto file=Context.Model.SourcePath.parent_path()/std::filesystem::path(std::u8string(name.begin(),name.end()));
        std::error_code ec;if (!std::filesystem::is_regular_file(file,ec)) {Context.Model.MissingDependencies.push_back(file);Context.Model.Warnings.push_back("Missing MTL: "+name);return false;}
        const auto before=materials->size();const auto bytes=Context.Read(file);std::istringstream stream(std::string(bytes.begin(),bytes.end()));tinyobj::LoadMtl(map,materials,&stream,warn,error);Directories.resize(materials->size());for (auto i=before;i<materials->size();++i) {Directories[i]=file.parent_path();}return true;
    }
};
using Key=std::tuple<int,int,int>;
struct Primitive {MeshPrimitive Part;std::map<Key,std::uint32_t> Vertices;};
}
std::shared_ptr<ModelAsset> ImportObj(const std::filesystem::path& path,const ImportSettings& settings) {
    ValidateImportSettings(settings);auto model=std::make_shared<ModelAsset>();model->SourcePath=path;model->Settings=settings;model->Format=AssetFormat::Obj;model->ContentHash=14695981039346656037ULL;
    ImportContext context{*model};const auto bytes=context.Read(path);std::istringstream input(std::string(bytes.begin(),bytes.end()));
    tinyobj::attrib_t attributes;std::vector<tinyobj::shape_t> shapes;std::vector<tinyobj::material_t> materials;std::string warning,error;Materials reader(context);
    if (!tinyobj::LoadObj(&attributes,&shapes,&materials,&warning,&error,&input,&reader,true,true)) {ImportInvalid("OBJ parse failed: "+error);}
    if (!warning.empty()) {model->Warnings.push_back(warning);}
    if (attributes.vertices.size()/3>settings.MaxVertices || shapes.size()>settings.MaxNodes) {ImportLimit("OBJ source exceeds vertex/node limits.");}
    for (std::size_t i=0;i<materials.size();++i) {
        const auto& source=materials[i];
        MaterialAsset material;material.Name=source.name;material.BaseColor={source.diffuse[0],source.diffuse[1],source.diffuse[2],1};
        if (source.dissolve<1) {model->Warnings.push_back("OBJ transparency is not supported; material uses opaque rendering: "+source.name);}
        if (!source.diffuse_texname.empty()) {material.BaseColorTexture=context.Texture(source.diffuse_texname,{},reader.Directories[i]);}
        if (!source.normal_texname.empty()) {material.NormalTexture=context.Texture(source.normal_texname,{},reader.Directories[i]);}
        if (material.BaseColorTexture && source.diffuse_texopt.clamp) {auto& texture=model->Textures[*material.BaseColorTexture];texture.WrapU=texture.WrapV=TextureWrap::ClampToEdge;}
        if (material.NormalTexture && source.normal_texopt.clamp) {auto& texture=model->Textures[*material.NormalTexture];texture.WrapU=texture.WrapV=TextureWrap::ClampToEdge;}
        const auto transformed=[](const tinyobj::texture_option_t& option) {return option.origin_offset[0]!=0 || option.origin_offset[1]!=0 || option.scale[0]!=1 || option.scale[1]!=1;};
        if (transformed(source.diffuse_texopt) || transformed(source.normal_texopt)) {model->Warnings.push_back("OBJ texture UV offset/scale is not evaluated: "+source.name);}
        if (!source.bump_texname.empty()) {model->Warnings.push_back("OBJ height/bump maps are not converted to normal maps: "+source.bump_texname);}
        if (!source.specular_texname.empty() || !source.emissive_texname.empty()) {model->Warnings.push_back("OBJ specular/emissive textures are not evaluated: "+source.name);}
        model->Materials.push_back(std::move(material));
    }
    const auto fallback=model->Materials.size();model->Materials.push_back({});
    for (const auto& shape:shapes) {
        if (!shape.lines.indices.empty() || !shape.points.indices.empty()) {model->Warnings.push_back("OBJ lines/points are skipped: "+shape.name);}
        std::map<std::size_t,Primitive> groups;std::size_t offset=0;
        for (std::size_t face=0;face<shape.mesh.num_face_vertices.size();++face) {
            const auto count=shape.mesh.num_face_vertices[face];if (count!=3 || offset+count>shape.mesh.indices.size()) {ImportInvalid("OBJ polygon triangulation failed.");}
            const int materialId=face<shape.mesh.material_ids.size() ? shape.mesh.material_ids[face]:-1;
            const auto material=materialId>=0 && static_cast<std::size_t>(materialId)<fallback ? static_cast<std::size_t>(materialId):fallback;
            auto& group=groups[material];group.Part.MaterialIndex=material;
            Vertex triangle[3]{};bool missing=false;
            for (std::size_t corner=0;corner<3;++corner) {
                const auto index=shape.mesh.indices[offset+corner];auto& v=triangle[corner];v.Color={1,1,1,1};
                if (index.vertex_index<0 || static_cast<std::size_t>(index.vertex_index)>=attributes.vertices.size()/3) {ImportInvalid("OBJ position index is out of range.");}
                std::copy_n(attributes.vertices.data()+index.vertex_index*3,3,v.Position.begin());
                if (attributes.colors.size()>=static_cast<std::size_t>(index.vertex_index+1)*3) {std::copy_n(attributes.colors.data()+index.vertex_index*3,3,v.Color.begin());}
                if (index.normal_index>=0) {if (static_cast<std::size_t>(index.normal_index)>=attributes.normals.size()/3) {ImportInvalid("OBJ normal index is out of range.");}std::copy_n(attributes.normals.data()+index.normal_index*3,3,v.Normal.begin());}else {missing=true;}
                if (index.texcoord_index>=0) {if (static_cast<std::size_t>(index.texcoord_index)>=attributes.texcoords.size()/2) {ImportInvalid("OBJ UV index is out of range.");}v.TexCoord={attributes.texcoords[index.texcoord_index*2],1-attributes.texcoords[index.texcoord_index*2+1]};}
            }
            if (missing) {
                if (!settings.GenerateMissingNormals) {ImportInvalid("OBJ has no normals; enable GenerateMissingNormals.");}
                const auto& a=triangle[0].Position;const auto& b=triangle[1].Position;const auto& c=triangle[2].Position;
                const float x1=b[0]-a[0],y1=b[1]-a[1],z1=b[2]-a[2],x2=c[0]-a[0],y2=c[1]-a[1],z2=c[2]-a[2];const float nx=y1*z2-z1*y2,ny=z1*x2-x1*z2,nz=x1*y2-y1*x2;const auto length=std::hypot(nx,ny,nz);
                if (!std::isfinite(length) || length<=0) {ImportInvalid("Degenerate OBJ triangle cannot generate a normal.");}
                for (auto& vertex:triangle) {vertex.Normal={nx/length,ny/length,nz/length};}
            }
            for (std::size_t corner=0;corner<3;++corner) {
                const auto index=shape.mesh.indices[offset+corner];const Key key{index.vertex_index,index.normal_index,index.texcoord_index};
                auto found=group.Vertices.find(key);auto& mesh=group.Part.Geometry;
                if (!missing && found!=group.Vertices.end()) {context.Geometry(0,1);mesh.Indices.push_back(found->second);}
                else {context.Geometry(1,1);const auto id=static_cast<std::uint32_t>(mesh.Vertices.size());mesh.Vertices.push_back(triangle[corner]);mesh.Indices.push_back(id);if (!missing) {group.Vertices.emplace(key,id);}}
            }
            offset+=count;
        }
        if (groups.empty()) {continue;}
        MeshAsset mesh;mesh.Name=shape.name;for (auto& [material,group]:groups) {(void)material;mesh.Primitives.push_back(std::move(group.Part));}
        const auto meshIndex=model->Meshes.size(),nodeIndex=model->Nodes.size();model->Meshes.push_back(std::move(mesh));ModelNode node;node.Name=shape.name;node.MeshIndex=meshIndex;model->Nodes.push_back(node);SkeletonJoint joint;joint.Name=shape.name;model->Skeleton.Joints.push_back(joint);model->Parts.push_back({meshIndex,Mat4::Identity(),static_cast<JointId>(nodeIndex),{}});
    }
    FinishImportedModel(*model);return model;
}
} // namespace lv::detail
