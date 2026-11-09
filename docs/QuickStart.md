# Phase 1 Quick Start

## Windows

Visual Studio Installer에서 **Desktop development with C++**, Windows SDK, C++ CMake tools를 설치합니다. x64 Developer PowerShell/Command Prompt에서 README의 preset 명령을 실행합니다. 일반 PowerShell에서 `cl`, `cmake`, `ninja`를 찾지 못한다면 개발자 셸을 사용하거나 Visual Studio에서 이 폴더를 열고 preset을 선택하세요.

Ninja 없이 Visual Studio generator를 사용해도 됩니다. 설치 버전에 맞는 기본 generator를 CMake가 선택합니다.

```powershell
cmake -S . -B out/build/vs -A x64
cmake --build out/build/vs --config Debug --parallel
ctest --test-dir out/build/vs -C Debug --output-on-failure
.\out\build\vs\bin\Debug\minuvex_foundation.exe
```

## macOS

Xcode Command Line Tools와 CMake를 설치합니다. Ninja가 있으면 README의 preset을 사용합니다. Ninja 없이 기본 Makefile generator로 빌드할 수도 있습니다.

```sh
cmake -S . -B out/build/macos -DCMAKE_BUILD_TYPE=Debug
cmake --build out/build/macos --parallel
ctest --test-dir out/build/macos --output-on-failure
./out/build/macos/bin/minuvex_foundation
```

Sokol 구현 파일만 Objective-C++/ARC로 컴파일하고, 엔진 로직은 C++20을 사용합니다. 셰이더 컴파일러는 빌드 호스트의 Intel/Apple Silicon 아키텍처에 맞춰 받습니다. macOS 최소 배포 버전 및 지원 GPU 범위는 실기기 검증 후 확정합니다.

## 검증

CPU 테스트는 창이나 GPU가 필요하지 않습니다. Debug/Release 양쪽에서 동작하며 `assert`가 제거되는 Release에서도 검사를 수행합니다.

```sh
ctest --test-dir out/build/macos --output-on-failure
```

실제 렌더링 점검은 데스크톱과 D3D11/Metal GPU가 필요합니다. `--smoke-test`는 8프레임을 제출한 뒤 종료하고, 초기화/리소스 생성/렌더링 오류 또는 조기 종료 시 0이 아닌 코드를 반환합니다. 이 점검은 픽셀 비교 테스트가 아니므로 화면도 직접 확인해야 합니다.

```powershell
.\out\build\windows-debug\bin\minuvex_foundation.exe --smoke-test
```

CTest에 추가하려면 configure 시 `-DMINUVEX_ENABLE_RENDER_SMOKE_TEST=ON`을 지정하세요. 그래픽 환경이 없는 CI에서는 기본값 OFF를 유지합니다.

시각 검증 항목:

1. 어두운 배경 중앙에 여러 색의 큐브가 표시되고 면마다 밝기가 다릅니다.
2. 회전 시 뒷면이 앞면을 덮지 않고, 카메라를 향한 면이 사라지지 않습니다.
3. 창 크기·최소화·복원·HiDPI 변경 후에도 비율이 맞고 정상 렌더링합니다.
4. Space로 일시정지/재개, Esc 또는 창 닫기로 종료합니다.
5. Windows와 macOS에서 같은 메시·좌표계·색상·조명으로 표시합니다.

## GPU 없는 Core 빌드

Sokol 다운로드 및 플랫폼 GPU 의존성을 모두 제외합니다.

```sh
cmake --preset core-only
cmake --build --preset core-only
ctest --preset core-only
```

## 오프라인 구성

이미 준비한 고정 버전 Sokol 소스와 해당 호스트용 `sokol-shdc` 경로를 지정합니다. 버전은 [외부 의존성](../third_party/README.md)과 일치해야 합니다. 사용자 지정 경로의 내용과 버전은 사용자가 관리합니다.

```sh
cmake -S . -B out/build/offline \
  -DFETCHCONTENT_SOURCE_DIR_SOKOL=/path/to/sokol \
  -DMINUVEX_SOKOL_SHDC=/path/to/sokol-shdc
```

첫 다운로드 이후 기존 빌드 디렉터리를 그대로 사용하면 체크섬이 일치하는 의존성을 재사용합니다. 생성된 셰이더와 다운로드는 `out/` 아래에 두며 커밋하지 않습니다.
