/*
 * Copyright (C) 2025 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <memory>

#include <gtest/gtest.h>

#include "DrmDisplayPipeline.h"

namespace android::drm_hwcomposer {

class NoOpBindable : public PipelineBindable<NoOpBindable> {};

class DrmDisplayPipelineTest : public ::testing::Test {};

TEST_F(DrmDisplayPipelineTest, BindPipeline_Success) {
  const auto pipeline = std::make_unique<DrmDisplayPipeline>();
  NoOpBindable bindable;

  const auto binding = bindable.BindPipeline(pipeline.get(),
                                             /*return_object_if_bound=*/true);

  ASSERT_NE(binding, nullptr);
  EXPECT_EQ(binding->Get()->GetPipeline(), pipeline.get());
  EXPECT_EQ(binding->Get(), &bindable);
}

TEST_F(DrmDisplayPipelineTest, BindPipeline_SamePipelineReturnsSameBinding) {
  const auto pipeline = std::make_unique<DrmDisplayPipeline>();
  NoOpBindable bindable;

  const auto binding1 = bindable.BindPipeline(pipeline.get(),
                                              /*return_object_if_bound=*/true);
  ASSERT_NE(binding1, nullptr);
  EXPECT_EQ(binding1->Get()->GetPipeline(), pipeline.get());
  EXPECT_EQ(binding1->Get(), &bindable);
  const auto binding2 = bindable.BindPipeline(pipeline.get(),
                                              /*return_object_if_bound=*/true);
  EXPECT_EQ(binding1, binding2);
}

TEST_F(DrmDisplayPipelineTest,
       BindPipeline_DifferentPipelineReturnsNullBinding) {
  const auto pipeline1 = std::make_unique<DrmDisplayPipeline>();
  const auto pipeline2 = std::make_unique<DrmDisplayPipeline>();
  NoOpBindable bindable;

  const auto binding1 = bindable.BindPipeline(pipeline1.get(),
                                              /*return_object_if_bound=*/true);
  ASSERT_NE(binding1, nullptr);
  EXPECT_EQ(binding1->Get()->GetPipeline(), pipeline1.get());
  EXPECT_EQ(binding1->Get(), &bindable);
  const auto binding2 = bindable.BindPipeline(pipeline2.get(),
                                              /*return_object_if_bound=*/true);
  EXPECT_EQ(binding2, nullptr);
}

TEST_F(DrmDisplayPipelineTest,
       BindPipeline_ReleasedBindingDoesntPreventNewBinding) {
  const auto pipeline1 = std::make_unique<DrmDisplayPipeline>();
  const auto pipeline2 = std::make_unique<DrmDisplayPipeline>();
  NoOpBindable bindable;

  auto binding1 = bindable.BindPipeline(pipeline1.get(),
                                        /*return_object_if_bound=*/true);
  ASSERT_NE(binding1, nullptr);
  EXPECT_EQ(binding1->Get()->GetPipeline(), pipeline1.get());
  EXPECT_EQ(binding1->Get(), &bindable);
  binding1.reset();
  const auto binding2 = bindable.BindPipeline(pipeline2.get(),
                                              /*return_object_if_bound=*/true);
  ASSERT_NE(binding2, nullptr);
  EXPECT_EQ(binding2->Get()->GetPipeline(), pipeline2.get());
  EXPECT_EQ(binding2->Get(), &bindable);
}

}  // namespace android::drm_hwcomposer
