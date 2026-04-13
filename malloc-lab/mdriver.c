/*
 * mdriver.c - CS:APP Malloc Lab 드라이버
 *
 * 여러 trace 파일을 사용해 mm.c의 malloc/free/realloc 구현을 테스트한다.
 *
 * Copyright (c) 2002, R. Bryant and D. O'Hallaron, 모든 권리 보유.
 * 허가 없이 사용, 수정 또는 복제할 수 없습니다.
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <assert.h>
#include <float.h>
#include <time.h>

extern char *optarg; // optarg 선언 추가

#include "mm.h"
#include "memlib.h"
#include "fsecs.h"
#include "config.h"

/**********************
 * 상수와 매크로
 **********************/

/* 기타 */
#define MAXLINE 1024	   /* 최대 문자열 길이 */
#define HDRLINES 4		   /* trace 파일의 헤더 줄 수 */
#define LINENUM(i) (i + 5) /* trace 요청 번호를 줄 번호로 변환(시작은 1) */

/* p가 ALIGNMENT 바이트 정렬이면 참 반환 */
#define IS_ALIGNED(p) ((((unsigned int)(p)) % ALIGNMENT) == 0)

/******************************
 * 주요 복합 자료형
 *****************************/

/* 각 블록 payload의 범위를 기록 */
typedef struct range_t
{
	char *lo;			  /* payload의 낮은 주소 */
	char *hi;			  /* payload의 높은 주소 */
	struct range_t *next; /* 다음 리스트 원소 */
} range_t;

/* 하나의 trace 연산(할당기 요청)을 표현 */
typedef struct
{
	enum
	{
		ALLOC,
		FREE,
		REALLOC
	} type;	   /* 요청 유형 */
	int index; /* 나중에 free()가 사용할 인덱스 */
	int size;  /* alloc/realloc 요청의 바이트 크기 */
} traceop_t;

/* 하나의 trace 파일 정보를 저장 */
typedef struct
{
	int sugg_heapsize;	 /* 권장 힙 크기(사용하지 않음) */
	int num_ids;		 /* alloc/realloc ID 개수 */
	int num_ops;		 /* 서로 다른 요청 개수 */
	int weight;			 /* 이 trace의 가중치(사용하지 않음) */
	char *filename;      /* trace 파일 이름 */
	traceop_t *ops;		 /* 요청 배열 */
	char **blocks;		 /* malloc/realloc이 반환한 포인터 배열 */
	size_t *block_sizes; /* 각 포인터에 대응하는 payload 크기 배열 */
} trace_t;

/*
 * fcyc로 시간을 측정하는 xxx_speed 함수에 전달할 인자를 보관한다.
 * fcyc는 입력으로 포인터 하나만 받을 수 있으므로 이 구조체가 필요하다.
 */
typedef struct
{
	trace_t *trace;
	range_t *ranges;
} speed_t;

/* 특정 trace에서 특정 malloc 함수의 주요 통계를 요약 */
typedef struct
{
	/* libc malloc과 학생 malloc 패키지(mm.c) 모두에 대해 정의됨 */
	double ops;	 /* trace 내 연산 수(malloc/free/realloc) */
	int valid;	 /* 할당기가 trace를 올바르게 처리했는가? */
	double secs; /* trace 실행에 걸린 시간(초) */

	/* 학생 malloc 패키지에 대해서만 정의됨 */
	double util; /* 이 trace의 공간 활용도(libc에서는 항상 0) */

	/* 참고: secs와 util은 valid가 참일 때만 의미가 있다 */
} stats_t;

/********************
 * 전역 변수
 *******************/
int verbose = 0;	   /* 자세한 출력을 위한 전역 플래그 */
static int errors = 0; /* 학생 malloc 실행 중 발견한 오류 수 */
char msg[MAXLINE];	   /* 오류 메시지를 조합할 때 사용 */

/* 기본 trace 파일이 있는 디렉터리 */
static char tracedir[MAXLINE] = TRACEDIR;

/* 기본 trace 파일 이름 목록 */
static char *default_tracefiles[] = {
	DEFAULT_TRACEFILES, NULL};

/*********************
 * 함수 프로토타입
 *********************/

/* range 리스트를 다루는 함수들 */
static int add_range(range_t **ranges, char *lo, int size,
					 int tracenum, int opnum);
static void remove_range(range_t **ranges, char *lo);
static void clear_ranges(range_t **ranges);

/* trace를 읽고, 저장 공간을 할당/해제하는 함수들 */
static trace_t *read_trace(char *tracedir, char *filename);
static void free_trace(trace_t *trace);

/* libc malloc의 정합성과 속도를 평가하는 루틴 */
static int eval_libc_valid(trace_t *trace, int tracenum);
static void eval_libc_speed(void *ptr);

