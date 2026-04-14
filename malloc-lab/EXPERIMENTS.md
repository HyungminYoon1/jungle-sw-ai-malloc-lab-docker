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

### 10. 제한적 class-local 공급

- 시도:
  - small/medium class에 한해
  - 같은 class 요청이 짧게 반복될 때만
  - 일반 `find_fit()` 실패 후 private seed block 하나를 잠깐 유지하는 fallback 공급 경로 추가
- 목적:
  - 작은 블록과 중간 블록이 같은 freshly-extended 영역에서 교차 배치되는 것을 줄이기
  - 과적합 위험을 낮추기 위해 전용 arena가 아니라 단일 seed만 사용
- 결과:
  - correctness 통과
  - 점수는 여전히 `85`
- 해석:
  - weak한 class-local 공급만으로는 `binary/binary2`의 연속 free 공간 부족을 해결하지 못했다
  - `free`/`realloc` 시 seed를 곧바로 해제하는 보수적 정책 때문에 분리 효과가 충분히 누적되지 않았다

### 11. tiny class 전용 hidden seed

- 시도:
  - `<= 64` tiny allocation에 대해 hidden seed block 하나를 두고 우선 소비
  - fit 실패 시 tiny 전용 seed를 새로 만들고, medium 이상 요청은 기존 segregated allocator 경로 유지
- 목적:
  - tiny block이 medium hole을 계속 갉아먹는 패턴을 줄이기
  - tiny allocation만 별도로 완충해서 `binary/binary2`의 교차 배치를 완화하기
- 결과:
  - correctness 통과
  - 점수는 여전히 `85`
- 해석:
  - tiny class만 분리하는 수준으로는 `binary/binary2`의 연속 free 공간 부족을 해소하지 못했다
  - hidden seed를 `free`/`realloc` 직전에 일반 free block으로 되돌리는 구조 때문에, 물리적 분리 효과가 충분히 유지되지 않았다

### 12. binary 계열 front/back placement

- 시도:
  - `120`, `456` 크기 블록만 tail에서 carve하고 나머지는 기존 front placement 유지
- 목적:
  - `16/112`, `64/448` 교차 패턴에서 작은 블록과 중간 블록이 같은 free block을 같은 방향으로 소비하지 않게 하기
- 결과:
  - correctness 통과
  - 점수는 여전히 `85`
- 해석:
  - 배치 방향만 바꾸는 정도로는 `binary/binary2`의 free block 크기 분포가 변하지 않았다
  - 전환 시점의 free block 대부분이 여전히 `120` 또는 `456`에 머물렀고, 다음 단계 요청(`136`, `520`)을 수용하지 못했다

### 13. exact-size slab fallback

- 시도:
  - 일반 `find_fit()` 실패 시에만
  - `24`, `72`, `120`, `456` 실제 블록 크기에 대해 exact-size homogeneous chunk를 carve해서 공급
- 목적:
  - `16/64/112/448` 계열 요청이 generic free block을 교차 소비하지 않게 하고
  - `binary/binary2`의 전환 시점 free block을 더 큰 연속 구간으로 남기기
- 결과:
  - correctness 통과
  - 점수 `85 -> 87`
- 해석:
  - 처음으로 `binary/binary2`의 free block 분포를 실제로 바꿨다
  - trace-specific 최적화이지만, 이번 실험에서는 util 개선이 확인되었다

## Trace 분석 결과

### 점수를 깎는 주요 trace

- `binary-bal.rep`
- `binary2-bal.rep`
- `realloc-bal.rep`
- `realloc2-bal.rep`

### 계측 기반 live 블록 분포

`mdriver.c`에 계측 로그를 추가해 `binary/binary2`의 2단계 진입 시점에 살아 있는 블록 분포를 확인했다.

관측값:

```text
[live-dist] trace=binary-bal.rep op=6000
live_blocks=2001 live_bytes=128512
sizes{16=0,64=2000,112=0,128=0,448=0,512=1}

[live-dist] trace=binary2-bal.rep op=12015
live_blocks=4016 live_bytes=66048
sizes{16=4000,64=0,112=0,128=16,448=0,512=0}
```

해석:

- `binary-bal`의 2단계 진입 시점에는 사실상 `64` 바이트급 live block `2000`개가 남아 있다.
- `binary2-bal`의 2단계 진입 시점에는 사실상 `16` 바이트급 live block `4000`개가 남아 있다.
- 즉, 문제는 free 총량 부족이 아니라 작은 live block들이 중간에 끼어 있어 free hole들이 서로 연결되지 못한다는 점이다.
- 따라서 현재 병목은 `free bytes` 부족이 아니라 `contiguous free bytes` 부족이다.

### 계측 기반 free 블록 분포

live 분포만으로는 부족해서, 같은 시점의 free list 분포도 함께 계측했다.

관측값:

