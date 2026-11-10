#include "GltfImport.h"

#include <cgltf.h>
#include <stb_image.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>
#include <numeric>
#include <span>
#include <string_view>

namespace lv::detail {
namespace {

[[noreturn]] void Invalid(const std::string& message) { throw ImportError(ImportErrorCode::InvalidData, message); }
[[noreturn]] void Unsupported(const std::string& message) { throw ImportError(ImportErrorCode::UnsupportedFeature, message); }
[[noreturn]] void Limit(const std::string& message) { throw ImportError(ImportErrorCode::ResourceLimit, message); }

struct Context {
    ModelAsset& Model;
    std::size_t ResourceBytes = 0;
    std::size_t Vertices = 0;
    std::size_t Indices = 0;

    void Hash(std::span<const std::uint8_t> bytes) {
        for (const auto byte : bytes) { Model.ContentHash ^= byte; Model.ContentHash *= 1099511628211ULL; }
    }
    void Reserve(std::size_t count) {
        if (count > Model.Settings.MaxResourceBytes - ResourceBytes) { Limit("Model exceeds the resource byte budget."); }
        ResourceBytes += count;
    }
    std::vector<std::uint8_t> Read(const std::filesystem::path& path) {
        std::error_code error;
        const auto size = std::filesystem::file_size(path, error);
        if (error) { throw ImportError(ImportErrorCode::Io, "Cannot read model resource: " + PathText(path)); }
        if (size > Model.Settings.MaxFileBytes) { Limit("Model resource exceeds MaxFileBytes."); }
        Reserve(static_cast<std::size_t>(size));
        std::ifstream stream(path, std::ios::binary);
        std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
        if (!stream || !stream.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()))) {
            throw ImportError(ImportErrorCode::Io, "Failed to read model resource: " + PathText(path));
        }
        const auto canonical = std::filesystem::canonical(path, error);
        if (error) { throw ImportError(ImportErrorCode::Io, "Cannot resolve a model dependency."); }
        if (std::find(Model.Dependencies.begin(), Model.Dependencies.end(), canonical) == Model.Dependencies.end()) {
            Model.Dependencies.push_back(canonical);
        }
        Hash(bytes);
        return bytes;
    }
};

int Hex(char value) {
    if (value >= '0' && value <= '9') { return value - '0'; }
    if (value >= 'A' && value <= 'F') { return value - 'A' + 10; }
    if (value >= 'a' && value <= 'f') { return value - 'a' + 10; }
    return -1;
}

std::vector<std::uint8_t> ReadUri(Context& context, std::string_view uri) {
    if (uri.starts_with("data:")) {
        const auto comma = uri.find(',');
        if (comma == std::string_view::npos || !uri.substr(0, comma).ends_with(";base64")) {
            Unsupported("Only base64-encoded data URIs are supported.");
        }
        const auto encoded = uri.substr(comma + 1);
        if (encoded.size() % 4 != 0) { Invalid("Invalid base64 resource length."); }
        const auto padding = encoded.ends_with("==") ? 2u : encoded.ends_with("=") ? 1u : 0u;
        const auto size = encoded.size() / 4 * 3 - padding;
        if (size > context.Model.Settings.MaxFileBytes) { Limit("Data URI exceeds MaxFileBytes."); }
        context.Reserve(size);
        std::vector<std::uint8_t> bytes;
        bytes.reserve(size);
        constexpr std::string_view alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        for (std::size_t i = 0; i < encoded.size(); i += 4) {
            std::uint32_t word = 0;
            for (std::size_t k = 0; k < 4; ++k) {
                const char c = encoded[i + k];
                const auto value = alphabet.find(c);
                if (value == std::string_view::npos && !(c == '=' && i + k >= encoded.size() - padding)) {
                    Invalid("Invalid base64 resource character or padding.");
                }
                word = (word << 6) | (c == '=' ? 0 : static_cast<std::uint32_t>(value));
            }
            bytes.push_back(static_cast<std::uint8_t>(word >> 16));
            if (bytes.size() < size) { bytes.push_back(static_cast<std::uint8_t>(word >> 8)); }
            if (bytes.size() < size) { bytes.push_back(static_cast<std::uint8_t>(word)); }
        }
        context.Hash(bytes);
        return bytes;
    }

    std::string decoded;
    for (std::size_t i = 0; i < uri.size(); ++i) {
        char c = uri[i];
        if (c == '%') {
            if (i + 2 >= uri.size() || Hex(uri[i + 1]) < 0 || Hex(uri[i + 2]) < 0) { Invalid("Invalid URI escape."); }
            c = static_cast<char>(Hex(uri[i + 1]) * 16 + Hex(uri[i + 2]));
            i += 2;
        }
        if (c == '\0' || c == ':' || c == '?' || c == '#' || c == '\\') {
            Unsupported("Resources must use relative local paths or base64 data URIs; network/file schemes are not supported.");
        }
        decoded.push_back(c);
    }
    const auto relative = std::filesystem::path(std::u8string(decoded.begin(), decoded.end()));
    if (relative.empty() || relative.is_absolute() || relative.has_root_path()) { Unsupported("Resource URI must be a relative local path."); }
    return context.Read(context.Model.SourcePath.parent_path() / relative);
}

