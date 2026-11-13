# Minvex API

**C++20 Graphics SDK · Version 1.16.0 · Namespace `lv`**

모듈별 주요 타입과 API를 정리한 사용 안내임. 전체 선언은 연결된 공개 헤더에서 확인할 수 있음.

## SDK 연결

```cmake
find_package(Minvex 1.16 CONFIG REQUIRED COMPONENTS viewer graphics)
add_executable(my_viewer main.cpp)
target_link_libraries(my_viewer PRIVATE minvex::viewer)
```

필요한 모듈을 `minvex::<모듈명>`으로 연결함. CPU 도구는 `assets`·`animation`·`scene_io`, Host 통합은 `embedded`, 편집 상태 관리는 `editor_document` 등을 사용함. `graphics`는 그래픽 SDK 여부를 검사하는 component임.

공개 헤더 경로는 `<minvex/...>`, CMake 옵션 접두사는 `MINVEX_`, SDK 데이터 경로는 `share/minvex`임. 이전 이름의 패키지·타깃·헤더 별칭은 제공하지 않으므로 SDK 소비자는 참조를 갱신하고 다시 빌드해야 함. 문서 형식은 새 저장 시 Minvex 식별자를 쓰며 이름 변경 이전의 Scene·Pose·Quality·Editor 설정 식별자는 읽기 호환을 유지함. C++ 네임스페이스는 기존 `lv`를 사용함.

## 모듈별 주요 API

