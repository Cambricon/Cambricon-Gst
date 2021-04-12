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

/**
 * @file edk_attribute.h
 *
 * Attribute macros
 */

#ifndef EDK_CXXUTIL_ATTRIBUTE_H_
#define EDK_CXXUTIL_ATTRIBUTE_H_

#ifdef __GNUC__
#define attribute_deprecated __attribute__((deprecated))
#elif defined(_MSC_VER)
#define attribute_deprecated __declspec(deprecated)
#else
#pragma message("attribute_deprecated is not defined for this compiler")
#define attribute_deprecated
#endif

#ifdef __GNUC__
#define EDK_LIKELY(x) (__builtin_expect(!!(x), 1))
#define EDK_UNLIKELY(x) (__builtin_expect(!!(x), 0))
#else
#define EDK_UNLIKELY(x) x
#define EDK_LIKELY(x) x
#endif

#endif  // EDK_CXXUTIL_ATTRIBUTE_H_
