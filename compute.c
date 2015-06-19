//
// swaption.cpp
//
// Authors      : Mark Broadie, Jatin Dewanwala
// Collaborator : Mikhail Smelyanskiy, Intel, Jike Chong (Berkeley)
//
// Routines to compute various security prices using HJM framework (via Simulation).
//
#include <math.h>
#include <string.h>
#include "compute.h"



// Length of time steps
#define SQRT_DELTA 0.7071067811865476



//
// Returns the inverse of cumulative normal distribution function.
//
// Reference: Moro, B., 1995, "The Full Monte," RISK (February), 57-58.
//
static double cum_normal_inv(double u) {
  const double x = u - 0.5;

  if (fabs(x) < 0.42) {
    // 얘만 수행하는 병렬화를 해야됨
    double r = x * x;
    return x * (((-25.44106049637*r + 41.39119773534)*r - 18.61500062529)*r + 2.50662823884)
      / ((((3.13082909833*r - 21.06224101826)*r + 23.08336743743)*r - 8.47351093090)*r + 1.0);
  } else {
    // 조건 밖에 해당되어서 특별처리하는부분
    // 이거 근사해서 빠르게 할 수 있을지도
    double r = x > 0.0 ? 1.0 - u : u;
    r = log(-log(r)); // 느림!!
    r = 0.3374754822726147 + r * (0.9761690190917186 + r * (0.1607979714918209 + r *
       (0.0276438810333863 + r * (0.0038405729373609 + r * (0.0003951896511919 + r *
       (0.0000321767881768 + r * (0.0000002888167364 + r *  0.0000003960315187 )))))));
    return x >= 0.0 ? r : -r;
  }
}


//
// Uniform random number generator
//
// 이 함수는 reentrant 합니다.
//
static double uniform_random(long ix) {
  ix *= 1513517L;
  ix %= 2147483647L;
  long k1 = ix/127773L;
  ix = 16807L*(ix - k1*127773L) - k1*2836L;
  if (ix < 0) { ix += 2147483647L; }
  return ix * 4.656612875e-10;
}


//
// This function computes and stores an HJM Path for given inputs
//
// 대부분
//
static void hjm_path(
    // Matrix that stores generated HJM path (Output)
    double ppdHJMPath[restrict N][N*BLOCKSIZE],
    // t=0 Forward curve
    const double pdForward[restrict],
    // Vector containing total drift corrections for different maturities
    const double pdTotalDrift[restrict],
    // Random number seed
    long seed)
{
  // t=0 forward curve stored N first row of ppdHJMPath
  // At time step 0: insert expected drift
  // rest reset to 0
  // KCM: if you are not going to do parallelization,change the order of the
  // loop. {for (j) for(b)} {for(i) memset(ppdHJMPATH[i])}
  for (int j = 0; j < N; ++j) {
    double val = pdForward[j];
    for (int b = 0; b < BLOCKSIZE; ++b) {
      ppdHJMPath[0][BLOCKSIZE*j + b] = val;
    }
  }

  // Initializing HJMPath to zero
  for (int i = 1; i < N; ++i) {
    memset(ppdHJMPath[i], 0, sizeof ppdHJMPath[i]);
  }


  //
  // Sequentially generating random numbers
  //
  double pdZ[FACTORS][N*BLOCKSIZE];
  for(int b = 0; b < BLOCKSIZE; ++b) {
    for (int j = 1; j < N; ++j) {
      for (int l = 0; l < FACTORS; ++l) {
        //compute random number in exact same sequence
        pdZ[l][BLOCKSIZE*j + b] = uniform_random(seed++);  /* 10% of the total executition time */
      }
    }
  }


  //
  // shocks to hit various factors for forward curve at t
  // 18% of the total executition time
  //
  for (int l = 0; l < FACTORS; ++l) {
    for (int b = 0; b < BLOCKSIZE; ++b) {
      for (int j = 1; j < N; ++j) {
        // 18% of the total executition time
        pdZ[l][BLOCKSIZE*j + b] = cum_normal_inv(pdZ[l][BLOCKSIZE*j + b]);
      }
    }
  }


  //
  // Generation of HJM Path1
  //

  // b is the blocks
  for(int b = 0; b < BLOCKSIZE; ++b){
    // j is the timestep
    for (int j = 1; j < N; ++j) {
      // l is the future steps
      for (int l = 0; l < N - j; ++l){
        // Total shock by which the forward curve is hit at (t, T-t)
        double dTotalShock = 0;

        // i steps through the stochastic factors
        for (int i = 0; i < FACTORS; ++i){
          dTotalShock += factors[i][l] * pdZ[i][BLOCKSIZE*j + b];
        }

        // As per formula
        ppdHJMPath[j][BLOCKSIZE*l+b] = ppdHJMPath[j-1][BLOCKSIZE*(l+1)+b] + pdTotalDrift[l]*DELTA + SQRT_DELTA*dTotalShock;
      }
    }
  }
}