| 모듈 / 헤더 | 주요 타입·함수 |
|---|---|
| **core** · [Math.h](../include/minvex/Math.h), [Mesh.h](../include/minvex/Mesh.h) | `Vec3`, `Quaternion`, `Mat4`, `Transform`, `Bounds`, `MeshData`, `MakeCube()`, `DecomposeTRS()`. 생성 헤더 `Version.h`의 `Version`, `BuildInfo`, `GetBuildInfo()` |
| **assets** · [Asset.h](../include/minvex/Asset.h), [AssetLoading.h](../include/minvex/AssetLoading.h) | `ModelHandle`, `MorphTarget`, `MaterialAsset`, `ShadingModel`, `ImportSettings`, `ImporterRegistry`, `AssetRegistry::LoadModel/PrepareModel/ReimportModel`, `InspectModel()`, `ValidateModel()`, `CopyModelToProject()`, `AssetLoader`, `AssetHotReload` |
| **scene** · [Scene.h](../include/minvex/Scene.h) | `Scene`, `ObjectId`, `Camera`, `DirectionalLight`, `PointLight`, `SpotLight`. `AddModel()`, `CreateObject()`, `SetParent()`, `SetTransform()`, `SetModel()`, `GetMaterial/SetMaterial/ResetMaterial`, `GetAnimation()`, `UpdateAnimations()`, `SceneAnimationEvent`, `PollAnimationEvents/DispatchAnimationEvents()` |
| **animation** · [Animation.h](../include/minvex/Animation.h) | `SkeletonAsset`, `SkinAsset`, `PoseAsset`, `PoseBuffer`, `PoseChannels`, `AdditivePose/AdditiveAnimationState`, `AnimationClip`, `AnimationEvent/AnimationEventTrack/AnimationEventOccurrence`, `AnimationBlendState`, `BlendTreeDesc/BlendTreeAsset/BlendTreeState`, `MorphTrack`, `MorphPose`, `AnimationPlayer`. `EvaluateAnimation()`, `BlendPoses()`, `ComposePose()`, `CreateBlendTree()`, `EvaluateBlendWeights/EvaluateBlendTree()`, `Play/PlayBlendTree()`, `CrossFade()`, `SetBlendParameter()`, `Seek/SeekBlendTree()`, `EvaluatePose/Evaluate()`, `SetAdditiveAnimation()`, `MakeAdditivePose/ApplyAdditivePose()`, `CapturePose()` |
| **scene_io** · [SceneIO.h](../include/minvex/SceneIO.h), [PoseIO.h](../include/minvex/PoseIO.h) | `SceneArchive`, `SaveScene(scene, path, library={})`, `LoadScene(path, registry)`, `SavePose(pose, skeleton, path)`, `LoadPose(path, skeleton)` |
| **viewer** · [Viewer.h](../include/minvex/Viewer.h) | `ViewerOptions`, `ModelOptions`, `ViewerRunOptions`, `Viewer`. `AddModel()`, `AddModelAsync()`, `GetAssetLoad()`, `CommitModelLoad()`, `SetHotReload()`, `SetCamera()`, `SetMaterial/ResetMaterial`, `SetQuality()`, `CrossFadeAnimation()`, `PlayBlendTree/SetBlendParameter/SeekBlendTree`, `SetAdditiveAnimation/SetAdditiveWeight/SetAdditiveMask/SeekAdditive/ClearAdditiveAnimation`, `GetScene()`, `Run()` |
| **embedded** · [EmbeddedRenderer.h](../include/minvex/EmbeddedRenderer.h) | `HostGraphicsContext`, `HostRenderTarget`, `EmbeddedFrameOptions`, `EmbeddedRenderer`. `Initialize()`, `Update()`, `Render()`, `WaitIdle()`, `DetachTarget()`, `Shutdown()` |
| **editor_document** · [EditorDocument.h](../include/minvex/EditorDocument.h) | `EditorDocument`는 Editor의 Scene·선택·미저장 상태 관리 모델. `OpenModel()`, `Open()`, `Save()`, `ImportModel()`, `PlaceModel()`, `SetWorldTransform()`, `SetMaterial/ResetMaterial`, `CrossFadeAnimation()`, `PlayBlendTree/SetBlendParameter/SeekBlendTree`, `SetAdditiveAnimation/SetAdditiveWeight/SetAdditiveMask/SeekAdditive/ClearAdditiveAnimation`, `BeginEdit/CommitEdit/CancelEdit`, `Undo/Redo`, `ClearUndoHistory()`, `SetMorphWeights/ClearMorphWeights` |
| **quality** · [Quality.h](../include/minvex/Quality.h) | `QualityPreset`, `QualityProfile`, `QualityOverrides`, `QualityCapabilities`, `QualityState`. `MakeQualityProfile()`, `ExpandQuality()`, `ResolveQuality()`, `SaveQuality()`, `LoadQuality()` |
| **telemetry** · [SystemInfo.h](../include/minvex/SystemInfo.h), [Telemetry.h](../include/minvex/Telemetry.h) | `HardwareInfo`, `MetricStatus`, `TelemetrySnapshot`. `SystemInfo::QueryHardware()`, `SystemMonitor::Sample()`, `FrameMonitor`, `EstimateQualityResources()`, `RecommendQuality()`, `SaveTelemetry()` |
| **debug** · [Debug.h](../include/minvex/Debug.h) | `DebugView`, `DebugOptions`, `DebugDraw`, `RenderDebugInfo`. `InspectJoint()`, `InspectBoneInfluences()`. Viewer/Embedded의 `SetDebugOptions()`, `SetDebugDraw()`, `GetRenderDebugInfo()` |
| **optimization** · [Optimization.h](../include/minvex/Optimization.h) | `MeshOptimization`, `OptimizationStatistics`, `OptimizeMesh()`, `GenerateMeshLods()`, `SelectMeshLod()`, `TextureMipBytes()` |

## 사용 예제

### Standalone Viewer

```cpp
#include <minvex/Viewer.h>
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

Standalone·Editor의 기본 창 아이콘은 `Minvex_logo_transparent.png`에서 만든 투명 아이콘임. 이미지 데이터는 런타임에 내장되어 별도 이미지 파일이나 실행 디렉터리에 의존하지 않음. Windows는 크기별 창·실행 파일 아이콘, macOS는 애플리케이션/Dock 아이콘 경로를 제공하며 macOS 표시는 실기기 미검증임.

### Animation / Cross-fade

아래 코드는 애플리케이션의 초기화·갱신 함수 안에서 사용함.

```cpp
#include <minvex/Asset.h>

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

