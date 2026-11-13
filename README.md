# Minuvex

**Lightweight 3D Graphics Engine**

### 한국어
C++ 애플리케이션에 3D 그래픽 기능을 간편하게 추가하는 경량 C++20 SDK 제공
GLB/glTF·OBJ·FBX 모델, Scene, Skeleton·Pose·Animation 및 렌더링 디버깅 지원  
Standalone·Embedded·Editor 실행 방식과 Low·Medium·High·Custom 품질 설정 제공

### English
Minuvex is a lightweight C++20 graphics SDK for integrating 3D rendering into C++ applications.  
Supports glTF/GLB, OBJ, FBX, scenes, skeletal animation, and rendering diagnostics.  
Offers Standalone, Embedded, and Editor modes with Low, Medium, High, and Custom quality settings.

## 빠른 설치 및 실행

### Windows
**CMake 3.24+, C++20 컴파일러, Ninja, Developer PowerShell x64**

```powershell
cmake --preset windows-release
cmake --build --preset windows-release --parallel 4
./out/build/windows-release/bin/minuvex_standalone.exe "path/to/model.glb"
./out/build/windows-release/bin/minuvex_editor.exe
# 금속/비금속·거칠기 비교 예제
./out/build/windows-release/bin/minuvex_rendering_enhancement.exe --pbr
```

### macOS
**CMake 3.24+, Xcode Command Line Tools, Ninja**

```sh
cmake --preset macos-release
cmake --build --preset macos-release --parallel 4
./out/build/macos-release/bin/minuvex_editor
```

### 기존 C++ 프로젝트에 SDK 연결
**CMake 설치 및 `minuvex::viewer` 연결**

```powershell
cmake --install out/build/windows-release --config Release --prefix out/sdk-native
```

```cmake
find_package(Minuvex 1.11 CONFIG REQUIRED COMPONENTS viewer graphics)
add_executable(my_viewer main.cpp)
target_link_libraries(my_viewer PRIVATE minuvex::viewer)
```

```powershell
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="C:/path/to/out/sdk-native"
cmake --build build
```

## 분야별 지원 기능

| 분야 | 현재 지원 기능 |
|---|---|
| **Asset** | glTF/GLB, OBJ/MTL, FBX(Static·Skin·Animation Bake), 검사·재임포트·Hot Reload·비동기 로딩 |
| **Scene** | 객체 계층·Transform, 모델 인스턴스 공유, Scene 저장·불러오기 |
| **Mesh** | Static/Skeletal Mesh, GPU Skinning(정점당 최대 4개 영향) |
| **Material·Lighting** | Base Color·Texture·Normal Map, PBR Metallic/Roughness, Directional/Point/Spot Light, Basic/PCF Shadow, HDR·FXAA |
| **Camera** | Perspective/Orthographic, Orbit·Pan·Zoom, 자동 Focus, Bounds Picking |
| **Skeleton·Pose** | Bone Transform, Reference/Full/Partial Pose, Override, Pose 저장·불러오기 |
| **Animation** | Clip 재생·정지·반복·역재생, 속도·시간 제어, Clip Cross-fade, Timeline Scrubbing |
| **실행 방식** | Standalone Viewer, Embedded Runtime, Editor |
| **Editor UX** | OS 파일 탐색기, Quick Open, Drag & Drop, Asset Browser, Hierarchy, Inspector, Gizmo, Undo/Redo |
| **Debugging** | Wireframe, Normal·UV·Depth, Skeleton·Bone Weight, Shadow Map, Bounding Box, GPU Resource Inspector |
| **Quality** | Low·Medium·High·Custom, 프로파일 저장·불러오기, 지원 기능 검사·Fallback |
| **Hardware·Performance** | CPU/GPU/OS 정보, FPS·Frame Time, 메모리 사용량·예산, 품질 추천 |
| **Optimization** | Frustum Culling, Rigid LOD, Static Instancing, Mesh Optimization, BC3, On-Demand Rendering |
| **SDK** | C++20, CMake 설치·연결, Facade API, Result/Error, 공식 예제, CPU-only 구성 |
| **Platform** | Windows D3D11, macOS Metal |

**PBR 범위:** 직접광 기반임. Low/Medium 기본 OFF, High ON이며 Custom에서 설정 가능함. IBL·Emissive/Occlusion·Alpha BLEND는 미지원임.

**미지원·후속 개발:** Morph Target, Blend Tree·Additive·Animation Event, PCSS, Python·WebAssembly 바인딩.

**확장 진행:** Phase 1(PBR) 구현·Windows 검증 완료임. Phase 2(Morph) 이후는 후속 개발임.

**검증:** Windows Release 핵심 6종·CPU-only Scene/재질 1종, PBR GPU 단독 15개 검사, SDK ZIP·독립 소비자·공개 헤더 18개 통과함. macOS 실기기는 미검증임.

## 최소 사양

| 항목 | 사양 |
|---|---|
| **Windows** | x64, DirectX 11 Feature Level 11_0 이상 |
| **macOS** | Metal 지원 GPU(최소 OS·장치 및 실기기 미검증) |
| **RAM** | 4GB 목표, 실측 최소 용량 미확정 |
| **개발 도구** | CMake 3.24+, C++20 컴파일러, Ninja, 플랫폼 SDK |
