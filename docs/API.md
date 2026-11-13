# Minuvex API

**C++20 Graphics SDK · Version 1.11.0 · Namespace `lv`**

모듈별 주요 타입과 API를 정리한 사용 안내임. 전체 선언은 연결된 공개 헤더에서 확인할 수 있음.

## SDK 연결

```cmake
find_package(Minuvex 1.11 CONFIG REQUIRED COMPONENTS viewer graphics)
add_executable(my_viewer main.cpp)
target_link_libraries(my_viewer PRIVATE minuvex::viewer)
```

필요한 모듈을 `minuvex::<모듈명>`으로 연결함. CPU 도구는 `assets`·`animation`·`scene_io`, Host 통합은 `embedded`, 편집 상태 관리는 `editor_document` 등을 사용함. `graphics`는 그래픽 SDK 여부를 검사하는 component임.

## 모듈별 주요 API

| 모듈 / 헤더 | 주요 타입·함수 |
|---|---|
| **core** · [Math.h](../include/minuvex/Math.h), [Mesh.h](../include/minuvex/Mesh.h) | `Vec3`, `Quaternion`, `Mat4`, `Transform`, `Bounds`, `MeshData`, `MakeCube()`, `DecomposeTRS()`. 생성 헤더 `Version.h`의 `Version`, `BuildInfo`, `GetBuildInfo()` |
| **assets** · [Asset.h](../include/minuvex/Asset.h), [AssetLoading.h](../include/minuvex/AssetLoading.h) | `ModelHandle`, `MaterialAsset`, `ShadingModel`, `ImportSettings`, `ImporterRegistry`, `AssetRegistry::LoadModel/PrepareModel/ReimportModel`, `InspectModel()`, `ValidateModel()`, `CopyModelToProject()`, `AssetLoader`, `AssetHotReload` |
| **scene** · [Scene.h](../include/minuvex/Scene.h) | `Scene`, `ObjectId`, `Camera`, `DirectionalLight`, `PointLight`, `SpotLight`. `AddModel()`, `CreateObject()`, `SetParent()`, `SetTransform()`, `SetModel()`, `GetMaterial/SetMaterial/ResetMaterial`, `GetAnimation()`, `UpdateAnimations()` |
| **animation** · [Animation.h](../include/minuvex/Animation.h) | `SkeletonAsset`, `SkinAsset`, `PoseAsset`, `AnimationClip`, `AnimationBlendState`, `AnimationPlayer`. `Bind()`, `Play()`, `CrossFade()`, `Pause()`, `Resume()`, `Stop()`, `Seek()`, `SetSpeed()`, `SetLoop()`, `Evaluate()`, `CapturePose()` |
| **scene_io** · [SceneIO.h](../include/minuvex/SceneIO.h), [PoseIO.h](../include/minuvex/PoseIO.h) | `SceneArchive`, `SaveScene(scene, path, library={})`, `LoadScene(path, registry)`, `SavePose(pose, skeleton, path)`, `LoadPose(path, skeleton)` |
| **viewer** · [Viewer.h](../include/minuvex/Viewer.h) | `ViewerOptions`, `ModelOptions`, `ViewerRunOptions`, `Viewer`. `AddModel()`, `AddModelAsync()`, `GetAssetLoad()`, `CommitModelLoad()`, `SetHotReload()`, `SetCamera()`, `SetMaterial/ResetMaterial`, `SetQuality()`, `CrossFadeAnimation()`, `GetScene()`, `Run()` |
| **embedded** · [EmbeddedRenderer.h](../include/minuvex/EmbeddedRenderer.h) | `HostGraphicsContext`, `HostRenderTarget`, `EmbeddedFrameOptions`, `EmbeddedRenderer`. `Initialize()`, `Update()`, `Render()`, `WaitIdle()`, `DetachTarget()`, `Shutdown()` |
| **editor_document** · [EditorDocument.h](../include/minuvex/EditorDocument.h) | `EditorDocument`는 Editor의 Scene·선택·미저장 상태 관리 모델. `OpenModel()`, `Open()`, `Save()`, `ImportModel()`, `PlaceModel()`, `SetWorldTransform()`, `SetMaterial/ResetMaterial`, `CrossFadeAnimation()`, `BeginEdit/CommitEdit/CancelEdit`, `Undo/Redo`, `ClearUndoHistory()` |
| **quality** · [Quality.h](../include/minuvex/Quality.h) | `QualityPreset`, `QualityProfile`, `QualityOverrides`, `QualityCapabilities`, `QualityState`. `MakeQualityProfile()`, `ExpandQuality()`, `ResolveQuality()`, `SaveQuality()`, `LoadQuality()` |
| **telemetry** · [SystemInfo.h](../include/minuvex/SystemInfo.h), [Telemetry.h](../include/minuvex/Telemetry.h) | `HardwareInfo`, `MetricStatus`, `TelemetrySnapshot`. `SystemInfo::QueryHardware()`, `SystemMonitor::Sample()`, `FrameMonitor`, `EstimateQualityResources()`, `RecommendQuality()`, `SaveTelemetry()` |
| **debug** · [Debug.h](../include/minuvex/Debug.h) | `DebugView`, `DebugOptions`, `DebugDraw`, `RenderDebugInfo`. `InspectJoint()`, `InspectBoneInfluences()`. Viewer/Embedded의 `SetDebugOptions()`, `SetDebugDraw()`, `GetRenderDebugInfo()` |
| **optimization** · [Optimization.h](../include/minuvex/Optimization.h) | `MeshOptimization`, `OptimizationStatistics`, `OptimizeMesh()`, `GenerateMeshLods()`, `SelectMeshLod()`, `TextureMipBytes()` |

