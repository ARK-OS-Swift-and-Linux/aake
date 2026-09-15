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

#include "aake.h"
#include <stdbool.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/sysinfo.h>
#include <sys/utsname.h>
#include <sys/wait.h>
#include <unistd.h>

// Resolve the repository root directory at runtime instead of hardcoding
const char *get_repo_root(void) {
  static char root[PATH_MAX] = {0};
  if (root[0] == '\0') {
    if (realpath(".", root) == NULL) {
      fprintf(stderr, "[ERROR] Cannot resolve repo root\n");
      exit(1);
    }
  }
  return root;
}

static int run_cmd_checked(const char *cmd) {
  int ret = system(cmd);
  if (WIFEXITED(ret))
    return WEXITSTATUS(ret);
  return 1;
}

// Build native C libraries (wayland, libxkbcommon) from repo sources via meson,
// and compile the GTK3 display app against the host system's GTK3.
static int build_native_graphics(void) {
  const char *repo = get_repo_root();
  char cmd[4096];
  int rc;

  printf("\033[1;36m[GRAPHICS]\033[0m Building native graphics stack...\n");
  snprintf(cmd, sizeof(cmd), "make -C %s/arkrt/Graphics", repo);
  rc = run_cmd_checked(cmd);
  if (rc != 0) {
    printf("\033[1;31m[GRAPHICS]\033[0m Graphics stack build failed\n");
    return 1;
  }

  printf("\033[1;36m[GRAPHICS]\033[0m Building native input stack...\n");
  snprintf(cmd, sizeof(cmd), "make -C %s/arkrt/Input", repo);
  rc = run_cmd_checked(cmd);
  if (rc != 0) {
    printf("\033[1;31m[GRAPHICS]\033[0m Input stack build failed\n");
    return 1;
  }


  // --- Collect built libraries into builddir ---
  snprintf(cmd, sizeof(cmd),
           "mkdir -p %s/builddir/arkrt/graphics/lib && "
           "find %s/arkrt/Graphics -name '*.so*' -exec cp -P {} %s/builddir/arkrt/graphics/lib/ \\; 2>/dev/null; "
           "find %s/arkrt/Input -name '*.so*' -exec cp -P {} %s/builddir/arkrt/graphics/lib/ \\; 2>/dev/null; true",
           repo, repo, repo, repo, repo);
  system(cmd);

  return 0;
}

static void generate_cross_file(bool is_x86_64) {
  const char *repo = get_repo_root();
  mkdir("cross", 0755);

  if (is_x86_64) {
    FILE *f = fopen("cross/x86_64.ini", "w");
    if (!f)
      return;
    fprintf(f, "[binaries]\n");
    fprintf(f, "c = ['clang', "
               "'-target', 'x86_64-linux-musl', "
               "'--sysroot=%s/prebuilts/Swift/"
               "swift-6.3.2-RELEASE_static-linux-0.1.0/swift-linux-musl/"
               "musl-1.2.5.sdk/x86_64']\n", repo);
    fprintf(f, "cpp = ['clang++', "
               "'-target', 'x86_64-linux-musl', "
               "'--sysroot=%s/prebuilts/Swift/"
               "swift-6.3.2-RELEASE_static-linux-0.1.0/swift-linux-musl/"
               "musl-1.2.5.sdk/x86_64']\n", repo);
    fprintf(f, "ar = 'ar'\n");
    fprintf(f, "strip = 'strip'\n");
    fprintf(f, "pkgconfig = 'pkg-config'\n\n");
    fprintf(f, "[host_machine]\n");
    fprintf(f, "system = 'linux'\n");
    fprintf(f, "cpu_family = 'x86_64'\n");
    fprintf(f, "cpu = 'x86_64'\n");
    fprintf(f, "endian = 'little'\n");
    fclose(f);
  } else {
    FILE *f = fopen("cross/aarch64.ini", "w");
    if (!f)
      return;
    fprintf(f, "[binaries]\n");
    fprintf(f, "c = ['clang', "
               "'-target', 'aarch64-linux-musl', "
               "'--sysroot=%s/prebuilts/Swift/"
               "swift-6.3.2-RELEASE_static-linux-0.1.0/swift-linux-musl/"
               "musl-1.2.5.sdk/aarch64']\n", repo);
    fprintf(f, "cpp = ['clang++', "
               "'-target', 'aarch64-linux-musl', "
               "'--sysroot=%s/prebuilts/Swift/"
               "swift-6.3.2-RELEASE_static-linux-0.1.0/swift-linux-musl/"
               "musl-1.2.5.sdk/aarch64']\n", repo);
    fprintf(f, "ar = 'aarch64-linux-gnu-ar'\n");
    fprintf(f, "strip = 'aarch64-linux-gnu-strip'\n");
    fprintf(f, "pkgconfig = 'aarch64-linux-gnu-pkg-config'\n\n");
    fprintf(f, "[host_machine]\n");
    fprintf(f, "system = 'linux'\n");
    fprintf(f, "cpu_family = 'aarch64'\n");
    fprintf(f, "cpu = 'aarch64'\n");
    fprintf(f, "endian = 'little'\n");
    fclose(f);
  }
}