bool Fits(std::size_t offset, std::size_t count, std::size_t stride, std::size_t elementSize, std::size_t size) {
    return count > 0 && stride >= elementSize && elementSize > 0 && offset <= size &&
        elementSize <= size - offset && (count - 1) <= (size - offset - elementSize) / stride;
}

void ValidateStorage(const cgltf_data& data, const ImportSettings& settings) {
    for (std::size_t i = 0; i < data.buffer_views_count; ++i) {
        const auto& view = data.buffer_views[i];
        if (view.has_meshopt_compression) { Unsupported("Meshopt-compressed buffers are not supported in Phase 2."); }
        if (!view.buffer || !view.buffer->data || view.offset > view.buffer->size || view.size > view.buffer->size - view.offset) {
            Invalid("Buffer view lies outside its loaded buffer.");
        }
    }
    for (std::size_t i = 0; i < data.accessors_count; ++i) {
        const auto& accessor = data.accessors[i];
        const auto component = cgltf_component_size(accessor.component_type);
        const auto element = cgltf_calc_size(accessor.type, accessor.component_type);
        if (!accessor.count || !component || !element) { Invalid("Invalid accessor type or count."); }
        if (accessor.count > std::max(settings.MaxVertices, settings.MaxIndices)) { Limit("Accessor exceeds configured element limits."); }
        if (accessor.buffer_view &&
            (!Fits(accessor.offset, accessor.count, accessor.stride, element, accessor.buffer_view->size) ||
             accessor.stride % component || accessor.offset % component || accessor.buffer_view->offset % component)) {
            Invalid("Accessor range, stride or component alignment is invalid.");
        }
        if (!accessor.buffer_view && !accessor.is_sparse) { Invalid("Accessor has neither storage nor sparse values."); }
        if (accessor.is_sparse) {
            const auto& sparse = accessor.sparse;
            const auto indexSize = cgltf_component_size(sparse.indices_component_type);
            if (sparse.count > accessor.count || !sparse.indices_buffer_view || !sparse.values_buffer_view ||
                (sparse.indices_component_type != cgltf_component_type_r_8u && sparse.indices_component_type != cgltf_component_type_r_16u && sparse.indices_component_type != cgltf_component_type_r_32u) ||
                !Fits(sparse.indices_byte_offset, sparse.count, indexSize, indexSize, sparse.indices_buffer_view->size) ||
                !Fits(sparse.values_byte_offset, sparse.count, element, element, sparse.values_buffer_view->size) ||
                sparse.indices_byte_offset % indexSize || sparse.indices_buffer_view->offset % indexSize ||
                sparse.values_byte_offset % component || sparse.values_buffer_view->offset % component) {
                Invalid("Sparse accessor range or alignment is invalid.");
            }
            const auto* indices = cgltf_buffer_view_data(sparse.indices_buffer_view) + sparse.indices_byte_offset;
            std::uint32_t previous = 0;
            for (std::size_t k = 0; k < sparse.count; ++k) {
                std::uint32_t value = 0;
                std::memcpy(&value, indices + k * indexSize, indexSize);
                if (value >= accessor.count || (k && value <= previous)) { Invalid("Sparse indices must be increasing and in bounds."); }
                previous = value;
            }
        }
    }
}

const cgltf_accessor* Attribute(const cgltf_primitive& primitive, cgltf_attribute_type type, int index = 0) {
    const cgltf_accessor* result = nullptr;
    for (std::size_t i = 0; i < primitive.attributes_count; ++i) {
        const auto& attribute = primitive.attributes[i];
        if (attribute.type == type && attribute.index == index) {
            if (result) { Invalid("Duplicate primitive attribute."); }
            result = attribute.data;
        }
    }
    return result;
}

