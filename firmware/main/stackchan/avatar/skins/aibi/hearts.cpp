/*
 * SPDX-License-Identifier: MIT
 */
#include "aibi.h"
#include <hal/hal.h>
#include <cmath>
#include <cstdlib>

using namespace stackchan::avatar;

// Reuse the heart bitmap from the original heart decorator (40x40), only ever scaled down
LV_IMAGE_DECLARE(decorator_heart);

static const lv_color_t _heart_color   = lv_color_hex(0xE13232);
static const uint32_t _emit_ms         = 220;   // one heart every
static const uint32_t _keep_emitting   = 1500;  // after the last stroke
static const uint32_t _heart_life_ms   = 1400;
static const int _start_y              = -50;   // just above the eyes
static const int _start_x_range        = 50;    // random within +-
static const int _end_x                = 145;   // top right corner, screen center is 0,0
static const int _end_y                = -105;
static const int _end_spread           = 20;    // random within +-
static const int _sway_px              = 10;
static const int _scale_start          = 128;   // 256 = 100%
static const int _scale_end            = 256;
static const int _fade_from            = 700;   // progress (0~1024) where fading starts
static const int _tilt_range           = 200;   // 0.1 degree, random within +-

static int rand_range(int lo, int hi)
{
    return lo + std::rand() % (hi - lo + 1);
}

AibiPetHeartsDecorator::AibiPetHeartsDecorator(lv_obj_t* parent)
{
    for (auto& heart : _hearts) {
        heart.obj = lv_image_create(parent);
        lv_image_set_src(heart.obj, &decorator_heart);
        lv_image_set_pivot(heart.obj, decorator_heart.header.w / 2, decorator_heart.header.h / 2);
        lv_obj_set_style_image_recolor(heart.obj, _heart_color, 0);
        lv_obj_set_style_image_recolor_opa(heart.obj, LV_OPA_COVER, 0);
        lv_obj_remove_flag(heart.obj, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_flag(heart.obj, LV_OBJ_FLAG_HIDDEN);
    }

    // Runs in the touch task, so only set a flag here
    _signal_id = GetHAL().onHeadPetGesture.connect([this](HeadPetGesture gesture) {
        if (gesture == HeadPetGesture::SwipeForward || gesture == HeadPetGesture::SwipeBackward) {
            _petted = true;
        }
    });
}

AibiPetHeartsDecorator::~AibiPetHeartsDecorator()
{
    GetHAL().onHeadPetGesture.disconnect(_signal_id);
    for (auto& heart : _hearts) {
        lv_anim_delete(&heart, nullptr);
    }
    // Heart objects belong to the parent and are deleted with it
}

void AibiPetHeartsDecorator::_update()
{
    const uint32_t now = lv_tick_get();

    if (_petted) {
        _petted     = false;
        _emit_until = now + _keep_emitting;
    }

    if ((int32_t)(_emit_until - now) > 0 && now - _last_emit >= _emit_ms) {
        _last_emit = now;
        emit_heart();
    }
}

void AibiPetHeartsDecorator::emit_heart()
{
    // Reuse the oldest heart
    auto& heart = _hearts[_next_heart];
    _next_heart = (_next_heart + 1) % _heart_count;
    lv_anim_delete(&heart, nullptr);

    heart.x0   = rand_range(-_start_x_range, _start_x_range);
    heart.y0   = _start_y;
    heart.x1   = _end_x + rand_range(-_end_spread, _end_spread);
    heart.y1   = _end_y + rand_range(-_end_spread, _end_spread);
    heart.sway = rand_range(0, 1) ? _sway_px : -_sway_px;

    lv_image_set_rotation(heart.obj, rand_range(-_tilt_range, _tilt_range));
    lv_obj_remove_flag(heart.obj, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(heart.obj);

    lv_anim_t anim;
    lv_anim_init(&anim);
    lv_anim_set_var(&anim, &heart);
    lv_anim_set_user_data(&anim, &heart);
    lv_anim_set_values(&anim, 0, 1024);
    lv_anim_set_duration(&anim, _heart_life_ms);
    lv_anim_set_path_cb(&anim, lv_anim_path_ease_out);
    lv_anim_set_custom_exec_cb(&anim, heart_anim_cb);
    lv_anim_start(&anim);
}

void AibiPetHeartsDecorator::heart_anim_cb(lv_anim_t* anim, int32_t progress)
{
    auto* heart = static_cast<Heart*>(lv_anim_get_user_data(anim));

    // Straight line to the corner, plus one sideways wobble perpendicular-ish to it
    const float wobble = std::sin(progress * 2 * 3.14159f / 1024) * heart->sway;
    const int x        = heart->x0 + (heart->x1 - heart->x0) * progress / 1024 + (int)wobble;
    const int y        = heart->y0 + (heart->y1 - heart->y0) * progress / 1024 + (int)(wobble / 2);
    lv_obj_align(heart->obj, LV_ALIGN_CENTER, x, y);

    lv_image_set_scale(heart->obj, _scale_start + (_scale_end - _scale_start) * progress / 1024);

    const int opa = progress < _fade_from ? 255 : 255 * (1024 - progress) / (1024 - _fade_from);
    lv_obj_set_style_opa(heart->obj, opa, 0);

    if (progress >= 1024) {
        lv_obj_add_flag(heart->obj, LV_OBJ_FLAG_HIDDEN);
    }
}