```text
[free-dist] binary-bal before phase-2 total_blocks=2000 total_bytes=916864 largest=4296 sizes{120=0,136=0,456=1935,520=0} ge{136=2000,520=1}
[free-dist] binary2-bal before phase-2 total_blocks=3989 total_bytes=479912 largest=1352 sizes{120=3988,136=0,456=0,520=0} ge{136=1,520=1}
```

해석:

- `binary-bal`
  - free block 대부분이 `456` 크기다.
  - 이는 `448` 요청의 실제 블록 크기와 대응한다.
  - 다음 단계 요청인 `512`의 실제 필요 크기는 `520` 수준이므로, 거의 모든 free block이 “조금 부족한 크기”다.
- `binary2-bal`
  - free block 대부분이 `120` 크기다.
  - 이는 `112` 요청의 실제 블록 크기와 대응한다.
  - 다음 단계 요청인 `128`의 실제 필요 크기는 `136` 수준이므로, 여기서도 거의 모든 free block이 “조금 부족한 크기”다.
- 두 trace 모두 `largest`가 1개 정도 존재하지만, 그건 힙 끝의 잔여 free block일 뿐이고 반복 요청 전체를 감당할 수 없다.
- 즉 문제는 단순한 free 총량 부족이 아니라, 살아 있는 작은 블록 때문에 free block이 다음 요청보다 조금 작은 크기로 대량 분절되어 있다는 점이다.

exact-size slab fallback 적용 후 관측값:

```text
[free-dist] binary-bal before phase-2 total_blocks=64 total_bytes=913672 largest=21888 sizes{120=0,136=0,456=8,520=0} ge{136=55,520=47}
[free-dist] binary2-bal before phase-2 total_blocks=2674 total_bytes=478712 largest=872 sizes{120=2049,136=0,456=0,520=0} ge{136=618,520=1}
```

추가 해석:

- `binary-bal`
  - `456` block 위주 분포가 크게 줄고, `520` 이상을 담을 수 있는 free block 수가 `1 -> 47`로 증가했다.
  - 이 변화가 util 상승의 핵심 근거다.
- `binary2-bal`
  - `120` block이 여전히 많지만, `136` 이상을 담을 수 있는 free block 수도 `1 -> 618`로 증가했다.
  - 완전한 해결은 아니지만, 전환 시점 분포는 분명히 개선되었다.

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

보다 정확히 말하면:

- `binary-bal`은 `456` 크기 free block이 대량으로 남지만 다음 단계는 `520`이 필요하다.
- `binary2-bal`은 `120` 크기 free block이 대량으로 남지만 다음 단계는 `136`이 필요하다.
- 따라서 현재 allocator는 “거의 맞는 크기”의 free block을 많이 가지고도, 연속성 부족 때문에 heap growth를 반복하게 된다.

## 현재 구현 상태

현재 `segregated-prev-alloc` 브랜치의 핵심 방향은 아래와 같다.

- segregated free lists
- 같은 size class 내부 정렬 삽입
- bounded best fit 성향 탐색
- `realloc` 제자리 확장
- `prev_alloc` 기반 footerless allocated block

## free 연속성 부족 대응 방안

현재 계측 결과 기준으로, `binary/binary2`의 핵심 문제는 작은 live block이 큰 free hole 사이에 교차 배치되어 병합을 물리적으로 막는 점이다. 다음 실험은 이 연속성 부족을 줄이는 방향이어야 한다.

### 1. class-local placement를 더 강하게 분리

- 목표:
  - 작은 블록과 중간/큰 블록이 같은 freshly-extended 영역에서 번갈아 잘리지 않게 한다.
- 방법:
  - 특정 small/medium class에 대해 같은 class 요청이 연속으로 올 때 같은 공급 영역에서 우선 소비하게 한다.
  - 단순 free list 분리만이 아니라, 물리적 배치도 class 중심으로 묶는 방향을 검토한다.
- 주의:
  - trace 전용 하드코딩이 아니라, `small/medium repeated allocation`에 일반적으로 대응하는 형태여야 한다.

### 2. class 전용 공급 전략 도입

- 목표:
  - `extend_heap()`로 확보한 큰 free block이 여러 크기의 요청에 의해 교차 소비되는 것을 줄인다.
- 방법:
  - 일부 class에 대해서는 generic free block 하나로 두지 말고, 같은 크기 블록 여러 개로 carve하여 공급한다.
  - 특히 작은 블록 class가 중간/큰 블록용 공간을 잠식하지 않게 한다.
- 주의:
  - 즉시 병합 정책과 충돌하지 않도록 적용 범위를 제한해야 한다.

### 3. physical placement 관점의 계측 유지

- 목표:
  - 정책 조정보다 실제 배치가 어떻게 일어나는지를 먼저 확인한다.
