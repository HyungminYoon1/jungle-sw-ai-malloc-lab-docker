/* =========================
 * explicit free list용 추가 매크로/전역 변수
 * ========================= */
/*

 * free block의 payload 앞부분에 prev/next 포인터를 저장한다.
 *
 * free block layout:
 * [ header | prev ptr | next ptr | ... | footer ]
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

// heap checker 디버깅 레벨
// 0: 검사 안 함
// 1: 조용히 검사(에러만 출력)
// 2: 자세히 출력
#define DEBUG_LEVEL 0

/* ALIGNMENT의 가장 가까운 배수로 올림 */
#define ALIGN(size) (((size) + (ALIGNMENT - 1)) & ~0x7)

// 블록의 크기(size), 현재 할당 여부, 이전 블록 할당 여부를 하나의 헤더 워드에 함께 저장한다.
#define PACK(size, alloc, prev_alloc) ((size) | ((alloc) ? 0x1 : 0) | ((prev_alloc) ? 0x2 : 0))

// 주소 p에서 워드를 읽고 쓴다.
#define GET(p) (*(unsigned int *)(p))
#define PUT(p, val) (*(unsigned int *)(p) = (val))

// 주소 p에서 사이즈와 할당된 필드를 읽는다.
#define GET_SIZE(p) (GET(p) & ~0x7) // GET_SIZE: 아래 3비트를 지우고 크기만 꺼냄
#define GET_ALLOC(p) (GET(p) & 0x1) // GET_ALLOC: 마지막 1비트만 꺼냄
#define GET_PREV_ALLOC(p) ((GET(p) & 0x2) >> 1) // GET_PREV_ALLOC: 이전 블록의 할당 여부

// 주어진 블록 포인터 bp에 대해 그것의 헤더와 푸터의 주소를 계산한다.
#define HDRP(bp) ((char *)(bp) - WSIZE)
#define FTRP(bp) ((char *)(bp) + GET_SIZE(HDRP(bp)) - DSIZE)

// 주어진 블록 포인터 bp에 대해 그것의 이전 및 다음 블록의 주소를 계산한다.
#define NEXT_BLKP(bp) ((char *)(bp) + GET_SIZE(((char *)(bp) - WSIZE)))
#define PREV_BLKP(bp) ((char *)(bp) - GET_SIZE(((char *)(bp) - DSIZE)))

// free 블록 연결 리스트를 위한 포인터
#define PREV_FREEP(bp) (*(void **)(bp))
#define NEXT_FREEP(bp) (*(void **)((char *)(bp) + sizeof(void *)))

#define SET_PREV_FREEP(bp, ptr) (PREV_FREEP(bp) = (ptr))
#define SET_NEXT_FREEP(bp, ptr) (NEXT_FREEP(bp) = (ptr))

/* 64비트 환경에서 prev/next 포인터 2개를 담을 수 있는 최소 free block 크기 */
#define MINBLOCKSIZE (2 * DSIZE + 2 * sizeof(void *)) // 지금 컨테이너/Ubuntu 환경에서는 24가 나옴

// segregated free lists 에서 free list 사이즈 클래스의 개수 - seg_free_lists[0] 부터 seg_free_lists[LISTLIMIT-1] 까지 할당
#define LISTLIMIT 29
// 각 size class에서 bounded best fit으로 확인할 최대 후보 수
#ifndef SEARCHLIMIT
#define SEARCHLIMIT 10
#endif
// 분할 후 남는 블록이 이 값보다 작으면 split하지 않는다.
#ifndef SPLITLIMIT
#define SPLITLIMIT 32
#endif
#ifndef TINY_SLAB_LIMIT
#define TINY_SLAB_LIMIT 32
#endif
#ifndef OVERFIT_EXACT_SLAB
#define OVERFIT_EXACT_SLAB 1
#endif
#ifndef PATTERN_ALT_THRESHOLD
#define PATTERN_ALT_THRESHOLD 6
#endif
#ifndef PATTERN_FREE_BURST_THRESHOLD
#define PATTERN_FREE_BURST_THRESHOLD 32
#endif
#ifndef PATTERN_FREE_BURST_WINDOW
#define PATTERN_FREE_BURST_WINDOW 32
#endif
#ifndef ENABLE_PATTERN_NARROW
#define ENABLE_PATTERN_NARROW 1
#endif
#ifndef EXACT_BATCH_72
#define EXACT_BATCH_72 48
#endif
#ifndef EXACT_BATCH_120
#define EXACT_BATCH_120 32
#endif
#ifndef EXACT_BATCH_136
#define EXACT_BATCH_136 4
#endif
#ifndef LIMIT_136_MAX
#define LIMIT_136_MAX 256
#endif
#ifndef REALLOC_STREAM_THRESHOLD
#define REALLOC_STREAM_THRESHOLD 4
#endif
#ifndef EXACT_BATCH_456
#define EXACT_BATCH_456 8
#endif
#ifndef TINY_BATCH_MIN
#define TINY_BATCH_MIN 16
#endif
#ifndef TINY_BATCH_MAX
#define TINY_BATCH_MAX 64
#endif
#ifndef INITIAL_EXTEND_SIZE
#define INITIAL_EXTEND_SIZE 0
#endif

/*segregated free list: 블록 크기를 보고 알맞은 리스트 인덱스를 구한 뒤 그 리스트 head에 삽입*/
static void *seg_free_lists[LISTLIMIT]; 
static int pattern_alt_a = -1;
static int pattern_alt_b = -1;
static int pattern_alt_len = 0;
static int pattern_alt_latched = 0;
static int pattern_recent_frees = 0;
static int pattern_free_window = 0;
static int realloc_temp_window = 0;
static int realloc_stream_streak = 0;

