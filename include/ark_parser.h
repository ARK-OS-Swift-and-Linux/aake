//
// Copyright 2026 Aarav Ravindra Kharade
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//

#ifndef ARK_PARSER_H
#define ARK_PARSER_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_LINE_LEN 8192
#define MAX_KEYS 64
#define MAX_SECTIONS 32
#define MAX_STANDALONE 32
#define MAX_LIST_ITEMS 256

typedef struct {
  char key[256];
  char value[4096];
} ArkKeyValuePair;

typedef struct {
  char name[256];
  ArkKeyValuePair pairs[MAX_KEYS];
  int pair_count;
} ArkSection;

typedef struct {
  char filepath[512];
  ArkKeyValuePair global_pairs[MAX_KEYS];
  int global_count;

  ArkSection sections[MAX_SECTIONS];
  int section_count;

  char standalone[MAX_STANDALONE][1024];
  int standalone_count;
} ArkConfig;

/* Parsed list result — caller must free items */
typedef struct {
  char items[MAX_LIST_ITEMS][256];
  int count;
} ArkList;

ArkConfig *ark_parse(const char *filepath);
const char *ark_get_global(ArkConfig *config, const char *key);
const char *ark_get_section_val(ArkConfig *config, const char *section,
                                const char *key);
void ark_set_global(ArkConfig *config, const char *key, const char *value);
void ark_dump(ArkConfig *config, const char *filepath);
void ark_free(ArkConfig *config);

/* New: List value support (comma-separated values) */
ArkList ark_get_list(ArkConfig *config, const char *key);
ArkList ark_get_section_list(ArkConfig *config, const char *section,
                             const char *key);

/* New: Boolean value support (true/false, yes/no, 1/0) */
int ark_get_bool(ArkConfig *config, const char *key, int default_val);
int ark_get_section_bool(ArkConfig *config, const char *section,
                         const char *key, int default_val);

#endif // ARK_PARSER_H
