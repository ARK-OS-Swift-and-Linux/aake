#include "aake.h"
#include "ark_parser.h"
#include <dirent.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static void ensure_dir(const char *dir) { mkdir(dir, 0755); }

static void process_ark_file(const char *dir_path, const char *ark_filename,
                             FILE *root_ninja, bool is_x86_64) {
  char ark_path[2048];
  snprintf(ark_path, sizeof(ark_path), "%s/%s", dir_path, ark_filename);
  if (access(ark_path, F_OK) != 0)
    return;

  ArkConfig *cfg = ark_parse(ark_path);
  if (!cfg)
    return;

  char ninja_dir[2048];
  snprintf(ninja_dir, sizeof(ninja_dir), "%s/.ninja", dir_path);
  ensure_dir(ninja_dir);

  char ninja_path[2048];
  snprintf(ninja_path, sizeof(ninja_path), "%s/.ninja/build.ninja", dir_path);
  FILE *nf = fopen(ninja_path, "w");
  if (!nf) {
    ark_free(cfg);
    return;
  }

  // Tell the master ninja to include this
  fprintf(root_ninja, "subninja ../%s\n",
          ninja_path + 2); // ninja is invoked in builddir so paths need ../

  for (int i = 0; i < cfg->section_count; i++) {
    if (strcmp(cfg->sections[i].name, "TARGET") == 0) {
      const char *name = NULL;
      const char *type = NULL;
      const char *sources = NULL;
      const char *c_args = NULL;
      const char *swift_args = NULL;
      const char *dest = NULL;
      const char *depends = NULL;
      for (int j = 0; j < cfg->sections[i].pair_count; j++) {
        if (strcmp(cfg->sections[i].pairs[j].key, "Name") == 0)
          name = cfg->sections[i].pairs[j].value;
        if (strcmp(cfg->sections[i].pairs[j].key, "Type") == 0)
          type = cfg->sections[i].pairs[j].value;
        if (strcmp(cfg->sections[i].pairs[j].key, "Sources") == 0)
          sources = cfg->sections[i].pairs[j].value;
        if (strcmp(cfg->sections[i].pairs[j].key, "Dest") == 0)
          dest = cfg->sections[i].pairs[j].value;
        if (strcmp(cfg->sections[i].pairs[j].key, "CArgs") == 0)
          c_args = cfg->sections[i].pairs[j].value;
        if (strcmp(cfg->sections[i].pairs[j].key, "SwiftArgs") == 0)
          swift_args = cfg->sections[i].pairs[j].value;
        if (strcmp(cfg->sections[i].pairs[j].key, "Depends") == 0)
          depends = cfg->sections[i].pairs[j].value;
      }

      if (!name || !type || !sources)
        continue;

      char src_copy[65536];
      strncpy(src_copy, sources, sizeof(src_copy) - 1);
      src_copy[sizeof(src_copy) - 1] = '\0';

      if (strcmp(type, "copy") == 0) {
        if (!dest)
          continue;
        char cp_srcs[65536] = "";
        char *tok = strtok(src_copy, ", ");
        while (tok) {
          char path[512];
          if (strcmp(tok, ".") == 0) {
            snprintf(path, sizeof(path), "../%s", dir_path + 2);
          } else {
            snprintf(path, sizeof(path), "../%s/%s", dir_path + 2, tok);
          }
          strcat(cp_srcs, path);
          strcat(cp_srcs, " ");
          tok = strtok(NULL, ", ");
        }
        char dest_path[512];
        if (strncmp(dest, "builddir", 11) == 0) {
          snprintf(dest_path, sizeof(dest_path), "../%s", dest);
        } else {
          snprintf(dest_path, sizeof(dest_path), "%s", dest);
        }
        fprintf(nf, "build %s: copy_to_dir %s\n", dest_path, cp_srcs);
        continue;
      }

      char c_objs[65536] = "";
      char swift_srcs[65536] = "";
      int has_swift = 0;

      char *tok = strtok(src_copy, ", ");
      while (tok) {
        // Remove leading/trailing spaces
        while (*tok == ' ')
          tok++;
        char *end = tok + strlen(tok) - 1;
        while (end > tok && *end == ' ') {
          *end = '\0';
          end--;
        }
        if (strlen(tok) == 0) {
          tok = strtok(NULL, ", ");
          continue;
        }

        char *ext = strrchr(tok, '.');
        if (ext && strcmp(ext, ".swift") == 0) {
          has_swift = 1;
          char path[512];
          snprintf(path, sizeof(path), "../%s/%s", dir_path + 2, tok);
          strcat(swift_srcs, path);
          strcat(swift_srcs, " ");
        } else if (ext && strcmp(ext, ".asm") == 0) {
          char obj_path[512];
          snprintf(obj_path, sizeof(obj_path), "../builddir/%s", tok);
          char *dot = strrchr(obj_path, '.');
          if (dot)
            strcpy(dot, ".bin");

          ensure_dir("builddir");
          fprintf(nf, "build %s: nasm ../%s/%s\n", obj_path, dir_path + 2, tok);
        } else {
          char obj_path[512];
          snprintf(obj_path, sizeof(obj_path), "%s/%s.o", dir_path + 2, tok);

          char out_dir[2048];
          snprintf(out_dir, sizeof(out_dir), "builddir/%s", dir_path + 2);
          ensure_dir(out_dir);

          if (strcmp(type, "efi_executable") == 0) {
            fprintf(nf, "build %s: efi_cc ../%s/%s\n", obj_path, dir_path + 2,
                    tok);
            if (c_args)
              fprintf(nf, "  cflags = $efi_cflags %s\n", c_args);
          } else {
            fprintf(nf, "build %s: cc ../%s/%s\n", obj_path, dir_path + 2, tok);
            if (c_args) {
              fprintf(nf, "  cflags = $cflags %s\n", c_args);
              fprintf(nf, "  c_args = %s\n", c_args);
            }
          }

          strcat(c_objs, obj_path);
          strcat(c_objs, " ");
        }

        tok = strtok(NULL, ", ");
      }

      if (has_swift) {
        char safe_module_name[512];
        strncpy(safe_module_name, name, sizeof(safe_module_name));
        for (int i = 0; safe_module_name[i]; i++) {
          if (safe_module_name[i] == '-' || safe_module_name[i] == '.') {
            safe_module_name[i] = '_';
          }
        }

        char module_iface[512];
        snprintf(module_iface, sizeof(module_iface), "builddir/%s.swiftmodule",
                 safe_module_name);
        fprintf(nf, "build %s: swiftc_emit_module %s", module_iface,
                swift_srcs);
        if (depends) {
          fprintf(nf, " |");
          char dep_copy[4096];
          strncpy(dep_copy, depends, sizeof(dep_copy) - 1);
          dep_copy[sizeof(dep_copy) - 1] = '\0';
          char *dep_tok = strtok(dep_copy, ", ");
          while (dep_tok) {
            while (*dep_tok == ' ')
              dep_tok++;
            char safe_dep[256];
            strncpy(safe_dep, dep_tok, sizeof(safe_dep) - 1);
            safe_dep[sizeof(safe_dep) - 1] = '\0';
            for (int k = 0; safe_dep[k]; k++) {
              if (safe_dep[k] == '-' || safe_dep[k] == '.')
                safe_dep[k] = '_';
            }
            fprintf(nf, " builddir/%s.swiftmodule", safe_dep);
            dep_tok = strtok(NULL, ", ");
          }
        }
        fprintf(nf, "\n");
        fprintf(nf, "  module_name = %s\n", safe_module_name);
        if (strstr(swift_srcs, "main.swift")) {
          fprintf(nf, "  parse_as_library =\n");
        } else {
          fprintf(nf, "  parse_as_library = -parse-as-library\n");
        }
        if (swift_args)
          fprintf(nf, "  swift_args = %s\n", swift_args);
        fprintf(nf, "\n");

        char *all_srcs[4096];
        int src_count = 0;
        char *swift_tok = strtok(strdup(swift_srcs), " ");
        while (swift_tok) {
          all_srcs[src_count++] = strdup(swift_tok);
          swift_tok = strtok(NULL, " ");
        }

        for (int s = 0; s < src_count; s++) {
          char module_obj[512];
          snprintf(module_obj, sizeof(module_obj), "%s/%s_swift_%d.o",
                   dir_path + 2, name, s);

          fprintf(nf, "build %s: swiftc_module %s | %s\n", module_obj,
                  all_srcs[s], module_iface);
          fprintf(nf, "  module_name = %s\n", safe_module_name);

          fprintf(nf, "  swift_context = ");
          for (int other = 0; other < src_count; other++) {
            if (other != s) {
              fprintf(nf, "%s ", all_srcs[other]);
            }
          }
          fprintf(nf, "\n");

          if (strstr(all_srcs[s], "main.swift")) {
            fprintf(nf, "  parse_as_library =\n");
          } else {
            fprintf(nf, "  parse_as_library = -parse-as-library\n");
          }

          if (swift_args) {
            fprintf(nf, "  swift_args = %s\n", swift_args);
          }

          strcat(c_objs, module_obj);
          strcat(c_objs, " ");
        }

        for (int s = 0; s < src_count; s++) {
          free(all_srcs[s]);
        }
      }

      char *out_objs = strdup(c_objs);

      // Link step
      if (strlen(out_objs) > 0) {
        if (strcmp(type, "executable") == 0) {
          if (has_swift) {
            fprintf(nf, "build %s/%s: swift_link %s\n", dir_path + 2, name,
                    out_objs);
          } else {
            fprintf(nf, "build %s/%s: link %s\n", dir_path + 2, name, out_objs);
          }
        } else if (strcmp(type, "efi_executable") == 0) {
          fprintf(nf, "build ../builddir/BOOT%s.EFI: efi_link %s\n",
                  is_x86_64 ? "X64" : "AA64", out_objs);
        } else if (strcmp(type, "static_library") == 0) {
          fprintf(nf, "build %s/lib%s.a: ar %s\n", dir_path + 2, name,
                  out_objs);
        } else if (strcmp(type, "shared_library") == 0) {
          fprintf(nf, "build %s/lib%s.so: link %s\n", dir_path + 2, name,
                  out_objs);
          fprintf(nf, "  ldflags = $ldflags -shared\n");
        }
      }
      free(out_objs);
    }
  }

  fclose(nf);
  ark_free(cfg);
}