MeshData ImportGeometry(Context& context, const cgltf_primitive& primitive) {
    if (primitive.type != cgltf_primitive_type_triangles) { Unsupported("Phase 2 supports TRIANGLES primitives only."); }
    if (primitive.has_draco_mesh_compression || primitive.targets_count) { Unsupported("Draco compression and morph targets are not supported in Phase 2."); }
    const auto* position = Attribute(primitive, cgltf_attribute_type_position);
    const auto* normal = Attribute(primitive, cgltf_attribute_type_normal);
    const auto* tangent = Attribute(primitive, cgltf_attribute_type_tangent);
    const auto* color = Attribute(primitive, cgltf_attribute_type_color);
    const auto* uv = Attribute(primitive, cgltf_attribute_type_texcoord);
    const auto* joints = Attribute(primitive,cgltf_attribute_type_joints);
    const auto* weights = Attribute(primitive,cgltf_attribute_type_weights);
    for (std::size_t i=0;i<primitive.attributes_count;++i) {
        const auto& a = primitive.attributes[i];
        if ((a.type == cgltf_attribute_type_joints || a.type == cgltf_attribute_type_weights) && a.index != 0) { Unsupported("Only four influences (JOINTS_0/WEIGHTS_0) are supported; additional sets are rejected."); }
    }
    if (bool(joints) != bool(weights)) { Invalid("JOINTS_0 and WEIGHTS_0 must be provided together."); }
    if (joints && (joints->type != cgltf_type_vec4 || joints->normalized ||
        (joints->component_type != cgltf_component_type_r_8u && joints->component_type != cgltf_component_type_r_16u))) { Invalid("JOINTS_0 requires unsigned byte/short VEC4."); }
    if (weights && (weights->type != cgltf_type_vec4 || !((weights->component_type == cgltf_component_type_r_32f && !weights->normalized) ||
        ((weights->component_type == cgltf_component_type_r_8u || weights->component_type == cgltf_component_type_r_16u) && weights->normalized)))) { Invalid("WEIGHTS_0 requires floats or normalized unsigned byte/short VEC4."); }
    if (!position || position->type != cgltf_type_vec3 || position->component_type != cgltf_component_type_r_32f) {
        Invalid("Primitive requires a float VEC3 POSITION accessor.");
    }
    const auto count = position->count;
    if (position->normalized || (normal && normal->normalized)) { Invalid("Float position/normal accessors cannot be normalized."); }
    if (count > context.Model.Settings.MaxVertices - context.Vertices) { Limit("Model exceeds MaxVertices."); }
    if (normal && (normal->type != cgltf_type_vec3 || normal->component_type != cgltf_component_type_r_32f)) { Invalid("NORMAL must be a float VEC3."); }
    if (tangent && (tangent->type != cgltf_type_vec4 || tangent->component_type != cgltf_component_type_r_32f || tangent->normalized || tangent->count != count)) {Invalid("TANGENT requires a matching float VEC4 accessor.");}
    if (color && color->type != cgltf_type_vec3 && color->type != cgltf_type_vec4) { Invalid("COLOR_0 must be VEC3 or VEC4."); }
    if (uv && uv->type != cgltf_type_vec2) { Invalid("TEXCOORD_0 must be VEC2."); }
    for (const auto* accessor : {color, uv}) {
        if (accessor && !((accessor->component_type == cgltf_component_type_r_32f && !accessor->normalized) ||
            ((accessor->component_type == cgltf_component_type_r_8u || accessor->component_type == cgltf_component_type_r_16u) && accessor->normalized))) {
            Invalid("Color/UV accessors require floats or normalized unsigned bytes/shorts.");
        }
    }
    for (auto* accessor : {normal, color, uv, joints, weights}) {
        if (accessor && accessor->count != count) { Invalid("Primitive attribute counts differ."); }
    }
    MeshData mesh;
    mesh.Vertices.resize(count);
    for (std::size_t i = 0; i < count; ++i) {
        auto& vertex = mesh.Vertices[i];
        if (tangent) {
            if (!cgltf_accessor_read_float(tangent,i,vertex.Tangent.data(),4)) {Invalid("Cannot decode TANGENT.");}
            const auto length=std::hypot(double(vertex.Tangent[0]),double(vertex.Tangent[1]),double(vertex.Tangent[2]));
            if (!std::isfinite(length) || length == 0 || (vertex.Tangent[3] != 1 && vertex.Tangent[3] != -1)) {Invalid("TANGENT must have a nonzero direction and handedness +/-1.");}
            for (int k=0;k<3;++k) {vertex.Tangent[k]=static_cast<float>(vertex.Tangent[k]/length);}
            if (!normal) {vertex.Tangent[3]=0;}
        }
        if (joints) {
            float indices[4]{};
            if (!cgltf_accessor_read_float(joints,i,indices,4) || !cgltf_accessor_read_float(weights,i,vertex.Weights.data(),4)) { Invalid("Cannot decode joint attributes."); }
            double sum = 0;
            for (std::size_t k=0;k<4;++k) {
                if (!std::isfinite(indices[k]) || indices[k] < 0 || indices[k] >= MaxSkinJoints || std::floor(indices[k]) != indices[k] ||
                    !std::isfinite(vertex.Weights[k]) || vertex.Weights[k] < 0) { Invalid("Invalid joint index or negative/nonfinite weight."); }
                vertex.Joints[k] = static_cast<std::uint16_t>(indices[k]); sum += vertex.Weights[k];
            }
            if (!std::isfinite(sum) || sum <= 0) { Invalid("Joint weights must have a finite positive sum."); }
            for (auto& weight : vertex.Weights) { weight = static_cast<float>(weight/sum); }
        }
        vertex.Color = {1, 1, 1, 1};
        if (!cgltf_accessor_read_float(position, i, vertex.Position.data(), 3) ||
            (normal && !cgltf_accessor_read_float(normal, i, vertex.Normal.data(), 3)) ||
            (color && !cgltf_accessor_read_float(color, i, vertex.Color.data(), cgltf_num_components(color->type))) ||
            (uv && !cgltf_accessor_read_float(uv, i, vertex.TexCoord.data(), 2))) { Invalid("Could not decode mesh attributes."); }
        if (normal) {
            const double length = std::hypot(double(vertex.Normal[0]), double(vertex.Normal[1]), double(vertex.Normal[2]));
            if (!std::isfinite(length) || length == 0) { Invalid("Mesh has an invalid normal."); }
            for (auto& value : vertex.Normal) { value = static_cast<float>(value / length); }
        }
        for (float value : vertex.Color) { if (!std::isfinite(value) || value < 0 || value > 1) { Invalid("Vertex color is outside [0, 1]."); } }
    }
    const auto indexCount = primitive.indices ? primitive.indices->count : count;
    if (!indexCount || indexCount % 3) { Invalid("TRIANGLES require a nonempty index count divisible by three."); }
    if (indexCount > context.Model.Settings.MaxIndices - context.Indices) { Limit("Model exceeds MaxIndices."); }
    mesh.Indices.resize(indexCount);
    if (primitive.indices) {
        if (primitive.indices->type != cgltf_type_scalar || primitive.indices->normalized ||
            (primitive.indices->component_type != cgltf_component_type_r_8u && primitive.indices->component_type != cgltf_component_type_r_16u && primitive.indices->component_type != cgltf_component_type_r_32u)) {
            Invalid("Indices require unsigned scalar components.");
        }
        for (std::size_t i = 0; i < indexCount; ++i) {
            const auto index = cgltf_accessor_read_index(primitive.indices, i);
            if (index >= count) { Invalid("Primitive index is out of range."); }
            mesh.Indices[i] = static_cast<std::uint32_t>(index);
        }
    } else { std::iota(mesh.Indices.begin(), mesh.Indices.end(), 0u); }
    if (primitive.material && (primitive.material->pbr_metallic_roughness.base_color_texture.texture || primitive.material->normal_texture.texture) && !uv) {
        Invalid("Textured primitive requires TEXCOORD_0.");
    }
    if (!normal) {
        if (!context.Model.Settings.GenerateMissingNormals) { Invalid("Mesh has no NORMAL; enable GenerateMissingNormals."); }
        if (indexCount > context.Model.Settings.MaxVertices - context.Vertices) { Limit("Generated flat normals exceed MaxVertices."); }
        std::vector<Vertex> flat;
        flat.reserve(indexCount);
        for (std::size_t i = 0; i < indexCount; i += 3) {
            auto a = mesh.Vertices[mesh.Indices[i]];
            auto b = mesh.Vertices[mesh.Indices[i + 1]];
            auto c = mesh.Vertices[mesh.Indices[i + 2]];
            const Vec3 ab{b.Position[0]-a.Position[0], b.Position[1]-a.Position[1], b.Position[2]-a.Position[2]};
            const Vec3 ac{c.Position[0]-a.Position[0], c.Position[1]-a.Position[1], c.Position[2]-a.Position[2]};
            const double x = double(ab.Y)*ac.Z - double(ab.Z)*ac.Y;
            const double y = double(ab.Z)*ac.X - double(ab.X)*ac.Z;
            const double z = double(ab.X)*ac.Y - double(ab.Y)*ac.X;
            const double length = std::hypot(x, y, z);
            if (!std::isfinite(length) || length == 0) { Invalid("Cannot generate a normal for a degenerate triangle."); }
            a.Normal = b.Normal = c.Normal = {static_cast<float>(x/length), static_cast<float>(y/length), static_cast<float>(z/length)};
            flat.insert(flat.end(), {a, b, c});
        }
        mesh.Vertices = std::move(flat);
        std::iota(mesh.Indices.begin(), mesh.Indices.end(), 0u);
    }
    try { ValidateMesh(mesh); } catch (const std::invalid_argument& error) { Invalid(error.what()); }
    context.Vertices += mesh.Vertices.size();
    context.Indices += mesh.Indices.size();
    return mesh;
}