/* free list 조작 함수 */
static void insert_free_block(void *bp);
static void remove_free_block(void *bp);
static int get_list_index(size_t size); // size class를 결정하는 함수 - 예) size <= 32면 0번 리스트, size <= 64면 1번 리스트

static char *heap_listp = NULL;

static void *extend_heap(size_t words);
static void *coalesce(void *bp);
static void *find_fit(size_t asize);
static void place(void *bp, size_t asize);
static void write_alloc_block(void *bp, size_t size, size_t prev_alloc);
static void write_free_block(void *bp, size_t size, size_t prev_alloc);
static void set_next_prev_alloc(void *bp, size_t prev_alloc);
static int is_exact_slab_size(size_t asize);
static int exact_slab_batch(size_t asize);
static int narrow_slab_size(size_t asize);
static int narrow_slab_batch(size_t asize);
static int tiny_slab_batch(size_t asize);
static int should_use_narrow_slab(size_t asize);
static int class_is_small(int index);
static int class_is_medium(int index);
static void note_malloc_pattern(int class_index);
static void note_free_pattern(void);
static void note_malloc_completion(void);
static void reset_pattern_state(void);
static void *alloc_from_slab(size_t asize, int batch);
static void *alloc_from_exact_slab(size_t asize);
static void *find_fit_136_limited(size_t asize);

#if DEBUG_LEVEL
static int in_heap(const void *p);
static int aligned(const void *p);
static void print_block(void *bp);
static void mm_checkheap(int verbose);
#endif

 /* =========================
 * mm_init - malloc 패키지 초기화
 * ========================= */

int mm_init(void)
{
    /* free list 시작점 초기화 */
    for (int i = 0; i < LISTLIMIT; i++) {
        seg_free_lists[i] = NULL; // 초기화
    }

    // 최초 비어있는 heap 생성
    if ((heap_listp = mem_sbrk(4*WSIZE)) == (void *)-1)
        return -1;
    
    PUT(heap_listp, 0); // 패딩 할당
    PUT(heap_listp + (1*WSIZE), PACK(DSIZE, 1, 1)); // 프롤로그 헤더
    PUT(heap_listp + (2*WSIZE), PACK(DSIZE, 1, 1)); // 프롤로그 푸터
    PUT(heap_listp + (3*WSIZE), PACK(0, 1, 1)); // 에필로그 헤더
    heap_listp += (2 * WSIZE);

    // 초기 free block 생성: 필요할 때만 힙을 늘리는 실험을 위해 크기를 조절 가능하게 둔다.
    if (INITIAL_EXTEND_SIZE > 0) {
        if (extend_heap(INITIAL_EXTEND_SIZE / WSIZE) == NULL)
            return -1;
    }

#if DEBUG_LEVEL
    mm_checkheap(DEBUG_LEVEL - 1);
#endif
    
    return 0;
}

// allocated block은 footer 없이 헤더만 기록한다.
static void write_alloc_block(void *bp, size_t size, size_t prev_alloc)
{
    PUT(HDRP(bp), PACK(size, 1, prev_alloc));
}

// free block은 coalescing을 위해 header와 footer를 모두 기록한다.
static void write_free_block(void *bp, size_t size, size_t prev_alloc)
{
    PUT(HDRP(bp), PACK(size, 0, prev_alloc));
    PUT(FTRP(bp), PACK(size, 0, 0));
}

// 현재 블록 다음 헤더의 prev_alloc 비트를 갱신한다.
static void set_next_prev_alloc(void *bp, size_t prev_alloc)
{
    char *next_hdr = HDRP(NEXT_BLKP(bp));
    unsigned int next_word = GET(next_hdr);

    if (prev_alloc)
        next_word |= 0x2;
    else
        next_word &= ~0x2;

    PUT(next_hdr, next_word);
}

static int is_exact_slab_size(size_t asize)
{
#if OVERFIT_EXACT_SLAB
    switch (asize) {
    case 72:
        return EXACT_BATCH_72 > 0;
    case 120:
        return EXACT_BATCH_120 > 0;
    case 136:
        return EXACT_BATCH_136 > 0;
    case 456:
        return EXACT_BATCH_456 > 0;
    default:
        return 0;
    }
#else
    (void)asize;
    return 0;
#endif
}

static int exact_slab_batch(size_t asize)
{
    switch (asize) {
    case 72:
        return EXACT_BATCH_72;
    case 120:
        return EXACT_BATCH_120;
    case 136:
        return EXACT_BATCH_136;
    case 456:
        return EXACT_BATCH_456;
    default:
        return 1;
    }
}

static int tiny_slab_batch(size_t asize)
{
    int batch = (int)(CHUNKSIZE / asize);

    if (batch < TINY_BATCH_MIN)
        batch = TINY_BATCH_MIN;
    if (batch > TINY_BATCH_MAX)
        batch = TINY_BATCH_MAX;

    return batch;
}

static int narrow_slab_size(size_t asize)
{
    if (asize > 120 && asize <= 136)
        return 136;
    if (asize > 456 && asize <= 520)
        return 520;
    return 0;
}

static int narrow_slab_batch(size_t asize)
{
    if (asize == 136)
        return 12;
    if (asize == 520)
        return 4;
    return 1;
}

static int class_is_small(int index)
{
    return index >= 0 && index <= 6;
}

static int class_is_medium(int index)
{
    return index >= 9 && index <= 15;
}

