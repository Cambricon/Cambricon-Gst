/*************************************************************************
 * Copyright (C) [2019] by Cambricon, Inc. All rights reserved
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *************************************************************************/

#ifndef EASYINFER_SHAPE_HPP_
#define EASYINFER_SHAPE_HPP_

#include <iostream>
#include <vector>

namespace edk {

/**
 * @brief ShapeEx to describe inference model input and output data
 * @warning No matter how data is placed in memory, dim values always keep in order of NHWC
 */
class ShapeEx {
 public:
  /// stored value type
  using value_type = int;

  /**
   * @brief Construct a new ShapeEx object
   */
  ShapeEx() = default;

  /**
   * @brief Construct a new ShapeEx object from shape vector
   *
   * @param v vector stored shape value
   */
  explicit ShapeEx(const std::vector<value_type>& v) noexcept { data_ = v; }

  ShapeEx(const ShapeEx&) = default;
  ShapeEx& operator=(const ShapeEx&) = default;
  ShapeEx(ShapeEx&&) = default;
  ShapeEx& operator=(ShapeEx&&) = default;

  /**
   * @brief Get value of nth dimension
   *
   * @param offset serial number of dimension
   * @return value_type shape value
   */
  value_type operator[](int offset) const { return data_[offset]; }

  /**
   * @brief Get value of nth dimension
   *
   * @param offset serial number of dimension
   * @return value_type reference to shape value
   */
  value_type& operator[](int offset) { return data_[offset]; }

  /**
   * @brief Returns the dimension size of ShapeEx
   *
   * @return size_t The dimension size of ShapeEx
   */
  size_t Size() const noexcept { return data_.size(); };

  /**
   * @brief Returns whether ShapeEx is empty
   *
   * @retval true if the ShapeEx doesn't have any value
   * @retval false otherwise
   */
  bool Empty() const noexcept { return data_.empty(); };

  /**
   * @brief Get vectorized shape value
   *
   * @return std::vector<value_type> vectorized shape value
   */
  std::vector<value_type> Vectorize() const noexcept { return data_; }

  /**
   * @brief Get batchsize
   *
   * @return value_type batch size
   */
  value_type BatchSize() const { return data_[0]; }

  /**
   * @brief Get n value
   *
   * @note Only work on ShapeEx of 4 dimension, will return 0 if size() != 4.
   * @return value_type n or 0
   */
  value_type N() const noexcept {
    if (Size() == 4) {
      return data_[0];
    }
    return 0;
  }

  /**
   * @brief Get height value
   *
   * @note Only work on ShapeEx of 4 dimension, will return 0 if size() != 4.
   * @return value_type height or 0
   */
  value_type H() const noexcept {
    if (Size() == 4) {
      return data_[1];
    }
    return 0;
  }

  /**
   * @brief Get width value
   *
   * @note Only work on ShapeEx of 4 dimension, will return 0 if size() != 4.
   * @return value_type width or 0
   */
  value_type W() const noexcept {
    if (Size() == 4) {
      return data_[2];
    }
    return 0;
  }

  /**
   * @brief Get channel value
   *
   * @note Only work on ShapeEx of 4 dimension, will return 0 if size() != 4.
   * @return value_type channel or 0
   */
  value_type C() const noexcept {
    if (Size() == 4) {
      return data_[3];
    }
    return 0;
  }

  /**
   * @brief Get total data count / batch size
   *
   * @return Data count
   */
  int64_t DataCount() const noexcept {
    int64_t cnt = 1;
    for (size_t i = 1; i < data_.size(); ++i) {
      cnt *= data_[i];
    }
    return cnt;
  }

  /**
   * @brief Get total data count
   *
   * @return Total data count
   */
  int64_t BatchDataCount() const noexcept {
    int64_t cnt = 1;
    for (size_t i = 0; i < data_.size(); ++i) {
      cnt *= data_[i];
    }
    return cnt;
  }

  /**
   * @brief put shape into ostream
   *
   * @param os Output stream
   * @param shape ShapeEx to be printed
   * @return Output stream
   */
  friend std::ostream& operator<<(std::ostream& os, const ShapeEx& shape) {
    os << "ShapeEx (";
    for (size_t i = 0; i < shape.Size() - 1; ++i) {
      os << shape[i] << ", ";
    }
    if (shape.Size() > 0) os << shape[shape.Size() - 1];
    os << ")";
    return os;
  }

  /**
   * @brief Judge whether two shapes are equal
   *
   * @param lhs a ShapeEx
   * @param rhs a ShapeEx
   * @retval true if two shapes are equal
   * @retval false otherwise
   */
  friend bool operator==(const ShapeEx& lhs, const ShapeEx& rhs) noexcept {
    if (lhs.Size() != rhs.Size()) return false;
    for (size_t i = 0; i < lhs.Size(); ++i) {
      if (lhs[i] != rhs[i]) return false;
    }
    return true;
  }

  /**
   * @brief Judge whether two shapes are not equal
   *
   * @param lhs a ShapeEx
   * @param rhs a ShapeEx
   * @retval true if two shapes are not equal
   * @retval false otherwise
   */
  friend bool operator!=(const ShapeEx& lhs, const ShapeEx& rhs) noexcept { return !(lhs == rhs); }

 private:
  std::vector<value_type> data_;
};  // class ShapeEx

/**
 * @brief Shape to describe inference model input and output data
 */
class Shape {
 public:
  /**
   * @brief Construct a new Shape object
   *
   * @param n data number
   * @param h height
   * @param w width
   * @param c channel
   * @param stride aligned width
   */
  explicit Shape(uint32_t n = 1, uint32_t h = 1, uint32_t w = 1, uint32_t c = 1, uint32_t stride = 1);

  /**
   * @brief Get stride, which is aligned width
   *
   * @return Stride
   */
  inline uint32_t Stride() const { return w > stride_ ? w : stride_; }

  /**
   * @brief Set the stride
   *
   * @param s Stride
   */
  inline void SetStride(uint32_t s) { stride_ = s; }

  /**
   * @brief Get Step, row length, equals to stride multiply c
   *
   * @return Step
   */
  inline uint64_t Step() const { return Stride() * c; }

  /**
   * @brief Get total data count, equal to memory size
   *
   * @return Data count
   */
  inline uint64_t DataCount() const { return n * h * Step(); }

  /**
   * @brief Get n * h * w * c, which is unaligned data size
   *
   * @return nhwc
   */
  inline uint64_t nhwc() const { return n * h * w * c; }

  /**
   * @brief Get h * w * c, which is size of one data part
   *
   * @return hwc
   */
  inline uint64_t hwc() const { return h * w * c; }

  /**
   * @brief Get h * w, which is size of one channel in one data part
   *
   * @return hw
   */
  inline uint64_t hw() const { return h * w; }

  /**
   * @brief Print shape
   *
   * @param os Output stream
   * @param shape Shape to be printed
   * @return Output stream
   */
  friend std::ostream &operator<<(std::ostream &os, const Shape &shape);

  /**
   * @brief Judge whether two shapes are equal
   *
   * @param other Another shape
   * @return Return true if two shapes are equal
   */
  bool operator==(const Shape &other) const;

  /**
   * @brief Judge whether two shapes are not equal
   *
   * @param other Another shape
   * @return Return true if two shapes are not equal
   */
  bool operator!=(const Shape &other) const;

  /// data number
  uint32_t n;
  /// height
  uint32_t h;
  /// width
  uint32_t w;
  /// channel
  uint32_t c;

 private:
  uint32_t stride_;
};  // class Shape

}  // namespace edk

#endif  // EASYINFER_SHAPE_HPP_