/* mm.c에 있는 학생 malloc 패키지의 정합성, 공간 활용도, 속도를 평가하는 루틴 */
static int eval_mm_valid(trace_t *trace, int tracenum, range_t **ranges);
static double eval_mm_util(trace_t *trace, int tracenum, range_t **ranges);
static void eval_mm_speed(void *ptr);

/* 여러 보조 루틴 */
static void printresults(int n, stats_t *stats);
static void usage(void);
static void unix_error(char *msg);
static void malloc_error(int tracenum, int opnum, char *msg);
static void app_error(char *msg);
static int should_log_growth(trace_t *trace);
static const char *op_name(int type);
static void dump_live_block_distribution(trace_t *trace, int opnum);

/**************
 * 메인 루틴
 **************/
int main(int argc, char **argv)
{
	int i;
	int c;
	char **tracefiles = NULL;	/* NULL로 끝나는 trace 파일 이름 배열 */
	int num_tracefiles = 0;		/* 해당 배열의 trace 개수 */
	trace_t *trace = NULL;		/* 단일 trace 파일을 메모리에 저장 */
	range_t *ranges = NULL;		/* 하나의 trace에 대한 블록 범위 추적 */
	stats_t *libc_stats = NULL; /* 각 trace에 대한 libc 통계 */
	stats_t *mm_stats = NULL;	/* 각 trace에 대한 mm(학생 구현) 통계 */
	speed_t speed_params;		/* xx_speed 루틴에 전달할 입력 인자 */

	int team_check = 1; /* 설정되면 팀 구조를 검사(-a로 해제) */
	int run_libc = 0;	/* 설정되면 libc malloc도 실행(-l로 설정) */
	int autograder = 0; /* 설정되면 autograder용 요약 정보 출력(-g) */

	/* 성능 지수 계산에 사용할 임시 변수 */
	double secs, ops, util, avg_mm_util, avg_mm_throughput, p1, p2, perfindex;
	int numcorrect;

	/*
	 * 명령행 인자를 읽고 해석
	 */
	while ((c = getopt(argc, argv, "f:t:hvVgal")) != EOF)
	{
		printf("getopt returned: %c\n", c); // 디버깅용 출력 추가

		switch (c)
		{
		case 'g': /* autograder용 요약 정보 생성 */
			autograder = 1;
			break;
		case 'f': /* 특정 trace 파일 하나만 사용(현재 디렉터리 기준 상대 경로) */
			num_tracefiles = 1;
			if ((tracefiles = realloc(tracefiles, 2 * sizeof(char *))) == NULL)
				unix_error("ERROR: realloc failed in main");
			strcpy(tracedir, "./");
			tracefiles[0] = strdup(optarg);
			tracefiles[1] = NULL;
			break;
		case 't':					 /* trace 파일이 있는 디렉터리 */
			if (num_tracefiles == 1) /* 이미 -f가 있으면 무시 */
				break;
			strcpy(tracedir, optarg);
			if (tracedir[strlen(tracedir) - 1] != '/')
				strcat(tracedir, "/"); /* 경로는 항상 "/"로 끝나야 함 */
			break;
		case 'a': /* 팀 구조 검사 안 함 */
			team_check = 0;
			break;
		case 'l': /* libc malloc도 실행 */
			run_libc = 1;
			break;
		case 'v': /* trace별 성능 상세 출력 */
			verbose = 1;
			break;
		case 'V': /* -v보다 더 자세히 출력 */
			verbose = 2;
			break;
		case 'h': /* 도움말 출력 */
			usage();
			exit(0);
		default:
			usage();
			exit(1);
		}
	}

	/*
	 * 팀 정보 검사 및 출력
	 */
	if (team_check)
	{
		/* 학생은 팀 정보를 반드시 입력해야 한다 */
		if (!strcmp(team.teamname, ""))
		{
			printf("ERROR: Please provide the information about your team in mm.c.\n");
			exit(1);
		}
		else
			printf("Team Name:%s\n", team.teamname);
		if ((*team.name1 == '\0') || (*team.id1 == '\0'))
		{
			printf("ERROR.  You must fill in all team member 1 fields!\n");
			exit(1);
		}
		else
			printf("Member 1 :%s:%s\n", team.name1, team.id1);

		if (((*team.name2 != '\0') && (*team.id2 == '\0')) ||
			((*team.name2 == '\0') && (*team.id2 != '\0')))
		{
			printf("ERROR.  You must fill in all or none of the team member 2 ID fields!\n");
			exit(1);
		}
		else if (*team.name2 != '\0')
			printf("Member 2 :%s:%s\n", team.name2, team.id2);
	}

	/*
	 * -f 인자가 없으면 default_traces[]에 정의된
	 * 전체 trace 파일 집합을 사용한다.
	 */
	if (tracefiles == NULL)
	{
		tracefiles = default_tracefiles;
		num_tracefiles = sizeof(default_tracefiles) / sizeof(char *) - 1;
		printf("Using default tracefiles in %s\n", tracedir);
	}

	/* 타이밍 패키지 초기화 */
	init_fsecs();

	/*
	 * 필요하면 libc malloc 패키지를 실행하고 평가
	 */
	if (run_libc)
	{
		if (verbose > 1)
			printf("\nTesting libc malloc\n");

		/* trace 파일마다 stats_t 하나씩 갖는 libc 통계 배열 할당 */
		libc_stats = (stats_t *)calloc(num_tracefiles, sizeof(stats_t));
		if (libc_stats == NULL)
			unix_error("libc_stats calloc in main failed");

		/* K-best 기법으로 libc malloc 패키지 평가 */
		for (i = 0; i < num_tracefiles; i++)
		{
			trace = read_trace(tracedir, tracefiles[i]);
			libc_stats[i].ops = trace->num_ops;
			if (verbose > 1)
				printf("Checking libc malloc for correctness, ");
			libc_stats[i].valid = eval_libc_valid(trace, i);
			if (libc_stats[i].valid)
			{
				speed_params.trace = trace;
				if (verbose > 1)
					printf("and performance.\n");
				libc_stats[i].secs = fsecs(eval_libc_speed, &speed_params);
			}
			free_trace(trace);
		}

		/* libc 결과를 간단한 표 형태로 출력 */
		if (verbose)
		{
			printf("\nResults for libc malloc:\n");
			printresults(num_tracefiles, libc_stats);
		}
	}

	/*
	 * 학생의 mm 패키지는 항상 실행하고 평가
	 */
	if (verbose > 1)
		printf("\nTesting mm malloc\n");

	/* trace 파일마다 stats_t 하나씩 갖는 mm 통계 배열 할당 */
	mm_stats = (stats_t *)calloc(num_tracefiles, sizeof(stats_t));
	if (mm_stats == NULL)
		unix_error("mm_stats calloc in main failed");

	/* memlib.c의 시뮬레이션 메모리 시스템 초기화 */
	mem_init();

	/* K-best 기법으로 학생의 mm malloc 패키지 평가 */
	for (i = 0; i < num_tracefiles; i++)
	{
		trace = read_trace(tracedir, tracefiles[i]);
		mm_stats[i].ops = trace->num_ops;
		if (verbose > 1)
			printf("Checking mm_malloc for correctness, ");
		mm_stats[i].valid = eval_mm_valid(trace, i, &ranges);
		if (mm_stats[i].valid)
		{
			if (verbose > 1)
				printf("efficiency, ");
			mm_stats[i].util = eval_mm_util(trace, i, &ranges);
			speed_params.trace = trace;
			speed_params.ranges = ranges;
			if (verbose > 1)
				printf("and performance.\n");
			mm_stats[i].secs = fsecs(eval_mm_speed, &speed_params);
		}
		free_trace(trace);
	}

	/* mm 결과를 간단한 표 형태로 출력 */
	if (verbose)
	{
		printf("\nResults for mm malloc:\n");
		printresults(num_tracefiles, mm_stats);
		printf("\n");
	}

	/*
	 * 학생 mm 패키지의 전체 통계를 누적
	 */
	secs = 0;
	ops = 0;
	util = 0;
	numcorrect = 0;
	for (i = 0; i < num_tracefiles; i++)
	{
		secs += mm_stats[i].secs;
		ops += mm_stats[i].ops;
		util += mm_stats[i].util;
		if (mm_stats[i].valid)
			numcorrect++;
	}
	avg_mm_util = util / num_tracefiles;

	/*
	 * 성능 지수를 계산하고 출력
	 */
	if (errors == 0)
	{
		avg_mm_throughput = ops / secs;

		p1 = UTIL_WEIGHT * avg_mm_util;
		if (avg_mm_throughput > AVG_LIBC_THRUPUT)
		{
			p2 = (double)(1.0 - UTIL_WEIGHT);
		}
		else
		{
			p2 = ((double)(1.0 - UTIL_WEIGHT)) *
				 (avg_mm_throughput / AVG_LIBC_THRUPUT);
		}

		perfindex = (p1 + p2) * 100.0;
		printf("Perf index = %.0f (util) + %.0f (thru) = %.0f/100\n",
			   p1 * 100,
			   p2 * 100,
			   perfindex);
	}
	else
	{ /* 오류가 있었음 */
		perfindex = 0.0;
		printf("Terminated with %d errors\n", errors);
	}

	if (autograder)
	{
		printf("correct:%d\n", numcorrect);
		printf("perfidx:%.0f\n", perfindex);
	}

	exit(0);
}