TextureWrap Wrap(cgltf_wrap_mode mode) {
    switch (mode) {
    case cgltf_wrap_mode_repeat: return TextureWrap::Repeat;
    case cgltf_wrap_mode_clamp_to_edge: return TextureWrap::ClampToEdge;
    case cgltf_wrap_mode_mirrored_repeat: return TextureWrap::MirroredRepeat;
    default: Invalid("Invalid texture wrap mode.");
    }
}

void ImportMaterials(Context& context, const cgltf_data& data) {
    auto& model = context.Model;
    // Keep source texture/image indices, decode only base-color images in use.
    model.Images.resize(data.images_count);
    model.Textures.resize(data.textures_count);
    std::vector<bool> decoded(data.images_count, false);
    for (std::size_t i = 0; i < data.materials_count; ++i) {
        const auto& source = data.materials[i];
        MaterialAsset material;
        if (source.name) { material.Name = source.name; }
        material.DoubleSided = source.double_sided != 0;
        material.Unlit = source.unlit != 0;
        if (source.alpha_mode == cgltf_alpha_mode_blend) { Unsupported("Alpha BLEND materials require a later transparency pass."); }
        material.AlphaCutoff = source.alpha_mode == cgltf_alpha_mode_mask ? source.alpha_cutoff : -1.0f;
        if (!std::isfinite(material.AlphaCutoff) || material.AlphaCutoff > 1 ||
            (source.alpha_mode == cgltf_alpha_mode_mask && material.AlphaCutoff < 0)) { Invalid("Invalid alpha cutoff."); }
        if (source.has_pbr_metallic_roughness) {
            std::copy_n(source.pbr_metallic_roughness.base_color_factor, 4, material.BaseColor.begin());
        }
        for (float value : material.BaseColor) { if (!std::isfinite(value) || value < 0 || value > 1) { Invalid("Invalid base color factor."); } }
        material.NormalScale=source.normal_texture.texture ? source.normal_texture.scale:1.0f;
        if (!std::isfinite(material.NormalScale) || std::abs(material.NormalScale) > 16) {Invalid("Normal texture scale must be finite and in [-16,16].");}
        for (int role=0;role<2;++role) {
        const auto& view = role == 0 ? source.pbr_metallic_roughness.base_color_texture:source.normal_texture;
        if (view.texture) {
            if (view.texcoord != 0 || view.has_transform) { Unsupported("Material textures require TEXCOORD_0 without KHR_texture_transform."); }
            const auto& sourceTexture = *view.texture;
            if (!sourceTexture.image) { Unsupported("Texture has no PNG/JPEG fallback image."); }
            const auto imageIndex = cgltf_image_index(&data, sourceTexture.image);
            const auto textureIndex = cgltf_texture_index(&data, view.texture);
            auto& texture = model.Textures[textureIndex];
            texture.ImageIndex = imageIndex;
            if (const auto* sampler = sourceTexture.sampler) {
                texture.WrapU = Wrap(sampler->wrap_s);
                texture.WrapV = Wrap(sampler->wrap_t);
                switch (sampler->min_filter) {
                case cgltf_filter_type_undefined: break;
                case cgltf_filter_type_nearest: texture.UseMipmaps = false; texture.MinFilter = TextureFilter::Nearest; break;
                case cgltf_filter_type_linear: texture.UseMipmaps = false; break;
                case cgltf_filter_type_nearest_mipmap_nearest: texture.MinFilter = TextureFilter::Nearest; texture.MipFilter = TextureFilter::Nearest; break;
                case cgltf_filter_type_linear_mipmap_nearest: texture.MipFilter = TextureFilter::Nearest; break;
                case cgltf_filter_type_nearest_mipmap_linear: texture.MinFilter = TextureFilter::Nearest; break;
                case cgltf_filter_type_linear_mipmap_linear: break;
                default: Invalid("Invalid texture minification filter.");
                }
                if (sampler->mag_filter == cgltf_filter_type_nearest) { texture.MagFilter = TextureFilter::Nearest; }
                else if (sampler->mag_filter != cgltf_filter_type_undefined && sampler->mag_filter != cgltf_filter_type_linear) { Invalid("Invalid texture magnification filter."); }
            }
            if (!role) {material.BaseColorTexture=textureIndex;}else {material.NormalTexture=textureIndex;}
            if (!decoded[imageIndex]) {
                const auto& image = *sourceTexture.image;
                std::vector<std::uint8_t> encoded;
                if (image.uri) { encoded = ReadUri(context, image.uri); }
                else if (image.buffer_view) {
                    const auto* bytes = cgltf_buffer_view_data(image.buffer_view);
                    if (image.buffer_view->size > model.Settings.MaxFileBytes) { Limit("Encoded image exceeds MaxFileBytes."); }
                    encoded.assign(bytes, bytes + image.buffer_view->size);
                } else { Invalid("Image has no URI or buffer view."); }
                if (encoded.empty() || encoded.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) { Invalid("Invalid encoded image size."); }
                int width = 0, height = 0, channels = 0;
                const auto byteCount = static_cast<int>(encoded.size());
                if (!stbi_info_from_memory(encoded.data(), byteCount, &width, &height, &channels)) { Invalid("Image is not a supported PNG/JPEG."); }
                if (width <= 0 || height <= 0 || width > model.Settings.MaxImageDimension || height > model.Settings.MaxImageDimension) { Limit("Texture exceeds MaxImageDimension."); }
                const auto decodedSize = static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4;
                context.Reserve(decodedSize);
                std::unique_ptr<stbi_uc, decltype(&stbi_image_free)> pixels(stbi_load_from_memory(encoded.data(), byteCount, &width, &height, &channels, 4), stbi_image_free);
                if (!pixels) { Invalid("PNG/JPEG decoding failed."); }
                model.Images[imageIndex] = {width, height, {pixels.get(), pixels.get() + decodedSize}};
                decoded[imageIndex] = true;
            }
        }
        }
        model.Materials.push_back(std::move(material));
    }
    model.Materials.push_back({}); // Default material, used when a primitive omits it.
}