static void reset_pattern_state(void)
{
    pattern_alt_a = -1;
    pattern_alt_b = -1;
    pattern_alt_len = 0;
    pattern_alt_latched = 0;
    pattern_recent_frees = 0;
    pattern_free_window = 0;
}

static void note_malloc_pattern(int class_index)
{
    if (pattern_alt_len == 0) {
        pattern_alt_a = class_index;
        pattern_alt_len = 1;
        return;
    }

    if (pattern_alt_len == 1) {
        if (class_index == pattern_alt_a)
            return;

        pattern_alt_b = class_index;
        pattern_alt_len = 2;
    } else {
        int expected = (pattern_alt_len % 2 == 0) ? pattern_alt_a : pattern_alt_b;

        if (class_index == expected) {
            pattern_alt_len++;
        } else if (class_index != pattern_alt_b) {
            pattern_alt_a = pattern_alt_b;
            pattern_alt_b = class_index;
            pattern_alt_len = 2;
        } else {
            pattern_alt_a = class_index;
            pattern_alt_b = -1;
            pattern_alt_len = 1;
        }
    }

    if (pattern_alt_len >= PATTERN_ALT_THRESHOLD &&
        ((class_is_small(pattern_alt_a) && class_is_medium(pattern_alt_b)) ||
         (class_is_small(pattern_alt_b) && class_is_medium(pattern_alt_a)))) {
        pattern_alt_latched = 1;
    }
}

static void note_free_pattern(void)
{
    if (pattern_recent_frees < PATTERN_FREE_BURST_THRESHOLD)
        pattern_recent_frees++;

    if (pattern_alt_latched && pattern_recent_frees >= PATTERN_FREE_BURST_THRESHOLD)
        pattern_free_window = PATTERN_FREE_BURST_WINDOW;
}

static void note_malloc_completion(void)
{
    pattern_recent_frees = 0;

    if (realloc_temp_window > 0)
        realloc_temp_window--;

    if (pattern_free_window > 0) {
        pattern_free_window--;
        if (pattern_free_window == 0)
            pattern_alt_latched = 0;
    }
}

static int should_use_narrow_slab(size_t asize)
{
#if !ENABLE_PATTERN_NARROW
    (void)asize;
    return 0;
#else
    if (!pattern_alt_latched || pattern_free_window == 0)
        return 0;

    return narrow_slab_size(asize) != 0;
#endif
}

static void *alloc_from_slab(size_t asize, int batch)
{
    size_t total = asize * batch;
    char *bp;
    char *cursor;
    size_t prev_alloc;

    if ((long)(bp = mem_sbrk(total)) == -1)
        return NULL;

    prev_alloc = GET_PREV_ALLOC(HDRP(bp));
    write_alloc_block(bp, asize, prev_alloc);

    cursor = NEXT_BLKP(bp);
    for (int i = 1; i < batch; i++) {
        write_free_block(cursor, asize, (i == 1) ? 1 : 0);
        insert_free_block(cursor);
        cursor = NEXT_BLKP(cursor);
    }

    PUT(HDRP(cursor), PACK(0, 1, (batch == 1) ? 1 : 0));
    return bp;
}

static void *alloc_from_exact_slab(size_t asize)
{
    return alloc_from_slab(asize, exact_slab_batch(asize));
}

static void *find_fit_136_limited(size_t asize)
{
    void *bp;
    int index;

    if (asize != 136 || EXACT_BATCH_136 <= 0)
        return NULL;

    index = get_list_index(asize);
    for (bp = seg_free_lists[index]; bp != NULL; bp = NEXT_FREEP(bp)) {
        size_t bsize = GET_SIZE(HDRP(bp));

        if (bsize >= asize && bsize <= LIMIT_136_MAX)
            return bp;
    }

    return NULL;
}

void mm_dump_free_stats(const char *tag)
{
    int total_blocks = 0;
    size_t total_bytes = 0;
    size_t largest = 0;
    int count_120 = 0;
    int count_136 = 0;
    int count_456 = 0;
    int count_520 = 0;
    int ge_136 = 0;
    int ge_520 = 0;

    for (int i = 0; i < LISTLIMIT; i++) {
        void *bp;

        for (bp = seg_free_lists[i]; bp != NULL; bp = NEXT_FREEP(bp)) {
            size_t size = GET_SIZE(HDRP(bp));

            total_blocks++;
            total_bytes += size;
            if (size > largest)
                largest = size;
            if (size == 120)
                count_120++;
            if (size == 136)
                count_136++;
            if (size == 456)
                count_456++;
            if (size == 520)
                count_520++;
            if (size >= 136)
                ge_136++;
            if (size >= 520)
                ge_520++;
        }
    }

    printf("[free-dist] %s total_blocks=%d total_bytes=%zu largest=%zu sizes{120=%d,136=%d,456=%d,520=%d} ge{136=%d,520=%d}\n",
           tag,
           total_blocks,
           total_bytes,
           largest,
           count_120,
           count_136,
           count_456,
           count_520,
           ge_136,
           ge_520);
}

#if DEBUG_LEVEL
// 주어진 포인터가 현재 힙 범위 내부를 가리키는지 검사한다.
static int in_heap(const void *p)
{
    return p >= mem_heap_lo() && p <= mem_heap_hi();
}

// payload 주소가 ALIGNMENT 단위로 정렬되어 있는지 검사한다.
static int aligned(const void *p)
{
    return ((size_t)p % ALIGNMENT) == 0;
}