static void run_test(bool is_x86_64, bool is_uefi) {
  char cmd[1024];
  if (is_x86_64) {
    if (is_uefi) {
      snprintf(cmd, sizeof(cmd),
               "qemu-system-x86_64 -m 2G -enable-kvm -vga std -device usb-ehci -device usb-kbd -device usb-mouse -drive "
               "if=pflash,format=raw,readonly=on,file=/usr/share/OVMF/x64/"
               "OVMF_CODE.4m.fd -drive if=pflash,format=raw,file=ovmf_vars.fd "
               "-drive file=finished/boot.img,format=raw -serial stdio");
      
      system("cp /usr/share/OVMF/x64/OVMF_VARS.4m.fd ovmf_vars.fd 2>/dev/null");
    } else {
      snprintf(cmd, sizeof(cmd),
               "qemu-system-x86_64 -m 2G -enable-kvm -vga std -device usb-ehci -device usb-kbd -device usb-mouse -drive "
               "file=finished/boot.img,format=raw -serial stdio");
    }
  } else {
    snprintf(cmd, sizeof(cmd),
             "qemu-system-aarch64 -M virt -cpu max -m 2G -device "
             "virtio-gpu-pci -device virtio-keyboard-pci -drive "
             "file=finished/rpi4/rpi4.img,format=raw,if=virtio -serial stdio");
  }
  printf("\033[1;34m[TEST]\033[0m Running: %s\n", cmd);
  system(cmd);
}

static void print_help(void) {
  printf("ArkOS Build System (aake)\n\n");
  printf("Usage:\n");
  printf("  aake [options] [commands]\n\n");
  printf("Commands:\n");
  printf("  build          Build ArkOS for the host architecture (default if "
         "options passed)\n");
  printf(
      "  test           Run ArkOS in QEMU (automatically builds if needed)\n");
  printf("  clear          Clean the build directory (use --cache-only to keep "
         "finished files)\n");
  printf("  sign           Sign binaries\n");
  printf("  set            Set configuration options (e.g. "
         "VISIBLE_BLUEPRINT=true)\n");
  printf("  font-gen       Generate TTF fonts to Swift/C arrays\n");
  printf("  cursor-gen     Generate mouse cursor arrays\n\n");
  printf("Options:\n");
  printf("  --x86_64       Build/test for x86_64 architecture\n");
  printf("  --arm64        Build/test for ARM64 architecture\n");
  printf("  --uefi         Test in UEFI mode (for x86_64)\n");
  printf("  --bios         Test in BIOS mode (for x86_64)\n");
  printf("  -v             Verbose output\n");
  printf("  -j <jobs>      Number of parallel jobs\n");
  printf("  --no-ninja     Generate build files but don't compile\n");
}

