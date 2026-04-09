#ifndef __CONFIG_H_
#define __CONFIG_H_

/*
 * config.h - malloc lab 설정 파일
 *
 * Copyright (c) 2002, R. Bryant and D. O'Hallaron, 모든 권리 보유.
 * 허가 없이 사용, 수정 또는 복제할 수 없습니다.
 */

/*
 * 드라이버가 기본 trace 파일을 찾을 때 사용할 기본 경로입니다.
 * 실행 시 -t 플래그로 이 값을 덮어쓸 수 있습니다.
 */
#define TRACEDIR "./traces/"

/*
 * 드라이버가 테스트에 사용할 TRACEDIR 내 기본 trace 파일 목록입니다.
 * 테스트 세트에서 trace를 추가하거나 제거하려면 이 값을 수정하면 됩니다.
 * 예를 들어 realloc 구현을 요구하지 않으려면 마지막 두 trace를
 * 삭제하면 됩니다.
 */
#define DEFAULT_TRACEFILES \
  "amptjp-bal.rep",\
  "cccp-bal.rep",\
  "cp-decl-bal.rep",\
  "expr-bal.rep",\
  "coalescing-bal.rep",\
  "random-bal.rep",\
  "random2-bal.rep",\
  "binary-bal.rep",\
  "binary2-bal.rep",\
  "realloc-bal.rep",\
  "realloc2-bal.rep"

/*
 * 이 상수는 기준 시스템에서 우리 trace를 사용해 측정한 libc malloc의
 * 예상 성능을 나타냅니다. 보통 학생들이 사용하는 것과 유사한 종류의
 * 시스템을 기준으로 합니다. 목적은 처리량이 성능 지수에 기여하는 값을
 * 상한 처리하는 데 있습니다. 학생 구현이 AVG_LIBC_THRUPUT을 넘어서면
 * 그 이후로는 점수상 추가 이득이 없습니다. 이는 매우 빠르지만
 * 지나치게 단순한 malloc 패키지를 만드는 것을 억제합니다.
 */
#define AVG_LIBC_THRUPUT      600E3  /* 초당 600 Kops */

 /*
  * 이 상수는 공간 활용도(UTIL_WEIGHT)와 처리량(1 - UTIL_WEIGHT)이
  * 성능 지수에 각각 얼마나 기여할지 결정합니다.
  */
#define UTIL_WEIGHT .60

/*
 * 바이트 단위 정렬 요구사항(4 또는 8)
 */
#define ALIGNMENT 8  

/*
 * 최대 힙 크기(바이트)
 */
#define MAX_HEAP (20*(1<<20))  /* 20 MB */

/*****************************************************************************
 * 타이밍 방법을 선택하려면 아래 USE_xxx 상수 중 정확히 하나만 "1"로 설정
 *****************************************************************************/
#define USE_FCYC   0   /* K-best 기법을 사용하는 사이클 카운터(x86, Alpha 전용) */
#define USE_ITIMER 0   /* 인터벌 타이머(모든 Unix 계열) */
#define USE_GETTOD 1   /* gettimeofday(모든 Unix 계열) */

#endif /* __CONFIG_H_ */