## 사용 예제

### Standalone Viewer

```cpp
#include <minuvex/Viewer.h>
#include <cstdio>

int main() {
    lv::Viewer viewer;
    auto object = viewer.AddModel("model.glb");
    if (!object) {
        std::fprintf(stderr, "%s\n", object.Error().Message.c_str());
        return 1;
    }
    return viewer.Run() ? 0 : 1;
}
```

기본값은 1280×720, VSync·기본 입력·자동 Focus 활성화, Medium 품질임. `ViewerRunOptions::OnFrame`에서 Scene을 갱신할 수 있으며 Animation은 기본적으로 callback 전에 자동 진행함. 직접 진행하려면 `AdvanceAnimations=false`를 사용함.

### Animation / Cross-fade

아래 코드는 애플리케이션의 초기화·갱신 함수 안에서 사용함.

```cpp
#include <minuvex/Asset.h>

lv::AssetRegistry assets;
auto model = assets.LoadModel("character.glb");
lv::AnimationPlayer player;
player.Bind(model);
if (model->Animations.size() >= 2) {
    player.Play(0);
    player.Update(.5);
    player.CrossFade(1, .3); // Clip 1로 0.3초 동안 전환
    player.Update(.15);
    auto weight = player.BlendWeight(); // 0.5
    auto pose = player.Evaluate();
}
```

`CrossFade(clip, seconds=.3)`는 0초이면 즉시 전환함. 재전환·Paused/Stopped 상태에서는 현재 base pose를 source로 고정함. Position/Scale은 선형 보간, Rotation은 Slerp, 수동 Pose override는 마지막에 적용함. Scale 부호가 호환되지 않는 조합은 거부함.

Pause·속도 0은 전환도 멈추며 Seek·Play·Stop·ResetPose는 전환을 해제함. `Stop()`은 Clip과 override를 유지하고 time=0, `ResetPose()`는 Clip·override를 해제하여 Reference Pose로 돌아감. `ApplyPose()`·`SetJointOverride()`로 full/partial Pose를 적용하며 speed 기본값은 1, loop는 true임.

### PBR Material

`MaterialAsset::Shading`은 `ShadingModel::Basic/Unlit/Pbr` 중 선택함. 기존 `Unlit=true` 플래그가 우선함. `MetallicFactor/RoughnessFactor`는 [0,1]이며, BaseColor texture는 sRGB·MetallicRoughnessTexture의 G/B는 linear로 처리함. 계산 시 roughness는 최소 .045로 제한함.

