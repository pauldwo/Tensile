#include <hip_runtime.h>
#include <cstdio>

// unroll can be 4, 8, 16 or 32
// unroll = 8 hangs
#define NUM_UNROLL_ITER     16

/*******************************************************************************
 * Kernel
 ******************************************************************************/

/* CT_SSSSS_Cij_Sk_Aik_Bjk_i16x4f_j16x4f_k16_O2 */

/* tile parameters */
#define WG_DIM_0I           16
#define WG_DIM_1J           16
#define MICRO_TILE_0I       4
#define MICRO_TILE_1J       4
#define MACRO_TILE_0I       64
#define MACRO_TILE_1J       64


/* global memory indices */
#define GET_GLOBAL_INDEX_C(IDX0I, IDX1J) ( (IDX0I)*strideC0I + (IDX1J)*strideC1J )
#define GET_GLOBAL_INDEX_A(IDX0I, IDXK) ( (IDX0I)*strideA0I + (IDXK)*strideAK )
#define GET_GLOBAL_INDEX_B(IDX1J, IDXK) ( (IDX1J)*strideB1J + (IDXK)*strideBK )

/* global tile indices being loaded */
/* fast read */
#define globalIdxA0I(LID) (groupIdx0I*MACRO_TILE_0I + (localSerial+(LID)*WG_DIM_0I*WG_DIM_1J)/NUM_UNROLL_ITER)
#define globalIdxAK(LID) (localSerial%NUM_UNROLL_ITER)
/* fast read */
#define globalIdxBK(LID) ((localSerial+(LID)*WG_DIM_0I*WG_DIM_1J)/MACRO_TILE_1J)
#define globalIdxB1J(LID) (groupIdx1J*MACRO_TILE_1J + (localSerial+(LID)*WG_DIM_0I*WG_DIM_1J)%MACRO_TILE_1J)

/* global non-tile indices being loaded */


/* local memory indices */
#define GET_LOCAL_INDEX_A(DIM0,DIM1) ((DIM0) + (DIM1)*(MACRO_TILE_0I) )
#define GET_LOCAL_INDEX_B(DIM0,DIM1) ((DIM1) + (DIM0)*(MACRO_TILE_1J) )

/* local indices being written */
#define localA0I (localSerial / NUM_UNROLL_ITER)
#define localAK (localSerial % NUM_UNROLL_ITER)
#define localAStride (WG_DIM_0I*WG_DIM_1J/NUM_UNROLL_ITER)
#define localB1J ( localSerial / MACRO_TILE_1J )
#define localBK ( localSerial % MACRO_TILE_1J )
#define localBStride  (WG_DIM_0I*WG_DIM_1J)

/* data types */
#define DATA_TYPE_STR_A float
#define DATA_TYPE_STR_B float
#define DATA_TYPE_STR_C float
#define DATA_TYPE_STR_ALPHA float
#define DATA_TYPE_STR_BETA float
#define FMA(A,B,DST) fmaf(A,B,DST)
#define TYPE_MAD(MULA,MULB,DST) DST = FMA(MULA,MULB,DST);
#define TYPE_MAD_WRITE(DST,ALPHA,REG,BETA) DST = (ALPHA)*(REG) + (BETA)*(DST);

