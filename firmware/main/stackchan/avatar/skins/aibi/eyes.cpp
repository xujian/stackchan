/*
 * SPDX-License-Identifier: MIT
 */
#include "aibi.h"
#include <cmath>

using namespace uitk;
using namespace uitk::lvgl_cpp;
using namespace stackchan::avatar;


// Tuning knobs (pixels, 320x240 screen)
static const Vector2i _eye_pos        = Vector2i(-60, 0);  // left eye center, right eye is mirrored
static const Vector2i _eye_min_offset = Vector2i(-20, -16);
static const Vector2i _eye_max_offset = Vector2i(20, 16);
static const Vector2i _eye_height     = Vector2i(40, 84);  // eye height at size -100 / 100, 64 at size 0
static const int _eye_aspect_x16      = 9;                 // width = height * 9 / 16

// Closed eye (weight 0): a thin wide line, used by blink and sleep
static const int _closed_height    = 8;
static const int _closed_width_pct = 150;

// Happy: a thick arc with round caps, like "∩"
static const int _arc_radius_pct = 83;  // center line radius, % of eye width, bigger = flatter
static const int _arc_width_pct  = 40;  // thickness, % of eye width
static const int _arc_sweep_deg  = 55;  // degrees on each side of the top, bigger = longer arc

// Angry / sad: top edge is cut at a slant
static const int _slant_depth_pct = 30;  // cut line at the eye center, % of height from the top

// Sad: the eye also droops and gets thinner, so it doesn't read as a mirrored angry
static const int _sad_drop      = 8;   // pixels down
static const int _sad_width_pct = 85;

// Doubt: one eye stays open, the other squints (right eye on screen)
static const int _doubt_squint_weight = 45;

// Surprised: bigger and rounder, popping up a little
static const int _surprised_w_pct = 125;
static const int _surprised_h_pct = 112;
static const int _surprised_rise  = 4;

// Thinking: both eyes drift up and to the right of the screen, flatter, the right one squints more
static const Vector2i _thinking_offset   = Vector2i(6, -8);
static const int _thinking_weight_left  = 80;
static const int _thinking_weight_right = 65;

// Shape transitions (blink, emotion change), driven by lv_anim so they stay smooth
// even when the stackchan update task slows down while xiaozhi is talking
static const uint32_t _anim_ms = 120;

// Dizzy: Archimedean spiral (radius grows evenly with angle), recomputed each frame to spin
static const int _spiral_inner_r      = 3;
static const int _spiral_outer_r      = 27;
static const float _spiral_turns      = 2.25f;
static const int _spiral_line_w       = 4;
static const int _spiral_size         = _spiral_outer_r * 2 + _spiral_line_w + 2;
static const uint32_t _spiral_turn_ms = 1200;  // one full turn

static const int _margin      = 12;  // room for surprised and thinking to grow and move
static const int _max_eye_w   = _eye_height.y * _eye_aspect_x16 / 16 * _closed_width_pct / 100;
static const int _container_w = _max_eye_w * 2 + _margin * 2;  // the happy arc is wider than the eye
static const int _container_h = _eye_height.y + _margin * 2;

