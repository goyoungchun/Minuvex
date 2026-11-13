# Minvex

<p align="center">
  <img src="assets/branding/Minvex_banner_transparent.png" alt="Minvex — Lightweight 3D Graphics Engine" width="760">
</p>

**Lightweight 3D Graphics Engine**

### 한국어
C++ 애플리케이션에 3D 그래픽 기능을 간편하게 추가하는 경량 C++20 SDK 제공
GLB/glTF·OBJ·FBX 모델, Scene, Skeleton·Pose·Animation 및 렌더링 디버깅 지원  
Standalone·Embedded·Editor 실행 방식과 Low·Medium·High·Custom 품질 설정 제공

### English
Minvex is a lightweight C++20 graphics SDK for integrating 3D rendering into C++ applications.  
Supports glTF/GLB, OBJ, FBX, scenes, skeletal animation, and rendering diagnostics.  
Offers Standalone, Embedded, and Editor modes with Low, Medium, High, and Custom quality settings.

## 빠른 설치 및 실행

### Windows
**CMake 3.24+, C++20 컴파일러, Ninja, Developer PowerShell x64**

```powershell
cmake --preset windows-release
cmake --build --preset windows-release --parallel 4
./out/build/windows-release/bin/minvex_standalone.exe "path/to/model.glb"
./out/build/windows-release/bin/minvex_editor.exe
# 금속/비금속·거칠기 비교 예제
./out/build/windows-release/bin/minvex_rendering_enhancement.exe --pbr
./out/build/windows-release/bin/minvex_rendering_enhancement.exe --morph
```

### macOS
**CMake 3.24+, Xcode Command Line Tools, Ninja**

