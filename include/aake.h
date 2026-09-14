/*
 * Copyright 2026 Aarav Ravindra Kharade
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef AAKE_H
#define AAKE_H

#include <stdbool.h>

void generate_blueprints(bool is_x86_64, bool visible_blueprint);
int run_ninja_ui(int jobs, bool verbose, const char *arch_str);
void package_images(bool is_arm64);
void package_sign(void);
void generate_fonts(void);
void generate_cursors(void);

#endif // AAKE_H
