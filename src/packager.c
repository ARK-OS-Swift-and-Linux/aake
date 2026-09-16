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
#include "ark_parser.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

// Helper to get file size
static size_t get_file_size(const char *path) {
  FILE *f = fopen(path, "rb");
  if (!f)
    return 0;
  fseek(f, 0, SEEK_END);
  size_t sz = ftell(f);
  fclose(f);
  return sz;
}

// Helper to align up to multiple
static size_t align_up(size_t val, size_t align) {
  return (val + align - 1) & ~(align - 1);
}

// C version of pack_boot.py
static void pack_boot_x86_64(void) {
  printf("  [Native Packager] Building hybrid UEFI/BIOS boot.img...\n");

  // Read files
  size_t bootloader_sz = get_file_size("builddir/bootloader.bin");
  size_t stage2_sz = get_file_size("builddir/stage2.bin");
  size_t kernel_sz = get_file_size("builddir/x86_64");
  size_t initramfs_sz = get_file_size("finished/initramfs.img");
  size_t animation_sz = get_file_size("builddir/animation.bin");

  if (kernel_sz == 0) {
    printf("  ERROR: Missing kernel at builddir/x86_64\n");
    return;
  }

  uint8_t *bootloader = malloc(bootloader_sz);
  uint8_t *stage2 = calloc(1, 7 * 512); // Padded to 7 sectors
  uint8_t *kernel = malloc(kernel_sz);
  uint8_t *initramfs = malloc(initramfs_sz);
  uint8_t *animation = animation_sz > 0 ? malloc(animation_sz) : NULL;

  FILE *f;
  f = fopen("builddir/bootloader.bin", "rb");
  if (f) {
    fread(bootloader, 1, bootloader_sz, f);
    fclose(f);
  }
  f = fopen("builddir/stage2.bin", "rb");
  if (f) {
    fread(stage2, 1, stage2_sz, f);
    fclose(f);
  }
  f = fopen("builddir/x86_64", "rb");
  if (f) {
    fread(kernel, 1, kernel_sz, f);
    fclose(f);
  }
  f = fopen("finished/initramfs.img", "rb");
  if (f) {
    fread(initramfs, 1, initramfs_sz, f);
    fclose(f);
  }
  if (animation) {
    f = fopen("builddir/animation.bin", "rb");
    if (f) {
      fread(animation, 1, animation_sz, f);
      fclose(f);
    }
  }

  // Calculate LBA sectors
  uint32_t bootloader_sectors = 1;
  uint32_t gpt_metadata_sectors = 33;

  uint32_t animation_lba = bootloader_sectors + gpt_metadata_sectors + 7;
  uint32_t animation_size_sectors = align_up(animation_sz, 512) / 512;

  uint32_t x86_64_lba = animation_lba + animation_size_sectors;
  uint32_t x86_64_size_sectors = align_up(kernel_sz, 512) / 512;

  uint32_t initramfs_lba = x86_64_lba + x86_64_size_sectors;
  uint32_t initramfs_size_sectors = align_up(initramfs_sz, 512) / 512;
  uint32_t initramfs_size_bytes = initramfs_sz;

  // Patch stage2 variables
  // Look for 8,0,0,0,0,0,0 (28 bytes)
  uint32_t pattern[7] = {8, 0, 0, 0, 0, 0, 0};
  int found_idx = -1;
  for (int i = 0; i < (7 * 512) - 28; i++) {
    if (memcmp(&stage2[i], pattern, 28) == 0) {
      found_idx = i;
      break;
    }
  }

  if (found_idx != -1) {
    uint32_t patched_vars[7] = {x86_64_lba,
                                x86_64_size_sectors,
                                initramfs_lba,
                                initramfs_size_sectors,
                                initramfs_size_bytes,
                                animation_lba,
                                animation_size_sectors};
    memcpy(&stage2[found_idx], patched_vars, 28);
  }

  // Construct BIOS part
  size_t bios_size_bytes = (1 * 512) + (gpt_metadata_sectors * 512) +
                           (7 * 512) + (animation_size_sectors * 512) +
                           (x86_64_size_sectors * 512) +
                           (initramfs_size_sectors * 512);
  size_t bios_aligned_size = align_up(bios_size_bytes, 1024 * 1024);
  uint32_t bios_aligned_sectors = bios_aligned_size / 512;

  uint8_t *bios_data_padded = calloc(1, bios_aligned_size);
  size_t offset = 0;
  memcpy(bios_data_padded + offset, bootloader, bootloader_sz);
  offset += (1 * 512) + (gpt_metadata_sectors * 512);
  memcpy(bios_data_padded + offset, stage2, 7 * 512);
  offset += 7 * 512;
  if (animation) {
    memcpy(bios_data_padded + offset, animation, animation_sz);
  }
  offset += animation_size_sectors * 512;
  memcpy(bios_data_padded + offset, kernel, kernel_sz);
  offset += x86_64_size_sectors * 512;
  memcpy(bios_data_padded + offset, initramfs, initramfs_sz);
  offset += initramfs_size_sectors * 512;

  // Create EFI System Partition (FAT32)
  system("rm -f builddir/efi_part.img");
  system(
      "dd if=/dev/zero of=builddir/efi_part.img bs=1M count=800 2>/dev/null");

  char cmd[512];
  snprintf(cmd, sizeof(cmd),
           "mkfs.vfat -F 32 -h %u builddir/efi_part.img >/dev/null 2>&1",
           bios_aligned_sectors);
  system(cmd);
  system("mmd -i builddir/efi_part.img ::/EFI >/dev/null 2>&1");
  system("mmd -i builddir/efi_part.img ::/EFI/BOOT >/dev/null 2>&1");
  system("mcopy -i builddir/efi_part.img builddir/BOOTX64.EFI "
         "::/EFI/BOOT/BOOTX64.EFI >/dev/null 2>&1");
  system("mcopy -i builddir/efi_part.img builddir/x86_64 ::/EFI/BOOT/x86_64 "
         ">/dev/null 2>&1");
  system("mcopy -i builddir/efi_part.img finished/initramfs.img "
         "::/EFI/BOOT/initramfs.img >/dev/null 2>&1");
  if (animation) {
    system("mcopy -i builddir/efi_part.img builddir/animation.bin "
           "::/EFI/BOOT/animation.bin >/dev/null 2>&1");
    system("mcopy -i builddir/efi_part.img builddir/animation.bin "
           "::/animation.bin >/dev/null 2>&1");
  }

  size_t efi_sz = get_file_size("builddir/efi_part.img");
  uint8_t *efi_data = malloc(efi_sz);
  f = fopen("builddir/efi_part.img", "rb");
  if (f) {
    fread(efi_data, 1, efi_sz, f);
    fclose(f);
  }
  uint32_t efi_part_sectors = efi_sz / 512;

  // Write out_path
  f = fopen("finished/boot.img", "wb");
  if (f) {
    fwrite(bios_data_padded, 1, bios_aligned_size, f);
    fwrite(efi_data, 1, efi_sz, f);

    // GPT backup padding
    uint8_t gpt_padding[33 * 512] = {0};
    fwrite(gpt_padding, 1, sizeof(gpt_padding), f);

    // Overwrite MBR boot code
    fseek(f, 0, SEEK_SET);
    fwrite(bootloader, 1, 446, f);

    fclose(f);
  }

  // sgdisk
  snprintf(cmd, sizeof(cmd),
           "sgdisk -g -n 1:%u:%u -t 1:ef00 -c 1:'EFI' finished/boot.img "
           ">/dev/null 2>&1",
           bios_aligned_sectors, bios_aligned_sectors + efi_part_sectors - 1);
  system(cmd);

  printf(
      "  [Native Packager] boot.img (hybrid UEFI/BIOS) built successfully!\n");

  free(bootloader);
  free(stage2);
  free(kernel);
  free(initramfs);
  if (animation)
    free(animation);
  free(bios_data_padded);
  free(efi_data);
}

