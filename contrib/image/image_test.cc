// Copyright (c) the JPEG XL Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "contrib/image/image.h"

#include <cstddef>

#include "hwy/base.h"

#undef HWY_TARGET_INCLUDE
#define HWY_TARGET_INCLUDE "contrib/image/image_test.cc"
#include "hwy/foreach_target.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include <random>
#include <utility>

#include "hwy/highway.h"
#include "hwy/tests/test_util-inl.h"

HWY_BEFORE_NAMESPACE();
namespace hwy {
namespace HWY_NAMESPACE {

// Ensure we can always write full aligned vectors.
struct TestAlignedT {
  template <typename T>
  void operator()(T /*unused*/) const {
    std::mt19937 rng(129);
    std::uniform_int_distribution<T> dist(0, 16);
    const HWY_FULL(T) d;

    for (size_t ysize = 1; ysize < 4; ++ysize) {
      for (size_t xsize = 1; xsize < 64; ++xsize) {
        Image<T> img(xsize, ysize);

        for (size_t y = 0; y < ysize; ++y) {
          T* HWY_RESTRICT row = img.MutableRow(y);
          for (size_t x = 0; x < xsize; x += Lanes(d)) {
            const auto values = Iota(d, dist(rng));
            Store(values, d, row + x);
          }
        }

        // Sanity check to prevent optimizing out the writes
        const auto x = std::uniform_int_distribution<size_t>(0, xsize - 1)(rng);
        const auto y = std::uniform_int_distribution<size_t>(0, ysize - 1)(rng);
        ASSERT_LT(img.ConstRow(y)[x], 16 + Lanes(d));
      }
    }
  }
};

void TestAligned() { ForUnsignedTypes(TestAlignedT()); }

// Ensure we can write an unaligned vector starting at the last valid value.
struct TestUnalignedT {
  template <typename T>
  void operator()(T /*unused*/) const {
    std::mt19937 rng(129);
    std::uniform_int_distribution<T> dist(0, 3);
    const HWY_FULL(T) d;

    for (size_t ysize = 1; ysize < 4; ++ysize) {
      for (size_t xsize = 1; xsize < 128; ++xsize) {
        Image<T> img(xsize, ysize);
        img.InitializePaddingForUnalignedAccesses();

// This test reads padding, which only works if it was initialized,
// which only happens in MSAN builds.
#if defined(MEMORY_SANITIZER) || HWY_IDE
        // Initialize only the valid samples
        for (size_t y = 0; y < ysize; ++y) {
          T* HWY_RESTRICT row = img.MutableRow(y);
          for (size_t x = 0; x < xsize; ++x) {
            row[x] = 1 << dist(rng);
          }
        }

        // Read padding bits
        auto accum = Zero(d);
        for (size_t y = 0; y < ysize; ++y) {
          T* HWY_RESTRICT row = img.MutableRow(y);
          for (size_t x = 0; x < xsize; ++x) {
            accum |= LoadU(d, row + x);
          }
        }

        // Ensure padding was zero
        HWY_ALIGN T lanes[MaxLanes(d)];
        Store(accum, d, lanes);
        for (size_t i = 0; i < Lanes(d); ++i) {
          ASSERT_LT(lanes[i], 16)
              << xsize << "x" << ysize << " vec size:" << HWY_LANES(uint8_t);
        }
#else  // Check that writing padding does not overwrite valid samples
       // Initialize only the valid samples
        for (size_t y = 0; y < ysize; ++y) {
          T* HWY_RESTRICT row = img.MutableRow(y);
          for (size_t x = 0; x < xsize; ++x) {
            row[x] = static_cast<T>(x);
          }
        }

        // Zero padding and rightmost sample
        for (size_t y = 0; y < ysize; ++y) {
          T* HWY_RESTRICT row = img.MutableRow(y);
          StoreU(Zero(d), d, row + xsize - 1);
        }

        // Ensure no samples except the rightmost were overwritten
        for (size_t y = 0; y < ysize; ++y) {
          T* HWY_RESTRICT row = img.MutableRow(y);
          for (size_t x = 0; x < xsize - 1; ++x) {
            ASSERT_EQ(static_cast<T>(x), row[x]);
          }
        }
#endif
      }
    }
  }
};

void TestUnaligned() { ForUnsignedTypes(TestUnalignedT()); }

// NOLINTNEXTLINE(google-readability-namespace-comments)
}  // namespace HWY_NAMESPACE
}  // namespace hwy
HWY_AFTER_NAMESPACE();

#if HWY_ONCE
namespace hwy {

class ImageTest : public hwy::TestWithParamTarget {};
HWY_TARGET_INSTANTIATE_TEST_SUITE_P(ImageTest);

HWY_EXPORT_AND_TEST_P(ImageTest, TestAligned);
HWY_EXPORT_AND_TEST_P(ImageTest, TestUnaligned);

}  // namespace hwy
#endif
