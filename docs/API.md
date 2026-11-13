# Minvex API

**C++20 Graphics SDK · Version 1.20.0 · Namespace `lv`**

모듈별 주요 타입과 API를 정리한 사용 안내임. 전체 선언은 연결된 공개 헤더에서 확인할 수 있음.

## SDK 연결

```cmake
find_package(Minvex 1.20 CONFIG REQUIRED COMPONENTS viewer graphics)
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

Viewer·EditorDocument도 `PlayBlendTree/SetBlendParameter/SeekBlendTree` Result Facade를 제공함. Editor Animation Controls에서 이름·파라미터·Clip·threshold/좌표 편집과 Apply/Play, 현재 weight·2D 삼각분할 보기를 제공하며 편집은 Undo/Redo 대상임. Scene v12는 최대 1024개 공유 Tree 정의와 독립 phase/파라미터/재생 상태를 저장하며 reader는 v1~v12임. Reimport는 실제 Skeleton/Morph layout과 모든 참조 Clip index/name·Scale 호환성을 검사함.

잘못된 입력/호환성은 low-level API에서 `std::invalid_argument`임. 반환한 handle은 정의의 수명을 유지하며 `Tree()` 참조는 Player/Scene 편집 후 다시 조회함. immutable 정의의 동시 조회·서로 다른 Player 사용은 가능하고 같은 Player의 동시 수정/평가는 금지임. SDK 1.20.0 소비자는 함께 재빌드해야 하며 Pose v1·ImportVersion 6은 유지함.

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

Viewer·EditorDocument에도 같은 제어의 Result Facade를 제공함. Editor는 Clip·Skeleton/현재 base/Clip sample 기준 선택, weight·시간·속도·loop, Joint/Subtree mask, Base/Delta/Final 비교를 제공함. 편집은 Undo/Redo 대상이며 Scene v12는 레이어 reference·mask·재생 상태를 저장하고 reader는 v1~v12임. Reimport는 Skeleton·레이어 Clip index/name·시간과 reference/Scale 호환성을 검사함. Pose v1은 최종 TRS snapshot만 저장함.

공유 Model/Clip은 수정하지 않으며 레이어·기준 Pose·mask는 Player별로 소유함. 상태 참조는 Player/Scene 편집 후 다시 조회하고 같은 Player의 동시 수정/평가는 금지임. weight 0 또는 전체 mask 0인 레이어의 시간 변화는 정적인 base의 On-Demand 이미지를 무효화하지 않음. SDK 1.20.0 소비자는 함께 재빌드해야 하며 ImportVersion 6은 유지함.

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

Editor는 Timeline marker, 추가·삭제·시간·이름·payload 편집, 수집 Seek 선택과 발생 로그를 제공함. marker 편집은 Undo/Redo 대상이며 큐 배출·로그는 문서를 dirty로 만들지 않음. Scene v12는 overlay와 Clip identity를 저장하고 reader는 v1~v12임. 발생 큐·callback·로그는 저장하지 않으며 Undo/Redo는 과거 큐를 재발송하지 않음. Reimport는 overlay Clip identity/duration을 검사함. SDK 1.20.0 소비자는 함께 재빌드해야 하며 Pose v1·ImportVersion 6은 유지함.

### Shared Skeleton / Leader Pose / Socket (SDK 1.20)

공개 정의는 `minvex/Skeleton.h`·`minvex/Scene.h`이며 `minvex::animation`과 `minvex::scene`에 구현됨. 제품명은 Minvex, C++ namespace는 `lv`임.

| API / 타입 | 동작 |
|---|---|
| `SkeletonSocket` | `Name`, 대상 노드 이름 `Joint`, 노드 기준 `LocalTransform` |
| `SharedSkeletonHandle` / `SharedSkeletonAsset` | 불변 Skeleton·공유 소켓 snapshot의 강한 공유 소유권. `Skeleton()`·`Sockets()`로 조회 |
| `CreateSharedSkeleton(skeleton, sockets={})` | 입력을 복사/이동하여 검증한 공유 정의 생성. 본이 하나 이상 필요함 |
| `FindSkeletonJoint(skeleton, name)` | 유일한 노드 이름을 JointId로 해석. 누락·중복 이름 거부 |
| `ValidateSkeletonSocket(skeleton, socket)` | 이름·노드 참조·유효한 offset TRS 검사 |
| `BindSkeleton(shared, modelSkeleton)` | `SkeletonBinding::ModelToShared/SharedToModel` 매핑. 일반 노드/누락된 공유 본은 `NoJoint` |
| `Scene::SetSharedSkeleton(id, handle)` | 모델 인스턴스에 공유 정의 연결/해제. 활성 Leader/Follower 관계가 있으면 변경을 거부함 |
| `Scene::SetLeaderPose(follower, leader)` | 최종 본 행렬의 source 설정. `NoObject`는 독립 Player 복원 |
| `Scene::EvaluatePose(id)` / `GetModelBounds(id)` | Leader까지 해석한 model-space Pose / Morph를 포함한 bounds 값 반환 |
| `Scene::SetSocket(id, socket)` / `GetSocket(id, name)` | 인스턴스 소켓 정의/override 및 유효 정의 조회. 인스턴스 정의가 공유 정의보다 우선함 |
| `Scene::RemoveSocket(id, name)` | 인스턴스 정의만 삭제함. 공유 정의가 있으면 복원하며, 대체 정의 없이 부착 중이면 거부함 |
| `Scene::GetSocketWorldTransform(id, name)` | 최종 Pose·객체 계층·socket offset을 포함한 world 행렬 |
| `Scene::CreateAttachmentNode(parent, socket, relative={}, name={})` | 모델 없는 부착 노드 생성. 아래에 모델·하위 노드 배치 가능 |
| `Scene::AttachToSocket(id, parent, socket, relative={})` | 기존 객체를 지정 소켓에 부착. 기본 상대 TRS는 identity로 snap함 |
| `Scene::DetachFromSocket(id, keepWorld=true)` | Scene root로 분리. false는 현재 상대 TRS 유지, true는 world TRS 보존 |
| `Scene::SetWorldTransform(id, world)` | 일반 부모 또는 애니메이션 socket frame을 포함하여 local TRS 계산 |
| `AnimationPlayer::GetBounds(pose)` | 지정한 model-node 순서 Global 행렬과 해당 Player의 Morph weight로 bounds 계산 |
| `InspectJoint(scene, id, joint)` | Leader를 포함한 local/global·해당 메시의 skin palette 진단 (`minvex::debug`) |

공유 매핑은 `IsJoint=true` 본의 유일한 이름, 가장 가까운 부모 본 이름과 model-space 기준 행렬을 검사함. 기준 행렬 오차는 요소별 `1e-5 × max(1, abs(a), abs(b))` 이내여야 함. 노드/팔레트 순서 차이와 조상 본을 유지한 부분 Skeleton은 허용함. Follower가 사용하는 모든 본은 Leader에도 있어야 하며 미등록 추가 본·다른 기준 자세·계층 변경은 거부함. 이 API는 리타기팅을 수행하지 않음. 메시 고유의 일반 노드는 Follower 기준 local 행렬을 유지하며, 매핑된 부모 본 아래에 있으면 함께 움직임.

```cpp
#include <minvex/Scene.h>