/*****************************************************************
 * 아래 루틴들은 할당된 각 블록 payload의 범위를 추적하는
 * range 리스트를 다룬다. 이 리스트를 사용해 겹치는 할당 블록을
 * 탐지한다.
 ****************************************************************/

/*
 * add_range - trace tracenum의 opnum 요청에 따라 학생의 mm_malloc을 호출해
 *             주소 lo에 size 바이트 블록을 막 할당한 상태다.
 *             블록의 정합성을 확인한 뒤 이 블록에 대한 range 구조체를 만들고
 *             range 리스트에 추가한다.
 */
static int add_range(range_t **ranges, char *lo, int size,
					 int tracenum, int opnum)
{
	char *hi = lo + size - 1;
	range_t *p;
	char msg[MAXLINE];

	assert(size > 0);

	/* Payload 주소는 ALIGNMENT 바이트 정렬이어야 한다 */
	if (!IS_ALIGNED(lo))
	{
		sprintf(msg, "Payload address (%p) not aligned to %d bytes",
				lo, ALIGNMENT);
		malloc_error(tracenum, opnum, msg);
		return 0;
	}

	/* Payload는 힙 범위 안에 있어야 한다 */
	if ((lo < (char *)mem_heap_lo()) || (lo > (char *)mem_heap_hi()) ||
		(hi < (char *)mem_heap_lo()) || (hi > (char *)mem_heap_hi()))
	{
		sprintf(msg, "Payload (%p:%p) lies outside heap (%p:%p)",
				lo, hi, mem_heap_lo(), mem_heap_hi());
		malloc_error(tracenum, opnum, msg);
		return 0;
	}

	/* Payload는 다른 어떤 payload와도 겹치면 안 된다 */
	for (p = *ranges; p != NULL; p = p->next)
	{
		if ((lo >= p->lo && lo <= p->hi) ||
			(hi >= p->lo && hi <= p->hi))
		{
			sprintf(msg, "Payload (%p:%p) overlaps another payload (%p:%p)\n",
					lo, hi, p->lo, p->hi);
			malloc_error(tracenum, opnum, msg);
			return 0;
		}
	}

	/*
	 * 모두 문제없다면 range 구조체를 생성해
	 * 이 블록의 범위를 range 리스트에 기록한다.
	 */
	if ((p = (range_t *)malloc(sizeof(range_t))) == NULL)
		unix_error("malloc error in add_range");
	p->next = *ranges;
	p->lo = lo;
	p->hi = hi;
	*ranges = p;
	return 1;
}