static void scan_dir(const char *dir, FILE *root_ninja, bool visible_blueprint,
                     bool is_x86_64) {
  DIR *d = opendir(dir);
  if (!d)
    return;

  struct dirent *dir_ent;
  while ((dir_ent = readdir(d)) != NULL) {
    if (strcmp(dir_ent->d_name, ".") == 0 || strcmp(dir_ent->d_name, "..") == 0)
      continue;
    if (strcmp(dir_ent->d_name, "builddir") == 0 ||
        strcmp(dir_ent->d_name, "builddir") == 0)
      continue;
    if (strcmp(dir_ent->d_name, "arkrt") == 0)
      continue;
    if (strcmp(dir_ent->d_name, ".git") == 0 ||
        strcmp(dir_ent->d_name, ".build") == 0)
      continue;

    char path[2048];
    snprintf(path, sizeof(path), "%s/%s", dir, dir_ent->d_name);

    struct stat st;
    if (stat(path, &st) == 0 && S_ISDIR(st.st_mode)) {
      // Handle visible vs hidden logic
      if (visible_blueprint) {
        char hidden_ark[4096], visible_ark[4096];
        snprintf(hidden_ark, sizeof(hidden_ark), "%s/.aake.ark", path);
        snprintf(visible_ark, sizeof(visible_ark), "%s/aake.ark", path);

        if (access(hidden_ark, F_OK) == 0)
          rename(hidden_ark, visible_ark);

        char fallback[4096];
        snprintf(fallback, sizeof(fallback), "%s/build.ark", path);
        if (access(visible_ark, F_OK) == 0) {
          process_ark_file(path, "aake.ark", root_ninja, is_x86_64);
        } else if (access(fallback, F_OK) == 0) {
          process_ark_file(path, "build.ark", root_ninja, is_x86_64);
        }
      } else {
        char hidden_ark[4096], visible_ark[4096];
        snprintf(hidden_ark, sizeof(hidden_ark), "%s/.aake.ark", path);
        snprintf(visible_ark, sizeof(visible_ark), "%s/aake.ark", path);

        if (access(visible_ark, F_OK) == 0)
          rename(visible_ark, hidden_ark);

        char fallback[4096];
        snprintf(fallback, sizeof(fallback), "%s/build.ark", path);
        if (access(hidden_ark, F_OK) == 0) {
          process_ark_file(path, ".aake.ark", root_ninja, is_x86_64);
        } else if (access(fallback, F_OK) == 0) {
          rename(fallback, hidden_ark);
          process_ark_file(path, ".aake.ark", root_ninja, is_x86_64);
        }
      }
      scan_dir(path, root_ninja, visible_blueprint, is_x86_64);
    }
  }
  closedir(d);
}