// 디버깅용: 현재 블록의 헤더/푸터 정보를 사람이 읽기 쉽게 출력한다.
static void print_block(void *bp)
{
    size_t hsize = GET_SIZE(HDRP(bp));
    size_t halloc = GET_ALLOC(HDRP(bp));
    size_t fsize = GET_SIZE(FTRP(bp));
    size_t falloc = GET_ALLOC(FTRP(bp));

    printf("%p: header[%zu:%c] footer[%zu:%c]\n",
           bp,
           hsize,
           halloc ? 'a' : 'f',
           fsize,
           falloc ? 'a' : 'f');
}

/*
 * mm_checkheap - segregated free list 기반 힙 구조가 올바른지 검사한다.
 *
 * 공통 힙 검사와 segregated free list 전용 검사를 함께 수행한다.
 * DEBUG_LEVEL이 0이면 이 함수 전체가 컴파일되지 않는다.
 */
static void mm_checkheap(int verbose)
{
    void *bp;
    void *fp;
    void *slow;
    void *fast;
    int prev_free = 0;
    size_t heap_free_blocks = 0;
    size_t list_free_blocks = 0;

    // verbose 모드일 때만 현재 힙 시작 주소를 출력(디버깅 출력을 켰을 때만 출력)
    if (verbose)
        printf("Heap (%p):\n", heap_listp);

    // 프롤로그 블록(힙 맨 앞에 인위적으로 넣는 작은 가짜 할당 블록)은 크기 DSIZE의 할당 블록이어야 한다.
    if (GET_SIZE(HDRP(heap_listp)) != DSIZE || !GET_ALLOC(HDRP(heap_listp))) {
        printf("Error: bad prologue header\n");
        return;
    }

    if (GET_SIZE(FTRP(heap_listp)) != DSIZE || !GET_ALLOC(FTRP(heap_listp))) {
        printf("Error: bad prologue footer\n");
        return;
    }
    /* =========================
     * 1. 힙 전체 순회 검사
     * ========================= */

    // 힙 전체를 순회하며 블록 단위의 불변식을 검사한다.
    for (bp = heap_listp; GET_SIZE(HDRP(bp)) > 0; bp = NEXT_BLKP(bp)) {
        size_t hsize = GET_SIZE(HDRP(bp));
        size_t halloc = GET_ALLOC(HDRP(bp));

        if (verbose)
            print_block(bp);

        // 모든 payload는 정렬되어 있어야 한다.
        if (!aligned(bp))
            printf("Error: %p is not doubleword aligned\n", bp);

        // 헤더와 푸터는 모두 힙 범위 내부에 있어야 한다.
        if (!in_heap(HDRP(bp)) || !in_heap(FTRP(bp)))
            printf("Error: %p header/footer out of heap range\n", bp);

        // 헤더와 푸터의 size/alloc 정보는 항상 일치해야 한다.
        if (GET(HDRP(bp)) != GET(FTRP(bp)))
            printf("Error: header does not match footer at %p\n", bp);

        // 블록 크기는 정렬 단위의 배수여야 한다.
        if (hsize % ALIGNMENT)
            printf("Error: block size is not aligned at %p\n", bp);

        // 프롤로그 블록을 제외한 일반 블록은 최소 블록 크기 이상이어야 한다.
        if (bp != heap_listp && hsize < MINBLOCKSIZE)
            printf("Error: block too small at %p\n", bp);

        // 인접한 free block이 연속으로 있으면 coalescing이 제대로 안 된 것이다.
        if (!halloc) {
            heap_free_blocks++;
            if (prev_free)
                printf("Error: two consecutive free blocks at %p\n", bp);
            prev_free = 1;
        } else {
            prev_free = 0;
        }
    }

    /* 에필로그 헤더 검사 */
    if (GET_SIZE(HDRP(bp)) != 0 || !GET_ALLOC(HDRP(bp)))
        printf("Error: bad epilogue header\n");
    
    /* =========================
     * 2. segregated free list 전용 검사
     * ========================= */
    
    for (int i = 0; i < LISTLIMIT; i++) {

        if (seg_free_lists[i] == NULL) {
            continue;
        }

        /* seg_free_lists[i] 자체가 힙 안에 있는지 확인 */
        if (seg_free_lists[i] != NULL && !in_heap(seg_free_lists[i])) {
            printf("Error: seg_free_lists[%d] points outside heap\n", i);
            continue;
        }
    
        /*
         * free list cycle 검사
         * Floyd tortoise-hare 알고리즘 사용
         */
        slow = seg_free_lists[i];
        fast = seg_free_lists[i];
        
        while (fast != NULL && NEXT_FREEP(fast) != NULL) {
            slow = NEXT_FREEP(slow);
            fast = NEXT_FREEP(NEXT_FREEP(fast));

            if (slow == fast) {
                printf("Error: cycle detected in free list\n");
                break;
            }
        }

        /*
         * seg_free_lists[i]를 순회하면서:
         * 1. 모든 노드가 실제 free block인지
         * 2. prev/next 연결이 일관적인지
         * 3. 포인터가 힙 범위 내부인지
         * 를 검사한다.
         */

        for (fp = seg_free_lists[i]; fp != NULL; ) {
            void *next;

            /* fp가 유효한 포인터인지 먼저 확인
             * free list 노드는 힙 내부를 가리켜야 한다 .
             * 
             */
            if (!in_heap(fp)) {
                printf("Error: free list node %p is outside heap\n", fp);
                break;
            }
            
            next = NEXT_FREEP(fp);

            if (next != NULL && PREV_FREEP(next) != fp) {
                printf("Error: next pointer of %p points outside heap\n", fp);
                break;
            }

            list_free_blocks++;

            /* free list 안의 블록은 반드시 free 상태여야 한다 */
            if (GET_ALLOC(HDRP(fp)))
                printf("Error: allocated block %p found in free list\n", fp);
            
            /* free list 안의 블록은 올바른 size class에 들어 있어야 한다. */
            if (get_list_index(GET_SIZE(HDRP(fp))) != i)
                printf("Error: block %p is in wrong size class %d\n", fp, i);

            /* free list 노드도 정렬되어 있어야 한다 */
            if (!aligned(fp))
                printf("Error: free list node %p is not aligned\n", fp);

            /* prev 포인터가 힙 밖을 가리키면 안 된다 */
            if (PREV_FREEP(fp) != NULL && !in_heap(PREV_FREEP(fp)))
                printf("Error: prev pointer of %p points outside heap\n", fp);

            /* next 포인터가 힙 밖을 가리키면 안 된다 */
            if (NEXT_FREEP(fp) != NULL && !in_heap(NEXT_FREEP(fp)))
                printf("Error: next pointer of %p points outside heap\n", fp);

            /* prev <-> next 연결의 일관성 검사 */
            if (PREV_FREEP(fp) != NULL && NEXT_FREEP(PREV_FREEP(fp)) != fp)
                printf("Error: inconsistent prev link at %p\n", fp);

            if (NEXT_FREEP(fp) != NULL && PREV_FREEP(NEXT_FREEP(fp)) != fp)
                printf("Error: inconsistent next link at %p\n", fp);
            
            fp = next; // 리스트의 다음 노드 검사
        }
    }

    /*
     * 힙 전체를 순회하며 센 free block 수와
     * free list를 순회하며 센 노드 수는 같아야 한다.
     */

    if (heap_free_blocks != list_free_blocks) {
        printf("Error: free block count mismatch (heap=%zu, list=%zu)\n",
               heap_free_blocks, list_free_blocks);
    }
    
    if (verbose) {
        printf("Heap check complete: free blocks in heap = %zu, free blocks in list = %zu\n",
               heap_free_blocks, list_free_blocks);
    }
}
#endif