/*
 * remove_range - payload 시작 주소가 lo인 블록의 range 기록을 제거
 */
static void remove_range(range_t **ranges, char *lo)
{
	range_t *p;
	range_t **prevpp = ranges;
	int size;

	for (p = *ranges; p != NULL; p = p->next)
	{
		if (p->lo == lo)
		{
			*prevpp = p->next;
			size = p->hi - p->lo + 1;
			free(p);
			break;
		}
		prevpp = &(p->next);
	}
}

/*
 * clear_ranges - 하나의 trace에 대한 모든 range 기록 해제
 */
static void clear_ranges(range_t **ranges)
{
	range_t *p;
	range_t *pnext;

	for (p = *ranges; p != NULL; p = pnext)
	{
		pnext = p->next;
		free(p);
	}
	*ranges = NULL;
}

/**********************************************
 * 아래 루틴들은 trace 파일을 다룬다
 *********************************************/

/*
 * read_trace - trace 파일을 읽어 메모리에 저장
 */
static trace_t *read_trace(char *tracedir, char *filename)
{
	FILE *tracefile;
	trace_t *trace;
	char type[MAXLINE];
	char path[MAXLINE];
	unsigned index, size;
	unsigned max_index = 0;
	unsigned op_index;

	if (verbose > 1)
		printf("Reading tracefile: %s\n", filename);

	/* trace 레코드 할당 */
	if ((trace = (trace_t *)malloc(sizeof(trace_t))) == NULL)
		unix_error("malloc 1 failed in read_trance");
	if ((trace->filename = strdup(filename)) == NULL)
		unix_error("strdup failed in read_trace");

	/* trace 파일 헤더 읽기 */
	strcpy(path, tracedir);
	strcat(path, filename);
	if ((tracefile = fopen(path, "r")) == NULL)
	{
		sprintf(msg, "Could not open %s in read_trace", path);
		unix_error(msg);
	}
	fscanf(tracefile, "%d", &(trace->sugg_heapsize)); /* 사용하지 않음 */
	fscanf(tracefile, "%d", &(trace->num_ids));
	fscanf(tracefile, "%d", &(trace->num_ops));
	fscanf(tracefile, "%d", &(trace->weight)); /* 사용하지 않음 */

	/* trace의 각 요청 줄을 이 배열에 저장한다 */
	if ((trace->ops =
			 (traceop_t *)malloc(trace->num_ops * sizeof(traceop_t))) == NULL)
		unix_error("malloc 2 failed in read_trace");

	/* 할당된 블록의 포인터 배열을 여기에 보관한다 */
	if ((trace->blocks =
			 (char **)malloc(trace->num_ids * sizeof(char *))) == NULL)
		unix_error("malloc 3 failed in read_trace");

	/* 각 블록에 대응하는 바이트 크기도 함께 저장한다 */
	if ((trace->block_sizes =
			 (size_t *)malloc(trace->num_ids * sizeof(size_t))) == NULL)
		unix_error("malloc 4 failed in read_trace");

	/* trace 파일의 모든 요청 줄을 읽는다 */
	index = 0;
	op_index = 0;
	while (fscanf(tracefile, "%s", type) != EOF)
	{
		switch (type[0])
		{
		case 'a':
			fscanf(tracefile, "%u %u", &index, &size);
			trace->ops[op_index].type = ALLOC;
			trace->ops[op_index].index = index;
			trace->ops[op_index].size = size;
			max_index = (index > max_index) ? index : max_index;
			break;
		case 'r':
			fscanf(tracefile, "%u %u", &index, &size);
			trace->ops[op_index].type = REALLOC;
			trace->ops[op_index].index = index;
			trace->ops[op_index].size = size;
			max_index = (index > max_index) ? index : max_index;
			break;
		case 'f':
			fscanf(tracefile, "%ud", &index);
			trace->ops[op_index].type = FREE;
			trace->ops[op_index].index = index;
			break;
		default:
			printf("Bogus type character (%c) in tracefile %s\n",
				   type[0], path);
			exit(1);
		}
		op_index++;
	}
	fclose(tracefile);
	assert(max_index == trace->num_ids - 1);
	assert(trace->num_ops == op_index);

	return trace;
}