// scene과 유효한 ModelHandle bodyModel/clothingModel/weaponModel을 준비한 뒤:
auto shared = lv::CreateSharedSkeleton(bodyModel->Skeleton, {{"Weapon", "RightHand"}});
auto body = scene.AddModel(bodyModel);
auto clothes = scene.AddModel(clothingModel, {}, body);
scene.SetSharedSkeleton(body, shared);
scene.SetSharedSkeleton(clothes, shared);
scene.SetLeaderPose(clothes, body);
auto slot = scene.CreateAttachmentNode(clothes, "Weapon", {}, "Weapon slot");
scene.AddModel(weaponModel, {}, slot);
scene.GetAnimation(body).Play(0);
scene.UpdateAnimations(.016); // Socket 갱신 호출은 필요 없음
auto finalPose = scene.EvaluatePose(clothes);
scene.DetachFromSocket(slot); // world TRS를 보존하여 root로 분리
scene.AttachToSocket(slot, clothes, "Weapon"); // identity offset으로 다시 장착
```

Leader 관계와 Transform 부모 관계는 별개임. 의상이 Body world transform을 따르려면 부모도 Body로 설정함. Bone matrix는 각 객체의 model-space 기준이며 Body의 world transform을 중복 적용하지 않음. 부착 행렬은 `parentWorld × resolvedPose.Global[joint] × socket.LocalTransform × child.LocalTransform`이고 하위 자식에도 누적됨. 일반 `SetParent()`는 소켓 부착을 해제하고 현재 local TRS를 유지함. 부모의 Visible=false는 부착 subtree도 숨기지만 Transform 부모가 아닌 숨겨진 Leader는 보이는 Follower를 계속 구동함.

Follower는 자체 애니메이션 Player의 TRS·Morph 재생과 이벤트 수집을 멈추고 Leader의 최종 Clip/Cross-fade/Blend Tree → Additive → 수동 override 결과를 사용함. Follower Player는 dormant 상태로 보존되며 직접 수정은 독립 상태에만 적용됨. Follower의 Morph weight 조회·수동 override, 재질, geometry, inverse bind는 자체 모델을 사용함. Leader 연결/해제는 Follower의 pending event queue를 비우며 과거 이벤트를 복제하지 않음. 해제 후 다음 Scene Update부터 보존된 독립 Player가 다시 진행함. Leader chain은 허용하고 self/순환/다른 Scene ID는 거부함.

`CollectRenderItems()`는 한 번의 호출 안에서 Leader Pose를 한 번 평가하고 같은 모델의 Follower는 같은 `RenderItem::Pose` snapshot을 공유함. 다른 노드 순서의 Follower는 매핑된 snapshot을 생성함. snapshot은 `shared_ptr<const EvaluatedPose>`가 소유하므로 Scene 수정/삭제 후에도 유지할 수 있음. `RenderItem::Animation`·재질 포인터와 `GetSocket()` 참조는 Scene 편집/삭제 전까지 유효하며 장기 보관 시 복사함. Scene은 모델·공유 Skeleton handle을 강하게 보유하고 모델 원본·공유 Skeleton·Clip을 수정하지 않음. 같은 Scene/Player의 동시 수정·평가는 금지하며 불변 공유 정의는 다른 Scene에서 동시 조회/바인딩할 수 있음.

소켓 이름·대상 노드 이름은 NUL 없는 1~256 bytes임. 인스턴스 정의와 공유 정의는 각각 최대 256개임. 공유 소켓은 본을 대상으로 하며 해당 정의에 바인딩된 각 모델에 존재해야 함. 공유 정의 자체는 불변이고 인스턴스 override로 조정함. 부착 subtree 삭제는 일반 Scene subtree 삭제와 같으며, 삭제된 Leader를 참조하는 다른 subtree의 살아 있는 Follower는 독립 Player로 복귀함. 모델 교체/재임포트는 소켓·Shared Skeleton·Leader 본 요구와 기존 Animation 상태를 먼저 검증한 뒤 적용하며 실패 시 모든 인스턴스를 유지함.

잘못된 입력·누락된 소켓/본·호환성·순환·singular TRS는 `std::invalid_argument`임. world-preserving detach 또는 SetWorldTransform 결과에 shear가 있으면 TRS로 저장할 수 없어 변경 전에 거부함. `EditorDocument`는 공유 Skeleton·Leader·소켓·부착 API를 같은 인수로 제공하고 Result 오류 및 Undo/Redo를 적용함. 전체 Pose 저장은 Follower의 표시되는 최종 local TRS를 저장하며 local shear는 거부함. overrides-only 저장은 dormant 독립 Player의 override임. Viewer는 owner thread에서 `GetScene()`을 통해 사용함.

Scene writer는 v12이며 reader는 v1~v12임. 최대 1024개의 공유 Skeleton 정의를 중복 없이 저장하고 본/기준 자세/공유 소켓, 인스턴스 소켓 override, Leader·Transform 부모·부착 소켓을 복원함. 파일의 ObjectId는 새 런타임 ID로 매핑하며 모순된 참조·순환·256단계 초과 chain은 거부함. v1~v11에서는 새 관계가 없는 일반 객체로 읽음. 기존 16 MiB JSON·65536 객체·4096 모델 제한을 유지함. 이전 SDK reader는 v12를 읽을 수 없음. Pose v1·ImportVersion 6은 유지하고 정적 SDK 소비자는 1.20.0 헤더/라이브러리로 함께 재빌드해야 함.

예제는 `examples/Equipment/Main.cpp`, 설치 SDK 소비자는 `examples/SdkConsumer/Equipment.cpp`임. C++/Editor 기능을 제공하며 Python/JavaScript의 장착 제어 바인딩은 아직 제공하지 않음. Windows MSVC Release CTest 19/19, CPU-only Debug 12/12 통과(D3D11 장착 픽셀·예제·Editor/Embedded smoke 포함). 저장 chain 경계 검사 후 관련 Native 5개·CPU-only 12개를 재검증함. Native SDK 설치본만으로 공개 헤더 개별 컴파일·장착 상태 저장/복원 소비자·장착 그래픽 예제 실행을 검증함. macOS Metal·WebGL2의 새 기능 실행 결과는 미검증임.

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

`Scene::SetMaterial/ResetMaterial`과 `EditorDocument::SetMaterial/ResetMaterial`도 제공함. SetMaterial은 재질 전체 값을 override하며 ResetMaterial로 원본을 복원함. override가 있는 객체는 개별 draw로 처리함. Document 편집은 Undo/Redo 대상이며 Scene v12에 저장함. Texture는 같은 모델 내부 index만 참조함. Reimport 시 override material index/name·texture index 호환성을 검사함. Quality v1의 새 pbr 키는 이전 SDK reader에서 거부될 수 있음. 기본값·저장 형식 변경 및 추가 필드로 정적 SDK 소비자는 함께 재빌드해야 함.

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

Mesh당 최대 8개 target이며 `ImportSettings::MaxMorphTargets`로 더 낮출 수 있음. 초과 입력은 ResourceLimit으로 거부하며 자르지 않음. Morph scalar sample도 MaxAnimationSamples에 포함하고 delta는 MaxResourceBytes 예산에 포함함. glTF sparse delta·Mesh/Node 기본값·STEP/LINEAR weight animation, FBX progressive shape의 target별 변환과 지정 bake rate를 지원함. FBX TRS/Morph는 같은 float key 시간표를 사용하고 반올림으로 겹친 key는 끝 값을 보존하며 합침. 원본 double duration은 유지하고 key가 duration을 넘지 않도록 처리함. CUBICSPLINE·geometry cache는 미지원임.

평가 순서는 Clip/Cross-fade/Blend Tree → 수동 weight override → GPU Morph → GPU Skinning임. Morph cross-fade도 live/frozen source·Pause·역재생을 따름. NORMAL/TANGENT를 정규화하고 누락 채널은 base 값을 유지함. GPU는 정점 remap과 일치하는 공유 storage buffer를 사용하며 Morph Mesh의 reduced LOD/instancing은 사용하지 않음. Bounds·Picking/Focus·Culling·Shadow·On-Demand·자원 비용에 변형을 반영함. GPU packed delta 비용은 정점×target×48 bytes임.

Scene v12는 node별 override와 frozen source Morph, layout signature를 저장함. 사용 중인 node/mesh·target 이름/순서·vertex 수·기본값 변경은 Reimport에서 거부함. Pose v1은 Morph를 저장하지 않음. 반환된 override 참조는 Player/Scene 편집 후 다시 조회하며 기존 owner-thread·오류 계약을 따름. `--morph` 렌더링 예제와 `examples/assets/MorphRibbon.gltf`를 제공함.

### Frame Pacing / 장착 예제 진단

`RenderScheduler`는 caller thread에서 사용하는 CPU 값 타입임. `Due(now, targetFps)`는 유한한 0 이상 초와 FPS 0~240을 받으며 잘못된 입력은 `std::invalid_argument`임. 0은 제한 없음이고, `Submitted(now, signature)`는 실제 제출을 기록함. `Invalidate()`는 다음 제출을 즉시 허용하고 새 epoch를 시작함. 시계가 뒤로 이동한 경우에도 다음 제출에서 epoch를 재설정함. On-Demand의 signature/dynamic 판정과 FPS 제한은 독립적임.

제한 시간은 첫 제출 epoch에 고정된 시간 격자를 사용함. 각 callback의 작은 지연을 다음 deadline에 누적하지 않으며, VSync 경계에서는 `min(0.5ms, framePeriod × 0.05)`의 이른 제출을 허용함. 허용한 프레임의 시간 slot은 다음 제출 시 계산에 포함되어 동일 slot을 중복 제출하지 않음. 높은 callback 빈도에서도 목표 평균 FPS를 유지하며, Viewer·Embedded·Editor의 공통 RenderSurface에 적용됨. VSync와 FPS 제한은 서로 다른 기능이며, 화면 주사율 그대로 재생할 애니메이션 미리보기는 `ViewerOptions::VSync=true`와 `Quality.Overrides.TargetFps=0`을 함께 사용할 수 있음. 서로 나누어떨어지지 않는 주사율/목표 FPS 조합의 표시 간격은 여전히 달라질 수 있음.

`minvex_equipment`는 위 VSync 설정을 기본으로 사용하며 `--target-fps 0..240`·`--no-vsync`로 비교할 수 있음. 팔 회전·몸 translation은 60 Hz cosine key로 방향 전환과 loop 경계를 완화하고 기존 Clip Slerp/Lerp로 평가함. `--profile path.json --frames 270`은 30프레임 warmup 후 wall-clock callback 간격을 기록함. 시뮬레이션 delta가 긴 stall 이후 clamp되어도 실제 간격을 기록하며, JSON 요약·frame별 CSV·기존 Telemetry JSON을 생성함. profile은 동기식 파일 IO이며 해당 path에 쓰기 권한이 필요하고 실패 시 예제는 오류를 출력하고 종료 코드 1을 반환함. 측정은 opt-in이며 수집된 본 행렬 비교는 Scene 상태를 변경하지 않음.

Windows D3D11 측정에서 기존 VSync+60 FPS 제한은 32.8 FPS·P95 33.9ms였고, 제한만 해제한 A/B는 60.0 FPS·P95 17.1ms였음. 수정 후 기본/명시적 60 FPS 제한 모두 60 FPS 근처, Body/Follower 행렬 오차 0으로 확인함. 팔의 240 Hz 두 루프 검사에서 인접 sample 각속도 변화의 최댓값은 0.165 rad/s 미만임. Native CTest 20/20·CPU-only 13/13·설치 SDK 소비자 검증을 실행함. macOS/WebGL2 결과는 실행 미검증임.

`RenderScheduler`의 비공개 저장 상태(epoch)가 추가되어 구조체 크기가 변경됨. 공개 함수 인수·Quality/Scene 파일 형식은 유지하며 정적 SDK 소비자는 헤더/라이브러리를 함께 재빌드해야 함. 바이너리 ABI는 고정하지 않음.

### PCSS Shadow

단일 Directional Sun의 직교 그림자에 blocker search → 거리별 penumbra → 가변 반경 PCF를 적용함. R32F raw map을 재사용하며 Spot/Point shadow·atlas·cascade는 제공하지 않음. Low는 Off, Medium은 Basic, High는 PCF 기본값을 유지하고 Custom에서 PCSS를 선택함.

| RenderSettings / QualityOverrides | 기본값 · 유효 범위 |
|---|---|
| `ShadowSourceAngle` | 전체 광원 각도 .535°, [0,20]°임. 0은 Basic과 같은 hard shadow임 |
| `ShadowBlockerSamples / ShadowFilterSamples` | 16 / 32, 각각 정수 [1,64]임 |
| `ShadowMaxFilterRadius` | 32, shadow-map texel 단위 [1,64]임 |
| `ShadowDepthBias / ShadowSlopeBias` | .0008 / .003, 정규화된 map depth 단위 [0,.1]임. Basic/PCF에도 적용함 |

```cpp
auto quality = lv::MakeQualityProfile(lv::QualityPreset::Medium);
quality.Overrides.Shadow = lv::ShadowMode::Pcss;
quality.Overrides.ShadowSourceAngle = 2.f;
quality.Overrides.ShadowBlockerSamples = 16;
quality.Overrides.ShadowFilterSamples = 32;
auto changed = viewer.SetQuality(quality); // Result<void> 확인 필요
// lv::SaveQuality(quality, "SoftSun.mvxquality.json")로 독립 저장함
```

직교 map의 선형 깊이를 world 거리로 변환하고 거리×tan(광원 각도/2)로 반경을 구함. 깊이의 perspective 나눗셈은 사용하지 않음. 고정 disk 샘플과 수신면 깊이 보정으로 프레임 간 패턴과 경사면 자기 그림자를 관리하며 맵 밖은 lit로 처리함. blocker가 없으면 완전한 lit, 최대 반경은 texel 기준으로 제한함. 동일한 Skinning·Morph·Instancing·Alpha Mask shadow pass를 사용하며 추가 attachment 메모리는 필요하지 않음.

`DebugView::ShadowBlockers / ShadowBlockerDepth / ShadowPenumbra / ShadowVisibility`는 각각 blocker 수/검색 수, 수신면 기준으로 보정한 평균 선형 깊이, 반경/최대 반경, 최종 가시성의 grayscale임. blocker가 없으면 깊이=1·반경=0임. 앞의 세 뷰는 PCSS, Visibility는 활성 shadow가 필요하며 부적합한 뷰/품질 전환은 `UnsupportedFeature`이고 이전 상태를 유지함. Editor Quality Settings와 Debugger에서 같은 설정·뷰를 제공하며 `minvex_rendering_enhancement --pcss`는 접촉·부유 그림자 예제임.

`ExpandQuality`는 비유한 값·범위 초과를 `InvalidArgument`로 반환함. `ResolveQuality`는 `QualityCapabilities::ShadowMapping`을 요구하고 `AllowFallback=true`인 경우만 shadow를 끔. 설정은 값으로 복사하며 Viewer/Embedded/Editor 적용은 기존 owner/graphics thread에서 pass 밖에 호출함. 변경은 On-Demand 이미지를 무효화함. Quality writer v2는 PCSS 키를 저장하고 reader는 v1~v2임. v1에 없는 값은 기본값이며 정적 SDK 1.19 소비자는 공개 구조체 변경에 따라 재빌드가 필요함.

### Python Binding

`import minvex as mvx`로 기존 C++ 엔진을 사용함. CPython 3.12·Windows/macOS 대상이며 `MINVEX_BUILD_PYTHON=ON`일 때만 pybind11을 요구함. GPU resource handle·Shader·Embedded Host는 Python에 노출하지 않음. 기본 C++ 구성은 Python 의존성이 없음.

```sh
python -m pip wheel . --wheel-dir out/wheels
python -m pip install --force-reinstall out/wheels/minvex-1.20.0-cp312-cp312-win_amd64.whl
python python/examples/viewer.py examples/assets/MorphRibbon.gltf --frames 60
```

루트 `pyproject.toml`의 scikit-build-core 빌드이며 CMake·C++20 도구와 플랫폼 SDK가 필요함. Windows는 x64 MSVC 런타임을 사용함. macOS wheel 파일명은 빌드 환경에 따름. 기본 wheel은 `MINVEX_BUILD_RENDERER=ON`을 매번 전달하므로 같은 `out/python/{wheel_tag}`의 이전 CPU-only OFF cache도 갱신함. `python -m pip wheel . --wheel-dir out/wheels-cpu -C cmake.define.MINVEX_BUILD_RENDERER=OFF`로 CPU-only wheel을 만들 수 있으며 이 구성의 `run()`은 `UnsupportedFeature`임. Native/CPU wheel의 버전과 파일명은 같으므로 출력 폴더를 구분하고, 같은 환경에 교체 설치할 때는 `--force-reinstall`을 사용함. PyPI 공개 설치를 전제로 하지 않으며 위 명령은 로컬 wheel 생성·설치임.

```python
import minvex as mvx