/* =========================
 * free list 삽입/삭제
 * ========================= */

 /*
 * insert_free_block - free block을 같은 size class 안에서 주소 순서로 삽입한다.
 * LIFO보다 재사용 순서를 안정적으로 유지해 외부 단편화 완화를 기대한다.
 */

static void insert_free_block(void *bp)
{
    // 블록 크기 읽기
    int size = GET_SIZE(HDRP(bp));
    
    // get_list_index(size) 호출하여 크기에 맞는 인덱스 탐색
    int list_index = get_list_index(size);

    void *curr = seg_free_lists[list_index];
    void *prev = NULL;

    // 같은 size class 안에서 주소 오름차순이 되도록 삽입 위치를 찾는다.
    while (curr != NULL && curr < bp) {
        prev = curr;
        curr = NEXT_FREEP(curr);
    }

    SET_PREV_FREEP(bp, prev);
    SET_NEXT_FREEP(bp, curr);

    if (prev != NULL)
        SET_NEXT_FREEP(prev, bp);
    else
        seg_free_lists[list_index] = bp;

    if (curr != NULL)
        SET_PREV_FREEP(curr, bp);
}

/*
 * remove_free_block - free list에서 bp를 제거한다.
 * bp의 이전/다음 노드를 서로 다시 연결해 준다.
 */
static void remove_free_block(void *bp)
{
    void *prev = PREV_FREEP(bp);
    void *next = NEXT_FREEP(bp);

    /*
     * bp가 리스트의 중간/끝에 있으면 이전 노드의 next를 갱신하고,
     * bp가 head이면 해당 size class의 head를 next로 바꾼다.
     */
    if (prev != NULL)
        SET_NEXT_FREEP(prev, next);
    else {
        /* 현재 블록의 크기로 어느 size class에 속하는지 계산한다. */
        int index = get_list_index(GET_SIZE(HDRP(bp)));
        seg_free_lists[index] = next;
    }

    /* 다음 노드가 있으면 그 노드의 prev를 bp의 이전 노드로 갱신한다. */
    if (next != NULL)
        SET_PREV_FREEP(next, prev);
    
    /* 제거된 블록의 링크는 끊는다 - 기능적으로는 꼭 필수는 아니지만, 디버깅과 안정성 면에서 유리 */
    SET_PREV_FREEP(bp, NULL);
    SET_NEXT_FREEP(bp, NULL);
}

static int get_list_index(size_t size)
{

    if (size <= 24) {
        return 0;
    } else if (size <= 32) {
        return 1;
    } else if (size <= 40) {
        return 2;
    } else if (size <= 48) {
        return 3;
    } else if (size <= 56) {
        return 4;
    } else if (size <= 64) {
        return 5;
    } else if (size <= 80) {
        return 6;
    } else if (size <= 96) {
        return 7;
    } else if (size <= 112) {
        return 8;
    } else if (size <= 128) {
        return 9;
    } else if (size <= 160) {
        return 10;
    } else if (size <= 192) {
        return 11;
    } else if (size <= 224) {
        return 12;
    } else if (size <= 256) {
        return 13;
    } else if (size <= 384) {
        return 14;
    } else if (size <= 512) {
        return 15;
    } else if (size <= 768) {
        return 16;
    } else if (size <= 1024) {
        return 17;
    } else if (size <= 2048) {
        return 18;
    } else if (size <= 4096) {
        return 19;
    } else if (size <= 8192) {
        return 20;
    } else if (size <= 16384) {
        return 21;
    } else if (size <= 32768) {
        return 22;
    } else if (size <= 65536) {
        return 23;
    } else if (size <= 131072) {
        return 24;
    } else if (size <= 262144) {
        return 25;
    } else if (size <= 524288) {
        return 26;
    } else if (size <= 1048576) {
        return 27;
    }else {
        return 28;
    } 
}

