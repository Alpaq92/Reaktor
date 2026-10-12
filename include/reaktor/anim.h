#ifndef REAKTOR_ANIM_PUBLIC_H
#define REAKTOR_ANIM_PUBLIC_H

#include "reaktor/nuklear.h"
#include <nukanim.h>

#define reaktor_anim_context                   nka_context
#define reaktor_anim_ease                      nka_ease
#define reaktor_anim_noise_opts                nka_noise_opts
#define reaktor_anim_stagger_grid_opts         nka_stagger_grid_opts
#define reaktor_anim_text_path_opts            nka_text_path_opts
#define reaktor_anim_text_stagger_opts         nka_text_stagger_opts
#define REAKTOR_ANIM_COL_HSV                   NKA_COL_HSV
#define REAKTOR_ANIM_COL_OKLAB                 NKA_COL_OKLAB
#define REAKTOR_ANIM_COL_OKLCH                 NKA_COL_OKLCH
#define REAKTOR_ANIM_COL_SRGB                  NKA_COL_SRGB
#define REAKTOR_ANIM_COL_SRGB_LINEAR           NKA_COL_SRGB_LINEAR
#define REAKTOR_ANIM_EASE_IN_OUT_SINE          NKA_EASE_IN_OUT_SINE
#define REAKTOR_ANIM_EASE_LINEAR               NKA_EASE_LINEAR
#define REAKTOR_ANIM_EASE_OUT_BACK             NKA_EASE_OUT_BACK
#define REAKTOR_ANIM_EASE_OUT_CUBIC            NKA_EASE_OUT_CUBIC
#define REAKTOR_ANIM_POLICY_CROSSFADE          NKA_POLICY_CROSSFADE
#define REAKTOR_ANIM_STAGGER_CENTER            NKA_STAGGER_CENTER
#define REAKTOR_ANIM_TEXT_ALIGN_CENTER         NKA_TEXT_ALIGN_CENTER
#define REAKTOR_ANIM_TEXT_FX_BOUNCE            NKA_TEXT_FX_BOUNCE
#define REAKTOR_ANIM_TEXT_FX_FADE              NKA_TEXT_FX_FADE
#define REAKTOR_ANIM_TEXT_FX_SCALE             NKA_TEXT_FX_SCALE
#define REAKTOR_ANIM_TEXT_FX_SLIDE_UP          NKA_TEXT_FX_SLIDE_UP
#define REAKTOR_ANIM_TEXT_FX_TYPEWRITER        NKA_TEXT_FX_TYPEWRITER
#define REAKTOR_ANIM_WAVE_SINE                 NKA_WAVE_SINE
#define reaktor_anim_animate                   nka_animate
#define reaktor_anim_clip_begin                nka_clip_begin
#define reaktor_anim_clip_duration             nka_clip_duration
#define reaktor_anim_clip_end                  nka_clip_end
#define reaktor_anim_clip_exists               nka_clip_exists
#define reaktor_anim_clip_key_float            nka_clip_key_float
#define reaktor_anim_clip_key_vec2             nka_clip_key_vec2
#define reaktor_anim_color_blend               nka_color_blend
#define reaktor_anim_ease_bezier               nka_ease_bezier
#define reaktor_anim_ease_spring               nka_ease_spring
#define reaktor_anim_ease_steps                nka_ease_steps
#define reaktor_anim_id                        nka_id
#define reaktor_anim_id_mix                    nka_id_mix
#define reaktor_anim_instance_get_float        nka_instance_get_float
#define reaktor_anim_instance_get_vec2         nka_instance_get_vec2
#define reaktor_anim_instance_seek             nka_instance_seek
#define reaktor_anim_instance_valid            nka_instance_valid
#define reaktor_anim_noise_channel_vec2        nka_noise_channel_vec2
#define reaktor_anim_noise_opts_default        nka_noise_opts_default
#define reaktor_anim_oscillate                 nka_oscillate
#define reaktor_anim_path_angle                nka_path_angle
#define reaktor_anim_path_begin                nka_path_begin
#define reaktor_anim_path_build_arc_lut        nka_path_build_arc_lut
#define reaktor_anim_path_cubic_to             nka_path_cubic_to
#define reaktor_anim_path_end                  nka_path_end
#define reaktor_anim_path_evaluate             nka_path_evaluate
#define reaktor_anim_path_evaluate_at_distance nka_path_evaluate_at_distance
#define reaktor_anim_path_exists               nka_path_exists
#define reaktor_anim_path_length               nka_path_length
#define reaktor_anim_path_line_to              nka_path_line_to
#define reaktor_anim_path_quadratic_to         nka_path_quadratic_to
#define reaktor_anim_play                      nka_play
#define reaktor_anim_play_with_delay           nka_play_with_delay
#define reaktor_anim_shake                     nka_shake
#define reaktor_anim_show_debug_timeline       nka_show_debug_timeline
#define reaktor_anim_stagger_grid_delay_index  nka_stagger_grid_delay_index
#define reaktor_anim_stagger_grid_opts_default nka_stagger_grid_opts_default
#define reaktor_anim_text_path_animated        nka_text_path_animated
#define reaktor_anim_text_path_opts_default    nka_text_path_opts_default
#define reaktor_anim_text_stagger              nka_text_stagger
#define reaktor_anim_text_stagger_duration     nka_text_stagger_duration
#define reaktor_anim_text_stagger_opts_default nka_text_stagger_opts_default
#define reaktor_anim_time                      nka_time
#define reaktor_anim_trigger_shake             nka_trigger_shake
#define reaktor_anim_tween_color               nka_tween_color
#define reaktor_anim_tween_float               nka_tween_float
#define reaktor_anim_tween_path                nka_tween_path
#define reaktor_anim_tween_path_angle          nka_tween_path_angle
#define reaktor_anim_wiggle_vec2               nka_wiggle_vec2

