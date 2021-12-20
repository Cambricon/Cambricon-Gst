/*************************************************************************
 * Hungarian.h: Header file for Class HungarianAlgorithm.
 *
 * This is a C++ wrapper with slight modification of a hungarian algorithm
 * implementation by Markus Buehren. The original implementation is a few
 * mex-functions for use in MATLAB, found here:
 * http://www.mathworks.com/matlabcentral/fileexchange/6543-functions-for-the-rectangular-assignment-problem
 *
 * Both this code and the orignal code are published under the BSD license.
 * by Cong Ma, 2016
 ************************************************************************/
#ifndef EASYTRACK_HUNGARIAN_H_
#define EASYTRACK_HUNGARIAN_H_

#include <iostream>
#include <vector>

#include "matrix.h"

class HungarianAlgorithm {
 public:
  float Solve(const edk::Matrix &DistMatrix, std::vector<int> *Assignment,
              void *workspace = nullptr);
  size_t GetWorkspaceSize(size_t rows, size_t cols) {
    return cols * rows * 11 + rows * 5 + cols;
  }

 private:
  void assignmentoptimal(int *assignment, float *cost, float *distMatrix,
                         int nOfRows, int nOfColumns, void* workspace);
  void buildassignmentvector(int *assignment, bool *starMatrix, int nOfRows, int nOfColumns);
  void computeassignmentcost(int *assignment, float *cost, float *distMatrix, int nOfRows);
  void step2a(int *assignment, float *distMatrix, bool *starMatrix, bool *newStarMatrix, bool *primeMatrix,
              bool *coveredColumns, bool *coveredRows, int nOfRows, int nOfColumns, int minDim);
  void step2b(int *assignment, float *distMatrix, bool *starMatrix, bool *newStarMatrix, bool *primeMatrix,
              bool *coveredColumns, bool *coveredRows, int nOfRows, int nOfColumns, int minDim);
  void step3(int *assignment, float *distMatrix, bool *starMatrix, bool *newStarMatrix, bool *primeMatrix,
             bool *coveredColumns, bool *coveredRows, int nOfRows, int nOfColumns, int minDim);
  void step4(int *assignment, float *distMatrix, bool *starMatrix, bool *newStarMatrix, bool *primeMatrix,
             bool *coveredColumns, bool *coveredRows, int nOfRows, int nOfColumns, int minDim, int row, int col);
  void step5(int *assignment, float *distMatrix, bool *starMatrix, bool *newStarMatrix, bool *primeMatrix,
             bool *coveredColumns, bool *coveredRows, int nOfRows, int nOfColumns, int minDim);
};

#endif  // EASYTRACK_HUNGARIAN_H_