/*
 * free_trace - trace 레코드와, 그것이 가리키는 세 개의 배열을 해제한다.
 *              이들은 모두 read_trace()에서 할당되었다.
 */
void free_trace(trace_t *trace)
{
	free(trace->filename);
	free(trace->ops); /* 세 개의 배열을 해제 */
	free(trace->blocks);
	free(trace->block_sizes);
	free(trace); /* 그리고 trace 레코드 자체도 해제 */
}

static int should_log_growth(trace_t *trace)
{
	return !strcmp(trace->filename, "binary-bal.rep") ||
		   !strcmp(trace->filename, "binary2-bal.rep") ||
		   !strcmp(trace->filename, "realloc-bal.rep") ||
		   !strcmp(trace->filename, "realloc2-bal.rep");
}

static const char *op_name(int type)
{
	switch (type)
	{
	case ALLOC:
		return "alloc";
	case FREE:
		return "free";
	case REALLOC:
		return "realloc";
	default:
		return "unknown";
	}
}

static void dump_live_block_distribution(trace_t *trace, int opnum)
{
	int i;
	int live_count = 0;
	int count16 = 0, count64 = 0, count112 = 0, count128 = 0, count448 = 0, count512 = 0;
	size_t live_bytes = 0;

	for (i = 0; i < trace->num_ids; i++)
	{
		size_t sz = trace->block_sizes[i];
		if (trace->blocks[i] == NULL || sz == 0)
			continue;

		live_count++;
		live_bytes += sz;

		if (sz == 16)
			count16++;
		else if (sz == 64)
			count64++;
		else if (sz == 112)
			count112++;
		else if (sz == 128)
			count128++;
		else if (sz == 448)
			count448++;
		else if (sz == 512)
			count512++;
	}

	printf("[live-dist] trace=%s op=%d live_blocks=%d live_bytes=%zu sizes{16=%d,64=%d,112=%d,128=%d,448=%d,512=%d}\n",
		   trace->filename,
		   opnum,
		   live_count,
		   live_bytes,
		   count16,
		   count64,
		   count112,
		   count128,
		   count448,
		   count512);
}

/**********************************************************************
 * 아래 함수들은 libc와 mm malloc 패키지의 정합성, 공간 활용도,
 * 처리량을 평가한다.
 **********************************************************************/

/*
 * eval_mm_valid - mm malloc 패키지의 정합성 검사
 */
