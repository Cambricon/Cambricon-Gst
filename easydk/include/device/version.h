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

#ifndef EDK_VERSION_H_
#define EDK_VERSION_H_

#include <string>

#define EDK_VERSION_MAJOR 2
#define EDK_VERSION_MINOR 5
#define EDK_VERSION_PATCH 0

#define EDK_GET_VERSION(major, minor, patch) (((major) << 20) | ((minor) << 10) | (patch))
#define EDK_VERSION EDK_GET_VERSION(EDK_VERSION_MAJOR, EDK_VERSION_MINOR, EDK_VERSION_PATCH)

namespace edk {

/**
 * @brief Get edk version string
 *
 * @return std::string version string
 */
std::string Version() {
  // clang-format off
  return std::to_string(EDK_VERSION_MAJOR) + "." +
         std::to_string(EDK_VERSION_MINOR) + "." +
         std::to_string(EDK_VERSION_PATCH);
  // clang-format on
}

}  // namespace edk

#endif  // EDK_VERSION_H_
