#include "TestHarness.h"

#include <cstring>
#include <algorithm>
#include <limits>
#include <json.hpp>

using namespace test;
namespace {

void TestImports() {
    lv::AssetRegistry registry;
    const auto gltf = registry.LoadModel(Fixture("SharedBox.gltf"));
    const auto glb = registry.LoadModel(Fixture("SharedBox.glb"));
    const auto external = registry.LoadModel(Fixture("External.gltf"));
    Check(registry.Size() == 3, "three source assets are registered");
    Check(gltf->Id != 0 && gltf->Id != glb->Id, "assets have unique nonzero IDs");
    Check(registry.Find(gltf->Id) == gltf && !registry.Find(0), "registry lookup and invalid asset ID");
    Check(registry.LoadModel(Fixture("../Fixtures/SharedBox.gltf")) == gltf, "canonical path load reuses the same CPU asset");
    Check(gltf->Settings == lv::ImportSettings{} && gltf->ImportVersion == 6, "registry retains import metadata");
    Check(gltf->ContentHash && gltf->SourcePath.is_absolute(), "asset has a content hash and canonical source");
    Check(gltf->Dependencies.size() == 1 && external->Dependencies.size() == 3, "embedded and external dependency lists");
    for (const auto& model : {gltf, glb, external}) {
        Check(model->Meshes.size() == 1 && model->Meshes[0].Primitives.size() == 1, "one source mesh is shared between glTF nodes");
        Check(model->Parts.size() == 2 && model->Nodes.size() == 4, "only nodes in the selected glTF scene are drawn");
        Check(model->Parts[0].MeshIndex == model->Parts[1].MeshIndex, "repeated node meshes refer to the same geometry");
        Check(model->Parts[1].Transform.LinearDeterminant() < 0, "mirrored node transforms are retained");
        Check(Near(model->ModelBounds.Min.X, -1.2f) && Near(model->ModelBounds.Max.X, 1.075f), "bounds incorporate hierarchy and nonuniform scale");
        const auto& mesh = model->Meshes[0].Primitives[0].Geometry;
        Check(mesh.Vertices.size() == 24 && mesh.Indices.size() == 36, "indexed interleaved geometry is decoded");
        Check(Near(mesh.Vertices[0].Normal[2], 1) && Near(mesh.Vertices[0].TexCoord[1], 1), "interleaved attribute offsets are respected");
        Check(Near(mesh.Vertices[0].Color[0], 1), "normalized unsigned-byte colors are decoded");
        Check(model->Images.size() == 1 && model->Images[0].Width == 2 && model->Images[0].Rgba.size() == 16, "embedded/external PNG becomes RGBA pixels");
        Check(model->Images[0].Rgba[4] == 30 && model->Images[0].Rgba[8] == 250, "image row order and decoded colors are preserved");
        Check(model->Textures[0].WrapV == lv::TextureWrap::ClampToEdge && model->Textures[0].UseMipmaps, "texture sampler settings are imported");
        Check(model->Materials[0].BaseColorTexture == 0 && Near(model->Materials[0].BaseColor[1], 0.8f), "base color factor and image reference are imported");
    }
    Check(gltf->Meshes[0].Primitives[0].Geometry.Indices == glb->Meshes[0].Primitives[0].Geometry.Indices, "GLB and glTF geometry agree");
    auto sparse = registry.LoadModel(Fixture("Sparse.gltf"));
    const auto& triangle = sparse->Meshes[0].Primitives[0].Geometry;
    Check(triangle.Vertices.size() == 3 && triangle.Indices == std::vector<std::uint32_t>{0,1,2}, "sparse, non-indexed primitive is decoded");
    Check(Near(triangle.Vertices[2].Position[1], 0.5f) && Near(triangle.Vertices[2].Normal[2], 1), "sparse positions and generated flat normals");
    Check(sparse->Materials.size() == 1 && !sparse->Materials[0].BaseColorTexture, "fallback material for missing material");
    const auto multi = registry.LoadModel(Fixture("MultiPrimitive.gltf"));
    Check(multi->Meshes[0].Primitives.size() == 2 && multi->Images[0].Rgba.size() == 16 && multi->Materials[1].BaseColorTexture == 0, "multiple primitives/materials share the decoded source image");

    lv::ImportSettings strict;
    strict.GenerateMissingNormals = false;
    ImportRejects([&] { registry.LoadModel(Fixture("Sparse.gltf"), strict); }, lv::ImportErrorCode::InvalidData, "missing normals can be rejected explicitly");
    auto variant = registry.LoadModel(Fixture("SharedBox.gltf"), strict);
    Check(variant != gltf && variant->Settings == strict, "different import settings do not alias a cached asset");

    TemporaryDirectory temp;
    const auto variantPath = temp.File("material.gltf");
    Write(variantPath, Replace(Read(Fixture("SharedBox.gltf")), "\"name\": \"Checker\",",
        "\"name\":\"Checker\",\"doubleSided\":true,\"alphaMode\":\"MASK\",\"alphaCutoff\":0.3,\"extensions\":{\"KHR_materials_unlit\":{}},"));
    const auto material = registry.LoadModel(variantPath);
    Check(material->Materials[0].DoubleSided && material->Materials[0].Unlit && Near(material->Materials[0].AlphaCutoff, 0.3f), "MASK, double-sided and unlit material properties are imported");
    Write(temp.File("matrix.gltf"), Replace(Read(Fixture("Sparse.gltf")), "\"mesh\": 0", "\"mesh\":0,\"matrix\":[2,0,0,0,0,3,0,0,0,0,4,0,1,2,3,1]"));
    const auto matrix = registry.LoadModel(temp.File("matrix.gltf"));
    Check(Near(matrix->ModelBounds.Min.X, 0) && Near(matrix->ModelBounds.Max.Y, 3.5f) && Near(matrix->ModelBounds.Max.Z, 3), "glTF matrix transforms affect imported bounds");
    const auto unicodePath = temp.Path / std::filesystem::path(u8"로봇.glb");
    std::filesystem::copy_file(Fixture("SharedBox.glb"), unicodePath);
    Check(registry.LoadModel(unicodePath)->Parts.size() == 2, "Unicode model paths work on Windows");
    lv::AssetRegistry another;
    Check(another.LoadModel(Fixture("SharedBox.gltf"))->Id != gltf->Id, "asset IDs do not collide between registries");
    registry.Clear();
    Check(registry.Size() == 0 && gltf->Meshes.size() == 1, "existing handles survive clearing the registry");
}

void TestFailuresAndLimits() {
    TemporaryDirectory temp;
    const auto json = Read(Fixture("SharedBox.gltf"));
    lv::AssetRegistry registry;
    const auto bad = temp.File("bad.gltf");
    const auto failure = [&](const std::string& text, lv::ImportErrorCode code, const char* message) {
        Write(bad, text);
        ImportRejects([&] { registry.LoadModel(bad); }, code, message);
        Check(registry.Size() == 0, "failed import leaves the registry unchanged");
    };
    failure("{bad", lv::ImportErrorCode::InvalidData, "malformed JSON is rejected");
    failure(Replace(json, "\"byteStride\": 36", "\"byteStride\": 4"), lv::ImportErrorCode::InvalidData, "undersized attribute stride is rejected");
    failure(Replace(json, "\"byteOffset\": 864", "\"byteOffset\": 9999999"), lv::ImportErrorCode::InvalidData, "out-of-bounds buffer view is rejected");
    failure(Replace(json, "\"count\": 24", "\"count\": 0"), lv::ImportErrorCode::InvalidData, "zero accessor count is rejected without underflow");
    failure(Replace(json, "\"normalized\": true", "\"normalized\": false"), lv::ImportErrorCode::InvalidData, "integer color attributes require normalization");
    failure(Replace(Read(Fixture("Sparse.gltf")), "base64,AAEC", "base64,AAAA"), lv::ImportErrorCode::InvalidData, "duplicate sparse indices are rejected");
    failure(Replace(json, "\"children\": [\n        1,", "\"children\": [\n        0,"), lv::ImportErrorCode::InvalidData, "cyclic node hierarchy is rejected");
    failure(Replace(Replace(Read(Fixture("Sparse.gltf")), "\"asset\":", "\"skins\":[{\"joints\":[0]}],\"asset\":"), "\"mesh\": 0", "\"mesh\":0,\"skin\":0"), lv::ImportErrorCode::InvalidData, "skinned mesh requires joint attributes");
    failure(Replace(json, "\"asset\":", "\"extensionsRequired\":[\"KHR_draco_mesh_compression\"],\"asset\":"), lv::ImportErrorCode::UnsupportedFeature, "unsupported required extension is explicit");
    failure(Replace(json, "\"attributes\":", "\"mode\":1,\"attributes\":"), lv::ImportErrorCode::UnsupportedFeature, "line primitives are explicitly unsupported");
    failure(Replace(json, "\"name\": \"Checker\",", "\"name\":\"Checker\",\"alphaMode\":\"BLEND\","), lv::ImportErrorCode::UnsupportedFeature, "alpha blending is explicitly unsupported");
    failure(Replace(json, "\"index\": 0", "\"index\":0,\"texCoord\":1"), lv::ImportErrorCode::UnsupportedFeature, "unsupported texture coordinate set is explicit");
    auto glb = Read(Fixture("SharedBox.glb"));
    glb.resize(20);
    Write(temp.File("short.glb"), glb);
    ImportRejects([&] { registry.LoadModel(temp.File("short.glb")); }, lv::ImportErrorCode::InvalidData, "truncated GLB is rejected");
    ImportRejects([&] { registry.LoadModel(temp.File("missing.glb")); }, lv::ImportErrorCode::Io, "missing model produces an IO error");
    ImportRejects([&] { registry.LoadModel(temp.Path / std::filesystem::path(u8"없는_로봇_🤖.glb")); }, lv::ImportErrorCode::Io, "Unicode errors retain the structured IO error code");

    Write(temp.File("external.gltf"), Read(Fixture("External.gltf")));
    Write(temp.File("geometry data.bin"), Read(Fixture("geometry data.bin")));
    Write(temp.File("checker image.png"), Read(Fixture("checker image.png")));
    auto geometry = Read(Fixture("geometry data.bin"));
    geometry[864] = geometry[865] = static_cast<char>(255);
    Write(temp.File("geometry data.bin"), geometry);
    ImportRejects([&] { registry.LoadModel(temp.File("external.gltf")); }, lv::ImportErrorCode::InvalidData, "out-of-range primitive indices are rejected");
    Write(temp.File("geometry data.bin"), "short");
    ImportRejects([&] { registry.LoadModel(temp.File("external.gltf")); }, lv::ImportErrorCode::InvalidData, "short external buffer is rejected");
    Write(temp.File("geometry data.bin"), Read(Fixture("geometry data.bin")));
    Write(temp.File("checker image.png"), "not an image");
    ImportRejects([&] { registry.LoadModel(temp.File("external.gltf")); }, lv::ImportErrorCode::InvalidData, "corrupt images are rejected");
    Write(temp.File("external.gltf"), Replace(Read(Fixture("External.gltf")), "geometry%20data.bin", "https://example.invalid/model.bin"));
    ImportRejects([&] { registry.LoadModel(temp.File("external.gltf")); }, lv::ImportErrorCode::UnsupportedFeature, "network resources are explicitly rejected");

    const auto limited = [&](lv::ImportSettings settings, const char* message) {
        ImportRejects([&] { registry.LoadModel(Fixture("SharedBox.gltf"), settings); }, lv::ImportErrorCode::ResourceLimit, message);
    };
    lv::ImportSettings limits;
    limits.MaxFileBytes = 10; limited(limits, "source byte limit is enforced");
    limits = {}; limits.MaxVertices = 23; limited(limits, "vertex limit is enforced");
    limits = {}; limits.MaxIndices = 35; limited(limits, "index limit is enforced");
    limits = {}; limits.MaxNodes = 3; limited(limits, "node limit is enforced");
    limits = {}; limits.MaxImageDimension = 1; limited(limits, "image dimension limit is enforced before decoding");
    limits = {}; limits.MaxResourceBytes = json.size() + 936 + 82 + 15; limited(limits, "decoded texture counts against resource budget");
    limits = {}; limits.MaxNodes = 0;
    Rejects([&] { registry.LoadModel(Fixture("SharedBox.gltf"), limits); }, "invalid import limits are rejected");
}

void TestLargeIndices() {
    TemporaryDirectory temp;
    constexpr std::size_t count = 65537;
    std::string bytes(count * 24 + 12, '\0');
    for (std::size_t i = 0; i < count; ++i) {
        const float vertex[6]{i == 1 ? 1.0f : 0.0f, i == 65536 ? 1.0f : 0.0f, 0, 0, 0, 1};
        std::memcpy(bytes.data() + i * 24, vertex, sizeof(vertex));
    }
    const std::uint32_t indices[]{0,1,65536};
    std::memcpy(bytes.data() + count * 24, indices, sizeof(indices));
    Write(temp.File("large.bin"), bytes);
    Write(temp.File("large.gltf"), "{\"asset\":{\"version\":\"2.0\"},\"buffers\":[{\"uri\":\"large.bin\",\"byteLength\":" + std::to_string(bytes.size()) +
        "}],\"bufferViews\":[{\"buffer\":0,\"byteStride\":24,\"byteLength\":" + std::to_string(count*24) +
        "},{\"buffer\":0,\"byteOffset\":" + std::to_string(count*24) + ",\"byteLength\":12}],\"accessors\":[{\"bufferView\":0,\"componentType\":5126,\"type\":\"VEC3\",\"count\":65537},{\"bufferView\":0,\"byteOffset\":12,\"componentType\":5126,\"type\":\"VEC3\",\"count\":65537},{\"bufferView\":1,\"componentType\":5125,\"type\":\"SCALAR\",\"count\":3}],\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":0,\"NORMAL\":1},\"indices\":2}]}],\"nodes\":[{\"mesh\":0}],\"scenes\":[{\"nodes\":[0]}]}");
    lv::AssetRegistry registry;
    const auto model = registry.LoadModel(temp.File("large.gltf"));
    Check(model->Meshes[0].Primitives[0].Geometry.Vertices.size() == count && model->Meshes[0].Primitives[0].Geometry.Indices[2] == 65536, "32-bit indices survive glTF import without truncation");
}
void TestCompatibility() {
    lv::AssetRegistry registry;
    Check(registry.Importers().Supports("model.OBJ") && registry.Importers().Supports("model.FBX") && !registry.Importers().Supports("model.xyz") && registry.Importers().Extensions().size() == 4,"common importer registry dispatches extensions case-insensitively");
    const auto preview=registry.PrepareModel(Fixture("Compatibility.obj"));
    Check(registry.Size() == 0 && preview->Format == lv::AssetFormat::Obj && preview->Status == lv::AssetImportStatus::ReadyWithWarnings,"OBJ preview is validated without registering it");
    const auto info=lv::InspectModel(*preview);
    Check(info.Meshes == 2 && info.Indices == 9 && info.Materials == 3 && info.Bones == 0 && info.Animations == 0,"OBJ quad triangulation, groups, negative indices and material partitioning");
    Check(preview->Dependencies.size() == 3 && preview->MissingDependencies.size() == 1 && preview->Materials[0].BaseColorTexture && !preview->Materials[1].BaseColorTexture,"OBJ/MTL textures and missing texture fallback retain dependency diagnostics");
    Check(preview->Textures[*preview->Materials[0].BaseColorTexture].WrapU == lv::TextureWrap::ClampToEdge,"OBJ MTL clamp option maps to the common texture sampler");
    Check(Near(preview->Meshes[0].Primitives[0].Geometry.Vertices[0].TexCoord[1],1),"OBJ UV origin is normalized to the engine convention");
    const auto committed=registry.AddPrepared(preview);
    Check(committed == preview && registry.LoadModel(Fixture("Compatibility.obj")) == preview && registry.Size() == 1,"preview commit and cached loads share immutable data");
    auto invalid=std::make_shared<lv::ModelAsset>(*preview);invalid->Meshes[0].Primitives[0].MaterialIndex=999;
    Rejects([&] {lv::ValidateModel(*invalid);},"common validation catches a foreign material before GPU upload");
    const auto fbx=registry.LoadModel(Fixture("Compatibility.fbx"));const auto rig=lv::InspectModel(*fbx);
    Check(fbx->Format == lv::AssetFormat::Fbx && rig.Skins == 1 && rig.Bones == 2 && rig.Animations == 1 && rig.Indices == 6,"FBX mesh, hierarchy, two-bone skin and animation layer import");
    Check(fbx->Materials[0].BaseColorTexture && fbx->Dependencies.size() == 2,"FBX external PNG texture is decoded and tracked as a dependency");
    Check(Near(fbx->ModelBounds.Min.X,1.5f) && Near(fbx->ModelBounds.Max.X,2.5f) && Near(fbx->ModelBounds.Max.Y,.5f),"FBX bind matrices preserve transformed mesh placement");
    lv::AnimationPlayer animation;animation.Bind(fbx);animation.Play(0);
    Check(Near(animation.GetBounds().Max.Y,fbx->ModelBounds.Max.Y) && Near(animation.GetBounds().Min.Y,fbx->ModelBounds.Min.Y),"FBX animation begins at its authored reference pose");animation.Seek(1);
    if (!Near(animation.GetBounds().Max.Y,1.5f)) {std::fprintf(stderr,"FBX diagnostic: duration=%g time=%g minY=%g maxY=%g\n",fbx->Animations[0].Duration,animation.Time(),animation.GetBounds().Min.Y,animation.GetBounds().Max.Y);for (const auto& track:fbx->Animations[0].Tracks) {std::fprintf(stderr,"  node=%s path=%d keys=%zu refY=%g firstY=%g lastY=%g\n",fbx->Nodes[track.Joint].Name.c_str(),int(track.Path),track.Times.size(),fbx->Skeleton.Joints[track.Joint].ReferenceLocal.Position.Y,track.Values.front()[1],track.Values.back()[1]);}}
    Check(Near(animation.GetBounds().Max.Y,1.5f) && Near(animation.GetBounds().Min.Y,-.5f),"FBX baked animation drives existing skin evaluation independently of the mesh node");
    const auto binary=registry.LoadModel(Fixture("CompatibilityBinary.fbx"));
    Check(binary->Format == lv::AssetFormat::Fbx && lv::InspectModel(*binary).Indices == 3,"FBX binary and generated normals use the common model path");
    lv::ImportSettings settings;settings.ImportAnimations=false;settings.ImportMaterials=false;
    const auto geometry=registry.LoadModel(Fixture("Compatibility.fbx"),settings);
    Check(geometry->Animations.empty() && geometry->Materials.size() == 1 && geometry->Skins.size() == 1,"animation/material import flags preserve skeleton/skinning");
    const auto gltfGeometry=registry.LoadModel(Fixture("AnimatedRibbon.gltf"),settings);Check(gltfGeometry->Animations.empty() && gltfGeometry->Materials.size() == 1 && gltfGeometry->Images.empty() && gltfGeometry->Skins.size() == 1,"glTF shares animation/material import flags with FBX");
    settings={};settings.MaxAnimationSamples=1;
    ImportRejects([&] {registry.LoadModel(Fixture("Compatibility.fbx"),settings);},lv::ImportErrorCode::ResourceLimit,"FBX bake obeys the animation key budget");
    settings={};settings.AnimationBakeRate=0;Rejects([&] {registry.LoadModel(Fixture("Compatibility.fbx"),settings);},"invalid FBX bake rate rejected");
    settings={};settings.GenerateMissingNormals=false;
    ImportRejects([&] {registry.LoadModel(Fixture("CompatibilityBinary.fbx"),settings);},lv::ImportErrorCode::InvalidData,"strict FBX normals option is enforced");
    TemporaryDirectory temp;
    for (const auto& [ticks,keys]:{std::pair{"1539538600",2u},std::pair{"1539538601",2u},std::pair{"32330310600",22u}}) {
        auto source=Read(Fixture("MorphProgressive.fbx"));while (source.find("46186158000")!=std::string::npos) {source=Replace(source,"46186158000",ticks);}source=Replace(source,"23093079000","769769300");
        const auto path=temp.File(ticks);Write(path.string()+".fbx",source);const auto fractional=registry.LoadModel(path.string()+".fbx");const auto& clip=fractional->Animations.at(0);
        Check(std::abs(clip.Duration-std::stod(ticks)/46186158000.0)<1e-12 && clip.Tracks[0].Times.back()<=clip.Duration && clip.Duration-clip.Tracks[0].Times.back()<1e-7 && clip.Tracks[0].Times.size()==keys && clip.MorphTracks.at(0).Times==clip.Tracks[0].Times,"fractional FBX duration is preserved with strictly increasing representable TRS/Morph keys");
        Check(Near(clip.MorphTracks[0].Values.back(),1) && std::any_of(clip.Tracks.begin(),clip.Tracks.end(),[](const auto& track){return track.Path==lv::AnimationPath::Translation && Near(track.Values.back()[1],2);}),"rounded FBX endpoint retains the authored TRS and progressive Morph values");
    }
    Write(temp.File("centimeters.fbx"),Replace(Read(Fixture("Compatibility.fbx")),"\"UnitScaleFactor\", \"double\", \"Number\", \"\",100","\"UnitScaleFactor\", \"double\", \"Number\", \"\",1"));
    const auto centimeters=registry.LoadModel(temp.File("centimeters.fbx"));
    Check(Near(centimeters->ModelBounds.Max.X,.025f) && Near(centimeters->ModelBounds.Max.Y,.005f),"FBX source units are converted to meters");
    Write(temp.File("broken.fbx"),"Kaydara FBX Binary");const auto prior=registry.Size();
    ImportRejects([&] {registry.LoadModel(temp.File("broken.fbx"));},lv::ImportErrorCode::InvalidData,"truncated FBX returns structured InvalidData");Check(registry.Size() == prior,"failed FBX import preserves the registry");
    Write(temp.File("flat.obj"),"v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n");
    const auto flat=registry.LoadModel(temp.File("flat.obj"));Check(Near(flat->Meshes[0].Primitives[0].Geometry.Vertices[0].Normal[2],1),"OBJ missing normals generate triangle normals");
    ImportRejects([&] {registry.LoadModel(temp.File("flat.obj"),settings);},lv::ImportErrorCode::InvalidData,"OBJ missing normals can be rejected");
    Write(temp.File("flat.obj"),Read(temp.File("flat.obj"))+"o Added\nf 1 2 3\n");const auto revised=registry.ReimportModel(flat->Id);
    Check(revised->Id == flat->Id && revised->Meshes.size() == 2 && !revised->Warnings.empty() && revised->Status == lv::AssetImportStatus::ReadyWithWarnings,"OBJ reimport preserves ID and warns about structural changes");
    Write(temp.File("bad.obj"),"v 0 0 0\nf 1 2 999\n");ImportRejects([&] {registry.LoadModel(temp.File("bad.obj"));},lv::ImportErrorCode::InvalidData,"malformed OBJ indices are rejected");
    Write(temp.File("source.custom"),"custom");registry.Importers().Register(".custom",[&](const auto&,const auto&) {return *flat;});
    const auto custom=registry.LoadModel(temp.File("source.custom"));Check(custom->Format == lv::AssetFormat::Custom && custom->Id!=flat->Id,"custom CPU importer uses common validation and independent identity");
    Rejects([&] {registry.Importers().Register(".OBJ",[](const auto&,const auto&) {return lv::ModelAsset{};});},"duplicate importer registration is explicit");
    const auto project=temp.File("project")/"assets";
    const auto package=lv::CopyModelToProject(preview,project);
    Check(package->SourcePath!=preview->SourcePath && package->ContentHash == preview->ContentHash && package->Dependencies.size() == preview->Dependencies.size() && std::filesystem::is_regular_file(package->SourcePath),"project copy preserves source, MTL and loaded texture layout");
    const auto copied=registry.LoadModel(package->SourcePath);Check(copied->MissingDependencies.size() == 1 && copied->Images.size() == 1,"copied OBJ remains independently importable with its diagnostics");
    auto changed=std::make_shared<lv::ModelAsset>(*flat);changed->ContentHash++;
    ImportRejects([&] {registry.AddPrepared(changed);},lv::ImportErrorCode::InvalidData,"stale preview cannot silently replace existing registered content");
}
} // namespace

