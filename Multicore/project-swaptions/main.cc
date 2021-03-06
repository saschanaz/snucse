#include <iostream>
#include <fstream>
#include <chrono>
#include <memory>
#include <string>
#include <array>
#include <cstring>
#include <cmath>
#include <cassert>
#include <pthread.h>
#include <CL/cl.h>
#include "compute.h"
#ifdef USE_MPI
#include "mpi.h"
#endif

#define TASKS 128

using namespace std;
using namespace chrono;


namespace {
  void init(task_t *task, int task_id);
  void check(cl_int);
}


int main(int argc, char *argv[]) {
  auto time = system_clock::now();

#ifdef USE_MPI
  MPI_Init(&argc, &argv);

  int size, rank;
  MPI_Comm_size(MPI_COMM_WORLD, &size);
  MPI_Comm_rank(MPI_COMM_WORLD, &rank);
  assert(TASKS % size == 0);

  const size_t task_count = TASKS/size;
  const size_t task_offset = task_count * rank;
#else
  const size_t task_count = TASKS;
  const size_t task_offset = 0;
#endif

  auto tasks = unique_ptr<task_t[]>(new task_t[task_count]);
  auto results = unique_ptr<result_t[]>(new result_t[task_count]);
  const size_t sizeof_tasks = sizeof(task_t)*task_count;
  const size_t sizeof_results = sizeof(result_t)*task_count;

  //
  // OpenCL
  //
  cl_platform_id platform;
  check(clGetPlatformIDs(1, &platform, NULL));

  cl_device_type type = CL_DEVICE_TYPE_GPU;
  // Get device count
  cl_uint device_count;
  check(clGetDeviceIDs(platform, type, 0, NULL, &device_count));
  assert(task_count % device_count == 0);

  // Get all devices
  auto devices = unique_ptr<cl_device_id[]>(new cl_device_id[device_count]);
  check(clGetDeviceIDs(platform, type, device_count, devices.get(), NULL));

  // create a single context for all devices
  cl_int e;
  auto ctxt = clCreateContext(NULL, device_count, devices.get(), NULL, NULL, &e); check(e);

  // for each device create a separate queue
  auto cmdqs = unique_ptr<cl_command_queue[]>(new cl_command_queue[device_count]);
  for (size_t i = 0; i < device_count; ++i) {
    cmdqs[i] = clCreateCommandQueue(ctxt, devices[i], 0, &e); check(e);
  }

  // GPU memory alloc
  for (size_t task_id = 0; task_id < task_count; ++task_id) {
    init(&tasks[task_id], task_offset + task_id);
  }

  auto buffer_tasks = unique_ptr<cl_mem[]>(new cl_mem[device_count]);
  auto buffer_results = unique_ptr<cl_mem[]>(new cl_mem[device_count]);
  for (cl_uint i = 0; i < device_count; ++i) {
    size_t chunk = (sizeof_tasks)/device_count;
    void* mem = (void*)((uintptr_t)tasks.get() + chunk*i);
    buffer_tasks[i] = clCreateBuffer(ctxt, CL_MEM_READ_ONLY | CL_MEM_USE_HOST_PTR, chunk, mem, &e); check(e);
    buffer_results[i] = clCreateBuffer(ctxt, CL_MEM_WRITE_ONLY, (sizeof_results)/device_count, NULL, &e); check(e);
  }

  // Initialize kernal
  ifstream ifs("compute.cl");
  string content((istreambuf_iterator<char>(ifs)), (istreambuf_iterator<char>()));
  auto code = content.c_str();
  const auto codelen = content.length();
  auto program = clCreateProgramWithSource(ctxt, 1, &code, &codelen, &e); check(e);
  e = clBuildProgram(program, device_count, devices.get(), NULL, NULL, NULL);
  if (e == CL_BUILD_PROGRAM_FAILURE) {
    // Print detailed message
    cerr << endl;
    cerr << "OpenCL compile error" << endl;

    size_t len, size = 2048;
    auto buffer = unique_ptr<char[]>(new char[2048]);
    clGetProgramBuildInfo(program, devices[0], CL_PROGRAM_BUILD_LOG, size, buffer.get(), &len);
    cerr << buffer.get() << endl;
    clGetProgramBuildInfo(program, devices[0], CL_PROGRAM_BUILD_STATUS, size, buffer.get(), &len);
    cerr << buffer.get() << endl;
    clGetProgramBuildInfo(program, devices[0], CL_PROGRAM_BUILD_OPTIONS, size, buffer.get(), &len);
    cerr << buffer.get() << endl;
    exit(1);
  }
  check(e);


  //
  // Create kernel
  //
  auto kernel = clCreateKernel(program, "swaption", &e); check(e);


  //
  // Calculate
  //
  for (cl_uint i = 0; i < device_count; ++i) {
    check(clSetKernelArg(kernel, 0, sizeof(cl_mem), &buffer_tasks[i]));
    check(clSetKernelArg(kernel, 1, sizeof(cl_mem), &buffer_results[i]));

    array<size_t, 2> global = {{ task_count/device_count, TRIALS }};
    array<size_t, 2> local = {{ 1, 128 }};
    check(clEnqueueNDRangeKernel(cmdqs[i], kernel, 2, NULL, global.data(), local.data(), 0, NULL, NULL));
  }


  //
  // Read result
  //
  for (cl_uint i = 0; i < device_count; ++i) {
    size_t chunk = (sizeof_results)/device_count;
    void* mem = (void*)((uintptr_t)results.get() + chunk*i);
    check(clEnqueueReadBuffer(cmdqs[i], buffer_results[i], CL_FALSE, 0, chunk, mem, 0, NULL, NULL));
  }

  for (cl_uint i = 0; i < device_count; ++i) {
    check(clFinish(cmdqs[i]));
  }

#ifdef USE_MPI
  // Gather result
  double sums[TASKS];
  double square_sums[TASKS];
  for (size_t task_id = 0; task_id < task_count; ++task_id) {
    double sum = 0;
    double square_sum = 0;
    for (int i = 0; i < TRIALS; ++i) {
      sum += results[task_id].sums[i];
      square_sum += results[task_id].square_sums[i];
    }

    // Store results
    sums[task_offset + task_id] = sum;
    square_sums[task_offset + task_id] = square_sum;
  }

  MPI_Gather(&sums[task_offset], task_count, MPI_DOUBLE, &sums[0], task_count, MPI_DOUBLE, 0, MPI_COMM_WORLD);
  MPI_Gather(&square_sums[task_offset], task_count, MPI_DOUBLE, &square_sums[0], task_count, MPI_DOUBLE, 0, MPI_COMM_WORLD);

  if (rank == 0) {
    for (size_t i = 0; i < TASKS; ++i) {
      double mean = sums[i]/TRIALS;
      double error = sqrt((square_sums[i] - sums[i]*sums[i]/TRIALS)/(TRIALS - 1.0)/TRIALS);
      printf("Swaption%lu: [SwaptionPrice: %.10lf StdError: %.10lf]\n", i, mean, error);
    }

    auto elapsed = duration<double>(system_clock::now() - time).count();
    cerr << "Total elapsed time : " << elapsed << " seconds" << endl;
  }

  MPI_Finalize();
#else
  for (size_t task_id = 0; task_id < task_count; ++task_id) {
    double sum = 0;
    double square_sum = 0;
    for (int i = 0; i < TRIALS; ++i) {
      sum += results[task_id].sums[i];
      square_sum += results[task_id].square_sums[i];
    }

    // Store results
    double mean = sum/TRIALS;
    double error = sqrt((square_sum - sum*sum/TRIALS)/(TRIALS - 1.0)/TRIALS);
    printf("Swaption%lu: [SwaptionPrice: %.10lf StdError: %.10lf]\n", task_id, mean, error);
  }

  auto elapsed = duration<double>(system_clock::now() - time).count();
  cerr << "Total elapsed time : " << elapsed << " seconds" << endl;
#endif
  return 0;
}