### Pose Blending Core

`PoseBuffer`는 SkeletonSignature·노드별 LocalTransforms·Channels를 소유하는 CPU 값 타입임. `PoseChannels`는 None/Translation/Rotation/Scale/All 비트이며 `|`·`&`로 조합함. `ReferencePoseBuffer()`의 채널은 None이고 `EvaluateAnimation()`은 Clip에 기록된 채널만 표시함. 없는 채널은 Reference Pose로 보완하며 읽기 전용 노드는 원본 행렬을 유지함.

```cpp
const auto& skeleton = model->Skeleton; // 유효한 ModelHandle
auto a = lv::EvaluateAnimation(skeleton, model->Animations.at(0), .5);
auto b = lv::EvaluateAnimation(skeleton, model->Animations.at(1), .25);
auto mixed = lv::BlendPoses(skeleton, a, b, .5f);
auto finalPose = lv::ApplyPoseOverrides(skeleton, mixed, player.Overrides());
auto matrices = lv::ComposePose(skeleton, finalPose); // Local/Global 행렬
```

`EvaluateAnimation()`은 유한한 0 이상 시간을 받아 duration까지 clamp함. 재생 상태·loop·Morph weight는 별도임. `BlendPoses()`는 weight [0,1], Translation/Scale Lerp·Rotation shortest-arc Slerp를 사용함. 선택한 Scale의 부호는 양쪽이 같아야 하며 endpoint에도 같은 검사를 적용함. 채널 누락은 weight 재정규화 없이 Reference와 혼합함.

`BlendPoses(..., weight, channelMask)`는 노드 수와 같은 `std::span<const PoseChannels>`를 선택적으로 받음. 빈 mask는 전체 채널 선택이며 미선택 채널은 from을 유지함. 결과 채널은 `from | (to & mask)`임. 수동 Pose는 `ApplyPoseOverrides()`로 마지막에 적용하며 해당 노드의 전체 TRS를 대체함. `AnimationPlayer::EvaluatePose()`도 같은 evaluator/composer 경로의 최종 local buffer를 반환함.

`ValidatePoseBuffer()`는 실제 Skeleton 서명·정확한 배열 길이·채널 비트·활성 TRS·읽기 전용 노드를 검사함. 누락 채널의 저장 값은 무시함. 불일치·잘못된 시간/weight·비정상 Quaternion/Scale은 `std::invalid_argument`임. 함수는 입력을 수정하거나 보관하지 않으며, immutable 입력을 공유하는 별도 버퍼 평가는 병렬 사용 가능함. Player의 동시 수정/평가는 금지임.

PoseBuffer와 채널 마스크는 평가 중간 결과이며 파일 저장 대상이 아님. 기존 Cross-fade의 live/frozen source·override 순서와 Morph 독립 채널을 유지함. 정적 SDK 소비자는 버전 변경 시 함께 재빌드해야 함.

### Blend Tree

`CreateBlendTree(model, BlendTreeDesc)`가 `shared_ptr<const BlendTreeAsset>`을 반환함. 정의·Clip 참조·삼각분할은 공유하고, `AnimationPlayer::Tree()`의 X/Y·Phase는 인스턴스마다 보유함. 정의는 생성 후 수정하지 않으며 Editor 편집은 새 Tree 생성으로 반영함.

```cpp
lv::BlendTreeDesc desc;
desc.Samples = {{0, 0, 0}, {1, 1, 0}}; // Clip index, threshold X, Y
auto tree = lv::CreateBlendTree(*model, desc);
player.PlayBlendTree(tree);
player.SetBlendParameter("Speed", .5f);
player.Update(.016);
auto weights = player.BlendWeights(); // Sample 순서와 같은 배열
```

Blend1D는 X 오름차순·Y=0인 1~64개 sample을 요구하며 인접 두 Clip을 혼합하고 범위 밖은 endpoint로 clamp함. Blend2D는 `Kind=BlendTreeKind::Blend2D`, 서로 다른 ParameterX/Y 이름과 3~64개 sample을 사용함. Delaunay facet 내부는 barycentric weight, 외부는 가장 가까운 convex-hull edge로 clamp함. `Triangles()`는 Editor/debug용 sample index 배열임.