static int eval_mm_valid(trace_t *trace, int tracenum, range_t **ranges)
{
	int i, j;
	int index;
	int size;
	int oldsize;
	char *newp;
	char *oldp;
	char *p;

	/* 힙을 초기화하고 range 리스트 기록을 모두 해제 */
	mem_reset_brk();
	clear_ranges(ranges);

	/* mm 패키지의 init 함수 호출 */
	if (mm_init() < 0)
	{
		malloc_error(tracenum, 0, "mm_init failed.");
		return 0;
	}

	/* trace의 각 연산을 순서대로 해석 */
	for (i = 0; i < trace->num_ops; i++)
	{
		index = trace->ops[i].index;
		size = trace->ops[i].size;

		switch (trace->ops[i].type)
		{

		case ALLOC: /* mm_malloc */

			/* 학생의 malloc 호출 */
			if ((p = mm_malloc(size)) == NULL)
			{
				malloc_error(tracenum, i, "mm_malloc failed.");
				return 0;
			}

			/*
			 * 새 블록의 범위를 검사하고 문제가 없으면 range 리스트에 추가한다.
			 * 블록은 올바르게 정렬되어 있어야 하고,
			 * 현재 할당된 어떤 블록과도 겹치면 안 된다.
			 */
			if (add_range(ranges, p, size, tracenum, i) == 0)
				return 0;

			/* 추가: cgw
			 * 범위를 인덱스의 하위 1바이트 값으로 채운다.
			 * 나중에 realloc할 때 기존 데이터가 새 블록으로
			 * 복사되었는지 확인하는 데 사용한다.
			 */
			memset(p, index & 0xFF, size);

			/* 영역 기록 */
			trace->blocks[index] = p;
			trace->block_sizes[index] = size;
			break;

		case REALLOC: /* mm_realloc */

			/* 학생의 realloc 호출 */
			oldp = trace->blocks[index];
			if ((newp = mm_realloc(oldp, size)) == NULL)
			{
				malloc_error(tracenum, i, "mm_realloc failed.");
				return 0;
			}

			/* range 리스트에서 기존 영역 제거 */
			remove_range(ranges, oldp);

			/* 새 블록의 정합성을 검사하고 range 리스트에 추가 */
			if (add_range(ranges, newp, size, tracenum, i) == 0)
				return 0;

			/* 추가: cgw
			 * 새 블록이 이전 블록의 데이터를 유지하는지 확인한 뒤,
			 * 새 인덱스의 하위 1바이트 값으로 새 블록을 채운다.
			 */
			oldsize = trace->block_sizes[index];
			if (size < oldsize)
				oldsize = size;
			for (j = 0; j < oldsize; j++)
			{
				if (newp[j] != (index & 0xFF))
				{
					malloc_error(tracenum, i, "mm_realloc did not preserve the "
											  "data from old block");
					return 0;
				}
			}
			memset(newp, index & 0xFF, size);

			/* 영역 기록 */
			trace->blocks[index] = newp;
			trace->block_sizes[index] = size;
			break;

		case FREE: /* mm_free */

			/* 리스트에서 영역을 제거하고 학생의 free 함수 호출 */
			p = trace->blocks[index];
			remove_range(ranges, p);
			mm_free(p);
			break;

		default:
			app_error("Nonexistent request type in eval_mm_valid");
		}
	}

	/* 현재까지 확인한 바로는 유효한 malloc 패키지 */
	return 1;
}

/*
 * eval_mm_util - 학생 패키지의 공간 활용도 평가
 *   핵심 아이디어는 최적 할당기, 즉 빈틈도 내부 단편화도 없는 할당기의
 *   힙 최고 수위 "hwm"을 기억하는 것이다.
 *   활용도는 hwm/heapsize 비율이며, heapsize는 해당 trace에서 학생의
 *   malloc 패키지를 실행한 뒤의 힙 크기(바이트)다.
 *   mem_sbrk() 구현은 brk 포인터 감소를 허용하지 않으므로
 *   brk는 항상 힙의 최고 수위가 된다.
 *
 */
