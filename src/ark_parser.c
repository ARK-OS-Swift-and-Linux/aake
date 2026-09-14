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

#include "ark_parser.h"

static void strip_newline(char *str) {
  int len = strlen(str);
  while (len > 0 && (str[len - 1] == '\n' || str[len - 1] == '\r')) {
    str[len - 1] = '\0';
    len--;
  }
}

static char *trim_whitespace(char *str) {
  while (*str == ' ' || *str == '\t')
    str++;
  int len = strlen(str);
  while (len > 0 && (str[len - 1] == ' ' || str[len - 1] == '\t')) {
    str[len - 1] = '\0';
    len--;
  }
  return str;
}

ArkConfig *ark_parse(const char *filepath) {
  ArkConfig *config = (ArkConfig *)malloc(sizeof(ArkConfig));
  memset(config, 0, sizeof(ArkConfig));
  strncpy(config->filepath, filepath, sizeof(config->filepath) - 1);

  FILE *f = fopen(filepath, "r");
  if (!f)
    return config;

  char line[MAX_LINE_LEN];
  ArkSection *curr_sec = NULL;

  while (fgets(line, sizeof(line), f)) {
    strip_newline(line);
    char *trimmed = trim_whitespace(line);
    if (strlen(trimmed) == 0 || trimmed[0] == '#')
      continue;

    // Check for section
    if (strncmp(trimmed, "---", 3) == 0) {
      char *end = strstr(trimmed + 3, "---");
      if (end && config->section_count < MAX_SECTIONS) {
        *end = '\0';
        curr_sec = &config->sections[config->section_count++];
        strncpy(curr_sec->name, trim_whitespace(trimmed + 3), 255);
      }
      continue;
    }

    // Check for Key=Value or Key:Value
    char *sep = strchr(trimmed, '=');
    if (!sep)
      sep = strchr(trimmed, ':');

    if (sep) {
      *sep = '\0';
      char *key = trim_whitespace(trimmed);
      char *val = trim_whitespace(sep + 1);

      if (curr_sec) {
        if (curr_sec->pair_count < MAX_KEYS) {
          strncpy(curr_sec->pairs[curr_sec->pair_count].key, key, 255);
          strncpy(curr_sec->pairs[curr_sec->pair_count].value, val, 4095);
          curr_sec->pairs[curr_sec->pair_count].value[4095] = '\0';
          curr_sec->pair_count++;
        }
      } else {
        if (config->global_count < MAX_KEYS) {
          strncpy(config->global_pairs[config->global_count].key, key, 255);
          strncpy(config->global_pairs[config->global_count].value, val, 4095);
          config->global_pairs[config->global_count].value[4095] = '\0';
          config->global_count++;
        }
      }
    } else {
      if (config->standalone_count < MAX_STANDALONE) {
        strncpy(config->standalone[config->standalone_count], trimmed, 1023);
        config->standalone[config->standalone_count][1023] = '\0';
        config->standalone_count++;
      }
    }
  }
  fclose(f);
  return config;
}

const char *ark_get_global(ArkConfig *config, const char *key) {
  for (int i = 0; i < config->global_count; i++) {
    if (strcmp(config->global_pairs[i].key, key) == 0) {
      return config->global_pairs[i].value;
    }
  }
  return NULL;
}

const char *ark_get_section_val(ArkConfig *config, const char *section,
                                const char *key) {
  for (int i = 0; i < config->section_count; i++) {
    if (strcmp(config->sections[i].name, section) == 0) {
      for (int j = 0; j < config->sections[i].pair_count; j++) {
        if (strcmp(config->sections[i].pairs[j].key, key) == 0) {
          return config->sections[i].pairs[j].value;
        }
      }
    }
  }
  return NULL;
}

void ark_set_global(ArkConfig *config, const char *key, const char *value) {
  for (int i = 0; i < config->global_count; i++) {
    if (strcmp(config->global_pairs[i].key, key) == 0) {
      strncpy(config->global_pairs[i].value, value, 4095);
      config->global_pairs[i].value[4095] = '\0';
      return;
    }
  }
  if (config->global_count < MAX_KEYS) {
    strncpy(config->global_pairs[config->global_count].key, key, 255);
    strncpy(config->global_pairs[config->global_count].value, value, 4095);
    config->global_pairs[config->global_count].value[4095] = '\0';
    config->global_count++;
  }
}

void ark_dump(ArkConfig *config, const char *filepath) {
  FILE *f = fopen(filepath, "w");
  if (!f)
    return;

  for (int i = 0; i < config->global_count; i++) {
    fprintf(f, "%s = %s\n", config->global_pairs[i].key,
            config->global_pairs[i].value);
  }

  for (int i = 0; i < config->section_count; i++) {
    fprintf(f, "\n--- %s ---\n\n", config->sections[i].name);
    for (int j = 0; j < config->sections[i].pair_count; j++) {
      fprintf(f, "%s = %s\n", config->sections[i].pairs[j].key,
              config->sections[i].pairs[j].value);
    }
  }

  if (config->standalone_count > 0) {
    fprintf(f, "\n");
    for (int i = 0; i < config->standalone_count; i++) {
      fprintf(f, "%s\n", config->standalone[i]);
    }
  }

  fclose(f);
}

void ark_free(ArkConfig *config) {
  if (config)
    free(config);
}

/* ── List Value Support ────────────────────────────────────────── */

static ArkList parse_csv(const char *value) {
  ArkList list;
  memset(&list, 0, sizeof(ArkList));
  if (!value)
    return list;

  char buf[512];
  strncpy(buf, value, sizeof(buf) - 1);
  buf[sizeof(buf) - 1] = '\0';

  char *token = strtok(buf, ",");
  while (token && list.count < MAX_LIST_ITEMS) {
    char *trimmed = trim_whitespace(token);
    if (strlen(trimmed) > 0) {
      strncpy(list.items[list.count], trimmed, 255);
      list.count++;
    }
    token = strtok(NULL, ",");
  }
  return list;
}

ArkList ark_get_list(ArkConfig *config, const char *key) {
  const char *val = ark_get_global(config, key);
  return parse_csv(val);
}

ArkList ark_get_section_list(ArkConfig *config, const char *section,
                             const char *key) {
  const char *val = ark_get_section_val(config, section, key);
  return parse_csv(val);
}

/* ── Boolean Value Support ─────────────────────────────────────── */

static int parse_bool(const char *value, int default_val) {
  if (!value)
    return default_val;
  if (strcmp(value, "true") == 0 || strcmp(value, "yes") == 0 ||
      strcmp(value, "1") == 0 || strcmp(value, "on") == 0)
    return 1;
  if (strcmp(value, "false") == 0 || strcmp(value, "no") == 0 ||
      strcmp(value, "0") == 0 || strcmp(value, "off") == 0)
    return 0;
  return default_val;
}

int ark_get_bool(ArkConfig *config, const char *key, int default_val) {
  const char *val = ark_get_global(config, key);
  return parse_bool(val, default_val);
}

int ark_get_section_bool(ArkConfig *config, const char *section,
                         const char *key, int default_val) {
  const char *val = ark_get_section_val(config, section, key);
  return parse_bool(val, default_val);
}