static void pack_boot_arm64(void) {
  printf("  [Native Packager] Building rpi4.img...\n");

  ArkConfig *cfg = ark_parse("hardware/rpi4/rpi4_config.ark");
  if (!cfg) {
    printf("  [Native Packager] ERROR: Missing or invalid "
           "hardware/rpi4/rpi4_config.ark\n");
    return;
  }

  const char *kernel_image = ark_get_section_val(cfg, "kernel", "kernel_image");
  const char *kernel_name = ark_get_section_val(cfg, "kernel", "kernel_name");
  const char *dtb_file = ark_get_section_val(cfg, "kernel", "dtb_file");
  const char *gpu_bootloader =
      ark_get_section_val(cfg, "firmware", "gpu_bootloader");
  const char *gpu_fixup = ark_get_section_val(cfg, "firmware", "gpu_fixup");
  const char *config_file = ark_get_section_val(cfg, "firmware", "config_file");
  const char *cmdline_file =
      ark_get_section_val(cfg, "firmware", "cmdline_file");
  const char *initramfs = ark_get_section_val(cfg, "kernel", "initramfs");

  // Fallbacks if missing
  if (!kernel_image)
    kernel_image = "kernel/prebuilts/arm64";
  if (!kernel_name)
    kernel_name = "kernel8.img";
  if (!dtb_file)
    dtb_file = "kernel/prebuilts/bcm2711-rpi-4-b.dtb";
  if (!gpu_bootloader)
    gpu_bootloader = "boot/rpi4/firmware/start4.elf";
  if (!gpu_fixup)
    gpu_fixup = "boot/rpi4/firmware/fixup4.dat";
  if (!config_file)
    config_file = "boot/rpi4/config.txt";
  if (!cmdline_file)
    cmdline_file = "boot/rpi4/cmdline.txt";
  if (!initramfs)
    initramfs = "finished/initramfs.img";

  const char *boot_sz_str =
      ark_get_section_val(cfg, "partitions", "boot_partition_size_mb");
  const char *sys_sz_str =
      ark_get_section_val(cfg, "partitions", "system_partition_size_mb");
  const char *vend_sz_str =
      ark_get_section_val(cfg, "partitions", "vendor_partition_size_mb");

  int boot_sz = boot_sz_str ? atoi(boot_sz_str) : 256;
  int sys_sz = sys_sz_str ? atoi(sys_sz_str) : 2048;
  int vend_sz = vend_sz_str ? atoi(vend_sz_str) : 100;

  int total_sz = boot_sz + sys_sz + vend_sz + 6; // +6 for padding/gpt

  char cmd[1024];

  system("mkdir -p finished/rpi4 builddir/rpi4/boot");

  // Stage boot files
  snprintf(cmd, sizeof(cmd), "cp %s builddir/rpi4/boot/%s 2>/dev/null || true",
           kernel_image, kernel_name);
  system(cmd);

  snprintf(cmd, sizeof(cmd),
           "cp %s builddir/rpi4/boot/initramfs.img 2>/dev/null || true",
           initramfs);
  system(cmd);

  snprintf(cmd, sizeof(cmd),
           "cp %s builddir/rpi4/boot/config.txt 2>/dev/null || true",
           config_file);
  system(cmd);

  snprintf(cmd, sizeof(cmd),
           "cp %s builddir/rpi4/boot/cmdline.txt 2>/dev/null || true",
           cmdline_file);
  system(cmd);

  snprintf(cmd, sizeof(cmd),
           "cp %s builddir/rpi4/boot/bcm2711-rpi-4-b.dtb 2>/dev/null || true",
           dtb_file);
  system(cmd);

  snprintf(cmd, sizeof(cmd),
           "cp %s builddir/rpi4/boot/start4.elf 2>/dev/null || true",
           gpu_bootloader);
  system(cmd);

  snprintf(cmd, sizeof(cmd),
           "cp %s builddir/rpi4/boot/fixup4.dat 2>/dev/null || true",
           gpu_fixup);
  system(cmd);

  // Create boot partition
  snprintf(
      cmd, sizeof(cmd),
      "dd if=/dev/zero of=builddir/rpi4/boot.img bs=1M count=%d 2>/dev/null",
      boot_sz);
  system(cmd);

  system("mkfs.vfat -F 32 -n ARKOS_BOOT builddir/rpi4/boot.img 2>/dev/null");

  // mcopy
  system("mcopy -i builddir/rpi4/boot.img builddir/rpi4/boot/* ::/ >/dev/null "
         "2>&1");

  // Assemble rpi4.img
  snprintf(
      cmd, sizeof(cmd),
      "dd if=/dev/zero of=finished/rpi4/rpi4.img bs=1M count=%d 2>/dev/null",
      total_sz);
  system(cmd);

  system("parted -s finished/rpi4/rpi4.img mklabel gpt");

  int start = 4;
  int end = start + boot_sz;
  snprintf(cmd, sizeof(cmd),
           "parted -s finished/rpi4/rpi4.img mkpart boot fat32 %dMiB %dMiB",
           start, end);
  system(cmd);

  start = end;
  end = start + sys_sz;
  snprintf(cmd, sizeof(cmd),
           "parted -s finished/rpi4/rpi4.img mkpart system ext4 %dMiB %dMiB",
           start, end);
  system(cmd);

  start = end;
  end = start + vend_sz;
  snprintf(cmd, sizeof(cmd),
           "parted -s finished/rpi4/rpi4.img mkpart vendor ext4 %dMiB %dMiB",
           start, end);
  system(cmd);

  system("parted -s finished/rpi4/rpi4.img set 1 boot on");

  snprintf(cmd, sizeof(cmd),
           "dd if=builddir/rpi4/boot.img of=finished/rpi4/rpi4.img bs=1M "
           "seek=4 conv=notrunc 2>/dev/null");
  system(cmd);

  snprintf(cmd, sizeof(cmd),
           "dd if=finished/sys.img of=finished/rpi4/rpi4.img bs=1M seek=%d "
           "conv=notrunc 2>/dev/null",
           4 + boot_sz);
  system(cmd);

  snprintf(cmd, sizeof(cmd),
           "dd if=finished/vend.img of=finished/rpi4/rpi4.img bs=1M seek=%d "
           "conv=notrunc 2>/dev/null",
           4 + boot_sz + sys_sz);
  system(cmd);

  ark_free(cfg);
  printf("  [Native Packager] rpi4.img built successfully!\n");
}

