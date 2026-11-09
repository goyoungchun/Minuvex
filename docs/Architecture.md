# Phase 1 Architecture

## 모듈 경계

```text
FoundationMesh (window, loop, input, graphics-context ownership)
  ├─ Sokol App + Glue + Log
  └─ minuvex::renderer (GPU mesh resources, pipeline, draw)
       ├─ Sokol GFX (D3D11 / Metal)
       └─ minuvex::core (Math, MeshData, validation)
```

`minuvex::core`는 플랫폼 API, Sokol, UI에 의존하지 않습니다. 공개 헤더는 `include/minuvex/Math.h`, `Mesh.h`이며 CPU 데이터만 노출합니다. `minuvex::renderer`의 현재 진입점은 `src/renderer/MeshRenderer.h`의 내부 API입니다. Phase 3 Facade를 미리 확정하지 않으며, Sokol 타입을 기본 공개 API에 노출하지 않습니다.

Sokol App/Glue는 예제 실행 파일 쪽에만 연결합니다. Renderer는 Window를 생성하거나 `sg_setup`/`sg_shutdown`을 호출하지 않습니다. 향후 Host Integration에서 그래픽 컨텍스트를 주입할 수 있도록 이 경계를 유지합니다. Phase 1은 Embedded Viewer 호환성을 보장하지 않습니다.

## 좌표 및 렌더링 계약

- 오른손 좌표계, Y-up, 카메라 로컬 전방 -Z
- column-major 행렬 저장, column vector: `projection * view * model * position`
- 원근 나눗셈 후 깊이 범위 `[0, 1]` (D3D11/Metal 공통)
- 삼각형 목록, CCW 앞면, 뒷면 제거, 깊이 쓰기 및 LessEqual 테스트
- 정점: float3 위치, float3 법선, float4 색상 / uint16 인덱스
- Phase 1 model 행렬은 회전 전용입니다. 비균일 스케일의 법선 변환은 아직 지원하지 않습니다.
- 셰이더는 정점 색상에 고정 방향광과 ambient를 적용합니다. 텍스처·Scene Light는 아직 없습니다.

`ValidateMesh`는 비어 있는 버퍼, 잘못된 삼각형 개수, 인덱스 범위, 비유한 정점 속성, 0 법선, 16비트 정점 한계를 GPU 업로드 전에 검사합니다. Phase 1의 잘못된 입력은 `std::invalid_argument`, 내부 호출 순서 오류는 `std::logic_error`, GPU 생성 실패는 `std::runtime_error`를 사용합니다. 구조화된 Result/Error API는 Phase 3에서 도입합니다. 예제는 Sokol 콜백 경계 안에서 예외를 처리합니다.

## 리소스 소유권

1. 예제의 Sokol App이 Window, GPU device/context, swapchain을 소유합니다.
2. 초기화 콜백에서 Glue로 환경을 받아 Sokol GFX를 초기화합니다.
3. MeshRenderer가 정점·인덱스 버퍼, 셰이더, 파이프라인을 생성하고 소유합니다. 입력 MeshData는 업로드 후 CPU 측에서 해제해도 됩니다.
4. 매 프레임 예제가 pass를 시작하고 Renderer가 draw를 제출합니다. 예제가 pass 종료 및 commit을 담당합니다.
5. 종료 시 Renderer의 리소스를 역순으로 해제한 뒤 Sokol GFX, 마지막으로 App의 컨텍스트를 종료합니다.

GPU 호출은 컨텍스트를 소유한 단일 렌더링 스레드에서만 수행합니다. Renderer 복사는 금지합니다. 생성 중 실패하면 부분 생성된 리소스도 해제합니다. 창 크기가 0인 프레임은 건너뛰며 실제 framebuffer 크기로 투영 비율을 계산합니다.

## 셰이더 빌드

`shaders/mesh.glsl`을 수정하면 CMake custom command가 고정된 `sokol-shdc`로 `mesh.glsl.h`를 빌드 디렉터리에 생성합니다. HLSL5와 Metal 소스를 둘 다 생성하고 백엔드 매크로로 필요한 부분을 선택합니다. Phase 1은 소스 임베딩 방식이며 실제 GPU 셰이더 컴파일은 초기화 시 D3D11/Metal이 수행합니다. 두 언어 생성 성공이 macOS GPU 실행 검증을 대체하지는 않습니다.

공식 자료: [Sokol](https://github.com/floooh/sokol), [sokol-shdc](https://github.com/floooh/sokol-tools/blob/master/docs/sokol-shdc.md).