static double eval_mm_util(trace_t *trace, int tracenum, range_t **ranges)
{
	int i;
	int index;
	int size, newsize, oldsize;
	int max_total_size = 0;
	int total_size = 0;
	int heap_before, heap_after;
	char *p;
	char *newp, *oldp;

	/* 힙과 mm malloc 패키지 초기화 */
	mem_reset_brk();
	if (mm_init() < 0)
		app_error("mm_init failed in eval_mm_util");
	memset(trace->blocks, 0, trace->num_ids * sizeof(char *));
	memset(trace->block_sizes, 0, trace->num_ids * sizeof(size_t));

	for (i = 0; i < trace->num_ops; i++)
	{
		heap_before = mem_heapsize();
		switch (trace->ops[i].type)
		{

		case ALLOC: /* mm_alloc */
			index = trace->ops[i].index;
			size = trace->ops[i].size;

			if ((p = mm_malloc(size)) == NULL)
				app_error("mm_malloc failed in eval_mm_util");

			/* 영역과 크기 기록 */
			trace->blocks[index] = p;
			trace->block_sizes[index] = size;

			/* 현재 할당된 모든 블록의 총 크기 추적 */
			total_size += size;

			/* 통계 갱신 */
			max_total_size = (total_size > max_total_size) ? total_size : max_total_size;
			break;

		case REALLOC: /* mm_realloc */
			index = trace->ops[i].index;
			newsize = trace->ops[i].size;
			oldsize = trace->block_sizes[index];

			oldp = trace->blocks[index];
			if ((newp = mm_realloc(oldp, newsize)) == NULL)
				app_error("mm_realloc failed in eval_mm_util");

			/* 영역과 크기 기록 */
			trace->blocks[index] = newp;
			trace->block_sizes[index] = newsize;

			/* 현재 할당된 모든 블록의 총 크기 추적 */
			total_size += (newsize - oldsize);

			/* 통계 갱신 */
			max_total_size = (total_size > max_total_size) ? total_size : max_total_size;
			break;

		case FREE: /* mm_free */
			index = trace->ops[i].index;
			size = trace->block_sizes[index];
			p = trace->blocks[index];

			mm_free(p);
			trace->blocks[index] = NULL;
			trace->block_sizes[index] = 0;

			/* 현재 할당된 모든 블록의 총 크기 추적 */
			total_size -= size;

			break;

		default:
			app_error("Nonexistent request type in eval_mm_util");
		}

		heap_after = mem_heapsize();

		if (!strcmp(trace->filename, "binary-bal.rep") && i == 5999)
		{
			dump_live_block_distribution(trace, i);
			mm_dump_free_stats("binary-bal before phase-2");
		}
		if (!strcmp(trace->filename, "binary2-bal.rep") && i == 12014)
		{
			dump_live_block_distribution(trace, i);
			mm_dump_free_stats("binary2-bal before phase-2");
		}

		if (should_log_growth(trace) && heap_after > heap_before)
		{
			int req_size = 0;
			if (trace->ops[i].type == ALLOC || trace->ops[i].type == REALLOC)
				req_size = trace->ops[i].size;

			printf("[growth] trace=%s op=%d line=%d type=%s index=%d req=%d total=%d max=%d heap=%d->%d util_now=%.4f\n",
				   trace->filename,
				   i,
				   LINENUM(i),
				   op_name(trace->ops[i].type),
				   trace->ops[i].index,
				   req_size,
				   total_size,
				   max_total_size,
				   heap_before,
				   heap_after,
				   heap_after ? ((double)total_size / (double)heap_after) : 0.0);
		}
	}

	return ((double)max_total_size / (double)mem_heapsize());
}

/*
 * eval_mm_speed - fcyc()가 mm malloc 패키지의 실행 시간을 측정할 때 사용하는 함수
 */
static void eval_mm_speed(void *ptr)
{
	int i, index, size, newsize;
	char *p, *newp, *oldp, *block;
	trace_t *trace = ((speed_t *)ptr)->trace;

	/* 힙을 초기화하고 mm 패키지를 초기화 */
	mem_reset_brk();
	if (mm_init() < 0)
		app_error("mm_init failed in eval_mm_speed");

	/* 각 trace 요청을 해석 */
	for (i = 0; i < trace->num_ops; i++)
		switch (trace->ops[i].type)
		{

		case ALLOC: /* mm_malloc */
			index = trace->ops[i].index;
			size = trace->ops[i].size;
			if ((p = mm_malloc(size)) == NULL)
				app_error("mm_malloc error in eval_mm_speed");
			trace->blocks[index] = p;
			break;

		case REALLOC: /* mm_realloc */
			index = trace->ops[i].index;
			newsize = trace->ops[i].size;
			oldp = trace->blocks[index];
			if ((newp = mm_realloc(oldp, newsize)) == NULL)
				app_error("mm_realloc error in eval_mm_speed");
			trace->blocks[index] = newp;
			break;

		case FREE: /* mm_free */
			index = trace->ops[i].index;
			block = trace->blocks[index];
			mm_free(block);
			break;

		default:
			app_error("Nonexistent request type in eval_mm_valid");
		}
}

/*
 * eval_libc_valid - libc malloc이 주어진 trace 집합을 끝까지 처리할 수 있는지 확인
 *    보수적으로 접근하여 libc malloc 호출 하나라도 실패하면 종료한다.
 *
 */
static int eval_libc_valid(trace_t *trace, int tracenum)
{
	int i, newsize;
	char *p, *newp, *oldp;

	for (i = 0; i < trace->num_ops; i++)
	{
		switch (trace->ops[i].type)
		{

		case ALLOC: /* malloc */
			if ((p = malloc(trace->ops[i].size)) == NULL)
			{
				malloc_error(tracenum, i, "libc malloc failed");
				unix_error("System message");
			}
			trace->blocks[trace->ops[i].index] = p;
			break;

		case REALLOC: /* realloc */
			newsize = trace->ops[i].size;
			oldp = trace->blocks[trace->ops[i].index];
			if ((newp = realloc(oldp, newsize)) == NULL)
			{
				malloc_error(tracenum, i, "libc realloc failed");
				unix_error("System message");
			}
			trace->blocks[trace->ops[i].index] = newp;
			break;

		case FREE: /* free */
			free(trace->blocks[trace->ops[i].index]);
			break;

		default:
			app_error("invalid operation type  in eval_libc_valid");
		}
	}

	return 1;
}

