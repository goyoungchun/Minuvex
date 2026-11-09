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
    const auto* color = Attribute(primitive, cgltf_attribute_type_color);
    const auto* uv = Attribute(primitive, cgltf_attribute_type_texcoord);
    if (!position || position->type != cgltf_type_vec3 || position->component_type != cgltf_component_type_r_32f) {
        Invalid("Primitive requires a float VEC3 POSITION accessor.");
    }
    const auto count = position->count;
    if (position->normalized || (normal && normal->normalized)) { Invalid("Float position/normal accessors cannot be normalized."); }
    if (count > context.Model.Settings.MaxVertices - context.Vertices) { Limit("Model exceeds MaxVertices."); }
    if (normal && (normal->type != cgltf_type_vec3 || normal->component_type != cgltf_component_type_r_32f)) { Invalid("NORMAL must be a float VEC3."); }
    if (color && color->type != cgltf_type_vec3 && color->type != cgltf_type_vec4) { Invalid("COLOR_0 must be VEC3 or VEC4."); }
    if (uv && uv->type != cgltf_type_vec2) { Invalid("TEXCOORD_0 must be VEC2."); }
    for (const auto* accessor : {color, uv}) {
        if (accessor && !((accessor->component_type == cgltf_component_type_r_32f && !accessor->normalized) ||
            ((accessor->component_type == cgltf_component_type_r_8u || accessor->component_type == cgltf_component_type_r_16u) && accessor->normalized))) {
            Invalid("Color/UV accessors require floats or normalized unsigned bytes/shorts.");
        }
    }
    for (auto* accessor : {normal, color, uv}) {
        if (accessor && accessor->count != count) { Invalid("Primitive attribute counts differ."); }
    }
    MeshData mesh;
    mesh.Vertices.resize(count);
    for (std::size_t i = 0; i < count; ++i) {
        auto& vertex = mesh.Vertices[i];
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
    if (primitive.material && primitive.material->pbr_metallic_roughness.base_color_texture.texture && !uv) {
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
        const auto& view = source.pbr_metallic_roughness.base_color_texture;
        if (view.texture) {
            if (view.texcoord != 0 || view.has_transform) { Unsupported("Phase 2 base-color textures require TEXCOORD_0 without KHR_texture_transform."); }
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
            material.BaseColorTexture = textureIndex;
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
        model.Materials.push_back(std::move(material));
    }
    model.Materials.push_back({}); // Default material, used when a primitive omits it.
}

void ImportNodes(Context& context, const cgltf_data& data) {
    auto& model = context.Model;
    if (data.nodes_count > model.Settings.MaxNodes) { Limit("Model exceeds MaxNodes."); }
    model.Nodes.resize(data.nodes_count);
    for (std::size_t i = 0; i < data.nodes_count; ++i) {
        const auto& source = data.nodes[i];
        auto& node = model.Nodes[i];
        if (source.skin || source.has_mesh_gpu_instancing) { Unsupported("Skins and EXT_mesh_gpu_instancing are not supported in Phase 2."); }
        if (source.has_matrix && (source.has_translation || source.has_rotation || source.has_scale)) { Invalid("Node cannot specify both matrix and TRS."); }
        if (source.name) { node.Name = source.name; }
        if (source.parent) { node.Parent = cgltf_node_index(&data, source.parent); }
        if (source.mesh) { node.MeshIndex = cgltf_mesh_index(&data, source.mesh); }
        try {
            if (source.has_matrix) { std::copy_n(source.matrix, 16, node.LocalTransform.Elements.begin()); }
            else {
                Transform transform;
                if (source.has_translation) { transform.Position = {source.translation[0], source.translation[1], source.translation[2]}; }
                if (source.has_rotation) { transform.Rotation = {source.rotation[0], source.rotation[1], source.rotation[2], source.rotation[3]}; }
                if (source.has_scale) { transform.Scale = {source.scale[0], source.scale[1], source.scale[2]}; }
                node.LocalTransform = transform.Matrix();
            }
            (void)node.LocalTransform.NormalMatrix();
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
            model.Parts.push_back({*node.MeshIndex, world});
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
            mesh.Primitives.push_back({ImportGeometry(context, primitive), primitive.material ? cgltf_material_index(data.get(), primitive.material) : data->materials_count});
        }
        if (mesh.Primitives.empty()) { Invalid("Mesh has no primitives."); }
        model->Meshes.push_back(std::move(mesh));
    }
    ImportNodes(context, *data);
    if (data->animations_count) { model->Warnings.push_back("Animation clips are not evaluated in Phase 2; the default node transforms are displayed."); }
    model->Warnings.push_back("Phase 2 uses base-color diffuse/unlit shading; advanced PBR, normal and emissive maps are not evaluated.");
    return model;
}

} // namespace lv::detail
