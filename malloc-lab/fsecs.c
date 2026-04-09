/****************************
 * 상위 수준 타이밍 래퍼
 ****************************/
#include <stdio.h>
#include "fsecs.h"
#include "fcyc.h"
#include "clock.h"
#include "ftimer.h"
#include "config.h"

static double Mhz;  /* 추정된 CPU 클록 주파수 */

extern int verbose; /* mdriver.c의 -v 옵션 */

/*
 * init_fsecs - 타이밍 패키지 초기화
 */
void init_fsecs(void)
{
    Mhz = 0; /* gcc -Wall 경고 방지 */

#if USE_FCYC
    if (verbose)
	printf("Measuring performance with a cycle counter.\n");

    /* fcyc 패키지의 주요 매개변수 설정 */
    set_fcyc_maxsamples(20); 
    set_fcyc_clear_cache(1);
    set_fcyc_compensate(1);
    set_fcyc_epsilon(0.01);
    set_fcyc_k(3);
    Mhz = mhz(verbose > 0);
#elif USE_ITIMER
    if (verbose)
	printf("Measuring performance with the interval timer.\n");
#elif USE_GETTOD
    if (verbose)
	printf("Measuring performance with gettimeofday().\n");
#endif
}

/*
 * fsecs - 함수 f의 실행 시간을 초 단위로 반환
 */
double fsecs(fsecs_test_funct f, void *argp) 
{
#if USE_FCYC
    double cycles = fcyc(f, argp);
    return cycles/(Mhz*1e6);
#elif USE_ITIMER
    return ftimer_itimer(f, argp, 10);
#elif USE_GETTOD
    return ftimer_gettod(f, argp, 10);
#endif 
}