/* 4x4 micro-tile */
#define MICRO_TILE \
  rA[0] = localA[offA + 0*WG_DIM_0I]; \
  rA[1] = localA[offA + 1*WG_DIM_0I]; \
  rA[2] = localA[offA + 2*WG_DIM_0I]; \
  rA[3] = localA[offA + 3*WG_DIM_0I]; \
  rB[0] = localB[offB + 0*WG_DIM_1J]; \
  rB[1] = localB[offB + 1*WG_DIM_1J]; \
  rB[2] = localB[offB + 2*WG_DIM_1J]; \
  rB[3] = localB[offB + 3*WG_DIM_1J]; \
  offA += MACRO_TILE_0I; \
  offB += MACRO_TILE_1J; \
  TYPE_MAD(rA[0],rB[0],rC[0][0]); \
  TYPE_MAD(rA[0],rB[1],rC[0][1]); \
  TYPE_MAD(rA[0],rB[2],rC[0][2]); \
  TYPE_MAD(rA[0],rB[3],rC[0][3]); \
  TYPE_MAD(rA[1],rB[0],rC[1][0]); \
  TYPE_MAD(rA[1],rB[1],rC[1][1]); \
  TYPE_MAD(rA[1],rB[2],rC[1][2]); \
  TYPE_MAD(rA[1],rB[3],rC[1][3]); \
  TYPE_MAD(rA[2],rB[0],rC[2][0]); \
  TYPE_MAD(rA[2],rB[1],rC[2][1]); \
  TYPE_MAD(rA[2],rB[2],rC[2][2]); \
  TYPE_MAD(rA[2],rB[3],rC[2][3]); \
  TYPE_MAD(rA[3],rB[0],rC[3][0]); \
  TYPE_MAD(rA[3],rB[1],rC[3][1]); \
  TYPE_MAD(rA[3],rB[2],rC[3][2]); \
  TYPE_MAD(rA[3],rB[3],rC[3][3]); \
  __syncthreads();

/* preprocessor definitions of kernel arguments*/
#define strideC0I 1
#define strideA0I 1
#define strideB1J 1