AibiEyes::AibiEyes(lv_obj_t* parent, lv_color_t primaryColor, lv_color_t secondaryColor, bool isLeftEye)
{
    _is_left_eye = isLeftEye;

    // Clips the lid, so it never covers the speech bubble
    _container = std::make_unique<Container>(parent);
    _container->setRadius(0);
    _container->setAlign(LV_ALIGN_CENTER);
    _container->setBorderWidth(0);
    _container->setBgOpa(0);
    _container->removeFlag(LV_OBJ_FLAG_SCROLLABLE);
    _container->setPadding(0, 0, 0, 0);
    _container->setSize(_container_w, _container_h);

    _eye = std::make_unique<Container>(_container->get());
    _eye->setRadius(LV_RADIUS_CIRCLE);
    _eye->align(LV_ALIGN_CENTER, 0, 0);
    _eye->setBorderWidth(0);
    _eye->setBgColor(primaryColor);
    _eye->removeFlag(LV_OBJ_FLAG_SCROLLABLE);

    // Only the indicator part is drawn: no background arc, no knob, not clickable
    _arc = lv_arc_create(_container->get());
    lv_obj_remove_style(_arc, nullptr, LV_PART_KNOB);
    lv_obj_remove_flag(_arc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_opa(_arc, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_arc_opa(_arc, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_pad_all(_arc, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(_arc, 0, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(_arc, primaryColor, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(_arc, true, LV_PART_INDICATOR);
    lv_arc_set_rotation(_arc, 0);
    lv_obj_align(_arc, LV_ALIGN_CENTER, 0, 0);

    _spiral = lv_line_create(_container->get());
    lv_obj_set_size(_spiral, _spiral_size, _spiral_size);
    lv_obj_align(_spiral, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_line_width(_spiral, _spiral_line_w, 0);
    lv_obj_set_style_line_color(_spiral, primaryColor, 0);
    lv_obj_set_style_line_rounded(_spiral, true, 0);
    draw_spiral(0);

    // Wide enough to still cover the eye when tilted, pivot on the bottom edge center (the cut line)
    _lid = std::make_unique<Container>(_container->get());
    _lid->setRadius(0);
    _lid->align(LV_ALIGN_CENTER, 0, 0);
    _lid->setBorderWidth(0);
    _lid->setBgColor(secondaryColor);
    _lid->removeFlag(LV_OBJ_FLAG_SCROLLABLE);
    _lid->setSize(_container_w * 2, _container_h);
    _lid->setTransformPivot(_container_w, _container_h);

    setSize(0);
    setWeight(100);
    setPosition(_position);
    setRotation(0);
    _animate = true;
}

AibiEyes::~AibiEyes()
{
    // A running animation would otherwise touch the objects below after they're gone
    stop_spin();
    lv_anim_delete(this, nullptr);
    _lid.reset();
    _eye.reset();
    _container.reset();  // also deletes _arc and _spiral
    _arc    = nullptr;
    _spiral = nullptr;
}

void AibiEyes::setPosition(const Vector2i& position)
{
    Element::setPosition(position);

    auto pos_x = _is_left_eye ? _eye_pos.x : -_eye_pos.x;
    pos_x += map_range(_position.x, -100, 100, _eye_min_offset.x, _eye_max_offset.x);
    auto pos_y = _eye_pos.y + map_range(_position.y, -100, 100, _eye_min_offset.y, _eye_max_offset.y);

    _container->setPos(pos_x, pos_y);
}

void AibiEyes::setWeight(int weight)
{
    Feature::setWeight(weight);
    update_shape();
}

void AibiEyes::setRotation(int rotation)
{
    // Callers pass negative angles (e.g. Sad), Element clamps to 0~3600, so wrap first
    Element::setRotation(((rotation % 3600) + 3600) % 3600);
    update_shape();
}

void AibiEyes::setSize(int size)
{
    Feature::setSize(size);
    update_shape();
}

void AibiEyes::update_shape()
{
    int base_h = map_range(_size, -100, 100, _eye_height.x, _eye_height.y);
    int base_w = base_h * _eye_aspect_x16 / 16;
    if (_surprised) {
        base_w = base_w * _surprised_w_pct / 100;
        base_h = base_h * _surprised_h_pct / 100;
    }

    // Back to -1800~1800, sign = tilt direction. |rotation| > 900 means happy, same as DefaultEyes
    const int rotation = _rotation > 1800 ? _rotation - 3600 : _rotation;
    const bool happy   = rotation > 900 || rotation < -900;
    const bool slanted = !happy && rotation != 0;
    const bool sad     = slanted && rotation < 0;

    Shape target;

    // Happy arc, closing (blink) makes it thinner
    target.arcRadius = base_w * _arc_radius_pct / 100;
    target.arcWidth  = map_range(_weight, 0, 100, 4, base_w * _arc_width_pct / 100);
    target.arcSweep  = happy ? _arc_sweep_deg : 1;  // grows out from the top when turning happy
    target.arcOpa    = happy ? LV_OPA_COVER : LV_OPA_TRANSP;
    target.spiralOpa = _dizzy ? LV_OPA_COVER : LV_OPA_TRANSP;

    if (happy) {
        // Capsule flattens into the arc's outline while fading out
        const float sweep = _arc_sweep_deg * 3.14159f / 180.0f;
        target.eyeW       = (int)(2 * target.arcRadius * std::sin(sweep)) + target.arcWidth;
        target.eyeH       = target.arcWidth;
        target.eyeX       = 0;
        target.eyeY       = 0;
        target.eyeOpa     = LV_OPA_TRANSP;
    } else {
        // Squash and stretch: closing the eye makes it flatter and wider instead of covering it
        const int open_w = sad ? base_w * _sad_width_pct / 100 : base_w;
        target.eyeW      = map_range(_weight, 0, 100, open_w * _closed_width_pct / 100, open_w);
        target.eyeH      = map_range(_weight, 0, 100, _closed_height, base_h);
        target.eyeX   = _thinking ? _thinking_offset.x : 0;
        target.eyeY   = sad ? _sad_drop : _surprised ? -_surprised_rise : _thinking ? _thinking_offset.y : 0;
        target.eyeOpa = _dizzy ? LV_OPA_TRANSP : LV_OPA_COVER;
    }

    // Unused lid is parked above the container instead of hidden, so it slides in rather than pops.
    // Mirror the tilt for the right eye so both inner corners move together
    const int lid_bottom =
        slanted ? target.eyeY - target.eyeH / 2 + target.eyeH * _slant_depth_pct / 100 : -_container_h / 2;
    target.lidY          = lid_bottom - _container_h / 2;
    target.lidRotation   = slanted ? (_is_left_eye ? rotation : -rotation) : 0;

    _to = target;
    lv_anim_delete(this, nullptr);

    if (!_animate) {
        apply_shape(_to);
        return;
    }

    // Start from what's on screen now, so a new target mid-animation doesn't jump
    _from = _shown;

    lv_anim_t anim;
    lv_anim_init(&anim);
    lv_anim_set_var(&anim, this);  // lets lv_anim_delete(this, ...) find it
    lv_anim_set_user_data(&anim, this);
    lv_anim_set_values(&anim, 0, 1024);
    lv_anim_set_duration(&anim, _anim_ms);
    lv_anim_set_path_cb(&anim, lv_anim_path_ease_in_out);
    lv_anim_set_custom_exec_cb(&anim, anim_exec_cb);
    lv_anim_start(&anim);
}

void AibiEyes::anim_exec_cb(lv_anim_t* anim, int32_t progress)
{
    auto* self    = static_cast<AibiEyes*>(lv_anim_get_user_data(anim));
    const auto& a = self->_from;
    const auto& b = self->_to;
    auto lerp     = [progress](int from, int to) { return from + (to - from) * progress / 1024; };

    self->apply_shape({
        lerp(a.eyeW, b.eyeW),
        lerp(a.eyeH, b.eyeH),
        lerp(a.eyeX, b.eyeX),
        lerp(a.eyeY, b.eyeY),
        lerp(a.eyeOpa, b.eyeOpa),
        lerp(a.arcRadius, b.arcRadius),
        lerp(a.arcWidth, b.arcWidth),
        lerp(a.arcSweep, b.arcSweep),
        lerp(a.arcOpa, b.arcOpa),
        lerp(a.spiralOpa, b.spiralOpa),
        lerp(a.lidY, b.lidY),
        lerp(a.lidRotation, b.lidRotation),
    });
}

void AibiEyes::apply_shape(const Shape& shape)
{
    _eye->setSize(shape.eyeW, shape.eyeH);
    _eye->setPos(shape.eyeX, shape.eyeY);
    _eye->setBgOpa(shape.eyeOpa);

    // Keep the arc's visible part vertically centered on the eye, using the full happy sweep
    // so the arc doesn't bob up and down while it grows
    static const float full_sweep = _arc_sweep_deg * 3.14159f / 180.0f;
    const int arc_size            = shape.arcRadius * 2 + shape.arcWidth;
    const int arc_y               = (int)(shape.arcRadius * (1 + std::cos(full_sweep)) / 2);
    lv_obj_set_size(_arc, arc_size, arc_size);
    lv_obj_set_y(_arc, arc_y);
    lv_obj_set_style_arc_width(_arc, shape.arcWidth, LV_PART_INDICATOR);
    lv_arc_set_angles(_arc, 270 - shape.arcSweep, 270 + shape.arcSweep);  // 270 = top
    lv_obj_set_style_opa(_arc, shape.arcOpa, LV_PART_MAIN);
    lv_obj_set_style_opa(_spiral, shape.spiralOpa, 0);

    _lid->setY(shape.lidY);
    _lid->setRotation(shape.lidRotation);
    _shown = shape;
}

void AibiEyes::setEmotion(const Emotion& emotion)
{
    if (getIgnoreEmotion()) {
        return;
    }

    _dizzy     = emotion == Emotion::Dizzy;
    _surprised = emotion == Emotion::Surprised;
    _thinking  = emotion == Emotion::Thinking;
    if (_dizzy) {
        start_spin();
    } else {
        stop_spin();
    }

    auto apply_style = [this](int weight, int rotation) {
        Feature::setWeight(weight);
        setRotation(rotation);
    };

    // Positive rotation = inner corner down (angry), negative = outer corner down (sad)
    switch (emotion) {
        case Emotion::Neutral:
            apply_style(100, 0);
            break;
        case Emotion::Happy:
            apply_style(100, 1800);
            break;
        case Emotion::Angry:
            apply_style(100, 250);
            break;
        case Emotion::Sad:
            apply_style(100, -250);
            break;
        case Emotion::Doubt:
            apply_style(_is_left_eye ? 100 : _doubt_squint_weight, 0);
            break;
        case Emotion::Sleepy:
            apply_style(0, 0);
            break;
        case Emotion::Dizzy:
            apply_style(100, 0);
            break;
        case Emotion::Surprised:
            apply_style(100, 0);
            break;
        case Emotion::Thinking:
            apply_style(_is_left_eye ? _thinking_weight_left : _thinking_weight_right, 0);
            break;
        default:
            break;
    }
}

void AibiEyes::setVisible(bool visible)
{
    Element::setVisible(visible);

    _container->setHidden(!visible);
}

void AibiEyes::start_spin()
{
    lv_anim_delete(_spiral, nullptr);

    lv_anim_t anim;
    lv_anim_init(&anim);
    lv_anim_set_var(&anim, _spiral);
    lv_anim_set_values(&anim, 0, 3600);
    lv_anim_set_duration(&anim, _spiral_turn_ms);
    lv_anim_set_repeat_count(&anim, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_user_data(&anim, this);
    lv_anim_set_custom_exec_cb(&anim, spin_exec_cb);
    lv_anim_start(&anim);
}

void AibiEyes::stop_spin()
{
    lv_anim_delete(_spiral, nullptr);
}

void AibiEyes::spin_exec_cb(lv_anim_t* anim, int32_t rotation)
{
    static_cast<AibiEyes*>(lv_anim_get_user_data(anim))->draw_spiral(rotation);
}

void AibiEyes::draw_spiral(int rotation)
{
    const float phase  = rotation * 3.14159f / 1800.0f;
    const float mirror = _is_left_eye ? 1.0f : -1.0f;  // right eye is a mirror image, spinning the other way
    const int center   = _spiral_size / 2;

    for (int i = 0; i < _spiral_points; i++) {
        const float t     = (float)i / (_spiral_points - 1);
        const float angle = t * _spiral_turns * 2 * 3.14159f + phase;
        const float r     = _spiral_inner_r + (_spiral_outer_r - _spiral_inner_r) * t;
        _spiral_pts[i].x  = center + (int)lroundf(mirror * r * std::cos(angle));
        _spiral_pts[i].y  = center + (int)lroundf(r * std::sin(angle));
    }
    lv_line_set_points(_spiral, _spiral_pts.data(), _spiral_points);  // also invalidates the object
}