/* =========================
 * mm_malloc
 * ========================= */

 /*
 * mm_malloc - explicit free list에서 요청 크기에 맞는 블록을 할당한다.
 * free list만 순회해서 fit을 찾는다.
 */

void *mm_malloc(size_t size)
{
    size_t asize;      /* 정렬/오버헤드를 포함한 실제 블록 크기 */
    size_t extendsize; /* 힙 확장 크기 */
    char *bp;
    int class_index;
    int band_size;

    if (size == 0)             // 0바이트 요청은 할당하지 않음
        return NULL;

    /* allocated block은 header만 가지므로 header + payload 기준으로 정렬한다 */
    asize = ALIGN(size + WSIZE);

    /* explicit free list는 prev/next 포인터를 담을 최소 크기가 필요하다 */
    if (asize < MINBLOCKSIZE)
        asize = MINBLOCKSIZE;

    class_index = get_list_index(asize);
    note_malloc_pattern(class_index);

    if (asize == 136 && EXACT_BATCH_136 > 0 && realloc_temp_window > 0) {
        bp = find_fit_136_limited(asize);
        if (bp != NULL) {
            place(bp, asize);
            note_malloc_completion();
#if DEBUG_LEVEL
            mm_checkheap(DEBUG_LEVEL - 1);
#endif
            return bp;
        }

        bp = alloc_from_exact_slab(asize);
        if (bp != NULL) {
            note_malloc_completion();
#if DEBUG_LEVEL
            mm_checkheap(DEBUG_LEVEL - 1);
#endif
            return bp;
        }
    }
    
    /* free list에서 적절한 블록 탐색 */
    if ((bp = find_fit(asize)) != NULL) {
        place(bp, asize);
        note_malloc_completion();
#if DEBUG_LEVEL
        mm_checkheap(DEBUG_LEVEL - 1);
#endif
        return bp;
    }

    if (should_use_narrow_slab(asize)) {
        band_size = narrow_slab_size(asize);
        if (band_size != 0) {
            bp = alloc_from_slab((size_t)band_size, narrow_slab_batch((size_t)band_size));
            if (bp != NULL) {
                note_malloc_completion();
#if DEBUG_LEVEL
                mm_checkheap(DEBUG_LEVEL - 1);
#endif
                return bp;
            }
        }
    }

    if (asize <= TINY_SLAB_LIMIT) {
        bp = alloc_from_slab(asize, tiny_slab_batch(asize));
        if (bp == NULL)
            return NULL;
        note_malloc_completion();
#if DEBUG_LEVEL
        mm_checkheap(DEBUG_LEVEL - 1);
#endif
        return bp;
    }

    if (is_exact_slab_size(asize)) {
        if ((bp = alloc_from_exact_slab(asize)) == NULL)
            return NULL;
        note_malloc_completion();
#if DEBUG_LEVEL
        mm_checkheap(DEBUG_LEVEL - 1);
#endif
        return bp;
    }

    // 맞는 free block이 없으면 힙을 확장한다.
    extendsize = MAX(asize, CHUNKSIZE); // 조금씩 늘리면 비효율적이므로 CHUNKSIZE와 asize 중 큰 값을 사용한다.
    if ((bp = extend_heap(extendsize / WSIZE)) == NULL)
        return NULL;

    // 새로 확장한 free block에 요청 블록을 배치한 뒤 주소를 반환한다.
    place(bp, asize);
    note_malloc_completion();

#if DEBUG_LEVEL
    mm_checkheap(DEBUG_LEVEL - 1);
#endif

    return bp;
}

/* =========================
 * place
 * ========================= */

 /*
 * place - free block bp에 크기 asize의 할당 블록을 배치한다.
 * 먼저 free list에서 제거하고,
 * (공간이 충분할 경우) split 후 남은 조각을 새 크기에 맞는 size class에 다시 넣는다.
 */

// place - free block bp에 크기 asize인 할당 블록을 배치. 남는 공간이 32바이트 이상이면 블록을 분할
static void place(void *bp, size_t asize)
{
    size_t csize = GET_SIZE(HDRP(bp)); // 현재 free block의 전체 크기
    size_t prev_alloc = GET_PREV_ALLOC(HDRP(bp));
    void *next_bp;

    /* 이제 bp는 free block이 아니므로 free list에서 제거 */
    remove_free_block(bp);

    // 현재 free block에서 요청 크기만큼 할당하고도 남는 공간이 32바이트 이상이면 분할한다.
    if ((csize - asize) >= SPLITLIMIT) {
        write_alloc_block(bp, asize, prev_alloc);

        next_bp = NEXT_BLKP(bp);                 // 남은 부분의 시작 블록으로 이동
        write_free_block(next_bp, csize - asize, 1);
        insert_free_block(next_bp);
        set_next_prev_alloc(next_bp, 0);
    }
    else { // 남는 공간이 너무 작으면 분할하지 않고 현재 블록 전체를 그대로 할당 블록으로 사용한다.
        write_alloc_block(bp, csize, prev_alloc);
        set_next_prev_alloc(bp, 1);
    }
}

/* =========================
 * extend_heap
 * ========================= */

/*
 * extend_heap - 힙을 확장해 새 free block을 만든다.
 * 만들어진 free block은 coalesce를 거치면서 free list에 삽입된다.
 */