extern "C"
__global__ void CT_SSSSS_Cij_Sk_Aik_Bjk_i16x4f_j16x4f_k_O2(
  hipLaunchParm lp,
  float       *          C,
  float const * __restrict__ A,
  float const * __restrict__ B,
  float const alpha,
  float const beta,
  unsigned int const offsetC,
  unsigned int const offsetA,
  unsigned int const offsetB,
  unsigned int const strideC1J,
  unsigned int const strideAK,
  unsigned int const strideBK,
  unsigned int const size0I,
  unsigned int const size1J,
  unsigned int const sizeK ) {

  /* apply offsets */
  C += offsetC;
  A += offsetA;
  B += offsetB;

  /* allocate registers */
  DATA_TYPE_STR_C rC[MICRO_TILE_0I][MICRO_TILE_1J] = {{0}};
  DATA_TYPE_STR_A rA[MICRO_TILE_0I];
  DATA_TYPE_STR_B rB[MICRO_TILE_1J];

  /* allocate local memory */
  __shared__ DATA_TYPE_STR_A localA[NUM_UNROLL_ITER*MACRO_TILE_0I];
  __shared__ DATA_TYPE_STR_B localB[NUM_UNROLL_ITER*MACRO_TILE_1J];

  /* c indices */
  unsigned int groupIdx0I = hc_get_group_id(0); // d0, tensorA
  unsigned int groupIdx1J = hc_get_group_id(1); // d1, tensorB
  unsigned int localIdx0I = hc_get_workitem_id(0); // d0
  unsigned int localIdx1J = hc_get_workitem_id(1); // d1
  unsigned int localSerial = localIdx0I + localIdx1J*WG_DIM_0I;

  /* which global Cij index */
  unsigned int globalIdxC1J = groupIdx1J*MACRO_TILE_1J + localIdx1J;
  unsigned int globalIdxC0I = groupIdx0I*MACRO_TILE_0I + localIdx0I;
  /* iterate over all summation indices */
  unsigned int sumIterK = sizeK / NUM_UNROLL_ITER;
  do {
     DATA_TYPE_STR_A *lA = localA + GET_LOCAL_INDEX_A(localA0I, localAK);
     DATA_TYPE_STR_B *lB = localB + GET_LOCAL_INDEX_B(localB1J, localBK);
    __syncthreads();

    /* load global -> local */
    lA[ 0*localAStride ] = A[ GET_GLOBAL_INDEX_A( globalIdxA0I(0), globalIdxAK(0) ) ];
#if NUM_UNROLL_ITER > 4
    lA[ 1*localAStride ] = A[ GET_GLOBAL_INDEX_A( globalIdxA0I(1), globalIdxAK(1) ) ];
#endif
#if NUM_UNROLL_ITER > 8
    lA[ 2*localAStride ] = A[ GET_GLOBAL_INDEX_A( globalIdxA0I(2), globalIdxAK(2) ) ];
    lA[ 3*localAStride ] = A[ GET_GLOBAL_INDEX_A( globalIdxA0I(3), globalIdxAK(3) ) ];
#endif
#if NUM_UNROLL_ITER > 16
    lA[ 4*localAStride ] = A[ GET_GLOBAL_INDEX_A( globalIdxA0I(4), globalIdxAK(4) ) ];
    lA[ 5*localAStride ] = A[ GET_GLOBAL_INDEX_A( globalIdxA0I(5), globalIdxAK(5) ) ];
    lA[ 6*localAStride ] = A[ GET_GLOBAL_INDEX_A( globalIdxA0I(6), globalIdxAK(6) ) ];
    lA[ 7*localAStride ] = A[ GET_GLOBAL_INDEX_A( globalIdxA0I(7), globalIdxAK(7) ) ];
#endif

    lB[ 0*localBStride ] = B[ GET_GLOBAL_INDEX_B( globalIdxB1J(0), globalIdxBK(0) ) ];
#if NUM_UNROLL_ITER > 4
    lB[ 1*localBStride ] = B[ GET_GLOBAL_INDEX_B( globalIdxB1J(1), globalIdxBK(1) ) ];
#endif
#if NUM_UNROLL_ITER > 8
    lB[ 2*localBStride ] = B[ GET_GLOBAL_INDEX_B( globalIdxB1J(2), globalIdxBK(2) ) ];
    lB[ 3*localBStride ] = B[ GET_GLOBAL_INDEX_B( globalIdxB1J(3), globalIdxBK(3) ) ];
#endif
#if NUM_UNROLL_ITER > 16
    lB[ 4*localBStride ] = B[ GET_GLOBAL_INDEX_B( globalIdxB1J(4), globalIdxBK(4) ) ];
    lB[ 5*localBStride ] = B[ GET_GLOBAL_INDEX_B( globalIdxB1J(5), globalIdxBK(5) ) ];
    lB[ 6*localBStride ] = B[ GET_GLOBAL_INDEX_B( globalIdxB1J(6), globalIdxBK(6) ) ];
    lB[ 7*localBStride ] = B[ GET_GLOBAL_INDEX_B( globalIdxB1J(7), globalIdxBK(7) ) ];
#endif

    __syncthreads();
    unsigned int offA = localIdx0I; // d0
    unsigned int offB = localIdx1J; // d1

    /* do mads */
    MICRO_TILE
    MICRO_TILE
    MICRO_TILE
    MICRO_TILE
#if NUM_UNROLL_ITER > 4
    MICRO_TILE
    MICRO_TILE
    MICRO_TILE
    MICRO_TILE
#endif
#if NUM_UNROLL_ITER > 8
    MICRO_TILE
    MICRO_TILE
    MICRO_TILE
    MICRO_TILE
    MICRO_TILE
    MICRO_TILE
    MICRO_TILE
    MICRO_TILE
#endif
#if NUM_UNROLL_ITER > 16
    MICRO_TILE
    MICRO_TILE
    MICRO_TILE
    MICRO_TILE
    MICRO_TILE
    MICRO_TILE
    MICRO_TILE
    MICRO_TILE
    MICRO_TILE
    MICRO_TILE
    MICRO_TILE
    MICRO_TILE
    MICRO_TILE
    MICRO_TILE
    MICRO_TILE
    MICRO_TILE
#endif


    A += strideAK*NUM_UNROLL_ITER;
    B += strideBK*NUM_UNROLL_ITER;
  } while (--sumIterK > 0);


  /* write global C */
  TYPE_MAD_WRITE( C[ GET_GLOBAL_INDEX_C( globalIdxC0I + 0*WG_DIM_0I, globalIdxC1J + 0*WG_DIM_1J) ], alpha, rC[0][0], beta)
  TYPE_MAD_WRITE( C[ GET_GLOBAL_INDEX_C( globalIdxC0I + 0*WG_DIM_0I, globalIdxC1J + 1*WG_DIM_1J) ], alpha, rC[0][1], beta)
  TYPE_MAD_WRITE( C[ GET_GLOBAL_INDEX_C( globalIdxC0I + 0*WG_DIM_0I, globalIdxC1J + 2*WG_DIM_1J) ], alpha, rC[0][2], beta)
  TYPE_MAD_WRITE( C[ GET_GLOBAL_INDEX_C( globalIdxC0I + 0*WG_DIM_0I, globalIdxC1J + 3*WG_DIM_1J) ], alpha, rC[0][3], beta)
  TYPE_MAD_WRITE( C[ GET_GLOBAL_INDEX_C( globalIdxC0I + 1*WG_DIM_0I, globalIdxC1J + 0*WG_DIM_1J) ], alpha, rC[1][0], beta)
  TYPE_MAD_WRITE( C[ GET_GLOBAL_INDEX_C( globalIdxC0I + 1*WG_DIM_0I, globalIdxC1J + 1*WG_DIM_1J) ], alpha, rC[1][1], beta)
  TYPE_MAD_WRITE( C[ GET_GLOBAL_INDEX_C( globalIdxC0I + 1*WG_DIM_0I, globalIdxC1J + 2*WG_DIM_1J) ], alpha, rC[1][2], beta)
  TYPE_MAD_WRITE( C[ GET_GLOBAL_INDEX_C( globalIdxC0I + 1*WG_DIM_0I, globalIdxC1J + 3*WG_DIM_1J) ], alpha, rC[1][3], beta)
  TYPE_MAD_WRITE( C[ GET_GLOBAL_INDEX_C( globalIdxC0I + 2*WG_DIM_0I, globalIdxC1J + 0*WG_DIM_1J) ], alpha, rC[2][0], beta)
  TYPE_MAD_WRITE( C[ GET_GLOBAL_INDEX_C( globalIdxC0I + 2*WG_DIM_0I, globalIdxC1J + 1*WG_DIM_1J) ], alpha, rC[2][1], beta)
  TYPE_MAD_WRITE( C[ GET_GLOBAL_INDEX_C( globalIdxC0I + 2*WG_DIM_0I, globalIdxC1J + 2*WG_DIM_1J) ], alpha, rC[2][2], beta)
  TYPE_MAD_WRITE( C[ GET_GLOBAL_INDEX_C( globalIdxC0I + 2*WG_DIM_0I, globalIdxC1J + 3*WG_DIM_1J) ], alpha, rC[2][3], beta)
  TYPE_MAD_WRITE( C[ GET_GLOBAL_INDEX_C( globalIdxC0I + 3*WG_DIM_0I, globalIdxC1J + 0*WG_DIM_1J) ], alpha, rC[3][0], beta)
  TYPE_MAD_WRITE( C[ GET_GLOBAL_INDEX_C( globalIdxC0I + 3*WG_DIM_0I, globalIdxC1J + 1*WG_DIM_1J) ], alpha, rC[3][1], beta)
  TYPE_MAD_WRITE( C[ GET_GLOBAL_INDEX_C( globalIdxC0I + 3*WG_DIM_0I, globalIdxC1J + 2*WG_DIM_1J) ], alpha, rC[3][2], beta)
  TYPE_MAD_WRITE( C[ GET_GLOBAL_INDEX_C( globalIdxC0I + 3*WG_DIM_0I, globalIdxC1J + 3*WG_DIM_1J) ], alpha, rC[3][3], beta)

}



