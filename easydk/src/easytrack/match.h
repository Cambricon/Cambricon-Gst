#ifndef EASYTRACK_MATCH_H_
#define EASYTRACK_MATCH_H_

#include <cmath>
#include <map>
#include <string>
#include <vector>
#include <utility>

#include "easytrack/easy_track.h"
#include "hungarian.h"
#include "matrix.h"
#include "track_data_type.h"

namespace edk {

typedef float (*DistanceFunc)(const std::vector<Feature> &track_feature_set, const Feature &detect_feature);

namespace detail {
struct HungarianWorkspace {
  void* ptr{nullptr};
  size_t len{0};
  void Refresh(size_t new_len) {
    if (new_len == 0) return;
    if (len < new_len) {
      if (ptr) free(ptr);
      ptr = malloc(new_len);
      if (!ptr) {
        len = 0;
        throw std::bad_alloc();
      }
      len = new_len;
    }
  }
  ~HungarianWorkspace() {
    if (ptr) {
      free(ptr);
      len = 0;
    }
  }
};
}  // namespace detail

static inline float InnerProduct(const std::vector<float>& lhs, const std::vector<float>& rhs) {
  size_t cnt = lhs.size();
  if (cnt != rhs.size()) THROW_EXCEPTION(Exception::INVALID_ARG, "inner product need two vector of equal size");
  float sum{0.f};
  for (size_t idx = 0; idx < cnt; ++idx) {
    sum += lhs[idx] * rhs[idx];
  }
  return sum;
}

static inline float L2Norm(const std::vector<float>& feature) {
  return std::sqrt(InnerProduct(feature, feature));
}

class MatchAlgorithm {
 public:
  static MatchAlgorithm *Instance(const std::string &dist_func = "Cosine");

  Matrix IoUCost(const std::vector<Rect> &det_rects, const std::vector<Rect> &tra_rects);

  void HungarianMatch(const Matrix &cost_matrix, std::vector<int> *assignment) {
    workspace_.Refresh(hungarian_.GetWorkspaceSize(cost_matrix.Rows(), cost_matrix.Cols()));
    hungarian_.Solve(cost_matrix, assignment, workspace_.ptr);
  }

  template <class... Args>
  float Distance(Args &&... args) {
    return dist_func_(std::forward<Args>(args)...);
  }

 private:
  explicit MatchAlgorithm(DistanceFunc func) : dist_func_(func) {}
  float IoU(const Rect &a, const Rect &b);
  static thread_local detail::HungarianWorkspace workspace_;
  HungarianAlgorithm hungarian_;
  DistanceFunc dist_func_;
};  // class MatchAlgorithm

}  // namespace edk

#endif  // EASYTRACK_MATCH_H_
