#ifndef CODBOZ_FRAME_INTERPOLATION_H
#define CODBOZ_FRAME_INTERPOLATION_H

#include "s3e_image.h"

extern volatile uint32_t codboz_frame_fixed_ticks;
extern volatile uint32_t codboz_frame_camera_views;
extern volatile uint32_t codboz_frame_interpolated_views;
extern volatile uint32_t codboz_frame_history_advances;
extern volatile uint32_t codboz_frame_snap_views;
extern volatile uint32_t codboz_frame_passthrough_views;
extern volatile uint32_t codboz_frame_unsupported_step_views;
extern volatile uint32_t codboz_frame_initial_views;
extern volatile uint32_t codboz_frame_stale_views;
extern volatile uint32_t codboz_frame_cut_views;
extern volatile uint32_t codboz_frame_invalid_views;

bool codboz_install_frame_interpolation(struct s3e_loaded_image *loaded);

#endif