#define CHECK(STATUS) \
  if (STATUS != hipSuccess) { \
    fprintf(stderr, "error: '%s' (%d) at %s:%d\n", hipGetErrorString(STATUS), STATUS, __FILE__, __LINE__); \
  }

/*******************************************************************************
 * main
 ******************************************************************************/
int main( int argc, char *argv[] ) {

  // GEMM parameters
  // col-major means dim0=col, dim1=row
  // size0C = size of C column = C num rows
  const unsigned int M = 64;
  const unsigned int N = 64;
  const unsigned int K = 64;
  const unsigned int size0C = M;
  const unsigned int size1C = N;
  const unsigned int size0A = M;
  const unsigned int size1A = K;
  const unsigned int size0B = K;
  const unsigned int size1B = N;

  // matrix sizes
  const size_t numElementsC = size0C*size1C;
  const size_t numElementsA = size0A*size1A;
  const size_t numElementsB = size0B*size1B;
  const size_t sizeC = numElementsC * sizeof(DATA_TYPE_STR_C);
  const size_t sizeA = numElementsA * sizeof(DATA_TYPE_STR_A);
  const size_t sizeB = numElementsB * sizeof(DATA_TYPE_STR_B);

  // allocate host buffers
  printf("allocating host buffers\n");
  DATA_TYPE_STR_C *hC = new DATA_TYPE_STR_C[numElementsC];
  DATA_TYPE_STR_A *hA = new DATA_TYPE_STR_A[numElementsA];
  DATA_TYPE_STR_B *hB = new DATA_TYPE_STR_B[numElementsB];

  // init host buffers
  printf("initializing host buffers\n");
  for (unsigned int i = 0; i < numElementsC; i++) { hC[i] = 1; }
  for (unsigned int i = 0; i < numElementsA; i++) { hA[i] = 1; }
  for (unsigned int i = 0; i < numElementsB; i++) { hB[i] = 1; }

  // allocate device buffers
  printf("allocating device buffers\n");
  hipError_t status;
  DATA_TYPE_STR_C *dC;
  DATA_TYPE_STR_A *dA;
  DATA_TYPE_STR_B *dB;
  status = hipMalloc( &dC, sizeC); CHECK(status);
  status = hipMalloc( &dA, sizeA); CHECK(status);
  status = hipMalloc( &dB, sizeB); CHECK(status);
  
  // init device buffers
  printf("initializing device buffers\n");
  status = hipMemcpy( dC, hC, sizeC, hipMemcpyHostToDevice ); CHECK(status);
  status = hipMemcpy( dA, hA, sizeA, hipMemcpyHostToDevice ); CHECK(status);
  status = hipMemcpy( dB, hB, sizeB, hipMemcpyHostToDevice ); CHECK(status);

  // alpha & beta
  DATA_TYPE_STR_ALPHA alpha = 2;
  DATA_TYPE_STR_BETA  beta  = 2;

  // enqueue dim
  dim3 workGroup( WG_DIM_0I, WG_DIM_1J, 1 );
  dim3 blocks(size0C/MACRO_TILE_0I, size1C/MACRO_TILE_1J, 1);  

  // enqueue kernel
  printf("enqueueing kernel block=%ux%u, work-group=%ux%u\n",blocks.x, blocks.y, workGroup.x, workGroup.y);
  hipLaunchKernel(
      HIP_KERNEL_NAME(CT_SSSSS_Cij_Sk_Aik_Bjk_i16x4f_j16x4f_k_O2),
      blocks,
      workGroup,
      0, // groupMemBytes
      nullptr, // stream
      dC,
      dA,
      dB,
      alpha,
      beta,
      0, // offset C
      0, // offset A
      0, // offset B
      size0C, // stride1C,
      size0A, // stride1A,
      size0B, // stride1B,
      M,
      N,
      K );

  // wait for kernel
  printf("synchronizing stream\n");
  status = hipStreamSynchronize( nullptr );
  CHECK(status);

  // copy result back to host
  printf("copying device results back to host\n");
  status = hipMemcpy( hC, dC, sizeC, hipMemcpyDeviceToHost ); CHECK(status);
  DATA_TYPE_STR_C answer = K*alpha + beta;
  printf("validating %f\n", answer);
  size_t numInvalid = 0;
  for (unsigned int i = 0; i < numElementsC; i++) {
    if (hC[i] != answer) {
      numInvalid++;
      if (numInvalid < 4*4) {
        printf("C[%u] = %f rather than %f\n", i, hC[i], answer);
      }
    }
  }
  if (numInvalid) {
    printf("FAILED validation\n");
  } else {
    printf("PASSED validation\n");
  }

}