Apple silicon과 Metal 지원 Intel Mac의 하드웨어 범위는 아래 [Mac 호환 범위](#mac-호환-범위)를 참고함. macOS 실기기 빌드·실행은 아직 미검증임.

```sh
cmake --preset macos-release
cmake --build --preset macos-release --parallel 4
./out/build/macos-release/bin/minvex_editor
```

### 기존 C++ 프로젝트에 SDK 연결
**CMake 설치 및 `minvex::viewer` 연결**

```powershell
cmake --install out/build/windows-release --config Release --prefix out/sdk-native
```

```cmake
find_package(Minvex 1.12 CONFIG REQUIRED COMPONENTS viewer graphics)
add_executable(my_viewer main.cpp)
target_link_libraries(my_viewer PRIVATE minvex::viewer)
```

```powershell
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="C:/path/to/out/sdk-native"
cmake --build build
```

## 분야별 지원 기능

| 분야 | 현재 지원 기능 |
|---|---|
| **Asset** | glTF/GLB, OBJ/MTL, FBX(Static·Skin·Animation·Blend Shape Bake), 검사·재임포트·Hot Reload·비동기 로딩 |
| **Scene** | 객체 계층·Transform, 모델 인스턴스 공유, Scene 저장·불러오기 |
| **Mesh** | Static/Skeletal Mesh, GPU Skinning, 정점당 최대 4개 Bone 영향 |
| **Morph** | Morph Target, GPU Morph, 노드별 독립 Weight, Weight Animation |
| **Material·Lighting** | Base Color·Texture·Normal Map, Directional/Point/Spot Light, Basic/PCF/PCSS Shadow, HDR·FXAA |
| **PBR** | Metallic/Roughness Material·Texture |
| **Camera** | Perspective/Orthographic, Orbit·Pan·Zoom, 자동 Focus, Bounds Picking |
| **Skeleton·Pose** | Bone Transform, Reference/Full/Partial Pose, Override, Pose 저장·불러오기 |
| **Animation** | Clip 재생·정지·반복·역재생, 속도·시간 제어, Pose Blending·채널 마스크, Blend1D/Blend2D, Additive Animation·Joint Mask, Animation Event, Clip Cross-fade, Timeline Scrubbing |
| **실행 방식** | Standalone Viewer, Embedded Runtime, Editor |
| **Editor UX** | OS 파일 탐색기, Quick Open, Drag & Drop, Asset Browser, Hierarchy, Inspector, Gizmo, Blend Tree·Additive·Event Track 편집, Undo/Redo |
| **Debugging** | Wireframe, Normal·UV·Depth, Skeleton·Bone Weight, Shadow Map·Blocker·Penumbra·Visibility, Bounding Box, GPU Resource Inspector |
| **Quality** | Low·Medium·High·Custom, 프로파일 저장·불러오기, 지원 기능 검사·Fallback |
| **Hardware·Performance** | CPU/GPU/OS 정보, FPS·Frame Time, 메모리 사용량·예산, 품질 추천 |
| **Optimization** | Frustum Culling, Rigid LOD, Static Instancing, Mesh Optimization, BC3, On-Demand Rendering |
| **SDK** | C++20, CMake 설치·연결, Facade API, Result/Error, 공식 예제, CPU-only 구성 |
| **Python** | CPython 3.12, Viewer·Scene·Animation·Blend Tree·Morph·Material·Quality·Event API, Wheel |
| **Web** | WebAssembly·WebGL2 Canvas, JavaScript API, URL·파일·메모리 로딩, Animation·Morph·PBR, Context 복구 |
| **Platform** | Windows D3D11, macOS Metal, Browser WebGL2 |

## 최소 사양

| 항목 | 사양 |
|---|---|
| **Windows** | x64, DirectX 11 Feature Level 11_0 이상 |
| **macOS** | Apple silicon 또는 Metal 지원 Intel Mac. 아래 하드웨어 범위 참조; Minvex 실기기 검증 전 |
| **RAM** | 4GB 목표 |
| **개발 도구** | CMake 3.24+, C++20 컴파일러, Ninja, 플랫폼 SDK |

### Mac 호환 범위

2026-11-13 Apple 공식 자료 기준임. 아래 표는 **Metal을 지원하는 하드웨어 후보**이며 Minvex의 실행·성능 보장 목록은 아님. 현재 macOS 실기기 검증과 최소 지원 OS 확정은 남아 있음.

| Mac의 칩 / GPU | 해당 하드웨어 | Metal 지원 및 확인 사항 |
|---|---|---|
| **Apple M1·M2·M3·M4·M5·M6** | 각 계열에서 실제 출시된 Pro·Max·Ultra 변형을 포함한 Apple silicon Mac | Metal 지원. MacBook Air/Pro, iMac, Mac mini/Studio/Pro 중 Apple silicon 탑재 모델. M6는 2026년형 14형 MacBook Pro 사양에서 확인 |
| **Apple A18 Pro** | MacBook Neo | Apple silicon Mac으로 Metal 지원. M 시리즈가 아닌 A18 Pro 탑재 Mac도 대상 |
| **Intel CPU + Intel GPU** | 예: Intel UHD Graphics 630, Intel Iris Plus Graphics | Apple의 Metal GPU 표에 등재됨. CPU의 i5/i7/i9 이름만으로 판단하지 않고 실제 GPU·Mac 모델·OS 확인 필요 |
| **Intel CPU + AMD GPU** | 예: AMD Radeon Pro 500 계열, Vega, Radeon 5000·6000 계열 | Metal 지원 범위에 포함됨. Radeon Pro 500 계열을 사용하는 Intel 모델은 Apple의 Metal 3 대상에서 제외됨 |
| **그 밖의 구형 Intel Mac** | Apple의 Metal/Metal 2 지원 모델 | 해당 macOS의 GPU 드라이버와 빌드 도구까지 확인해야 함. Metal 지원 이력만으로 최신 Minvex 빌드 가능 여부를 확정하지 않음 |

Apple silicon 범위는 [Apple의 칩 탑재 Mac 목록](https://support.apple.com/en-us/116943), GPU별 범위는 [Metal Feature Set Tables](https://developer.apple.com/metal/Metal-Feature-Set-Tables.pdf)를 근거로 함. 최신 칩의 제품 근거: [M6 MacBook Pro](https://support.apple.com/en-us/148300), [A18 Pro MacBook Neo](https://www.apple.com/macbook-neo/specs/).

Apple의 모델·OS별 Metal 범위는 다음과 같음. 출처: [Apple Metal 지원 안내](https://support.apple.com/en-us/102894).

- **Metal/Metal 2:** MacBook Pro·Air·Mac mini·iMac은 2012년 이후, MacBook은 2015년 이후, Mac Pro는 Late 2013 이후. Mid 2010/Mid 2012 Mac Pro는 Metal 지원 그래픽 카드 장착 필요. Apple이 안내하는 OS 범위는 OS X 10.11 이후이며, 이는 Minvex의 최소 OS를 뜻하지 않음.
- **Metal 3:** macOS Ventura 13 이후와 대상 GPU 필요. MacBook Pro/MacBook은 2017년 이후, Air/mini는 2018년 이후, iMac은 2020년 이후 및 21.5형 2017년 모델·iMac Pro, Mac Pro는 2019년 이후, Mac Studio는 2022년 이후가 Apple 목록에 포함됨. 위 Radeon Pro 500 계열 예외 적용.
- **Metal 4:** Apple silicon Mac과 macOS Tahoe 26 이후 조합이 Apple의 지원 대상임. 각 Mac 자체가 지원하는 OS도 충족해야 함. Minvex의 Metal 3/4 필수 요구사항은 현재 별도로 선언하지 않음.

**현재 Minvex 조건:** Sokol Metal 백엔드와 `metal_macos` 셰이더를 사용함. 최소 macOS deployment target은 프로젝트에서 고정하지 않았고, 실제 사용 가능 OS는 C++20 도구 체인·빌드 SDK·GPU 기능·셰이더 컴파일을 함께 검증해 확정해야 함. 네이티브 빌드는 Apple silicon의 `arm64`, Intel Mac의 `x86_64`에 맞춰 SDK와 애플리케이션을 구성함. Shadow·HDR·MSAA·텍스처 압축 지원은 실행 중 GPU capability에 따라 달라짐. 세부 계약은 [API 문서](docs/API.md#macos-metal-실행-조건)를 참고함.

**내 Mac 확인:** Option 키를 누른 채 Apple 메뉴 → 시스템 정보 → 그래픽/디스플레이에서 GPU 이름과 `Metal 지원` 또는 `Metal Family`를 확인함. 터미널에서는 다음 명령으로 GPU/Metal 정보와 OS 버전을 확인할 수 있음. 확인한 정보를 실제 macOS 실행 검증 결과와 함께 기록해야 지원 확정으로 취급함.

```sh
system_profiler SPDisplaysDataType
sw_vers -productVersion
```
