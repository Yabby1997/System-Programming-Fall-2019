/*
 * mm-explicit.c - an empty malloc package
 *
 * NOTE TO STUDENTS: Replace this header comment with your own header
 * comment that gives a high level description of your solution.
 *
 * @id : 201602022 
 * @name : SEUNGHUN YANG
 */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "mm.h"
#include "memlib.h"

/* If you want debugging output, use the following macro.  When you hand
 * in, remove the #define DEBUG line. */
#define DEBUG
#ifdef DEBUG
# define dbg_printf(...) printf(__VA_ARGS__)
#else
# define dbg_printf(...)
#endif


/* do not change the following! */
#ifdef DRIVER
/* create aliases for driver tests */
#define malloc mm_malloc
#define free mm_free
#define realloc mm_realloc
#define calloc mm_calloc
#endif /* def DRIVER */

/* single word (4) or double word (8) alignment */
#define ALIGNMENT 8

/* rounds up to the nearest multiple of ALIGNMENT */
#define ALIGN(p) (((size_t)(p) + (ALIGNMENT-1)) & ~0x7)

//매크로 정의 
#define WSIZE 4                                                                 //워드사이즈 4
#define DSIZE 8                                                                 //더블워드 사이즈 8
#define CHUNKSIZE (1<<12)                                                       //청크사이즈 4096
#define OVERHEAD 8                                                              //header + footer 를 위한 오버헤드 공간 8

#define MAX(x, y) ((x) > (y) ? (x) : (y))                                       //x, y중 큰 값 

#define PACK(size, alloc) ((size) | (alloc))                                    //size or alloc으로 size와 alloc값을 더한다. size 는 8배수라 하위 3비트를 안쓴다.

#define GET(p) (*(unsigned int*)(p))                                            //포인터 p의 위치의 unsigned int를 가져온다. 
#define PUT(p, val) (*(unsigned int*)(p) = (val))                               //포인터 p의 위치에 unsigned int를 쓴다.

#define GET_SIZE(p) (GET(p) & ~0x7)                                             //p의 위치에서 가져온 unsigned int는 사이즈alloc을 모두 포함한다. 1 ... 1000과 and해 사이즈만 가져옴.
#define GET_ALLOC(p) (GET(p) & 0x1)                                             //p의 위치에서 가져온 unsigned int에 0000 ... 0001을 and해 alloc정보만 가져온다. 

#define HDRP(bp) ((char*)(bp) - WSIZE)                                          //블럭 포인터 (payload의 시작주소) 부터 한 워드만큼 빼주면 헤더의 주소가 된다. 헤더는 한워드 크기다.
#define FTRP(bp) ((char*)(bp) + GET_SIZE(HDRP(bp)) - DSIZE)                     //사이즈 + bp - 오버헤드(뒷header + 현footer) 해주면 푸터의 주소가 된다. 푸터도 한워드 크기.

#define NEXT_BLKP(bp) ((char*)(bp) + GET_SIZE(HDRP(bp)))                        //현 블럭 포인터에서 사이즈만큼만큼 더하면 다음블럭의 주소
#define PREV_BLKP(bp) ((char*)(bp) - GET_SIZE(((char*)(bp) - OVERHEAD)))        //앞 블럭의 푸터에서 사이즈를 얻어내서 현블럭에서 빼면 앞블럭의 주소

#define NEXT_FREEP(bp) ((char*)(bp))											//가용 블럭의 bp가 가리키는 곳에 다음 free block의 주소정보가 저장된다.
#define PREV_FREEP(bp) ((char*)(bp) + WSIZE)									//다음 free block 주소의 다음 워드에 이전 free block의 주소정보가 저장된다.

#define SIZE_PTR(p)  ((size_t*)(((char*)(p)) - SIZE_T_SIZE))        
#define SIZE_T_SIZE (ALIGN(sizeof(size_t)))

static char *heap_listp = 0;                                            //힙리스트의 포인터 
static char *epilogue_p = 0;											//에필로그의 포인터