아래 코드는 유효한 `viewer`와 `ObjectId id`가 있는 함수 안에서 사용함. 공유 Asset 대신 객체의 재질 override를 수정함.

```cpp
auto material = viewer.GetScene().GetMaterial(id, 0);
material.Shading = lv::ShadingModel::Pbr;
material.MetallicFactor = .8f;
material.RoughnessFactor = .3f;
auto changed = viewer.SetMaterial(id, 0, material); // Result<void> 확인 필요
// viewer.ResetMaterial(id, 0)으로 원본 복원
```

렌더링에는 Advanced lighting과 `QualityOverrides::Pbr=true`가 필요함. Low/Medium은 기본 OFF, High는 ON임. `QualityCapabilities::PbrLighting`을 확인하며 미지원은 거부하고 AllowFallback일 때만 Basic 재질 경로로 대체함. 직접광 Directional/Point/Spot, Normal Map·Skinning·Instancing·Sun Shadow를 사용하며 IBL은 제공하지 않음.

`Scene::SetMaterial/ResetMaterial`과 `EditorDocument::SetMaterial/ResetMaterial`도 제공함. SetMaterial은 재질 전체 값을 override하며 ResetMaterial로 원본을 복원함. override가 있는 객체는 개별 draw로 처리함. Document 편집은 Undo/Redo 대상이며 Scene v7에 저장함. Texture는 같은 모델 내부 index만 참조함. Reimport 시 override material index/name·texture index 호환성을 검사함. Quality v1의 새 pbr 키는 이전 SDK reader에서 거부될 수 있음. 기본값·저장 형식 변경 및 추가 필드로 정적 SDK 소비자는 함께 재빌드해야 함.

### Embedded Runtime

호출 순서는 `Initialize()` → 프레임마다 `Update()/Render()` → 종료 시 `WaitIdle()/Shutdown()`임. Host가 창·이벤트·입력·device·target·Present를 관리함. resize 전에는 GPU 완료 대기 → `DetachTarget()` → Host target 교체 순서를 따름.

같은 device의 단일 mip/slice/sample 2D target, 1~8192 framebuffer pixels를 사용함. Color는 RGBA8/BGRA8 linear/sRGB, D3D11 depth는 D32_FLOAT 또는 D32_FLOAT_S8X24_UINT임. D3D11.1 immediate context의 Host state를 복원함. Metal은 borrowed queue로 순서를 연결하거나 별도 queue 사용 전에 Host가 동기화함. Scene depth 공유·alpha 합성은 지원하지 않음.

실제 통합 코드는 [Windows Host](../examples/EmbeddedViewer/MainD3D11.cpp) · [macOS Host](../examples/EmbeddedViewer/MainMetal.mm)를 참고함.

## 소유권과 스레드

| 대상 | 사용 규칙 |
|---|---|
| **Model / Animation** | `ModelHandle`은 immutable Asset의 강한 공유 참조. Player마다 시간·Pose·blend 상태를 독립 보유하며 동시 수정/평가 금지 |
| **Scene / EditorDocument** | 단일 호출 스레드 사용. Object 삭제·Scene 교체 후 기존 참조 사용 금지. Document 편집은 실패 시에도 snapshot 복원이 가능하므로 원소 참조를 다시 조회 |
| **Viewer** | 생성한 owner thread에서 사용. macOS Run은 main thread. `RequestClose()`·`IsRunning()`만 thread-safe. 프로세스당 Standalone Run 세션 1회 |
| **EmbeddedRenderer** | 생성·사용·Shutdown·파괴를 같은 render thread에서 수행. `IsInitialized()`만 thread-safe. Host 자원은 borrowed이며 사용 중 수명 보장 필요 |
| **그래픽 세션** | 프로세스당 활성 세션 하나. Standalone·Embedded·Editor를 동시에 초기화하지 않음 |
| **AssetLoader** | `Submit/Poll/Cancel/Forget`은 thread-safe. worker는 CPU Import만 수행하며 Scene 반영은 owner thread에서 명시적으로 처리. Cancel은 실행 중 파서를 강제 중단하지 않음 |