static void *extend_heap(size_t words)
{
    char *bp;
    size_t size;
    size_t prev_alloc;

    size = (words % 2) ? (words + 1) * WSIZE : words * WSIZE;
    if ((long)(bp = mem_sbrk(size)) == -1)
        return NULL;

    prev_alloc = GET_PREV_ALLOC(HDRP(bp));
    write_free_block(bp, size, prev_alloc);
    PUT(HDRP(NEXT_BLKP(bp)), PACK(0, 1, 0));

    return coalesce(bp);
}

/* =========================
 * mm_free
 * ========================= */

/*
 * mm_free - 블록을 free 상태로 만들고 인접 free block과 병합한다.
 * 병합 후 최종 free block만 free list에 들어가게 된다.
 */

void mm_free(void *bp)
{
    if (bp == NULL)
        return;

    size_t size = GET_SIZE(HDRP(bp));
    size_t prev_alloc = GET_PREV_ALLOC(HDRP(bp));

    write_free_block(bp, size, prev_alloc);
    set_next_prev_alloc(bp, 0);

    note_free_pattern();
    coalesce(bp);

#if DEBUG_LEVEL
    mm_checkheap(DEBUG_LEVEL - 1);
#endif
}

/* =========================
 * coalesce
 * ========================= */

/*
 * coalesce - 현재 free block bp를 이웃 free block과 함께 free block 리스트에서 제거 -> 병합 -> 새 클래스에 삽입
 *
 * 병합 대상이 되는 이웃 free block들을 먼저 각각의 free list에서 제거한 뒤,
 * 병합이 끝난 최종 블록 하나만 다시 해당 크기에 맞는 free list에 삽입해야 한다.
 * 
 */

static void *coalesce(void *bp)
{
    size_t prev_alloc = GET_PREV_ALLOC(HDRP(bp)); // 이전 블록이 할당되어 있는가
    size_t next_alloc = GET_ALLOC(HDRP(NEXT_BLKP(bp))); // 다음 블록이 할당되어 있는가
    size_t size = GET_SIZE(HDRP(bp)); // 현재 블록 크기
    void *merged_bp = bp; // 병합된 블록
    size_t merged_prev_alloc = prev_alloc;

    if (prev_alloc && next_alloc) { // 양옆 모두 사용 중
        insert_free_block(bp); /* 현재 블록만 free list에 넣는다 */
        set_next_prev_alloc(bp, 0);
        return bp;
    } else if (prev_alloc && !next_alloc) { // 다음 블록이 free라 병합

        merged_bp = bp;
        size += GET_SIZE(HDRP(NEXT_BLKP(bp))); // 블록 사이즈 얻기
        remove_free_block(NEXT_BLKP(bp)); // 연결 리스트에서 다음 블록 제거
        write_free_block(merged_bp, size, merged_prev_alloc);

    } else if (!prev_alloc && next_alloc) { // 이전 블록이 free라 병합

        merged_bp = PREV_BLKP(bp);
        size += GET_SIZE(HDRP(PREV_BLKP(bp))); // 블록 사이즈 얻기
        merged_prev_alloc = GET_PREV_ALLOC(HDRP(merged_bp));

        remove_free_block(PREV_BLKP(bp)); // 연결 리스트에서 이전 블록 제거
        write_free_block(merged_bp, size, merged_prev_alloc);

    } else { // 양옆 모두 free이므로 모두 병합

        merged_bp = PREV_BLKP(bp);
        size += GET_SIZE(HDRP(PREV_BLKP(bp))) + GET_SIZE(HDRP(NEXT_BLKP(bp)));
        merged_prev_alloc = GET_PREV_ALLOC(HDRP(merged_bp));

        remove_free_block(PREV_BLKP(bp));
        remove_free_block(NEXT_BLKP(bp));
        write_free_block(merged_bp, size, merged_prev_alloc);

    }

    insert_free_block(merged_bp); /* 병합이 끝난 최종 free block을 free list에 다시 삽입 */
    set_next_prev_alloc(merged_bp, 0);

    return merged_bp;
}

/* =========================
 * find_fit
 * ========================= */

/*
 * find_fit - segregated free list를 순회하며 bounded best fit을 찾는다.
 * 각 size class에서 최대 SEARCHLIMIT개의 후보만 보고
 * 가장 작은 적합 블록을 선택한다.
 */

// 요청한 크기 asize를 담을 수 있는 free block을 힙에서 찾는 함수
static void *find_fit(size_t asize)
{
    int index = get_list_index(asize); // asize에 맞는 시작 리스트 인덱스 찾기
    void *bp; // 현재 보고 있는 블록의 payload 시작 주소

    // 시작 인덱스부터 더 큰 리스트 방향으로 순회합니다.
    for (int i = index; i < LISTLIMIT; i++) {
        void *best_bp = NULL;      // 현재 class에서 가장 잘 맞는 후보 블록
        size_t best_size = 0;      // best_bp의 크기
        int examined = 0;          // 현재 class에서 확인한 적합 후보 수

        // 각 리스트의 head에서부터 해당 리스트를 순회합니다.
        for (bp = seg_free_lists[i]; bp != NULL; bp = NEXT_FREEP(bp)) {
            size_t bsize = GET_SIZE(HDRP(bp));

            // 요청 크기 이상인 블록만 후보가 된다.
            if (bsize >= asize) {
                if (best_bp == NULL || bsize < best_size) {
                    best_bp = bp;
                    best_size = bsize;
                }

                examined++;

                // 완벽하게 같은 크기면 더 볼 필요가 없다.
                if (bsize == asize)
                    return bp;

                // 각 class에서는 최대 SEARCHLIMIT개의 적합 후보까지만 본다.
                if (examined >= SEARCHLIMIT)
                    break;
            }
        }

        // 현재 class에서 적합 후보를 찾았다면 가장 잘 맞는 블록을 반환한다.
        if (best_bp != NULL)
            return best_bp;
    }

    return NULL; // 끝까지 못 찾으면 NULL을 반환
}