with mvx.Viewer() as viewer:
    model = viewer.add_model("character.glb")
    model.play_animation("Walk")  # index도 가능함
    quality = mvx.make_quality_profile(mvx.QualityPreset.Medium)
    quality.overrides.pbr = True
    viewer.set_quality(quality)
    material = model.get_material(0)
    material.shading = mvx.ShadingModel.Pbr
    material.roughness_factor = .3
    model.set_material(0, material)
    viewer.run(on_event=lambda e: print(e.occurrence.event.name))
```

| 대상 | 주요 API |
|---|---|
| `Viewer` | `ViewerOptions`, `add_model`, `scene`, `set_quality`, `quality`, `set_debug_view`, `frame_scene`, `request_redraw`, `run`, `request_close`, `is_running`, `close` |
| `Scene / Object` | `create_object`, `objects`, `object(id)`, `camera/sun`, `save/load`, `update_animations`; Object의 `id/name/transform/visible`, `set_parent`, `remove` |
| Animation / Blend Tree | `animation_clips`, `play_animation`, `pause/resume/stop_animation`, `seek_animation`, `animation_speed/loop/time`, `create_blend_tree(BlendTreeDesc)`, `play_blend_tree`, `set_blend_parameter`, `seek_blend_tree`, `blend_weights` |
| Pose / Additive | `skeleton_nodes`, `reference_pose`, `capture_pose`, `apply_pose/reset_pose`; `set/restore/clear_additive_animation`, `additive`, `set_additive_weight/mask`, `seek_additive` |
| Morph / Material | `morph_nodes`, `get/set/clear_morph_weights`; `get/set/reset_material`, `Material`의 PBR·texture·alpha 필드 |
| Quality / Event | `make_quality_profile`, `QualityProfile.overrides`, `expand/save/load_quality`; `AnimationEvent`, `set/reset_animation_events`, `poll/dispatch_animation_events`, `clear_animation_events` |

`Vec3`·`Quaternion`·`Transform`·Camera·Material·Quality·프레임/이벤트는 값 타입임. `model.transform`·`get_material()`·`scene.camera` 조회값을 수정한 뒤 다시 대입/설정해야 반영함. Clip 이름이 중복이면 index를 요구함. Morph node ID와 기본/현재 weight는 `morph_nodes`로 조회함. PBR 렌더링에는 C++과 같이 Advanced lighting·Pbr 품질 설정이 필요함.

`set_additive_animation(clip, weight=1, reference=Pose(), mask=[])`는 TRS 레이어 1개를 설정함. 빈 reference는 Skeleton reference이며 `reference_pose`·`capture_pose()` 또는 `Pose/PoseJoint`로 full/partial 기준을 지정함. `skeleton_nodes`는 node/name/parent/editable/is_joint 복사본이며 root parent는 None임. 비어 있지 않은 mask는 읽기 전용 노드도 포함한 전체 노드 수와 길이가 같아야 함. `additive`는 없으면 None, 있으면 독립 `AdditiveAnimationState` 복사본임. 수정한 time/speed/loop/weight/state/reference/joint_mask는 `restore_additive_animation(state)`로 검증·반영함. `Pose.joints`도 목록을 수정한 뒤 재대입함. 잘못된 mask·기준 Pose·시간은 기존 레이어를 유지하며 `InvalidArgument`임. Pause/Resume/Stop·수동 Pose override 순서는 C++ 계약과 같음.

Viewer가 C++ 소유자이며 Scene/Object는 약한 핸들임. Viewer 소멸·`close()` 후 접근은 `InvalidState`, 제거된 객체 접근은 `InvalidHandle`임. id는 식별자 값으로 유지됨. Scene load는 ID를 재매핑하고 이전 핸들을 무효화하며 `run/dispatch` 밖에서 호출함. `close()`는 owner thread에서 반복 호출 가능하고 실행/dispatch 중에는 `InvalidState`임. 실행 중에는 `request_close()` 후 run 반환 뒤 닫음.

생성 스레드에서 모델/Scene/애니메이션을 사용하며 위반은 `WrongThread`임. `request_close/is_running`만 다른 스레드에서 사용 가능함. macOS `run`은 main thread임. Blocking `run`·Import·파일 IO 중에는 GIL을 해제하고 Python callback은 owner thread에서 GIL을 확보함. `on_frame(viewer, info)` 뒤 `on_event(event)`를 전달하며 callback은 run 동안만 유지됨. 프레임·이벤트 복사본은 callback 반환·객체 삭제 후에도 유효함. 자동 애니메이션 진행을 끄려면 `advance_animations=False`임.

`mvx.Error`는 `RuntimeError` 계열이며 `code/message/hint/source_path`를 제공함. C++ Result·검증 예외를 변환하고 Python callback 예외는 `CallbackFailure`·`__cause__`로 원인을 보존함. Python 인수의 타입 오류는 `TypeError`일 수 있음. 이벤트 polling·수집 Seek·큐 배출·1회 전달은 기존 C++ 계약과 같으며 Python 객체를 저장 형식에 직렬화하지 않음.

### WebAssembly Binding

Emscripten 4.0.10·Embind·Sokol WebGL2의 별도 Web Host임. `MINVEX_BUILD_WEB`은 Emscripten에서 기본 ON이며 Native SDK·Python 구성은 기본 OFF임. Native Editor·WebGPU·pthread는 포함하지 않으며 COOP/COEP 없이 main thread에서 실행함. Fetch는 비동기이며 Import·Animation 평가는 C++ main thread 작업임.

셰이더 컴파일러 `sokol-shdc`는 빌드 호스트 기준으로 Windows·macOS·Linux x86_64/arm64에서 선택하며 고정 revision·SHA-256을 검증함. Emscripten 타깃 아키텍처와 구분함. 다른 호스트 또는 오프라인 빌드는 `MINVEX_SOKOL_SHDC`로 실행 파일을 지정함.

```sh
emcmake cmake -S . -B out/build/web -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF
cmake --build out/build/web --target minvex_web --parallel 4
python -m http.server 8000 --directory out/build/web/web
```

브라우저에서 `http://localhost:8000`의 파일 선택 예제를 실행함. 배포 파일은 `minvex-native.js/.wasm`, `minvex.js`, `index.html`임. HTTP(S)에서 Wasm MIME을 `application/wasm`으로 제공함. `cmake --install out/build/web --prefix out/web-sdk --component web`으로 라이선스와 함께 설치함. 테스트는 `web`의 Playwright 의존성 설치 후 `node web/tests/browser.mjs`이며 `MINVEX_WEB_DIR`로 빌드 출력 경로를 지정함.