void ImportNodes(Context& context, const cgltf_data& data) {
    auto& model = context.Model;
    if (data.nodes_count > model.Settings.MaxNodes) { Limit("Model exceeds MaxNodes."); }
    model.Nodes.resize(data.nodes_count);
    model.Skeleton.Joints.resize(data.nodes_count);
    for (std::size_t i = 0; i < data.nodes_count; ++i) {
        const auto& source = data.nodes[i];
        auto& node = model.Nodes[i];
        if (source.has_mesh_gpu_instancing) { Unsupported("EXT_mesh_gpu_instancing is not supported."); }
        if (source.has_matrix && (source.has_translation || source.has_rotation || source.has_scale)) { Invalid("Node cannot specify both matrix and TRS."); }
        if (source.name) { node.Name = source.name; }
        if (source.parent) { node.Parent = cgltf_node_index(&data, source.parent); }
        if (source.mesh) { node.MeshIndex = cgltf_mesh_index(&data, source.mesh); }
        if (source.skin) { node.SkinIndex = cgltf_skin_index(&data,source.skin); if (!source.mesh) { Invalid("A skinned node must reference a mesh."); } }
        auto& joint = model.Skeleton.Joints[i]; joint.Name = node.Name;
        joint.MatrixAuthored = source.has_matrix != 0;
        joint.Parent = node.Parent ? static_cast<JointId>(*node.Parent) : NoJoint;
        try {
            if (source.has_matrix) {
                std::copy_n(source.matrix, 16, node.LocalTransform.Elements.begin());
                try { joint.ReferenceLocal = DecomposeTRS(node.LocalTransform); }
                catch (const std::invalid_argument&) { joint.Editable = false; }
            }
            else {
                Transform transform;
                if (source.has_translation) { transform.Position = {source.translation[0], source.translation[1], source.translation[2]}; }
                if (source.has_rotation) { transform.Rotation = {source.rotation[0], source.rotation[1], source.rotation[2], source.rotation[3]}; }
                if (source.has_scale) { transform.Scale = {source.scale[0], source.scale[1], source.scale[2]}; }
                node.LocalTransform = transform.Matrix();
                joint.ReferenceLocal = transform;
            }
            (void)node.LocalTransform.NormalMatrix();
            joint.ReferenceMatrix = node.LocalTransform;
        } catch (const std::invalid_argument& error) { Invalid(std::string("Invalid node transform: ") + error.what()); }
    }
    std::vector<std::size_t> roots;
    const auto* scene = data.scene ? data.scene : data.scenes_count ? &data.scenes[0] : nullptr;
    if (scene) {
        for (std::size_t i = 0; i < scene->nodes_count; ++i) { roots.push_back(cgltf_node_index(&data, scene->nodes[i])); }
    } else {
        for (std::size_t i = 0; i < model.Nodes.size(); ++i) { if (!model.Nodes[i].Parent) { roots.push_back(i); } }
    }
    struct Pending { std::size_t Index; Mat4 Parent; };
    std::vector<Pending> stack;
    for (auto it = roots.rbegin(); it != roots.rend(); ++it) { stack.push_back({*it, Mat4::Identity()}); }
    std::vector<bool> visited(data.nodes_count, false);
    bool hasBounds = false;
    while (!stack.empty()) {
        auto [index, parent] = stack.back();
        stack.pop_back();
        if (visited[index]) { Invalid("Selected scene contains a repeated or cyclic node."); }
        visited[index] = true;
        const auto& node = model.Nodes[index];
        const auto world = parent * node.LocalTransform;
        try { (void)world.NormalMatrix(); } catch (const std::invalid_argument& error) { Invalid(error.what()); }
        if (node.MeshIndex) {
            model.Parts.push_back({*node.MeshIndex, world,static_cast<JointId>(index),node.SkinIndex});
            for (const auto& primitive : model.Meshes[*node.MeshIndex].Primitives) {
                for (const auto& vertex : primitive.Geometry.Vertices) {
                    const auto point = world.TransformPoint({vertex.Position[0], vertex.Position[1], vertex.Position[2]});
                    if (!std::isfinite(point.X) || !std::isfinite(point.Y) || !std::isfinite(point.Z)) { Invalid("Transformed bounds exceed float range."); }
                    if (!hasBounds) { model.ModelBounds = {point, point}; hasBounds = true; }
                    auto& low = model.ModelBounds.Min;
                    auto& high = model.ModelBounds.Max;
                    low = {std::min(low.X, point.X), std::min(low.Y, point.Y), std::min(low.Z, point.Z)};
                    high = {std::max(high.X, point.X), std::max(high.Y, point.Y), std::max(high.Z, point.Z)};
                }
            }
        }
        const auto& source = data.nodes[index];
        for (std::size_t k = source.children_count; k > 0; --k) { stack.push_back({cgltf_node_index(&data, source.children[k - 1]), world}); }
    }
    if (!hasBounds) { Invalid("Selected glTF scene contains no renderable geometry."); }
}