중복·비정상 좌표·퇴화 영역·잘못된 Clip·Scale 부호를 검사함. 2D 좌표는 전체 extent로 정규화하며 점 간 거리 ≤1e-7 또는 facet 면적 기준 cross ≤1e-10인 수치 퇴화를 거부함. 이름은 최대 256 bytes임. `EvaluateBlendWeights()`는 sample별 비음수 weight를 반환하고, `EvaluateBlendTree(model, tree, x, y, phase)`는 독립 CPU PoseBuffer를 반환함. 유효 contributor의 weight를 정규화하고 등록 순서로 Lerp·progressive shortest-arc Slerp를 적용함. 누락 TRS는 Reference, Morph는 Node/Mesh 기본값으로 보완하며 수동 override를 마지막에 적용함.

`PlayBlendTree(tree, restart=true)`는 Clip/cross-fade를 해제하고 첫 sample 좌표로 시작함. 같은 handle에 restart=false이면 현재 파라미터·phase를 유지함. 공통 phase [0,1]에서 각 Clip을 `phase × duration`으로 샘플링하고 cycle duration은 현재 weight의 가중 평균임. 길이 0인 Clip은 t=0으로 샘플링하며 전체 cycle이 0이면 정지한 상수 Pose임. 발 접촉 시점의 자동 동기화는 제공하지 않음.

`SetBlendParameter(name, value)`·`SeekBlendTree(phase)`·`BlendTreeDuration()`을 제공함. 파라미터는 유한한 값, phase는 [0,1]임. 파라미터 편집은 phase를 유지하며 Pause·속도·Loop·역재생은 기존 제어를 따름. Stop은 Tree·파라미터를 유지하고 phase=0, ResetPose는 Tree도 해제함. Play는 단일 Clip으로 전환하고 Tree에서 CrossFade하면 현재 base TRS/Morph를 frozen source로 사용함.

Viewer·EditorDocument도 `PlayBlendTree/SetBlendParameter/SeekBlendTree` Result Facade를 제공함. Editor Animation Controls에서 이름·파라미터·Clip·threshold/좌표 편집과 Apply/Play, 현재 weight·2D 삼각분할 보기를 제공하며 편집은 Undo/Redo 대상임. Scene v11은 최대 1024개 공유 Tree 정의와 독립 phase/파라미터/재생 상태를 저장하며 reader는 v1~v11임. Reimport는 실제 Skeleton/Morph layout과 모든 참조 Clip index/name·Scale 호환성을 검사함.

잘못된 입력/호환성은 low-level API에서 `std::invalid_argument`임. 반환한 handle은 정의의 수명을 유지하며 `Tree()` 참조는 Player/Scene 편집 후 다시 조회함. immutable 정의의 동시 조회·서로 다른 Player 사용은 가능하고 같은 Player의 동시 수정/평가는 금지임. SDK 1.16.0 소비자는 함께 재빌드해야 하며 Pose v1·ImportVersion 6은 유지함.

### Additive Animation

`AdditivePose`는 SkeletonSignature·LocalDeltas·유효 Channels를 소유함. `MakeAdditivePose(skeleton, sample, reference)`는 Translation 차이, `reference⁻¹ × sample` local Rotation delta, Scale 비율을 생성함. 샘플에 없는 채널은 neutral delta이고 기준에 없는 채널은 Skeleton Reference 값을 사용함.

```cpp
player.PlayBlendTree(tree); // 단일 Clip 또는 Reference base도 가능함
player.SetAdditiveAnimation(clip, .4f); // 기본 기준은 Skeleton Reference Pose임
player.SetAdditiveMask(jointWeights); // 비어 있거나 노드 수와 같은 [0,1] 배열임
player.SeekAdditive(.5);
auto delta = player.EvaluateAdditive();
auto finalPose = player.EvaluatePose();
```