static void *coalesce(void *bp){
	size_t prev_alloc = GET_ALLOC(FTRP(PREV_BLKP(bp)));                 //앞블럭의 footer로부터 alloc여부를 얻어온다.
	size_t next_alloc = GET_ALLOC(HDRP(NEXT_BLKP(bp)));                 //다음블럭의 header로부터 alloc여부를 얻어온다.
	size_t size = GET_SIZE(HDRP(bp));                                   //현재블럭의 사이즈도 구한다.

	if(prev_alloc && next_alloc){                                       //앞 뒤 모두 할당되어있다면 coalesce할 수 없다. 그냥 인자로 입력된 bp 반환
		return bp;
	}

	else if(prev_alloc && !next_alloc){                                 //앞은 할당되어있지만 뒤는 가용일 경우 현재와 뒤를 합쳐줄 수 있다.
	    size += GET_SIZE(HDRP(NEXT_BLKP(bp)));                          //사이즈를 증가시킨다.
	    PUT(HDRP(bp), PACK(size, 0));                                   //현재 블럭부터 뒷블럭까지 포함시키는것이므로 현재블럭의 헤더에 새로운 사이즈를 넣어준다.
	    PUT(FTRP(bp), PACK(size, 0));                                   //footer에도 마찬가지로 넣어준다. 현재의 헤더 사이즈값이 이미 바뀌었으므로 현재 푸터의 값을 바꾸면 됨.
	    return bp;
	}

	else if(!prev_alloc && next_alloc){                                 //뒤는 할당되어있지만 앞은 가용일 경우
	    size += GET_SIZE(HDRP(PREV_BLKP(bp)));                          //사이즈를 증가시킨다. 
	    PUT(FTRP(bp), PACK(size, 0));                                   //푸터는 현재 블럭의 것을 변경해야하고
	    PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0));                        //헤더는 이전 블럭의 것을 변경해야한다. 
	    return PREV_BLKP(bp);                                             //block 의 시작점이 이전 블럭으로 변경되었으므로 bp가 가리키는곳을 이전 블럭으로 옮겨준다. 
	}

	else{                                                                           //뒤와 앞 모두 가용인 경우
	    size += GET_SIZE(HDRP(PREV_BLKP(bp))) + GET_SIZE(FTRP(NEXT_BLKP(bp)));      //사이즈를 증가시킨다.
	    PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0));                                    //헤더는 이전 블럭의 것을 변경해야하고
	    PUT(FTRP(NEXT_BLKP(bp)), PACK(size, 0));                                    //푸터는 이후 블럭의 것을 변경해야한다.
	    return PREV_BLKP(bp);                                                         //block 의 시작지점이 이전 블럭으로 변경되었으므로 bp가 가리키는곳을 이전 블럭으로 옮겨준다.
	}
	return bp;                                                                      //coalescing이 끝난 블럭의 주소를 반환해준다. 
}

static void *extend_heap(size_t words){
	char *bp;
    size_t size;

    size = (words % 2) ? (words + 1) * WSIZE : words * WSIZE;           //워드가 홀수개라면(mod연산 결과 1) 홀수개만큼 워드를 할당, 아니라면 짝수개만큼 할당한다. 

    if ((long)(bp = mem_sbrk(size)) == -1)
	   return NULL;                                                     //만약 할당에 실패하면 NULL반환 

    PUT(HDRP(bp), PACK(size, 0));                                       //free block header
    PUT(FTRP(bp), PACK(size, 0));                                       //free block footer
    PUT(HDRP(NEXT_BLKP(bp)), PACK(0, 1));                               //new header

    return coalesce(bp);                                                //가용블럭이 있다면 합쳐준다.
}

