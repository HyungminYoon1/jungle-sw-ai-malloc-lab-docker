/* 사이클 카운터를 사용하기 위한 루틴 */

/* 카운터 시작 */
void start_counter();

/* 카운터 시작 이후의 사이클 수 반환 */
double get_counter();

/* 카운터 오버헤드 측정 */
double ovhd();

/* 기본 대기 시간을 사용해 프로세서 클록 속도 측정 */
double mhz(int verbose);

/* 정확도를 더 세밀하게 제어하며 프로세서 클록 속도 측정 */
double mhz_full(int verbose, int sleeptime);

/** 타이머 인터럽트 오버헤드를 보정하는 특수 카운터 */

void start_comp_counter();

double get_comp_counter();
