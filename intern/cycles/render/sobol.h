/*
 * Copyright 2011-2013 Blender Foundation
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef __SOBOL_H__
#define __SOBOL_H__

#include "util/util_types.h"

CCL_NAMESPACE_BEGIN

#define SOBOL_BITS 32
#define SOBOL_MAX_DIMENSIONS 21201

void sobol_generate_direction_vectors(uint vectors[][SOBOL_BITS], int dimensions);

int sobol_dither_matrix_size();
void sobol_generate_dither_matrix(float2 *matrix);


CCL_NAMESPACE_END

#endif /* __SOBOL_H__ */