namespace {
  //
  // Initialize buffers
  //
  void init(task_t *task, int task_id) {
    double *pdForward = task->forward;
    double *pdTotalDrift = task->drifts;
    double *seeds = task->seeds;
    double *pdSwapPayoffs = task->payoffs;


    // Mathmatical constants
    const double strike = (double)task_id/TASKS;
    const double dPaymentInterval = 1.0;
    const int iFreqRatio = dPaymentInterval/DELTA + 0.5;
    const double dTenor = 2.0;
    const int iSwapTimePoints = dTenor/DELTA + 0.5;


    // Store swap payoffs
    memset(pdSwapPayoffs, 0, sizeof pdSwapPayoffs);
    for (int i = iFreqRatio; i <= iSwapTimePoints; i += iFreqRatio) {
      double tmp = exp(strike*dPaymentInterval);
      // Normally, the bond pays coupon equal to this amount
      // But at terminal time point, bond pays coupon plus par amount
      pdSwapPayoffs[i] = i == iSwapTimePoints ? tmp : tmp - 1;
    }

    // Initialize yield curve
    double pdYield[N];
    double tmp = 0.1;
    for (int i = 0; i < N; ++i) {
      pdYield[i] = tmp;
      tmp += 0.005;
    }

    // Generating forward curve at t=0 from supplied yield curve
    pdForward[0] = pdYield[0];
    for (int i = 1; i < N; ++i) {
      pdForward[i] = (i + 1)*pdYield[i] - i*pdYield[i - 1];
    }

    // Computation of drifts from factor volatilities
    double ppdDrifts[FACTORS][N - 1];
    for (int i = 0; i < FACTORS; ++i) {
      ppdDrifts[i][0] = 0.5*DELTA*factors[i][0]*factors[i][0];
    }
    // Computation of factor drifts for other maturities
    for (int i = 0; i < FACTORS; ++i) {
      for (int j = 1; j < N - 1; ++j) {
        double sum0 = 0;
        for(int l = 0; l < j; ++l) { sum0 -= ppdDrifts[i][l]; }
        ppdDrifts[i][j] = sum0;

        double sum = 0;
        for(int l = 0; l <= j; ++l) { sum += factors[i][l]; }
        ppdDrifts[i][j] += 0.5*DELTA*sum*sum;
      }
    }
    // Computation of total drifts for all maturities
    for (int i = 0; i < N - 1; ++i) {
      double sum = 0;
      for(int j = 0; j < FACTORS; ++j) { sum += ppdDrifts[j][i]; }
      pdTotalDrift[i] = sum;
    }

    // Init seeds
    double seed = 100;
    for (int i = 0; i < TRIALS; ++i) {
      seeds[i] = seed;
      seed += (N - 1)*FACTORS;
    }
  }