#ifndef REAKTOR_APP_FWD
#define REAKTOR_APP_FWD
typedef struct App App;
#endif

enum {
    REAKTOR_EASE_LINEAR = 0,

    REAKTOR_EASE_QUAD_IN,   REAKTOR_EASE_QUAD_OUT,   REAKTOR_EASE_QUAD_IN_OUT,
    REAKTOR_EASE_CUBIC_IN,  REAKTOR_EASE_CUBIC_OUT,  REAKTOR_EASE_CUBIC_IN_OUT,
    REAKTOR_EASE_QUART_IN,  REAKTOR_EASE_QUART_OUT,  REAKTOR_EASE_QUART_IN_OUT,
    REAKTOR_EASE_QUINT_IN,  REAKTOR_EASE_QUINT_OUT,  REAKTOR_EASE_QUINT_IN_OUT,

    REAKTOR_EASE_SINE_IN,   REAKTOR_EASE_SINE_OUT,   REAKTOR_EASE_SINE_IN_OUT,
    REAKTOR_EASE_EXPO_IN,   REAKTOR_EASE_EXPO_OUT,   REAKTOR_EASE_EXPO_IN_OUT,
    REAKTOR_EASE_CIRC_IN,   REAKTOR_EASE_CIRC_OUT,   REAKTOR_EASE_CIRC_IN_OUT,

    REAKTOR_EASE_BACK_IN,   REAKTOR_EASE_BACK_OUT,   REAKTOR_EASE_BACK_IN_OUT,
    REAKTOR_EASE_ELASTIC_IN, REAKTOR_EASE_ELASTIC_OUT,
    REAKTOR_EASE_ELASTIC_IN_OUT,
    REAKTOR_EASE_BOUNCE_IN, REAKTOR_EASE_BOUNCE_OUT,
    REAKTOR_EASE_BOUNCE_IN_OUT,

    REAKTOR_EASE_COUNT
};

float reaktor_ease_at(unsigned char curve, float t);
const char *reaktor_ease_name(unsigned char curve);

float reaktor_animate(unsigned id, unsigned channel, float to,
                      float ms, unsigned char curve);

float reaktor_anim_progress(unsigned id, unsigned channel);

struct reaktor_anim_context *reaktor_anim(App *app);

#endif