```javascript
import {createViewer} from './minvex.js';
const viewer = await createViewer({canvas: document.querySelector('canvas'), quality: 'low'});
const model = await viewer.loadModel('./robot.glb');
if (model.info.clips.length) model.playAnimation(0);
// 종료 시 프레임·Fetch·입력·GPU·Embind 객체를 함께 해제함
viewer.dispose();
```

| 대상 | 주요 API |
|---|---|
| Viewer | `createViewer({canvas, quality, autoStart, onEvent, onError})`, `start/stop/render`, `setCamera/frameScene`, `setQuality/quality`, `pollAnimationEvents`, `dispose` |
| Asset | 비동기 `loadModel(url, options)`, `loadBuffer(bytes, {name, files, resolve})`, `loadFiles(FileList, {entry})`임. GLB·외부 glTF의 buffers/images·base64 URI를 지원함 |
| Object | `info`, `clone/remove`, `setTransform({position, rotation, scale})`, `setMaterial({baseColor, metallic, roughness, unlit}, index)`임. 배열은 Vec3·Quaternion·RGBA 순서임 |
| Animation | `play/pause/stop/seekAnimation`, `setAnimationSpeed`, `setMorphWeights/clearMorphWeights`, `playBlendTree`, `setBlendParameter/seekBlendTree`, `setAdditiveAnimation/setAdditiveWeight/seekAdditive/clearAdditiveAnimation`, `setAnimationEvents`임 |