//
// swaption.c
//
static void discount_factors(
    double pdDiscountFactors[restrict],
    int num,
    double years,
    const double pdRatePath[restrict])
{
  // HJM time-step length
  const double delta = years/num;

  // Precompute the exponientials
  double pdexpRes[(num - 1)*BLOCKSIZE];
  for (int j = 0; j < (num - 1)*BLOCKSIZE; ++j) { pdexpRes[j] = -pdRatePath[j]*delta; }
  for (int j = 0; j < (num - 1)*BLOCKSIZE; ++j) { pdexpRes[j] = exp(pdexpRes[j]);  }


  // Initializing the discount factor vector
  for (int i = 0; i < num*BLOCKSIZE; ++i) {
    pdDiscountFactors[i] = 1.0;
  }

  for (int i = 1; i < num; ++i) {
    for (int b = 0; b < BLOCKSIZE; ++b) {
      for (int j = 0; j < i; ++j) {
        // kcm: do both loop unrolling and reorder pdexpRes vector elements above. -> SSE
        // optimization is possible.
        pdDiscountFactors[i*BLOCKSIZE + b] *= pdexpRes[j*BLOCKSIZE + b];
      }
    }
  }
}


void swaption(task_t *task, size_t id) {
  double *pdForward = task->forward;
  double *pdTotalDrift = task->drifts;
  double *seeds = task->seeds;
  double *pdSwapPayoffs = task->payoffs;
  double *sums = task->sums;
  double *square_sums = task->square_sums;


  //
  // Constants
  //
  const int iSwapStartTimeIndex = MATURITY/DELTA + 0.5;
  const double dSwapVectorYears = SWAP_VECTOR_LENGTH*DELTA;


  // For each trial a new HJM Path is generated
  double ppdHJMPath[N][N*BLOCKSIZE];
  hjm_path(ppdHJMPath, pdForward, pdTotalDrift, seeds[id]); // 51% of the time goes here

  // Now we compute the discount factor vector
  // 여긴 안해도 될거같음
  double pdDiscountingRatePath[N*BLOCKSIZE];
  for (int i = 0; i < N; ++i) {
    for (int b = 0; b < BLOCKSIZE; ++b) {
      pdDiscountingRatePath[BLOCKSIZE*i + b] = ppdHJMPath[i][0 + b];
    }
  }

  // Store discount factors for the rate path along which the swaption
  double pdPayoffDiscountFactors[N*BLOCKSIZE];
  discount_factors(pdPayoffDiscountFactors, N, YEARS, pdDiscountingRatePath); // 15% of the time goes here

  // Now we compute discount factors along the swap path
  double pdSwapRatePath[SWAP_VECTOR_LENGTH*BLOCKSIZE];
  for (size_t i = 0; i < SWAP_VECTOR_LENGTH; ++i) {
    for (int b = 0; b < BLOCKSIZE; ++b) {
      pdSwapRatePath[i*BLOCKSIZE + b] = ppdHJMPath[iSwapStartTimeIndex][i*BLOCKSIZE + b];
    }
  }

  // Payments made will be discounted corresponding to swaption maturity
  double pdSwapDiscountFactors[SWAP_VECTOR_LENGTH*BLOCKSIZE];
  discount_factors(pdSwapDiscountFactors, SWAP_VECTOR_LENGTH, dSwapVectorYears, pdSwapRatePath);


  double sum = 0;
  double square_sum = 0;
  for (int b = 0; b < BLOCKSIZE; ++b) {
    double tmp = -1;
    for (size_t i = 0; i < SWAP_VECTOR_LENGTH; ++i) {
      tmp += pdSwapPayoffs[i]*pdSwapDiscountFactors[i*BLOCKSIZE + b];
    }

    if (tmp <= 0) { continue; }
    tmp *= pdPayoffDiscountFactors[iSwapStartTimeIndex*BLOCKSIZE + b];

    // Accumulate
    sum += tmp;
    square_sum += tmp*tmp;
  }
  sums[id] = sum;
  square_sums[id] = square_sum;
}