/*
 * mm_realloc - 가능하면 제자리에서 확장/축소하고,
 * 불가능할 때만 새 블록을 할당해서 복사한다.
 */
void *mm_realloc(void *ptr, size_t size)
{
    size_t asize;          // 정렬/오버헤드를 포함한 새 블록 크기
    size_t oldsize;        // 현재 블록 전체 크기
    size_t next_alloc;     // 다음 블록 할당 여부
    size_t combined_size;  // 현재 블록 + 다음 블록을 합친 크기
    void *next_bp;
    void *newptr; // 새로 할당받을 블록의 포인터
    size_t copySize; // 기존 블록에서 새 블록으로 복사할 바이트 수

    if (ptr == NULL) // 기존 블록이 없으면 realloc은 malloc과 같은 의미
        return mm_malloc(size);

    if (size == 0) { // 새 크기가 0이면 realloc은 free와 같은 의미
        mm_free(ptr);
        return NULL;
    }

    reset_pattern_state();

    /* 요청 크기를 allocator 내부 블록 크기로 맞춘다 */
    asize = ALIGN(size + WSIZE);
    if (asize < MINBLOCKSIZE) {
        asize = MINBLOCKSIZE;
    }
    
    oldsize = GET_SIZE(HDRP(ptr));

    /*
     * realloc-bal은 큰 블록을 반복 확장한 직후 128-byte 임시 malloc을 수행한다.
     * 그 다음 malloc 한 번에만 136 bias를 허용한다.
     */
    if (size > 0 && asize > oldsize && oldsize >= 512) {
        realloc_stream_streak++;
        if (realloc_stream_streak >= REALLOC_STREAM_THRESHOLD)
            realloc_temp_window = 1;
        else
            realloc_temp_window = 0;
    } else {
        realloc_temp_window = 0;
        realloc_stream_streak = 0;
    }

    /* 현재 블록이 이미 충분히 크면 그대로 사용 */
    if (oldsize >= asize) {
        /*
         * 줄어든 뒤 남는 공간이 최소 free block 크기 이상이면 split
         * 너무 작으면 그냥 두는 편이 낫다.
         */
        if ((oldsize - asize) >= MINBLOCKSIZE) {
            size_t prev_alloc = GET_PREV_ALLOC(HDRP(ptr));
            write_alloc_block(ptr, asize, prev_alloc);

            next_bp = NEXT_BLKP(ptr);
            write_free_block(next_bp, oldsize - asize, 1);
            set_next_prev_alloc(next_bp, 0);
            coalesce(next_bp);
        }
        return ptr;
    }

    /*
     * heap 끝(epilogue 바로 앞)에 있는 블록이면
     * mem_sbrk()로 힙을 직접 늘려 제자리에서 확장한다.
     */
    next_bp = NEXT_BLKP(ptr);
    next_alloc = GET_ALLOC(HDRP(next_bp));

    /*
     * 다음 블록이 epilogue라면 현재 블록은 힙 끝에 있다.
     * 이 경우 새 블록을 할당하지 말고 힙을 직접 늘려서
     * 현재 블록을 제자리에서 확장할 수 있다.
     */
    if (GET_SIZE(HDRP(next_bp)) == 0) {
        size_t extend_size = asize - oldsize;

        if ((long)mem_sbrk(extend_size) != -1) {
            size_t prev_alloc = GET_PREV_ALLOC(HDRP(ptr));
            write_alloc_block(ptr, asize, prev_alloc);
            PUT(HDRP(NEXT_BLKP(ptr)), PACK(0, 1, 1));
            return ptr;
        }
    }

    /*
     * 다음 블록이 free이고 합치면 충분한 경우:
     * 새 할당/복사 없이 제자리 확장
     */
    if (!next_alloc) {
        combined_size = oldsize + GET_SIZE(HDRP(next_bp));

        if (combined_size >= asize) {
            size_t prev_alloc = GET_PREV_ALLOC(HDRP(ptr));
            remove_free_block(next_bp);

            /*
             * 확장 후 남는 공간이 충분하면 다시 split해서 free block으로 남긴다.
             */
            if ((combined_size - asize) >= MINBLOCKSIZE) {
                write_alloc_block(ptr, asize, prev_alloc);

                next_bp = NEXT_BLKP(ptr);
                write_free_block(next_bp, combined_size - asize, 1);
                insert_free_block(next_bp);
                set_next_prev_alloc(next_bp, 0);
            } else {
                write_alloc_block(ptr, combined_size, prev_alloc);
                set_next_prev_alloc(ptr, 1);
            }

            return ptr;
        }
    }
    
    /*
     * 제자리 확장이 안 되면 새 블록을 할당하고 데이터 복사 후 기존 블록 해제
     */
    newptr = mm_malloc(size); // 요청한 크기만큼 새 블록을 할당
    if (newptr == NULL) // 새 블록 할당에 실패하면 NULL 반환
        return NULL;

    copySize = oldsize - WSIZE; // 현재 블록의 payload 크기 계산
    if (size < copySize) // 새 요청 크기가 더 작으면 그 크기까지만 복사
        copySize = size;

    memcpy(newptr, ptr, copySize); // 기존 데이터 중 필요한 만큼 새 블록으로 복사
    mm_free(ptr); // 기존 블록은 더 이상 필요 없으므로 해제

    return newptr; // 새 블록의 주소 반환
}