`ApplyAdditivePose(skeleton, base, delta, weight, mask={})`는 weight×Joint mask로 Translation을 더하고, `baseRotation × Slerp(identity, delta, w)`와 `baseScale × Lerp(1, ratio, w)`를 적용함. weight/mask는 유한한 [0,1]이며 0은 base를 유지함. 기준 Scale 절댓값은 1e-8 이상이고 sample과 부호가 같아야 함. 비정상 Quaternion·배열·서명·비율은 `ValidateAdditivePose()` 또는 합성 API에서 `std::invalid_argument`임.

Player는 TRS Additive 레이어 1개를 보유함. `SetAdditiveAnimation(clip, weight, reference={}, mask={})`의 명시적 full/partial PoseAsset은 고정 snapshot이며 빈 기본 인수는 Skeleton Reference임. TRS 채널이 없는 Clip은 거부하고 Morph weight는 기존 base/Tree 평가를 유지함. 순서는 Clip/Cross-fade/Tree → Additive → 수동 Pose override임. `EvaluateBasePose()`·`EvaluateAdditive()`·`EvaluatePose()`로 각 단계를 비교할 수 있음.

`Additive()`의 상태는 Clip·Time·Speed·Loop·Weight·State·Reference·JointMask임. 레이어 속도는 Player 속도와 곱해지고 playhead/loop는 base와 독립임. `RestoreAdditiveAnimation(state)`로 전체 설정을 검증·교체하며 `SetAdditiveWeight/SetAdditiveMask/SeekAdditive()`로 개별 편집함. Seek는 duration까지 clamp함. Pause/Resume은 base와 레이어를 함께 제어하고 Stop은 두 시간을 0으로 되돌리며 설정은 유지함. Play/PlayBlendTree/CrossFade는 레이어를 유지하고 ResetPose는 해제함. `ClearAdditiveAnimation()`은 base를 유지하며 레이어만 제거함.

Viewer·EditorDocument에도 같은 제어의 Result Facade를 제공함. Editor는 Clip·Skeleton/현재 base/Clip sample 기준 선택, weight·시간·속도·loop, Joint/Subtree mask, Base/Delta/Final 비교를 제공함. 편집은 Undo/Redo 대상이며 Scene v11은 레이어 reference·mask·재생 상태를 저장하고 reader는 v1~v11임. Reimport는 Skeleton·레이어 Clip index/name·시간과 reference/Scale 호환성을 검사함. Pose v1은 최종 TRS snapshot만 저장함.

공유 Model/Clip은 수정하지 않으며 레이어·기준 Pose·mask는 Player별로 소유함. 상태 참조는 Player/Scene 편집 후 다시 조회하고 같은 Player의 동시 수정/평가는 금지임. weight 0 또는 전체 mask 0인 레이어의 시간 변화는 정적인 base의 On-Demand 이미지를 무효화하지 않음. SDK 1.16.0 소비자는 함께 재빌드해야 하며 ImportVersion 6은 유지함.

### Animation Event

`AnimationEvent`는 TimeSeconds·Name·Payload이며 `AnimationClip::Events`는 시간순 기본 track임. `SetAnimationEvents(clip, events)`는 공유 Asset을 수정하지 않는 Player별 overlay를 교체함. 빈 overlay는 기본 track을 숨기고 `ResetAnimationEvents(clip)`은 원본을 복원함. `AnimationEvents(clip)`·`EventOverrides()`로 조회함.

```cpp
std::array<lv::AnimationEvent, 1> events{{{.35, "Footstep", "{\"side\":\"L\"}"}}};
player.SetAnimationEvents(clip, events);
player.Play(clip);
player.Update(.4);
auto batch = player.PollAnimationEvents(); // 소유권을 가진 발생 snapshot 배열임
// 또는 player.DispatchAnimationEvents(callback)에서 안전한 시점에 전달함
```

