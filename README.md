# Malloc Lab Docker Workspace

이 저장소는 **CS:APP Malloc Lab**을 Docker + VSCode DevContainer 환경에서 바로 실습할 수 있도록 구성한 프로젝트입니다.  
직접 `malloc`, `free`, `realloc`을 구현하면서 동적 메모리 할당기의 동작 원리와 메모리 관리 기법을 익히는 것이 핵심 목표입니다.

## 프로젝트 주제

이번 프로젝트의 주제는 **나만의 메모리 할당기 구현**입니다.

- C 언어로 동적 메모리 할당기를 직접 구현합니다.
- `mdriver`를 통해 정합성과 성능을 함께 검증합니다.
- 단순히 동작하는 코드가 아니라, **공간 활용도**와 **처리량**까지 개선하는 것이 목표입니다.
- 기본 구현은 `implicit free list` 방식에서 시작하고, 여유가 있다면 `explicit free list`, `segregated list` 등으로 확장할 수 있습니다.

## 구현 대상

실제로 구현해야 하는 핵심 파일은 [`malloc-lab/mm.c`](/workspaces/jungle-sw-ai-malloc-lab-docker/malloc-lab/mm.c) 입니다.

이 파일에서 아래 함수들을 작성하게 됩니다.

- `mm_init`
- `mm_malloc`
- `mm_free`
- `mm_realloc`

드라이버 프로그램인 [`malloc-lab/mdriver.c`](/workspaces/jungle-sw-ai-malloc-lab-docker/malloc-lab/mdriver.c) 가 여러 trace 파일을 실행하면서 구현의 정확성과 성능을 채점합니다.

## 학습 포인트

- 힙이 어떻게 확장되는지
- 블록 헤더/푸터를 어떻게 설계할지
- 할당/해제 시 단편화를 어떻게 줄일지
- `realloc` 시 기존 데이터를 어떻게 안전하게 유지할지
- 정렬(alignment), coalescing, free list 관리가 성능에 어떤 영향을 주는지

관련 키워드는 `sbrk`, 힙 메모리, 단편화, 포인터 연산, 메모리 정렬입니다.

## 빠른 시작

1. VSCode에서 이 저장소를 엽니다.
2. `Dev Containers: Reopen in Container`로 컨테이너 환경을 시작합니다.
3. [`malloc-lab`](/workspaces/jungle-sw-ai-malloc-lab-docker/malloc-lab) 디렉터리에서 과제를 진행합니다.
4. `make`로 빌드한 뒤 `./mdriver` 또는 `./mdriver -f traces/binary2-bal.rep`처럼 원하는 trace로 테스트합니다.

## 저장소 구조

- [malloc-lab](/workspaces/jungle-sw-ai-malloc-lab-docker/malloc-lab): 과제 본체와 채점 드라이버
- [.devcontainer](/workspaces/jungle-sw-ai-malloc-lab-docker/.devcontainer): Docker/DevContainer 설정
- [.vscode](/workspaces/jungle-sw-ai-malloc-lab-docker/.vscode): 공용 디버깅 및 작업 실행 설정
- [DOCKER_DEVCONTAINER_GUIDE.md](/workspaces/jungle-sw-ai-malloc-lab-docker/DOCKER_DEVCONTAINER_GUIDE.md): 개발 환경 설정 문서

## 참고 자료

- [`malloc-lab/README.md`](/workspaces/jungle-sw-ai-malloc-lab-docker/malloc-lab/README.md): 원본 Malloc Lab 안내
- `CS:APP` 9장
- CMU Malloc Lab PDF

이 저장소의 목적은 Docker 설정 자체가 아니라, **Malloc Lab 구현을 빠르게 시작하고 반복적으로 실험할 수 있는 작업 환경**을 제공하는 데 있습니다.