`info`는 Transform·Clip·Node별 Morph weight·시간·Blend weight의 복사본임. Blend Tree description은 `kind: '1d'/'2d'`, `parameterX/parameterY`, `samples: [{clip, x, y}]`임. Additive의 `clip, weight=1, mask=[]`는 Skeleton reference를 기준으로 함. `seekAnimation(time, collect=false)`는 기본 무발생이며 이벤트는 `onEvent` 또는 polling으로 배출함. Clip·Node·Material index와 정수 품질 설정은 소수점·범위 초과 값을 `InvalidArgument`로 거부하며 다른 대상이나 적용 상태를 수정하지 않음.

메모리 입력은 임시 MEMFS에 복사한 뒤 기존 Importer를 호출하고 파일을 제거함. `files`는 URI→typed array의 Object/Map이며 `resolve(uri, absoluteURL, signal)`로 의존성을 제공할 수 있음. URL 로딩은 상대 URI를 원래 URL 기준으로 Fetch하고, File Input은 선택한 파일·상대 경로·유일한 basename으로 해석함. 네트워크/file URI를 C++ 파일 경로로 전달하지 않음. ArrayBuffer/typed array의 추가 소유 복사 전에 파일당 64 MiB·의존성 합계 128 MiB를 검사하며 초과는 `InsufficientResource`임. 이미지 최대 4096·정점 50만·Scene 객체 256개가 초기 제한임. dispose 후 resolver가 완료되어도 다음 의존성 요청이나 데이터 복사·Import를 진행하지 않음.