void TestPbr() {
    TemporaryDirectory temp;auto json=nlohmann::json::parse(Read(Fixture("SharedBox.gltf")));auto& material=json["materials"][0];
    material["pbrMetallicRoughness"]["metallicFactor"]=.75;material["pbrMetallicRoughness"]["roughnessFactor"]=.25;material["pbrMetallicRoughness"]["metallicRoughnessTexture"]={{"index",0}};
    const auto path=temp.File("Pbr.gltf");Write(path,json.dump());lv::AssetRegistry assets;const auto model=assets.LoadModel(path);const auto& m=model->Materials[0];
    Check(m.Shading == lv::ShadingModel::Pbr && Near(m.MetallicFactor,.75f) && Near(m.RoughnessFactor,.25f) && m.MetallicRoughnessTexture == 0,"glTF PBR factors and MR texture reference imported");
    Check(model->Images.size() == 1 && !model->Images[0].Rgba.empty(),"base/MR texture share one decoded source image");
    auto invalid=m;invalid.RoughnessFactor=-.1f;Rejects([&]{lv::ValidateMaterial(invalid,*model);},"negative roughness rejected");invalid=m;invalid.MetallicFactor=std::numeric_limits<float>::quiet_NaN();Rejects([&]{lv::ValidateMaterial(invalid,*model);},"nonfinite metallic rejected");invalid=m;invalid.MetallicRoughnessTexture=999;Rejects([&]{lv::ValidateMaterial(invalid,*model);},"foreign MR texture rejected");
    json["materials"][0]["pbrMetallicRoughness"]["roughnessFactor"]=1.2;Write(temp.File("InvalidPbr.gltf"),json.dump());ImportRejects([&]{assets.LoadModel(temp.File("InvalidPbr.gltf"));},lv::ImportErrorCode::InvalidData,"out-of-range imported roughness rejected");
    json["materials"][0]["pbrMetallicRoughness"]["roughnessFactor"]=.25;json["materials"][0]["pbrMetallicRoughness"]["metallicRoughnessTexture"]["texCoord"]=1;Write(temp.File("InvalidUv.gltf"),json.dump());ImportRejects([&]{assets.LoadModel(temp.File("InvalidUv.gltf"));},lv::ImportErrorCode::UnsupportedFeature,"unsupported MR UV set rejected");
}
void TestMorphInputs() {
    TemporaryDirectory temp;lv::AssetRegistry assets;auto json=nlohmann::json::parse(Read(Fixture("MorphSparse.gltf")));json["meshes"][0]["primitives"][0]["attributes"].erase("NORMAL");const auto path=temp.File("FlatMorph.gltf");Write(path,json.dump());const auto flat=assets.LoadModel(path),original=assets.LoadModel(Fixture("MorphSparse.gltf"));const auto& primitive=flat->Meshes[0].Primitives[0];const auto point=original->Meshes[0].Primitives[0].Geometry.Vertices[1].Position;bool correct=primitive.Geometry.Vertices.size() == primitive.Geometry.Indices.size();
    for (std::size_t i=0;i<primitive.Geometry.Vertices.size();++i) {const auto& p=primitive.Geometry.Vertices[i].Position;const bool matching=p == point;correct &= primitive.MorphTargets[0].PositionDeltas[i].X == (matching ? 1.0f:0.0f);}
    Check(correct,"generated flat normals expand sparse morph deltas with the original vertex indices");
    json=nlohmann::json::parse(Read(Fixture("MorphRibbon.gltf")));auto& mesh=json["meshes"][0];const auto target=mesh["primitives"][0]["targets"][0];mesh["primitives"][0]["targets"]=nlohmann::json::array();mesh["weights"]=nlohmann::json::array();mesh["extras"]["targetNames"]=nlohmann::json::array();for (int i=0;i<9;++i) {mesh["primitives"][0]["targets"].push_back(target);mesh["weights"].push_back(0);mesh["extras"]["targetNames"].push_back("Target "+std::to_string(i));}json["nodes"][3]["weights"]=mesh["weights"];json.erase("animations");Write(temp.File("TooManyMorphs.gltf"),json.dump());ImportRejects([&]{assets.LoadModel(temp.File("TooManyMorphs.gltf"));},lv::ImportErrorCode::ResourceLimit,"GPU target limit overflow is rejected rather than truncated");
    json=nlohmann::json::parse(Read(Fixture("MorphRibbon.gltf")));json["animations"]=nlohmann::json::array({json["animations"][4]});Write(temp.File("MorphSampleLimit.gltf"),json.dump());lv::ImportSettings limited;limited.MaxAnimationSamples=5;ImportRejects([&]{assets.LoadModel(temp.File("MorphSampleLimit.gltf"),limited);},lv::ImportErrorCode::ResourceLimit,"morph scalar sample budget reports ResourceLimit");
    auto model=std::make_shared<lv::ModelAsset>(*original);model->Meshes[0].Primitives[0].MorphTargets[0].NormalDeltas={{0,0,1}};Rejects([&]{lv::ValidateModel(*model);},"mismatched morph channel length rejected");
}
int main(int argc,char** argv) {
    try {
        if (argc>1 && std::string_view(argv[1]) == "--compatibility") {TestCompatibility();return Finish();}
        TestImports();TestFailuresAndLimits();TestLargeIndices();TestCompatibility();TestPbr();TestMorphInputs();
        {
            TemporaryDirectory temp;const auto file=temp.File("reimport.gltf");const auto source=Read(Fixture("SharedBox.gltf"));Write(file,source);
            lv::AssetRegistry registry;const auto old=registry.LoadModel(file);Write(file,source+"\n");const auto fresh=registry.ReimportModel(old->Id);
            Check(fresh != old && fresh->Id == old->Id && fresh->ContentHash != old->ContentHash && registry.LoadModel(file) == fresh && registry.Size() == 1,"explicit reimport preserves ID and replaces cache without mutating retained handles");
            Write(file,"{broken}");ImportRejects([&]{registry.ReimportModel(old->Id);},lv::ImportErrorCode::InvalidData,"malformed reimport fails recoverably");
            Check(registry.Find(old->Id) == fresh && old->Meshes.size() == fresh->Meshes.size(),"failed reimport retains cached and previous immutable data");
        }
        TemporaryDirectory temp;auto source=Replace(Read(Fixture("SharedBox.gltf")),"\"name\": \"Checker\",","\"name\": \"Checker\", \"normalTexture\": {\"index\":0,\"scale\":0.4},");Write(temp.File("normal.gltf"),source);lv::AssetRegistry registry;auto model=registry.LoadModel(temp.File("normal.gltf"));
        Check(model->Materials[0].NormalTexture == 0 && Near(model->Materials[0].NormalScale,.4f) && model->Images[0].Width == 2,"normal texture import retains UV0 binding, scale and decoded linear source pixels");
        Write(temp.File("bad-normal.gltf"),Replace(source,"\"scale\":0.4","\"scale\":0.4,\"texCoord\":1"));ImportRejects([&]{registry.LoadModel(temp.File("bad-normal.gltf"));},lv::ImportErrorCode::UnsupportedFeature,"unsupported normal-map UV set is rejected explicitly");
    }
    catch (const std::exception& error) { Check(false, error.what()); }
    return Finish();
}