정방향은 `(old, new]`, 역방향은 `[new, old)`임. 같은 시각은 등록 순서를 유지하며 여러 loop도 모두 수집함. loop 경계에서는 정방향 duration→0, 역방향 0→duration 순서로 각각 한 번 발생함. Play·Stop·Pause·Pose 평가·0초 Clip은 자체 이벤트를 발생시키지 않음. Seek/SeekBlendTree는 기본 Silent이며 `AnimationSeekEvents::Collect`를 지정하면 wrapping 없이 이동 구간만 수집함.

Cross-fade는 target Clip만 수집함. Tree는 동일 Clip sample의 weight를 합산해 가장 큰 Clip 하나를 선택하고 동률은 첫 sample 순서임. 주 Clip 변경 시 과거 구간을 재생하지 않으며 Additive Clip은 기본 수집 대상이 아님. 발생값은 Model·Clip index/name·수집 당시 EventIndex·Event snapshot·Reverse를 포함함. index는 편집 이후의 영구 ID가 아니며 loop 반복은 별도 발생임.

Player 큐는 최대 1024개임. 초과 시 `AnimationEventOverflow`를 던지고 해당 Update/Seek의 시간·기존 큐는 유지함. Scene Update는 모든 Player의 이벤트 용량을 먼저 확인함. `PollAnimationEvents()`는 큐를 비워 소유 배열을 반환하고 `PendingAnimationEvents()`는 편집/배출 전까지 유효한 조회 span임. Scene의 batch는 ObjectId를 포함한 `SceneAnimationEvent`이며 객체 순서·Player 내 이동 순서로 반환함.

`DispatchAnimationEvents(callback)`은 큐 snapshot을 먼저 분리하고 모든 callback을 한 번씩 시도함. callback 중 객체 삭제·새 이벤트 수집은 현재 batch를 바꾸지 않음. 실패 시 첫 원예외를 Cause로 가진 `AnimationEventCallbackError`를 마지막에 던짐. Viewer·EditorDocument는 `SetAnimationEvents/ResetAnimationEvents/PollAnimationEvents/DispatchAnimationEvents`와 collecting Seek의 Result Facade를 제공하며 overflow는 InsufficientResource, callback 오류는 CallbackFailure임.

track당 최대 1024개, overlay 최대 256 track, 기본 track별·인스턴스 전체 overlay별 이름/페이로드 합계는 각각 1 MiB임. 이름은 1~256 bytes, payload는 최대 4096 bytes의 NUL 없는 UTF-8이며 시간은 유한한 [0,duration] 오름차순임. 잘못된 데이터는 `std::invalid_argument`임. Player/Scene의 동시 수정·평가는 금지이고 Viewer는 생성한 owner thread에서 사용함. callback은 명시적 Dispatch를 호출한 스레드에서 실행하며 전달된 참조는 해당 호출 동안만 유효함.

Editor는 Timeline marker, 추가·삭제·시간·이름·payload 편집, 수집 Seek 선택과 발생 로그를 제공함. marker 편집은 Undo/Redo 대상이며 큐 배출·로그는 문서를 dirty로 만들지 않음. Scene v11은 overlay와 Clip identity를 저장하고 reader는 v1~v11임. 발생 큐·callback·로그는 저장하지 않으며 Undo/Redo는 과거 큐를 재발송하지 않음. Reimport는 overlay Clip identity/duration을 검사함. SDK 1.16.0 소비자는 함께 재빌드해야 하며 Pose v1·ImportVersion 6은 유지함.

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

`Scene::SetMaterial/ResetMaterial`과 `EditorDocument::SetMaterial/ResetMaterial`도 제공함. SetMaterial은 재질 전체 값을 override하며 ResetMaterial로 원본을 복원함. override가 있는 객체는 개별 draw로 처리함. Document 편집은 Undo/Redo 대상이며 Scene v11에 저장함. Texture는 같은 모델 내부 index만 참조함. Reimport 시 override material index/name·texture index 호환성을 검사함. Quality v1의 새 pbr 키는 이전 SDK reader에서 거부될 수 있음. 기본값·저장 형식 변경 및 추가 필드로 정적 SDK 소비자는 함께 재빌드해야 함.

