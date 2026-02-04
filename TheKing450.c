/*
*============================================================================
*TheKing450 - WinBoard 2 Protocol with Custom PV Lines
*Cfish(Stockfish in C)-Single File Merge
*NNUE Support Structure Included
*============================================================================
*
*Based on:Cfish 220126(Stockfish port to C)
*Modified by:K7 ChessLab 2026
*Strength:~2800-3000+ ELO
*Protocol: WinBoard/XBoard v2
*PV Lines: depth*1001 (1001,2002,3003...50050) for Chessmaster
*
*COMPILATION:
*-----------
*Linux:
*gcc-O3-march=native-DNDEBUG-DIS_64BIT-DUSE_POPCNT \
*CFBrain_MERGED.c-o CFBrain-lpthread-lm
*
*Windows(ucrt64):
*clang -O3 -march=native CLSD.c -o CLSD.exe -fuse-ld=lld
*
*FEATURES:
*---------
*-Full Stockfish search algorithm
*-Classical evaluation(Material,PST,Pawns,Mobility,King Safety,etc.)
*-Transposition table
*-Move ordering(MVV-LVA,killers,history)
*-Time management
*-Multi-threading support
*-UCI protocol
*
*DISABLED:
*---------
*-NNUE(neural network)
*-Syzygy tablebases
*-Polyglot opening book
*-Benchmark command
*
*============================================================================
*/
#define NDEBUG
#define IS_64BIT
#define USE_POPCNT
#undef NNUE
#undef NNUE_PURE
#undef NNUE_EMBEDDED
#include <assert.h>
#include <ctype.h>
#include <inttypes.h>
#include <math.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifndef _WIN32
#include <pthread.h>
#include <sys/time.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#else
#include <windows.h>
#undef THREAD_RESUME
#endif
typedef int64_t TimePoint;
typedef uint64_t NodeMask;
struct PRNG{
uint64_t s;
};
typedef struct PRNG PRNG;
#ifdef _WIN32
typedef HANDLE FD;
#else
typedef int FD;
#endif
#ifdef _WIN32
typedef struct{
void*ptr;
HANDLE hMap;
}alloc_t;
#else
typedef struct{
void*ptr;
size_t size;
}alloc_t;
#endif
static void init_sliding_attacks(void);
static inline uint64_t mul_hi64(uint64_t a,uint64_t b){
#if defined(_MSC_VER) && defined(_WIN64)
return __umulh(a,b);
#elif defined(__GNUC__) && defined(__x86_64__)
unsigned __int128 product=(unsigned __int128)a*(unsigned __int128)b;
return product >> 64;
#else
uint64_t a_lo=(uint32_t)a;
uint64_t a_hi=a >> 32;
uint64_t b_lo=(uint32_t)b;
uint64_t b_hi=b >> 32;
uint64_t p0=a_lo*b_lo;
uint64_t p1=a_lo*b_hi;
uint64_t p2=a_hi*b_lo;
uint64_t p3=a_hi*b_hi;
uint32_t cy=(uint32_t)(((p0 >> 32)+(uint32_t)p1+(uint32_t)p2)>> 32);
return p3+(p1 >> 32)+(p2 >> 32)+cy;
#endif
}
#ifdef _WIN32
static inline int64_t now(void){
LARGE_INTEGER freq,count;
QueryPerformanceFrequency(&freq);
QueryPerformanceCounter(&count);
return(int64_t)(count.QuadPart*1000/freq.QuadPart);
}
#else
#include <sys/time.h>
static inline int64_t now(void){
struct timeval tv;
gettimeofday(&tv,NULL);
return(int64_t)(tv.tv_sec*1000+tv.tv_usec/1000);
}
#endif
enum{
ST_MAIN_SEARCH,ST_CAPTURES_INIT,ST_GOOD_CAPTURES,ST_KILLERS,ST_KILLERS_2,
ST_QUIET_INIT,ST_QUIET,ST_BAD_CAPTURES,
ST_EVASION,ST_EVASIONS_INIT,ST_ALL_EVASIONS,
ST_QSEARCH,ST_QCAPTURES_INIT,ST_QCAPTURES,ST_QCHECKS,
ST_PROBCUT,ST_PROBCUT_INIT,ST_PROBCUT_2
};
enum{Tempo=28};
static inline void prng_init(PRNG*rng,uint64_t seed){
rng->s=seed;
}
static inline uint64_t prng_rand(PRNG*rng){
uint64_t s=rng->s;
s^=s >> 12,s^=s << 25,s^=s >> 27;
rng->s=s;
return s*2685821657736338717ULL;
}
static inline uint64_t prng_sparse_rand(PRNG*rng){
return prng_rand(rng)&prng_rand(rng)&prng_rand(rng);
}
//
#ifndef TYPES_H
#define TYPES_H
#ifndef NDEBUG
#endif
#ifdef _WIN32
#endif
#define INLINE static inline __attribute__((always_inline))
#define NOINLINE __attribute__((noinline))
#define PURE
#if defined __has_attribute
#if __has_attribute(minsize)
#define SMALL __attribute__((minsize))
#elif __has_attribute(optimize)
#define SMALL __attribute__((optimize("Os")))
#endif
#endif
#ifndef SMALL
#define SMALL
#endif
#if defined(_WIN64) && defined(_MSC_VER) // No Makefile used
#  include <intrin.h> // Microsoft header for _BitScanForward64()
#  define IS_64BIT
#endif
#if defined(USE_POPCNT) && (defined(__INTEL_COMPILER) || defined(_MSC_VER))
#  include <nmmintrin.h> // Intel and Microsoft header for _mm_popcnt_u64()
#endif
#if !defined(NO_PREFETCH) && (defined(__INTEL_COMPILER) || defined(_MSC_VER))
#  include <xmmintrin.h> // Intel and Microsoft header for _mm_prefetch()
#endif
#if defined(USE_PEXT)
#  include <immintrin.h> // Header for _pext_u64() intrinsic
#  define pext(b, m) _pext_u64(b, m)
#else
#  define pext(b, m) (0)
#endif
#ifdef USE_POPCNT
#define HasPopCnt 1
#else
#define HasPopCnt 0
#endif
#ifdef USE_PEXT
#define HasPext 1
#else
#define HasPext 0
#endif
#ifdef IS_64BIT
#define Is64Bit 1
#else
#define Is64Bit 0
#endif
#ifdef NUMA
#define HasNuma 1
#else
#define HasNuma 0
#endif
typedef uint64_t Key;
typedef uint64_t Bitboard;
enum{MAX_MOVES=256,MAX_PLY=246};
enum{MOVE_NONE=0,MOVE_NULL=65};
enum{NORMAL,PROMOTION,ENPASSANT,CASTLING};
enum{WHITE=false,BLACK=true};
enum{KING_SIDE,QUEEN_SIDE};
enum{
NO_CASTLING=0,WHITE_OO=1,WHITE_OOO=2,
BLACK_OO=4,BLACK_OOO=8,ANY_CASTLING=15
};
INLINE int make_castling_right(int c,int s)
{
return c==WHITE?s==QUEEN_SIDE?WHITE_OOO:WHITE_OO
:s==QUEEN_SIDE?BLACK_OOO:BLACK_OO;
}
enum{PHASE_ENDGAME=0,PHASE_MIDGAME=128};
enum{MG,EG};
enum{
SCALE_FACTOR_DRAW=0,SCALE_FACTOR_NORMAL=64,
SCALE_FACTOR_MAX=128,SCALE_FACTOR_NONE=255
};
enum{BOUND_NONE,BOUND_UPPER,BOUND_LOWER,BOUND_EXACT};
enum{
VALUE_ZERO=0,VALUE_DRAW=0,
VALUE_KNOWN_WIN=10000,VALUE_MATE=32000,
VALUE_INFINITE=32001,VALUE_NONE=32002
};
#ifdef LONG_MATES
enum{MAX_MATE_PLY=600};
#else
enum{MAX_MATE_PLY=MAX_PLY};
#endif
enum{
VALUE_TB_WIN_IN_MAX_PLY=VALUE_MATE-2*MAX_PLY,
VALUE_TB_LOSS_IN_MAX_PLY=-VALUE_MATE+2*MAX_PLY,
VALUE_MATE_IN_MAX_PLY=VALUE_MATE-MAX_PLY,
VALUE_MATED_IN_MAX_PLY=-VALUE_MATE+MAX_PLY
};
enum{
PawnValueMg=126,PawnValueEg=208,
KnightValueMg=781,KnightValueEg=854,
BishopValueMg=825,BishopValueEg=915,
RookValueMg=1276,RookValueEg=1380,
QueenValueMg=2538,QueenValueEg=2682,
MidgameLimit=15258,EndgameLimit=3915
};
enum{PAWN=1,KNIGHT,BISHOP,ROOK,QUEEN,KING};
enum{
W_PAWN=1,W_KNIGHT,W_BISHOP,W_ROOK,W_QUEEN,W_KING,
B_PAWN=9,B_KNIGHT,B_BISHOP,B_ROOK,B_QUEEN,B_KING
};
enum{
DEPTH_QS_CHECKS=0,
DEPTH_QS_NO_CHECKS=-1,
DEPTH_QS_RECAPTURES=-5,
DEPTH_NONE=-6,
DEPTH_OFFSET=-7
};
enum{
SQ_A1,SQ_B1,SQ_C1,SQ_D1,SQ_E1,SQ_F1,SQ_G1,SQ_H1,
SQ_A2,SQ_B2,SQ_C2,SQ_D2,SQ_E2,SQ_F2,SQ_G2,SQ_H2,
SQ_A3,SQ_B3,SQ_C3,SQ_D3,SQ_E3,SQ_F3,SQ_G3,SQ_H3,
SQ_A4,SQ_B4,SQ_C4,SQ_D4,SQ_E4,SQ_F4,SQ_G4,SQ_H4,
SQ_A5,SQ_B5,SQ_C5,SQ_D5,SQ_E5,SQ_F5,SQ_G5,SQ_H5,
SQ_A6,SQ_B6,SQ_C6,SQ_D6,SQ_E6,SQ_F6,SQ_G6,SQ_H6,
SQ_A7,SQ_B7,SQ_C7,SQ_D7,SQ_E7,SQ_F7,SQ_G7,SQ_H7,
SQ_A8,SQ_B8,SQ_C8,SQ_D8,SQ_E8,SQ_F8,SQ_G8,SQ_H8,
SQ_NONE
};
enum{
NORTH=8,EAST=1,SOUTH=-8,WEST=-1,
NORTH_EAST=NORTH+EAST,SOUTH_EAST=SOUTH+EAST,
NORTH_WEST=NORTH+WEST,SOUTH_WEST=SOUTH+WEST,
};
enum{FILE_A,FILE_B,FILE_C,FILE_D,FILE_E,FILE_F,FILE_G,FILE_H};
enum{RANK_1,RANK_2,RANK_3,RANK_4,RANK_5,RANK_6,RANK_7,RANK_8};
typedef uint32_t Move;
typedef int32_t Phase;
typedef int32_t Value;
typedef bool Color;
typedef uint32_t Piece;
typedef uint32_t PieceType;
typedef int32_t Depth;
typedef uint32_t Square;
typedef uint32_t File;
typedef uint32_t Rank;
typedef uint32_t Score;
enum{SCORE_ZERO};
#define make_score(mg,eg) ((((unsigned)(eg))<<16) + (mg))
INLINE Value eg_value(Score s)
{
return(int16_t)((s+0x8000)>> 16);
}
INLINE Value mg_value(Score s)
{
return(int16_t)s;
}
INLINE Score score_divide(Score s,int i)
{
return make_score(mg_value(s)/i,eg_value(s)/i);
}
extern Value PieceValue[2][16];
extern uint32_t NonPawnPieceValue[16];
#define SQUARE_FLIP(sq) ((sq) ^ 0x38)
#define mate_in(ply) ((Value)(VALUE_MATE - (ply)))
#define mated_in(ply) ((Value)(-VALUE_MATE + (ply)))
#define make_square(f,r) ((Square)(((r) << 3) + (f)))
#define make_piece(c,pt) ((Piece)(((c) << 3) + (pt)))
#define type_of_p(p) ((p) & 7)
#define color_of(p) ((p) >> 3)
#define square_is_ok(s) ((Square)(s) <= SQ_H8)
#define file_of(s) ((s) & 7)
#define rank_of(s) ((s) >> 3)
#define relative_square(c,s) ((Square)((s) ^ ((c) * 56)))
#define relative_rank(c,r) ((r) ^ ((c) * 7))
#define relative_rank_s(c,s) relative_rank(c,rank_of(s))
#define pawn_push(c) ((c) == WHITE ? 8 : -8)
#define from_sq(m) ((Square)((m)>>6) & 0x3f)
#define to_sq(m) ((Square)((m) & 0x3f))
#define from_to(m) ((m) & 0xfff)
#define type_of_m(m) ((m) >> 14)
#define promotion_type(m) ((((m)>>12) & 3) + KNIGHT)
#define make_move(from,to) ((Move)((to) | ((from) << 6)))
#define reverse_move(m) (make_move(to_sq(m), from_sq(m)))
#define make_promotion(from,to,pt) ((Move)((to) | ((from)<<6) | (PROMOTION<<14) | (((pt)-KNIGHT)<<12)))
#define make_enpassant(from,to) ((Move)((to) | ((from)<<6) | (ENPASSANT<<14)))
#define make_castling(from,to) ((Move)((to) | ((from)<<6) | (CASTLING<<14)))
#define move_is_ok(m) (from_sq(m) != to_sq(m))
INLINE bool opposite_colors(Square s1,Square s2)
{
Square s=s1^s2;
return((s >> 3)^s)&1;
}
INLINE Key make_key(uint64_t seed)
{
return seed*6364136223846793005ULL+1442695040888963407ULL;
}
typedef struct Position Position;
typedef struct LimitsType LimitsType;
typedef struct RootMove RootMove;
typedef struct RootMoves RootMoves;
typedef struct PawnEntry PawnEntry;
typedef struct MaterialEntry MaterialEntry;
enum{MAX_LPH=4};
typedef Move CounterMoveStat[16][64];
typedef int16_t PieceToHistory[16][64];
typedef PieceToHistory CounterMoveHistoryStat[2][2][16][64];
typedef int16_t ButterflyHistory[2][4096];
typedef int16_t CapturePieceToHistory[16][64][8];
typedef int16_t LowPlyHistory[MAX_LPH][4096];
struct ExtMove{
Move move;
int value;
};
typedef struct ExtMove ExtMove;
struct PSQT{
Score psq[16][64];
};
extern struct PSQT psqt;
#undef max
#undef min
#define MAX(T) INLINE T max_##T(T a, T b) { return a > b ? a : b; }
MAX(int)
MAX(uint64_t)
MAX(unsigned)
MAX(int64_t)
MAX(int16_t)
MAX(uint8_t)
MAX(double)
MAX(size_t)
MAX(long)
#undef MAX
#define MIN(T) INLINE T min_##T(T a, T b) { return a < b ? a : b; }
MIN(int)
MIN(uint64_t)
MIN(unsigned)
MIN(int64_t)
MIN(int16_t)
MIN(uint8_t)
MIN(double)
MIN(size_t)
MIN(long)
#undef MIN
#define CLAMP(T) INLINE T clamp_##T(T a, T b, T c) { return a < b ? b : a > c ? c : a; }
CLAMP(int)
CLAMP(uint64_t)
CLAMP(unsigned)
CLAMP(int64_t)
CLAMP(int16_t)
CLAMP(uint8_t)
CLAMP(double)
CLAMP(size_t)
CLAMP(long)
#undef CLAMP
#ifndef __APPLE__
#define TEMPLATE(F,a,...) _Generic((a), \
int:F##_int,\
uint64_t:F##_uint64_t,\
unsigned:F##_unsigned,\
int64_t:F##_int64_t,\
int16_t:F##_int16_t,\
uint8_t:F##_uint8_t,\
double:F##_double \
)(a,__VA_ARGS__)
#else
#define TEMPLATE(F,a,...) _Generic((a), \
int:F##_int,\
uint64_t:F##_uint64_t,\
unsigned:F##_unsigned,\
int64_t:F##_int64_t,\
int16_t:F##_int16_t,\
uint8_t:F##_uint8_t,\
size_t:F##_size_t,\
long:F##_long,\
double:F##_double \
)(a,__VA_ARGS__)
#endif
#define max(a,b) TEMPLATE(max,a,b)
#define min(a,b) TEMPLATE(min,a,b)
#define clamp(a,b,c) TEMPLATE(clamp,a,b,c)
#ifdef NDEBUG
#define assume(x) do { if (!(x)) __builtin_unreachable(); } while (0)
#else
#define assume(x) assert(x)
#endif
#define likely(x) __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)
#ifdef NNUE
struct DirtyPiece{
int dirtyNum;
Piece pc[3];
Square from[3];
Square to[3];
};
typedef struct DirtyPiece DirtyPiece;
#endif
#endif
static inline int bind_thread_to_numa_node(int idx){(void)idx;return 0;}
static inline void*numa_alloc(size_t size){return malloc(size);}
static inline void numa_free(void*ptr,size_t size){(void)size;free(ptr);}
#define masks_equal(a,b) 1
#ifdef _WIN32
typedef HANDLE map_t;
#else
typedef size_t map_t;
#endif
static inline void prefetch(void*addr){(void)addr;}
static inline void prefetch2(void*addr){(void)addr;}
INLINE Bitboard attacks_bb_bishop(Square s,Bitboard occupied);
INLINE Bitboard attacks_bb_rook(Square s,Bitboard occupied);
struct Position;
typedef Value(EgFunc)(const struct Position*,Color);
#define NUM_EVAL 9
#define NUM_SCALING 6
extern EgFunc*endgame_funcs[NUM_EVAL+NUM_SCALING+6];
//
#ifndef BITBOARD_H
#define BITBOARD_H
void bitbases_init(void);
bool bitbases_probe(Square wksq,Square wpsq,Square bksq,Color us);
void bitboards_init(void);
void print_pretty(Bitboard b);
#define AllSquares (~0ULL)
#define DarkSquares  0xAA55AA55AA55AA55ULL
#define LightSquares (~DarkSquares)
#define FileABB 0x0101010101010101ULL
#define FileBBB (FileABB << 1)
#define FileCBB (FileABB << 2)
#define FileDBB (FileABB << 3)
#define FileEBB (FileABB << 4)
#define FileFBB (FileABB << 5)
#define FileGBB (FileABB << 6)
#define FileHBB (FileABB << 7)
#define Rank1BB 0xFFULL
#define Rank2BB (Rank1BB << (8 * 1))
#define Rank3BB (Rank1BB << (8 * 2))
#define Rank4BB (Rank1BB << (8 * 3))
#define Rank5BB (Rank1BB << (8 * 4))
#define Rank6BB (Rank1BB << (8 * 5))
#define Rank7BB (Rank1BB << (8 * 6))
#define Rank8BB (Rank1BB << (8 * 7))
#define QueenSide   (FileABB | FileBBB | FileCBB | FileDBB)
#define CenterFiles (FileCBB | FileDBB | FileEBB | FileFBB)
#define KingSide    (FileEBB | FileFBB | FileGBB | FileHBB)
#define Center      ((FileDBB | FileEBB) & (Rank4BB | Rank5BB))
extern uint8_t SquareDistance[64][64];
extern Bitboard SquareBB[64];
extern Bitboard FileBB[8];
extern Bitboard RankBB[8];
extern Bitboard ForwardRanksBB[2][8];
extern Bitboard BetweenBB[64][64];
extern Bitboard LineBB[64][64];
extern Bitboard DistanceRingBB[64][8];
extern Bitboard ForwardFileBB[2][64];
extern Bitboard PassedPawnSpan[2][64];
extern Bitboard PawnAttackSpan[2][64];
extern Bitboard PseudoAttacks[8][64];
extern Bitboard PawnAttacks[2][64];
INLINE __attribute__((pure))Bitboard sq_bb(Square s)
{
return SquareBB[s];
}
#if __x86_64__
INLINE Bitboard inv_sq(Bitboard b,Square s)
{
__asm__("btcq %1, %0":"+r"(b):"r"((uint64_t)s):"cc");
return b;
}
#else
INLINE Bitboard inv_sq(Bitboard b,Square s)
{
return b^sq_bb(s);
}
#endif
INLINE bool more_than_one(Bitboard b)
{
return b&(b-1);
}
INLINE Bitboard rank_bb(Rank r)
{
return RankBB[r];
}
INLINE Bitboard rank_bb_s(Square s)
{
return RankBB[rank_of(s)];
}
INLINE Bitboard file_bb(File f)
{
return FileBB[f];
}
INLINE Bitboard file_bb_s(Square s)
{
return FileBB[file_of(s)];
}
INLINE Bitboard shift_bb(int Direction,Bitboard b)
{
return Direction==NORTH?b << 8
:Direction==SOUTH?b >> 8
:Direction==NORTH+NORTH?b << 16
:Direction==SOUTH+SOUTH?b >> 16
:Direction==EAST?(b&~FileHBB)<< 1
:Direction==WEST?(b&~FileABB)>> 1
:Direction==NORTH_EAST?(b&~FileHBB)<< 9
:Direction==SOUTH_EAST?(b&~FileHBB)>> 7
:Direction==NORTH_WEST?(b&~FileABB)<< 7
:Direction==SOUTH_WEST?(b&~FileABB)>> 9
:0;
}
INLINE Bitboard pawn_attacks_bb(Bitboard b,const Color C)
{
return C==WHITE?shift_bb(NORTH_WEST,b)|shift_bb(NORTH_EAST,b)
:shift_bb(SOUTH_WEST,b)|shift_bb(SOUTH_EAST,b);
}
INLINE Bitboard pawn_double_attacks_bb(Bitboard b,const Color C)
{
return C==WHITE?shift_bb(NORTH_WEST,b)&shift_bb(NORTH_EAST,b)
:shift_bb(SOUTH_WEST,b)&shift_bb(SOUTH_EAST,b);
}
INLINE Bitboard adjacent_files_bb(unsigned f)
{
return shift_bb(EAST,FileBB[f])|shift_bb(WEST,FileBB[f]);
}
INLINE Bitboard between_bb(Square s1,Square s2)
{
return BetweenBB[s1][s2];
}
INLINE Bitboard forward_ranks_bb(Color c,unsigned r)
{
return ForwardRanksBB[c][r];
}
INLINE Bitboard forward_file_bb(Color c,Square s)
{
return ForwardFileBB[c][s];
}
INLINE Bitboard pawn_attack_span(Color c,Square s)
{
return PawnAttackSpan[c][s];
}
INLINE Bitboard passed_pawn_span(Color c,Square s)
{
return PassedPawnSpan[c][s];
}
INLINE uint64_t aligned(Move m,Square s)
{
return((Bitboard*)LineBB)[m&4095]&sq_bb(s);
}
INLINE int distance(Square x,Square y)
{
return SquareDistance[x][y];
}
INLINE unsigned distance_f(Square x,Square y)
{
unsigned f1=file_of(x),f2=file_of(y);
return f1 < f2?f2-f1:f1-f2;
}
INLINE unsigned distance_r(Square x,Square y)
{
unsigned r1=rank_of(x),r2=rank_of(y);
return r1 < r2?r2-r1:r1-r2;
}
#define attacks_bb_queen(s, occupied) (attacks_bb_bishop((s), (occupied)) | attacks_bb_rook((s), (occupied)))
#if defined(MAGIC_FANCY)
#elif defined(MAGIC_PLAIN)
#elif defined(MAGIC_BLACK)
#elif defined(BMI2_FANCY)
#elif defined(BMI2_PLAIN)
#elif defined(AVX2_BITBOARD)
#endif
INLINE Bitboard attacks_bb(int pt,Square s,Bitboard occupied)
{
assert(pt!=PAWN);
switch(pt){
case BISHOP:
return attacks_bb_bishop(s,occupied);
case ROOK:
return attacks_bb_rook(s,occupied);
case QUEEN:
return attacks_bb_queen(s,occupied);
default:
return PseudoAttacks[pt][s];
}
}
INLINE int popcount(Bitboard b)
{
#ifndef USE_POPCNT
extern uint8_t PopCnt16[1 << 16];
union{Bitboard bb;uint16_t u[4];}v={b};
return PopCnt16[v.u[0]]+PopCnt16[v.u[1]]+PopCnt16[v.u[2]]+PopCnt16[v.u[3]];
#elif defined(_MSC_VER) || defined(__INTEL_COMPILER)
return(int)_mm_popcnt_u64(b);
#else // Assumed gcc or compatible compiler
return __builtin_popcountll(b);
#endif
}
#if defined(__GNUC__)
INLINE int lsb(Bitboard b)
{
assert(b);
return __builtin_ctzll(b);
}
INLINE int msb(Bitboard b)
{
assert(b);
return 63^__builtin_clzll(b);
}
#elif defined(_MSC_VER)
#if defined(_WIN64)
INLINE Square lsb(Bitboard b)
{
assert(b);
unsigned long idx;
_BitScanForward64(&idx,b);
return(Square)idx;
}
INLINE Square msb(Bitboard b)
{
assert(b);
unsigned long idx;
_BitScanReverse64(&idx,b);
return(Square)idx;
}
#else
INLINE Square lsb(Bitboard b)
{
assert(b);
unsigned long idx;
if((uint32_t)b){
_BitScanForward(&idx,(uint32_t)b);
return idx;
}else{
_BitScanForward(&idx,(uint32_t)(b >> 32));
return idx+32;
}
}
INLINE Square msb(Bitboard b)
{
assert(b);
unsigned long idx;
if(b >> 32){
_BitScanReverse(&idx,(uint32_t)(b >> 32));
return idx+32;
}else{
_BitScanReverse(&idx,(uint32_t)b);
return idx;
}
}
#endif
#else
#error "Compiler not supported."
#endif
INLINE Square pop_lsb(Bitboard*b)
{
const Square s=lsb(*b);
*b&=*b-1;
return s;
}
INLINE Square frontmost_sq(Color c,Bitboard b)
{
return c==WHITE?msb(b):lsb(b);
}
INLINE Square backmost_sq(Color c,Bitboard b)
{
return c==WHITE?lsb(b):msb(b);
}
#endif
//
#ifndef POSITION_H
#define POSITION_H
#ifndef _WIN32
#endif
#ifdef NNUE
#endif
extern const char PieceToChar[];
extern Key matKey[16];
struct Zob{
Key psq[16][64];
Key enpassant[8];
Key castling[16];
Key side,noPawns;
};
extern struct Zob zob;
void psqt_init(void);
void zob_init(void);
struct Stack{
#ifndef NNUE_PURE
Key pawnKey;
#endif
Key materialKey;
#ifndef NNUE_PURE
Score psq;
#endif
union{
uint16_t nonPawnMaterial[2];
uint32_t nonPawn;
};
union{
struct{
uint8_t pliesFromNull;
uint8_t rule50;
};
uint16_t plyCounters;
};
uint8_t castlingRights;
uint8_t capturedPiece;
uint8_t epSquare;
Key key;
Bitboard checkersBB;
Move*pv;
PieceToHistory*history;
Move currentMove;
Move excludedMove;
Move killers[2];
Value staticEval;
Value statScore;
int moveCount;
bool ttPv;
bool ttHit;
uint8_t ply;
uint8_t stage;
uint8_t recaptureSquare;
uint8_t mp_ply;
Move countermove;
Depth depth;
Move ttMove;
Value threshold;
Move mpKillers[2];
ExtMove*cur,*endMoves,*endBadCaptures;
Bitboard blockersForKing[2];
union{
struct{
Bitboard pinnersForKing[2];
};
struct{
Bitboard dummy;
Bitboard checkSquares[7];
};
};
Square ksq;
#ifdef NNUE
Accumulator accumulator;
DirtyPiece dirtyPiece;
#endif
};
typedef struct Stack Stack;
#define StateCopySize offsetof(Stack, capturedPiece)
#define StateSize offsetof(Stack, pv)
#define SStackBegin(st) (&st.pv)
#define SStackSize (offsetof(Stack, countermove) - offsetof(Stack, pv))
struct Position{
Stack*st;
Bitboard byTypeBB[7];
Bitboard byColorBB[2];
Color sideToMove;
uint8_t chess960;
uint8_t board[64];
uint8_t pieceCount[16];
uint8_t castlingRightsMask[64];
uint8_t castlingRookSquare[16];
Bitboard castlingPath[16];
Key rootKeyFlip;
uint16_t gamePly;
bool hasRepeated;
ExtMove*moveList;
RootMoves*rootMoves;
Stack*stack;
uint64_t nodes;
uint64_t tbHits;
uint64_t ttHitAverage;
int pvIdx,pvLast;
int selDepth,nmpMinPly;
Color nmpColor;
Depth rootDepth;
Depth completedDepth;
Score contempt;
int failedHighCnt;
CounterMoveStat*counterMoves;
ButterflyHistory*mainHistory;
LowPlyHistory*lowPlyHistory;
CapturePieceToHistory*captureHistory;
PawnEntry*pawnTable;
MaterialEntry*materialTable;
CounterMoveHistoryStat*counterMoveHistory;
uint64_t bestMoveChanges;
atomic_bool resetCalls;
int callsCnt;
int action;
int threadIdx;
#ifndef _WIN32
pthread_t nativeThread;
pthread_mutex_t mutex;
pthread_cond_t sleepCondition;
#else
HANDLE nativeThread;
HANDLE startEvent,stopEvent;
#endif
void*stackAllocation;
};
void pos_set(Position*pos,char*fen,int isChess960);
void pos_fen(const Position*pos,char*fen);
void print_pos(Position*pos);
PURE Bitboard slider_blockers(const Position*pos,Bitboard sliders,Square s,
Bitboard*pinners);
PURE bool is_legal(const Position*pos,Move m);
PURE bool is_pseudo_legal(const Position*pos,Move m);
PURE bool gives_check_special(const Position*pos,Stack*st,Move m);
void do_move(Position*pos,Move m,int givesCheck);
void undo_move(Position*pos,Move m);
void do_null_move(Position*pos);
INLINE void undo_null_move(Position*pos);
PURE bool see_test(const Position*pos,Move m,int value);
PURE Key key_after(const Position*pos,Move m);
PURE bool is_draw(const Position*pos);
PURE bool has_game_cycle(const Position*pos,int ply);
#define pieces() (pos->byTypeBB[0])
#define pieces_p(p) (pos->byTypeBB[p])
#define pieces_pp(p1,p2) (pos->byTypeBB[p1] | pos->byTypeBB[p2])
#define pieces_c(c) (pos->byColorBB[c])
#define pieces_cp(c,p) (pieces_p(p) & pieces_c(c))
#define pieces_cpp(c,p1,p2) (pieces_pp(p1,p2) & pieces_c(c))
#define piece_on(s) (pos->board[s])
#define ep_square() (pos->st->epSquare)
#define is_empty(s) (!piece_on(s))
#define piece_count(c,p) (pos->pieceCount[make_piece(c,p)])
#define square_of(c,p) lsb(pieces_cp(c,p))
#define loop_through_pieces(c,p,s) \
for(Bitboard bb_pieces=pieces_cp(c,p);\
bb_pieces&&(s=pop_lsb(&bb_pieces),true);)
#define piece_count_mk(c, p) (((material_key()) >> (20 * (c) + 4 * (p) + 4)) & 15)
#define can_castle_cr(cr) (pos->st->castlingRights & (cr))
#define can_castle_c(c) can_castle_cr((WHITE_OO | WHITE_OOO) << (2 * (c)))
#define can_castle_any() (pos->st->castlingRights)
#define castling_impeded(cr) (pieces() & pos->castlingPath[cr])
#define castling_rook_square(cr) (pos->castlingRookSquare[cr])
#define checkers() (pos->st->checkersBB)
#define attackers_to(s) attackers_to_occ(pos,s,pieces())
#define attacks_from_pawn(s,c) (PawnAttacks[c][s])
#define attacks_from_knight(s) (PseudoAttacks[KNIGHT][s])
#define attacks_from_bishop(s) attacks_bb_bishop(s, pieces())
#define attacks_from_rook(s) attacks_bb_rook(s, pieces())
#define attacks_from_queen(s) (attacks_from_bishop(s)|attacks_from_rook(s))
#define attacks_from_king(s) (PseudoAttacks[KING][s])
#define attacks_from(pc,s) attacks_bb(pc,s,pieces())
#define moved_piece(m) (piece_on(from_sq(m)))
#define captured_piece() (pos->st->capturedPiece)
#define raw_key() (pos->st->key)
#define key() (pos->st->rule50 < 14 ? pos->st->key : pos->st->key ^ make_key((pos->st->rule50 - 14) / 8))
#define material_key() (pos->st->materialKey)
#define pawn_key() (pos->st->pawnKey)
#define stm() (pos->sideToMove)
#define game_ply() (pos->gamePly)
#define is_chess960() (pos->chess960)
#define nodes_searched() (pos->nodes)
#define rule50_count() (pos->st->rule50)
#define psq_score() (pos->st->psq)
#define non_pawn_material_c(c) (pos->st->nonPawnMaterial[c])
#define non_pawn_material() (non_pawn_material_c(WHITE) + non_pawn_material_c(BLACK))
#define pawns_only() (!pos->st->nonPawn)
INLINE Bitboard blockers_for_king(const Position*pos,Color c)
{
return pos->st->blockersForKing[c];
}
INLINE bool pawn_passed(const Position*pos,Color c,Square s)
{
return!(pieces_cp(!c,PAWN)&passed_pawn_span(c,s));
}
INLINE bool opposite_bishops(const Position*pos)
{
return piece_count(WHITE,BISHOP)==1
&&piece_count(BLACK,BISHOP)==1
&&(pieces_p(BISHOP)&DarkSquares)
&&(pieces_p(BISHOP)&~DarkSquares);
}
INLINE bool is_capture_or_promotion(const Position*pos,Move m)
{
assert(move_is_ok(m));
return type_of_m(m)!=NORMAL?type_of_m(m)!=CASTLING:!is_empty(to_sq(m));
}
INLINE bool is_capture(const Position*pos,Move m)
{
assert(move_is_ok(m));
return(!is_empty(to_sq(m))&&type_of_m(m)!=CASTLING)||type_of_m(m)==ENPASSANT;
}
INLINE bool gives_check(const Position*pos,Stack*st,Move m)
{
return type_of_m(m)==NORMAL&&!(blockers_for_king(pos,!stm())&pieces_c(stm()))
?(bool)(st->checkSquares[type_of_p(moved_piece(m))]&sq_bb(to_sq(m)))
:gives_check_special(pos,st,m);
}
void pos_set_check_info(Position*pos);
INLINE void undo_null_move(Position*pos)
{
assert(!checkers());
pos->st--;
pos->sideToMove=!pos->sideToMove;
}
#if 0
INLINE Bitboard slider_blockers(const Position*pos,Bitboard sliders,Square s,
Bitboard*pinners)
{
Bitboard result=0,snipers;
*pinners=0;
snipers=((PseudoAttacks[ROOK ][s]&pieces_pp(QUEEN,ROOK))
|(PseudoAttacks[BISHOP][s]&pieces_pp(QUEEN,BISHOP)))&sliders;
while(snipers){
Square sniperSq=pop_lsb(&snipers);
Bitboard b=between_bb(s,sniperSq)&pieces();
if(!more_than_one(b)){
result|=b;
if(b&pieces_c(color_of(piece_on(s))))
*pinners|=sq_bb(sniperSq);
}
}
return result;
}
#endif
INLINE Bitboard attackers_to_occ(const Position*pos,Square s,
Bitboard occupied)
{
return(attacks_from_pawn(s,BLACK)&pieces_cp(WHITE,PAWN))
|(attacks_from_pawn(s,WHITE)&pieces_cp(BLACK,PAWN))
|(attacks_from_knight(s)&pieces_p(KNIGHT))
|(attacks_bb_rook(s,occupied)&pieces_pp(ROOK,QUEEN))
|(attacks_bb_bishop(s,occupied)&pieces_pp(BISHOP,QUEEN))
|(attacks_from_king(s)&pieces_p(KING));
}
#endif
//
#ifndef MOVEGEN_H
#define MOVEGEN_H
#define GEN_CAPTURES     0
#define GEN_QUIETS       1
#define GEN_QUIET_CHECKS 2
#define GEN_EVASIONS     3
#define GEN_NON_EVASIONS 4
#define GEN_LEGAL        5
ExtMove*generate_captures(const Position*pos,ExtMove*list);
ExtMove*generate_quiets(const Position*pos,ExtMove*list);
ExtMove*generate_quiet_checks(const Position*pos,ExtMove*list);
ExtMove*generate_evasions(const Position*pos,ExtMove*list);
ExtMove*generate_non_evasions(const Position*pos,ExtMove*list);
ExtMove*generate_legal(const Position*pos,ExtMove*list);
#endif
//
#ifndef SEARCH_H
#define SEARCH_H
struct RootMove{
int pvSize;
Value score;
Value previousScore;
int selDepth;
int tbRank;
Value tbScore;
Move pv[MAX_PLY];
};
typedef struct RootMove RootMove;
struct RootMoves{
int size;
RootMove move[MAX_MOVES];
};
typedef struct RootMoves RootMoves;
struct LimitsType{
int time[2];
int inc[2];
int npmsec;
int movestogo;
int depth;
int movetime;
int mate;
bool infinite;
uint64_t nodes;
TimePoint startTime;
int numSearchmoves;
Move searchmoves[MAX_MOVES];
};
typedef struct LimitsType LimitsType;
extern LimitsType Limits;
INLINE int use_time_management(void)
{
return Limits.time[WHITE]||Limits.time[BLACK];
}
void search_init(void);
void search_clear(void);
uint64_t perft(Position*pos,Depth depth);
void start_thinking(Position*pos,bool ponderMode);
#endif
//
#ifndef THREAD_H
#define THREAD_H
#ifndef _WIN32
#else
#endif
#define MAX_THREADS 512
#ifndef _WIN32
#define LOCK_T pthread_mutex_t
#define LOCK_INIT(x) pthread_mutex_init(&(x), NULL)
#define LOCK_DESTROY(x) pthread_mutex_destroy(&(x))
#define LOCK(x) pthread_mutex_lock(&(x))
#define UNLOCK(x) pthread_mutex_unlock(&(x))
#else
#define LOCK_T HANDLE
#define LOCK_INIT(x) do { x = CreateMutex(NULL, FALSE, NULL); } while (0)
#define LOCK_DESTROY(x) CloseHandle(x)
#define LOCK(x) WaitForSingleObject(x, INFINITE)
#define UNLOCK(x) ReleaseMutex(x)
#endif
enum{
THREAD_SLEEP,THREAD_SEARCH,THREAD_TT_CLEAR,THREAD_EXIT,THREAD_RESUME
};
void thread_search(Position*pos);
void thread_wake_up(Position*pos,int action);
void thread_wait_until_sleeping(Position*pos);
void thread_wait(Position*pos,atomic_bool*b);
struct MainThread{
double previousTimeReduction;
Value previousScore;
Value iterValue[4];
};
typedef struct MainThread MainThread;
extern MainThread mainThread;
void mainthread_search(void);
struct ThreadPool{
Position*pos[MAX_THREADS];
int numThreads;
#ifndef _WIN32
pthread_mutex_t mutex;
pthread_cond_t sleepCondition;
bool initializing;
#else
HANDLE event;
#endif
bool searching,sleeping,stopOnPonderhit;
atomic_bool ponder,stop,increaseDepth;
LOCK_T lock;
};
typedef struct ThreadPool ThreadPool;
void threads_init(void);
void threads_exit(void);
void threads_start_thinking(Position*pos,LimitsType*);
void threads_set_number(int num);
uint64_t threads_nodes_searched(void);
uint64_t threads_tb_hits(void);
extern ThreadPool Threads;
INLINE Position*threads_main(void)
{
return Threads.pos[0];
}
extern CounterMoveHistoryStat**cmhTables;
extern int numCmhTables;
#endif
//
#ifndef TT_H
#define TT_H
struct TTEntry{
uint16_t key16;
uint8_t depth8;
uint8_t genBound8;
uint16_t move16;
int16_t value16;
int16_t eval16;
};
typedef struct TTEntry TTEntry;
enum{CacheLineSize=64,ClusterSize=3};
struct Cluster{
TTEntry entry[ClusterSize];
char padding[2];
};
typedef struct Cluster Cluster;
struct TranspositionTable{
size_t clusterCount;
Cluster*table;
alloc_t alloc;
uint8_t generation8;
};
typedef struct TranspositionTable TranspositionTable;
extern TranspositionTable TT;
INLINE void tte_save(TTEntry*tte,Key k,Value v,bool pv,int b,Depth d,
Move m,Value ev)
{
if(m||(uint16_t)k!=tte->key16)
tte->move16=(uint16_t)m;
if((uint16_t)k!=tte->key16
||d-DEPTH_OFFSET > tte->depth8-4
||b==BOUND_EXACT)
{
assert(d > DEPTH_OFFSET&&d < 256+DEPTH_OFFSET);
tte->key16=(uint16_t)k;
tte->depth8=(uint8_t)(d-DEPTH_OFFSET);
tte->genBound8=(uint8_t)(TT.generation8|((uint8_t)pv << 2)|b);
tte->value16=(int16_t)v;
tte->eval16=(int16_t)ev;
}
}
INLINE Move tte_move(TTEntry*tte)
{
return tte->move16;
}
INLINE Value tte_value(TTEntry*tte)
{
return tte->value16;
}
INLINE Value tte_eval(TTEntry*tte)
{
return tte->eval16;
}
INLINE Depth tte_depth(TTEntry*tte)
{
return tte->depth8+DEPTH_OFFSET;
}
INLINE bool tte_is_pv(TTEntry*tte)
{
return tte->genBound8&0x4;
}
INLINE int tte_bound(TTEntry*tte)
{
return tte->genBound8&0x3;
}
void tt_free(void);
INLINE void tt_new_search(void)
{
TT.generation8+=8;
}
INLINE TTEntry*tt_first_entry(Key key)
{
return&TT.table[mul_hi64(key,TT.clusterCount)].entry[0];
}
TTEntry*tt_probe(Key key,bool*found);
int tt_hashfull(void);
void tt_allocate(size_t mbSize);
void tt_clear(void);
void tt_clear_worker(int idx);
#endif
//
#ifndef TIMEMAN_H
#define TIMEMAN_H
struct TimeManagement{
TimePoint startTime;
int optimumTime;
int maximumTime;
int64_t availableNodes;
int tempoNNUE;
};
extern struct TimeManagement Time;
void time_init(Color us,int ply);
#define time_optimum() Time.optimumTime
#define time_maximum() Time.maximumTime
INLINE TimePoint time_elapsed(void)
{
return Limits.npmsec?(int64_t)threads_nodes_searched()
:now()-Time.startTime;
}
#endif
//
#ifndef UCI_H
#define UCI_H
struct Option;
typedef struct Option Option;
typedef void(*OnChange)(Option*);
enum{
OPT_TYPE_CHECK,OPT_TYPE_SPIN,OPT_TYPE_BUTTON,OPT_TYPE_STRING,
OPT_TYPE_COMBO,OPT_TYPE_DISABLED
};
enum{
OPT_CONTEMPT,
OPT_ANALYSIS_CONTEMPT,
OPT_THREADS,
OPT_HASH,
OPT_CLEAR_HASH,
OPT_PONDER,
OPT_MULTI_PV,
OPT_SKILL_LEVEL,
OPT_MOVE_OVERHEAD,
OPT_SLOW_MOVER,
OPT_NODES_TIME,
OPT_ANALYSE_MODE,
OPT_CHESS960,
OPT_SYZ_PATH,
OPT_SYZ_PROBE_DEPTH,
OPT_SYZ_50_MOVE,
OPT_SYZ_PROBE_LIMIT,
OPT_SYZ_USE_DTM,
OPT_BOOK_FILE,
OPT_BOOK_FILE2,
OPT_BOOK_BEST_MOVE,
OPT_BOOK_DEPTH,
#ifdef NNUE
OPT_EVAL_FILE,
#ifndef NNUE_PURE
OPT_USE_NNUE,
#endif
#endif
OPT_LARGE_PAGES,
OPT_NUMA
};
struct Option{
char*name;
int type;
int def,minVal,maxVal;
char*defString;
OnChange onChange;
int value;
char*valString;
};
void options_init(void);
void options_free(void);
void print_options(void);
int option_value(int opt);
const char*option_string_value(int opt);
const char*option_default_string_value(int opt);
void option_set_value(int opt,int value);
bool option_set_by_name(char*name,char*value);
void setoption(char*str);
void position(Position*pos,char*str);
void uci_loop(int argc,char*argv[]);
char*uci_value(char*str,Value v);
char*uci_square(char*str,Square s);
char*uci_move(char*str,Move m,int chess960);
void print_pv(Position*pos,Depth depth,Value alpha,Value beta);
Move uci_to_move(const Position*pos,char*str);
#endif
//
#ifndef MATERIAL_H
#define MATERIAL_H
struct MaterialEntry{
Key key;
int16_t gamePhase;
Score score;
uint8_t eval_func;
uint8_t eval_func_side;
uint8_t scal_func[2];
uint8_t factor[2];
};
typedef struct MaterialEntry MaterialEntry;
typedef MaterialEntry MaterialTable[8192];
void material_entry_fill(const Position*pos,MaterialEntry*e,Key key);
INLINE MaterialEntry*material_probe(const Position*pos)
{
Key key=material_key();
MaterialEntry*e=&pos->materialTable[key >>(64-13)];
if(unlikely(e->key!=key))
material_entry_fill(pos,e,key);
return e;
}
INLINE Score material_imbalance(MaterialEntry*me)
{
return me->score;
}
INLINE bool material_specialized_eval_exists(MaterialEntry*me)
{
return me->eval_func!=0;
}
INLINE Value material_evaluate(MaterialEntry*me,const Position*pos)
{
return endgame_funcs[me->eval_func](pos,me->eval_func_side);
}
INLINE int material_scale_factor(MaterialEntry*me,const Position*pos,
Color c)
{
int sf=SCALE_FACTOR_NONE;
if(me->scal_func[c])
sf=endgame_funcs[me->scal_func[c]](pos,c);
return sf!=SCALE_FACTOR_NONE?sf:me->factor[c];
}
#endif
//
#ifndef PAWNS_H
#define PAWNS_H
#ifndef NNUE_PURE
#define PAWN_ENTRIES 16384
struct PawnEntry{
Key key;
Bitboard passedPawns[2];
Bitboard pawnAttacks[2];
Bitboard pawnAttacksSpan[2];
Score kingSafety[2];
Score score;
uint8_t kingSquares[2];
uint8_t castlingRights[2];
uint8_t semiopenFiles[2];
uint8_t pawnsOnSquares[2][2];
uint8_t blockedCount;
uint8_t passedCount;
uint8_t openFiles;
};
typedef struct PawnEntry PawnEntry;
typedef PawnEntry PawnTable[PAWN_ENTRIES];
Score do_king_safety_white(PawnEntry*pe,const Position*pos,Square ksq);
Score do_king_safety_black(PawnEntry*pe,const Position*pos,Square ksq);
Value shelter_storm_white(const Position*pos,Square ksq);
Value shelter_storm_black(const Position*pos,Square ksq);
void pawn_entry_fill(const Position*pos,PawnEntry*e,Key k);
INLINE PawnEntry*pawn_probe(const Position*pos)
{
Key key=pawn_key();
PawnEntry*e=&pos->pawnTable[key&(PAWN_ENTRIES-1)];
if(unlikely(e->key!=key))
pawn_entry_fill(pos,e,key);
return e;
}
INLINE bool is_on_semiopen_file(const PawnEntry*pe,Color c,Square s)
{
return pe->semiopenFiles[c]&(1 << file_of(s));
}
INLINE int pawns_on_same_color_squares(PawnEntry*pe,Color c,Square s)
{
return pe->pawnsOnSquares[c][!!(DarkSquares&sq_bb(s))];
}
INLINE Score king_safety_white(PawnEntry*pe,const Position*pos,Square ksq)
{
if(pe->kingSquares[WHITE]==ksq
&&pe->castlingRights[WHITE]==can_castle_c(WHITE))
return pe->kingSafety[WHITE];
else
return pe->kingSafety[WHITE]=do_king_safety_white(pe,pos,ksq);
}
INLINE Score king_safety_black(PawnEntry*pe,const Position*pos,Square ksq)
{
if(pe->kingSquares[BLACK]==ksq
&&pe->castlingRights[BLACK]==can_castle_c(BLACK))
return pe->kingSafety[BLACK];
else
return pe->kingSafety[BLACK]=do_king_safety_black(pe,pos,ksq);
}
#endif
#endif
//
#ifndef ENDGAME_H
#define ENDGAME_H
typedef Value(EgFunc)(const Position*,Color);
#define NUM_EVAL 9
#define NUM_SCALING 6
extern EgFunc*endgame_funcs[NUM_EVAL+NUM_SCALING+6];
extern Key endgame_keys[NUM_EVAL+NUM_SCALING][2];
void endgames_init(void);
#endif
#ifndef SETTINGS_H
#define SETTINGS_H
struct settings{
NodeMask mask;
size_t ttSize;
size_t numThreads;
bool numaEnabled;
bool largePages;
bool clear;
};
extern struct settings settings,delayedSettings;
void process_delayed_settings(void);
#endif
int TB_MaxCardinality=0;
int TB_MaxCardinalityDTM=0;
void TB_init(const char*path){(void)path;}
void TB_release(void){}
void TB_free(void){}
int TB_probe_wdl(void*pos,int*success){(void)pos;*success=0;return 0;}
int TB_probe_dtz(void*pos,int*success){(void)pos;*success=0;return 0;}
int TB_probe_dtm(void*pos,int wdl,int*success){(void)pos;(void)wdl;*success=0;return 0;}
int TB_root_probe_wdl(void*pos,void*rm){(void)pos;(void)rm;return 0;}
int TB_root_probe_dtz(void*pos,void*rm){(void)pos;(void)rm;return 0;}
int TB_root_probe_dtm(void*pos,void*rm){(void)pos;(void)rm;return 0;}
void TB_expand_mate(void*pos,void*rm){(void)pos;(void)rm;}
void*polybook=NULL;
void*polybook2=NULL;
void pb_init(void**book,const char*filename){(void)book;(void)filename;}
void pb_set_book_depth(int depth){(void)depth;}
void pb_set_best_book_move(int value){(void)value;}
void pb_free(void){}
unsigned pb_probe(void*book,void*pos){(void)book;(void)pos;return 0;}
void benchmark(Position*pos,char*str){(void)pos;(void)str;}
void nnue_export_net(void){}
#define stats_clear(s) memset(s, 0, sizeof(*s))
static const int CounterMovePruneThreshold=0;
INLINE void cms_update(PieceToHistory cms,Piece pc,Square to,int v)
{
cms[pc][to]+=v-cms[pc][to]*abs(v)/29952;
}
INLINE void history_update(ButterflyHistory history,Color c,Move m,int v)
{
m&=4095;
history[c][m]+=v-history[c][m]*abs(v)/13365;
}
INLINE void cpth_update(CapturePieceToHistory history,Piece pc,Square to,
int captured,int v)
{
history[pc][to][captured]+=v-history[pc][to][captured]*abs(v)/10692;
}
INLINE void lph_update(LowPlyHistory history,int ply,Move m,int v)
{
m&=4095;
history[ply][m]+=v-history[ply][m]*abs(v)/10692;
}
INLINE void mp_init(const Position*pos,Move ttm,Depth d,int ply)
{
assert(d > 0);
Stack*st=pos->st;
st->depth=d;
st->mp_ply=ply;
Square prevSq=to_sq((st-1)->currentMove);
st->countermove=(*pos->counterMoves)[piece_on(prevSq)][prevSq];
st->mpKillers[0]=st->killers[0];
st->mpKillers[1]=st->killers[1];
st->ttMove=ttm;
st->stage=checkers()?ST_EVASION:ST_MAIN_SEARCH;
if(!ttm||!is_pseudo_legal(pos,ttm))
st->stage++;
}
INLINE void mp_init_q(const Position*pos,Move ttm,Depth d,Square s)
{
assert(d <=0);
Stack*st=pos->st;
st->ttMove=ttm;
st->stage=checkers()?ST_EVASION:ST_QSEARCH;
if(!(ttm
&&(checkers()||d > DEPTH_QS_RECAPTURES||to_sq(ttm)==s)
&&is_pseudo_legal(pos,ttm)))
st->stage++;
st->depth=d;
st->recaptureSquare=s;
}
INLINE void mp_init_pc(const Position*pos,Move ttm,Value th)
{
assert(!checkers());
Stack*st=pos->st;
st->threshold=th;
st->ttMove=ttm;
st->stage=ST_PROBCUT;
if(!(ttm&&is_pseudo_legal(pos,ttm)&&is_capture(pos,ttm)
&&see_test(pos,ttm,th)))
st->stage++;
}
enum{MAX_INDEX_BB=2*24*64*64};
static uint32_t KPKBitbase[MAX_INDEX_BB/32];
static unsigned bb_index(unsigned us,Square bksq,Square wksq,Square psq)
{
return wksq|(bksq << 6)|(us << 12)|(file_of(psq)<< 13)|((RANK_7-rank_of(psq))<< 15);
}
enum{RES_INVALID=0,RES_UNKNOWN=1,RES_DRAW=2,RES_WIN=4};
bool bitbases_probe(Square wksq,Square wpsq,Square bksq,Color us)
{
assert(file_of(wpsq)<=FILE_D);
unsigned idx=bb_index(us,bksq,wksq,wpsq);
return KPKBitbase[idx/32]&(1U <<(idx&0x1F));
}
static uint8_t bb_initial(unsigned idx)
{
int ksq[2]={(idx >> 0)&0x3f,(idx >> 6)&0x3f};
Color us=(idx >> 12)&0x01;
int psq=make_square((idx >> 13)&0x03,RANK_7-((idx >> 15)&0x07));
if(distance(ksq[WHITE],ksq[BLACK])<=1
||ksq[WHITE]==psq
||ksq[BLACK]==psq
||(us==WHITE&&(PawnAttacks[WHITE][psq]&sq_bb(ksq[BLACK]))))
return RES_INVALID;
if(us==WHITE
&&rank_of(psq)==RANK_7
&&ksq[us]!=psq+NORTH
&&(distance(ksq[!us],psq+NORTH)> 1
||(PseudoAttacks[KING][ksq[us]]&sq_bb((psq+NORTH)))))
return RES_WIN;
if(us==BLACK
&&(!(PseudoAttacks[KING][ksq[us]]&~(PseudoAttacks[KING][ksq[!us]]|PawnAttacks[!us][psq]))
||(PseudoAttacks[KING][ksq[us]]&sq_bb(psq)&~PseudoAttacks[KING][ksq[!us]])))
return RES_DRAW;
return RES_UNKNOWN;
}
static uint8_t bb_classify(uint8_t*db,unsigned idx)
{
int ksq[2]={(idx >> 0)&0x3f,(idx >> 6)&0x3f};
Color us=(idx >> 12)&0x01;
int psq=make_square((idx >> 13)&0x03,RANK_7-((idx >> 15)&0x07));
Color them=!us;
int good=(us==WHITE?RES_WIN:RES_DRAW);
int bad=(us==WHITE?RES_DRAW:RES_WIN);
uint8_t r=RES_INVALID;
Bitboard b=PseudoAttacks[KING][ksq[us]];
while(b)
r|=us==WHITE?db[bb_index(them,ksq[them],pop_lsb(&b),psq)]
:db[bb_index(them,pop_lsb(&b),ksq[them],psq)];
if(us==WHITE){
if(rank_of(psq)< RANK_7)
r|=db[bb_index(them,ksq[them],ksq[us],psq+NORTH)];
if(rank_of(psq)==RANK_2
&&psq+NORTH!=ksq[us]
&&psq+NORTH!=ksq[them])
r|=db[bb_index(them,ksq[them],ksq[us],psq+NORTH+NORTH)];
}
return db[idx]=r&good?good:r&RES_UNKNOWN?RES_UNKNOWN:bad;
}
void bitbases_init()
{
uint8_t*db=malloc(MAX_INDEX_BB);
unsigned idx,repeat=1;
for(idx=0;idx < MAX_INDEX_BB;idx++)
db[idx]=bb_initial(idx);
while(repeat)
for(repeat=idx=0;idx < MAX_INDEX_BB;idx++)
repeat|=(db[idx]==RES_UNKNOWN&&bb_classify(db,idx)!=RES_UNKNOWN);
for(idx=0;idx < MAX_INDEX_BB;++idx)
if(db[idx]==RES_WIN)
KPKBitbase[idx/32]|=1UL <<(idx&0x1F);
free(db);
}
//
#ifndef USE_POPCNT
uint8_t PopCnt16[1 << 16];
#endif
uint8_t SquareDistance[64][64];
#ifndef AVX2_BITBOARD
static int RookDirs[]={NORTH,EAST,SOUTH,WEST};
static int BishopDirs[]={NORTH_EAST,SOUTH_EAST,SOUTH_WEST,NORTH_WEST};
static Bitboard sliding_attack(int dirs[],Square sq,Bitboard occupied)
{
Bitboard attack=0;
for(int i=0;i < 4;i++)
for(Square s=sq+dirs[i];
square_is_ok(s)&&distance(s,s-dirs[i])==1;s+=dirs[i])
{
attack|=sq_bb(s);
if(occupied&sq_bb(s))
break;
}
return attack;
}
#endif
#if defined(MAGIC_FANCY)
#elif defined(MAGIC_PLAIN)
#elif defined(MAGIC_BLACK)
#elif defined(BMI2_FANCY)
#elif defined(BMI2_PLAIN)
#elif defined(AVX2_BITBOARD)
#endif
Bitboard SquareBB[64];
Bitboard FileBB[8];
Bitboard RankBB[8];
Bitboard ForwardRanksBB[2][8];
Bitboard BetweenBB[64][64];
Bitboard LineBB[64][64];
Bitboard DistanceRingBB[64][8];
Bitboard ForwardFileBB[2][64];
Bitboard PassedPawnSpan[2][64];
Bitboard PawnAttackSpan[2][64];
Bitboard PseudoAttacks[8][64];
Bitboard PawnAttacks[2][64];
#ifndef PEDANTIC
Bitboard EPMask[16];
Bitboard CastlingPath[64];
int CastlingRightsMask[64];
Square CastlingRookSquare[16];
Key CastlingHash[16];
Bitboard CastlingBits[16];
Score CastlingPSQ[16];
Square CastlingRookFrom[16];
Square CastlingRookTo[16];
#endif
#ifndef USE_POPCNT
INLINE unsigned popcount16(unsigned u)
{
u-=(u >> 1)&0x5555U;
u=((u >> 2)&0x3333U)+(u&0x3333U);
u=((u >> 4)+u)&0x0F0FU;
return(u*0x0101U)>> 8;
}
#endif
void print_pretty(Bitboard b)
{
printf("+---+---+---+---+---+---+---+---+\n");
for(int r=7;r >=0;r--){
for(int f=0;f <=7;f++)
printf((b&sq_bb(8*r+f))?"| X ":"|   ");
printf("| %d\n+---+---+---+---+---+---+---+---+\n",1+r);
}
printf("  a   b   c   d   e   f   g   h\n");
}
void bitboards_init(void)
{
#ifndef USE_POPCNT
for(unsigned i=0;i <(1 << 16);++i)
PopCnt16[i]=popcount16(i);
#endif
for(Square s=0;s < 64;s++)
SquareBB[s]=1ULL << s;
for(int f=0;f < 8;f++)
FileBB[f]=f > FILE_A?FileBB[f-1] << 1:FileABB;
for(int r=0;r < 8;r++)
RankBB[r]=r > RANK_1?RankBB[r-1] << 8:Rank1BB;
for(int r=0;r < 7;r++)
ForwardRanksBB[WHITE][r]=~(ForwardRanksBB[BLACK][r+1]=ForwardRanksBB[BLACK][r]|RankBB[r]);
for(int c=0;c < 2;c++)
for(Square s=0;s < 64;s++){
ForwardFileBB[c][s]=ForwardRanksBB[c][rank_of(s)]&FileBB[file_of(s)];
PawnAttackSpan[c][s]=ForwardRanksBB[c][rank_of(s)]&adjacent_files_bb(file_of(s));
PassedPawnSpan[c][s]=ForwardFileBB[c][s]|PawnAttackSpan[c][s];
}
for(Square s1=0;s1 < 64;s1++)
for(Square s2=0;s2 < 64;s2++)
if(s1!=s2){
SquareDistance[s1][s2]=max(distance_f(s1,s2),distance_r(s1,s2));
DistanceRingBB[s1][SquareDistance[s1][s2]]|=sq_bb(s2);
}
#ifndef PEDANTIC
for(Square s=SQ_A4;s <=SQ_H5;s++)
EPMask[s-SQ_A4]=((sq_bb(s)>> 1)&~FileHBB)
|((sq_bb(s)<< 1)&~FileABB);
#endif
int steps[][5]={
{0},{7,9},{6,10,15,17},{0},{0},{0},{1,7,8,9}
};
for(int c=0;c < 2;c++)
for(int pt=PAWN;pt <=KING;pt++)
for(int s=0;s < 64;s++)
for(int i=0;steps[pt][i];i++){
Square to=s+(Square)(c==WHITE?steps[pt][i]:-steps[pt][i]);
if(square_is_ok(to)&&distance(s,to)< 3){
if(pt==PAWN)
PawnAttacks[c][s]|=sq_bb(to);
else
PseudoAttacks[pt][s]|=sq_bb(to);
}
}
init_sliding_attacks();
for(Square s1=0;s1 < 64;s1++){
PseudoAttacks[QUEEN][s1]=PseudoAttacks[BISHOP][s1]=attacks_bb_bishop(s1,0);
PseudoAttacks[QUEEN][s1]|=PseudoAttacks[ROOK][s1]=attacks_bb_rook(s1,0);
for(Square s2=0;s2 < 64;s2++){
BetweenBB[s1][s2]=sq_bb(s2);
for(int pt=BISHOP;pt <=ROOK;pt++){
if(!(PseudoAttacks[pt][s1]&sq_bb(s2)))
continue;
LineBB[s1][s2]=(attacks_bb(pt,s1,0)&attacks_bb(pt,s2,0))|sq_bb(s1)|sq_bb(s2);
BetweenBB[s1][s2]|=attacks_bb(pt,s1,sq_bb(s2))&attacks_bb(pt,s2,sq_bb(s1));
}
}
}
}
Bitboard RookMasks [64];
Bitboard RookMagics [64];
Bitboard*RookAttacks[64];
uint8_t RookShifts [64];
Bitboard BishopMasks [64];
Bitboard BishopMagics [64];
Bitboard*BishopAttacks[64];
uint8_t BishopShifts [64];
static Bitboard RookTable[0x19000];
static Bitboard BishopTable[0x1480];
typedef unsigned(Fn)(Square,Bitboard);
INLINE unsigned magic_index_bishop(Square s,Bitboard occupied)
{
if(Is64Bit)
return(unsigned)(((occupied&BishopMasks[s])*BishopMagics[s])
>> BishopShifts[s]);
unsigned lo=(unsigned)(occupied)&(unsigned)(BishopMasks[s]);
unsigned hi=(unsigned)(occupied >> 32)&(unsigned)(BishopMasks[s] >> 32);
return(lo*(unsigned)(BishopMagics[s])^hi*(unsigned)(BishopMagics[s] >> 32))>> BishopShifts[s];
}
INLINE unsigned magic_index_rook(Square s,Bitboard occupied)
{
if(Is64Bit)
return(unsigned)(((occupied&RookMasks[s])*RookMagics[s])
>> RookShifts[s]);
unsigned lo=(unsigned)(occupied)&(unsigned)(RookMasks[s]);
unsigned hi=(unsigned)(occupied >> 32)&(unsigned)(RookMasks[s] >> 32);
return(lo*(unsigned)(RookMagics[s])^hi*(unsigned)(RookMagics[s] >> 32))>> RookShifts[s];
}
INLINE Bitboard attacks_bb_bishop(Square s,Bitboard occupied)
{
return BishopAttacks[s][magic_index_bishop(s,occupied)];
}
INLINE Bitboard attacks_bb_rook(Square s,Bitboard occupied)
{
return RookAttacks[s][magic_index_rook(s,occupied)];
}
static void init_magics(Bitboard table[],Bitboard*attacks[],
Bitboard magics[],Bitboard masks[],uint8_t shifts[],
int deltas[],Fn index)
{
int seeds[][8]={{8977,44560,54343,38998,5731,95205,104912,17020},
{728,10316,55013,32803,12281,15100,16645,255}};
Bitboard occupancy[4096],reference[4096],edges,b;
int age[4096]={0},current=0,i,size;
attacks[0]=table;
for(Square s=0;s < 64;s++){
edges=((Rank1BB|Rank8BB)&~rank_bb_s(s))|((FileABB|FileHBB)&~file_bb_s(s));
masks[s]=sliding_attack(deltas,s,0)&~edges;
shifts[s]=(Is64Bit?64:32)-popcount(masks[s]);
b=size=0;
do{
occupancy[size]=b;
reference[size]=sliding_attack(deltas,s,b);
if(HasPext)
attacks[s][pext(b,masks[s])]=reference[size];
size++;
b=(b-masks[s])&masks[s];
}while(b);
if(s < 63)
attacks[s+1]=attacks[s]+size;
if(HasPext)
continue;
PRNG rng;
prng_init(&rng,seeds[Is64Bit][rank_of(s)]);
do{
do
magics[s]=prng_sparse_rand(&rng);
while(popcount((magics[s]*masks[s])>> 56)< 6);
for(current++,i=0;i < size;i++){
unsigned idx=index(s,occupancy[i]);
if(age[idx] < current){
age[idx]=current;
attacks[s][idx]=reference[i];
}
else if(attacks[s][idx]!=reference[i])
break;
}
}while(i < size);
}
}
static void init_sliding_attacks(void)
{
init_magics(RookTable,RookAttacks,RookMagics,RookMasks,
RookShifts,RookDirs,magic_index_rook);
init_magics(BishopTable,BishopAttacks,BishopMagics,BishopMasks,
BishopShifts,BishopDirs,magic_index_bishop);
}
//
#ifdef _WIN32
#else
#endif
char Version[]="";
#ifndef _WIN32
pthread_mutex_t ioMutex=PTHREAD_MUTEX_INITIALIZER;
#else
HANDLE ioMutex;
#endif
static char months[]="JanFebMarAprMayJunJulAugSepOctNovDec";
static char date[]=__DATE__;
void print_engine_info(bool to_uci)
{
char my_date[64];
printf("TheKing450 %s",Version);
if(strlen(Version)==0){
int day,month,year;
strcpy(my_date,date);
char*str=strtok(my_date," ");
for(month=1;strncmp(str,&months[3*month-3],3)!=0;month++);
str=strtok(NULL," ");
day=atoi(str);
str=strtok(NULL," ");
year=atoi(str);
printf("%02d%02d%02d",day,month,year % 100);
}
printf(
#ifdef IS_64BIT
" 64"
#endif
#ifdef USE_AVX512
" AVX512"
#elif USE_PEXT
" BMI2"
#elif USE_AVX2
" AVX2"
#elif defined(USE_NEON)
" NEON"
#elif defined(USE_POPCNT)
" POPCNT"
#endif
#ifdef USE_VNNI
"-VNNI"
#endif
#ifdef NUMA
" NUMA"
#endif
"%s\n",to_uci?"\nid author The Stockfish developers"
:" by Syzygy based on Stockfish");
fflush(stdout);
}
void print_compiler_info(void)
{
#define stringify2(x) #x
#define stringify(x) stringify2(x)
#define make_version_string(major, minor, patch) stringify(major) "." stringify(minor) "." stringify(patch)
printf("\nCompiled by "
#ifdef __clang__
"clang " make_version_string(__clang_major__,__clang_minor__,
__clang_patchlevel__)
#elif __INTEL_COMPILER
"Intel compiler (version " stringify(__INTEL_COMPILER)
" update " stringify(__INTEL_COMPILER_UPDATE)")"
#elif _MSC_VER
"MSVC (version " stringify(_MSC_FULL_VER)"." stringify(_MSC_BUILD)")"
#elif __GNUC__
"gcc (GNUC) "
make_version_string(__GNUC__,__GNUC_MINOR__,__GNUC_PATCHLEVEL__)
#else
"Unknown compiler (unknown version)"
#endif
#ifdef __APPLE__
" on Apple"
#elif __CYGWIN__
" on Cygwin"
#elif __MINGW64__
" on MinGW64"
#elif __MINGW32__
" on MinGW32"
#elif __ANDROID__
" on Android"
#elif __linux__
" on Linux"
#elif _WIN64
" on Microsoft Windows 64-bit"
#elif _WIN32
" on Microsoft Windows 32-bit"
#else
" on unknown system"
#endif
"\nCompilation settings include: "
#ifdef IS_64BIT
"64bit"
#else
"32bit"
#endif
#ifdef USE_VNNI
" VNNI"
#endif
#ifdef USE_AVX512
" AVX512"
#endif
#ifdef USE_PEXT
" BMI2"
#endif
#ifdef USE_AVX2
" AVX2"
#endif
#ifdef USE_AVX
" AVX"
#endif
#ifdef USE_SSE41
" SSE41"
#endif
#ifdef USE_SSSE3
" SSSE3"
#endif
#ifdef USE_SSE2
" SSE2"
#endif
#ifdef USE_POPCNT
" POPCNT"
#endif
#ifdef USE_MMX
" MMX"
#endif
#ifdef USE_NEON
" NEON"
#endif
#ifdef NNUE_SPARSE
" sparse"
#endif
#ifndef NDEBUG
" DEBUG"
#endif
"\n__VERSION__ macro expands to: "
#ifdef __VERSION__
__VERSION__
#else
"(undefined macro)"
#endif
"\n\n");
}
ssize_t getline(char**lineptr,size_t*n,FILE*stream)
{
if(*n==0)
*lineptr=malloc(*n=100);
int c=0;
size_t i=0;
while((c=getc(stream))!=EOF){
(*lineptr)[i++]=c;
if(i==*n)
*lineptr=realloc(*lineptr,*n+=100);
if(c=='\n')break;
}
(*lineptr)[i]=0;
return i;
}
#ifdef _WIN32
typedef SIZE_T(WINAPI*GLPM)(void);
size_t largePageMinimum;
bool large_pages_supported(void)
{
GLPM impGetLargePageMinimum=
(GLPM)(void(*)(void))GetProcAddress(GetModuleHandle("kernel32.dll"),
"GetLargePageMinimum");
if(!impGetLargePageMinimum)
return 0;
if((largePageMinimum=impGetLargePageMinimum())==0)
return 0;
LUID privLuid;
if(!LookupPrivilegeValue(NULL,SE_LOCK_MEMORY_NAME,&privLuid))
return 0;
HANDLE token;
if(!OpenProcessToken(GetCurrentProcess(),TOKEN_ADJUST_PRIVILEGES,&token))
return 0;
TOKEN_PRIVILEGES tokenPrivs;
tokenPrivs.PrivilegeCount=1;
tokenPrivs.Privileges[0].Luid=privLuid;
tokenPrivs.Privileges[0].Attributes=SE_PRIVILEGE_ENABLED;
if(!AdjustTokenPrivileges(token,FALSE,&tokenPrivs,0,NULL,NULL))
return 0;
return 1;
}
void flockfile(FILE*F){(void)F;}
void funlockfile(FILE*F){(void)F;}
#endif
FD open_file(const char*name)
{
#ifndef _WIN32
return open(name,O_RDONLY);
#else
return CreateFile(name,GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,
FILE_FLAG_RANDOM_ACCESS,NULL);
#endif
}
void close_file(FD fd)
{
#ifndef _WIN32
close(fd);
#else
CloseHandle(fd);
#endif
}
size_t file_size(FD fd)
{
#ifndef _WIN32
struct stat statbuf;
fstat(fd,&statbuf);
return statbuf.st_size;
#else
DWORD sizeLow,sizeHigh;
sizeLow=GetFileSize(fd,&sizeHigh);
return((uint64_t)sizeHigh << 32)|sizeLow;
#endif
}
const void*map_file(FD fd,map_t*map)
{
#ifndef _WIN32
*map=file_size(fd);
void*data=mmap(NULL,*map,PROT_READ,MAP_SHARED,fd,0);
#ifdef MADV_RANDOM
madvise(data,*map,MADV_RANDOM);
#endif
return data==MAP_FAILED?NULL:data;
#else
DWORD sizeLow,sizeHigh;
sizeLow=GetFileSize(fd,&sizeHigh);
*map=CreateFileMapping(fd,NULL,PAGE_READONLY,sizeHigh,sizeLow,NULL);
if(*map==NULL)
return NULL;
return MapViewOfFile(*map,FILE_MAP_READ,0,0,0);
#endif
}
void unmap_file(const void*data,map_t map)
{
if(!data)return;
#ifndef _WIN32
munmap((void*)data,map);
#else
UnmapViewOfFile(data);
CloseHandle(map);
#endif
}
void*allocate_memory(size_t size,bool lp,alloc_t*alloc)
{
void*ptr=NULL;
#ifdef _WIN32
if(lp){
size_t pageSize=largePageMinimum;
size_t lpSize=(size+pageSize-1)&~(pageSize-1);
ptr=VirtualAlloc(NULL,lpSize,
MEM_COMMIT|MEM_RESERVE|MEM_LARGE_PAGES,PAGE_READWRITE);
}else
ptr=VirtualAlloc(NULL,size,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
alloc->ptr=ptr;
return ptr;
#else /* Unix */
size_t alignment=lp?1ULL << 21:1;
size_t allocSize=size+alignment-1;
#if defined(__APPLE__) && defined(VM_FLAGS_SUPERPAGE_SIZE_2MB)
if(lp)
ptr=mmap(NULL,allocSize,PROT_READ|PROT_WRITE,
MAP_PRIVATE|MAP_ANONYMOUS,VM_FLAGS_SUPERPAGE_SIZE_2MB,0);
else
ptr=mmap(NULL,allocSize,PROT_READ|PROT_WRITE,
MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
#else
ptr=mmap(NULL,allocSize,PROT_READ|PROT_WRITE,
MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
#if defined(__linux__) && defined(MADV_HUGEPAGE)
if(lp)
madvise(ptr,allocSize,MADV_HUGEPAGE);
#endif
#endif
alloc->ptr=ptr;
alloc->size=allocSize;
return(void*)(((uintptr_t)ptr+alignment-1)&~(alignment-1));
#endif
}
void free_memory(alloc_t*alloc)
{
#ifdef _WIN32
VirtualFree(alloc->ptr,0,MEM_RELEASE);
#else
munmap(alloc->ptr,alloc->size);
#endif
}
static void set_castling_right(Position*pos,Color c,Square rfrom);
static void set_state(Position*pos,Stack*st);
#ifndef NDEBUG
static int pos_is_ok(Position*pos,int*failedStep);
static int check_pos(Position*pos);
#else
#define check_pos(p) do {} while (0)
#endif
struct Zob zob;
Key matKey[16]={
0ULL,
0x5ced000000000101ULL,
0xe173000000001001ULL,
0xd64d000000010001ULL,
0xab88000000100001ULL,
0x680b000001000001ULL,
0x0000000000000001ULL,
0ULL,
0ULL,
0xf219000010000001ULL,
0xbb14000100000001ULL,
0x58df001000000001ULL,
0xa15f010000000001ULL,
0x7c94100000000001ULL,
0x0000000000000001ULL,
0ULL
};
const char PieceToChar[]=" PNBRQK  pnbrqk";
int failed_step;
INLINE void put_piece(Position*pos,Color c,Piece piece,Square s)
{
pos->board[s]=piece;
pos->byTypeBB[0]|=sq_bb(s);
pos->byTypeBB[type_of_p(piece)]|=sq_bb(s);
pos->byColorBB[c]|=sq_bb(s);
}
INLINE void remove_piece(Position*pos,Color c,Piece piece,Square s)
{
pos->byTypeBB[0]^=sq_bb(s);
pos->byTypeBB[type_of_p(piece)]^=sq_bb(s);
pos->byColorBB[c]^=sq_bb(s);
}
INLINE void move_piece(Position*pos,Color c,Piece piece,Square from,
Square to)
{
Bitboard fromToBB=sq_bb(from)^sq_bb(to);
pos->byTypeBB[0]^=fromToBB;
pos->byTypeBB[type_of_p(piece)]^=fromToBB;
pos->byColorBB[c]^=fromToBB;
pos->board[from]=0;
pos->board[to]=piece;
}
INLINE void set_check_info(Position*pos)
{
Stack*st=pos->st;
st->blockersForKing[WHITE]=slider_blockers(pos,pieces_c(BLACK),square_of(WHITE,KING),&st->pinnersForKing[WHITE]);
st->blockersForKing[BLACK]=slider_blockers(pos,pieces_c(WHITE),square_of(BLACK,KING),&st->pinnersForKing[BLACK]);
Color them=!stm();
st->ksq=square_of(them,KING);
st->checkSquares[PAWN]=attacks_from_pawn(st->ksq,them);
st->checkSquares[KNIGHT]=attacks_from_knight(st->ksq);
st->checkSquares[BISHOP]=attacks_from_bishop(st->ksq);
st->checkSquares[ROOK]=attacks_from_rook(st->ksq);
st->checkSquares[QUEEN]=st->checkSquares[BISHOP]|st->checkSquares[ROOK];
st->checkSquares[KING]=0;
}
void print_pos(Position*pos)
{
char fen[128];
pos_fen(pos,fen);
flockfile(stdout);
printf("\n +---+---+---+---+---+---+---+---+\n");
for(int r=7;r >=0;r--){
for(int f=0;f <=7;f++)
printf(" | %c",PieceToChar[pos->board[8*r+f]]);
printf(" | %d\n +---+---+---+---+---+---+---+---+\n",r+1);
}
printf("   a   b   c   d   e   f   g   h\n\nFen: %s\nKey: %16"PRIX64"\nCheckers: ",fen,key());
char buf[16];
for(Bitboard b=checkers();b;)
printf("%s ",uci_square(buf,pop_lsb(&b)));
if(popcount(pieces())<=TB_MaxCardinality&&!can_castle_cr(ANY_CASTLING)){
int s1,s2;
int wdl=TB_probe_wdl(pos,&s1);
int dtz=TB_probe_dtz(pos,&s2);
printf("\nTablebases WDL: %4d (%d)\nTablebases DTZ: %4d (%d)",wdl,s1,dtz,s2);
if(s1&&wdl!=0){
Value dtm=TB_probe_dtm(pos,wdl,&s1);
printf("\nTablebases DTM: %s (%d)",uci_value(buf,dtm),s1);
}
}
printf("\n");
fflush(stdout);
funlockfile(stdout);
}
INLINE Key H1(Key h)
{
return h&0x1fff;
}
INLINE Key H2(Key h)
{
return(h >> 16)&0x1fff;
}
static Key cuckoo[8192];
static uint16_t cuckooMove[8192];
void zob_init(void){
PRNG rng;
prng_init(&rng,1070372);
for(int c=0;c < 2;c++)
for(int pt=PAWN;pt <=KING;pt++)
for(Square s=0;s < 64;s++)
zob.psq[make_piece(c,pt)][s]=prng_rand(&rng);
for(int f=0;f < 8;f++)
zob.enpassant[f]=prng_rand(&rng);
for(int cr=0;cr < 16;cr++)
zob.castling[cr]=prng_rand(&rng);
zob.side=prng_rand(&rng);
zob.noPawns=prng_rand(&rng);
int count=0;
for(int c=0;c < 2;c++)
for(int pt=PAWN;pt <=KING;pt++){
int pc=make_piece(c,pt);
for(Square s1=0;s1 < 64;s1++)
for(Square s2=s1+1;s2 < 64;s2++)
if(PseudoAttacks[pt][s1]&sq_bb(s2)){
Move move=make_move(s1,s2);
Key key=zob.psq[pc][s1]^zob.psq[pc][s2]^zob.side;
uint32_t i=H1(key);
while(true){
Key tmpKey=cuckoo[i];
cuckoo[i]=key;
key=tmpKey;
Move tmpMove=cuckooMove[i];
cuckooMove[i]=move;
move=tmpMove;
if(!move)break;
i=(i==H1(key))?H2(key):H1(key);
}
count++;
}
}
assert(count==3668);
}
void pos_set(Position*pos,char*fen,int isChess960)
{
unsigned char col,row,token;
Square sq=SQ_A8;
Stack*st=pos->st;
memset(pos,0,offsetof(Position,moveList));
pos->st=st;
memset(st,0,StateSize);
for(int i=0;i < 16;i++)
pos->pieceCount[i]=0;
while((token=*fen++)&&token!=' '){
if(token >='0'&&token <='9')
sq+=token-'0';
else if(token=='/')
sq-=16;
else{
for(int piece=0;piece < 16;piece++)
if(PieceToChar[piece]==token){
put_piece(pos,color_of(piece),piece,sq++);
pos->pieceCount[piece]++;
break;
}
}
}
token=*fen++;
pos->sideToMove=token=='w'?WHITE:BLACK;
token=*fen++;
while((token=*fen++)&&!isspace(token)){
Square rsq;
int c=islower(token)?BLACK:WHITE;
Piece rook=make_piece(c,ROOK);
token=toupper(token);
if(token=='K')
for(rsq=relative_square(c,SQ_H1);piece_on(rsq)!=rook;--rsq);
else if(token=='Q')
for(rsq=relative_square(c,SQ_A1);piece_on(rsq)!=rook;++rsq);
else if(token >='A'&&token <='H')
rsq=make_square(token-'A',relative_rank(c,RANK_1));
else
continue;
set_castling_right(pos,c,rsq);
}
if(((col=*fen++)&&(col >='a'&&col <='h'))
&&((row=*fen++)&&(row==(stm()==WHITE?'6':'3'))))
{
st->epSquare=make_square(col-'a',row-'1');
if(!(attackers_to(st->epSquare)&pieces_cp(stm(),PAWN)))
st->epSquare=0;
}
else
st->epSquare=0;
st->rule50=strtol(fen,&fen,10);
pos->gamePly=strtol(fen,NULL,10);
pos->gamePly=max(2*(pos->gamePly-1),0)+(stm()==BLACK);
pos->chess960=isChess960;
set_state(pos,st);
assert(pos_is_ok(pos,&failed_step));
}
static void set_castling_right(Position*pos,Color c,Square rfrom)
{
Square kfrom=square_of(c,KING);
int cs=kfrom < rfrom?KING_SIDE:QUEEN_SIDE;
int cr=(WHITE_OO <<((cs==QUEEN_SIDE)+2*c));
Square kto=relative_square(c,cs==KING_SIDE?SQ_G1:SQ_C1);
Square rto=relative_square(c,cs==KING_SIDE?SQ_F1:SQ_D1);
pos->st->castlingRights|=cr;
pos->castlingRightsMask[kfrom]|=cr;
pos->castlingRightsMask[rfrom]|=cr;
pos->castlingRookSquare[cr]=rfrom;
for(Square s=min(rfrom,rto);s <=max(rfrom,rto);s++)
if(s!=kfrom&&s!=rfrom)
pos->castlingPath[cr]|=sq_bb(s);
for(Square s=min(kfrom,kto);s <=max(kfrom,kto);s++)
if(s!=kfrom&&s!=rfrom)
pos->castlingPath[cr]|=sq_bb(s);
}
static void set_state(Position*pos,Stack*st)
{
st->key=st->materialKey=0;
#ifndef NNUE_PURE
st->pawnKey=zob.noPawns;
st->psq=0;
#endif
st->nonPawn=0;
st->checkersBB=attackers_to(square_of(stm(),KING))&pieces_c(!stm());
set_check_info(pos);
for(Bitboard b=pieces();b;){
Square s=pop_lsb(&b);
Piece pc=piece_on(s);
st->key^=zob.psq[pc][s];
#ifndef NNUE_PURE
st->psq+=psqt.psq[pc][s];
#endif
}
st->key^=zob.enpassant[file_of(st->epSquare)];
if(stm()==BLACK)
st->key^=zob.side;
st->key^=zob.castling[st->castlingRights];
#ifndef NNUE_PURE
for(Bitboard b=pieces_p(PAWN);b;){
Square s=pop_lsb(&b);
st->pawnKey^=zob.psq[piece_on(s)][s];
}
#endif
for(PieceType pt=PAWN;pt <=KING;pt++){
st->materialKey+=piece_count(WHITE,pt)*matKey[8*WHITE+pt];
st->materialKey+=piece_count(BLACK,pt)*matKey[8*BLACK+pt];
}
for(PieceType pt=KNIGHT;pt <=QUEEN;pt++)
for(int c=0;c < 2;c++)
st->nonPawn+=piece_count(c,pt)*NonPawnPieceValue[make_piece(c,pt)];
}
void pos_fen(const Position*pos,char*str)
{
int cnt;
for(int r=7;r >=0;r--){
for(int f=0;f < 8;f++){
for(cnt=0;f < 8&&!piece_on(8*r+f);f++)
cnt++;
if(cnt)*str++='0'+cnt;
if(f < 8)*str++=PieceToChar[piece_on(8*r+f)];
}
if(r > 0)*str++='/';
}
*str++=' ';
*str++=stm()==WHITE?'w':'b';
*str++=' ';
int cr=pos->st->castlingRights;
if(!is_chess960()){
if(cr&WHITE_OO)*str++='K';
if(cr&WHITE_OOO)*str++='Q';
if(cr&BLACK_OO)*str++='k';
if(cr&BLACK_OOO)*str++='q';
}else{
if(cr&WHITE_OO)*str++='A'+file_of(castling_rook_square(make_castling_right(WHITE,KING_SIDE)));
if(cr&WHITE_OOO)*str++='A'+file_of(castling_rook_square(make_castling_right(WHITE,QUEEN_SIDE)));
if(cr&BLACK_OO)*str++='a'+file_of(castling_rook_square(make_castling_right(BLACK,KING_SIDE)));
if(cr&BLACK_OOO)*str++='a'+file_of(castling_rook_square(make_castling_right(BLACK,QUEEN_SIDE)));
}
if(!cr)
*str++='-';
*str++=' ';
if(ep_square()!=0){
*str++='a'+file_of(ep_square());
*str++='1'+rank_of(ep_square());
}else{
*str++='-';
}
sprintf(str," %d %d",rule50_count(),1+(game_ply()-(stm()==BLACK))/2);
}
#if 1
Bitboard slider_blockers(const Position*pos,Bitboard sliders,Square s,
Bitboard*pinners)
{
Bitboard blockers=0,snipers;
*pinners=0;
snipers=((PseudoAttacks[ROOK ][s]&pieces_pp(QUEEN,ROOK))
|(PseudoAttacks[BISHOP][s]&pieces_pp(QUEEN,BISHOP)))&sliders;
Bitboard occupancy=pieces()^snipers;
while(snipers){
Square sniperSq=pop_lsb(&snipers);
Bitboard b=between_bb(s,sniperSq)&occupancy;
if(b&&!more_than_one(b)){
blockers|=b;
if(b&pieces_c(color_of(piece_on(s))))
*pinners|=sq_bb(sniperSq);
}
}
return blockers;
}
#endif
#if 0
Bitboard attackers_to_occ(const Position*pos,Square s,Bitboard occupied)
{
return(attacks_from_pawn(s,BLACK)&pieces_cp(WHITE,PAWN))
|(attacks_from_pawn(s,WHITE)&pieces_cp(BLACK,PAWN))
|(attacks_from_knight(s)&pieces_p(KNIGHT))
|(attacks_bb_rook(s,occupied)&pieces_pp(ROOK,QUEEN))
|(attacks_bb_bishop(s,occupied)&pieces_pp(BISHOP,QUEEN))
|(attacks_from_king(s)&pieces_p(KING));
}
#endif
bool is_legal(const Position*pos,Move m)
{
assert(move_is_ok(m));
Color us=stm();
Square from=from_sq(m);
Square to=to_sq(m);
assert(color_of(moved_piece(m))==us);
assert(piece_on(square_of(us,KING))==make_piece(us,KING));
if(unlikely(type_of_m(m)==ENPASSANT)){
Square ksq=square_of(us,KING);
Square capsq=to^8;
Bitboard occupied=pieces()^sq_bb(from)^sq_bb(capsq)^sq_bb(to);
assert(to==ep_square());
assert(moved_piece(m)==make_piece(us,PAWN));
assert(piece_on(capsq)==make_piece(!us,PAWN));
assert(piece_on(to)==0);
return!(attacks_bb_rook(ksq,occupied)&pieces_cpp(!us,QUEEN,ROOK))
&&!(attacks_bb_bishop(ksq,occupied)&pieces_cpp(!us,QUEEN,BISHOP));
}
if(unlikely(type_of_m(m)==CASTLING)){
to=relative_square(us,to > from?SQ_G1:SQ_C1);
int step=to > from?WEST:EAST;
for(Square s=to;s!=from;s+=step)
if(attackers_to(s)&pieces_c(!us))
return false;
return!is_chess960()||!(blockers_for_king(pos,us)&sq_bb(to_sq(m)));
}
if(pieces_p(KING)&sq_bb(from))
return!(attackers_to_occ(pos,to,pieces()^sq_bb(from))&pieces_c(!us));
return!(blockers_for_king(pos,us)&sq_bb(from))
||aligned(m,square_of(us,KING));
}
#if 0
bool is_pseudo_legal_old(Position*pos,Move m)
{
Color us=stm();
Square from=from_sq(m);
Square to=to_sq(m);
Piece pc=moved_piece(m);
if(type_of_m(m)!=NORMAL){
ExtMove list[MAX_MOVES];
ExtMove*last=generate_legal(pos,list);
for(ExtMove*p=list;p < last;p++)
if(p->move==m)
return true;
return false;
}
if(promotion_type(m)-KNIGHT!=0)
return false;
if(pc==0||color_of(pc)!=us)
return false;
if(pieces_c(us)&sq_bb(to))
return false;
if(type_of_p(pc)==PAWN){
if(!((to+0x08)&0x30))
return false;
if(!(attacks_from_pawn(from,us)&pieces_c(!us)&sq_bb(to))
&&!((from+pawn_push(us)==to)&&is_empty(to))
&&!((from+2*pawn_push(us)==to)
&&(rank_of(from)==relative_rank(us,RANK_2))
&&is_empty(to)
&&is_empty(to-pawn_push(us))))
return false;
}
else if(!(attacks_from(pc,from)&sq_bb(to)))
return false;
if(checkers()){
if(type_of_p(pc)!=KING){
if(more_than_one(checkers()))
return false;
if(!((between_bb(lsb(checkers()),square_of(us,KING))|checkers())&sq_bb(to)))
return false;
}
else if(attackers_to_occ(pos,to,pieces()^sq_bb(from))&pieces_c(!us))
return false;
}
return true;
}
#endif
bool is_pseudo_legal(const Position*pos,Move m)
{
Color us=stm();
Square from=from_sq(m);
if(!(pieces_c(us)&sq_bb(from)))
return false;
if(unlikely(type_of_m(m)==CASTLING)){
if(checkers())return false;
ExtMove list[MAX_MOVES];
ExtMove*end=generate_quiets(pos,list);
for(ExtMove*p=list;p < end;p++)
if(p->move==m)return true;
return false;
}
Square to=to_sq(m);
if(pieces_c(us)&sq_bb(to))
return false;
PieceType pt=type_of_p(piece_on(from));
if(pt!=PAWN){
if(type_of_m(m)!=NORMAL)
return false;
switch(pt){
case KNIGHT:
if(!(attacks_from_knight(from)&sq_bb(to)))
return false;
break;
case BISHOP:
if(!(attacks_from_bishop(from)&sq_bb(to)))
return false;
break;
case ROOK:
if(!(attacks_from_rook(from)&sq_bb(to)))
return false;
break;
case QUEEN:
if(!(attacks_from_queen(from)&sq_bb(to)))
return false;
break;
case KING:
if(!(attacks_from_king(from)&sq_bb(to)))
return false;
if(checkers()
&&(attackers_to_occ(pos,to,pieces()^sq_bb(from))&pieces_c(!us)))
return false;
return true;
default:
assume(false);
break;
}
}else{
if(likely(type_of_m(m)==NORMAL)){
if(!((to+0x08)&0x30))
return false;
if(!(attacks_from_pawn(from,us)&pieces_c(!us)&sq_bb(to))
&&!((from+pawn_push(us)==to)&&is_empty(to))
&&!(from+2*pawn_push(us)==to
&&rank_of(from)==relative_rank(us,RANK_2)
&&is_empty(to)&&is_empty(to-pawn_push(us))))
return false;
}
else if(likely(type_of_m(m)==PROMOTION)){
if(!(attacks_from_pawn(from,us)&pieces_c(!us)&sq_bb(to))
&&!((from+pawn_push(us)==to)&&is_empty(to)))
return false;
}
else
return to==ep_square()&&(attacks_from_pawn(from,us)&sq_bb(to));
}
if(checkers()){
if(more_than_one(checkers()))
return false;
if(!(between_bb(square_of(us,KING),lsb(checkers()))&sq_bb(to)))
return false;
}
return true;
}
#if 0
int is_pseudo_legal(Position*pos,Move m)
{
int r1=is_pseudo_legal_old(pos,m);
int r2=is_pseudo_legal_new(pos,m);
if(r1!=r2){
printf("old: %d, new: %d\n",r1,r2);
printf("old: %d\n",is_pseudo_legal_old(pos,m));
printf("new: %d\n",is_pseudo_legal_new(pos,m));
exit(1);
}
return r1;
}
#endif
bool gives_check_special(const Position*pos,Stack*st,Move m)
{
assert(move_is_ok(m));
assert(color_of(moved_piece(m))==stm());
Square from=from_sq(m);
Square to=to_sq(m);
if((blockers_for_king(pos,!stm())&sq_bb(from))&&!aligned(m,st->ksq))
return true;
switch(type_of_m(m)){
case NORMAL:
return st->checkSquares[type_of_p(piece_on(from))]&sq_bb(to);
case PROMOTION:
return attacks_bb(promotion_type(m),to,pieces()^sq_bb(from))&sq_bb(st->ksq);
case ENPASSANT:
{
if(st->checkSquares[PAWN]&sq_bb(to))
return true;
Square capsq=make_square(file_of(to),rank_of(from));
Bitboard b=inv_sq(inv_sq(inv_sq(pieces(),from),to),capsq);
return(attacks_bb_rook(st->ksq,b)&pieces_cpp(stm(),QUEEN,ROOK))
||(attacks_bb_bishop(st->ksq,b)&pieces_cpp(stm(),QUEEN,BISHOP));
}
case CASTLING:
{
Square rto=relative_square(stm(),to > from?SQ_F1:SQ_D1);
return(PseudoAttacks[ROOK][rto]&sq_bb(st->ksq))
&&(attacks_bb_rook(rto,pieces()^sq_bb(from))&sq_bb(st->ksq));
}
default:
assume(false);
return false;
}
}
void do_move(Position*pos,Move m,int givesCheck)
{
assert(move_is_ok(m));
Key key=pos->st->key^zob.side;
Stack*st=++pos->st;
memcpy(st,st-1,(StateCopySize+7)&~7);
st->plyCounters+=0x101;
#ifdef NNUE
st->accumulator.state[WHITE]=ACC_EMPTY;
st->accumulator.state[BLACK]=ACC_EMPTY;
DirtyPiece*dp=&(st->dirtyPiece);
dp->dirtyNum=1;
#endif
Color us=stm();
Color them=!us;
Square from=from_sq(m);
Square to=to_sq(m);
Piece piece=piece_on(from);
Piece captured=type_of_m(m)==ENPASSANT
?make_piece(them,PAWN):piece_on(to);
assert(color_of(piece)==us);
assert(is_empty(to)
||color_of(piece_on(to))==(type_of_m(m)!=CASTLING?them:us));
assert(type_of_p(captured)!=KING);
if(unlikely(type_of_m(m)==CASTLING)){
assert(piece==make_piece(us,KING));
assert(captured==make_piece(us,ROOK));
Square rfrom,rto;
int kingSide=to > from;
rfrom=to;
rto=relative_square(us,kingSide?SQ_F1:SQ_D1);
to=relative_square(us,kingSide?SQ_G1:SQ_C1);
#ifdef NNUE
dp->dirtyNum=2;
dp->pc[1]=captured;
dp->from[1]=rfrom;
dp->to[1]=rto;
#endif
remove_piece(pos,us,piece,from);
remove_piece(pos,us,captured,rfrom);
pos->board[from]=pos->board[rfrom]=0;
put_piece(pos,us,piece,to);
put_piece(pos,us,captured,rto);
#ifndef NNUE_PURE
st->psq+=psqt.psq[captured][rto]-psqt.psq[captured][rfrom];
#endif
key^=zob.psq[captured][rfrom]^zob.psq[captured][rto];
captured=0;
}
else if(captured){
Square capsq=to;
if(type_of_p(captured)==PAWN){
if(unlikely(type_of_m(m)==ENPASSANT)){
capsq^=8;
assert(piece==make_piece(us,PAWN));
assert(to==(st-1)->epSquare);
assert(relative_rank_s(us,to)==RANK_6);
assert(is_empty(to));
assert(piece_on(capsq)==make_piece(them,PAWN));
pos->board[capsq]=0;
}
#ifndef NNUE_PURE
st->pawnKey^=zob.psq[captured][capsq];
#endif
}else
st->nonPawn-=NonPawnPieceValue[captured];
#ifdef NNUE
dp->dirtyNum=2;
dp->pc[1]=captured;
dp->from[1]=capsq;
dp->to[1]=SQ_NONE;
#endif
remove_piece(pos,them,captured,capsq);
pos->pieceCount[captured]--;
key^=zob.psq[captured][capsq];
st->materialKey-=matKey[captured];
#ifndef NNUE_PURE
prefetch(&pos->materialTable[st->materialKey >>(64-13)]);
st->psq-=psqt.psq[captured][capsq];
#endif
st->plyCounters=0;
}
st->capturedPiece=captured;
key^=zob.psq[piece][from]^zob.psq[piece][to];
if(unlikely((st-1)->epSquare!=0))
key^=zob.enpassant[file_of((st-1)->epSquare)];
st->epSquare=0;
if(st->castlingRights
&&(pos->castlingRightsMask[from]|pos->castlingRightsMask[to]))
{
key^=zob.castling[st->castlingRights];
st->castlingRights&=~(pos->castlingRightsMask[from]|pos->castlingRightsMask[to]);
key^=zob.castling[st->castlingRights];
}
#ifdef NNUE
dp->pc[0]=piece;
dp->from[0]=from;
dp->to[0]=to;
#endif
if(likely(type_of_m(m)!=CASTLING))
move_piece(pos,us,piece,from,to);
if(type_of_p(piece)==PAWN){
if((to^from)==16
&&(attacks_from_pawn(to^8,us)&pieces_cp(them,PAWN)))
{
st->epSquare=to^8;
key^=zob.enpassant[file_of(st->epSquare)];
}
else if(type_of_m(m)==PROMOTION){
Piece promotion=make_piece(us,promotion_type(m));
assert(relative_rank_s(us,to)==RANK_8);
assert(type_of_p(promotion)>=KNIGHT&&type_of_p(promotion)<=QUEEN);
remove_piece(pos,us,piece,to);
pos->pieceCount[piece]--;
put_piece(pos,us,promotion,to);
pos->pieceCount[promotion]++;
#ifdef NNUE
dp->to[0]=SQ_NONE;
dp->pc[dp->dirtyNum]=promotion;
dp->from[dp->dirtyNum]=SQ_NONE;
dp->to[dp->dirtyNum]=to;
dp->dirtyNum++;
#endif
key^=zob.psq[piece][to]^zob.psq[promotion][to];
#ifndef NNUE_PURE
st->pawnKey^=zob.psq[piece][to];
#endif
st->materialKey+=matKey[promotion]-matKey[piece];
#ifndef NNUE_PURE
st->psq+=psqt.psq[promotion][to]-psqt.psq[piece][to];
#endif
st->nonPawn+=NonPawnPieceValue[promotion];
}
#ifndef NNUE_PURE
st->pawnKey^=zob.psq[piece][from]^zob.psq[piece][to];
prefetch2(&pos->pawnTable[st->pawnKey&(PAWN_ENTRIES-1)]);
#endif
st->plyCounters=0;
}
#ifndef NNUE_PURE
st->psq+=psqt.psq[piece][to]-psqt.psq[piece][from];
#endif
st->key=key;
#if 1
st->checkersBB=givesCheck
?attackers_to(square_of(them,KING))&pieces_c(us):0;
#else
st->checkersBB=0;
if(givesCheck){
if(type_of_m(m)!=NORMAL||((st-1)->blockersForKing[them]&sq_bb(from)))
st->checkersBB=attackers_to(square_of(them,KING))&pieces_c(us);
else
st->checkersBB=(st-1)->checkSquares[piece&7]&sq_bb(to);
}
#endif
pos->sideToMove=!pos->sideToMove;
pos->nodes++;
set_check_info(pos);
assert(pos_is_ok(pos,&failed_step));
}
void undo_move(Position*pos,Move m)
{
assert(move_is_ok(m));
pos->sideToMove=!pos->sideToMove;
Color us=stm();
Square from=from_sq(m);
Square to=to_sq(m);
Piece pc=piece_on(to);
assert(is_empty(from)||type_of_m(m)==CASTLING);
assert(type_of_p(pos->st->capturedPiece)!=KING);
if(unlikely(type_of_m(m)==PROMOTION)){
assert(relative_rank_s(us,to)==RANK_8);
assert(type_of_p(pc)==promotion_type(m));
assert(type_of_p(pc)>=KNIGHT&&type_of_p(pc)<=QUEEN);
remove_piece(pos,us,pc,to);
pos->pieceCount[pc]--;
pc=make_piece(us,PAWN);
put_piece(pos,us,pc,to);
pos->pieceCount[pc]++;
}
if(unlikely(type_of_m(m)==CASTLING)){
Square rfrom,rto;
int kingSide=to > from;
rfrom=to;
rto=relative_square(us,kingSide?SQ_F1:SQ_D1);
to=relative_square(us,kingSide?SQ_G1:SQ_C1);
Piece king=make_piece(us,KING);
Piece rook=make_piece(us,ROOK);
remove_piece(pos,us,king,to);
remove_piece(pos,us,rook,rto);
pos->board[to]=pos->board[rto]=0;
put_piece(pos,us,king,from);
put_piece(pos,us,rook,rfrom);
}else{
move_piece(pos,us,pc,to,from);
if(pos->st->capturedPiece){
Square capsq=to;
if(unlikely(type_of_m(m)==ENPASSANT)){
capsq^=8;
assert(type_of_p(pc)==PAWN);
assert(to==(pos->st-1)->epSquare);
assert(relative_rank_s(us,to)==RANK_6);
assert(is_empty(capsq));
assert(pos->st->capturedPiece==make_piece(!us,PAWN));
}
put_piece(pos,!us,pos->st->capturedPiece,capsq);
pos->pieceCount[pos->st->capturedPiece]++;
}
}
pos->st--;
assert(pos_is_ok(pos,&failed_step));
}
void do_null_move(Position*pos)
{
assert(!checkers());
Stack*st=++pos->st;
memcpy(st,st-1,(StateSize+7)&~7);
#ifdef NNUE
st->accumulator.state[WHITE]=ACC_EMPTY;
st->accumulator.state[BLACK]=ACC_EMPTY;
st->dirtyPiece.dirtyNum=0;
st->dirtyPiece.pc[0]=0;
#endif
if(unlikely(st->epSquare)){
st->key^=zob.enpassant[file_of(st->epSquare)];
st->epSquare=0;
}
st->key^=zob.side;
prefetch(tt_first_entry(st->key));
st->rule50++;
st->pliesFromNull=0;
pos->sideToMove=!pos->sideToMove;
set_check_info(pos);
assert(pos_is_ok(pos,&failed_step));
}
Key key_after(const Position*pos,Move m)
{
Square from=from_sq(m);
Square to=to_sq(m);
Piece pc=piece_on(from);
Piece captured=piece_on(to);
Key k=pos->st->key^zob.side;
if(captured)
k^=zob.psq[captured][to];
return k^zob.psq[pc][to]^zob.psq[pc][from];
}
bool see_test(const Position*pos,Move m,int value)
{
if(unlikely(type_of_m(m)!=NORMAL))
return 0 >=value;
Square from=from_sq(m),to=to_sq(m);
Bitboard occ;
int swap=PieceValue[MG][piece_on(to)]-value;
if(swap < 0)
return false;
swap=PieceValue[MG][piece_on(from)]-swap;
if(swap <=0)
return true;
occ=pieces()^sq_bb(from)^sq_bb(to);
Color stm=color_of(piece_on(from));
Bitboard attackers=attackers_to_occ(pos,to,occ),stmAttackers;
bool res=true;
while(true){
stm=!stm;
attackers&=occ;
if(!(stmAttackers=attackers&pieces_c(stm)))break;
if((stmAttackers&blockers_for_king(pos,stm))
&&(pos->st->pinnersForKing[stm]&occ))
stmAttackers&=~blockers_for_king(pos,stm);
if(!stmAttackers)break;
res=!res;
Bitboard bb;
if((bb=stmAttackers&pieces_p(PAWN))){
if((swap=PawnValueMg-swap)< res)break;
occ^=bb&-bb;
attackers|=attacks_bb_bishop(to,occ)&pieces_pp(BISHOP,QUEEN);
}
else if((bb=stmAttackers&pieces_p(KNIGHT))){
if((swap=KnightValueMg-swap)< res)break;
occ^=bb&-bb;
}
else if((bb=stmAttackers&pieces_p(BISHOP))){
if((swap=BishopValueMg-swap)< res)break;
occ^=bb&-bb;
attackers|=attacks_bb_bishop(to,occ)&pieces_pp(BISHOP,QUEEN);
}
else if((bb=stmAttackers&pieces_p(ROOK))){
if((swap=RookValueMg-swap)< res)break;
occ^=bb&-bb;
attackers|=attacks_bb_rook(to,occ)&pieces_pp(ROOK,QUEEN);
}
else if((bb=stmAttackers&pieces_p(QUEEN))){
if((swap=QueenValueMg-swap)< res)break;
occ^=bb&-bb;
attackers|=(attacks_bb_bishop(to,occ)&pieces_pp(BISHOP,QUEEN))
|(attacks_bb_rook(to,occ)&pieces_pp(ROOK,QUEEN));
}
else
return(attackers&~pieces_c(stm))?!res:res;
}
return res;
}
SMALL
bool is_draw(const Position*pos)
{
Stack*st=pos->st;
if(unlikely(st->rule50 > 99)){
if(!checkers())
return true;
return generate_legal(pos,(st-1)->endMoves)!=(st-1)->endMoves;
}
int e=st->pliesFromNull-4;
if(e >=0){
Stack*stp=st-2;
for(int i=0;i <=e;i+=2){
stp-=2;
if(stp->key==st->key)
return true;
}
}
return false;
}
bool has_game_cycle(const Position*pos,int ply)
{
unsigned int j;
int end=pos->st->pliesFromNull;
Key originalKey=pos->st->key;
Stack*stp=pos->st-1;
for(int i=3;i <=end;i+=2){
stp-=2;
Key moveKey=originalKey^stp->key;
if((j=H1(moveKey),cuckoo[j]==moveKey)
||(j=H2(moveKey),cuckoo[j]==moveKey))
{
Move m=cuckooMove[j];
if(!((((Bitboard*)BetweenBB)[m]^sq_bb(to_sq(m)))&pieces())){
if(ply > i
||color_of(piece_on(is_empty(from_sq(m))?to_sq(m):from_sq(m)))==stm())
return true;
}
}
}
return false;
}
void pos_set_check_info(Position*pos)
{
set_check_info(pos);
}
#ifndef NDEBUG
static int pos_is_ok(Position*pos,int*failedStep)
{
int Fast=1;
enum{Default,King,Bitboards,StackOK,Lists,Castling};
for(int step=Default;step <=(Fast?Default:Castling);step++){
if(failedStep)
*failedStep=step;
if(step==Default)
if((stm()!=WHITE&&stm()!=BLACK)
||piece_on(square_of(WHITE,KING))!=W_KING
||piece_on(square_of(BLACK,KING))!=B_KING
||(ep_square()&&relative_rank_s(stm(),ep_square())!=RANK_6))
return 0;
#if 0
if(step==King)
if(std::count(board,board+SQUARE_NB,W_KING)!=1
||std::count(board,board+SQUARE_NB,B_KING)!=1
||attackers_to(square_of(!stm(),KING))&pieces_c(stm()))
return 0;
#endif
if(step==Bitboards){
if((pieces_c(WHITE)&pieces_c(BLACK))
||(pieces_c(WHITE)|pieces_c(BLACK))!=pieces())
return 0;
for(int p1=PAWN;p1 <=KING;p1++)
for(int p2=PAWN;p2 <=KING;p2++)
if(p1!=p2&&(pieces_p(p1)&pieces_p(p2)))
return 0;
}
if(step==StackOK){
Stack si=*(pos->st);
set_state(pos,&si);
if(memcmp(&si,pos->st,StateSize))
return 0;
}
if(step==Lists)
for(int c=0;c < 2;c++)
for(int pt=PAWN;pt <=KING;pt++)
if(piece_count(c,pt)!=popcount(pieces_cp(c,pt)))
return 0;
if(step==Castling)
for(int c=0;c < 2;c++)
for(int s=0;s < 2;s++){
int cr=make_castling_right(c,s);
if(!can_castle_cr(cr))
continue;
if(piece_on(pos->castlingRookSquare[cr])!=make_piece(c,ROOK)
||pos->castlingRightsMask[pos->castlingRookSquare[cr]]!=cr
||(pos->castlingRightsMask[square_of(c,KING)]&cr)!=cr)
return 0;
}
}
return 1;
}
#endif
//
enum{CAPTURES,QUIETS,QUIET_CHECKS,EVASIONS,NON_EVASIONS,LEGAL};
INLINE ExtMove*make_promotions(ExtMove*list,Square to,Square ksq,
const int Type,const int D)
{
if(Type==CAPTURES||Type==EVASIONS||Type==NON_EVASIONS){
(list++)->move=make_promotion(to-D,to,QUEEN);
if(attacks_from_knight(to)&sq_bb(ksq))
(list++)->move=make_promotion(to-D,to,KNIGHT);
}
if(Type==QUIETS||Type==EVASIONS||Type==NON_EVASIONS){
(list++)->move=make_promotion(to-D,to,ROOK);
(list++)->move=make_promotion(to-D,to,BISHOP);
if(!(attacks_from_knight(to)&sq_bb(ksq)))
(list++)->move=make_promotion(to-D,to,KNIGHT);
}
return list;
}
INLINE ExtMove*generate_pawn_moves(const Position*pos,ExtMove*list,
Bitboard target,const Color Us,const int Type)
{
const Color Them=Us==WHITE?BLACK:WHITE;
const Bitboard TRank8BB=Us==WHITE?Rank8BB:Rank1BB;
const Bitboard TRank7BB=Us==WHITE?Rank7BB:Rank2BB;
const Bitboard TRank3BB=Us==WHITE?Rank3BB:Rank6BB;
const int Up=Us==WHITE?NORTH:SOUTH;
const int Right=Us==WHITE?NORTH_EAST:SOUTH_WEST;
const int Left=Us==WHITE?NORTH_WEST:SOUTH_EAST;
const Bitboard emptySquares=Type==QUIETS||Type==QUIET_CHECKS
?target:~pieces();
const Bitboard enemies=Type==EVASIONS?checkers()
:Type==CAPTURES?target:pieces_c(Them);
Bitboard pawnsOn7=pieces_cp(Us,PAWN)&TRank7BB;
Bitboard pawnsNotOn7=pieces_cp(Us,PAWN)&~TRank7BB;
if(Type!=CAPTURES){
Bitboard b1=shift_bb(Up,pawnsNotOn7)&emptySquares;
Bitboard b2=shift_bb(Up,b1&TRank3BB)&emptySquares;
if(Type==EVASIONS){
b1&=target;
b2&=target;
}
if(Type==QUIET_CHECKS){
Stack*st=pos->st;
Bitboard dcCandidatePawns=blockers_for_king(pos,Them)&~file_bb_s(st->ksq);
b1&=attacks_from_pawn(st->ksq,Them)|shift_bb(Up,dcCandidatePawns);
b2&=attacks_from_pawn(st->ksq,Them)|shift_bb(Up+Up,dcCandidatePawns);
}
while(b1){
Square to=pop_lsb(&b1);
(list++)->move=make_move(to-Up,to);
}
while(b2){
Square to=pop_lsb(&b2);
(list++)->move=make_move(to-Up-Up,to);
}
}
if(pawnsOn7&&(Type!=EVASIONS||(target&TRank8BB))){
Bitboard b1=shift_bb(Right,pawnsOn7)&enemies;
Bitboard b2=shift_bb(Left,pawnsOn7)&enemies;
Bitboard b3=shift_bb(Up,pawnsOn7)&emptySquares;
if(Type==EVASIONS)
b3&=target;
while(b1)
list=make_promotions(list,pop_lsb(&b1),pos->st->ksq,Type,Right);
while(b2)
list=make_promotions(list,pop_lsb(&b2),pos->st->ksq,Type,Left);
while(b3)
list=make_promotions(list,pop_lsb(&b3),pos->st->ksq,Type,Up);
}
if(Type==CAPTURES||Type==EVASIONS||Type==NON_EVASIONS){
Bitboard b1=shift_bb(Right,pawnsNotOn7)&enemies;
Bitboard b2=shift_bb(Left,pawnsNotOn7)&enemies;
while(b1){
Square to=pop_lsb(&b1);
(list++)->move=make_move(to-Right,to);
}
while(b2){
Square to=pop_lsb(&b2);
(list++)->move=make_move(to-Left,to);
}
if(ep_square()!=0){
assert(rank_of(ep_square())==relative_rank(Us,RANK_6));
if(Type==EVASIONS&&(target&sq_bb(ep_square()+Up)))
return list;
b1=pawnsNotOn7&attacks_from_pawn(ep_square(),Them);
assert(b1);
while(b1)
(list++)->move=make_enpassant(pop_lsb(&b1),ep_square());
}
}
return list;
}
INLINE ExtMove*generate_moves(const Position*pos,ExtMove*list,
Bitboard target,const Color Us,const int Pt,const bool Checks)
{
assert(Pt!=KING&&Pt!=PAWN);
Bitboard bb=pieces_cp(Us,Pt);
while(bb){
Square from=pop_lsb(&bb);
Bitboard b=attacks_bb(Pt,from,pieces())&target;
if(Checks&&(Pt==QUEEN||!(blockers_for_king(pos,!Us)&sq_bb(from))))
b&=pos->st->checkSquares[Pt];
while(b)
(list++)->move=make_move(from,pop_lsb(&b));
}
return list;
}
INLINE ExtMove*generate_all(const Position*pos,ExtMove*list,const Color Us,
const int Type)
{
const bool Checks=Type==QUIET_CHECKS;
const Square ksq=square_of(Us,KING);
Bitboard target;
if(Type==EVASIONS&&more_than_one(checkers()))
goto kingMoves;
target=Type==EVASIONS?between_bb(ksq,lsb(checkers()))
:Type==NON_EVASIONS?~pieces_c(Us)
:Type==CAPTURES?pieces_c(!Us):~pieces();
list=generate_pawn_moves(pos,list,target,Us,Type);
list=generate_moves(pos,list,target,Us,KNIGHT,Checks);
list=generate_moves(pos,list,target,Us,BISHOP,Checks);
list=generate_moves(pos,list,target,Us,ROOK,Checks);
list=generate_moves(pos,list,target,Us,QUEEN,Checks);
kingMoves:
if(!Checks||blockers_for_king(pos,!Us)&sq_bb(ksq)){
Bitboard b=attacks_from(KING,ksq)&(Type==EVASIONS?~pieces_c(Us):target);
if(Checks)
b&=~PseudoAttacks[QUEEN][square_of(!Us,KING)];
while(b)
(list++)->move=make_move(ksq,pop_lsb(&b));
if((Type==QUIETS||Type==NON_EVASIONS)&&can_castle_c(Us)){
const int OO=make_castling_right(Us,KING_SIDE);
if(!castling_impeded(OO)&&can_castle_cr(OO))
(list++)->move=make_castling(ksq,castling_rook_square(OO));
const int OOO=make_castling_right(Us,QUEEN_SIDE);
if(!castling_impeded(OOO)&&can_castle_cr(OOO))
(list++)->move=make_castling(ksq,castling_rook_square(OOO));
}
}
return list;
}
INLINE ExtMove*generate(const Position*pos,ExtMove*list,const int Type)
{
assert(Type!=LEGAL);
assert((Type==EVASIONS)==(bool)checkers());
Color us=stm();
return us==WHITE?generate_all(pos,list,WHITE,Type)
:generate_all(pos,list,BLACK,Type);
}
NOINLINE ExtMove*generate_captures(const Position*pos,ExtMove*list)
{
return generate(pos,list,CAPTURES);
}
NOINLINE ExtMove*generate_quiets(const Position*pos,ExtMove*list)
{
return generate(pos,list,QUIETS);
}
NOINLINE ExtMove*generate_evasions(const Position*pos,ExtMove*list)
{
return generate(pos,list,EVASIONS);
}
NOINLINE ExtMove*generate_quiet_checks(const Position*pos,ExtMove*list)
{
return generate(pos,list,QUIET_CHECKS);
}
NOINLINE ExtMove*generate_non_evasions(const Position*pos,ExtMove*list)
{
return generate(pos,list,NON_EVASIONS);
}
NOINLINE ExtMove*generate_legal(const Position*pos,ExtMove*list)
{
Color us=stm();
Bitboard pinned=blockers_for_king(pos,us)&pieces_c(us);
Square ksq=square_of(us,KING);
ExtMove*cur=list;
list=checkers()?generate_evasions(pos,list)
:generate_non_evasions(pos,list);
while(cur!=list)
if(((pinned&&pinned&sq_bb(from_sq(cur->move)))
||from_sq(cur->move)==ksq
||type_of_m(cur->move)==ENPASSANT)
&&!is_legal(pos,cur->move))
cur->move=(--list)->move;
else
++cur;
return list;
}
//
Value PieceValue[2][16]={
{0,PawnValueMg,KnightValueMg,BishopValueMg,RookValueMg,QueenValueMg},
{0,PawnValueEg,KnightValueEg,BishopValueEg,RookValueEg,QueenValueEg}
};
uint32_t NonPawnPieceValue[16];
#ifndef NNUE_PURE
#define S(mg, eg) make_score(mg, eg)
static const Score Bonus[][8][4]={
{{0}},
{{0}},
{
{S(-175,-96),S(-92,-65),S(-74,-49),S(-73,-21)},
{S(-77,-67),S(-41,-54),S(-27,-18),S(-15,8)},
{S(-61,-40),S(-17,-27),S(6,-8),S(12,29)},
{S(-35,-35),S(8,-2),S(40,13),S(49,28)},
{S(-34,-45),S(13,-16),S(44,9),S(51,39)},
{S(-9,-51),S(22,-44),S(58,-16),S(53,17)},
{S(-67,-69),S(-27,-50),S(4,-51),S(37,12)},
{S(-201,-100),S(-83,-88),S(-56,-56),S(-26,-17)}
},
{
{S(-37,-40),S(-4,-21),S(-6,-26),S(-16,-8)},
{S(-11,-26),S(6,-9),S(13,-12),S(3,1)},
{S(-5,-11),S(15,-1),S(-4,-1),S(12,7)},
{S(-4,-14),S(8,-4),S(18,0),S(27,12)},
{S(-8,-12),S(20,-1),S(15,-10),S(22,11)},
{S(-11,-21),S(4,4),S(1,3),S(8,4)},
{S(-12,-22),S(-10,-14),S(4,-1),S(0,1)},
{S(-34,-32),S(1,-29),S(-10,-26),S(-16,-17)}
},
{
{S(-31,-9),S(-20,-13),S(-14,-10),S(-5,-9)},
{S(-21,-12),S(-13,-9),S(-8,-1),S(6,-2)},
{S(-25,6),S(-11,-8),S(-1,-2),S(3,-6)},
{S(-13,-6),S(-5,1),S(-4,-9),S(-6,7)},
{S(-27,-5),S(-15,8),S(-4,7),S(3,-6)},
{S(-22,6),S(-2,1),S(6,-7),S(12,10)},
{S(-2,4),S(12,5),S(16,20),S(18,-5)},
{S(-17,18),S(-19,0),S(-1,19),S(9,13)}
},
{
{S(3,-69),S(-5,-57),S(-5,-47),S(4,-26)},
{S(-3,-54),S(5,-31),S(8,-22),S(12,-4)},
{S(-3,-39),S(6,-18),S(13,-9),S(7,3)},
{S(4,-23),S(5,-3),S(9,13),S(8,24)},
{S(0,-29),S(14,-6),S(12,9),S(5,21)},
{S(-4,-38),S(10,-18),S(6,-11),S(8,1)},
{S(-5,-50),S(6,-27),S(10,-24),S(8,-8)},
{S(-2,-74),S(-2,-52),S(1,-43),S(-2,-34)}
},
{
{S(271,1),S(327,45),S(271,85),S(198,76)},
{S(278,53),S(303,100),S(234,133),S(179,135)},
{S(195,88),S(258,130),S(169,169),S(120,175)},
{S(164,103),S(190,156),S(138,172),S(98,172)},
{S(154,96),S(179,166),S(105,199),S(70,199)},
{S(123,92),S(145,172),S(81,184),S(31,191)},
{S(88,47),S(120,121),S(65,116),S(33,131)},
{S(59,11),S(89,59),S(45,73),S(-1,78)}
}
};
static const Score PBonus[8][8]={
{0},
{S(2,-8),S(4,-6),S(11,9),S(18,5),S(16,16),S(21,6),S(9,-6),S(-3,-18)},
{S(-9,-9),S(-15,-7),S(11,-10),S(15,5),S(31,2),S(23,3),S(6,-8),S(-20,-5)},
{S(-3,7),S(-20,1),S(8,-8),S(19,-2),S(39,-14),S(17,-13),S(2,-11),S(-5,-6)},
{S(11,12),S(-4,6),S(-11,2),S(2,-6),S(11,-5),S(0,-4),S(-12,14),S(5,9)},
{S(3,27),S(-11,18),S(-6,19),S(22,29),S(-8,30),S(-5,9),S(-14,8),S(-11,14)},
{S(-7,-1),S(6,-14),S(-2,13),S(-11,22),S(4,24),S(-14,17),S(10,7),S(-9,7)}
};
#undef S
struct PSQT psqt;
#endif
void psqt_init(void)
{
for(int pt=PAWN;pt <=KING;pt++){
PieceValue[MG][make_piece(BLACK,pt)]=PieceValue[MG][pt];
PieceValue[EG][make_piece(BLACK,pt)]=PieceValue[EG][pt];
#ifndef NNUE_PURE
Score score=make_score(PieceValue[MG][pt],PieceValue[EG][pt]);
for(Square s=0;s < 64;s++){
int f=min(file_of(s),FILE_H-file_of(s));
psqt.psq[make_piece(WHITE,pt)][s]=
score+(type_of_p(pt)==PAWN?PBonus[rank_of(s)][file_of(s)]
:Bonus[pt][rank_of(s)][f]);
psqt.psq[make_piece(BLACK,pt)][s^0x38]=
-psqt.psq[make_piece(WHITE,pt)][s];
}
#endif
}
union{
uint16_t val[2];
uint32_t combi;
}tmp;
NonPawnPieceValue[W_PAWN]=NonPawnPieceValue[B_PAWN]=0;
for(int pt=KNIGHT;pt < KING;pt++){
tmp.val[0]=PieceValue[MG][pt];
tmp.val[1]=0;
NonPawnPieceValue[pt]=tmp.combi;
tmp.val[0]=0;
tmp.val[1]=PieceValue[MG][pt];
NonPawnPieceValue[pt+8]=tmp.combi;
}
}
//
#ifndef NNUE_PURE
#define S(mg,eg) make_score(mg,eg)
static const Score QuadraticOurs[][8]={
{S(1419,1455)},
{S(101,28),S(37,39)},
{S(57,64),S(249,187),S(-49,-62)},
{S(0,0),S(118,137),S(10,27),S(0,0)},
{S(-63,-68),S(-5,3),S(100,81),S(132,118),S(-246,-244)},
{S(-210,-211),S(37,14),S(147,141),S(161,105),S(-158,-174),S(-9,-31)}
};
static const Score QuadraticTheirs[][8]={
{0},
{S(33,30)},
{S(46,18),S(106,84)},
{S(75,35),S(59,44),S(60,15)},
{S(26,35),S(6,22),S(38,39),S(-12,-2)},
{S(97,93),S(100,163),S(-58,-91),S(112,192),S(276,225)}
};
#undef S
INLINE bool is_KXK(const Position*pos,int us)
{
return!more_than_one(pieces_c(!us))
&&non_pawn_material_c(us)>=RookValueMg;
}
INLINE bool is_KBPsK(const Position*pos,int us)
{
return non_pawn_material_c(us)==BishopValueMg
&&pieces_cp(us,PAWN);
}
INLINE bool is_KQKRPs(const Position*pos,int us){
return!piece_count(us,PAWN)
&&non_pawn_material_c(us)==QueenValueMg
&&piece_count(!us,ROOK)==1
&&pieces_cp(!us,PAWN);
}
static Score imbalance(int us,int pieceCount[][8])
{
int*pc_us=pieceCount[us];
int*pc_them=pieceCount[!us];
Score bonus=SCORE_ZERO;
for(int pt1=0;pt1 <=QUEEN;pt1++){
if(!pc_us[pt1])
continue;
int v=0;
for(int pt2=0;pt2 <=pt1;pt2++)
v+=QuadraticOurs[pt1][pt2]*pc_us[pt2]
+QuadraticTheirs[pt1][pt2]*pc_them[pt2];
bonus+=pc_us[pt1]*v;
}
return bonus;
}
typedef int PieceCountType[2][8];
void material_entry_fill(const Position*pos,MaterialEntry*e,Key key)
{
memset(e,0,sizeof(MaterialEntry));
e->key=key;
e->factor[WHITE]=e->factor[BLACK]=(uint8_t)SCALE_FACTOR_NORMAL;
Value npm_w=non_pawn_material_c(WHITE);
Value npm_b=non_pawn_material_c(BLACK);
Value npm=clamp(npm_w+npm_b,EndgameLimit,MidgameLimit);
e->gamePhase=((npm-EndgameLimit)*PHASE_MIDGAME)/(MidgameLimit-EndgameLimit);
for(int i=0;i < NUM_EVAL;i++)
for(int c=0;c < 2;c++)
if(endgame_keys[i][c]==key){
e->eval_func=1+i;
e->eval_func_side=c;
return;
}
for(int c=0;c < 2;c++)
if(is_KXK(pos,c)){
e->eval_func=10;
e->eval_func_side=c;
return;
}
for(int i=0;i < NUM_SCALING;i++)
for(int c=0;c < 2;c++)
if(endgame_keys[NUM_EVAL+i][c]==key){
e->scal_func[c]=11+i;
return;
}
for(int c=0;c < 2;c++){
if(is_KBPsK(pos,c))
e->scal_func[c]=17;
else if(is_KQKRPs(pos,c))
e->scal_func[c]=18;
}
if(npm_w+npm_b==0&&pieces_p(PAWN)){
if(!pieces_cp(BLACK,PAWN)){
assert(piece_count(WHITE,PAWN)>=2);
e->scal_func[WHITE]=19;
}
else if(!pieces_cp(WHITE,PAWN)){
assert(piece_count(BLACK,PAWN)>=2);
e->scal_func[BLACK]=19;
}
else if(popcount(pieces_p(PAWN))==2){
e->scal_func[WHITE]=20;
e->scal_func[BLACK]=20;
}
}
if(!piece_count(WHITE,PAWN)&&npm_w-npm_b <=BishopValueMg)
e->factor[WHITE]=(uint8_t)(npm_w < RookValueMg?SCALE_FACTOR_DRAW:
npm_b <=BishopValueMg?4:14);
if(!piece_count(BLACK,PAWN)&&npm_b-npm_w <=BishopValueMg)
e->factor[BLACK]=(uint8_t)(npm_b < RookValueMg?SCALE_FACTOR_DRAW:
npm_w <=BishopValueMg?4:14);
#define pc(c,p) piece_count_mk(c,p)
int PieceCount[2][8]={
{pc(0,BISHOP)> 1,pc(0,PAWN),pc(0,KNIGHT),
pc(0,BISHOP),pc(0,ROOK),pc(0,QUEEN)},
{pc(1,BISHOP)> 1,pc(1,PAWN),pc(1,KNIGHT),
pc(1,BISHOP),pc(1,ROOK),pc(1,QUEEN)}
};
#undef pc
Score tmp=imbalance(WHITE,PieceCount)-imbalance(BLACK,PieceCount);
e->score=make_score(mg_value(tmp)/16,eg_value(tmp)/16);
}
#else
typedef int make_iso_compilers_happy;
#endif
//
#ifndef NNUE_PURE
#define V(v) ((Value)(v))
#define S(mg, eg) make_score(mg, eg)
static const Score Backward=S(9,22);
static const Score Doubled=S(13,51);
static const Score DoubledEarly=S(20,7);
static const Score Isolated=S(3,15);
static const Score WeakLever=S(4,58);
static const Score WeakUnopposed=S(13,24);
static const Score BlockedPawn[2]={S(-17,-6),S(-9,2)};
static const Score BlockedStorm[8]={
S(0,0),S(0,0),S(75,78),S(-8,16),S(-6,10),S(-6,6),S(0,2)
};
static const int Connected[8]={0,5,7,11,23,48,87};
#undef V
#define V(mg) S(mg,0)
static const Score ShelterStrength[4][8]={
{V(-5),V(82),V(92),V(54),V(36),V(22),V(28)},
{V(-44),V(63),V(33),V(-50),V(-30),V(-12),V(-62)},
{V(-11),V(77),V(22),V(-6),V(31),V(8),V(-45)},
{V(-39),V(-12),V(-29),V(-50),V(-43),V(-68),V(-164)}
};
static const Score UnblockedStorm[4][8]={
{V(87),V(-288),V(-168),V(96),V(47),V(44),V(46)},
{V(42),V(-25),V(120),V(45),V(34),V(-9),V(24)},
{V(-8),V(51),V(167),V(35),V(-4),V(-16),V(-12)},
{V(-17),V(-13),V(100),V(4),V(9),V(-16),V(-31)}
};
static const Score KingOnFile[2][2]={
{S(-21,10),S(-7,1)},{S(0,-3),S(9,-4)}
};
#undef S
#undef V
INLINE Score pawn_evaluate(const Position*pos,PawnEntry*e,const Color Us)
{
const Color Them=Us==WHITE?BLACK:WHITE;
const int Up=Us==WHITE?NORTH:SOUTH;
const int Down=Us==WHITE?SOUTH:NORTH;
Bitboard neighbours,stoppers,doubled,support,phalanx,opposed;
Bitboard lever,leverPush,blocked;
Square s;
bool backward,passed;
Score score=SCORE_ZERO;
Bitboard ourPawns=pieces_cp(Us,PAWN);
Bitboard theirPawns=pieces_p(PAWN)^ourPawns;
Bitboard doubleAttackThem=pawn_double_attacks_bb(theirPawns,Them);
e->passedPawns[Us]=0;
e->semiopenFiles[Us]=0xFF;
e->kingSquares[Us]=SQ_NONE;
e->pawnAttacks[Us]=e->pawnAttacksSpan[Us]=pawn_attacks_bb(ourPawns,Us);
e->pawnsOnSquares[Us][BLACK]=popcount(ourPawns&DarkSquares);
e->pawnsOnSquares[Us][WHITE]=popcount(ourPawns&LightSquares);
e->blockedCount+=popcount(shift_bb(Up,ourPawns)
&(theirPawns|doubleAttackThem));
loop_through_pieces(Us,PAWN,s){
assert(piece_on(s)==make_piece(Us,PAWN));
int f=file_of(s);
int r=relative_rank_s(Us,s);
e->semiopenFiles[Us]&=~(1 << f);
opposed=theirPawns&forward_file_bb(Us,s);
blocked=theirPawns&sq_bb(s+Up);
stoppers=theirPawns&passed_pawn_span(Us,s);
lever=theirPawns&PawnAttacks[Us][s];
leverPush=theirPawns&PawnAttacks[Us][s+Up];
doubled=ourPawns&sq_bb(s-Up);
neighbours=ourPawns&adjacent_files_bb(f);
phalanx=neighbours&rank_bb_s(s);
support=neighbours&rank_bb_s(s-Up);
if(doubled){
if(!(ourPawns&shift_bb(Down,theirPawns|pawn_attacks_bb(theirPawns,Them))))
score-=DoubledEarly;
}
backward=!(neighbours&forward_ranks_bb(Them,rank_of(s+Up)))
&&(leverPush|blocked);
if(!backward&&!blocked)
e->pawnAttacksSpan[Us]|=pawn_attack_span(Us,s);
passed=!(stoppers^lever)
||(!(stoppers^leverPush)
&&popcount(phalanx)>=popcount(leverPush))
||(stoppers==blocked&&r >=RANK_5
&&(shift_bb(Up,support)&~(theirPawns|doubleAttackThem)));
passed&=!(forward_file_bb(Us,s)&ourPawns);
if(passed)
e->passedPawns[Us]|=sq_bb(s);
if(support|phalanx){
int v=Connected[r]*(2+!!phalanx-!!opposed)
+22*popcount(support);
score+=make_score(v,v*(r-2)/4);
}
else if(!neighbours){
if(opposed
&&(ourPawns&forward_file_bb(Them,s))
&&!(theirPawns&adjacent_files_bb(f)))
score-=Doubled;
else
score-=Isolated+(!opposed?WeakUnopposed:0);
}
else if(backward)
score-=Backward+(!opposed&&((s+1)&0x06)?WeakUnopposed:0);
if(!support)
score-=(doubled?Doubled:0)
+(more_than_one(lever)?WeakLever:0);
if(blocked&&r >=RANK_5)
score+=BlockedPawn[r-RANK_5];
}
return score;
}
void pawn_entry_fill(const Position*pos,PawnEntry*e,Key key)
{
e->key=key;
e->blockedCount=0;
e->score=pawn_evaluate(pos,e,WHITE)-pawn_evaluate(pos,e,BLACK);
e->openFiles=popcount(e->semiopenFiles[WHITE]&e->semiopenFiles[BLACK]);
e->passedCount=popcount(e->passedPawns[WHITE]|e->passedPawns[BLACK]);
}
INLINE Score evaluate_shelter(const PawnEntry*pe,const Position*pos,
Square ksq,const Color Us)
{
const Color Them=Us==WHITE?BLACK:WHITE;
Bitboard b=pieces_p(PAWN)&~forward_ranks_bb(Them,rank_of(ksq));
Bitboard ourPawns=b&pieces_c(Us)&~pe->pawnAttacks[Them];
Bitboard theirPawns=b&pieces_c(Them);
Score bonus=make_score(5,5);
File center=clamp(file_of(ksq),FILE_B,FILE_G);
for(File f=center-1;f <=center+1;f++){
b=ourPawns&file_bb(f);
int ourRank=b?relative_rank_s(Us,backmost_sq(Us,b)):0;
b=theirPawns&file_bb(f);
int theirRank=b?relative_rank_s(Us,frontmost_sq(Them,b)):0;
int d=min(f,FILE_H-f);
bonus+=ShelterStrength[d][ourRank];
if(ourRank&&(ourRank==theirRank-1)){
bonus-=BlockedStorm[theirRank];
}else
bonus-=UnblockedStorm[d][theirRank];
}
bonus-=KingOnFile[is_on_semiopen_file(pe,Us,ksq)][is_on_semiopen_file(pe,Them,ksq)];
return bonus;
}
INLINE Score do_king_safety(PawnEntry*pe,const Position*pos,Square ksq,
const Color Us)
{
pe->kingSquares[Us]=ksq;
pe->castlingRights[Us]=can_castle_c(Us);
int minPawnDist;
Bitboard pawns=pieces_cp(Us,PAWN);
if(!pawns)
minPawnDist=6;
else if(pawns&PseudoAttacks[KING][ksq])
minPawnDist=1;
else for(minPawnDist=1;
minPawnDist < 6&&!(DistanceRingBB[ksq][minPawnDist]&pawns);
minPawnDist++);
Score shelter=evaluate_shelter(pe,pos,ksq,Us);
if(can_castle_cr(make_castling_right(Us,KING_SIDE))){
Score s=evaluate_shelter(pe,pos,relative_square(Us,SQ_G1),Us);
if(mg_value(s)> mg_value(shelter))
shelter=s;
}
if(can_castle_cr(make_castling_right(Us,QUEEN_SIDE))){
Score s=evaluate_shelter(pe,pos,relative_square(Us,SQ_C1),Us);
if(mg_value(s)> mg_value(shelter))
shelter=s;
}
return shelter-make_score(0,16*minPawnDist);
}
NOINLINE Score do_king_safety_white(PawnEntry*pe,const Position*pos,
Square ksq)
{
return do_king_safety(pe,pos,ksq,WHITE);
}
NOINLINE Score do_king_safety_black(PawnEntry*pe,const Position*pos,
Square ksq)
{
return do_king_safety(pe,pos,ksq,BLACK);
}
#else
typedef int make_iso_compilers_happy;
#endif
//
#ifndef NNUE_PURE
static int PushToEdges[64];
static int PushToCorners[64];
static const int PushClose[8]={140,120,100,80,60,40,20,0};
static const int PushAway [8]={-20,0,20,40,60,80,100,120};
#ifndef NDEBUG
static bool verify_material(const Position*pos,int c,Value npm,int pawnsCnt)
{
return non_pawn_material_c(c)==npm
&&piece_count(c,PAWN)==pawnsCnt;
}
#endif
static Square normalize(const Position*pos,Color strongSide,Square sq)
{
assert(piece_count(strongSide,PAWN)==1);
if(file_of(square_of(strongSide,PAWN))>=FILE_E)
sq^=0x07;
if(strongSide==BLACK)
sq^=0x38;
return sq;
}
static Key calc_key(const char*code,Color c)
{
Key key=0;
int color=c << 3;
for(;*code;code++)
for(int i=1;;i++)
if(*code==PieceToChar[i]){
key+=matKey[i^color];
break;
}
return key;
}
static EgFunc EvaluateKPK,EvaluateKNNK,EvaluateKNNKP,EvaluateKBNK,
EvaluateKRKP,EvaluateKRKB,EvaluateKRKN,EvaluateKQKP,
EvaluateKQKR,EvaluateKXK;
static EgFunc ScaleKRPKR,ScaleKRPKB,ScaleKBPKB,ScaleKBPKN,ScaleKBPPKB,
ScaleKRPPKRP,ScaleKBPsK,ScaleKQKRPs,ScaleKPKP,ScaleKPsK;
EgFunc*endgame_funcs[NUM_EVAL+NUM_SCALING+6]={
NULL,
&EvaluateKPK,
&EvaluateKNNK,
&EvaluateKNNKP,
&EvaluateKBNK,
&EvaluateKRKP,
&EvaluateKRKB,
&EvaluateKRKN,
&EvaluateKQKP,
&EvaluateKQKR,
&EvaluateKXK,
&ScaleKRPKR,
&ScaleKRPKB,
&ScaleKBPKB,
&ScaleKBPKN,
&ScaleKBPPKB,
&ScaleKRPPKRP,
&ScaleKBPsK,
&ScaleKQKRPs,
&ScaleKPsK,
&ScaleKPKP
};
Key endgame_keys[NUM_EVAL+NUM_SCALING][2];
static const char*endgame_codes[NUM_EVAL+NUM_SCALING]={
"KPk","KNNk","KNNkp","KBNk","KRkp","KRkb","KRkn","KQkp","KQkr",
"KRPkr","KRPkb","KBPkb","KBPkn","KBPPkb","KRPPkrp"
};
void endgames_init(void)
{
for(int i=0;i < NUM_EVAL+NUM_SCALING;i++){
endgame_keys[i][WHITE]=calc_key(endgame_codes[i],WHITE);
endgame_keys[i][BLACK]=calc_key(endgame_codes[i],BLACK);
}
for(int s=0;s < 64;s++){
int f=file_of(s),r=rank_of(s);
int fd=min(f,7-f),rd=min(r,7-r);
PushToEdges[s]=90-(7*fd*fd/2+7*rd*rd/2);
PushToCorners[s]=420*abs(7-r-f);
}
}
static Value EvaluateKXK(const Position*pos,Color strongSide)
{
Color weakSide=!strongSide;
assert(verify_material(pos,weakSide,VALUE_ZERO,0));
assert(!checkers());
if(stm()==weakSide){
ExtMove list[MAX_MOVES];
if(generate_legal(pos,list)==list)
return VALUE_DRAW;
}
Square winnerKSq=square_of(strongSide,KING);
Square loserKSq=square_of(weakSide,KING);
Value result=non_pawn_material_c(strongSide)
+piece_count(strongSide,PAWN)*PawnValueEg
+PushToEdges[loserKSq]
+PushClose[distance(winnerKSq,loserKSq)];
if(pieces_pp(QUEEN,ROOK)
||(pieces_p(BISHOP)&&pieces_p(KNIGHT))
||((pieces_p(BISHOP)&DarkSquares)
&&(pieces_p(BISHOP)&LightSquares)))
result=min(result+VALUE_KNOWN_WIN,VALUE_TB_WIN_IN_MAX_PLY-1);
return strongSide==stm()?result:-result;
}
static Value EvaluateKBNK(const Position*pos,Color strongSide)
{
Color weakSide=!strongSide;
assert(verify_material(pos,strongSide,KnightValueMg+BishopValueMg,0));
assert(verify_material(pos,weakSide,VALUE_ZERO,0));
Square winnerKSq=square_of(strongSide,KING);
Square loserKSq=square_of(weakSide,KING);
Square bishopSq=lsb(pieces_p(BISHOP));
if(opposite_colors(bishopSq,SQ_A1)){
winnerKSq=winnerKSq^0x38;
loserKSq=loserKSq^0x38;
}
Value result=VALUE_KNOWN_WIN+3520
+PushClose[distance(winnerKSq,loserKSq)]
+PushToCorners[loserKSq];
return strongSide==stm()?result:-result;
}
static Value EvaluateKPK(const Position*pos,Color strongSide)
{
Color weakSide=!strongSide;
assert(verify_material(pos,strongSide,VALUE_ZERO,1));
assert(verify_material(pos,weakSide,VALUE_ZERO,0));
Square wksq=normalize(pos,strongSide,square_of(strongSide,KING));
Square bksq=normalize(pos,strongSide,square_of(weakSide,KING));
Square psq=normalize(pos,strongSide,lsb(pieces_p(PAWN)));
Color us=strongSide==stm()?WHITE:BLACK;
if(!bitbases_probe(wksq,psq,bksq,us))
return VALUE_DRAW;
Value result=VALUE_KNOWN_WIN+PawnValueEg+(Value)(rank_of(psq));
return strongSide==stm()?result:-result;
}
static Value EvaluateKRKP(const Position*pos,Color strongSide)
{
Color weakSide=!strongSide;
assert(verify_material(pos,strongSide,RookValueMg,0));
assert(verify_material(pos,weakSide,VALUE_ZERO,1));
Square wksq=relative_square(strongSide,square_of(strongSide,KING));
Square bksq=relative_square(strongSide,square_of(weakSide,KING));
Square rsq=relative_square(strongSide,lsb(pieces_p(ROOK)));
Square psq=relative_square(strongSide,lsb(pieces_p(PAWN)));
Square queeningSq=make_square(file_of(psq),RANK_1);
Value result;
if(forward_file_bb(WHITE,wksq)&sq_bb(psq))
result=RookValueEg-distance(wksq,psq);
else if(distance(bksq,psq)>=3+(stm()==weakSide)
&&distance(bksq,rsq)>=3)
result=RookValueEg-distance(wksq,psq);
else if(rank_of(bksq)<=RANK_3
&&distance(bksq,psq)==1
&&rank_of(wksq)>=RANK_4
&&distance(wksq,psq)> 2+(stm()==strongSide))
result=(Value)(80)-8*distance(wksq,psq);
else
result=(Value)(200)-8*(distance(wksq,psq+SOUTH)
-distance(bksq,psq+SOUTH)
-distance(psq,queeningSq));
return strongSide==stm()?result:-result;
}
static Value EvaluateKRKB(const Position*pos,Color strongSide)
{
Color weakSide=!strongSide;
assert(verify_material(pos,strongSide,RookValueMg,0));
assert(verify_material(pos,weakSide,BishopValueMg,0));
Value result=(Value)PushToEdges[square_of(weakSide,KING)];
return strongSide==stm()?result:-result;
}
static Value EvaluateKRKN(const Position*pos,Color strongSide)
{
Color weakSide=!strongSide;
assert(verify_material(pos,strongSide,RookValueMg,0));
assert(verify_material(pos,weakSide,KnightValueMg,0));
Square bksq=square_of(weakSide,KING);
Square bnsq=lsb(pieces_p(KNIGHT));
Value result=(Value)PushToEdges[bksq]+PushAway[distance(bksq,bnsq)];
return strongSide==stm()?result:-result;
}
static Value EvaluateKQKP(const Position*pos,Color strongSide)
{
Color weakSide=!strongSide;
assert(verify_material(pos,strongSide,QueenValueMg,0));
assert(verify_material(pos,weakSide,VALUE_ZERO,1));
Square winnerKSq=square_of(strongSide,KING);
Square loserKSq=square_of(weakSide,KING);
Square pawnSq=lsb(pieces_p(PAWN));
Value result=(Value)PushClose[distance(winnerKSq,loserKSq)];
if(relative_rank_s(weakSide,pawnSq)!=RANK_7
||distance(loserKSq,pawnSq)!=1
||((FileBBB|FileDBB|FileEBB|FileGBB)&sq_bb(pawnSq)))
result+=QueenValueEg-PawnValueEg;
return strongSide==stm()?result:-result;
}
static Value EvaluateKQKR(const Position*pos,Color strongSide)
{
Color weakSide=!strongSide;
assert(verify_material(pos,strongSide,QueenValueMg,0));
assert(verify_material(pos,weakSide,RookValueMg,0));
Square winnerKSq=square_of(strongSide,KING);
Square loserKSq=square_of(weakSide,KING);
Value result=QueenValueEg
-RookValueEg
+PushToEdges[loserKSq]
+PushClose[distance(winnerKSq,loserKSq)];
return strongSide==stm()?result:-result;
}
static Value EvaluateKNNKP(const Position*pos,Color strongSide)
{
Color weakSide=!strongSide;
assert(verify_material(pos,strongSide,2*KnightValueMg,0));
assert(verify_material(pos,weakSide,VALUE_ZERO,1));
Value result=PawnValueEg
+2*PushToEdges[square_of(weakSide,KING)]
-10*relative_rank_s(weakSide,square_of(weakSide,PAWN));
return strongSide==stm()?result:-result;
}
Value EvaluateKNNK(const Position*pos,Color strongSide)
{
(void)pos,(void)strongSide;
return VALUE_DRAW;
}
int ScaleKBPsK(const Position*pos,Color strongSide)
{
Color weakSide=!strongSide;
assert(non_pawn_material_c(strongSide)==BishopValueMg);
assert(pieces_cp(strongSide,PAWN));
Bitboard pawns=pieces_cp(strongSide,PAWN);
File pawnsFile=file_of(lsb(pawns));
if((pawnsFile==FILE_A||pawnsFile==FILE_H)
&&!(pawns&~file_bb(pawnsFile))){
Square bishopSq=square_of(strongSide,BISHOP);
Square queeningSq=relative_square(strongSide,make_square(pawnsFile,RANK_8));
Square kingSq=square_of(weakSide,KING);
if(opposite_colors(queeningSq,bishopSq)
&&distance(queeningSq,kingSq)<=1)
return SCALE_FACTOR_DRAW;
}
if((pawnsFile==FILE_B||pawnsFile==FILE_G)
&&!(pieces_p(PAWN)&~file_bb(pawnsFile))
&&non_pawn_material_c(weakSide)==0
&&piece_count(weakSide,PAWN))
{
Square weakPawnSq=backmost_sq(weakSide,pieces_cp(weakSide,PAWN));
Square strongKingSq=square_of(strongSide,KING);
Square weakKingSq=square_of(weakSide,KING);
Square bishopSq=square_of(strongSide,BISHOP);
if(relative_rank_s(strongSide,weakPawnSq)==RANK_7
&&(pieces_cp(strongSide,PAWN)&sq_bb(weakPawnSq+pawn_push(weakSide)))
&&(opposite_colors(bishopSq,weakPawnSq)||piece_count(strongSide,PAWN)==1)){
unsigned strongKingDist=distance(weakPawnSq,strongKingSq);
unsigned weakKingDist=distance(weakPawnSq,weakKingSq);
if(relative_rank_s(strongSide,weakKingSq)>=RANK_7
&&weakKingDist <=2
&&weakKingDist <=strongKingDist)
return SCALE_FACTOR_DRAW;
}
}
return SCALE_FACTOR_NONE;
}
static int ScaleKQKRPs(const Position*pos,Color strongSide)
{
Color weakSide=!strongSide;
assert(verify_material(pos,strongSide,QueenValueMg,0));
assert(piece_count(weakSide,ROOK)==1);
assert(pieces_cp(weakSide,PAWN));
Square kingSq=square_of(weakSide,KING);
Square rsq=lsb(pieces_p(ROOK));
if(relative_rank_s(weakSide,kingSq)<=RANK_2
&&relative_rank_s(weakSide,square_of(strongSide,KING))>=RANK_4
&&relative_rank_s(weakSide,rsq)==RANK_3
&&(pieces_p(PAWN)
&attacks_from_king(kingSq)
&attacks_from_pawn(rsq,strongSide)))
return SCALE_FACTOR_DRAW;
return SCALE_FACTOR_NONE;
}
static int ScaleKRPKR(const Position*pos,Color strongSide)
{
Color weakSide=!strongSide;
assert(verify_material(pos,strongSide,RookValueMg,1));
assert(verify_material(pos,weakSide,RookValueMg,0));
Square wksq=normalize(pos,strongSide,square_of(strongSide,KING));
Square bksq=normalize(pos,strongSide,square_of(weakSide,KING));
Square wrsq=normalize(pos,strongSide,square_of(strongSide,ROOK));
Square wpsq=normalize(pos,strongSide,lsb(pieces_p(PAWN)));
Square brsq=normalize(pos,strongSide,square_of(weakSide,ROOK));
File f=file_of(wpsq);
Rank r=rank_of(wpsq);
Square queeningSq=make_square(f,RANK_8);
signed tempo=(stm()==strongSide);
if(r <=RANK_5
&&distance(bksq,queeningSq)<=1
&&wksq <=SQ_H5
&&(rank_of(brsq)==RANK_6||(r <=RANK_3&&rank_of(wrsq)!=RANK_6)))
return SCALE_FACTOR_DRAW;
if(r==RANK_6
&&distance(bksq,queeningSq)<=1
&&rank_of(wksq)+tempo <=RANK_6
&&(rank_of(brsq)==RANK_1||(!tempo&&distance_f(brsq,wpsq)>=3)))
return SCALE_FACTOR_DRAW;
if(r >=RANK_6
&&bksq==queeningSq
&&rank_of(brsq)==RANK_1
&&(!tempo||distance(wksq,wpsq)>=2))
return SCALE_FACTOR_DRAW;
if(wpsq==SQ_A7
&&wrsq==SQ_A8
&&(bksq==SQ_H7||bksq==SQ_G7)
&&file_of(brsq)==FILE_A
&&(rank_of(brsq)<=RANK_3||file_of(wksq)>=FILE_D||rank_of(wksq)<=RANK_5))
return SCALE_FACTOR_DRAW;
if(r <=RANK_5
&&bksq==wpsq+NORTH
&&distance(wksq,wpsq)-tempo >=2
&&distance(wksq,brsq)-tempo >=2)
return SCALE_FACTOR_DRAW;
if(r==RANK_7
&&f!=FILE_A
&&file_of(wrsq)==f
&&wrsq!=queeningSq
&&(distance(wksq,queeningSq)< distance(bksq,queeningSq)-2+tempo)
&&(distance(wksq,queeningSq)< distance(bksq,wrsq)+tempo))
return SCALE_FACTOR_MAX-2*distance(wksq,queeningSq);
if(f!=FILE_A
&&file_of(wrsq)==f
&&wrsq < wpsq
&&(distance(wksq,queeningSq)< distance(bksq,queeningSq)-2+tempo)
&&(distance(wksq,wpsq+NORTH)< distance(bksq,wpsq+NORTH)-2+tempo)
&&(distance(bksq,wrsq)+tempo >=3
||(distance(wksq,queeningSq)< distance(bksq,wrsq)+tempo
&&(distance(wksq,wpsq+NORTH)< distance(bksq,wrsq)+tempo))))
return SCALE_FACTOR_MAX
-8*distance(wpsq,queeningSq)
-2*distance(wksq,queeningSq);
if(r <=RANK_4&&bksq > wpsq){
if(file_of(bksq)==file_of(wpsq))
return 10;
if(distance_f(bksq,wpsq)==1
&&distance(wksq,bksq)> 2)
return 24-2*distance(wksq,bksq);
}
return SCALE_FACTOR_NONE;
}
static int ScaleKRPKB(const Position*pos,Color strongSide)
{
Color weakSide=!strongSide;
assert(verify_material(pos,strongSide,RookValueMg,1));
assert(verify_material(pos,weakSide,BishopValueMg,0));
if(pieces_p(PAWN)&(FileABB|FileHBB)){
Square ksq=square_of(weakSide,KING);
Square bsq=lsb(pieces_p(BISHOP));
Square psq=lsb(pieces_p(PAWN));
Rank rk=relative_rank_s(strongSide,psq);
Square push=pawn_push(strongSide);
if(rk==RANK_5&&!opposite_colors(bsq,psq)){
int d=distance(psq+3*push,ksq);
if(d <=2&&!(d==0&&ksq==square_of(strongSide,KING)+2*push))
return 24;
else
return 48;
}
if(rk==RANK_6
&&distance(psq+2*push,ksq)<=1
&&(PseudoAttacks[BISHOP][bsq]&sq_bb(psq+push))
&&distance_f(bsq,psq)>=2)
return 8;
}
return SCALE_FACTOR_NONE;
}
static int ScaleKRPPKRP(const Position*pos,Color strongSide)
{
Color weakSide=!strongSide;
assert(verify_material(pos,strongSide,RookValueMg,2));
assert(verify_material(pos,weakSide,RookValueMg,1));
Square wpsq1=lsb(pieces_cp(strongSide,PAWN));
Square wpsq2=msb(pieces_cp(strongSide,PAWN));
Square bksq=square_of(weakSide,KING);
if(pawn_passed(pos,strongSide,wpsq1)||pawn_passed(pos,strongSide,wpsq2))
return SCALE_FACTOR_NONE;
Rank r=max(relative_rank_s(strongSide,wpsq1),relative_rank_s(strongSide,wpsq2));
if(distance_f(bksq,wpsq1)<=1
&&distance_f(bksq,wpsq2)<=1
&&relative_rank_s(strongSide,bksq)> r)
{
assert(r > RANK_1&&r < RANK_7);
return 7*r;
}
return SCALE_FACTOR_NONE;
}
static int ScaleKPsK(const Position*pos,Color strongSide)
{
Color weakSide=!strongSide;
assert(non_pawn_material_c(strongSide)==0);
assert(piece_count(strongSide,PAWN)>=2);
assert(verify_material(pos,weakSide,VALUE_ZERO,0));
Square ksq=square_of(weakSide,KING);
Bitboard pawns=pieces_cp(strongSide,PAWN);
if(!(pawns&~(FileABB|FileHBB))
&&!(pawns&~passed_pawn_span(weakSide,ksq)))
return SCALE_FACTOR_DRAW;
return SCALE_FACTOR_NONE;
}
static int ScaleKBPKB(const Position*pos,Color strongSide)
{
Color weakSide=!strongSide;
assert(verify_material(pos,strongSide,BishopValueMg,1));
assert(verify_material(pos,weakSide,BishopValueMg,0));
Square pawnSq=lsb(pieces_p(PAWN));
Square strongBishopSq=square_of(strongSide,BISHOP);
Square weakBishopSq=square_of(weakSide,BISHOP);
Square weakKingSq=square_of(weakSide,KING);
if(file_of(weakKingSq)==file_of(pawnSq)
&&relative_rank_s(strongSide,pawnSq)< relative_rank_s(strongSide,weakKingSq)
&&(opposite_colors(weakKingSq,strongBishopSq)
||relative_rank_s(strongSide,weakKingSq)<=RANK_6))
return SCALE_FACTOR_DRAW;
if(opposite_colors(strongBishopSq,weakBishopSq))
return SCALE_FACTOR_DRAW;
return SCALE_FACTOR_NONE;
}
static int ScaleKBPPKB(const Position*pos,Color strongSide)
{
Color weakSide=!strongSide;
assert(verify_material(pos,strongSide,BishopValueMg,2));
assert(verify_material(pos,weakSide,BishopValueMg,0));
Square wbsq=square_of(strongSide,BISHOP);
Square bbsq=square_of(weakSide,BISHOP);
if(!opposite_colors(wbsq,bbsq))
return SCALE_FACTOR_NONE;
Square ksq=square_of(weakSide,KING);
Square psq1=lsb(pieces_cp(strongSide,PAWN));
Square psq2=msb(pieces_cp(strongSide,PAWN));
int r1=rank_of(psq1);
int r2=rank_of(psq2);
Square blockSq1,blockSq2;
if(relative_rank_s(strongSide,psq1)> relative_rank_s(strongSide,psq2)){
blockSq1=psq1+pawn_push(strongSide);
blockSq2=make_square(file_of(psq2),rank_of(psq1));
}else{
blockSq1=psq2+pawn_push(strongSide);
blockSq2=make_square(file_of(psq1),rank_of(psq2));
}
switch(distance_f(psq1,psq2)){
case 0:
if(file_of(ksq)==file_of(blockSq1)
&&relative_rank_s(strongSide,ksq)>=relative_rank_s(strongSide,blockSq1)
&&opposite_colors(ksq,wbsq))
return SCALE_FACTOR_DRAW;
else
return SCALE_FACTOR_NONE;
case 1:
if(ksq==blockSq1
&&opposite_colors(ksq,wbsq)
&&(bbsq==blockSq2
||(attacks_from_bishop(blockSq2)&pieces_cp(weakSide,BISHOP))
||distance(r1,r2)>=2))
return SCALE_FACTOR_DRAW;
else if(ksq==blockSq2
&&opposite_colors(ksq,wbsq)
&&(bbsq==blockSq1
||(attacks_from_bishop(blockSq1)
&pieces_cp(weakSide,BISHOP))))
return SCALE_FACTOR_DRAW;
else
return SCALE_FACTOR_NONE;
default:
return SCALE_FACTOR_NONE;
}
}
static int ScaleKBPKN(const Position*pos,Color strongSide)
{
Color weakSide=!strongSide;
assert(verify_material(pos,strongSide,BishopValueMg,1));
assert(verify_material(pos,weakSide,KnightValueMg,0));
Square pawnSq=lsb(pieces_p(PAWN));
Square strongBishopSq=lsb(pieces_p(BISHOP));
Square weakKingSq=square_of(weakSide,KING);
if(file_of(weakKingSq)==file_of(pawnSq)
&&relative_rank_s(strongSide,pawnSq)< relative_rank_s(strongSide,weakKingSq)
&&(opposite_colors(weakKingSq,strongBishopSq)
||relative_rank_s(strongSide,weakKingSq)<=RANK_6))
return SCALE_FACTOR_DRAW;
return SCALE_FACTOR_NONE;
}
static int ScaleKPKP(const Position*pos,Color strongSide)
{
Color weakSide=!strongSide;
assert(verify_material(pos,strongSide,VALUE_ZERO,1));
assert(verify_material(pos,weakSide,VALUE_ZERO,1));
Square wksq=normalize(pos,strongSide,square_of(strongSide,KING));
Square bksq=normalize(pos,strongSide,square_of(weakSide,KING));
Square psq=normalize(pos,strongSide,square_of(strongSide,PAWN));
Color us=strongSide==stm()?WHITE:BLACK;
if(rank_of(psq)>=RANK_5&&file_of(psq)!=FILE_A)
return SCALE_FACTOR_NONE;
return bitbases_probe(wksq,psq,bksq,us)?SCALE_FACTOR_NONE:SCALE_FACTOR_DRAW;
}
#else
typedef int make_iso_compilers_happy;
#endif
//
#ifdef NNUE
#endif
#ifndef NNUE_PURE
struct EvalInfo{
MaterialEntry*me;
PawnEntry*pe;
Bitboard mobilityArea[2];
Bitboard attackedBy[2][8];
Bitboard attackedBy2[2];
Bitboard kingRing[2];
int kingAttackersCount[2];
int kingAttackersWeight[2];
int kingAttacksCount[2];
};
typedef struct EvalInfo EvalInfo;
static const Bitboard KingFlank[8]={
QueenSide^FileDBB,QueenSide,QueenSide,CenterFiles,
CenterFiles,KingSide,KingSide,KingSide^FileEBB
};
enum{
LazyThreshold1=1565,
LazyThreshold2=1102,
SpaceThreshold=11551,
NNUEThreshold1=682,
NNUEThreshold2=176
};
static const int KingAttackWeights[8]={0,0,81,52,44,10};
static const int SafeCheck[][2]={
{0},{0},{803,1292},{639,974},{1087,1878},{759,1132}
};
#define V(v) (Value)(v)
#define S(mg,eg) make_score(mg,eg)
static const Score MobilityBonus[4][32]={
{S(-62,-79),S(-53,-57),S(-12,-31),S(-3,-17),S(3,7),S(12,13),
S(21,16),S(28,21),S(37,26)},
{S(-47,-59),S(-20,-25),S(14,-8),S(29,12),S(39,21),S(53,40),
S(53,56),S(60,58),S(62,65),S(69,72),S(78,78),S(83,87),
S(91,88),S(96,98)},
{S(-60,-82),S(-24,-15),S(0,17),S(3,43),S(4,72),S(14,100),
S(20,102),S(30,122),S(41,133),S(41,139),S(41,153),S(45,160),
S(57,165),S(58,170),S(67,175)},
{S(-29,-49),S(-16,-29),S(-8,-8),S(-8,17),S(18,39),S(25,54),
S(23,59),S(37,73),S(41,76),S(54,95),S(65,95),S(68,101),
S(69,124),S(70,128),S(70,132),S(70,133),S(71,136),S(72,140),
S(74,147),S(76,149),S(90,153),S(104,169),S(105,171),S(106,171),
S(112,178),S(114,185),S(114,187),S(119,221)}
};
static const Score BishopPawns[8]={
S(3,8),S(3,9),S(2,8),S(3,8),S(3,8),S(2,8),S(3,9),S(3,8)
};
static const Score RookOnClosedFile=S(10,5);
static const Score RookOnOpenFile[2]={S(19,6),S(47,26)};
static const Score ThreatByMinor[8]={
S(0,0),S(5,32),S(55,41),S(77,56),S(89,119),S(79,162)
};
static const Score ThreatByRook[8]={
S(0,0),S(3,44),S(37,68),S(42,60),S(0,39),S(58,43)
};
static const Value PassedRank[2][8]={
{V(0),V(7),V(16),V(17),V(64),V(170),V(278)},
{V(0),V(27),V(32),V(40),V(71),V(174),V(262)}
};
static const Score PassedFile[8]={
S(0,0),S(11,8),S(22,16),S(33,24),
S(33,24),S(22,16),S(11,8),S(0,0)
};
static const Score BishopKingProtector=S(6,9);
static const Score BishopOnKingRing=S(24,0);
static const Score BishopOutpost=S(31,24);
static const Score BishopXRayPawns=S(4,5);
static const Score CorneredBishop=S(50,50);
static const Score FlankAttacks=S(8,0);
static const Score Hanging=S(69,36);
static const Score KnightKingProtector=S(8,9);
static const Score KnightOnQueen=S(16,11);
static const Score KnightOutpost=S(57,38);
static const Score LongDiagonalBishop=S(45,0);
static const Score MinorBehindPawn=S(18,3);
static const Score PawnlessFlank=S(17,95);
static const Score ReachableOutpost=S(31,22);
static const Score RestrictedPiece=S(7,7);
static const Score RookOnKingRing=S(16,0);
static const Score SliderOnQueen=S(60,18);
static const Score ThreatByKing=S(24,89);
static const Score ThreatByPawnPush=S(48,39);
static const Score ThreatBySafePawn=S(173,94);
static const Score TrappedRook=S(55,13);
static const Score UncontestedOutpost=S(1,10);
static const Score WeakQueen=S(56,15);
static const Score WeakQueenProtection=S(14,0);
static const Value CorneredBishopV=50;
#undef S
#undef V
INLINE void evalinfo_init(const Position*pos,EvalInfo*ei,const Color Us)
{
const Color Them=Us==WHITE?BLACK:WHITE;
const int Down=Us==WHITE?SOUTH:NORTH;
const Bitboard LowRanks=Us==WHITE?Rank2BB|Rank3BB
:Rank7BB|Rank6BB;
const Square ksq=square_of(Us,KING);
Bitboard dblAttackByPawn=pawn_double_attacks_bb(pieces_cp(Us,PAWN),Us);
Bitboard b=pieces_cp(Us,PAWN)&(shift_bb(Down,pieces())|LowRanks);
ei->mobilityArea[Us]=~(b|pieces_cpp(Us,KING,QUEEN)|blockers_for_king(pos,Us)|ei->pe->pawnAttacks[Them]);
b=ei->attackedBy[Us][KING]=attacks_from_king(square_of(Us,KING));
ei->attackedBy[Us][PAWN]=ei->pe->pawnAttacks[Us];
ei->attackedBy[Us][0]=b|ei->attackedBy[Us][PAWN];
ei->attackedBy2[Us]=(b&ei->attackedBy[Us][PAWN])|dblAttackByPawn;
Square s=make_square(clamp(file_of(ksq),FILE_B,FILE_G),
clamp(rank_of(ksq),RANK_2,RANK_7));
ei->kingRing[Us]=PseudoAttacks[KING][s]|sq_bb(s);
ei->kingAttackersCount[Them]=popcount(ei->kingRing[Us]&ei->pe->pawnAttacks[Them]);
ei->kingAttacksCount[Them]=ei->kingAttackersWeight[Them]=0;
ei->kingRing[Us]&=~dblAttackByPawn;
}
INLINE Score evaluate_pieces(const Position*pos,EvalInfo*ei,Score*mobility,
const Color Us,const int Pt)
{
const Color Them=Us==WHITE?BLACK:WHITE;
const int Down=Us==WHITE?SOUTH:NORTH;
const Bitboard OutpostRanks=Us==WHITE?Rank4BB|Rank5BB|Rank6BB
:Rank5BB|Rank4BB|Rank3BB;
Bitboard b,bb;
Square s;
Score score=SCORE_ZERO;
ei->attackedBy[Us][Pt]=0;
loop_through_pieces(Us,Pt,s){
b=Pt==BISHOP?attacks_bb_bishop(s,pieces()^pieces_p(QUEEN))
:Pt==ROOK?attacks_bb_rook(s,
pieces()^pieces_p(QUEEN)^pieces_cp(Us,ROOK))
:attacks_from(Pt,s);
if(blockers_for_king(pos,Us)&sq_bb(s))
b&=LineBB[square_of(Us,KING)][s];
ei->attackedBy2[Us]|=ei->attackedBy[Us][0]&b;
ei->attackedBy[Us][Pt]|=b;
ei->attackedBy[Us][0]|=b;
if(b&ei->kingRing[Them]){
ei->kingAttackersCount[Us]++;
ei->kingAttackersWeight[Us]+=KingAttackWeights[Pt];
ei->kingAttacksCount[Us]+=popcount(b&ei->attackedBy[Them][KING]);
}
else if(Pt==ROOK&&(file_bb_s(s)&ei->kingRing[Them]))
score+=RookOnKingRing;
else if(Pt==BISHOP&&(attacks_bb_bishop(s,pieces_p(PAWN))&ei->kingRing[Them]))
score+=BishopOnKingRing;
int mob=popcount(b&ei->mobilityArea[Us]);
mobility[Us]+=MobilityBonus[Pt-2][mob];
if(Pt==BISHOP||Pt==KNIGHT){
bb=OutpostRanks&(ei->attackedBy[Us][PAWN]|shift_bb(Down,pieces_p(PAWN)))
&~ei->pe->pawnAttacksSpan[Them];
Bitboard targets=pieces_c(Them)&~pieces_p(PAWN);
if(Pt==KNIGHT
&&(bb&sq_bb(s)&~CenterFiles)
&&!(b&targets)
&&(!more_than_one(targets&(sq_bb(s)&QueenSide?QueenSide:KingSide))))
score+=UncontestedOutpost*popcount(pieces_p(PAWN)&(sq_bb(s)&QueenSide?QueenSide:KingSide));
else if(bb&sq_bb(s))
score+=Pt==KNIGHT?KnightOutpost:BishopOutpost;
else if(Pt==KNIGHT&&bb&b&~pieces_c(Us))
score+=ReachableOutpost;
if(shift_bb(Down,pieces_p(PAWN))&sq_bb(s))
score+=MinorBehindPawn;
score-=(Pt==KNIGHT?KnightKingProtector:BishopKingProtector)*
distance(s,square_of(Us,KING));
if(Pt==BISHOP){
Bitboard blocked=pieces_cp(Us,PAWN)&shift_bb(Down,pieces());
score-=BishopPawns[file_of(s)]
*pawns_on_same_color_squares(ei->pe,Us,s)
*(!(ei->attackedBy[Us][PAWN]&sq_bb(s))
+popcount(blocked&CenterFiles));
score-=BishopXRayPawns*popcount(PseudoAttacks[BISHOP][s]&pieces_cp(Them,PAWN));
if(more_than_one(attacks_bb_bishop(s,pieces_p(PAWN))&Center))
score+=LongDiagonalBishop;
if(is_chess960()
&&(s==relative_square(Us,SQ_A1)||s==relative_square(Us,SQ_H1)))
{
Square d=pawn_push(Us)+(file_of(s)==FILE_A?EAST:WEST);
if(piece_on(s+d)==make_piece(Us,PAWN))
score-=!is_empty(s+d+pawn_push(Us))?CorneredBishop*4
:CorneredBishop*3;
}
}
}
if(Pt==ROOK){
if(is_on_semiopen_file(ei->pe,Us,s))
score+=RookOnOpenFile[is_on_semiopen_file(ei->pe,Them,s)];
else{
if(pieces_cp(Us,PAWN)&shift_bb(Down,pieces())&file_bb_s(s))
score-=RookOnClosedFile;
if(mob <=3){
File kf=file_of(square_of(Us,KING));
if((kf < FILE_E)==(file_of(s)< kf))
score-=TrappedRook*(1+!can_castle_c(Us));
}
}
}
if(Pt==QUEEN){
Bitboard pinners;
if(slider_blockers(pos,pieces_cpp(Them,ROOK,BISHOP),s,&pinners))
score-=WeakQueen;
}
}
return score;
}
INLINE Score evaluate_king(const Position*pos,EvalInfo*ei,Score*mobility,
const Color Us)
{
const Color Them=Us==WHITE?BLACK:WHITE;
const Bitboard Camp=Us==WHITE
?AllSquares^Rank6BB^Rank7BB^Rank8BB
:AllSquares^Rank1BB^Rank2BB^Rank3BB;
const Square ksq=square_of(Us,KING);
Bitboard weak,b1,b2,b3,safe,unsafeChecks=0;
Bitboard rookChecks,queenChecks,bishopChecks,knightChecks;
int kingDanger=0;
Score score=Us==WHITE?king_safety_white(ei->pe,pos,ksq)
:king_safety_black(ei->pe,pos,ksq);
weak=ei->attackedBy[Them][0]
&~ei->attackedBy2[Us]
&(~ei->attackedBy[Us][0]
|ei->attackedBy[Us][KING]|ei->attackedBy[Us][QUEEN]);
safe=~pieces_c(Them);
safe&=~ei->attackedBy[Us][0]|(weak&ei->attackedBy2[Them]);
b1=attacks_bb_rook(ksq,pieces()^pieces_cp(Us,QUEEN));
b2=attacks_bb_bishop(ksq,pieces()^pieces_cp(Us,QUEEN));
rookChecks=b1&ei->attackedBy[Them][ROOK]&safe;
if(rookChecks)
kingDanger+=SafeCheck[ROOK][more_than_one(rookChecks)];
else
unsafeChecks|=b1&ei->attackedBy[Them][ROOK];
queenChecks=(b1|b2)&ei->attackedBy[Them][QUEEN]&safe
&~(ei->attackedBy[Us][QUEEN]|rookChecks);
if(queenChecks)
kingDanger+=SafeCheck[QUEEN][more_than_one(queenChecks)];
bishopChecks=b2&ei->attackedBy[Them][BISHOP]&safe
&~queenChecks;
if(bishopChecks)
kingDanger+=SafeCheck[BISHOP][more_than_one(bishopChecks)];
else
unsafeChecks|=b2&ei->attackedBy[Them][BISHOP];
knightChecks=attacks_from_knight(ksq)&ei->attackedBy[Them][KNIGHT];
if(knightChecks&safe)
kingDanger+=SafeCheck[KNIGHT][more_than_one(knightChecks&safe)];
else
unsafeChecks|=knightChecks;
b1=ei->attackedBy[Them][0]&KingFlank[file_of(ksq)]&Camp;
b2=b1&ei->attackedBy2[Them];
b3=ei->attackedBy[Us][0]&KingFlank[file_of(ksq)]&Camp;
int kingFlankAttack=popcount(b1)+popcount(b2);
int kingFlankDefense=popcount(b3);
kingDanger+=ei->kingAttackersCount[Them]*ei->kingAttackersWeight[Them]
+183*popcount(ei->kingRing[Us]&weak)
+148*popcount(unsafeChecks)
+98*popcount(blockers_for_king(pos,Us))
+69*ei->kingAttacksCount[Them]
+3*kingFlankAttack*kingFlankAttack/8
+mg_value(mobility[Them]-mobility[Us])
-873*!pieces_cp(Them,QUEEN)
-100*!!(ei->attackedBy[Us][KNIGHT]&ei->attackedBy[Us][KING])
-6*mg_value(score)/8
-4*kingFlankDefense
+37;
if(kingDanger > 100)
score-=make_score(kingDanger*kingDanger/4096,kingDanger/16);
if(!(pieces_p(PAWN)&KingFlank[file_of(ksq)]))
score-=PawnlessFlank;
score-=FlankAttacks*kingFlankAttack;
return score;
}
INLINE Score evaluate_threats(const Position*pos,EvalInfo*ei,const Color Us)
{
const Color Them=Us==WHITE?BLACK:WHITE;
const int Up=Us==WHITE?NORTH:SOUTH;
const Bitboard TRank3BB=Us==WHITE?Rank3BB:Rank6BB;
enum{Minor,Rook};
Bitboard b,weak,defended,nonPawnEnemies,stronglyProtected,safe;
Score score=SCORE_ZERO;
nonPawnEnemies=pieces_c(Them)&~pieces_p(PAWN);
stronglyProtected=ei->attackedBy[Them][PAWN]
|(ei->attackedBy2[Them]&~ei->attackedBy2[Us]);
defended=nonPawnEnemies&stronglyProtected;
weak=pieces_c(Them)&~stronglyProtected&ei->attackedBy[Us][0];
if(defended|weak){
b=(defended|weak)&(ei->attackedBy[Us][KNIGHT]|ei->attackedBy[Us][BISHOP]);
while(b)
score+=ThreatByMinor[piece_on(pop_lsb(&b))-8*Them];
b=weak&ei->attackedBy[Us][ROOK];
while(b)
score+=ThreatByRook[piece_on(pop_lsb(&b))-8*Them];
if(weak&ei->attackedBy[Us][KING])
score+=ThreatByKing;
b=~ei->attackedBy[Them][0]
|(nonPawnEnemies&ei->attackedBy2[Us]);
score+=Hanging*popcount(weak&b);
score+=WeakQueenProtection*popcount(weak&ei->attackedBy[Them][QUEEN]);
}
b=ei->attackedBy[Them][0]
&~stronglyProtected
&ei->attackedBy[Us][0];
score+=RestrictedPiece*popcount(b);
safe=~ei->attackedBy[Them][0]|ei->attackedBy[Us][0];
b=pieces_cp(Us,PAWN)&safe;
b=pawn_attacks_bb(b,Us)&nonPawnEnemies;
score+=ThreatBySafePawn*popcount(b);
b=shift_bb(Up,pieces_cp(Us,PAWN))&~pieces();
b|=shift_bb(Up,b&TRank3BB)&~pieces();
b&=~ei->attackedBy[Them][PAWN]&safe;
b=pawn_attacks_bb(b,Us)&nonPawnEnemies;
score+=ThreatByPawnPush*popcount(b);
if(piece_count(Them,QUEEN)==1){
bool queenImbalance=!pieces_cp(Us,QUEEN);
Square s=square_of(Them,QUEEN);
safe=ei->mobilityArea[Us]
&~pieces_cp(Us,PAWN)
&~stronglyProtected;
b=ei->attackedBy[Us][KNIGHT]&attacks_from_knight(s);
score+=KnightOnQueen*popcount(b&safe)*(1+queenImbalance);
b=(ei->attackedBy[Us][BISHOP]&attacks_from_bishop(s))
|(ei->attackedBy[Us][ROOK ]&attacks_from_rook(s));
score+=SliderOnQueen*popcount(b&safe&ei->attackedBy2[Us])*(1+queenImbalance);
}
return score;
}
INLINE int capped_distance(Square s1,Square s2)
{
return min(distance(s1,s2),5);
}
INLINE Score evaluate_passed(const Position*pos,EvalInfo*ei,const Color Us)
{
const Color Them=Us==WHITE?BLACK:WHITE;
const int Up=Us==WHITE?NORTH:SOUTH;
const int Down=Us==WHITE?SOUTH:NORTH;
Bitboard b,bb,squaresToQueen,unsafeSquares,blockedPassers,helpers;
Score score=SCORE_ZERO;
b=ei->pe->passedPawns[Us];
blockedPassers=b&shift_bb(Down,pieces_cp(Them,PAWN));
if(blockedPassers){
helpers=shift_bb(Up,pieces_cp(Us,PAWN))
&~pieces_c(Them)
&(~ei->attackedBy2[Them]|ei->attackedBy[Us][0]);
b&=~blockedPassers
|shift_bb(WEST,helpers)
|shift_bb(EAST,helpers);
}
while(b){
Square s=pop_lsb(&b);
assert(!(pieces_cp(Them,PAWN)&forward_file_bb(Us,s+Up)));
int r=relative_rank_s(Us,s);
Value mbonus=PassedRank[MG][r],ebonus=PassedRank[EG][r];
if(r > RANK_3){
int w=5*r-13;
Square blockSq=s+Up;
ebonus+=((capped_distance(square_of(Them,KING),blockSq)*19)/4
-capped_distance(square_of(Us,KING),blockSq)*2)*w;
if(r!=RANK_7)
ebonus-=capped_distance(square_of(Us,KING),blockSq+Up)*w;
if(is_empty(blockSq)){
squaresToQueen=forward_file_bb(Us,s);
unsafeSquares=passed_pawn_span(Us,s);
bb=forward_file_bb(Them,s)&pieces_pp(ROOK,QUEEN);
if(!(pieces_c(Them)&bb))
unsafeSquares&=ei->attackedBy[Them][0]|pieces_c(Them);
int k=!unsafeSquares?36
:!(unsafeSquares&~ei->attackedBy[Us][PAWN])?30
:!(unsafeSquares&squaresToQueen)?17
:!(unsafeSquares&sq_bb(blockSq))?7:0;
if((pieces_c(Us)&bb)||(ei->attackedBy[Us][0]&sq_bb(blockSq)))
k+=5;
mbonus+=k*w,ebonus+=k*w;
}
}
score+=make_score(mbonus,ebonus)-PassedFile[file_of(s)];
}
return score;
}
INLINE Score evaluate_space(const Position*pos,EvalInfo*ei,const Color Us)
{
if(non_pawn_material()< SpaceThreshold)
return SCORE_ZERO;
const Color Them=Us==WHITE?BLACK:WHITE;
const int Down=Us==WHITE?SOUTH:NORTH;
const Bitboard SpaceMask=Us==WHITE
?(FileCBB|FileDBB|FileEBB|FileFBB)&(Rank2BB|Rank3BB|Rank4BB)
:(FileCBB|FileDBB|FileEBB|FileFBB)&(Rank7BB|Rank6BB|Rank5BB);
Bitboard safe=SpaceMask
&~pieces_cp(Us,PAWN)
&~ei->attackedBy[Them][PAWN];
Bitboard behind=pieces_cp(Us,PAWN);
behind|=shift_bb(Down,behind);
behind|=shift_bb(Down+Down,behind);
int bonus=popcount(safe)+popcount(behind&safe&~ei->attackedBy[Them][0]);
int weight=popcount(pieces_c(Us))-3+min(ei->pe->blockedCount,9);
Score score=make_score(bonus*weight*weight/16,0);
return score;
}
INLINE Value evaluate_winnable(const Position*pos,EvalInfo*ei,Score score)
{
int outflanking=distance_f(square_of(WHITE,KING),square_of(BLACK,KING))
+rank_of(square_of(WHITE,KING))-rank_of(square_of(BLACK,KING));
bool pawnsOnBothFlanks=(pieces_p(PAWN)&QueenSide)
&&(pieces_p(PAWN)&KingSide);
bool almostUnwinnable=outflanking < 0
&&!pawnsOnBothFlanks;
bool infiltration=rank_of(square_of(WHITE,KING))> RANK_4
||rank_of(square_of(BLACK,KING))< RANK_5;
int complexity=9*ei->pe->passedCount
+12*popcount(pieces_p(PAWN))
+9*outflanking
+21*pawnsOnBothFlanks
+24*infiltration
+51*!non_pawn_material()
-43*almostUnwinnable
-110;
Value mg=mg_value(score);
Value eg=eg_value(score);
int u=((mg > 0)-(mg < 0))*clamp(complexity+50,-abs(mg),0);
int v=((eg > 0)-(eg < 0))*max(complexity,-abs(eg));
mg+=u;
eg+=v;
Color strongSide=eg > VALUE_DRAW?WHITE:BLACK;
int sf=material_scale_factor(ei->me,pos,strongSide);
if(sf==SCALE_FACTOR_NORMAL){
if(opposite_bishops(pos)){
if(non_pawn_material_c(WHITE)==BishopValueMg
&&non_pawn_material_c(BLACK)==BishopValueMg)
sf=18+4*popcount(ei->pe->passedPawns[strongSide]);
else
sf=22+3*popcount(pieces_c(strongSide));
}else if(non_pawn_material_c(WHITE)==RookValueMg
&&non_pawn_material_c(BLACK)==RookValueMg
&&piece_count(strongSide,PAWN)-piece_count(!strongSide,PAWN)<=1
&&!(KingSide&pieces_cp(strongSide,PAWN))!=!(QueenSide&pieces_cp(strongSide,PAWN))
&&(attacks_from_king(square_of(!strongSide,KING))&pieces_cp(!strongSide,PAWN)))
sf=36;
else if(popcount(pieces_p(QUEEN))==1)
sf=37+3*(pieces_cp(WHITE,QUEEN)?piece_count(BLACK,BISHOP)+piece_count(BLACK,KNIGHT)
:piece_count(WHITE,BISHOP)+piece_count(WHITE,KNIGHT));
else
sf=min(sf,36+7*piece_count(strongSide,PAWN))-4*!pawnsOnBothFlanks;;
sf-=4*!pawnsOnBothFlanks;
}
v=mg*ei->me->gamePhase
+eg*(PHASE_MIDGAME-ei->me->gamePhase)*sf/SCALE_FACTOR_NORMAL;
v/=PHASE_MIDGAME;
return v;
}
static Value evaluate_classical(const Position*pos)
{
assert(!checkers());
Score mobility[2]={SCORE_ZERO,SCORE_ZERO};
Value v;
EvalInfo ei;
ei.me=material_probe(pos);
if(material_specialized_eval_exists(ei.me))
return material_evaluate(ei.me,pos);
Score score=psq_score()+material_imbalance(ei.me)+pos->contempt;
ei.pe=pawn_probe(pos);
score+=ei.pe->score;
#define lazy_skip(v) (abs(mg_value(score) + eg_value(score)) / 2 > v + non_pawn_material() / 64)
if(lazy_skip(LazyThreshold1))
goto make_v;
evalinfo_init(pos,&ei,WHITE);
evalinfo_init(pos,&ei,BLACK);
score+=evaluate_pieces(pos,&ei,mobility,WHITE,KNIGHT)
-evaluate_pieces(pos,&ei,mobility,BLACK,KNIGHT)
+evaluate_pieces(pos,&ei,mobility,WHITE,BISHOP)
-evaluate_pieces(pos,&ei,mobility,BLACK,BISHOP)
+evaluate_pieces(pos,&ei,mobility,WHITE,ROOK)
-evaluate_pieces(pos,&ei,mobility,BLACK,ROOK)
+evaluate_pieces(pos,&ei,mobility,WHITE,QUEEN)
-evaluate_pieces(pos,&ei,mobility,BLACK,QUEEN);
score+=mobility[WHITE]-mobility[BLACK];
score+=evaluate_king(pos,&ei,mobility,WHITE)
-evaluate_king(pos,&ei,mobility,BLACK);
score+=evaluate_passed(pos,&ei,WHITE)
-evaluate_passed(pos,&ei,BLACK);
if(lazy_skip(LazyThreshold2))
goto make_v;
score+=evaluate_threats(pos,&ei,WHITE)
-evaluate_threats(pos,&ei,BLACK);
score+=evaluate_space(pos,&ei,WHITE)
-evaluate_space(pos,&ei,BLACK);
make_v:
v=evaluate_winnable(pos,&ei,score);
v=(v/16)*16;
v=(stm()==WHITE?v:-v)+Tempo;
return v;
}
#ifdef NNUE
int useNNUE;
static Value fix_FRC(const Position*pos)
{
if(!(pieces_p(BISHOP)&0x8100000000000081ULL))
return 0;
Value v=0;
if(piece_on(SQ_A1)==W_BISHOP&&piece_on(SQ_B2)==W_PAWN)
v+=!is_empty(SQ_B3)?-CorneredBishopV*4
:-CorneredBishopV*3;
if(piece_on(SQ_H1)==W_BISHOP&&piece_on(SQ_G2)==W_PAWN)
v+=!is_empty(SQ_G3)?-CorneredBishopV*4
:-CorneredBishopV*3;
if(piece_on(SQ_A8)==B_BISHOP&&piece_on(SQ_B7)==B_PAWN)
v+=!is_empty(SQ_B6)?CorneredBishopV*4
:CorneredBishopV*3;
if(piece_on(SQ_H8)==B_BISHOP&&piece_on(SQ_G7)==B_PAWN)
v+=!is_empty(SQ_G6)?CorneredBishopV*4
:CorneredBishopV*3;
return stm()==WHITE?v:-v;
}
#define adjusted_NNUE() \
(nnue_evaluate(pos)*(580+mat/32-4*rule50_count())/1024 \
+Time.tempoNNUE+(is_chess960()?fix_FRC(pos):0))
#endif
Value evaluate(const Position*pos)
{
Value v;
#ifdef NNUE
const int mat=non_pawn_material()+4*PawnValueMg*popcount(pieces_p(PAWN));
if(useNNUE==EVAL_HYBRID){
Value psq=abs(eg_value(psq_score()));
int r50=16+rule50_count();
bool largePsq=psq*16 >(NNUEThreshold1+non_pawn_material()/64)*r50;
bool classical=largePsq||(psq > PawnValueMg/4&&!(pos->nodes&0x0B));
bool lowPieceEndgame=non_pawn_material()==BishopValueMg
||(non_pawn_material()< 2*RookValueMg
&&popcount(pieces_p(PAWN))< 2);
v=classical||lowPieceEndgame?evaluate_classical(pos)
:adjusted_NNUE();
if(classical&&largePsq&&!lowPieceEndgame
&&(abs(v)*16 < NNUEThreshold2*r50
||(opposite_bishops(pos)
&&abs(v)*16 <(NNUEThreshold1+non_pawn_material()/64)*r50
&&!(pos->nodes&0xB))))
v=adjusted_NNUE();
}else if(useNNUE==EVAL_PURE)
v=adjusted_NNUE();
else
v=evaluate_classical(pos);
#else
v=evaluate_classical(pos);
#endif
v=v*(100-rule50_count())/100;
return clamp(v,VALUE_TB_LOSS_IN_MAX_PLY+1,VALUE_TB_WIN_IN_MAX_PLY-1);
}
#else /* NNUE_PURE */
Value evaluate(const Position*pos)
{
Value v;
int mat=non_pawn_material()+4*PawnValueMg*popcount(pieces_p(PAWN));
v=adjusted_NNUE();
v=v*(100-rule50_count())/100;
return clamp(v,VALUE_TB_LOSS_IN_MAX_PLY+1,VALUE_TB_WIN_IN_MAX_PLY-1);
}
#endif
//
INLINE void partial_insertion_sort(ExtMove*begin,ExtMove*end,int limit)
{
for(ExtMove*sortedEnd=begin,*p=begin+1;p < end;p++)
if(p->value >=limit){
ExtMove tmp=*p,*q;
*p=*++sortedEnd;
for(q=sortedEnd;q!=begin&&(q-1)->value < tmp.value;q--)
*q=*(q-1);
*q=tmp;
}
}
static Move pick_best(ExtMove*begin,ExtMove*end)
{
ExtMove*p,*q;
for(p=begin,q=begin+1;q < end;q++)
if(q->value > p->value)
p=q;
Move m=p->move;
int v=p->value;
*p=*begin;
begin->value=v;
return m;
}
static void score_captures(const Position*pos)
{
Stack*st=pos->st;
CapturePieceToHistory*history=pos->captureHistory;
for(ExtMove*m=st->cur;m < st->endMoves;m++)
m->value=PieceValue[MG][piece_on(to_sq(m->move))]*6
+(*history)[moved_piece(m->move)][to_sq(m->move)][type_of_p(piece_on(to_sq(m->move)))];
}
SMALL
static void score_quiets(const Position*pos)
{
Stack*st=pos->st;
ButterflyHistory*history=pos->mainHistory;
LowPlyHistory*lph=pos->lowPlyHistory;
PieceToHistory*cmh=(st-1)->history;
PieceToHistory*fmh=(st-2)->history;
PieceToHistory*fmh2=(st-4)->history;
PieceToHistory*fmh3=(st-6)->history;
Color c=stm();
for(ExtMove*m=st->cur;m < st->endMoves;m++){
uint32_t move=m->move&4095;
Square to=move&63;
Square from=move >> 6;
m->value=(*history)[c][move]
+2*(*cmh)[piece_on(from)][to]
+(*fmh)[piece_on(from)][to]
+(*fmh2)[piece_on(from)][to]
+(*fmh3)[piece_on(from)][to]
+(st->mp_ply < MAX_LPH?min(4,st->depth/3)*(*lph)[st->mp_ply][move]:0);
}
}
static void score_evasions(const Position*pos)
{
Stack*st=pos->st;
ButterflyHistory*history=pos->mainHistory;
PieceToHistory*cmh=(st-1)->history;
Color c=stm();
for(ExtMove*m=st->cur;m < st->endMoves;m++)
if(is_capture(pos,m->move))
m->value=PieceValue[MG][piece_on(to_sq(m->move))]
-type_of_p(moved_piece(m->move));
else
m->value=(*history)[c][from_to(m->move)]
+2*(*cmh)[moved_piece(m->move)][to_sq(m->move)]
-(1 << 28);
}
Move next_move(const Position*pos,bool skipQuiets)
{
Stack*st=pos->st;
Move move;
switch(st->stage){
case ST_MAIN_SEARCH:case ST_EVASION:case ST_QSEARCH:case ST_PROBCUT:
st->endMoves=(st-1)->endMoves;
st->stage++;
return st->ttMove;
case ST_CAPTURES_INIT:
st->endBadCaptures=st->cur=(st-1)->endMoves;
st->endMoves=generate_captures(pos,st->cur);
score_captures(pos);
st->stage++;
case ST_GOOD_CAPTURES:
while(st->cur < st->endMoves){
move=pick_best(st->cur++,st->endMoves);
if(move!=st->ttMove){
if(see_test(pos,move,-69*(st->cur-1)->value/1024))
return move;
(st->endBadCaptures++)->move=move;
}
}
st->stage++;
move=st->mpKillers[0];
if(move&&move!=st->ttMove&&is_pseudo_legal(pos,move)
&&!is_capture(pos,move))
return move;
case ST_KILLERS:
st->stage++;
move=st->mpKillers[1];
if(move&&move!=st->ttMove&&is_pseudo_legal(pos,move)
&&!is_capture(pos,move))
return move;
case ST_KILLERS_2:
st->stage++;
move=st->countermove;
if(move&&move!=st->ttMove&&move!=st->mpKillers[0]
&&move!=st->mpKillers[1]&&is_pseudo_legal(pos,move)
&&!is_capture(pos,move))
return move;
case ST_QUIET_INIT:
if(!skipQuiets){
st->cur=st->endBadCaptures;
st->endMoves=generate_quiets(pos,st->cur);
score_quiets(pos);
partial_insertion_sort(st->cur,st->endMoves,-3000*st->depth);
}
st->stage++;
case ST_QUIET:
if(!skipQuiets)
while(st->cur < st->endMoves){
move=(st->cur++)->move;
if(move!=st->ttMove&&move!=st->mpKillers[0]
&&move!=st->mpKillers[1]&&move!=st->countermove)
return move;
}
st->stage++;
st->cur=(st-1)->endMoves;
case ST_BAD_CAPTURES:
if(st->cur < st->endBadCaptures)
return(st->cur++)->move;
break;
case ST_EVASIONS_INIT:
st->cur=(st-1)->endMoves;
st->endMoves=generate_evasions(pos,st->cur);
score_evasions(pos);
st->stage++;
case ST_ALL_EVASIONS:
while(st->cur < st->endMoves){
move=pick_best(st->cur++,st->endMoves);
if(move!=st->ttMove)
return move;
}
break;
case ST_QCAPTURES_INIT:
st->cur=(st-1)->endMoves;
st->endMoves=generate_captures(pos,st->cur);
score_captures(pos);
st->stage++;
case ST_QCAPTURES:
while(st->cur < st->endMoves){
move=pick_best(st->cur++,st->endMoves);
if(move!=st->ttMove&&(st->depth > DEPTH_QS_RECAPTURES
||to_sq(move)==st->recaptureSquare))
return move;
}
if(st->depth <=DEPTH_QS_NO_CHECKS)
break;
st->cur=(st-1)->endMoves;
st->endMoves=generate_quiet_checks(pos,st->cur);
st->stage++;
case ST_QCHECKS:
while(st->cur < st->endMoves){
move=(st->cur++)->move;
if(move!=st->ttMove)
return move;
}
break;
case ST_PROBCUT_INIT:
st->cur=(st-1)->endMoves;
st->endMoves=generate_captures(pos,st->cur);
score_captures(pos);
st->stage++;
case ST_PROBCUT_2:
while(st->cur < st->endMoves){
move=pick_best(st->cur++,st->endMoves);
if(move!=st->ttMove&&see_test(pos,move,st->threshold))
return move;
}
break;
default:
assume(false);
}
return 0;
}
//
#ifndef _WIN32
#endif
TranspositionTable TT;
void tt_free(void)
{
if(TT.table)
free_memory(&TT.alloc);
TT.table=NULL;
}
void tt_allocate(size_t mbSize)
{
TT.clusterCount=mbSize*1024*1024/sizeof(Cluster);
size_t size=TT.clusterCount*sizeof(Cluster);
TT.table=NULL;
if(settings.largePages){
TT.table=allocate_memory(size,true,&TT.alloc);
#if !defined(__linux__)
if(!TT.table)
fprintf(stderr,"# Unable to allocate large pages for TT\n");
else
fprintf(stderr,"# TT allocated using large pages\n");
#endif
}
if(!TT.table)
TT.table=allocate_memory(size,false,&TT.alloc);
if(!TT.table)
goto failed;
tt_clear();
return;
failed:
fprintf(stderr,"Failed to allocate %"PRIu64"MB for "
"transposition table.\n",(uint64_t)mbSize);
exit(EXIT_FAILURE);
}
void tt_clear(void)
{
if(TT.table){
for(int idx=0;idx < Threads.numThreads;idx++)
thread_wake_up(Threads.pos[idx],THREAD_TT_CLEAR);
for(int idx=0;idx < Threads.numThreads;idx++)
thread_wait_until_sleeping(Threads.pos[idx]);
}
}
void tt_clear_worker(int idx)
{
size_t total=TT.clusterCount*sizeof(Cluster);
size_t slice=(total+Threads.numThreads-1)/Threads.numThreads;
size_t blocks=(slice+(2*1024*1024)-1)/(2*1024*1024);
size_t begin=idx*blocks*(2*1024*1024);
size_t end=begin+blocks*(2*1024*1024);
begin=min(begin,total);
end=min(end,total);
memset((uint8_t*)TT.table+begin,0,end-begin);
}
TTEntry*tt_probe(Key key,bool*found)
{
TTEntry*tte=tt_first_entry(key);
uint16_t key16=key;
for(int i=0;i < ClusterSize;i++)
if(tte[i].key16==key16||!tte[i].depth8){
tte[i].genBound8=TT.generation8|(tte[i].genBound8&0x7);
*found=tte[i].depth8;
return&tte[i];
}
TTEntry*replace=tte;
for(int i=1;i < ClusterSize;i++)
if(replace->depth8-((263+TT.generation8-replace->genBound8)&0xF8)
> tte[i].depth8-((263+TT.generation8-tte[i].genBound8)&0xF8))
replace=&tte[i];
*found=false;
return replace;
}
int tt_hashfull(void)
{
int cnt=0;
for(int i=0;i < 1000/ClusterSize;i++){
const TTEntry*tte=&TT.table[i].entry[0];
for(int j=0;j < ClusterSize;j++)
cnt+=tte[j].depth8&&(tte[j].genBound8&0xf8)==TT.generation8;
}
return cnt*1000/(ClusterSize*(1000/ClusterSize));
}
//
struct TimeManagement Time;
void time_init(Color us,int ply)
{
int moveOverhead=option_value(OPT_MOVE_OVERHEAD);
int slowMover=option_value(OPT_SLOW_MOVER);
int npmsec=option_value(OPT_NODES_TIME);
double optScale,maxScale;
if(npmsec){
if(!Time.availableNodes)
Time.availableNodes=npmsec*Limits.time[us];
Limits.time[us]=(int)Time.availableNodes;
Limits.inc[us]*=npmsec;
Limits.npmsec=npmsec;
}
Time.startTime=Limits.startTime;
int mtg=Limits.movestogo?min(Limits.movestogo,50):50;
TimePoint timeLeft=max(1,Limits.time[us]+Limits.inc[us]*(mtg-1)-moveOverhead*(2+mtg));
timeLeft=slowMover*timeLeft/100;
if(Limits.movestogo==0){
optScale=min(0.0084+pow(ply+3.0,0.5)*0.0042,
0.2*Limits.time[us]/(double)timeLeft);
maxScale=min(7.0,4.0+ply/12.0);
}
else{
optScale=min((0.8+ply/120.0)/mtg,
0.8*Limits.time[us]/(double)timeLeft);
maxScale=min(6.3,1.5+0.11*mtg);
}
Time.optimumTime=optScale*timeLeft;
Time.maximumTime=min(0.8*Limits.time[us]-moveOverhead,maxScale*Time.optimumTime);
if(use_time_management()){
int strength=log(max(1,(int)(Time.optimumTime*Threads.numThreads/10)))*60;
Time.tempoNNUE=clamp((strength+264)/24,18,30);
}else
Time.tempoNNUE=28;
if(option_value(OPT_PONDER))
Time.optimumTime+=Time.optimumTime/4;
}
//
static void thread_idle_loop(Position*pos);
#ifndef _WIN32
#define THREAD_FUNC void *
#else
#define THREAD_FUNC DWORD WINAPI
#endif
ThreadPool Threads;
MainThread mainThread;
CounterMoveHistoryStat**cmhTables=NULL;
int numCmhTables=0;
static THREAD_FUNC thread_init(void*arg)
{
int idx=(intptr_t)arg;
int node;
if(settings.numaEnabled)
node=bind_thread_to_numa_node(idx);
else
node=0;
#ifdef PER_THREAD_CMH
(void)node;
int t=idx;
#else
int t=node;
#endif
if(t >=numCmhTables){
int old=numCmhTables;
numCmhTables=t+16;
cmhTables=realloc(cmhTables,
numCmhTables*sizeof(CounterMoveHistoryStat*));
while(old < numCmhTables)
cmhTables[old++]=NULL;
}
if(!cmhTables[t]){
if(settings.numaEnabled)
cmhTables[t]=numa_alloc(sizeof(CounterMoveHistoryStat));
else
cmhTables[t]=calloc(1,sizeof(CounterMoveHistoryStat));
for(int chk=0;chk < 2;chk++)
for(int c=0;c < 2;c++)
for(int j=0;j < 16;j++)
for(int k=0;k < 64;k++)
(*cmhTables[t])[chk][c][0][0][j][k]=CounterMovePruneThreshold-1;
}
Position*pos;
if(settings.numaEnabled){
pos=numa_alloc(sizeof(Position));
#ifndef NNUE_PURE
pos->pawnTable=numa_alloc(PAWN_ENTRIES*sizeof(PawnEntry));
pos->materialTable=numa_alloc(8192*sizeof(MaterialEntry));
#endif
pos->counterMoves=numa_alloc(sizeof(CounterMoveStat));
pos->mainHistory=numa_alloc(sizeof(ButterflyHistory));
pos->captureHistory=numa_alloc(sizeof(CapturePieceToHistory));
pos->lowPlyHistory=numa_alloc(sizeof(LowPlyHistory));
pos->rootMoves=numa_alloc(sizeof(RootMoves));
pos->stackAllocation=numa_alloc(63+(MAX_PLY+110)*sizeof(Stack));
pos->moveList=numa_alloc(10000*sizeof(ExtMove));
}else{
pos=calloc(1,sizeof(Position));
#ifndef NNUE_PURE
pos->pawnTable=calloc(PAWN_ENTRIES,sizeof(PawnEntry));
pos->materialTable=calloc(8192,sizeof(MaterialEntry));
#endif
pos->counterMoves=calloc(1,sizeof(CounterMoveStat));
pos->mainHistory=calloc(1,sizeof(ButterflyHistory));
pos->captureHistory=calloc(1,sizeof(CapturePieceToHistory));
pos->lowPlyHistory=calloc(1,sizeof(LowPlyHistory));
pos->rootMoves=calloc(1,sizeof(RootMoves));
pos->stackAllocation=calloc(63+(MAX_PLY+110),sizeof(Stack));
pos->moveList=calloc(10000,sizeof(ExtMove));
}
pos->stack=(Stack*)(((uintptr_t)pos->stackAllocation+0x3f)&~0x3f);
pos->threadIdx=idx;
pos->counterMoveHistory=cmhTables[t];
atomic_store(&pos->resetCalls,false);
pos->selDepth=pos->callsCnt=0;
#ifndef _WIN32  // linux
pthread_mutex_init(&pos->mutex,NULL);
pthread_cond_init(&pos->sleepCondition,NULL);
Threads.pos[idx]=pos;
pthread_mutex_lock(&Threads.mutex);
Threads.initializing=false;
pthread_cond_signal(&Threads.sleepCondition);
pthread_mutex_unlock(&Threads.mutex);
#else // Windows
pos->startEvent=CreateEvent(NULL,FALSE,FALSE,NULL);
pos->stopEvent=CreateEvent(NULL,FALSE,FALSE,NULL);
Threads.pos[idx]=pos;
SetEvent(Threads.event);
#endif
thread_idle_loop(pos);
return 0;
}
static void thread_create(int idx)
{
#ifndef _WIN32
pthread_t thread;
Threads.initializing=true;
pthread_mutex_lock(&Threads.mutex);
pthread_create(&thread,NULL,thread_init,(void*)(intptr_t)idx);
while(Threads.initializing)
pthread_cond_wait(&Threads.sleepCondition,&Threads.mutex);
pthread_mutex_unlock(&Threads.mutex);
#else
HANDLE thread=CreateThread(NULL,0,thread_init,(void*)(intptr_t)idx,
0,NULL);
WaitForSingleObject(Threads.event,INFINITE);
#endif
Threads.pos[idx]->nativeThread=thread;
}
static void thread_destroy(Position*pos)
{
#ifndef _WIN32
pthread_mutex_lock(&pos->mutex);
pos->action=THREAD_EXIT;
pthread_cond_signal(&pos->sleepCondition);
pthread_mutex_unlock(&pos->mutex);
pthread_join(pos->nativeThread,NULL);
pthread_cond_destroy(&pos->sleepCondition);
pthread_mutex_destroy(&pos->mutex);
#else
pos->action=THREAD_EXIT;
SetEvent(pos->startEvent);
WaitForSingleObject(pos->nativeThread,INFINITE);
CloseHandle(pos->startEvent);
CloseHandle(pos->stopEvent);
#endif
if(settings.numaEnabled){
#ifndef NNUE_PURE
numa_free(pos->pawnTable,PAWN_ENTRIES*sizeof(PawnEntry));
numa_free(pos->materialTable,8192*sizeof(MaterialEntry));
#endif
numa_free(pos->counterMoves,sizeof(CounterMoveStat));
numa_free(pos->mainHistory,sizeof(ButterflyHistory));
numa_free(pos->captureHistory,sizeof(CapturePieceToHistory));
numa_free(pos->lowPlyHistory,sizeof(LowPlyHistory));
numa_free(pos->rootMoves,sizeof(RootMoves));
numa_free(pos->stackAllocation,63+(MAX_PLY+110)*sizeof(Stack));
numa_free(pos->moveList,10000*sizeof(ExtMove));
numa_free(pos,sizeof(Position));
}else{
#ifndef NNUE_PURE
free(pos->pawnTable);
free(pos->materialTable);
#endif
free(pos->counterMoves);
free(pos->mainHistory);
free(pos->captureHistory);
free(pos->lowPlyHistory);
free(pos->rootMoves);
free(pos->stackAllocation);
free(pos->moveList);
free(pos);
}
}
void thread_wait_until_sleeping(Position*pos)
{
#ifndef _WIN32
pthread_mutex_lock(&pos->mutex);
while(pos->action!=THREAD_SLEEP)
pthread_cond_wait(&pos->sleepCondition,&pos->mutex);
pthread_mutex_unlock(&pos->mutex);
#else
WaitForSingleObject(pos->stopEvent,INFINITE);
#endif
if(pos->threadIdx==0)
Threads.searching=false;
}
void thread_wait(Position*pos,atomic_bool*condition)
{
#ifndef _WIN32
pthread_mutex_lock(&pos->mutex);
while(!atomic_load(condition))
pthread_cond_wait(&pos->sleepCondition,&pos->mutex);
pthread_mutex_unlock(&pos->mutex);
#else
(void)condition;
WaitForSingleObject(pos->startEvent,INFINITE);
#endif
}
void thread_wake_up(Position*pos,int action)
{
#ifndef _WIN32
pthread_mutex_lock(&pos->mutex);
#endif
if(action!=THREAD_RESUME)
pos->action=action;
#ifndef _WIN32
pthread_cond_signal(&pos->sleepCondition);
pthread_mutex_unlock(&pos->mutex);
#else
SetEvent(pos->startEvent);
#endif
}
static void thread_idle_loop(Position*pos)
{
while(true){
#ifndef _WIN32
pthread_mutex_lock(&pos->mutex);
while(pos->action==THREAD_SLEEP){
pthread_cond_signal(&pos->sleepCondition);
pthread_cond_wait(&pos->sleepCondition,&pos->mutex);
}
pthread_mutex_unlock(&pos->mutex);
#else
WaitForSingleObject(pos->startEvent,INFINITE);
#endif
if(pos->action==THREAD_EXIT){
break;
}else if(pos->action==THREAD_TT_CLEAR){
tt_clear_worker(pos->threadIdx);
}else{
if(pos->threadIdx==0)
mainthread_search();
else
thread_search(pos);
}
pos->action=THREAD_SLEEP;
#ifdef _WIN32
SetEvent(pos->stopEvent);
#endif
}
}
void threads_init(void)
{
#ifndef _WIN32
pthread_mutex_init(&Threads.mutex,NULL);
pthread_cond_init(&Threads.sleepCondition,NULL);
#else
Threads.event=CreateEvent(NULL,FALSE,FALSE,NULL);
#endif
#ifdef NUMA
numa_init();
#endif
Threads.numThreads=1;
thread_create(0);
}
void threads_exit(void)
{
threads_set_number(0);
#ifndef _WIN32
pthread_cond_destroy(&Threads.sleepCondition);
pthread_mutex_destroy(&Threads.mutex);
#else
CloseHandle(Threads.event);
#endif
#ifdef NUMA
numa_exit();
#endif
}
void threads_set_number(int num)
{
while(Threads.numThreads < num)
thread_create(Threads.numThreads++);
while(Threads.numThreads > num)
thread_destroy(Threads.pos[--Threads.numThreads]);
search_init();
if(num==0&&numCmhTables > 0){
for(int i=0;i < numCmhTables;i++)
if(cmhTables[i]){
if(settings.numaEnabled)
numa_free(cmhTables[i],sizeof(CounterMoveHistoryStat));
else
free(cmhTables[i]);
}
free(cmhTables);
cmhTables=NULL;
numCmhTables=0;
}
if(num==0)
Threads.searching=false;
}
uint64_t threads_nodes_searched(void)
{
uint64_t nodes=0;
for(int idx=0;idx < Threads.numThreads;idx++)
nodes+=Threads.pos[idx]->nodes;
return nodes;
}
uint64_t threads_tb_hits(void)
{
uint64_t hits=0;
for(int idx=0;idx < Threads.numThreads;idx++)
hits+=Threads.pos[idx]->tbHits;
return hits;
}
//
#define load_rlx(x) atomic_load_explicit(&(x), memory_order_relaxed)
#define store_rlx(x,y) atomic_store_explicit(&(x), y, memory_order_relaxed)
LimitsType Limits;
int TB_Cardinality,TB_CardinalityDTM;
static bool TB_RootInTB,TB_UseRule50;
static Depth TB_ProbeDepth;
static int base_ct;
enum{NonPV,PV};
static const uint64_t ttHitAverageWindow=4096;
static const uint64_t ttHitAverageResolution=1024;
INLINE int futility_margin(Depth d,bool improving){
return 234*(d-improving);
}
static int Reductions[MAX_MOVES];
INLINE Depth reduction(int i,Depth d,int mn)
{
int r=Reductions[d]*Reductions[mn];
return(r+503)/1024+(!i&&r > 915);
}
INLINE int futility_move_count(bool improving,Depth depth)
{
return improving?3+depth*depth:(3+depth*depth)/2;
}
static Value stat_bonus(Depth depth)
{
int d=depth;
return d > 14?66:6*d*d+231*d-206;
}
static Value value_draw(Position*pos)
{
return VALUE_DRAW+2*(pos->nodes&1)-1;
}
struct Skill{
/*
Skill(int l):level(l){}
int enabled()const{return level < 20;}
int time_to_pick(Depth depth)const{return depth==1+level;}
Move best_move(size_t multiPV){return best?best:pick_best(multiPV);}
Move pick_best(size_t multiPV);
*/
int level;
Move best;
};
static Value search_PV(Position*pos,Stack*ss,Value alpha,Value beta,
Depth depth);
static Value search_NonPV(Position*pos,Stack*ss,Value alpha,Depth depth,
bool cutNode);
static Value qsearch_PV_true(Position*pos,Stack*ss,Value alpha,Value beta,
Depth depth);
static Value qsearch_PV_false(Position*pos,Stack*ss,Value alpha,Value beta,
Depth depth);
static Value qsearch_NonPV_true(Position*pos,Stack*ss,Value alpha,
Depth depth);
static Value qsearch_NonPV_false(Position*pos,Stack*ss,Value alpha,
Depth depth);
static Value value_to_tt(Value v,int ply);
static Value value_from_tt(Value v,int ply,int r50c);
static void update_pv(Move*pv,Move move,Move*childPv);
static void update_cm_stats(Stack*ss,Piece pc,Square s,int bonus);
static void update_quiet_stats(const Position*pos,Stack*ss,Move move,
int bonus,Depth depth);
static void update_capture_stats(const Position*pos,Move move,Move*captures,
int captureCnt,int bonus);
static void check_time(void);
static void stable_sort(RootMove*rm,int num);
static void uci_print_pv(Position*pos,Depth depth,Value alpha,Value beta);
static int extract_ponder_from_tt(RootMove*rm,Position*pos);
void search_init(void)
{
for(int i=1;i < MAX_MOVES;i++)
Reductions[i]=(21.3+2*log(Threads.numThreads))*log(i+0.25*log(i));
}
void search_clear(void)
{
if(!settings.ttSize){
delayedSettings.clear=true;
return;
}
Time.availableNodes=0;
tt_clear();
for(int i=0;i < numCmhTables;i++)
if(cmhTables[i]){
stats_clear(cmhTables[i]);
for(int chk=0;chk < 2;chk++)
for(int c=0;c < 2;c++)
for(int j=0;j < 16;j++)
for(int k=0;k < 64;k++)
(*cmhTables[i])[chk][c][0][0][j][k]=CounterMovePruneThreshold-1;
}
for(int idx=0;idx < Threads.numThreads;idx++){
Position*pos=Threads.pos[idx];
stats_clear(pos->counterMoves);
stats_clear(pos->mainHistory);
stats_clear(pos->captureHistory);
stats_clear(pos->lowPlyHistory);
}
TB_release();
mainThread.previousScore=VALUE_INFINITE;
mainThread.previousTimeReduction=1;
}
static uint64_t perft_helper(Position*pos,Depth depth);
INLINE uint64_t perft_node(Position*pos,Depth depth,const bool Root)
{
uint64_t cnt,nodes=0;
const bool leaf=(depth==2);
ExtMove*m=Root?pos->moveList:(pos->st-1)->endMoves;
ExtMove*last=pos->st->endMoves=generate_legal(pos,m);
for(;m < last;m++){
if(Root&&depth <=1){
cnt=1;
nodes++;
}else{
do_move(pos,m->move,gives_check(pos,pos->st,m->move));
cnt=leaf?(uint64_t)(generate_legal(pos,last)-last)
:perft_helper(pos,depth-1);
nodes+=cnt;
undo_move(pos,m->move);
}
if(Root){
char buf[16];
printf("%s: %"PRIu64"\n",uci_move(buf,m->move,is_chess960()),cnt);
}
}
return nodes;
}
static NOINLINE uint64_t perft_helper(Position*pos,Depth depth)
{
return perft_node(pos,depth,false);
}
NOINLINE uint64_t perft(Position*pos,Depth depth)
{
return perft_node(pos,depth,true);
}
void mainthread_search(void)
{
Position*pos=Threads.pos[0];
Color us=stm();
time_init(us,game_ply());
tt_new_search();
char buf[16];
bool playBookMove=false;
#ifdef NNUE
/* Suppress UCI info strings for WinBoard mode
switch(useNNUE){
case EVAL_HYBRID:
fprintf(stderr,"# Hybrid NNUE evaluation enabled\n");
break;
case EVAL_PURE:
fprintf(stderr,"# Pure NNUE evaluation enabled\n");
break;
case EVAL_CLASSICAL:
fprintf(stderr,"# Classical evaluation enabled\n");
break;
}
*/
#endif
base_ct=option_value(OPT_CONTEMPT)*PawnValueEg/100;
const char*s=option_string_value(OPT_ANALYSIS_CONTEMPT);
if(Limits.infinite||option_value(OPT_ANALYSE_MODE))
base_ct=strcmp(s,"off")==0?0
:strcmp(s,"white")==0&&us==BLACK?-base_ct
:strcmp(s,"black")==0&&us==WHITE?-base_ct
:base_ct;
if(pos->rootMoves->size > 0){
Move bookMove=0;
if(!Limits.infinite&&!Limits.mate){
bookMove=pb_probe(&polybook,pos);
if(!bookMove)
bookMove=pb_probe(&polybook2,pos);
}
for(int i=0;i < pos->rootMoves->size;i++)
if(pos->rootMoves->move[i].pv[0]==bookMove){
RootMove tmp=pos->rootMoves->move[0];
pos->rootMoves->move[0]=pos->rootMoves->move[i];
pos->rootMoves->move[i]=tmp;
playBookMove=true;
break;
}
if(!playBookMove){
Threads.pos[0]->bestMoveChanges=0;
for(int idx=1;idx < Threads.numThreads;idx++){
Threads.pos[idx]->bestMoveChanges=0;
thread_wake_up(Threads.pos[idx],THREAD_SEARCH);
}
thread_search(pos);
}
}
LOCK(Threads.lock);
if(!Threads.stop&&(Threads.ponder||Limits.infinite)){
Threads.sleeping=true;
UNLOCK(Threads.lock);
thread_wait(pos,&Threads.stop);
}else
UNLOCK(Threads.lock);
Threads.stop=true;
if(pos->rootMoves->size > 0){
if(!playBookMove){
for(int idx=1;idx < Threads.numThreads;idx++)
thread_wait_until_sleeping(Threads.pos[idx]);
}
}else{
pos->rootMoves->move[0].pv[0]=0;
pos->rootMoves->move[0].pvSize=1;
pos->rootMoves->size++;
/* WinBoard: output result for no legal moves */
if(checkers())
printf("0-1 {checkmate}\n");
else
printf("1/2-1/2 {stalemate}\n");
fflush(stdout);
}
if(Limits.npmsec)
Time.availableNodes+=Limits.inc[us]-threads_nodes_searched();
Position*bestThread=pos;
if(option_value(OPT_MULTI_PV)==1
&&!playBookMove
&&!Limits.depth
&&pos->rootMoves->move[0].pv[0]!=0)
{
int i,num=0,maxNum=min(pos->rootMoves->size,Threads.numThreads);
Move mvs[maxNum];
int64_t votes[maxNum];
Value minScore=pos->rootMoves->move[0].score;
for(int idx=1;idx < Threads.numThreads;idx++)
minScore=min(minScore,Threads.pos[idx]->rootMoves->move[0].score);
for(int idx=0;idx < Threads.numThreads;idx++){
Position*p=Threads.pos[idx];
Move m=p->rootMoves->move[0].pv[0];
for(i=0;i < num;i++)
if(mvs[i]==m)break;
if(i==num){
num++;
mvs[i]=m;
votes[i]=0;
}
votes[i]+=(p->rootMoves->move[0].score-minScore+14)*p->completedDepth;
}
int64_t bestVote=votes[0];
for(int idx=1;idx < Threads.numThreads;idx++){
Position*p=Threads.pos[idx];
for(i=0;mvs[i]!=p->rootMoves->move[0].pv[0];i++);
if(abs(bestThread->rootMoves->move[0].score)>=VALUE_TB_WIN_IN_MAX_PLY){
if(p->rootMoves->move[0].score > bestThread->rootMoves->move[0].score)
bestThread=p;
}else if(p->rootMoves->move[0].score >=VALUE_TB_WIN_IN_MAX_PLY
||(p->rootMoves->move[0].score > VALUE_TB_LOSS_IN_MAX_PLY
&&votes[i] > bestVote))
{
bestVote=votes[i];
bestThread=p;
}
}
}
mainThread.previousScore=bestThread->rootMoves->move[0].score;
if(bestThread!=pos)
uci_print_pv(bestThread,bestThread->completedDepth,
-VALUE_INFINITE,VALUE_INFINITE);
flockfile(stdout);
/* WinBoard format: "move" instead of "bestmove" */
printf("move %s\n",uci_move(buf,bestThread->rootMoves->move[0].pv[0],is_chess960()));
fflush(stdout);
funlockfile(stdout);
}
void thread_search(Position*pos)
{
Value bestValue,alpha,beta,delta;
Move pv[MAX_PLY+1];
Move lastBestMove=0;
Depth lastBestMoveDepth=0;
double timeReduction=1.0,totBestMoveChanges=0;
int iterIdx=0;
Stack*ss=pos->st;
for(int i=-7;i < 3;i++){
memset(SStackBegin(ss[i]),0,SStackSize);
#ifdef NNUE
ss[i].accumulator.state[WHITE]=ACC_INIT;
ss[i].accumulator.state[BLACK]=ACC_INIT;
#endif
}
(ss-1)->endMoves=pos->moveList;
for(int i=-7;i < 0;i++)
ss[i].history=&(*pos->counterMoveHistory)[0][0][0][0];
for(int i=0;i <=MAX_PLY;i++)
ss[i].ply=i;
ss->pv=pv;
bestValue=delta=alpha=-VALUE_INFINITE;
beta=VALUE_INFINITE;
pos->completedDepth=0;
if(pos->threadIdx==0){
if(mainThread.previousScore==VALUE_INFINITE)
for(int i=0;i < 4;i++)
mainThread.iterValue[i]=VALUE_ZERO;
else
for(int i=0;i < 4;i++)
mainThread.iterValue[i]=mainThread.previousScore;
}
memmove(&((*pos->lowPlyHistory)[0]),&((*pos->lowPlyHistory)[2]),
(MAX_LPH-2)*sizeof((*pos->lowPlyHistory)[0]));
memset(&((*pos->lowPlyHistory)[MAX_LPH-2]),0,2*sizeof((*pos->lowPlyHistory)[0]));
int multiPV=option_value(OPT_MULTI_PV);
#if 0
Skill skill(option_value(OPT_SKILL_LEVEL));
if(skill.enabled())
multiPV=std::max(multiPV,(size_t)4);
#endif
RootMoves*rm=pos->rootMoves;
multiPV=min(multiPV,rm->size);
pos->ttHitAverage=ttHitAverageWindow*ttHitAverageResolution/2;
int searchAgainCounter=0;
while(++pos->rootDepth < MAX_PLY
&&!Threads.stop
&&!(Limits.depth
&&pos->threadIdx==0
&&pos->rootDepth > Limits.depth))
{
if(pos->threadIdx==0)
totBestMoveChanges/=2;
for(int idx=0;idx < rm->size;idx++)
rm->move[idx].previousScore=rm->move[idx].score;
pos->contempt=stm()==WHITE?make_score(base_ct,base_ct/2)
:-make_score(base_ct,base_ct/2);
int pvFirst=0,pvLast=0;
if(!Threads.increaseDepth)
searchAgainCounter++;
for(int pvIdx=0;pvIdx < multiPV&&!Threads.stop;pvIdx++){
pos->pvIdx=pvIdx;
if(pvIdx==pvLast){
pvFirst=pvLast;
for(pvLast++;pvLast < rm->size;pvLast++)
if(rm->move[pvLast].tbRank!=rm->move[pvFirst].tbRank)
break;
pos->pvLast=pvLast;
}
pos->selDepth=0;
if(abs(rm->move[pvIdx].tbRank)> 1000){
bestValue=rm->move[pvIdx].score=rm->move[pvIdx].tbScore;
alpha=-VALUE_INFINITE;
beta=VALUE_INFINITE;
goto skip_search;
}
if(pos->rootDepth >=4){
Value previousScore=rm->move[pvIdx].previousScore;
delta=17;
alpha=max(previousScore-delta,-VALUE_INFINITE);
beta=min(previousScore+delta,VALUE_INFINITE);
int ct=base_ct+(113-base_ct/2)*previousScore/(abs(previousScore)+147);
pos->contempt=stm()==WHITE?make_score(ct,ct/2)
:-make_score(ct,ct/2);
}
pos->failedHighCnt=0;
while(true){
Depth adjustedDepth=max(1,pos->rootDepth-pos->failedHighCnt-searchAgainCounter);
bestValue=search_PV(pos,ss,alpha,beta,adjustedDepth);
stable_sort(&rm->move[pvIdx],pvLast-pvIdx);
if(Threads.stop)
break;
if(pos->threadIdx==0
&&multiPV==1
&&(bestValue <=alpha||bestValue >=beta)
&&time_elapsed()> 3000)
uci_print_pv(pos,pos->rootDepth,alpha,beta);
if(bestValue <=alpha){
beta=(alpha+beta)/2;
alpha=max(bestValue-delta,-VALUE_INFINITE);
pos->failedHighCnt=0;
if(pos->threadIdx==0)
Threads.stopOnPonderhit=false;
}else if(bestValue >=beta){
beta=min(bestValue+delta,VALUE_INFINITE);
pos->failedHighCnt++;
}else
break;
delta+=delta/4+5;
assert(alpha >=-VALUE_INFINITE&&beta <=VALUE_INFINITE);
}
stable_sort(&rm->move[pvFirst],pvIdx-pvFirst+1);
skip_search:
if(pos->threadIdx==0
&&(Threads.stop||pvIdx+1==multiPV||time_elapsed()> 3000))
uci_print_pv(pos,pos->rootDepth,alpha,beta);
}
if(!Threads.stop)
pos->completedDepth=pos->rootDepth;
if(rm->move[0].pv[0]!=lastBestMove){
lastBestMove=rm->move[0].pv[0];
lastBestMoveDepth=pos->rootDepth;
}
if(Limits.mate
&&bestValue >=VALUE_MATE_IN_MAX_PLY
&&VALUE_MATE-bestValue <=2*Limits.mate)
Threads.stop=true;
if(pos->threadIdx!=0)
continue;
#if 0
if(skill.enabled()&&skill.time_to_pick(thread->rootDepth))
skill.pick_best(multiPV);
#endif
if(use_time_management()
&&!Threads.stop
&&!Threads.stopOnPonderhit)
{
double fallingEval=(318+6*(mainThread.previousScore-bestValue)
+6*(mainThread.iterValue[iterIdx]-bestValue))/825.0;
fallingEval=clamp(fallingEval,0.5,1.5);
timeReduction=lastBestMoveDepth+9 < pos->completedDepth?1.92:0.95;
double reduction=(1.47+mainThread.previousTimeReduction)/(2.32*timeReduction);
for(int i=0;i < Threads.numThreads;i++){
totBestMoveChanges+=Threads.pos[i]->bestMoveChanges;
Threads.pos[i]->bestMoveChanges=0;
}
double bestMoveInstability=1+2*totBestMoveChanges/Threads.numThreads;
double totalTime=time_optimum()*fallingEval*reduction*bestMoveInstability;
if(rm->size==1)
totalTime=min(500.0,totalTime);
if(time_elapsed()> totalTime){
if(Threads.ponder)
Threads.stopOnPonderhit=true;
else
Threads.stop=true;
}
else if(Threads.increaseDepth
&&!Threads.ponder
&&time_elapsed()> totalTime*0.58)
Threads.increaseDepth=false;
else
Threads.increaseDepth=true;
}
mainThread.iterValue[iterIdx]=bestValue;
iterIdx=(iterIdx+1)&3;
}
if(pos->threadIdx!=0)
return;
mainThread.previousTimeReduction=timeReduction;
#if 0
if(skill.enabled())
std::swap(rm[0],*std::find(rm.begin(),
rm.end(),skill.best_move(multiPV)));
#endif
}
INLINE Value search_node(Position*pos,Stack*ss,Value alpha,Value beta,
Depth depth,bool cutNode,const int NT)
{
const bool PvNode=NT==PV;
const bool rootNode=PvNode&&ss->ply==0;
const Depth maxNextDepth=rootNode?depth:depth+1;
if(pos->st->pliesFromNull >=3
&&alpha < VALUE_DRAW
&&!rootNode
&&has_game_cycle(pos,ss->ply))
{
alpha=value_draw(pos);
if(alpha >=beta)
return alpha;
}
if(depth <=0)
return PvNode
?checkers()
?qsearch_PV_true(pos,ss,alpha,beta,0)
:qsearch_PV_false(pos,ss,alpha,beta,0)
:checkers()
?qsearch_NonPV_true(pos,ss,alpha,0)
:qsearch_NonPV_false(pos,ss,alpha,0);
assert(-VALUE_INFINITE <=alpha&&alpha < beta&&beta <=VALUE_INFINITE);
assert(PvNode||(alpha==beta-1));
assert(0 < depth&&depth < MAX_PLY);
assert(!(PvNode&&cutNode));
Move pv[MAX_PLY+1],capturesSearched[32],quietsSearched[64];
TTEntry*tte;
Key posKey;
Move ttMove,move,excludedMove,bestMove;
Depth extension,newDepth;
Value bestValue,value,ttValue,eval,maxValue,probCutBeta;
bool formerPv,givesCheck,improving,didLMR;
bool captureOrPromotion,inCheck,doFullDepthSearch,moveCountPruning;
bool ttCapture,singularQuietLMR;
Piece movedPiece;
int moveCount,captureCount,quietCount;
inCheck=checkers();
moveCount=captureCount=quietCount=ss->moveCount=0;
bestValue=-VALUE_INFINITE;
maxValue=VALUE_INFINITE;
if(load_rlx(pos->resetCalls)){
store_rlx(pos->resetCalls,false);
pos->callsCnt=Limits.nodes?min(1024,Limits.nodes/1024):1024;
}
if(--pos->callsCnt <=0){
for(int idx=0;idx < Threads.numThreads;idx++)
store_rlx(Threads.pos[idx]->resetCalls,true);
check_time();
}
if(PvNode&&pos->selDepth < ss->ply)
pos->selDepth=ss->ply;
if(!rootNode){
if(load_rlx(Threads.stop)||is_draw(pos)||ss->ply >=MAX_PLY)
return ss->ply >=MAX_PLY&&!inCheck?evaluate(pos)
:value_draw(pos);
if(PvNode){
alpha=max(mated_in(ss->ply),alpha);
beta=min(mate_in(ss->ply+1),beta);
if(alpha >=beta)
return alpha;
}else{
if(alpha < mated_in(ss->ply))
return mated_in(ss->ply);
if(alpha >=mate_in(ss->ply+1))
return alpha;
}
}
assert(0 <=ss->ply&&ss->ply < MAX_PLY);
(ss+1)->ttPv=false;
(ss+1)->excludedMove=bestMove=0;
(ss+2)->killers[0]=(ss+2)->killers[1]=0;
Square prevSq=to_sq((ss-1)->currentMove);
if(!rootNode)
(ss+2)->statScore=0;
excludedMove=ss->excludedMove;
posKey=!excludedMove?key():key()^make_key(excludedMove);
tte=tt_probe(posKey,&ss->ttHit);
ttValue=ss->ttHit?value_from_tt(tte_value(tte),ss->ply,rule50_count()):VALUE_NONE;
ttMove=rootNode?pos->rootMoves->move[pos->pvIdx].pv[0]
:ss->ttHit?tte_move(tte):0;
if(!excludedMove)
ss->ttPv=PvNode||(ss->ttHit&&tte_is_pv(tte));
formerPv=ss->ttPv&&!PvNode;
if(ss->ttPv
&&depth > 12
&&ss->ply-1 < MAX_LPH
&&!captured_piece()
&&move_is_ok((ss-1)->currentMove))
lph_update(*pos->lowPlyHistory,ss->ply-1,(ss-1)->currentMove,stat_bonus(depth-5));
pos->ttHitAverage=(ttHitAverageWindow-1)*pos->ttHitAverage/ttHitAverageWindow+ttHitAverageResolution*ss->ttHit;
if(!PvNode
&&ss->ttHit
&&tte_depth(tte)>=depth
&&ttValue!=VALUE_NONE
&&(ttValue >=beta?(tte_bound(tte)&BOUND_LOWER)
:(tte_bound(tte)&BOUND_UPPER)))
{
if(ttMove){
if(ttValue >=beta){
if(!is_capture_or_promotion(pos,ttMove))
update_quiet_stats(pos,ss,ttMove,stat_bonus(depth),depth);
if((ss-1)->moveCount <=2&&!captured_piece())
update_cm_stats(ss-1,piece_on(prevSq),prevSq,-stat_bonus(depth+1));
}
else if(!is_capture_or_promotion(pos,ttMove)){
int penalty=-stat_bonus(depth);
history_update(*pos->mainHistory,stm(),ttMove,penalty);
update_cm_stats(ss,moved_piece(ttMove),to_sq(ttMove),penalty);
}
}
if(rule50_count()< 90)
return ttValue;
}
if(!rootNode&&TB_Cardinality){
int piecesCnt=popcount(pieces());
if(piecesCnt <=TB_Cardinality
&&(piecesCnt < TB_Cardinality||depth >=TB_ProbeDepth)
&&rule50_count()==0
&&!can_castle_any())
{
int found,wdl=TB_probe_wdl(pos,&found);
if(found){
pos->tbHits++;
int drawScore=TB_UseRule50?1:0;
value=wdl <-drawScore?VALUE_MATED_IN_MAX_PLY+ss->ply+1
:wdl > drawScore?VALUE_MATE_IN_MAX_PLY-ss->ply-1
:VALUE_DRAW+2*wdl*drawScore;
int b=wdl <-drawScore?BOUND_UPPER
:wdl > drawScore?BOUND_LOWER:BOUND_EXACT;
if(b==BOUND_EXACT
||(b==BOUND_LOWER?value >=beta:value <=alpha))
{
tte_save(tte,posKey,value_to_tt(value,ss->ply),ss->ttPv,b,
min(MAX_PLY-1,depth+6),0,VALUE_NONE);
return value;
}
if(piecesCnt <=TB_CardinalityDTM){
Value mate=TB_probe_dtm(pos,wdl,&found);
if(found){
mate+=wdl > 0?-ss->ply:ss->ply;
tte_save(tte,posKey,value_to_tt(mate,ss->ply),ss->ttPv,
BOUND_EXACT,min(MAX_PLY-1,depth+6),0,VALUE_NONE);
return mate;
}
}
if(PvNode){
if(b==BOUND_LOWER){
bestValue=value;
if(bestValue > alpha)
alpha=bestValue;
}else
maxValue=value;
}
}
}
}
if(inCheck){
ss->staticEval=eval=VALUE_NONE;
improving=false;
goto moves_loop;
}else if(ss->ttHit){
if((eval=tte_eval(tte))==VALUE_NONE)
eval=evaluate(pos);
ss->staticEval=eval;
if(eval==VALUE_DRAW)
eval=value_draw(pos);
if(ttValue!=VALUE_NONE
&&(tte_bound(tte)&(ttValue > eval?BOUND_LOWER:BOUND_UPPER)))
eval=ttValue;
}else{
if((ss-1)->currentMove!=MOVE_NULL)
ss->staticEval=eval=evaluate(pos);
else
ss->staticEval=eval=-(ss-1)->staticEval+2*Tempo;
tte_save(tte,posKey,VALUE_NONE,ss->ttPv,BOUND_NONE,DEPTH_NONE,0,
eval);
}
if(move_is_ok((ss-1)->currentMove)
&&!(ss-1)->checkersBB
&&!captured_piece())
{
int bonus=clamp(-depth*4*((ss-1)->staticEval+ss->staticEval-2*Tempo),-1000,1000);
history_update(*pos->mainHistory,!stm(),(ss-1)->currentMove,bonus);
}
improving=(ss-2)->staticEval==VALUE_NONE
?(ss->staticEval >(ss-4)->staticEval||(ss-4)->staticEval==VALUE_NONE)
:ss->staticEval >(ss-2)->staticEval;
if(!PvNode
&&depth < 9
&&eval-futility_margin(depth,improving)>=beta
&&eval < VALUE_KNOWN_WIN)
return eval;
if(!PvNode
&&(ss-1)->currentMove!=MOVE_NULL
&&(ss-1)->statScore < 24185
&&eval >=beta
&&eval >=ss->staticEval
&&ss->staticEval >=beta-24*depth-34*improving+162*ss->ttPv+159
&&!excludedMove
&&non_pawn_material_c(stm())
&&(ss->ply >=pos->nmpMinPly||stm()!=pos->nmpColor))
{
assert(eval-beta >=0);
Depth R=(1062+68*depth)/256+min((eval-beta)/190,3);
ss->currentMove=MOVE_NULL;
ss->history=&(*pos->counterMoveHistory)[0][0][0][0];
do_null_move(pos);
ss->endMoves=(ss-1)->endMoves;
Value nullValue=-search_NonPV(pos,ss+1,-beta,depth-R,!cutNode);
undo_null_move(pos);
if(nullValue >=beta){
if(nullValue >=VALUE_TB_WIN_IN_MAX_PLY)
nullValue=beta;
if(pos->nmpMinPly||(abs(beta)< VALUE_KNOWN_WIN&&depth < 14))
return nullValue;
assert(!pos->nmpMinPly);
pos->nmpMinPly=ss->ply+3*(depth-R)/4;
pos->nmpColor=stm();
Value v=search_NonPV(pos,ss,beta-1,depth-R,false);
pos->nmpMinPly=0;
if(v >=beta)
return nullValue;
}
}
probCutBeta=beta+209-44*improving;
if(!PvNode
&&depth > 4
&&abs(beta)< VALUE_TB_WIN_IN_MAX_PLY
&&!(ss->ttHit
&&tte_depth(tte)>=depth-3
&&ttValue!=VALUE_NONE
&&ttValue < probCutBeta))
{
mp_init_pc(pos,ttMove,probCutBeta-ss->staticEval);
int probCutCount=2+2*cutNode;
bool ttPv=ss->ttPv;
ss->ttPv=false;
while((move=next_move(pos,0))
&&probCutCount)
if(move!=excludedMove&&is_legal(pos,move)){
assert(is_capture_or_promotion(pos,move));
assert(depth >=5);
captureOrPromotion=true;
probCutCount--;
ss->currentMove=move;
ss->history=&(*pos->counterMoveHistory)[inCheck][captureOrPromotion][moved_piece(move)][to_sq(move)];
givesCheck=gives_check(pos,ss,move);
do_move(pos,move,givesCheck);
value=givesCheck
?-qsearch_NonPV_true(pos,ss+1,-probCutBeta,0)
:-qsearch_NonPV_false(pos,ss+1,-probCutBeta,0);
if(value >=probCutBeta)
value=-search_NonPV(pos,ss+1,-probCutBeta,depth-4,!cutNode);
undo_move(pos,move);
if(value >=probCutBeta){
if(!(ss->ttHit
&&tte_depth(tte)>=depth-3
&&ttValue!=VALUE_NONE))
tte_save(tte,posKey,value_to_tt(value,ss->ply),ttPv,
BOUND_LOWER,depth-3,move,ss->staticEval);
return value;
}
}
ss->ttPv=ttPv;
}
if(PvNode&&depth >=6&&!ttMove)
depth-=2;
moves_loop:
ttCapture=ttMove&&is_capture_or_promotion(pos,ttMove);
probCutBeta=beta+400;
if(inCheck
&&!PvNode
&&depth >=4
&&ttCapture
&&(tte_bound(tte)&BOUND_LOWER)
&&tte_depth(tte)>=depth-3
&&ttValue >=probCutBeta
&&abs(ttValue)<=VALUE_KNOWN_WIN
&&abs(beta)<=VALUE_KNOWN_WIN
)
return probCutBeta;
PieceToHistory*cmh=(ss-1)->history;
PieceToHistory*fmh=(ss-2)->history;
PieceToHistory*fmh2=(ss-4)->history;
PieceToHistory*fmh3=(ss-6)->history;
mp_init(pos,ttMove,depth,ss->ply);
value=bestValue;
singularQuietLMR=moveCountPruning=false;
bool likelyFailLow=PvNode
&&ttMove
&&(tte_bound(tte)&BOUND_UPPER)
&&tte_depth(tte)>=depth;
while((move=next_move(pos,moveCountPruning))){
assert(move_is_ok(move));
if(move==excludedMove)
continue;
if(rootNode){
int idx;
for(idx=pos->pvIdx;idx < pos->pvLast;idx++)
if(pos->rootMoves->move[idx].pv[0]==move)
break;
if(idx==pos->pvLast)
continue;
}
if(!rootNode&&!is_legal(pos,move))
continue;
ss->moveCount=++moveCount;
/* Suppress UCI currmove output for WinBoard mode
if(rootNode&&pos->threadIdx==0&&time_elapsed()> 3000){
char buf[16];
printf("info depth %d currmove %s currmovenumber %d\n",
depth,
uci_move(buf,move,is_chess960()),
moveCount+pos->pvIdx);
fflush(stdout);
}
*/
if(PvNode)
(ss+1)->pv=NULL;
extension=0;
captureOrPromotion=is_capture_or_promotion(pos,move);
movedPiece=moved_piece(move);
givesCheck=gives_check(pos,ss,move);
newDepth=depth-1;
if(!rootNode
&&non_pawn_material_c(stm())
&&bestValue > VALUE_TB_LOSS_IN_MAX_PLY)
{
moveCountPruning=moveCount >=futility_move_count(improving,depth);
int lmrDepth=max(newDepth-reduction(improving,depth,moveCount),0);
if(captureOrPromotion
||givesCheck)
{
if(!givesCheck
&&lmrDepth < 1
&&(*pos->captureHistory)[movedPiece][to_sq(move)][type_of_p(piece_on(to_sq(move)))] < 0)
continue;
if(!see_test(pos,move,-218*depth))
continue;
}else{
if(lmrDepth < 4
&&(*cmh)[movedPiece][to_sq(move)] < CounterMovePruneThreshold
&&(*fmh)[movedPiece][to_sq(move)] < CounterMovePruneThreshold)
continue;
if(lmrDepth < 7
&&!inCheck
&&ss->staticEval+174+157*lmrDepth <=alpha
&&(*cmh)[movedPiece][to_sq(move)]
+(*fmh)[movedPiece][to_sq(move)]
+(*fmh2)[movedPiece][to_sq(move)]
+(*fmh3)[movedPiece][to_sq(move)]/3 < 28255)
continue;
if(!see_test(pos,move,-(30-min(lmrDepth,18))*lmrDepth*lmrDepth))
continue;
}
}
if(depth >=7
&&move==ttMove
&&!rootNode
&&!excludedMove
&&abs(ttValue)< VALUE_KNOWN_WIN
&&(tte_bound(tte)&BOUND_LOWER)
&&tte_depth(tte)>=depth-3)
{
Value singularBeta=ttValue-((formerPv+4)*depth)/2;
Depth singularDepth=(depth-1+3*formerPv)/2;
ss->excludedMove=move;
Move cm=ss->countermove;
Move k1=ss->mpKillers[0],k2=ss->mpKillers[1];
value=search_NonPV(pos,ss,singularBeta-1,singularDepth,cutNode);
ss->excludedMove=0;
if(value < singularBeta){
extension=1;
singularQuietLMR=!ttCapture;
if(!PvNode&&value < singularBeta-140)
extension=2;
}
else if(singularBeta >=beta)
return singularBeta;
else if(ttValue >=beta){
mp_init(pos,ttMove,depth,ss->ply);
ss->stage++;
ss->countermove=cm;
ss->mpKillers[0]=k1;ss->mpKillers[1]=k2;
ss->excludedMove=move;
value=search_NonPV(pos,ss,beta-1,(depth+3)/2,cutNode);
ss->excludedMove=0;
if(value >=beta)
return beta;
}
mp_init(pos,ttMove,depth,ss->ply);
ss->stage++;
ss->countermove=cm;
ss->mpKillers[0]=k1;ss->mpKillers[1]=k2;
}
newDepth+=extension;
prefetch(tt_first_entry(key_after(pos,move)));
ss->currentMove=move;
ss->history=&(*pos->counterMoveHistory)[inCheck][captureOrPromotion][movedPiece][to_sq(move)];
do_move(pos,move,givesCheck);
if(rootNode)pos->st[-1].key^=pos->rootKeyFlip;
if(depth >=3
&&moveCount > 1+2*rootNode
&&(!captureOrPromotion
||moveCountPruning
||ss->staticEval+PieceValue[EG][captured_piece()] <=alpha
||cutNode
||(!PvNode&&!formerPv&&(*pos->captureHistory)[movedPiece][to_sq(move)][type_of_p(captured_piece())] < 3678)
||pos->ttHitAverage < 432*ttHitAverageResolution*ttHitAverageWindow/1024)
&&(!PvNode||ss->ply > 1||pos->threadIdx % 4!=3))
{
Depth r=reduction(improving,depth,moveCount);
if(pos->ttHitAverage > 537*ttHitAverageResolution*ttHitAverageWindow/1024)
r--;
if(ss->ttPv&&!likelyFailLow)
r-=2;
if((rootNode||!PvNode)&&pos->rootDepth > 10&&pos->bestMoveChanges <=2)
r++;
if((ss-1)->moveCount > 13)
r--;
if(singularQuietLMR)
r--;
if(!captureOrPromotion){
if(ttCapture)
r++;
if(rootNode)
r+=pos->failedHighCnt*pos->failedHighCnt*moveCount/512;
if(cutNode)
r+=2;
ss->statScore=(*cmh)[movedPiece][to_sq(move)]
+(*fmh)[movedPiece][to_sq(move)]
+(*fmh2)[movedPiece][to_sq(move)]
+(*pos->mainHistory)[!stm()][from_to(move)]
-4741;
if(!inCheck)
r-=ss->statScore/14790;
}
Depth d=clamp(newDepth-r,1,newDepth+(r <-1&&moveCount <=5));
value=-search_NonPV(pos,ss+1,-(alpha+1),d,1);
doFullDepthSearch=value > alpha&&d < newDepth;
didLMR=true;
}else{
doFullDepthSearch=!PvNode||moveCount > 1;
didLMR=false;
}
if(doFullDepthSearch){
value=-search_NonPV(pos,ss+1,-(alpha+1),newDepth,!cutNode);
if(didLMR&&!captureOrPromotion){
int bonus=value > alpha?stat_bonus(newDepth)
:-stat_bonus(newDepth);
update_cm_stats(ss,movedPiece,to_sq(move),bonus);
}
}
if(PvNode
&&(moveCount==1||(value > alpha&&(rootNode||value < beta))))
{
(ss+1)->pv=pv;
(ss+1)->pv[0]=0;
value=-search_PV(pos,ss+1,-beta,-alpha,min(maxNextDepth,newDepth));
}
if(rootNode)pos->st[-1].key^=pos->rootKeyFlip;
undo_move(pos,move);
assert(value >-VALUE_INFINITE&&value < VALUE_INFINITE);
if(load_rlx(Threads.stop))
return 0;
if(rootNode){
RootMove*rm=NULL;
for(int idx=0;idx < pos->rootMoves->size;idx++)
if(pos->rootMoves->move[idx].pv[0]==move){
rm=&pos->rootMoves->move[idx];
break;
}
if(moveCount==1||value > alpha){
rm->score=value;
rm->selDepth=pos->selDepth;
rm->pvSize=1;
assert((ss+1)->pv);
for(Move*m=(ss+1)->pv;*m;++m)
rm->pv[rm->pvSize++]=*m;
if(moveCount > 1)
pos->bestMoveChanges++;
}else
rm->score=-VALUE_INFINITE;
}
if(value > bestValue){
bestValue=value;
if(value > alpha){
bestMove=move;
if(PvNode&&!rootNode)
update_pv(ss->pv,move,(ss+1)->pv);
if(PvNode&&value < beta)
alpha=value;
else{
assert(value >=beta);
ss->statScore=0;
break;
}
}
}
if(move!=bestMove){
if(captureOrPromotion&&captureCount < 32)
capturesSearched[captureCount++]=move;
else if(!captureOrPromotion&&quietCount < 64)
quietsSearched[quietCount++]=move;
}
}
/*
if(Threads.stop)
return VALUE_DRAW;
*/
if(!moveCount)
bestValue=excludedMove?alpha
:inCheck?mated_in(ss->ply):VALUE_DRAW;
else if(bestMove){
if(!is_capture_or_promotion(pos,bestMove)){
int bonus=bestValue > beta+PawnValueMg
?stat_bonus(depth+1)
:min(stat_bonus(depth+1),stat_bonus(depth));
update_quiet_stats(pos,ss,bestMove,bonus,depth);
for(int i=0;i < quietCount;i++){
history_update(*pos->mainHistory,stm(),quietsSearched[i],-bonus);
update_cm_stats(ss,moved_piece(quietsSearched[i]),
to_sq(quietsSearched[i]),-bonus);
}
}
update_capture_stats(pos,bestMove,capturesSearched,captureCount,
stat_bonus(depth+1));
if(((ss-1)->moveCount==1+(ss-1)->ttHit||(ss-1)->currentMove==(ss-1)->killers[0])
&&!captured_piece())
update_cm_stats(ss-1,piece_on(prevSq),prevSq,-stat_bonus(depth+1));
}
else if((depth >=3||PvNode)
&&!captured_piece())
update_cm_stats(ss-1,piece_on(prevSq),prevSq,stat_bonus(depth));
if(PvNode)
bestValue=min(bestValue,maxValue);
if(bestValue <=alpha)
ss->ttPv=ss->ttPv||((ss-1)->ttPv&&depth > 3);
else if(depth > 3)
ss->ttPv=ss->ttPv&&(ss+1)->ttPv;
if(!excludedMove&&!(rootNode&&pos->pvIdx))
tte_save(tte,posKey,value_to_tt(bestValue,ss->ply),ss->ttPv,
bestValue >=beta?BOUND_LOWER:
PvNode&&bestMove?BOUND_EXACT:BOUND_UPPER,
depth,bestMove,ss->staticEval);
assert(bestValue >-VALUE_INFINITE&&bestValue < VALUE_INFINITE);
return bestValue;
}
static NOINLINE Value search_PV(Position*pos,Stack*ss,Value alpha,
Value beta,Depth depth)
{
return search_node(pos,ss,alpha,beta,depth,0,PV);
}
static NOINLINE Value search_NonPV(Position*pos,Stack*ss,Value alpha,
Depth depth,bool cutNode)
{
return search_node(pos,ss,alpha,alpha+1,depth,cutNode,NonPV);
}
INLINE Value qsearch_node(Position*pos,Stack*ss,Value alpha,Value beta,
Depth depth,const int NT,const bool InCheck)
{
const bool PvNode=NT==PV;
assert(InCheck==(bool)checkers());
assert(alpha >=-VALUE_INFINITE&&alpha < beta&&beta <=VALUE_INFINITE);
assert(PvNode||(alpha==beta-1));
assert(depth <=0);
Move pv[MAX_PLY+1];
TTEntry*tte;
Key posKey;
Move ttMove,move,bestMove;
Value bestValue,value,ttValue,futilityValue,futilityBase,oldAlpha;
bool pvHit,givesCheck;
Depth ttDepth;
int moveCount;
if(PvNode){
oldAlpha=alpha;
(ss+1)->pv=pv;
ss->pv[0]=0;
}
bestMove=0;
moveCount=0;
if(is_draw(pos)||ss->ply >=MAX_PLY)
return ss->ply >=MAX_PLY&&!InCheck?evaluate(pos):VALUE_DRAW;
assert(0 <=ss->ply&&ss->ply < MAX_PLY);
ttDepth=InCheck||depth >=DEPTH_QS_CHECKS?DEPTH_QS_CHECKS
:DEPTH_QS_NO_CHECKS;
posKey=key();
tte=tt_probe(posKey,&ss->ttHit);
ttValue=ss->ttHit?value_from_tt(tte_value(tte),ss->ply,rule50_count()):VALUE_NONE;
ttMove=ss->ttHit?tte_move(tte):0;
pvHit=ss->ttHit&&tte_is_pv(tte);
if(!PvNode
&&ss->ttHit
&&tte_depth(tte)>=ttDepth
&&ttValue!=VALUE_NONE
&&(ttValue >=beta?(tte_bound(tte)&BOUND_LOWER)
:(tte_bound(tte)&BOUND_UPPER)))
return ttValue;
if(InCheck){
ss->staticEval=VALUE_NONE;
bestValue=futilityBase=-VALUE_INFINITE;
}else{
if(ss->ttHit){
if((ss->staticEval=bestValue=tte_eval(tte))==VALUE_NONE)
ss->staticEval=bestValue=evaluate(pos);
if(ttValue!=VALUE_NONE
&&(tte_bound(tte)&(ttValue > bestValue?BOUND_LOWER:BOUND_UPPER)))
bestValue=ttValue;
}else
ss->staticEval=bestValue=
(ss-1)->currentMove!=MOVE_NULL?evaluate(pos)
:-(ss-1)->staticEval+2*Tempo;
if(bestValue >=beta){
if(!ss->ttHit)
tte_save(tte,posKey,value_to_tt(bestValue,ss->ply),false,
BOUND_LOWER,DEPTH_NONE,0,ss->staticEval);
return bestValue;
}
if(PvNode&&bestValue > alpha)
alpha=bestValue;
futilityBase=bestValue+155;
}
ss->history=&(*pos->counterMoveHistory)[0][0][0][0];
mp_init_q(pos,ttMove,depth,to_sq((ss-1)->currentMove));
while((move=next_move(pos,0))){
assert(move_is_ok(move));
givesCheck=gives_check(pos,ss,move);
moveCount++;
if(bestValue > VALUE_TB_LOSS_IN_MAX_PLY
&&!givesCheck
&&futilityBase >-VALUE_KNOWN_WIN
&&type_of_m(move)!=PROMOTION)
{
if(moveCount > 2)
continue;
futilityValue=futilityBase+PieceValue[EG][piece_on(to_sq(move))];
if(futilityValue <=alpha){
bestValue=max(bestValue,futilityValue);
continue;
}
if(futilityBase <=alpha&&!see_test(pos,move,1)){
bestValue=max(bestValue,futilityBase);
continue;
}
}
if(bestValue > VALUE_TB_LOSS_IN_MAX_PLY
&&!see_test(pos,move,0))
continue;
prefetch(tt_first_entry(key_after(pos,move)));
if(!is_legal(pos,move)){
moveCount--;
continue;
}
ss->currentMove=move;
bool captureOrPromotion=is_capture_or_promotion(pos,move);
ss->history=&(*pos->counterMoveHistory)[InCheck]
[captureOrPromotion]
[moved_piece(move)]
[to_sq(move)];
if(!captureOrPromotion
&&bestValue > VALUE_TB_LOSS_IN_MAX_PLY
&&(*(ss-1)->history)[moved_piece(move)][to_sq(move)] < CounterMovePruneThreshold
&&(*(ss-2)->history)[moved_piece(move)][to_sq(move)] < CounterMovePruneThreshold)
continue;
do_move(pos,move,givesCheck);
value=PvNode?givesCheck
?-qsearch_PV_true(pos,ss+1,-beta,-alpha,depth-1)
:-qsearch_PV_false(pos,ss+1,-beta,-alpha,depth-1)
:givesCheck
?-qsearch_NonPV_true(pos,ss+1,-beta,depth-1)
:-qsearch_NonPV_false(pos,ss+1,-beta,depth-1);
undo_move(pos,move);
assert(value >-VALUE_INFINITE&&value < VALUE_INFINITE);
if(value > bestValue){
bestValue=value;
if(value > alpha){
bestMove=move;
if(PvNode)
update_pv(ss->pv,move,(ss+1)->pv);
if(PvNode&&value < beta)
alpha=value;
else
break;
}
}
}
if(InCheck&&bestValue==-VALUE_INFINITE)
return mated_in(ss->ply);
tte_save(tte,posKey,value_to_tt(bestValue,ss->ply),pvHit,
bestValue >=beta?BOUND_LOWER:
PvNode&&bestValue > oldAlpha?BOUND_EXACT:BOUND_UPPER,
ttDepth,bestMove,ss->staticEval);
assert(bestValue >-VALUE_INFINITE&&bestValue < VALUE_INFINITE);
return bestValue;
}
static NOINLINE Value qsearch_PV_true(Position*pos,Stack*ss,Value alpha,
Value beta,Depth depth)
{
return qsearch_node(pos,ss,alpha,beta,depth,PV,true);
}
static NOINLINE Value qsearch_PV_false(Position*pos,Stack*ss,Value alpha,
Value beta,Depth depth)
{
return qsearch_node(pos,ss,alpha,beta,depth,PV,false);
}
static NOINLINE Value qsearch_NonPV_true(Position*pos,Stack*ss,Value alpha,
Depth depth)
{
return qsearch_node(pos,ss,alpha,alpha+1,depth,NonPV,true);
}
static NOINLINE Value qsearch_NonPV_false(Position*pos,Stack*ss,Value alpha,
Depth depth)
{
return qsearch_node(pos,ss,alpha,alpha+1,depth,NonPV,false);
}
#define rm_lt(m1,m2) ((m1).tbRank != (m2).tbRank ? (m1).tbRank < (m2).tbRank : (m1).score != (m2).score ? (m1).score < (m2).score : (m1).previousScore < (m2).previousScore)
static void stable_sort(RootMove*rm,int num)
{
int i,j;
for(i=1;i < num;i++)
if(rm_lt(rm[i-1],rm[i])){
RootMove tmp=rm[i];
rm[i]=rm[i-1];
for(j=i-1;j > 0&&rm_lt(rm[j-1],tmp);j--)
rm[j]=rm[j-1];
rm[j]=tmp;
}
}
static Value value_to_tt(Value v,int ply)
{
assert(v!=VALUE_NONE);
return v >=VALUE_TB_WIN_IN_MAX_PLY?v+ply
:v <=VALUE_TB_LOSS_IN_MAX_PLY?v-ply:v;
}
static Value value_from_tt(Value v,int ply,int r50c)
{
if(v==VALUE_NONE)
return VALUE_NONE;
if(v >=VALUE_TB_WIN_IN_MAX_PLY){
if(v >=VALUE_MATE_IN_MAX_PLY&&VALUE_MATE-v > 99-r50c)
return VALUE_MATE_IN_MAX_PLY-1;
return v-ply;
}
if(v <=VALUE_TB_LOSS_IN_MAX_PLY){
if(v <=VALUE_MATED_IN_MAX_PLY&&VALUE_MATE+v > 99-r50c)
return VALUE_MATED_IN_MAX_PLY+1;
return v+ply;
}
return v;
}
static void update_pv(Move*pv,Move move,Move*childPv)
{
for(*pv++=move;childPv&&*childPv;)
*pv++=*childPv++;
*pv=0;
}
static void update_cm_stats(Stack*ss,Piece pc,Square s,int bonus)
{
if(move_is_ok((ss-1)->currentMove))
cms_update(*(ss-1)->history,pc,s,bonus);
if(move_is_ok((ss-2)->currentMove))
cms_update(*(ss-2)->history,pc,s,bonus);
if(ss->checkersBB)
return;
if(move_is_ok((ss-4)->currentMove))
cms_update(*(ss-4)->history,pc,s,bonus);
if(move_is_ok((ss-6)->currentMove))
cms_update(*(ss-6)->history,pc,s,bonus);
}
static void update_capture_stats(const Position*pos,Move move,Move*captures,
int captureCnt,int bonus)
{
Piece moved_piece=moved_piece(move);
int captured=type_of_p(piece_on(to_sq(move)));
if(is_capture_or_promotion(pos,move))
cpth_update(*pos->captureHistory,moved_piece,to_sq(move),captured,bonus);
for(int i=0;i < captureCnt;i++){
moved_piece=moved_piece(captures[i]);
captured=type_of_p(piece_on(to_sq(captures[i])));
cpth_update(*pos->captureHistory,moved_piece,to_sq(captures[i]),captured,-bonus);
}
}
static void update_quiet_stats(const Position*pos,Stack*ss,Move move,
int bonus,Depth depth)
{
if(ss->killers[0]!=move){
ss->killers[1]=ss->killers[0];
ss->killers[0]=move;
}
Color c=stm();
history_update(*pos->mainHistory,c,move,bonus);
update_cm_stats(ss,moved_piece(move),to_sq(move),bonus);
if(type_of_p(moved_piece(move))!=PAWN)
history_update(*pos->mainHistory,c,reverse_move(move),-bonus);
if(move_is_ok((ss-1)->currentMove)){
Square prevSq=to_sq((ss-1)->currentMove);
(*pos->counterMoves)[piece_on(prevSq)][prevSq]=move;
}
if(depth > 11&&ss->ply < MAX_LPH)
lph_update(*pos->lowPlyHistory,ss->ply,move,stat_bonus(depth-7));
}
#if 0
Move Skill::pick_best(size_t multiPV){
const RootMoves&rm=Threads.main()->rootMoves;
static PRNG rng(now());
Value topScore=rm[0].score;
int delta=std::min(topScore-rm[multiPV-1].score,PawnValueMg);
int weakness=120-2*level;
int maxScore=-VALUE_INFINITE;
for(size_t i=0;i < multiPV;++i)
{
int push=(weakness*int(topScore-rm[i].score)
+delta*(rng.rand<unsigned>()% weakness))/128;
if(rm[i].score+push > maxScore)
{
maxScore=rm[i].score+push;
best=rm[i].pv[0];
}
}
return best;
}
#endif
static void check_time(void)
{
TimePoint elapsed=time_elapsed();
if(Threads.ponder)
return;
if((use_time_management()&&elapsed > time_maximum()-10)
||(Limits.movetime&&elapsed >=Limits.movetime)
||(Limits.nodes&&threads_nodes_searched()>=Limits.nodes))
Threads.stop=1;
}
static void uci_print_pv(Position*pos,Depth depth,Value alpha,Value beta)
{
TimePoint elapsed=time_elapsed()+1;
RootMoves*rm=pos->rootMoves;
int pvIdx=pos->pvIdx;
int multiPV=min(option_value(OPT_MULTI_PV),rm->size);
uint64_t nodes_searched=threads_nodes_searched();
uint64_t tbhits=threads_tb_hits();
char buf[16];
flockfile(stdout);
for(int i=0;i < multiPV;i++){
bool updated=rm->move[i].score!=-VALUE_INFINITE;
if(depth==1&&!updated&&i > 0)
continue;
Depth d=updated?depth:max(1,depth-1);
Value v=updated?rm->move[i].score:rm->move[i].previousScore;
if(v==-VALUE_INFINITE)
v=VALUE_ZERO;
bool tb=TB_RootInTB&&abs(v)< VALUE_MATE_IN_MAX_PLY;
if(tb)
v=rm->move[i].tbScore;
if(abs(v)> VALUE_MATE-MAX_MATE_PLY
&&rm->move[i].pvSize < VALUE_MATE-abs(v)
&&TB_MaxCardinalityDTM > 0)
TB_expand_mate(pos,&rm->move[i]);
/* WinBoard format: depth score time nodes pv */
/* Custom PV depth: actual_depth * 1001 for Chessmaster */
int wb_depth=d*1001;
int wb_score=v*100/PawnValueEg;
/* Check for mate scores */
if(abs(v)>=VALUE_MATE_IN_MAX_PLY){
if(v > 0)wb_score=100000-(VALUE_MATE-v);
else wb_score=-100000+(VALUE_MATE+v);
}
printf("%d %d %"PRIi64" %"PRIu64,wb_depth,wb_score,elapsed/10,nodes_searched);
for(int idx=0;idx < rm->move[i].pvSize;idx++)
printf(" %s",uci_move(buf,rm->move[i].pv[idx],is_chess960()));
printf("\n");
}
fflush(stdout);
funlockfile(stdout);
}
static int extract_ponder_from_tt(RootMove*rm,Position*pos)
{
bool ttHit;
assert(rm->pvSize==1);
if(!rm->pv[0])
return 0;
do_move(pos,rm->pv[0],gives_check(pos,pos->st,rm->pv[0]));
TTEntry*tte=tt_probe(key(),&ttHit);
if(ttHit){
Move m=tte_move(tte);
ExtMove list[MAX_MOVES];
ExtMove*last=generate_legal(pos,list);
for(ExtMove*p=list;p < last;p++)
if(p->move==m){
rm->pv[rm->pvSize++]=m;
break;
}
}
undo_move(pos,rm->pv[0]);
return rm->pvSize > 1;
}
static void TB_rank_root_moves(Position*pos,RootMoves*rm)
{
TB_RootInTB=false;
TB_UseRule50=option_value(OPT_SYZ_50_MOVE);
TB_ProbeDepth=option_value(OPT_SYZ_PROBE_DEPTH);
TB_Cardinality=option_value(OPT_SYZ_PROBE_LIMIT);
bool dtz_available=true,dtm_available=false;
if(TB_Cardinality > TB_MaxCardinality){
TB_Cardinality=TB_MaxCardinality;
TB_ProbeDepth=0;
}
TB_CardinalityDTM=option_value(OPT_SYZ_USE_DTM)
?min(TB_Cardinality,TB_MaxCardinalityDTM)
:0;
if(TB_Cardinality >=popcount(pieces())&&!can_castle_any()){
TB_RootInTB=TB_root_probe_dtz(pos,rm);
if(!TB_RootInTB){
dtz_available=false;
TB_RootInTB=TB_root_probe_wdl(pos,rm);
}
if(TB_RootInTB&&TB_CardinalityDTM >=popcount(pieces()))
dtm_available=TB_root_probe_dtm(pos,rm);
}
if(TB_RootInTB){
stable_sort(rm->move,rm->size);
if(dtm_available||dtz_available||rm->move[0].tbRank <=0)
TB_Cardinality=0;
}
else
for(int i=0;i < rm->size;i++)
rm->move[i].tbRank=0;
}
void start_thinking(Position*root,bool ponderMode)
{
if(Threads.searching)
thread_wait_until_sleeping(threads_main());
Threads.stopOnPonderhit=false;
Threads.stop=false;
Threads.increaseDepth=true;
Threads.ponder=ponderMode;
ExtMove list[MAX_MOVES];
ExtMove*end=generate_legal(root,list);
if(Limits.numSearchmoves){
ExtMove*p=list;
for(ExtMove*m=p;m < end;m++)
for(int i=0;i < Limits.numSearchmoves;i++)
if(m->move==Limits.searchmoves[i]){
(p++)->move=m->move;
break;
}
end=p;
}
RootMoves*moves=Threads.pos[0]->rootMoves;
moves->size=end-list;
for(int i=0;i < moves->size;i++)
moves->move[i].pv[0]=list[i].move;
TB_rank_root_moves(root,moves);
for(int idx=0;idx < Threads.numThreads;idx++){
Position*pos=Threads.pos[idx];
pos->selDepth=0;
pos->nmpMinPly=0;
pos->rootDepth=0;
pos->nodes=pos->tbHits=0;
RootMoves*rm=pos->rootMoves;
rm->size=end-list;
for(int i=0;i < rm->size;i++){
rm->move[i].pvSize=1;
rm->move[i].pv[0]=moves->move[i].pv[0];
rm->move[i].score=-VALUE_INFINITE;
rm->move[i].previousScore=-VALUE_INFINITE;
rm->move[i].selDepth=0;
rm->move[i].tbRank=moves->move[i].tbRank;
rm->move[i].tbScore=moves->move[i].tbScore;
}
memcpy(pos,root,offsetof(Position,moveList));
int n=max(7,root->st->pliesFromNull);
for(int i=0;i <=n;i++)
memcpy(&pos->stack[i],&root->st[i-n],StateSize);
pos->st=pos->stack+n;
(pos->st-1)->endMoves=pos->moveList;
pos_set_check_info(pos);
}
if(TB_RootInTB)
Threads.pos[0]->tbHits=end-list;
Threads.searching=true;
thread_wake_up(threads_main(),THREAD_SEARCH);
}
//
#ifndef _WIN32
#endif
static void on_clear_hash(Option*opt)
{
(void)opt;
if(settings.ttSize)
search_clear();
}
static void on_hash_size(Option*opt)
{
delayedSettings.ttSize=opt->value;
}
static void on_numa(Option*opt)
{
#ifdef NUMA
read_numa_nodes(opt->valString);
#else
(void)opt;
#endif
}
static void on_threads(Option*opt)
{
delayedSettings.numThreads=opt->value;
}
static void on_tb_path(Option*opt)
{
TB_init(opt->valString);
}
static void on_large_pages(Option*opt)
{
delayedSettings.largePages=opt->value;
}
static void on_book_file(Option*opt)
{
pb_init(&polybook,opt->valString);
}
static void on_book_file2(Option*opt)
{
pb_init(&polybook2,opt->valString);
}
static void on_best_book_move(Option*opt)
{
pb_set_best_book_move(opt->value);
}
static void on_book_depth(Option*opt)
{
pb_set_book_depth(opt->value);
}
#ifdef IS_64BIT
#define MAXHASHMB 33554432
#else
#define MAXHASHMB 2048
#endif
static Option optionsMap[]={
{"Contempt",OPT_TYPE_SPIN,24,-100,100,NULL,NULL,0,NULL},
{"Analysis Contempt",OPT_TYPE_COMBO,0,0,0,
"Off var Off var White var Black",NULL,0,NULL},
{"Threads",OPT_TYPE_SPIN,1,1,MAX_THREADS,NULL,on_threads,0,NULL},
{"Hash",OPT_TYPE_SPIN,16,1,MAXHASHMB,NULL,on_hash_size,0,NULL},
{"Clear Hash",OPT_TYPE_BUTTON,0,0,0,NULL,on_clear_hash,0,NULL},
{"Ponder",OPT_TYPE_CHECK,0,0,0,NULL,NULL,0,NULL},
{"MultiPV",OPT_TYPE_SPIN,1,1,500,NULL,NULL,0,NULL},
{"Skill Level",OPT_TYPE_SPIN,20,0,20,NULL,NULL,0,NULL},
{"Move Overhead",OPT_TYPE_SPIN,10,0,5000,NULL,NULL,0,NULL},
{"Slow Mover",OPT_TYPE_SPIN,100,10,1000,NULL,NULL,0,NULL},
{"nodestime",OPT_TYPE_SPIN,0,0,10000,NULL,NULL,0,NULL},
{"UCI_AnalyseMode",OPT_TYPE_CHECK,0,0,0,NULL,NULL,0,NULL},
{"UCI_Chess960",OPT_TYPE_CHECK,0,0,0,NULL,NULL,0,NULL},
{"SyzygyPath",OPT_TYPE_STRING,0,0,0,"<empty>",on_tb_path,0,NULL},
{"SyzygyProbeDepth",OPT_TYPE_SPIN,1,1,100,NULL,NULL,0,NULL},
{"Syzygy50MoveRule",OPT_TYPE_CHECK,1,0,0,NULL,NULL,0,NULL},
{"SyzygyProbeLimit",OPT_TYPE_SPIN,7,0,7,NULL,NULL,0,NULL},
{"SyzygyUseDTM",OPT_TYPE_CHECK,1,0,0,NULL,NULL,0,NULL},
{"BookFile",OPT_TYPE_STRING,0,0,0,"<empty>",on_book_file,0,NULL},
{"BookFile2",OPT_TYPE_STRING,0,0,0,"<empty>",on_book_file2,0,NULL},
{"BestBookMove",OPT_TYPE_CHECK,1,0,0,NULL,on_best_book_move,0,NULL},
{"BookDepth",OPT_TYPE_SPIN,255,1,255,NULL,on_book_depth,0,NULL},
#ifdef NNUE
{"EvalFile",OPT_TYPE_STRING,0,0,0,DefaultEvalFile,NULL,0,NULL},
#ifndef NNUE_PURE
{"Use NNUE",OPT_TYPE_COMBO,0,0,0,
"Hybrid var Hybrid var Pure var Classical",NULL,0,NULL},
#endif
#endif
{"LargePages",OPT_TYPE_CHECK,1,0,0,NULL,on_large_pages,0,NULL},
{"NUMA",OPT_TYPE_STRING,0,0,0,"all",on_numa,0,NULL},
{0}
};
void options_init()
{
char*s;
size_t len;
#ifdef NUMA
if(!numaAvail)
optionsMap[OPT_NUMA].type=OPT_TYPE_DISABLED;
#else
optionsMap[OPT_NUMA].type=OPT_TYPE_DISABLED;
#endif
#ifdef _WIN32
if(!large_pages_supported())
optionsMap[OPT_LARGE_PAGES].type=OPT_TYPE_DISABLED;
#endif
#if defined(__linux__) && !defined(MADV_HUGEPAGE)
optionsMap[OPT_LARGE_PAGES].type=OPT_TYPE_DISABLED;
#endif
optionsMap[OPT_SKILL_LEVEL].type=OPT_TYPE_DISABLED;
if(sizeof(size_t)< 8){
optionsMap[OPT_SYZ_PROBE_LIMIT].def=5;
optionsMap[OPT_SYZ_PROBE_LIMIT].maxVal=5;
}
for(Option*opt=optionsMap;opt->name!=NULL;opt++){
if(opt->type==OPT_TYPE_DISABLED)
continue;
switch(opt->type){
case OPT_TYPE_CHECK:
case OPT_TYPE_SPIN:
opt->value=opt->def;
case OPT_TYPE_BUTTON:
break;
case OPT_TYPE_STRING:
opt->valString=strdup(opt->defString);
break;
case OPT_TYPE_COMBO:
s=strstr(opt->defString," var");
len=strlen(opt->defString)-strlen(s);
opt->valString=malloc(len+1);
strncpy(opt->valString,opt->defString,len);
opt->valString[len]=0;
for(s=opt->valString;*s;s++)
*s=tolower(*s);
break;
}
if(opt->onChange)
opt->onChange(opt);
}
}
void options_free(void)
{
for(Option*opt=optionsMap;opt->name!=NULL;opt++)
if(opt->type==OPT_TYPE_STRING)
free(opt->valString);
}
static const char*optTypeStr[]={
"check","spin","button","string","combo"
};
void print_options(void)
{
for(Option*opt=optionsMap;opt->name!=NULL;opt++){
if(opt->type==OPT_TYPE_DISABLED)
continue;
printf("option name %s type %s",opt->name,optTypeStr[opt->type]);
switch(opt->type){
case OPT_TYPE_CHECK:
printf(" default %s",opt->def?"true":"false");
break;
case OPT_TYPE_SPIN:
printf(" default %d min %d max %d",opt->def,opt->minVal,opt->maxVal);
case OPT_TYPE_BUTTON:
break;
case OPT_TYPE_STRING:
case OPT_TYPE_COMBO:
printf(" default %s",opt->defString);
break;
}
printf("\n");
}
fflush(stdout);
}
int option_value(int optIdx)
{
return optionsMap[optIdx].value;
}
const char*option_string_value(int optIdx)
{
return optionsMap[optIdx].valString;
}
const char*option_default_string_value(int optIdx)
{
return optionsMap[optIdx].defString;
}
void option_set_value(int optIdx,int value)
{
Option*opt=&optionsMap[optIdx];
opt->value=value;
if(opt->onChange)
opt->onChange(opt);
}
bool option_set_by_name(char*name,char*value)
{
for(Option*opt=optionsMap;opt->name!=NULL;opt++){
if(opt->type==OPT_TYPE_DISABLED)
continue;
if(strcasecmp(opt->name,name)==0){
int val;
switch(opt->type){
case OPT_TYPE_CHECK:
if(strcmp(value,"true")==0)
opt->value=1;
else if(strcmp(value,"false")==0)
opt->value=0;
else
return true;
break;
case OPT_TYPE_SPIN:
val=atoi(value);
if(val < opt->minVal||val > opt->maxVal)
return true;
opt->value=val;
case OPT_TYPE_BUTTON:
break;
case OPT_TYPE_STRING:
free(opt->valString);
opt->valString=strdup(value);
break;
case OPT_TYPE_COMBO:
free(opt->valString);
opt->valString=strdup(value);
for(char*s=opt->valString;*s;s++)
*s=tolower(*s);
}
if(opt->onChange)
opt->onChange(opt);
return true;
}
}
return false;
}
#ifdef NNUE
#endif
struct settings settings,delayedSettings;
void process_delayed_settings(void)
{
bool ttChange=delayedSettings.ttSize!=settings.ttSize;
bool lpChange=delayedSettings.largePages!=settings.largePages;
bool numaChange=settings.numaEnabled!=delayedSettings.numaEnabled
||(settings.numaEnabled
&&!masks_equal(settings.mask,delayedSettings.mask));
#ifdef NUMA
if(numaChange){
threads_set_number(0);
settings.numThreads=0;
#ifndef _WIN32
if((settings.numaEnabled=delayedSettings.numaEnabled))
copy_bitmask_to_bitmask(delayedSettings.mask,settings.mask);
#endif
settings.numaEnabled=delayedSettings.numaEnabled;
}
#endif
if(settings.numThreads!=delayedSettings.numThreads){
settings.numThreads=delayedSettings.numThreads;
threads_set_number(settings.numThreads);
}
if(numaChange||ttChange||lpChange){
tt_free();
settings.largePages=delayedSettings.largePages;
settings.ttSize=delayedSettings.ttSize;
tt_allocate(settings.ttSize);
}
if(delayedSettings.clear){
delayedSettings.clear=false;
search_clear();
}
#ifdef NNUE
nnue_init();
#endif
}
//
extern void benchmark(Position*pos,char*str);
static const char StartFEN[]=
"rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";
void position(Position*pos,char*str)
{
char fen[128];
char*moves;
moves=strstr(str,"moves");
if(moves){
if(moves > str)moves[-1]=0;
moves+=5;
}
if(strncmp(str,"fen",3)==0){
strncpy(fen,str+4,127);
fen[127]=0;
}else if(strncmp(str,"startpos",8)==0)
strcpy(fen,StartFEN);
else
return;
pos->st=pos->stack+100;
pos_set(pos,fen,option_value(OPT_CHESS960));
if(moves){
int ply=0;
for(moves=strtok(moves," \t");moves;moves=strtok(NULL," \t")){
Move m=uci_to_move(pos,moves);
if(!m)break;
do_move(pos,m,gives_check(pos,pos->st,m));
pos->gamePly++;
if(++ply==100){
memcpy(pos->st-100,pos->st,StateSize);
pos->st-=100;
pos_set_check_info(pos);
ply-=100;
}
}
if(pos->st->pliesFromNull > 99)
pos->st->pliesFromNull=99;
int k=(pos->st-(pos->stack+100))-max(7,pos->st->pliesFromNull);
for(;k < 0;k++)
memcpy(pos->stack+100+k,pos->stack+200+k,StateSize);
}
pos->rootKeyFlip=pos->st->key;
(pos->st-1)->endMoves=pos->moveList;
for(int k=0;k <=pos->st->pliesFromNull;k++){
int l;
for(l=k+4;l <=pos->st->pliesFromNull;l+=2)
if((pos->st-k)->key==(pos->st-l)->key)
break;
if(l <=pos->st->pliesFromNull)
pos->hasRepeated=true;
else
(pos->st-k)->key=0;
}
pos->rootKeyFlip^=pos->st->key;
pos->st->key^=pos->rootKeyFlip;
}
void setoption(char*str)
{
char*name,*value;
name=strstr(str,"name");
if(!name){
name="";
goto error;
}
name+=4;
while(isblank(*name))
name++;
value=strstr(name,"value");
if(value){
char*p=value-1;
while(isblank(*p))
p--;
p[1]=0;
value+=5;
while(isblank(*value))
value++;
}
if(!value||strlen(value)==0)
value="<empty>";
if(option_set_by_name(name,value))
return;
error:
fprintf(stderr,"No such option: %s\n",name);
}
static void go(Position*pos,char*str)
{
char*token;
bool ponderMode=false;
process_delayed_settings();
Limits=(struct LimitsType){0};
Limits.startTime=now();
for(token=strtok(str," \t");token;token=strtok(NULL," \t")){
if(strcmp(token,"searchmoves")==0)
while((token=strtok(NULL," \t")))
Limits.searchmoves[Limits.numSearchmoves++]=uci_to_move(pos,token);
else if(strcmp(token,"wtime")==0)
Limits.time[WHITE]=atoi(strtok(NULL," \t"));
else if(strcmp(token,"btime")==0)
Limits.time[BLACK]=atoi(strtok(NULL," \t"));
else if(strcmp(token,"winc")==0)
Limits.inc[WHITE]=atoi(strtok(NULL," \t"));
else if(strcmp(token,"binc")==0)
Limits.inc[BLACK]=atoi(strtok(NULL," \t"));
else if(strcmp(token,"movestogo")==0)
Limits.movestogo=atoi(strtok(NULL," \t"));
else if(strcmp(token,"depth")==0)
Limits.depth=atoi(strtok(NULL," \t"));
else if(strcmp(token,"nodes")==0)
Limits.nodes=strtoull(strtok(NULL," \t"),NULL,10);
else if(strcmp(token,"movetime")==0)
Limits.movetime=atoi(strtok(NULL," \t"));
else if(strcmp(token,"mate")==0)
Limits.mate=atoi(strtok(NULL," \t"));
else if(strcmp(token,"infinite")==0)
Limits.infinite=true;
else if(strcmp(token,"ponder")==0)
ponderMode=true;
else if(strcmp(token,"perft")==0){
char str_buf[64];
sprintf(str_buf,"%d %d %d current perft",option_value(OPT_HASH),
option_value(OPT_THREADS),atoi(strtok(NULL," \t")));
benchmark(pos,str_buf);
return;
}
}
start_thinking(pos,ponderMode);
}
/* WinBoard v2 Protocol Variables */
static int wb_force_mode=0;
static int wb_computer_side=1; /* BLACK */
static int wb_time_left=300000;
static int wb_otime_left=300000;
static int wb_moves_per_tc=40;
static int wb_time_base=300000;
static int wb_time_inc=0;
static int wb_post=1;
static int wb_analyze_mode=0;
static int wb_max_depth=128;

static void wb_go(Position*pos)
{
if(wb_force_mode||wb_analyze_mode)return;
if(stm()!=wb_computer_side)return;
process_delayed_settings();
Limits=(struct LimitsType){0};
Limits.startTime=now();
/* Set time control */
if(wb_moves_per_tc==1){
/* Fixed time per move (st command) */
Limits.movetime=wb_time_left;
}else{
/* Calculate time to use */
int moves_to_go=wb_moves_per_tc > 0?wb_moves_per_tc:30;
Limits.time[stm()]=wb_time_left;
Limits.time[!stm()]=wb_otime_left;
Limits.inc[stm()]=wb_time_inc;
Limits.inc[!stm()]=wb_time_inc;
Limits.movestogo=moves_to_go;
}
if(wb_max_depth < 128)Limits.depth=wb_max_depth;
start_thinking(pos,false);
/* Wait for search to complete */
thread_wait_until_sleeping(threads_main());
/* Update our position with the move we just played */
Position*searchPos=Threads.pos[0];
if(searchPos->rootMoves->size > 0){
Move m=searchPos->rootMoves->move[0].pv[0];
if(m){
do_move(pos,m,gives_check(pos,pos->st,m));
pos->gamePly++;
}
}
}
void uci_loop(int argc,char**argv)
{
Position pos;
char fen[strlen(StartFEN)+1];
char str_buf[64];
char*token;
LOCK_INIT(Threads.lock);
Threads.searching=false;
Threads.sleeping=false;
pos.stackAllocation=malloc(63+215*sizeof(Stack));
pos.stack=(Stack*)(((uintptr_t)pos.stackAllocation+0x3f)&~0x3f);
pos.moveList=malloc(1000*sizeof(ExtMove));
pos.st=pos.stack+100;
pos.st[-1].endMoves=pos.moveList;
size_t buf_size=1;
for(int i=1;i < argc;i++)
buf_size+=strlen(argv[i])+1;
if(buf_size < 4096)buf_size=4096;
char*cmd=malloc(buf_size);
cmd[0]=0;
for(int i=1;i < argc;i++){
strcat(cmd,argv[i]);
strcat(cmd," ");
}
strcpy(fen,StartFEN);
pos_set(&pos,fen,0);
pos.rootKeyFlip=pos.st->key;
do{
if(argc==1&&!getline(&cmd,&buf_size,stdin))
strcpy(cmd,"quit");
if(cmd[strlen(cmd)-1]=='\n')
cmd[strlen(cmd)-1]=0;
if(cmd[strlen(cmd)-1]=='\r')
cmd[strlen(cmd)-1]=0;
token=cmd;
while(isblank(*token))
token++;
char*str=token;
while(*str&&!isblank(*str))
str++;
if(*str){
*str++=0;
while(isblank(*str))
str++;
}
/* WinBoard Protocol Commands */
if(strcmp(token,"xboard")==0){
printf("\n");fflush(stdout);
}
else if(strcmp(token,"protover")==0){
printf("feature myname=\"TheKing450\"\n");
printf("feature setboard=1 playother=1 ping=1 usermove=1\n");
printf("feature time=1 draw=0 sigint=0 sigterm=0 reuse=1\n");
printf("feature colors=0 san=0 name=1 nps=0 memory=1\n");
printf("feature variants=\"normal\"\n");
printf("feature option=\"Hash -spin 64 1 4096\"\n");
printf("feature option=\"Threads -spin 1 1 128\"\n");
printf("feature done=1\n");
fflush(stdout);
}
else if(strcmp(token,"accepted")==0||strcmp(token,"rejected")==0){
/* Ignore */
}
else if(strcmp(token,"new")==0){
strcpy(fen,StartFEN);
pos_set(&pos,fen,0);
pos.rootKeyFlip=pos.st->key;
wb_force_mode=0;
wb_computer_side=1; /* BLACK */
wb_max_depth=128;
process_delayed_settings();
search_clear();
}
else if(strcmp(token,"quit")==0){
if(Threads.searching){
Threads.stop=true;
LOCK(Threads.lock);
if(Threads.sleeping)
thread_wake_up(threads_main(),THREAD_RESUME);
Threads.sleeping=false;
UNLOCK(Threads.lock);
}
}
else if(strcmp(token,"force")==0){
wb_force_mode=1;
Threads.stop=true;
}
else if(strcmp(token,"go")==0){
wb_force_mode=0;
wb_computer_side=pos.sideToMove;
wb_go(&pos);
}
else if(strcmp(token,"playother")==0){
wb_force_mode=0;
wb_computer_side=!pos.sideToMove;
}
else if(strcmp(token,"white")==0){
/* Deprecated but handle it */
pos.sideToMove=0;
wb_computer_side=1;
}
else if(strcmp(token,"black")==0){
pos.sideToMove=1;
wb_computer_side=0;
}
else if(strcmp(token,"level")==0){
int mps=0,base=0,inc=0;
char*p=str;
mps=atoi(p);
while(*p&&*p != ' ')p++;
while(*p==' ')p++;
base=atoi(p)*60000;
while(*p&&*p != ':'&&*p != ' ')p++;
if(*p==':'){p++;base+=atoi(p)*1000;}
while(*p&&*p != ' ')p++;
while(*p==' ')p++;
if(*p)inc=(int)(atof(p)*1000);
wb_moves_per_tc=mps;
wb_time_base=base;
wb_time_inc=inc;
wb_time_left=base;
wb_otime_left=base;
}
else if(strcmp(token,"st")==0){
wb_time_left=(int)(atof(str)*1000);
wb_moves_per_tc=1;
}
else if(strcmp(token,"sd")==0){
wb_max_depth=atoi(str);
if(wb_max_depth < 1)wb_max_depth=1;
if(wb_max_depth > 128)wb_max_depth=128;
}
else if(strcmp(token,"time")==0){
wb_time_left=atoi(str)*10;
}
else if(strcmp(token,"otim")==0){
wb_otime_left=atoi(str)*10;
}
else if(strcmp(token,"usermove")==0){
Move m=uci_to_move(&pos,str);
if(!m){
printf("Illegal move: %s\n",str);
fflush(stdout);
}else{
do_move(&pos,m,gives_check(&pos,pos.st,m));
pos.gamePly++;
wb_go(&pos);
}
}
else if(strcmp(token,"?")==0){
Threads.stop=true;
}
else if(strcmp(token,"ping")==0){
printf("pong %s\n",str);
fflush(stdout);
}
else if(strcmp(token,"draw")==0){
/* Ignore draw offers */
}
else if(strcmp(token,"result")==0){
wb_force_mode=1;
}
else if(strcmp(token,"setboard")==0){
pos_set(&pos,str,0);
pos.rootKeyFlip=pos.st->key;
}
else if(strcmp(token,"hint")==0){
ExtMove list[MAX_MOVES];
ExtMove*last=generate_legal(&pos,list);
if(last > list){
char buf[16];
printf("Hint: %s\n",uci_move(buf,list[0].move,pos.chess960));
fflush(stdout);
}
}
else if(strcmp(token,"undo")==0){
if(pos.st > pos.stack+100){
undo_move(&pos,pos.st->currentMove);
pos.gamePly--;
}
}
else if(strcmp(token,"remove")==0){
if(pos.st > pos.stack+101){
undo_move(&pos,pos.st->currentMove);
pos.gamePly--;
undo_move(&pos,pos.st->currentMove);
pos.gamePly--;
}
}
else if(strcmp(token,"hard")==0){
/* Pondering on - not implemented */
}
else if(strcmp(token,"easy")==0){
/* Pondering off */
}
else if(strcmp(token,"post")==0){
wb_post=1;
}
else if(strcmp(token,"nopost")==0){
wb_post=0;
}
else if(strcmp(token,"analyze")==0){
wb_analyze_mode=1;
wb_force_mode=1;
process_delayed_settings();
Limits=(struct LimitsType){0};
Limits.infinite=true;
Limits.startTime=now();
start_thinking(&pos,false);
}
else if(strcmp(token,"exit")==0){
wb_analyze_mode=0;
Threads.stop=true;
}
else if(strcmp(token,".")==0){
printf("stat01: 0 %"PRIu64" 0 0 0\n",threads_nodes_searched());
fflush(stdout);
}
else if(strcmp(token,"memory")==0){
char opt[64];
sprintf(opt,"name Hash value %s",str);
setoption(opt);
}
else if(strcmp(token,"cores")==0){
char opt[64];
sprintf(opt,"name Threads value %s",str);
setoption(opt);
}
else if(strcmp(token,"option")==0){
/* WinBoard option command: option Name=Value */
char*eq=strchr(str,'=');
if(eq){
char opt[128];
*eq=0;
sprintf(opt,"name %s value %s",str,eq+1);
setoption(opt);
}
}
else if(strcmp(token,"random")==0){
/* Ignore random command */
}
else if(strcmp(token,"name")==0){
/* Opponent name - ignore */
}
else if(strcmp(token,"computer")==0){
/* Opponent is computer - ignore */
}
else if(strcmp(token,"rating")==0){
/* Rating info - ignore */
}
else if(strcmp(token,"ics")==0){
/* ICS mode - ignore */
}
else if(strcmp(token,"stop")==0){
if(Threads.searching){
Threads.stop=true;
LOCK(Threads.lock);
if(Threads.sleeping)
thread_wake_up(threads_main(),THREAD_RESUME);
Threads.sleeping=false;
UNLOCK(Threads.lock);
}
}
/* Also support UCI commands for compatibility */
else if(strcmp(token,"uci")==0){
flockfile(stdout);
printf("id name ");
print_engine_info(true);
printf("\n");
print_options();
printf("uciok\n");
fflush(stdout);
funlockfile(stdout);
}
else if(strcmp(token,"ucinewgame")==0){
process_delayed_settings();
search_clear();
}
else if(strcmp(token,"isready")==0){
process_delayed_settings();
printf("readyok\n");
fflush(stdout);
}
else if(strcmp(token,"position")==0)position(&pos,str);
else if(strcmp(token,"setoption")==0)setoption(str);
else if(strcmp(token,"d")==0)print_pos(&pos);
else if(strncmp(token,"#",1)==0){
/* Comment - ignore */
}
else{
/* Try to parse as a move (WinBoard sends moves directly sometimes) */
Move m=uci_to_move(&pos,token);
if(m){
do_move(&pos,m,gives_check(&pos,pos.st,m));
pos.gamePly++;
wb_go(&pos);
}else if(strlen(token)>0){
printf("Error (unknown command): %s\n",token);
fflush(stdout);
}
}
}while(argc==1&&strcmp(token,"quit")!=0);
if(Threads.searching)
thread_wait_until_sleeping(threads_main());
free(cmd);
free(pos.stackAllocation);
free(pos.moveList);
LOCK_DESTROY(Threads.lock);
}
char*uci_value(char*str,Value v)
{
if(abs(v)< VALUE_MATE_IN_MAX_PLY)
sprintf(str,"cp %d",v*100/PawnValueEg);
else
sprintf(str,"mate %d",
(v > 0?VALUE_MATE-v+1:-VALUE_MATE-v)/2);
return str;
}
char*uci_square(char*str,Square s)
{
str[0]='a'+file_of(s);
str[1]='1'+rank_of(s);
str[2]=0;
return str;
}
char*uci_move(char*str,Move m,int chess960)
{
char buf1[8],buf2[8];
Square from=from_sq(m);
Square to=to_sq(m);
if(m==0)
return "(none)";
if(m==MOVE_NULL)
return "0000";
if(type_of_m(m)==CASTLING&&!chess960)
to=make_square(to > from?FILE_G:FILE_C,rank_of(from));
strcat(strcpy(str,uci_square(buf1,from)),uci_square(buf2,to));
if(type_of_m(m)==PROMOTION){
str[strlen(str)+1]=0;
str[strlen(str)]=" pnbrqk"[promotion_type(m)];
}
return str;
}
Move uci_to_move(const Position*pos,char*str)
{
if(strlen(str)==5)
str[4]=tolower(str[4]);
ExtMove list[MAX_MOVES];
ExtMove*last=generate_legal(pos,list);
char buf[16];
for(ExtMove*m=list;m < last;m++)
if(strcmp(str,uci_move(buf,m->move,pos->chess960))==0)
return m->move;
return 0;
}
//
int main(int argc,char**argv)
{
print_engine_info(false);
psqt_init();
bitboards_init();
zob_init();
bitbases_init();
#ifndef NNUE_PURE
endgames_init();
#endif
threads_init();
options_init();
search_clear();
uci_loop(argc,argv);
threads_exit();
TB_free();
options_free();
tt_free();
pb_free();
  #ifdef NNUE
nnue_free();
  #endif
return 0;
}