## 오류 처리

[Result.h](../include/minuvex/Result.h)의 `Result<T>`는 성공 시 `Value()`, 실패 시 `Error()`로 조회함. `Error`는 `Code`, `Message`, `Hint`, `SourcePath`를 제공함. 실패한 Result의 Value 또는 성공한 Result의 Error 접근은 `std::logic_error`임.

| 오류 | 의미 |
|---|---|
| `InvalidArgument` · `InvalidHandle` · `InvalidData` | 잘못된 인수·핸들·파일/데이터 |
| `InvalidState` · `WrongThread` | 호출 순서·수명·owner thread 위반 |
| `AssetLoad` · `SceneIo` · `QualityIo` · `TelemetryIo` | Import 또는 파일 입출력 실패 |
| `UnsupportedFeature` · `InsufficientResource` | 미지원 기능 또는 리소스 부족 |
| `GraphicsInitialization` · `Rendering` · `CallbackFailure` | 그래픽 초기화·렌더링·callback 실패 |

Facade·IO는 Result를 사용하고, low-level Asset/Scene/Animation API는 `ImportError`·`std::invalid_argument` 등의 예외를 던질 수 있음. 표준 할당 실패까지 항상 Result로 변환하는 계약은 아니므로 애플리케이션 경계에서 처리함.

## 저장 형식과 호환성

| 항목 | 현재 규칙 |
|---|---|
| **Scene** | writer v7 / reader v1~v7. 외부 모델 참조·계층·카메라·조명·Pose·Clip/Cross-fade·인스턴스별 재질 override 저장. 로드 시 runtime ID 재매핑 |
| **Pose / Quality / Import** | Pose v1, Quality v1, ImportVersion 5. Pose는 skeleton signature 호환 필요 |
| **Quality** | Low·Medium·High, Overrides가 있으면 Custom. 기본 `AllowFallback=false`; 미지원 설정 대체는 명시적으로 허용 |
| **SDK** | 정적 라이브러리. 같은 architecture·compiler/toolset·configuration·C++ runtime으로 소비자 재빌드 필요. 바이너리 ABI 비고정 |

Scene v7은 이전 SDK reader로 읽을 수 없음. Scene/Pose/Quality 저장은 임시 파일 작성 후 교체하며 모델 파일 자체를 Scene에 내장하지 않음. Undo 이력·UI 상태·Telemetry는 Scene에 저장하지 않음.

Windows D3D11 핵심 실행 검증을 완료했으며 macOS Metal은 구현 상태로 실기기 미검증임. CPU-only 구성의 native 초기화·실행은 `UnsupportedFeature`를 반환함. IBL·Emissive/Occlusion·Alpha BLEND·Blend Tree/Additive·Morph/Event·PCSS·Python/WebAssembly는 후속 범위임.

## 검증 범위

SDK 1.11.0은 Windows Release 핵심 6종(Asset 137·Quality 75·Document 140·Telemetry 70개 검사, Editor 및 픽셀 통합)을 통과함. 최종 자원 생성 실패 롤백 보완 후 영향 있는 픽셀 통합 89개만 재확인하고, Point/Spot 조명 보강은 PBR 단독 15개 검사로 확인함. 기본 CTest 등록 13개 유지함. CPU-only Document 140개, SDK ZIP/SHA-256·공백 prefix relocation·공개 헤더 18개·독립 CPU/Pose/PBR API 소비자와 PBR 구체 예제 3프레임을 확인함. HLSL5/Metal 셰이더 생성은 통과했으며 macOS 실기기·원격 CI·장기 성능 측정은 미검증임.

[기능·빌드 안내](../README.md) · [공식 SDK 예제](../examples/SdkConsumer) · [공개 헤더](../include/minuvex)
