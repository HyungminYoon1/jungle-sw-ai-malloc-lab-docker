/*
 * mm-implicit free list - 해제된 블록을 힙 안에서 관리합니다.
 *
 * 각 블록에 헤더/푸터를 둠
 * 헤더/푸터에 블록 크기와 할당 여부를 저장함
 * free된 블록은 "힙 안에 그대로 남아 있는 빈 블록"이 됨
 * malloc이 오면 힙 처음부터 순회하면서 쓸 수 있는 free block을 찾음
 * 인접 free block이 있으면 coalesce로 합침
 * 
 */
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <string.h>

#include "mm.h"
#include "memlib.h"

/*********************************************************
 * 학생 안내: 다른 작업을 하기 전에 아래 구조체에
 * 팀 정보를 먼저 입력하세요.
 ********************************************************/
team_t team = {
    /* 팀 이름 */
    "team303-5",
    /* 첫 번째 팀원의 전체 이름 */
    "hmyoon",
    /* 첫 번째 팀원의 로그인 ID */
    "hmyoon",
    /* 두 번째 팀원의 전체 이름(없으면 비워 두기) */
    "",
    /* 두 번째 팀원의 로그인 ID(없으면 비워 두기) */
    ""};

/* 워드(4바이트) 또는 더블 워드(8바이트) 정렬 */
#define ALIGNMENT 8 
#define WSIZE 4 // 워드, 헤더, 푸터 사이즈(bytes)
#define DSIZE 8 // 더블워드 사이즈(bytes)
#define CHUNKSIZE (1 << 12) // heap을 확장하는 사이즈(bytes)

#define MAX(x, y) ((x) > (y) ? (x) : (y))

/* ALIGNMENT의 가장 가까운 배수로 올림 */
#define ALIGN(size) (((size) + (ALIGNMENT - 1)) & ~0x7)

// 블록의 크기(size)와 할당 여부(allocated bit)를 하나의 워드(word) 값 안에 같이 넣어 저장한다
#define PACK(size, alloc) ((size) | (alloc))

// 주소 p에서 워드를 읽고 쓴다.
#define GET(p) (*(unsigned int *)(p))
#define PUT(p, val) (*(unsigned int *)(p) = (val))

// 주소 p에서 사이즈와 할당된 필드를 읽는다.
#define GET_SIZE(p) (GET(p) & ~0x7) // GET_SIZE: 아래 3비트를 지우고 크기만 꺼냄
#define GET_ALLOC(p) (GET(p) & 0x1) // GET_ALLOC: 마지막 1비트만 꺼냄

// 주어진 블록 포인터 bp에 대해 그것의 헤더와 푸터의 주소를 계산한다.
#define HDRP(bp) ((char *)(bp) - WSIZE)
#define FTRP(bp) ((char *)(bp) + GET_SIZE(HDRP(bp)) - DSIZE)

// 주어진 블록 포인터 bp에 대해 그것의 이전 및 다음 블록의 주소를 계산한다.
#define NEXT_BLKP(bp) ((char *)(bp) + GET_SIZE(((char *)(bp) - WSIZE)))
#define PREV_BLKP(bp) ((char *)(bp) - GET_SIZE(((char *)(bp) - DSIZE)))

// #define SIZE_T_SIZE (ALIGN(sizeof(size_t)))

static char *heap_listp = NULL;

static void *extend_heap(size_t words);
static void *coalesce(void *bp);
static void *find_fit(size_t asize);
static void place(void *bp, size_t asize);

/*
 * mm_init - malloc 패키지 초기화
 */
int mm_init(void)
{
    // 최초 비어있는 heap 생성
    if ((heap_listp = mem_sbrk(4*WSIZE)) == (void *)-1)
        return -1;
    
    PUT(heap_listp, 0); // 패딩 할당
    PUT(heap_listp + (1*WSIZE), PACK(DSIZE, 1)); // 프롤로그 헤더
    PUT(heap_listp + (2*WSIZE), PACK(DSIZE, 1)); // 프롤로그 푸터
    PUT(heap_listp + (3*WSIZE), PACK(0, 1)); // 에필로그 헤더
    heap_listp += (2 * WSIZE);

    // 비어있는 heap을 CHUNKSIZE 만큼의 free 블록으로 확장한다.
    if (extend_heap(CHUNKSIZE/WSIZE) == NULL)
        return -1;
    
    return 0;
}

//  mm_malloc - implicit free list에서 요청 크기에 맞는 블록을 할당
void *mm_malloc(size_t size)
{
    size_t asize;      // 정렬과 헤더/푸터를 포함한 실제 할당 블록 크기
    size_t extendsize; // 적절한 free block이 없을 때 힙을 얼마나 늘릴지
    char *bp;

    if (size == 0)             // 0바이트 요청은 할당하지 않음
        return NULL;

    // 최소 블록 크기를 보장하면서 8바이트 정렬을 맞춘다.
    if (size <= DSIZE) // size <= DSIZE 이면 최소 블록 크기 16바이트(헤더+푸터+최소 payload)로 맞춘다.
        asize = 2 * DSIZE; 
    else // 그보다 크면 헤더/푸터 오버헤드를 포함해서 DSIZE 배수로 올림한다.
        asize = DSIZE * ((size + DSIZE + (DSIZE - 1)) / DSIZE);

    // first fit 방식으로 들어갈 수 있는 free block을 찾으면 place()로 해당 블록에 배치하고 주소를 반환한다.
    if ((bp = find_fit(asize)) != NULL) {
        place(bp, asize);
        return bp;
    }

    // 맞는 free block이 없으면 힙을 확장한다.
    extendsize = MAX(asize, CHUNKSIZE); // 조금씩 늘리면 비효율적이므로 CHUNKSIZE와 asize 중 큰 값을 사용한다.
    if ((bp = extend_heap(extendsize / WSIZE)) == NULL)
        return NULL;

    // 새로 확장한 free block에 요청 블록을 배치한 뒤 주소를 반환한다.
    place(bp, asize);
    return bp;
}