### Morph Target

`MeshPrimitive::MorphTargets`가 POSITION/NORMAL/TANGENT delta를 소유하고, `MeshAsset::MorphWeights`·`ModelNode::MorphWeights`가 기본값을 제공함. Node 값이 없으면 Mesh 기본값을 사용함. 공유 Asset을 바꾸지 않고 Player별·Node별 override를 적용함.

```cpp
auto& player = viewer.GetScene().GetAnimation(id);
auto weights = player.EvaluateMorphWeights(node); // 유효한 Morph mesh node ID
weights[0] = .7f;
auto changed = viewer.SetMorphWeights(id, node, weights); // Result<void> 확인 필요
// viewer.ClearMorphWeights(id, node)로 animation/default 값 복원
```

`AnimationPlayer::SetMorphWeights/ClearMorphWeights`, `Viewer`·`EditorDocument`의 같은 이름 Facade를 제공함. `ClearMorphOverrides()`는 전체 수동 weight를 지움. `MorphOverrides()`는 수동 상태, `EvaluateMorph(primitive, weights)`는 CPU 기준 MeshData를 반환함. weight는 각 target의 독립 가중치이며 합을 정규화하지 않음. 유한한 [-100,100]과 정확한 target 수를 요구함. ResetPose는 Morph override도 지우고, ClearOverrides/독립 Pose 파일은 Bone TRS만 다룸.

Mesh당 최대 8개 target이며 `ImportSettings::MaxMorphTargets`로 더 낮출 수 있음. 초과 입력은 ResourceLimit으로 거부하며 자르지 않음. Morph scalar sample도 MaxAnimationSamples에 포함하고 delta는 MaxResourceBytes 예산에 포함함. glTF sparse delta·Mesh/Node 기본값·STEP/LINEAR weight animation, FBX progressive shape의 target별 변환과 지정 bake rate를 지원함. CUBICSPLINE·geometry cache는 미지원임.

평가 순서는 Clip/cross-fade → 수동 weight override → GPU Morph → GPU Skinning임. Morph cross-fade도 live/frozen source·Pause·역재생을 따름. NORMAL/TANGENT를 정규화하고 누락 채널은 base 값을 유지함. GPU는 정점 remap과 일치하는 공유 storage buffer를 사용하며 Morph Mesh의 reduced LOD/instancing은 사용하지 않음. Bounds·Picking/Focus·Culling·Shadow·On-Demand·자원 비용에 변형을 반영함. GPU packed delta 비용은 정점×target×48 bytes임.

Scene v11은 node별 override와 frozen source Morph, layout signature를 저장함. 사용 중인 node/mesh·target 이름/순서·vertex 수·기본값 변경은 Reimport에서 거부함. Pose v1은 Morph를 저장하지 않음. 반환된 override 참조는 Player/Scene 편집 후 다시 조회하며 기존 owner-thread·오류 계약을 따름. `--morph` 렌더링 예제와 `examples/assets/MorphRibbon.gltf`를 제공함.

### Embedded Runtime

호출 순서는 `Initialize()` → 프레임마다 `Update()/Render()` → 종료 시 `WaitIdle()/Shutdown()`임. Host가 창·이벤트·입력·device·target·Present를 관리함. resize 전에는 GPU 완료 대기 → `DetachTarget()` → Host target 교체 순서를 따름.

같은 device의 단일 mip/slice/sample 2D target, 1~8192 framebuffer pixels를 사용함. Color는 RGBA8/BGRA8 linear/sRGB, D3D11 depth는 D32_FLOAT 또는 D32_FLOAT_S8X24_UINT임. D3D11.1 immediate context의 Host state를 복원함. Metal은 borrowed queue로 순서를 연결하거나 별도 queue 사용 전에 Host가 동기화함. Scene depth 공유·alpha 합성은 지원하지 않음.

실제 통합 코드는 [Windows Host](../examples/EmbeddedViewer/MainD3D11.cpp) · [macOS Host](../examples/EmbeddedViewer/MainMetal.mm)를 참고함.