int main(int argc, char **argv) {
  if (argc == 1 || (argc == 2 && (strcmp(argv[1], "-h") == 0 ||
                                  strcmp(argv[1], "--help") == 0))) {
    print_help();
    return 0;
  }

  bool do_ninja = true;
  bool is_x86_64 = false;
  bool is_arm64 = false;
  bool is_test = false;
  bool is_uefi = false;
  bool verbose = false;
  bool has_build = false;
  char *specific_module = NULL;
  int jobs = -1;

  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "build") == 0) {
      has_build = true;
    } else if (strcmp(argv[i], "--x86_64") == 0) {
      is_x86_64 = true;
    } else if (strcmp(argv[i], "--arm64") == 0 ||
               strcmp(argv[i], "--arch64") == 0) {
      is_arm64 = true;
    } else if (strcmp(argv[i], "test") == 0) {
      is_test = true;
    } else if (strcmp(argv[i], "--uefi") == 0) {
      is_uefi = true;
    } else if (strcmp(argv[i], "--bios") == 0) {
      is_uefi = false;
    } else if (strcmp(argv[i], "clear") == 0 || strcmp(argv[i], "clean") == 0) {
      bool cache_only =
          (i + 1 < argc && strcmp(argv[i + 1], "--cache-only") == 0);
      if (cache_only) {
        printf("\033[1;33m[CLEAN]\033[0m Clearing cache directories only "
               "(.ninja, builddir, cross)...\n");
        system("rm -rf builddir cross");
        system("find . -type d -name '.ninja' -exec rm -rf {} +");
      } else {
        printf(
            "\033[1;33m[CLEAN]\033[0m Cleaning complete build directory...\n");
        system("rm -rf builddir finished cross");
        system("find . -type d -name '.ninja' -exec rm -rf {} +");
      }
      return 0;
    } else if (strcmp(argv[i], "set") == 0 && i + 1 < argc) {
      if (strncmp(argv[i + 1], "VISIBLE_BLUEPRINT=", 18) == 0) {
        const char *val = argv[i + 1] + 18;
        FILE *cf = fopen(".aake_config", "w");
        if (cf) {
          fprintf(cf, "VISIBLE_BLUEPRINT=%s\n", val);
          fclose(cf);
          printf("\033[1;32m[CONFIG]\033[0m VISIBLE_BLUEPRINT set to %s\n",
                 val);
        }
      }
      return 0;
    } else if (strcmp(argv[i], "m") == 0 && i + 1 < argc) {
      specific_module = argv[i + 1];
      i++;
    } else if (strcmp(argv[i], "sync") == 0) {
      bool do_status = (i + 1 < argc && strcmp(argv[i + 1], "--status") == 0);
      if (do_status) {
        printf("Scanning for subtree local modifications...\n");
        system("python3 ./arkos/build/arkrt_sync.py status");
        return 0;
      }
      printf("Syncing source code and subtrees...\n");
      system("git pull");
      system("python3 ./arkos/build/arkrt_sync.py pull");
      return 0;
    } else if (strcmp(argv[i], "sign") == 0) {
      printf("Signing binaries...\n");
      package_sign();
      return 0;
    } else if (strcmp(argv[i], "font-gen") == 0) {
      generate_fonts();
      return 0;
    } else if (strcmp(argv[i], "cursor-gen") == 0) {
      generate_cursors();
      return 0;
    } else if (strcmp(argv[i], "--no-ninja") == 0) {
      do_ninja = false;
    } else if (strcmp(argv[i], "-v") == 0) {
      verbose = true;
    } else if (strcmp(argv[i], "--arkos-version") == 0) {
      printf("ArkOS version: current alpha\n");
      return 0;
    } else if (strcmp(argv[i], "--no-warnings") == 0) {
      // Ignore
    } else if (strcmp(argv[i], "--cache-only") == 0) {
      // Handled in clear
    } else if (strncmp(argv[i], "-j", 2) == 0) {
      jobs = atoi(argv[i] + 2);
    }
  }

  if (is_test && !has_build) {
    do_ninja = false;
  }

  if (!is_x86_64 && !is_arm64) {
    struct utsname unameData;
    uname(&unameData);
    if (strstr(unameData.machine, "x86_64") ||
        strstr(unameData.machine, "amd64")) {
      is_x86_64 = true;
    } else {
      is_arm64 = true;
    }
  }

  struct sysinfo info;
  if (sysinfo(&info) == 0) {
    long long total_ram = (long long)info.totalram * info.mem_unit;
    long long six_gb = 6LL * 1024 * 1024 * 1024;
    if (total_ram < six_gb) {
      printf("\033[1;31m[WARNING]\033[0m Your RAM is lower than the "
             "recommended amount of 6GB!\n");
      printf("If your command fails due to memory errors, lower down your -j "
             "value or add a large amount of swapspace!\n");
    }
  }

  struct utsname unameData;
  uname(&unameData);

  printf("\n\033[1;36m====================================================\033["
         "0m\n");
  printf("\033[1;36m AAKE BUILD SYSTEM \033[0m\n");
  printf("\033[1;36m====================================================\033["
         "0m\n");
  printf(" \033[1;33mSystem:\033[0m %s %s\n", unameData.sysname,
         unameData.release);
  printf(" \033[1;33mTarget:\033[0m %s\n",
         is_x86_64 ? "x86_64" : "aarch64 (rpi4)");
  printf(" \033[1;33mPart:\033[0m %s\n", "Core OS");
  printf("\033[1;36m====================================================\033["
         "0m\n\n");

  // Read config
  bool visible_blueprint = false;
  FILE *cf = fopen(".aake_config", "r");
  if (cf) {
    char line[128];
    while (fgets(line, sizeof(line), cf)) {
      if (strncmp(line, "VISIBLE_BLUEPRINT=true", 22) == 0) {
        visible_blueprint = true;
      }
    }
    fclose(cf);
  }

  if (do_ninja) {
    // 1. Generate blueprints (Ninja)
    printf("\033[1;34m[AAKE]\033[0m Parsing blueprints and generating ninja "
           "files...\n");
    if (access("arkrt/parse_spm.py", F_OK) == 0) {
      system("cd arkrt && python3 parse_spm.py >/dev/null 2>&1");
    }
    generate_blueprints(is_x86_64, visible_blueprint);

    // 2. Cross Compilation files (if still needed, though ninja might not need
    // them directly) We will keep them for compatibility if any other tool uses
    // them
    generate_cross_file(is_x86_64);

    // 3. Build ArkRT Swift packages
  {
    const char *arkrt_packages[] = {"Kernel",   "Network", "Service", "System",
                                    "Terminal", "Input",   "Init"};
    int num_pkgs = sizeof(arkrt_packages) / sizeof(arkrt_packages[0]);
    int any_failed = 0;

    system("mkdir -p builddir/arkrt");

    for (int p = 0; p < num_pkgs; p++) {
      char pkg_path[256];
      snprintf(pkg_path, sizeof(pkg_path), "arkrt/%s/Package.swift",
               arkrt_packages[p]);
      if (access(pkg_path, F_OK) != 0)
        continue;

      printf("\033[1;35m[ARKRT]\033[0m [%d/%d] Building %s...\n", p + 1,
             num_pkgs, arkrt_packages[p]);

      char cmd[1024];
      snprintf(
          cmd, sizeof(cmd),
          "swift build --package-path arkrt/%s --build-path builddir/arkrt/%s "
          "2>&1; echo $? > /tmp/arkrt_build_rc",
          arkrt_packages[p], arkrt_packages[p]);

      system(cmd);

      // Flatten outputs so they can be easily found
      char flatten_cmd[1024];
      snprintf(
          flatten_cmd, sizeof(flatten_cmd),
          "find builddir/arkrt/%s -name '*.swiftmodule' -exec cp -rf {} "
          "builddir/arkrt/ \\; ;"
          "find builddir/arkrt/%s -name '*.a' -exec cp -f {} builddir/arkrt/ \\; ;"
          "find builddir/arkrt/%s -name 'Init' -type f -exec cp {} "
          "builddir/arkrt/init_bin \\; ;"
          "find builddir/arkrt/%s -name 'BootAnim' -type f -exec cp {} "
          "builddir/arkrt/bootanim_bin \\; ;"
          "find builddir/arkrt/%s -name 'sash' -type f -exec cp {} "
          "builddir/arkrt/sash_bin \\;",
          arkrt_packages[p], arkrt_packages[p], arkrt_packages[p], arkrt_packages[p], arkrt_packages[p]);
      system(flatten_cmd);

      int ret = 1;
      FILE *rc = fopen("/tmp/arkrt_build_rc", "r");
      if (rc) {
        fscanf(rc, "%d", &ret);
        fclose(rc);
      }

      if (ret != 0) {
        printf("\033[1;31m[ARKRT]\033[0m Failed to build %s\n",
               arkrt_packages[p]);
        any_failed = 1;
      } else {
        printf("\033[1;32m[ARKRT]\033[0m %s built successfully\n",
               arkrt_packages[p]);
      }
    }

    if (any_failed) {
      printf("\033[1;31m[ARKRT]\033[0m Some ArkRT packages failed to build\n");
      return 1;
    }
  }

    // 3b. Build native graphics/input stack (wayland, xkbcommon, GTK3 app)
    printf("\033[1;34m[AAKE]\033[0m Building native graphics/input stack...\n");
    if (build_native_graphics() != 0) {
      printf("\033[1;31m[AAKE]\033[0m Native graphics build failed\n");
      return 1;
    }
  }

  // 4. Ninja Compilation
  if (do_ninja) {

    // If specific module is provided, parse its .aake.ark to get the target
    // Name
    char ninja_target[256];
    snprintf(ninja_target, sizeof(ninja_target), "%s",
             is_x86_64 ? "x86_64" : "aarch64");

    if (specific_module) {
      char ark_path[1024];
      snprintf(ark_path, sizeof(ark_path), "%s/.aake.ark", specific_module);
      if (access(ark_path, F_OK) != 0) {
        snprintf(ark_path, sizeof(ark_path), "%s/aake.ark", specific_module);
      }

      FILE *f = fopen(ark_path, "r");
      if (f) {
        char line[256];
        while (fgets(line, sizeof(line), f)) {
          if (strncmp(line, "Name = ", 7) == 0) {
            sscanf(line + 7, "%s", ninja_target);
            break;
          }
        }
        fclose(f);
      } else {
        printf("\033[1;31m[ERROR]\033[0m Could not find .aake.ark in %s\n",
               specific_module);
      }
    }

    if (run_ninja_ui(jobs, verbose, ninja_target) != 0) {

      return 1; // build failed
    }

    // 5. Packaging
    printf("\033[1;34m[AAKE]\033[0m Packaging ArkOS...\n");
    package_images(is_arm64);
  }

  if (is_test) {
    run_test(is_x86_64, is_uefi);
  }

  return 0;
}