// place - free block bp에 크기 asize인 할당 블록을 배치. 남는 공간이 최소 블록 크기 이상이면 블록을 분할
static void place(void *bp, size_t asize)
{
    size_t csize = GET_SIZE(HDRP(bp)); // 현재 free block의 전체 크기

    // 현재 free block에서 요청 크기만큼 할당하고도 남는 공간이 최소 블록 크기(2 * DSIZE) 이상이면 분할한다.
    if ((csize - asize) >= (2 * DSIZE)) {
        PUT(HDRP(bp), PACK(asize, 1));           // 앞부분을 할당 블록의 헤더로 설정
        PUT(FTRP(bp), PACK(asize, 1));           // 앞부분을 할당 블록의 푸터로 설정

        bp = NEXT_BLKP(bp);                      // 남은 부분의 시작 블록으로 이동
        PUT(HDRP(bp), PACK(csize - asize, 0));  // 남은 부분을 free block 헤더로 설정
        PUT(FTRP(bp), PACK(csize - asize, 0));  // 남은 부분을 free block 푸터로 설정
    }
    else { // 남는 공간이 너무 작으면 분할하지 않고 현재 블록 전체를 그대로 할당 블록으로 사용한다.
        PUT(HDRP(bp), PACK(csize, 1));          // 전체 블록을 할당 상태로 표시
        PUT(FTRP(bp), PACK(csize, 1));          // 전체 블록의 푸터도 할당 상태로 표시
    }
}

// 새 가용 블록으로 heap 확장 함수
static void *extend_heap(size_t words)
{
    char *bp;
    size_t size;

    size = (words % 2) ? (words + 1) * WSIZE : words * WSIZE;
    if ((long)(bp = mem_sbrk(size)) == -1)
        return NULL;

    PUT(HDRP(bp), PACK(size, 0));
    PUT(FTRP(bp), PACK(size, 0));
    PUT(HDRP(NEXT_BLKP(bp)), PACK(0, 1));

    return coalesce(bp);
}

/*
 * mm_free - 블록 해제 시 병합 함수 호출
 */
void mm_free(void *bp)
{
    if (bp == NULL) {
        return;
    }

    size_t size = GET_SIZE(HDRP(bp));

    PUT(HDRP(bp), PACK(size, 0));
    PUT(FTRP(bp), PACK(size, 0));
    coalesce(bp);
}

// free 블록 병합 함수
static void *coalesce(void *bp)
{
    size_t prev_alloc = GET_ALLOC(FTRP(PREV_BLKP(bp))); // 이전 블록이 할당되어 있는가
    size_t next_alloc = GET_ALLOC(HDRP(NEXT_BLKP(bp))); // 다음 블록이 할당되어 있는가
    size_t size = GET_SIZE(HDRP(bp)); // 현재 블록 크기

    if (prev_alloc && next_alloc) { // 양옆 모두 사용 중
        return bp;
    } else if (prev_alloc && !next_alloc) { // 다음 블록과 병합
        size += GET_SIZE(HDRP(NEXT_BLKP(bp)));
        PUT(HDRP(bp), PACK(size, 0));
        PUT(FTRP(bp), PACK(size, 0));
    } else if (!prev_alloc && next_alloc) { // 이전 블록과 병합
        size += GET_SIZE(HDRP(PREV_BLKP(bp)));
        PUT(FTRP(bp), PACK(size, 0));
        PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0));
        bp = PREV_BLKP(bp);
    } else { // 양옆 모두 free이므로 모두 병합
        size += GET_SIZE(HDRP(PREV_BLKP(bp))) + GET_SIZE(FTRP(NEXT_BLKP(bp)));
        PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0));
        PUT(FTRP(NEXT_BLKP(bp)), PACK(size, 0));
        bp = PREV_BLKP(bp);
    }

    return bp;
}

// 요청한 크기 asize를 담을 수 있는 free block을 힙에서 찾는 함수
static void *find_fit(size_t asize)
{
    void *bp; // 현재 보고 있는 블록의 payload 시작 주소

    for (bp = heap_listp; GET_SIZE(HDRP(bp)) > 0; bp = NEXT_BLKP(bp)) {
        if (!GET_ALLOC(HDRP(bp)) && (asize <= GET_SIZE(HDRP(bp))))
            return bp;
    }
    return NULL;
}

void *mm_realloc(void *ptr, size_t size)
{
    void *newptr; // 새로 할당받을 블록의 포인터
    size_t copySize; // 기존 블록에서 새 블록으로 복사할 바이트 수

    if (ptr == NULL) // 기존 블록이 없으면 realloc은 malloc과 같은 의미
        return mm_malloc(size);

    if (size == 0) { // 새 크기가 0이면 realloc은 free와 같은 의미
        mm_free(ptr);
        return NULL;
    }

    newptr = mm_malloc(size); // 요청한 크기만큼 새 블록을 할당
    if (newptr == NULL) // 새 블록 할당에 실패하면 NULL 반환
        return NULL;

    copySize = GET_SIZE(HDRP(ptr)) - DSIZE; // 현재 블록의 payload 크기 계산
    if (size < copySize) // 새 요청 크기가 더 작으면 그 크기까지만 복사
        copySize = size;

    memcpy(newptr, ptr, copySize); // 기존 데이터 중 필요한 만큼 새 블록으로 복사
    mm_free(ptr); // 기존 블록은 더 이상 필요 없으므로 해제

    return newptr; // 새 블록의 주소 반환
}

