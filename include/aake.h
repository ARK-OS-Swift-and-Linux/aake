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