WebGL2 Skinning은 최소 uniform 사양에 맞춰 Skin당 48 Joint이며 초과는 `UnsupportedFeature`임. Morph는 C++ 평가→객체·Node별 정점 stream→GPU Skinning 순서로 적용하며 공유 Asset의 weight를 바꾸지 않음. 직접광 PBR·Normal Map·Static Instancing·Frustum Culling을 재사용함. Canvas는 CSS pixel 기준임. `setCamera`는 position/target/up·projection·orthographicHeight·fovRadians·nearPlane/farPlane을 지원하고 입력은 드래그 Orbit·우클릭 Pan·휠 Zoom임.

품질은 preset 문자열 또는 `{preset, allowFallback, pbr, targetFps, resolutionScale, shadow, postProcessing, msaaSamples, onDemand}`임. 초기 Web 경로는 Shadow/PCSS·HDR·BC3 OFF, MSAA·anisotropy 1임. `allowFallback` 기본 true이며 High의 미지원 값은 조정 내역으로 반환함. false이면 거부하고 이전 설정을 유지함. 웹 PCSS는 지원 기능으로 포함하지 않음.

Viewer마다 독립 Wasm module·Canvas를 사용하며 같은 Canvas의 중복 소유는 거부함. 브라우저 GC에 GPU 해제를 맡기지 않고 `dispose()`를 호출함. Object는 약한 Viewer 참조이며 삭제 후 `InvalidHandle`, dispose 후 `InvalidState`임. 미완료 Fetch는 취소하며 File 읽기 중 dispose도 완료 후 `InvalidState`임. File 읽기 실패는 `AssetLoad`이며 cause를 보존함. context loss에서는 프레임을 멈추고 CPU Scene을 유지하며 복구 시 extension·GPU 자원을 재생성함. `MinvexError`의 `code/message/hint/sourcePath/cause`를 제공함. rAF 오류는 `onError` 또는 Canvas의 `minvexerror` 이벤트로 전달함. callback 실패는 `CallbackFailure`이며 이벤트 큐는 최대 1024개임. 초과 프레임은 오류로 중단하고 새 전달분은 저장하지 않으며 시간은 이미 진행된 상태임.

### Embedded Runtime

호출 순서는 `Initialize()` → 프레임마다 `Update()/Render()` → 종료 시 `WaitIdle()/Shutdown()`임. Host가 창·이벤트·입력·device·target·Present를 관리함. resize 전에는 GPU 완료 대기 → `DetachTarget()` → Host target 교체 순서를 따름.

같은 device의 단일 mip/slice/sample 2D target, 1~8192 framebuffer pixels를 사용함. Color는 RGBA8/BGRA8 linear/sRGB, D3D11 depth는 D32_FLOAT 또는 D32_FLOAT_S8X24_UINT임. D3D11.1 immediate context의 Host state를 복원함. Metal은 borrowed queue로 순서를 연결하거나 별도 queue 사용 전에 Host가 동기화함. Scene depth 공유·alpha 합성은 지원하지 않음.

실제 통합 코드는 [Windows Host](../examples/EmbeddedViewer/MainD3D11.cpp) · [macOS Host](../examples/EmbeddedViewer/MainMetal.mm)를 참고함.

## macOS Metal 실행 조건