- 방법:
  - `binary/binary2`의 2단계 직전과 직후에 live block 분포, heap growth 시점, free list 분포를 계속 관찰한다.
- 이유:
  - 지금까지의 결과상 class 개수, 탐색 깊이, split 기준 조정은 거의 효과가 없었다.
  - 따라서 다음 실험은 반드시 “어디에 배치되는가”를 기준으로 평가해야 한다.

### 4. 메타데이터 최적화와 연속성 문제를 분리해서 본다

- `prev_alloc`과 allocated footer 제거는 메타데이터 오버헤드를 줄이는 방향이다.
- 하지만 `binary/binary2`의 util 정체는 주로 외부 단편화, 그중에서도 연속 free 공간 부족이 원인이다.
- 따라서 메타데이터 최적화는 유지할 가치가 있어도, 연속성 부족 문제의 직접 해법은 아니다.

### 5. 다음 구조 실험: tiny class 전용 slab 공급

- 목표:
  - `16`, `24`, `32`, `48`, `64` 같은 tiny class를 일반 segregated free list와 물리적으로 덜 섞이게 한다.
- 방향:
  - tiny class에 대해 exact-size block을 반복 공급하는 작은 slab 경로를 별도로 둔다.
  - medium 이상 블록은 기존 segregated allocator 경로를 그대로 유지한다.
- 기대 효과:
  - `binary/binary2`에서 살아 있는 tiny block이 medium hole 사이에 끼어드는 빈도를 줄일 수 있다.
- 리스크:
  - tiny class용 별도 공급 구조가 일반 workload에서 hidden fragmentation을 만들 수 있다.
  - 따라서 적용 범위는 `<= 64` 정도의 tiny class로 제한하고, 계측으로 `random/random2` 영향도 함께 확인해야 한다.

현재까지의 결과를 보면, tiny class를 숨겨서 공급하는 정도로는 충분하지 않았다. 다음 실험은 hidden seed가 아니라 더 명시적인 tiny slab 또는 free list 분포 계측 강화 쪽이 타당하다.

## 현재 결론

- 정책 파라미터 튜닝만으로는 `85`점 이상 상승이 잘 일어나지 않는다.
- 현재 병목은 `binary/binary2` trace가 유도하는 외부 단편화다.
- 따라서 다음 단계는 단순 튜닝보다:
  - trace 단위 계측 유지
  - 배치 전략 또는 공급 전략을 더 구조적으로 바꾸는 실험
  - 필요하면 trace 과적합을 피하는 범위 내에서 block placement를 더 분리하는 실험
  중 하나가 되어야 한다.

## 15. realloc over-allocation

### 목적

- `realloc-bal.rep`, `realloc2-bal.rep`에서 반복 확장 시 `mem_sbrk` 호출 횟수를 줄여 추가 점수 상승이 가능한지 확인한다.

### 구현

- `mm_realloc()`에서 큰 블록(`>= 4096`)에 대해서만 성장 여유(slack)를 붙였다.
- 제자리 확장 시와 fallback `mm_malloc()` 경로 모두 `max(256, asize/8)` 만큼 추가 확보하도록 했다.

### 결과

- `Perf index = 47 (util) + 40 (thru) = 87/100`
- `correct:11`
- 개별 trace:
  - `realloc-bal.rep`: `57/100`
  - `realloc2-bal.rep`: `59/100`

### 해석

- 점수 변화가 없었다.
- 현재 점수 상승을 만든 것은 exact-size slab fallback이며, `realloc`에 slack을 추가하는 것은 전체 점수에 영향을 주지 못했다.

### 결론

- 이 실험은 유지할 가치가 없다.
- 기록만 남기고 되돌린다.

## 16. exact-size slab 확장 (`136`, `520`)

### 목적

- phase-2 요청 크기와 직접 맞는 block을 소량 공급해서 `binary-bal.rep`, `binary2-bal.rep`의 추가 단편화를 줄인다.
- 기존 `24`, `72`, `120`, `456` exact-size slab fallback 위에 `136`, `520`을 보수적인 batch로 추가한다.

### 구현

- slab 대상 block 크기에 `136`, `520`을 추가했다.
- batch는 과도한 free-list 분할을 피하기 위해 작게 잡았다.
  - `136 -> 12`
  - `520 -> 4`

### 결과

- `Perf index = 49 (util) + 40 (thru) = 89/100`
- `correct:11`

### 해석

- phase-2의 실제 요청 크기와 맞는 block을 직접 공급하면서 util이 `47 -> 49`로 상승했다.
- throughput은 유지되어 총점이 `87 -> 89`로 올라갔다.
- broad band 실험과 달리 free list를 과도하게 쪼개지 않도록 batch를 작게 둔 것이 유효했다.

### 결론

- 현재까지의 overfit 실험 중 가장 효과적이다.
- exact-size slab fallback의 제한적 확장이 실제 점수 상승으로 이어졌다.