void ImportRig(Context& context, const cgltf_data& data) {
    auto& model = context.Model;
    if (data.skins_count > 256 || data.animations_count > 256) { Limit("At most 256 skins/clips per model are supported."); }
    for (std::size_t i=0;i<data.skins_count;++i) {
        const auto& source = data.skins[i]; SkinAsset skin;
        if (source.name) { skin.Name = source.name; }
        if (!source.joints_count) { Invalid("Skin has no joints."); }
        if (source.joints_count > MaxSkinJoints) { Limit("Skin exceeds the 128-joint GPU palette limit."); }
        const auto* inverse = source.inverse_bind_matrices;
        if (inverse && (inverse->count < source.joints_count || inverse->type != cgltf_type_mat4 || inverse->component_type != cgltf_component_type_r_32f || inverse->normalized)) { Invalid("Inverse bind matrices require at least one float MAT4 per joint."); }
        context.Reserve(source.joints_count*(sizeof(JointId)+sizeof(Mat4)));
        for (std::size_t k=0;k<source.joints_count;++k) {
            if (!source.joints[k]) { Invalid("Missing skin joint."); }
            const auto joint = static_cast<JointId>(cgltf_node_index(&data,source.joints[k]));
            if (std::find(skin.Joints.begin(),skin.Joints.end(),joint) != skin.Joints.end()) { Invalid("Skin has duplicate joints."); }
            skin.Joints.push_back(joint); model.Skeleton.Joints.at(joint).IsJoint = true;
            auto matrix = Mat4::Identity();
            if (inverse && !cgltf_accessor_read_float(inverse,k,matrix.Elements.data(),16)) { Invalid("Cannot read inverse bind matrix."); }
            try { (void)matrix.NormalMatrix(); } catch (const std::invalid_argument& e) { Invalid(e.what()); }
            skin.InverseBindMatrices.push_back(matrix);
        }
        model.Skins.push_back(std::move(skin));
    }
    for (const auto& node : model.Nodes) {
        if (!node.SkinIndex) { continue; }
        const auto& skin = model.Skins.at(*node.SkinIndex);
        for (const auto& primitive : model.Meshes.at(*node.MeshIndex).Primitives) {
            if (!primitive.HasSkinning) { Invalid("Every primitive of a skinned mesh needs JOINTS_0 and WEIGHTS_0."); }
            for (const auto& vertex : primitive.Geometry.Vertices) {
                for (auto joint : vertex.Joints) { if (joint >= skin.Joints.size()) { Invalid("Vertex joint index exceeds the node's skin palette."); } }
            }
        }
    }
    try { ValidateSkeleton(model.Skeleton); } catch (const std::invalid_argument& e) { Invalid(e.what()); }
    model.Skeleton.Signature = SkeletonSignature(model.Skeleton);
    for (std::size_t i=0;i<data.animations_count;++i) {
        const auto& source = data.animations[i]; AnimationClip clip;
        clip.Name = source.name ? source.name : "Animation " + std::to_string(i);
        if (source.channels_count > model.Nodes.size()*3) { Limit("Animation exceeds the per-node track limit."); }
        for (std::size_t k=0;k<source.channels_count;++k) {
            const auto& channel = source.channels[k]; const auto* sampler = channel.sampler;
            if (!channel.target_node) { Invalid("Animation channel has no target node."); }
            if (!sampler || !sampler->input || !sampler->output) { Invalid("Animation channel has an incomplete sampler."); }
            if (sampler->interpolation != cgltf_interpolation_type_linear && sampler->interpolation != cgltf_interpolation_type_step) { Unsupported("CUBICSPLINE animation interpolation is deferred; use STEP or LINEAR."); }
            if (channel.target_path == cgltf_animation_path_type_weights) { Unsupported("Morph-weight animation is not supported."); }
            if (channel.target_node->has_matrix) { Invalid("glTF animated nodes must use TRS, not matrix."); }
            AnimationTrack track;
            track.Joint = static_cast<JointId>(cgltf_node_index(&data,channel.target_node));
            switch (channel.target_path) {
            case cgltf_animation_path_type_translation: track.Path = AnimationPath::Translation; break;
            case cgltf_animation_path_type_rotation: track.Path = AnimationPath::Rotation; break;
            case cgltf_animation_path_type_scale: track.Path = AnimationPath::Scale; break;
            default: Unsupported("Unknown animation channel path.");
            }
            track.Interpolation = sampler->interpolation == cgltf_interpolation_type_step ? AnimationInterpolation::Step : AnimationInterpolation::Linear;
            const auto& input = *sampler->input; const auto& output = *sampler->output;
            if (input.type != cgltf_type_scalar || input.component_type != cgltf_component_type_r_32f || input.normalized || !input.count ||
                output.count != input.count || output.type != (track.Path == AnimationPath::Rotation ? cgltf_type_vec4 : cgltf_type_vec3) || output.component_type != cgltf_component_type_r_32f || output.normalized) { Invalid("Animation requires float scalar times and matching float VEC3/VEC4 values."); }
            context.Reserve(input.count*(sizeof(float)+sizeof(std::array<float,4>)));
            track.Times.resize(input.count); track.Values.resize(input.count);
            for (std::size_t key=0;key<input.count;++key) {
                if (!cgltf_accessor_read_float(&input,key,&track.Times[key],1) || !cgltf_accessor_read_float(&output,key,track.Values[key].data(),cgltf_num_components(output.type))) { Invalid("Cannot read animation samples."); }
            }
            clip.Duration = std::max(clip.Duration,double(track.Times.back()));
            clip.Tracks.push_back(std::move(track));
        }
        try { ValidateAnimation(model.Skeleton,clip); } catch (const std::invalid_argument& e) { Invalid(e.what()); }
        model.Animations.push_back(std::move(clip));
    }
}

} // namespace