static void *find_fit(size_t asize){
	void* bp;

	for(bp = heap_listp; GET_SIZE(HDRP(bp)) > 0; bp = NEXT_BLKP(bp)){   //heap_list 첫부분부터 각 블럭 사이즈가 0보다 크다면 다음 블럭들로 옮겨가며 반복적으로 확인
		if(!GET_ALLOC(HDRP(bp)) && (asize <= GET_SIZE(HDRP(bp)))){      //현재 확인중인 블럭이 할당되어있지 않으면서 필요한 사이즈도 만족한다면
			return bp;                                                  //첫번째로 확인된 알맞은 블럭이므로 이 블럭의 포인터를 반환 
        }
    }
    return NULL;                                                        //찾지못한다면 NULL반환
}

static void place(void *bp, size_t asize){
	size_t csize = GET_SIZE(HDRP(bp));                                  //인자로 입력된 블럭의 사이즈 csize

	if((csize - asize) >= (DSIZE + OVERHEAD)){                          //csize에서 할당할 사이즈인 asize를 빼고도 한 블럭이 들어올 최소 사이즈 (오버헤드와 데이터해서 16)이 된다면
		PUT(HDRP(bp), PACK(asize, 1));                                  //bp에 asize만큼만 블럭을 새로만들어 header footer를 해주고 남은 사이즈에 새로운 free블럭을 만들어줘도 된다.
	    PUT(FTRP(bp), PACK(asize, 1));                                  //split하는 것.
	    bp = NEXT_BLKP(bp);                                             //split하기위해 방금 할당한 블럭의 다음 블럭으로 bp를 이동한다.
	    PUT(HDRP(bp), PACK(csize - asize, 0));                          //뒤의 남은 공간인 csize-asize 만큼의 공간에 새로운 가용블럭을 만들어준다 (split) 
	    PUT(FTRP(bp), PACK(csize - asize, 0));                          //할당한것과 동일하게 header와 footer를 설정해준다.
	}
	else{
	    PUT(HDRP(bp), PACK(csize, 1));                                  //만약 가용공간을 split하기에 충분한 공간이 없다면 그냥 주어진 크기로 만들어만 준다. 
	    PUT(FTRP(bp), PACK(csize, 1));
	}
}


/*
 * Initialize: return -1 on error, 0 on success.
 */
int mm_init(void) {
	if((heap_listp = mem_sbrk(DSIZE + 4 * WSIZE)) == NULL)            	//implicit 보다 prev와 next 포인터가 추가된다. 따라서 16바이트가 아니라 24바이트를 줌.
		return -1;
	
	PUT(heap_listp, NULL);												//initial next
	PUT(heap_listp + WSIZE, NULL);										//initial prev
	PUT(heap_listp + DSIZE, 0);											//initial padding
	PUT(head_listp + DSIZE + WSIZE, PACK(OVERHEAD, 1));					//prologue header
	PUT(head_listp + DSIZE + DSIZE, PACK(OVERHEAD, 1));					//prologue footer
	PUT(head_listp + DSIZE + DSIZE + WSIZE, PACK(0, 1));				//epilogue header
	heap_listp += DSIZE + DSIZE;										//bp의 위치로 이동

	if(extend_heap(CHUNKSIZE / WSIZE) == NULL)
		return -1;

    return 0;
}

/*
 * malloc
 */
void *malloc (size_t size) {
    return NULL;
}

/*
 * free
 */
void free (void *ptr) {
    if(!ptr) return;
}

/*
 * realloc - you may want to look at mm-naive.c
 */
void *realloc(void *oldptr, size_t size) {
    return NULL;
}

/*
 * calloc - you may want to look at mm-naive.c
 * This function is not tested by mdriver, but it is
 * needed to run the traces.
 */
void *calloc (size_t nmemb, size_t size) {
    return NULL;
}


/*
 * Return whether the pointer is in the heap.
 * May be useful for debugging.
 */
static int in_heap(const void *p) {
    return p < mem_heap_hi() && p >= mem_heap_lo();
}

/*
 * Return whether the pointer is aligned.
 * May be useful for debugging.
 */
static int aligned(const void *p) {
    return (size_t)ALIGN(p) == (size_t)p;
}

/*
 * mm_checkheap
 */
void mm_checkheap(int verbose) {
}