macOS 하드웨어 기준은 [README의 최소 사양](../README.md#최소-사양)에 기재한 Apple M1 이상 또는 A18 Pro임. macOS 실기기 검증과 최소 지원 OS 확정은 남아 있음.

- **백엔드·셰이더:** [CMake 셰이더 설정](../cmake/Shaders.cmake)은 `metal_macos` 소스를 생성함. 현재 고정 Sokol 백엔드는 Metal 라이브러리를 실행 중 컴파일하며 MSL 언어 버전을 명시적으로 고정하지 않음. Metal 3/4 전용 API가 필수라는 계약은 없으며 최소 Metal/MSL 버전은 실기기에서 확정해야 함. Windows에서 Metal 소스 생성 성공은 macOS GPU 컴파일·실행 검증을 대신하지 않음.
- **OS·아키텍처:** 프로젝트는 `CMAKE_OSX_DEPLOYMENT_TARGET`을 고정하지 않음. Apple silicon의 `arm64` 또는 Intel의 `x86_64`에 맞는 macOS SDK·컴파일러·라이브러리를 함께 사용해야 함. Apple의 Metal 지원 OS 범위와 Minvex 바이너리의 실행 가능한 OS 범위는 별도로 확인함. macOS용 Python wheel도 해당 플랫폼에서 빌드·검증해야 하며 Windows wheel을 그대로 사용하지 않음.
- **GPU 기능:** [GraphicsQuality.cpp](../src/renderer/GraphicsQuality.cpp)는 실제 컨텍스트의 컬러·깊이·Shadow·HDR·BC3 포맷과 MSAA sample count를 조회함. GPU 초기화 후 `GetQualityCapabilities()`를 확인하고 `AllowFallback` 정책에 따라 품질을 적용함. GPU 제품명만으로 High 품질이나 모든 포맷 지원을 확정하지 않음. Metal의 기능 조회 원칙은 [Apple GPU 기능 감지 안내](https://developer.apple.com/documentation/metal/detecting-gpu-features-and-metal-software-versions)를 참고함.
- **Embedded:** Host가 제공하는 `MTLDevice`·command queue·target의 실제 GPU와 포맷을 기준으로 판정함. 다중 GPU Intel Mac에서는 시스템에 나열된 GPU와 Host가 선택한 GPU가 다를 수 있음. 기존 owner-thread·target·동기화 계약을 함께 충족해야 함.
- **현재 검증 상태:** macOS 실기기 빌드, Standalone/Editor 실행·정상 종료, Embedded Host, Python wheel, 입력·resize·HiDPI, 품질별 GPU 출력은 미검증임. macOS 최소 지원 버전·GPU별 성능·공식 지원 기종은 위 검증을 수행한 뒤 확정함.

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
| **Scene** | writer v12 / reader v1~v12. 외부 모델 참조·계층·카메라·조명·Pose·Clip/Cross-fade·Blend Tree·Additive·Event overlay·재질/Morph override·Shared Skeleton·Leader Pose·Socket 저장. 로드 시 runtime ID 재매핑 |
| **Pose / Quality / Import** | Pose v1, Quality writer v2 / reader v1~v2, ImportVersion 6. Pose는 skeleton signature 호환 필요 |
| **Quality** | Low·Medium·High, Overrides가 있으면 Custom. 기본 `AllowFallback=false`; 미지원 설정 대체는 명시적으로 허용 |
| **SDK** | 정적 라이브러리. 같은 architecture·compiler/toolset·configuration·C++ runtime으로 소비자 재빌드 필요. 바이너리 ABI 비고정 |

Scene v12는 v11 이하만 지원하는 이전 SDK reader로 읽을 수 없음. Scene/Pose/Quality 저장은 임시 파일 작성 후 교체하며 모델 파일 자체를 Scene에 내장하지 않음. Undo 이력·UI 상태·Telemetry는 Scene에 저장하지 않음.

Scene의 모델·텍스처 디렉터리 상대 참조는 실제 부모 디렉터리를 기준으로 계산함. macOS의 `/var` → `/private/var` 같은 디렉터리 링크나 Windows junction을 통해 저장·로드해도 같은 리소스를 가리키며, 프로젝트를 함께 옮길 수 있음. 공개 함수와 Scene 형식 버전은 변경하지 않음.

Windows D3D11 핵심 실행 검증을 완료했으며 macOS Metal은 구현 상태로 실기기 미검증임. CPU-only 구성의 native 초기화·실행은 `UnsupportedFeature`를 반환함. IBL·Emissive/Occlusion·Alpha BLEND는 후속 범위임.

## 검증 범위

### Python wheel 캐시·Editor 회귀 수정 검증 · 2026-11-13

`pyproject.toml`의 wheel CMake 기본값에 `MINVEX_BUILD_RENDERER=ON`을 추가함. 명시적인 CPU-only OFF는 우선 적용하며, 이후 renderer 옵션을 생략한 기본 빌드는 같은 cache를 ON으로 되돌림. C++ SDK API·소유권·스레드·오류·저장 형식은 변경하지 않음.

Editor smoke는 `ResidentModels=1`을 별도로 검사하고 적용된 품질의 Instancing 설정에 따라 draw call을 검사함. 정적 SharedBox의 두 winding 그룹은 인스턴싱 ON에서 2개 draw call·`InstancedObjects=4`, OFF에서 4개 draw call·0개 인스턴싱을 요구함. 애니메이션 smoke는 2개 draw call·0개 정적 인스턴싱을 요구함. draw call 검사 자체를 제거하거나 허용 범위를 넓히지 않음.

| 재검증 항목 | 실행 결과 |
|---|---|
| 수정한 원본 Editor를 포함한 Windows MSVC Release 전체 확장 CTest | **42/42 통과**, 이전 `phase4.editor` 실패 해소 |
| 별도 Custom quality의 Static Instancing OFF Editor smoke | 저장/복원·파일 작업·Gizmo·Undo/Redo까지 통과 |
| 같은 기본 build-dir에서 CPU-only → 기본 native wheel | OFF wheel 생성·설치 후 renderer 옵션 없는 기본 wheel 빌드·설치 통과. cache OFF → ON, 실제 `graphics_available=False` → `True` 확인 |
| CPU-only wheel 바인딩·실행 거부 | `--expect-graphics off` **6/6 통과** |
| 기본 native wheel 바인딩 | `--expect-graphics on` **6/6 통과** |
| 기본 native wheel 실제 실행 | normal·frame callback 실패·event callback 실패·GIL 해제/다른 스레드 종료 **4/4 통과** |
| 이전 CPU-only wheel을 native로 검사하는 회귀 guard | `--expect-graphics on`에서 예상한 실패를 확인하여 잘못된 wheel을 잡는 것을 검증함 |

CI Python job도 같은 cache에서 CPU-only wheel을 먼저 만들고 기본 native wheel을 생성하여 각각 `--expect-graphics off/on`으로 검사함. Native 설치는 같은 버전의 CPU-only wheel을 교체하므로 `--force-reinstall`을 사용함. Windows에서 이 순서의 빌드·설치·테스트를 실행했으며 수정한 원격 CI와 macOS job은 아직 실행하지 않았음.

검증은 CPython 3.12·scikit-build-core 1.1.2·Windows MSVC Release에서 수행함. CPU-only 빌드에 로컬 Sokol/shdc 경로를 지정해 네트워크 다운로드를 피했으며, 같은 cache의 후속 기본 native 빌드에는 renderer·build-dir override를 전달하지 않았음. Visual Studio MSBuild가 샌드박스 안에서 진행되지 않아 같은 명령을 샌드박스 밖에서 실행하여 빌드·설치를 완료함. junction을 만드는 Python 바인딩 검사도 샌드박스 밖에서 실행함.

로그는 [전체 Native CTest](../out/verification-fixes/native.xml), [인스턴싱 OFF Editor](../out/verification-fixes/editor-no-instancing.log), [wheel cache 전환](../out/verification-fixes/cache-transition.log), [CPU-only wheel 빌드](../out/verification-fixes/wheel-cpu-build.log), [기본 native wheel 빌드](../out/verification-fixes/wheel-default-build.log), [CPU-only 바인딩](../out/verification-fixes/python-cpu-tests.log), [Native 바인딩](../out/verification-fixes/python-native-tests.log), [Native 실제 실행](../out/verification-fixes/python-native-runtime.log), [이전 잘못된 wheel의 guard 실패](../out/verification-fixes/old-wheel-guard.log)에 유지함. 아래 SDK·Web·CPU-only CTest 결과는 최초 감사 기록이며 이번 수정에서 반복 실행하지 않음.

### 수정 전 지원 기능 검증 · SDK 1.20.0 · 2026-11-13

기준은 README·이 문서의 현재 지원 기능이며 ProjectPlan의 후속 목표는 완료 판정에서 제외함. 검증 시작 소스는 `0c9c56d70b555116d3892e5ea7ec5e8d89e1bc39`임. 이 최초 감사 시점에는 구현 소스·원본 테스트를 변경하지 않았으며 문서의 SDK 버전·Scene 호환성 요약을 현재 구현과 맞춤.

| 구성 | 실행 결과 | 실제 확인 범위 |
|---|---|---|
| Windows MSVC Release, render smoke·extended tests ON | CTest 41/42 통과, `phase4.editor` 1개 실패 | Core·Asset·Scene·Animation·Quality·Telemetry·Result·Viewer·Editor·Embedded·장착 예제, D3D11 PBR/Morph/PCSS·디버그·최적화 픽셀 검사 |
| CPU-only MSVC Debug | CTest 13/13 통과 | GPU 없는 Scene·Asset·Pose·Animation·EditorDocument·Quality·Telemetry·장착·오류 처리 |
| Native Release·CPU-only Debug SDK | 양쪽 archive·SHA-256·설치/이동·독립 소비자 통과 | 모든 공개 헤더 개별 컴파일, CPU·Animation 소비자. Native 설치본의 Shared Skeleton·Leader Pose·Socket·Scene v12 소비자도 별도 실행 |
| CPython 3.12 Windows wheel | CPU-only 바인딩 6/6, Native 바인딩 6/6·native 모드 4/4 통과 | Unicode·디렉터리 junction·프로젝트 이동·Scene/재질·Animation/Tree/Morph·Pose/Additive·수명·오류·Quality, native 실행·프레임/이벤트 callback 실패·GIL 해제/다른 스레드 종료 |
| Emscripten Release·Web 배포 설치 | Chrome·Firefox 모두 통과 | Canvas 픽셀, GLB·외부 glTF·URL/파일/메모리, PBR·Skinning·독립 Morph·Tree·Additive·Event·입력 제한·context 복구·dispose. Chrome은 headless SwiftShader, COOP/COEP 없는 단일 thread |
| 릴리스 도구 | 합성 fixture 기반 14개 도구 검사·3개 패키지 계약 검사 통과 | 보고서 검증·변조 checksum 거부·multi-config 이름. 실제 Unity 대비 성능 측정은 아님 |

수정 전 확인한 문제(두 항목 모두 위의 후속 수정 검증에서 해소):

1. `editor/Main.cpp`의 `SmokeAfterDraw()`는 정적 모델 두 인스턴스에 draw call 4개를 기대함. 실제 진단값은 `ResidentModels=1`, `LastDrawCalls=2`, `InstancedObjects=4`임. 원본 smoke를 별도 실행해 실패를 재현함. `out/verification/EditorProbe.cpp` 복사본에서 기대값만 2개로 바꾸면 저장/복원·resize·파일 작업·Gizmo·Undo/Redo까지 통과함. 인스턴싱 동작과 어긋난 원본 회귀 검사 보완이 필요하며 원본 42개 전부 통과로 기록하지 않음.
2. `pyproject.toml`의 공통 `out/python/{wheel_tag}`에 renderer OFF cache가 있으면 기본 `pip wheel` 명령으로 만들어진 1.20.0 wheel도 `graphics_available=False`이고 native 실행에 실패했음. 당시 별도 build-dir·명시적 renderer ON wheel은 `graphics_available=True`이며 native 4개 모드를 통과했음. 후속 수정에서는 기본값을 보완하고 같은 cache에서의 기본 native 복구를 검증함.

증거는 로컬 `out/`에 유지함:

- [Native 전체 CTest 결과](../out/build/minvex-release/verification-native.xml), [Editor 실패 재현](../out/build/minvex-release/verification-editor-repro.xml), [CPU-only CTest 결과](../out/build/minvex-core-only/verification-core.xml)
- [Editor 진단](../out/verification/editor-probe.log), [Native SDK](../out/verification/native-sdk.log), [CPU-only SDK](../out/verification/core-sdk.log), [장착 SDK 소비자](../out/verification/equipment-sdk.log)
- [Python CPU-only 바인딩](../out/verification/python-tests.log), [Native 바인딩](../out/verification/python-renderer-tests.log), [Native 실행](../out/verification/python-renderer-native-tests.log), [기본 wheel의 native 실패](../out/verification/python-native-tests.log)
- [Web 빌드 출력 테스트](../out/verification/web-tests.log), [Web 설치본 테스트](../out/verification/web-installed-tests.log), [릴리스 도구](../out/verification/release-tools.log)

SDK 검사 중 MSVC compiler probe의 PDB 오류는 `_CL_=/Z7`로 우회함. 브라우저 loopback HTTP와 Python junction 검사는 샌드박스 밖에서 같은 테스트를 재실행하여 통과함. 이는 제품 실패와 구분함. macOS Metal·macOS Python wheel·Safari·모바일·원격 CI·최소 사양 기기의 성능과 메모리 목표는 실행하지 않았음. OS 파일 대화상자의 실제 사용자 조작 전체는 자동 smoke가 대체하지 않으며 테스트가 주입한 파일 선택 이후의 처리 경로를 확인한 범위임.

### 이전 검증 기록

SDK 1.19.1 Windows Release의 관련 핵심 CTest 5종(Asset·Scene·Animation·Quality·통합 D3D11 픽셀)을 확인함. FBX 비정수 길이·끝 키 보존, PBR·Morph·Blend Tree·Additive·Event·PCSS를 포함함. 기존 CTest 13종 등록은 유지함.

Python native·CPU-only Wheel 빌드·설치, 핵심 바인딩 5종·native 실행·CPU-only 실행 오류를 확인함. Pose/Additive 상태·mask·snapshot·오류 시 기존 상태 유지도 포함함.

이번 CI 오류 수정에서는 Windows native Wheel을 재생성·설치하고 디렉터리 링크 경로·텍스처 디렉터리·프로젝트 이동을 포함한 바인딩 6종과 C++ Document·Viewer·Animation CTest 3종을 확인함. 기존 wheel에서 경로 회귀 테스트의 실패를 재현한 뒤 수정 wheel의 통과를 확인함. macOS의 기존 실패 로그에서 Scene 재로딩 경로 오류를 확인했으며, 수정 후 macOS 실행 결과는 아직 없음.

Emscripten 4.0.10 Release 빌드·Web 배포 설치와 Chrome·Firefox의 실제 Canvas 픽셀·GLB/외부 glTF·PBR·Skinning·독립 Morph·Blend Tree·Additive·Event·파일 입력·context 복구·dispose를 확인함. 정수 입력 거부·입력 크기 제한·취소 후 resolver 중단을 포함함. Chrome은 headless SwiftShader 경로이며 COOP/COEP 없는 단일 thread 실행임. Safari·모바일·macOS 실기기·원격 CI는 이번에 실행하지 않았음.

Ubuntu CI의 `sokol-shdc` 선택 누락을 수정하고 Linux x86_64·arm64 선택 경로의 CMake configure·공식 바이너리 다운로드·SHA-256 검증을 Windows에서 수행함. Emscripten Release도 새 출력 폴더에서 빌드하고 Web 배포 설치·Chrome/Firefox 테스트를 확인함. Linux 바이너리 실행과 수정 후 Ubuntu 전체 CI는 미검증임.

[기능·빌드 안내](../README.md) · [공식 SDK 예제](../examples/SdkConsumer) · [공개 헤더](../include/minvex)