void generate_blueprints(bool is_x86_64, bool visible_blueprint) {
  if (access("../arkrt/parse_spm.py", F_OK) == 0) {
    system("cd ../arkrt && python3 parse_spm.py >/dev/null 2>&1");
  }

  ensure_dir("builddir");
  FILE *f = fopen("builddir/build.ninja", "w");
  if (!f)
    return;

  // Resolve repo root dynamically — no hardcoded paths
  extern const char *get_repo_root(void);
  const char *repo = get_repo_root();

  // Write root ninja configuration
  fprintf(f, "ninja_required_version = 1.8.2\n\n");

  const char *target = is_x86_64 ? "x86_64" : "aarch64";
  const char *sysroot_arch = is_x86_64 ? "x86_64" : "aarch64";

  // Swift SDK subpath (relative to repo root)
  const char *sdk_subpath =
      "prebuilts/Swift/swift-6.3.2-RELEASE_static-linux-0.1.0/"
      "swift-linux-musl/musl-1.2.5.sdk";

  fprintf(f, "cc = clang\n");
  fprintf(f,
          "cflags = -target %s-linux-musl "
          "--sysroot=%s/%s/%s -fPIC -I../include -I../../include\n",
          target, repo, sdk_subpath, sysroot_arch);
  fprintf(
      f,
      "swift_cflags = -target %s-swift-linux-musl -sdk "
      "%s/%s/"
      "%s -resource-dir "
      "%s/%s/"
      "%s/usr/lib/swift_static -Xclang-linker -resource-dir -Xclang-linker "
      "%s/%s/"
      "%s/usr/lib/swift/clang -module-cache-path /tmp/swift-cache -I../include "
      "-I../../include -Ibuilddir/arkrt -module-alias Network=ArkNetwork\n",
      target, repo, sdk_subpath, sysroot_arch, repo, sdk_subpath, sysroot_arch,
      repo, sdk_subpath, sysroot_arch);
  fprintf(f,
          "swift_frontend_cflags = -target %s-swift-linux-musl -sdk "
          "%s/%s/"
          "%s -resource-dir "
          "%s/%s/"
          "%s/usr/lib/swift_static -module-cache-path "
          "/tmp/swift-cache -I../include -I../../include -Ibuilddir/arkrt "
          "-module-alias Network=ArkNetwork\n",
          target, repo, sdk_subpath, sysroot_arch, repo, sdk_subpath,
          sysroot_arch);
  fprintf(f,
          "ldflags = -target %s-linux-musl "
          "--sysroot=%s/%s/"
          "%s\n\n",
          target, repo, sdk_subpath, sysroot_arch);

  fprintf(f,
          "efi_cflags = -target %s-pc-win32-coff -fno-stack-protector "
          "-fshort-wchar -fno-builtin -I../prebuilts/efi/inc "
          "-I../prebuilts/efi/inc/%s -I../prebuilts/efi/inc/protocol\n",
          is_x86_64 ? "x86_64" : "aarch64", is_x86_64 ? "x86_64" : "aarch64");

  fprintf(f, "rule efi_cc\n");
  fprintf(f, "  command = $cc $efi_cflags -c $in -o $out\n");
  fprintf(f, "  description = EFI_CC $out\n\n");

  fprintf(f, "rule efi_link\n");
  fprintf(
      f,
      "  command = ld.lld -flavor "
      "link -subsystem:efi_application -entry:efi_main $in -out:$out\n");
  fprintf(f, "  description = EFI_LINK $out\n\n");

  fprintf(f, "rule cc\n");
  fprintf(f, "  command = $cc $cflags -c $in -o $out\n");
  fprintf(f, "  description = CC $out\n\n");

  fprintf(f, "rule nasm\n");
  fprintf(f, "  command = nasm -f bin $in -o $out\n");
  fprintf(f, "  description = NASM $out\n\n");

  fprintf(f, "rule swiftc_emit_module\n");
  fprintf(f, "  command = swiftc -frontend $swift_frontend_cflags $swift_args "
             "-module-name $module_name $parse_as_library -emit-module "
             "-emit-module-path $out $in\n");
  fprintf(f, "  description = SWIFTC IFACE $module_name\n\n");

  fprintf(f, "rule swiftc_module\n");
  fprintf(f, "  command = swiftc -frontend $swift_frontend_cflags $swift_args "
             "-module-name $module_name $parse_as_library -c -primary-file $in "
             "$swift_context -o $out\n");
  fprintf(f, "  description = SWIFTC MODULE $module_name $in\n\n");

  fprintf(f, "rule link\n");
  fprintf(f, "  command = $cc $ldflags $in -o $out\n");
  fprintf(f, "  description = LINK $out\n\n");

  fprintf(f, "rule swift_link\n");
  fprintf(
      f,
      "  command = swiftc -target %s-swift-linux-musl -sdk "
      "%s/%s/"
      "%s -static-stdlib -resource-dir "
      "%s/%s/"
      "%s/usr/lib/swift_static -Xclang-linker -resource-dir -Xclang-linker "
      "%s/%s/"
      "%s/usr/lib/swift/clang -use-ld=lld -Xlinker -lswift_RegexParser $in "
      "builddir/arkrt/*.a -o $out\n",
      target, repo, sdk_subpath, sysroot_arch, repo, sdk_subpath, sysroot_arch,
      repo, sdk_subpath, sysroot_arch);
  fprintf(f, "  description = SWIFT LINK $out\n\n");

  fprintf(f, "rule ar\n");
  if (is_x86_64) {
    fprintf(f, "  command = ar rcs $out $in\n");
  } else {
    fprintf(f, "  command = aarch64-linux-gnu-ar rcs $out $in\n");
  }
  fprintf(f, "  description = AR $out\n\n");

  fprintf(f, "rule copy_to_dir\n");
  fprintf(f, "  command = mkdir -p $out && for f in $in; do if [ -d \"$$f\" ]; "
             "then cp -r $$f/* $out/ 2>/dev/null || true; else cp -r $$f "
             "$out/; fi; done\n");
  fprintf(f, "  description = COPY $in to $out/\n\n");

  scan_dir(".", f, visible_blueprint, is_x86_64);
  fclose(f);
}

