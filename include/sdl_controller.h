#ifndef CODBOZ_SDL_CONTROLLER_H
#define CODBOZ_SDL_CONTROLLER_H

#include <stdint.h>

int sdl_controller_connected(void);
void sdl_controller_update(uint64_t now_ms);
int sdl_controller_button(int button);
int32_t sdl_controller_axis(int axis);
uint8_t sdl_controller_hat_mask(void);
void sdl_controller_shutdown(void);

#endif
