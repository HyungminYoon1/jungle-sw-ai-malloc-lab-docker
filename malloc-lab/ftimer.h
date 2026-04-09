/*
 * 함수 실행 시간 측정기
 */
typedef void (*ftimer_test_funct)(void *);

/* Unix 인터벌 타이머로 f(argp)의 실행 시간을 추정한다.
   n회 실행 평균을 반환한다 */
double ftimer_itimer(ftimer_test_funct f, void *argp, int n);


/* gettimeofday로 f(argp)의 실행 시간을 추정한다.
   n회 실행 평균을 반환한다 */
double ftimer_gettod(ftimer_test_funct f, void *argp, int n);