void package_sign(void) {
  system("mkdir -p builddir");
  system("python3 tools/generate_keys.py sign builddir/arkrt/init_bin 2>/dev/null || true");
  system("python3 tools/generate_keys.py sign builddir/arkrt/bootanim_bin 2>/dev/null || true");
  system("python3 tools/generate_keys.py sign arkrt/Graphics/GTK3/Support/seatd/builddir/seatd 2>/dev/null || true");
  system("python3 tools/generate_keys.py sign arkrt/Graphics/GTK3/Graphics/weston/builddir/frontend/weston 2>/dev/null || true");
  system("python3 tools/generate_keys.py sign builddir/arkrt/sash_bin 2>/dev/null || true");
  system("python3 tools/generate_keys.py sign builddir/arkrt/ls_bin 2>/dev/null || true");
}

void package_images(bool is_arm64) {
  system("mkdir -p finished builddir/system/frameworks "
         "builddir/system/services builddir/vendor builddir/initramfs_ext/keys");

  if (is_arm64) {
    system("cp kernel/prebuilts/arm64 builddir/arm64 2>/dev/null || true");
  } else {
    system("cp kernel/prebuilts/x86_64 builddir/x86_64 2>/dev/null || true");
  }

  system("python3 boot/source/animationframes/generate_frames.py builddir/ 2>/dev/null || true");

  package_sign();

  system("rm -rf builddir/initramfs_ext && mkdir -p "
         "builddir/initramfs_ext/system/services builddir/initramfs_ext/run "
         "builddir/initramfs_ext/tmp builddir/initramfs_ext/var/log "
         "builddir/initramfs_ext/system/bin builddir/initramfs_ext/dev "
         "builddir/initramfs_ext/proc builddir/initramfs_ext/sys builddir/initramfs_ext/etc "
         "builddir/initramfs_ext/keys");
  system("echo 'root:x:0:0:root:/root:/bin/sh\n' > builddir/initramfs_ext/etc/passwd && "
         "echo 'root:x:0:\n' > builddir/initramfs_ext/etc/group");
         
  system("cp keys/MAIN.pub builddir/initramfs_ext/keys/MAIN.pub 2>/dev/null || true");
  system("cp keys/BASE.pub builddir/initramfs_ext/keys/BASE.pub 2>/dev/null || true");
  system("cp keys/BASE.cert builddir/initramfs_ext/keys/BASE.cert 2>/dev/null || true");
  system("cp keys/WIDE.key builddir/initramfs_ext/keys/WIDE.key 2>/dev/null || true");

  system("cp builddir/arkrt/init_bin builddir/initramfs_ext/init && "
         "cp builddir/arkrt/init_bin.sig builddir/initramfs_ext/init.sig 2>/dev/null || true && "
         "chmod +x builddir/initramfs_ext/init");
  system("cp builddir/system/apps/setup_app/ui_test "
         "builddir/initramfs_ext/system/ 2>/dev/null || true");

  // Copy the BootAnim app built by aake
  system("cp builddir/arkrt/bootanim_bin builddir/initramfs_ext/system/bin/BootAnim 2>/dev/null && "
         "cp builddir/arkrt/bootanim_bin.sig builddir/initramfs_ext/system/bin/BootAnim.sig 2>/dev/null || true && "
         "chmod +x builddir/initramfs_ext/system/bin/BootAnim || true");

  // Copy seatd and weston binaries
  system("cp arkrt/Graphics/GTK3/Support/seatd/builddir/seatd builddir/initramfs_ext/system/bin/seatd 2>/dev/null && "
         "cp arkrt/Graphics/GTK3/Support/seatd/builddir/seatd.sig builddir/initramfs_ext/system/bin/seatd.sig 2>/dev/null || true && "
         "chmod +x builddir/initramfs_ext/system/bin/seatd || true");
  
  system("cp arkrt/Graphics/GTK3/Graphics/weston/builddir/frontend/weston builddir/initramfs_ext/system/bin/weston 2>/dev/null && "
         "cp arkrt/Graphics/GTK3/Graphics/weston/builddir/frontend/weston.sig builddir/initramfs_ext/system/bin/weston.sig 2>/dev/null || true && "
         "chmod +x builddir/initramfs_ext/system/bin/weston || true");
         
  // Copy fallback shell
  system("cp builddir/arkrt/sash_bin builddir/initramfs_ext/system/bin/sh 2>/dev/null && "
         "cp builddir/arkrt/sash_bin.sig builddir/initramfs_ext/system/bin/sh.sig 2>/dev/null || true && "
         "cp builddir/arkrt/ls_bin builddir/initramfs_ext/system/bin/ls 2>/dev/null && "
         "cp builddir/arkrt/ls_bin.sig builddir/initramfs_ext/system/bin/ls.sig 2>/dev/null || true && "
         "chmod +x builddir/initramfs_ext/system/bin/sh || true");

  // Copy built wayland/xkbcommon libs from the meson build
  system("cp -P builddir/arkrt/graphics/lib/*.so* "
         "builddir/initramfs_ext/lib/ 2>/dev/null || true");

  // Copy Weston plugins
  system("mkdir -p builddir/initramfs_ext/usr/local/lib/libweston-17 && "
         "find arkrt/Graphics/GTK3/Graphics/weston/builddir -name \"*.so\" -exec cp {} builddir/initramfs_ext/usr/local/lib/libweston-17/ \\; 2>/dev/null || true");

  system("mkdir -p builddir/initramfs_ext/lib && cp -r "
         "arkrt/ark.display.graphics/prebuilts/x86_64/lib/* "
         "builddir/initramfs_ext/lib/ 2>/dev/null || true");

  // Resolve all shared lib dependencies for init and the display app via ldd
  system("ldd builddir/arkrt/init_bin "
         "builddir/arkrt/bootanim_bin "
         "arkrt/Graphics/GTK3/Support/seatd/builddir/seatd "
         "arkrt/Graphics/GTK3/Graphics/weston/builddir/frontend/weston "
         "builddir/initramfs_ext/system/bin/sh "
         "builddir/initramfs_ext/usr/local/lib/libweston-17/*.so "
         "2>/dev/null | grep -o '/[^ "
         "]*\\.so[^ ]*' | sort -u | xargs -I {} cp -Ln {} "
         "builddir/initramfs_ext/lib/ 2>/dev/null || true");

  system("find arkrt/Graphics/GTK3/Graphics/mesa/builddir -name \"*.so*\" -type f -exec cp -a {} builddir/initramfs_ext/lib/ \\; 2>/dev/null || true");
  system("find arkrt/Graphics/GTK3/Graphics/libdrm/builddir -name \"*.so*\" -type f -exec cp -a {} builddir/initramfs_ext/lib/ \\; 2>/dev/null || true");
  system("find arkrt/Graphics/GTK3/Graphics/libepoxy/builddir -name \"*.so*\" -type f -exec cp -a {} builddir/initramfs_ext/lib/ \\; 2>/dev/null || true");

  system("rm -rf builddir/initramfs_ext/lib64 && ln -sf lib builddir/initramfs_ext/lib64");
  system("mkdir -p builddir/initramfs_ext/usr/lib/swift/lib/swift builddir/initramfs_ext/usr/lib/gbm builddir/initramfs_ext/usr/lib/dri && "
         "ln -sf /lib builddir/initramfs_ext/usr/lib/swift/lib/swift/linux && "
         "ln -sf /lib/* builddir/initramfs_ext/usr/lib/ 2>/dev/null || true");
  system("ln -sf /lib/dri_gbm.so builddir/initramfs_ext/usr/lib/gbm/dri_gbm.so 2>/dev/null || true");
  system("ln -sf /lib/*_dri.so builddir/initramfs_ext/usr/lib/dri/ 2>/dev/null || true");
  system("cp -L /usr/lib/swift/lib/swift/linux/lib*.so "
         "builddir/initramfs_ext/lib/ 2>/dev/null || cp -L "
         "/usr/lib/swift/linux/lib*.so builddir/initramfs_ext/lib/ 2>/dev/null "
         "|| true");

  // Copy Roboto Bold font for the GTK3 display app
  system("mkdir -p builddir/initramfs_ext/usr/share/fonts/TTF && "
         "cp /usr/share/fonts/TTF/Roboto-Bold.ttf "
         "builddir/initramfs_ext/usr/share/fonts/TTF/ 2>/dev/null || true");

  // Minimal fontconfig so GTK3/Pango can find the font at runtime
  system("mkdir -p builddir/initramfs_ext/etc/fonts && "
         "echo '<?xml version=\"1.0\"?>\n"
         "<fontconfig>\n"
         "  <dir>/usr/share/fonts</dir>\n"
         "  <cachedir>/tmp</cachedir>\n"
         "</fontconfig>' > builddir/initramfs_ext/etc/fonts/fonts.conf");

  // Copy GLib schemas and XKB configurations for GTK3 initialization
  system("mkdir -p builddir/initramfs_ext/usr/share/glib-2.0 && "
         "cp -r /usr/share/glib-2.0/schemas builddir/initramfs_ext/usr/share/glib-2.0/ 2>/dev/null || true");
  
  system("mkdir -p builddir/initramfs_ext/usr/share/X11 && "
         "cp -r /usr/share/X11/xkb builddir/initramfs_ext/usr/share/X11/ 2>/dev/null || true");

  system("cp -r builddir/system/services/* "
         "builddir/initramfs_ext/system/services/ 2>/dev/null || true");

  system("cp builddir/system/bin/static_test builddir/initramfs_ext/system/bin/ 2>/dev/null || true");

  system("cd builddir/initramfs_ext && find . | cpio -H newc -o 2>/dev/null | "
         "gzip > ../../finished/initramfs.img");

  system("dd if=/dev/zero of=finished/sys.img bs=1M count=2048 2>/dev/null");
  system("mkfs.ext4 -d builddir/system finished/sys.img 2>/dev/null");

  system("dd if=/dev/zero of=finished/vend.img bs=1M count=100 2>/dev/null");
  system("mkfs.ext4 -F -d builddir/vendor finished/vend.img 2>/dev/null || "
         "mkfs.ext4 -F finished/vend.img 2>/dev/null");

  if (!is_arm64) {
    pack_boot_x86_64();
  } else {
    pack_boot_arm64();
  }

  printf("Build assembly complete!\n");
}