  const char *clGetErrorMessage(cl_int error_code) {
    switch (error_code) {
    // run-time and JIT compiler errors
    case 0: return "CL_SUCCESS";
    case -1: return "CL_DEVICE_NOT_FOUND";
    case -2: return "CL_DEVICE_NOT_AVAILABLE";
    case -3: return "CL_COMPILER_NOT_AVAILABLE";
    case -4: return "CL_MEM_OBJECT_ALLOCATION_FAILURE";
    case -5: return "CL_OUT_OF_RESOURCES";
    case -6: return "CL_OUT_OF_HOST_MEMORY";
    case -7: return "CL_PROFILING_INFO_NOT_AVAILABLE";
    case -8: return "CL_MEM_COPY_OVERLAP";
    case -9: return "CL_IMAGE_FORMAT_MISMATCH";
    case -10: return "CL_IMAGE_FORMAT_NOT_SUPPORTED";
    case -11: return "CL_BUILD_PROGRAM_FAILURE";
    case -12: return "CL_MAP_FAILURE";
    case -13: return "CL_MISALIGNED_SUB_BUFFER_OFFSET";
    case -14: return "CL_EXEC_STATUS_ERROR_FOR_EVENTS_IN_WAIT_LIST";
    case -15: return "CL_COMPILE_PROGRAM_FAILURE";
    case -16: return "CL_LINKER_NOT_AVAILABLE";
    case -17: return "CL_LINK_PROGRAM_FAILURE";
    case -18: return "CL_DEVICE_PARTITION_FAILED";
    case -19: return "CL_KERNEL_ARG_INFO_NOT_AVAILABLE";

    // compile-time errors
    case -30: return "CL_INVALID_VALUE";
    case -31: return "CL_INVALID_DEVICE_TYPE";
    case -32: return "CL_INVALID_PLATFORM";
    case -33: return "CL_INVALID_DEVICE";
    case -34: return "CL_INVALID_CONTEXT";
    case -35: return "CL_INVALID_QUEUE_PROPERTIES";
    case -36: return "CL_INVALID_COMMAND_QUEUE";
    case -37: return "CL_INVALID_HOST_PTR";
    case -38: return "CL_INVALID_MEM_OBJECT";
    case -39: return "CL_INVALID_IMAGE_FORMAT_DESCRIPTOR";
    case -40: return "CL_INVALID_IMAGE_SIZE";
    case -41: return "CL_INVALID_SAMPLER";
    case -42: return "CL_INVALID_BINARY";
    case -43: return "CL_INVALID_BUILD_OPTIONS";
    case -44: return "CL_INVALID_PROGRAM";
    case -45: return "CL_INVALID_PROGRAM_EXECUTABLE";
    case -46: return "CL_INVALID_KERNEL_NAME";
    case -47: return "CL_INVALID_KERNEL_DEFINITION";
    case -48: return "CL_INVALID_KERNEL";
    case -49: return "CL_INVALID_ARG_INDEX";
    case -50: return "CL_INVALID_ARG_VALUE";
    case -51: return "CL_INVALID_ARG_SIZE";
    case -52: return "CL_INVALID_KERNEL_ARGS";
    case -53: return "CL_INVALID_WORK_DIMENSION";
    case -54: return "CL_INVALID_WORK_GROUP_SIZE";
    case -55: return "CL_INVALID_WORK_ITEM_SIZE";
    case -56: return "CL_INVALID_GLOBAL_OFFSET";
    case -57: return "CL_INVALID_EVENT_WAIT_LIST";
    case -58: return "CL_INVALID_EVENT";
    case -59: return "CL_INVALID_OPERATION";
    case -60: return "CL_INVALID_GL_OBJECT";
    case -61: return "CL_INVALID_BUFFER_SIZE";
    case -62: return "CL_INVALID_MIP_LEVEL";
    case -63: return "CL_INVALID_GLOBAL_WORK_SIZE";
    case -64: return "CL_INVALID_PROPERTY";
    case -65: return "CL_INVALID_IMAGE_DESCRIPTOR";
    case -66: return "CL_INVALID_COMPILER_OPTIONS";
    case -67: return "CL_INVALID_LINKER_OPTIONS";
    case -68: return "CL_INVALID_DEVICE_PARTITION_COUNT";

    // extension errors
    case -1000: return "CL_INVALID_GL_SHAREGROUP_REFERENCE_KHR";
    case -1001: return "CL_PLATFORM_NOT_FOUND_KHR";
    case -1002: return "CL_INVALID_D3D10_DEVICE_KHR";
    case -1003: return "CL_INVALID_D3D10_RESOURCE_KHR";
    case -1004: return "CL_D3D10_RESOURCE_ALREADY_ACQUIRED_KHR";
    case -1005: return "CL_D3D10_RESOURCE_NOT_ACQUIRED_KHR";
    default: return "Unknown OpenCL error";
    }
  }

  void check(cl_int err) {
    if (err == CL_SUCCESS) { return; }
    cerr << clGetErrorMessage(err) << endl;
    exit(1);
  }
}