## 소유권과 스레드

| 대상 | 사용 규칙 |
|---|---|
| **Model / Animation** | `ModelHandle`은 immutable Asset의 강한 공유 참조. Player마다 시간·Pose·blend·Tree 파라미터/phase·Additive 상태를 독립 보유하며 동시 수정/평가 금지 |
| **Scene / EditorDocument** | 단일 호출 스레드 사용. Object 삭제·Scene 교체 후 기존 참조 사용 금지. Document 편집은 실패 시에도 snapshot 복원이 가능하므로 원소 참조를 다시 조회 |
| **Viewer** | 생성한 owner thread에서 사용. macOS Run은 main thread. `RequestClose()`·`IsRunning()`만 thread-safe. 프로세스당 Standalone Run 세션 1회 |
| **EmbeddedRenderer** | 생성·사용·Shutdown·파괴를 같은 render thread에서 수행. `IsInitialized()`만 thread-safe. Host 자원은 borrowed이며 사용 중 수명 보장 필요 |
| **그래픽 세션** | 프로세스당 활성 세션 하나. Standalone·Embedded·Editor를 동시에 초기화하지 않음 |
| **AssetLoader** | `Submit/Poll/Cancel/Forget`은 thread-safe. worker는 CPU Import만 수행하며 Scene 반영은 owner thread에서 명시적으로 처리. Cancel은 실행 중 파서를 강제 중단하지 않음 |

## 오류 처리

[Result.h](../include/minvex/Result.h)의 `Result<T>`는 성공 시 `Value()`, 실패 시 `Error()`로 조회함. `Error`는 `Code`, `Message`, `Hint`, `SourcePath`를 제공함. 실패한 Result의 Value 또는 성공한 Result의 Error 접근은 `std::logic_error`임.

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
| **Scene** | writer v11 / reader v1~v11. 외부 모델 참조·계층·카메라·조명·Pose·Clip/Cross-fade·Blend Tree·Additive·Event overlay·재질/Morph override 저장. 로드 시 runtime ID 재매핑 |
| **Pose / Quality / Import** | Pose v1, Quality v1, ImportVersion 6. Pose는 skeleton signature 호환 필요 |
| **Quality** | Low·Medium·High, Overrides가 있으면 Custom. 기본 `AllowFallback=false`; 미지원 설정 대체는 명시적으로 허용 |
| **SDK** | 정적 라이브러리. 같은 architecture·compiler/toolset·configuration·C++ runtime으로 소비자 재빌드 필요. 바이너리 ABI 비고정 |

Scene v11은 이전 SDK reader로 읽을 수 없음. Scene/Pose/Quality 저장은 임시 파일 작성 후 교체하며 모델 파일 자체를 Scene에 내장하지 않음. Undo 이력·UI 상태·Telemetry는 Scene에 저장하지 않음.

Windows D3D11 핵심 실행 검증을 완료했으며 macOS Metal은 구현 상태로 실기기 미검증임. CPU-only 구성의 native 초기화·실행은 `UnsupportedFeature`를 반환함. IBL·Emissive/Occlusion·Alpha BLEND·PCSS·Python/WebAssembly는 후속 범위임.

## 검증 범위

SDK 1.16.0의 Windows Release 전체 빌드와 Animation 391개·Document·Editor marker/log·저장·재열기를 확인함. 정·역방향 반열린 경계·복수 loop·큰 시간의 Seek/loop·Tree 주 Clip·overlay·queue overflow·callback 예외/객체 삭제/재진입·Scene v11을 검사함.

CPU-only Debug SDK와 공백 포함 설치 경로의 공개 헤더 18개·독립 Event polling/dispatch 소비자를 확인함. 기본 CTest 등록은 13종으로 유지하며 macOS 실기기·전체 회귀는 이번 검증 범위에 포함하지 않았음.

[기능·빌드 안내](../README.md) · [공식 SDK 예제](../examples/SdkConsumer) · [공개 헤더](../include/minvex)