/*
 * eval_libc_speed - fcyc()가 trace 집합에서 libc malloc 패키지의 실행 시간을
 *                   측정할 때 사용하는 함수
 */
static void eval_libc_speed(void *ptr)
{
	int i;
	int index, size, newsize;
	char *p, *newp, *oldp, *block;
	trace_t *trace = ((speed_t *)ptr)->trace;

	for (i = 0; i < trace->num_ops; i++)
	{
		switch (trace->ops[i].type)
		{
		case ALLOC: /* malloc */
			index = trace->ops[i].index;
			size = trace->ops[i].size;
			if ((p = malloc(size)) == NULL)
				unix_error("malloc failed in eval_libc_speed");
			trace->blocks[index] = p;
			break;

		case REALLOC: /* realloc */
			index = trace->ops[i].index;
			newsize = trace->ops[i].size;
			oldp = trace->blocks[index];
			if ((newp = realloc(oldp, newsize)) == NULL)
				unix_error("realloc failed in eval_libc_speed\n");

			trace->blocks[index] = newp;
			break;

		case FREE: /* free */
			index = trace->ops[i].index;
			block = trace->blocks[index];
			free(block);
			break;
		}
	}
}

/*************************************
 * 기타 보조 루틴
 ************************************/

/*
 * printresults - malloc 패키지의 성능 요약 출력
 */
static void printresults(int n, stats_t *stats)
{
	int i;
	double secs = 0;
	double ops = 0;
	double util = 0;

	/* 각 trace의 개별 결과 출력 */
	printf("%5s%7s %5s%8s%10s%6s\n",
		   "trace", " valid", "util", "ops", "secs", "Kops");
	for (i = 0; i < n; i++)
	{
		if (stats[i].valid)
		{
			printf("%2d%10s%5.0f%%%8.0f%10.6f%6.0f\n",
				   i,
				   "yes",
				   stats[i].util * 100.0,
				   stats[i].ops,
				   stats[i].secs,
				   (stats[i].ops / 1e3) / stats[i].secs);
			secs += stats[i].secs;
			ops += stats[i].ops;
			util += stats[i].util;
		}
		else
		{
			printf("%2d%10s%6s%8s%10s%6s\n",
				   i,
				   "no",
				   "-",
				   "-",
				   "-",
				   "-");
		}
	}

	/* trace 집합의 종합 결과 출력 */
	if (errors == 0)
	{
		printf("%12s%5.0f%%%8.0f%10.6f%6.0f\n",
			   "Total       ",
			   (util / n) * 100.0,
			   ops,
			   secs,
			   (ops / 1e3) / secs);
	}
	else
	{
		printf("%12s%6s%8s%10s%6s\n",
			   "Total       ",
			   "-",
			   "-",
			   "-",
			   "-");
	}
}

/*
 * app_error - 일반 애플리케이션 오류 보고
 */
void app_error(char *msg)
{
	printf("%s\n", msg);
	exit(1);
}

/*
 * unix_error - Unix 스타일 오류 보고
 */
void unix_error(char *msg)
{
	printf("%s: %s\n", msg, strerror(errno));
	exit(1);
}

/*
 * malloc_error - mm_malloc 패키지가 반환한 오류 보고
 */
void malloc_error(int tracenum, int opnum, char *msg)
{
	errors++;
	printf("ERROR [trace %d, line %d]: %s\n", tracenum, LINENUM(opnum), msg);
}

/*
 * usage - 명령행 인자 설명
 */
static void usage(void)
{
	fprintf(stderr, "Usage: mdriver [-hvVal] [-f <file>] [-t <dir>]\n");
	fprintf(stderr, "Options\n");
	fprintf(stderr, "\t-a         Don't check the team structure.\n");
	fprintf(stderr, "\t-f <file>  Use <file> as the trace file.\n");
	fprintf(stderr, "\t-g         Generate summary info for autograder.\n");
	fprintf(stderr, "\t-h         Print this message.\n");
	fprintf(stderr, "\t-l         Run libc malloc as well.\n");
	fprintf(stderr, "\t-t <dir>   Directory to find default traces.\n");
	fprintf(stderr, "\t-v         Print per-trace performance breakdowns.\n");
	fprintf(stderr, "\t-V         Print additional debug info.\n");
}