std::shared_ptr<ModelAsset> ImportGltf(const std::filesystem::path& path, const ImportSettings& settings) {
    if (!settings.MaxFileBytes || !settings.MaxResourceBytes || !settings.MaxVertices || !settings.MaxIndices ||
        !settings.MaxNodes || settings.MaxImageDimension <= 0 || settings.MaxImageDimension > 32768 ||
        settings.MaxVertices >= std::numeric_limits<std::uint32_t>::max() || settings.MaxIndices > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        throw std::invalid_argument("Invalid glTF import limits.");
    }
    auto model = std::make_shared<ModelAsset>();
    model->SourcePath = path;
    model->Settings = settings;
    model->ContentHash = 14695981039346656037ULL;
    Context context{*model};
    const auto source = context.Read(path);
    cgltf_options options{};
    cgltf_data* parsed = nullptr;
    const auto parseResult = cgltf_parse(&options, source.data(), source.size(), &parsed);
    if (parseResult != cgltf_result_success) { Invalid("Cannot parse glTF 2.0/GLB (cgltf error " + std::to_string(parseResult) + ")."); }
    std::unique_ptr<cgltf_data, decltype(&cgltf_free)> data(parsed, cgltf_free);
    if (!data->asset.version || std::string_view(data->asset.version) != "2.0") { Unsupported("Only glTF 2.0 is supported."); }
    for (std::size_t i = 0; i < data->extensions_required_count; ++i) {
        if (std::string_view(data->extensions_required[i]) != "KHR_materials_unlit") { Unsupported(std::string("Unsupported required extension: ") + data->extensions_required[i]); }
    }
    if (data->nodes_count > settings.MaxNodes) { Limit("Model exceeds MaxNodes."); }
    std::vector<std::vector<std::uint8_t>> buffers(data->buffers_count);
    for (std::size_t i = 0; i < data->buffers_count; ++i) {
        auto& buffer = data->buffers[i];
        if (!buffer.size || buffer.size > settings.MaxResourceBytes) { Limit("Invalid or excessive buffer length."); }
        if (buffer.uri) {
            buffers[i] = ReadUri(context, buffer.uri);
            if (buffers[i].size() < buffer.size) { Invalid("Buffer resource is shorter than byteLength."); }
            buffer.data = buffers[i].data();
        } else if (i == 0 && data->bin && data->bin_size >= buffer.size) { buffer.data = const_cast<void*>(data->bin); }
        else { Invalid("Buffer has no data or the GLB BIN chunk is truncated."); }
        buffer.data_free_method = cgltf_data_free_method_none;
    }
    ValidateStorage(*data, settings);
    if (cgltf_validate(data.get()) != cgltf_result_success) { Invalid("glTF validation failed (accessors, indices, hierarchy or scenes)."); }
    ImportMaterials(context, *data);
    for (std::size_t i = 0; i < data->meshes_count; ++i) {
        const auto& sourceMesh = data->meshes[i];
        MeshAsset mesh;
        if (sourceMesh.name) { mesh.Name = sourceMesh.name; }
        for (std::size_t k = 0; k < sourceMesh.primitives_count; ++k) {
            const auto& primitive = sourceMesh.primitives[k];
            mesh.Primitives.push_back({ImportGeometry(context, primitive), primitive.material ? cgltf_material_index(data.get(), primitive.material) : data->materials_count,Attribute(primitive,cgltf_attribute_type_joints) != nullptr});
        }
        if (mesh.Primitives.empty()) { Invalid("Mesh has no primitives."); }
        model->Meshes.push_back(std::move(mesh));
    }
    ImportNodes(context, *data);
    ImportRig(context,*data);
    if (!model->Skins.empty()) {
        try { AnimationPlayer reference; reference.Bind(model); model->ModelBounds = reference.GetBounds(); }
        catch (const std::invalid_argument& e) { Invalid(e.what()); }
    }
    model->Warnings.push_back("Base-color diffuse/unlit shading; advanced PBR, normal and emissive maps are not evaluated.");
    return model;
}

} // namespace lv::detail
