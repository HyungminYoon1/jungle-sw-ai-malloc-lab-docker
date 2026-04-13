# Malloc Lab Experiments

## 목적

`mm.c` 구현을 단계적으로 개선하면서 correctness를 유지한 채 성능 점수를 올리는 과정을 기록한다.

기본 측정 기준은 아래와 같다.

- 실행 명령: `./mdriver -g`
- 주요 지표:
  - `util`: 메모리 사용 효율
  - `thru`: 처리량
  - `perf index`: `util + thru`

## 브랜치 요약

- `implicit-free-list`
  - implicit free list 기반 구현
- `explicit-free-list`
  - explicit free list 전환
- `segregated-free-lists`
  - segregated free lists 도입
- `segregated-tuning`
  - size class, 탐색 정책, split 정책 실험
- `segregated-prev-alloc`
  - allocated block footer 제거, `prev_alloc` 비트 도입

## 단계별 실험 기록

### 1. Implicit Free List

- 구조:
  - implicit free list
  - boundary tag
  - first fit
- 결과:
  - correctness 통과
  - 높은 점수 확인
- 비고:
  - 구현은 단순하지만, explicit/segregated 전환 실험을 위해 별도 브랜치로 보존

### 2. Explicit Free List

- 구조:
  - free block payload에 `pred/succ` 저장
  - explicit doubly linked free list
- 주요 변경:
  - `insert_free_block()`
  - `remove_free_block()`
  - `find_fit()`
  - `coalesce()`
  - `place()`
- 결과:
  - correctness 통과
  - implicit 대비 구조는 명확해졌지만 점수는 기대만큼 오르지 않음

### 3. Segregated Free Lists

- 구조:
  - size class별 free list 분리
  - `get_list_index(size)`로 class 선택
- 주요 변경:
  - `seg_free_lists[LISTLIMIT]`
  - class별 `insert/remove/find_fit`
- 결과:
  - correctness 통과
  - 점수 약 `84~85`
- 해석:
  - free list 분리 자체는 효과가 있으나, 이후 점수 정체 발생

### 4. Size Class 세분화

- 시도:
  - size class 수를 증가
  - 작은 구간을 더 촘촘하게 분리
- 결과:
  - 점수 변화 거의 없음
- 해석:
  - 현재 병목은 class 개수 부족이 아님

### 5. `SEARCHLIMIT` 조정

- 시도:
  - bounded best fit에서 후보 수 증가
  - 예: `4 -> 10 -> 20`
- 결과:
  - 점수 변화 거의 없음
- 해석:
  - 탐색 깊이 부족이 병목이 아님

### 6. `SPLITLIMIT` 조정

- 시도:
  - split 임계값을 `32`, `40`, `48`, `64` 등으로 변경
- 결과:
  - 점수 변화 없음
- 해석:
  - split threshold 역시 핵심 병목이 아님

### 7. `realloc` 최적화

- 시도:
  - shrink 시 split
  - 다음 free block과 병합하여 제자리 확장
  - heap 끝(epilogue 앞)에서 `mem_sbrk()`로 제자리 확장
- 결과:
  - correctness 유지
  - 점수 변화 거의 없음
- 해석:
  - `realloc` trace 개선 효과는 제한적
  - 전체 util 병목의 전부는 아님

### 8. 정렬 삽입 + bounded best fit

- 시도:
  - free list 삽입 정책을 단순 LIFO에서 정렬 삽입으로 변경
  - 같은 class 내부에서 더 적합한 블록을 앞쪽에 유지
- 현재 상태:
  - 같은 class 내부 크기 오름차순 삽입
  - `find_fit()`은 bounded best fit
- 결과:
  - correctness 통과
  - 점수는 여전히 `85`
- 해석:
  - 단순 삽입 순서만으로는 병목 해소 불가

### 9. `prev_alloc` 도입

- 시도:
  - allocated block의 footer 제거
  - header에 `prev_alloc` 비트 추가
- 목적:
  - 메타데이터 오버헤드 감소
  - allocated block당 공간 절약
- 결과:
  - correctness 통과
  - 점수는 여전히 `85`
- 해석:
  - 내부 단편화는 다소 개선될 여지가 있지만, 현재 낮은 util trace의 주원인은 아님

## Trace 분석 결과

### 점수를 깎는 주요 trace

- `binary-bal.rep`
- `binary2-bal.rep`
- `realloc-bal.rep`
- `realloc2-bal.rep`

### `binary-bal.rep` 패턴

- 반복 할당:
  - `64, 448, 64, 448, ...`
- 이후:
  - `448` 쪽만 free
  - 그 다음 `512` 반복 할당

문제:

- free된 `448` hole은 개별적으로 `512` 요청을 수용하지 못함
- 사이의 `64` 블록이 살아 있어서 병합도 불가
- 결과적으로 free 총량은 충분해도 연속 공간이 부족하여 heap growth 발생

### `binary2-bal.rep` 패턴

- 반복 할당:
  - `16, 112, 16, 112, ...`
- 이후:
  - `112` 쪽만 free
  - 그 다음 `128` 반복 할당

문제:

- free된 `112` hole은 개별적으로 `128` 요청을 수용하지 못함
- 사이의 `16` 블록 때문에 병합 불가
- 역시 외부 단편화로 인해 heap growth 발생

### 해석

현재 `85`점 정체의 핵심은 다음과 같다.

- size class 개수 부족 아님
- bounded best fit 후보 수 부족 아님
- split 임계값 문제 아님
- 삽입 순서 문제 아님

핵심 병목은 `binary/binary2`가 의도적으로 만드는 구조적 외부 단편화다.

## 현재 구현 상태

현재 `segregated-prev-alloc` 브랜치의 핵심 방향은 아래와 같다.

- segregated free lists
- 같은 size class 내부 정렬 삽입
- bounded best fit 성향 탐색
- `realloc` 제자리 확장
- `prev_alloc` 기반 footerless allocated block

## 현재 결론

- 정책 파라미터 튜닝만으로는 `85`점 이상 상승이 잘 일어나지 않는다.
- 현재 병목은 `binary/binary2` trace가 유도하는 외부 단편화다.
- 따라서 다음 단계는 단순 튜닝보다:
  - trace 단위 계측 유지
  - 배치 전략 또는 공급 전략을 더 구조적으로 바꾸는 실험
  - 필요하면 trace 과적합을 피하는 범위 내에서 block placement를 더 분리하는 실험
  중 하나가 되어야 한다.
