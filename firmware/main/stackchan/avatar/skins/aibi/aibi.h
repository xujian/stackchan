/*
 * SPDX-License-Identifier: MIT
 *
 * AIBI-style skin: two mint capsule eyes that change shape, no mouth.
 */
#pragma once
#include "../../avatar/avatar.h"
#include "../../avatar/decorator.h"
#include "../../avatar/elements/feature.h"
#include "../default/default.h"
#include <lvgl.h>
#include <smooth_lvgl.hpp>
#include <array>
#include <memory>

namespace stackchan::avatar {

/**
 * @brief AIBI-style avatar, reuses DefaultSpeechBubble
 *
 */
class AibiAvatar : public Avatar {
public:
    lv_color_t primaryColor   = lv_color_hex(0x3DE8A0);
    lv_color_t secondaryColor = lv_color_black();

    void init(lv_obj_t* parent, const lv_font_t* font = &lv_font_montserrat_16);
    uitk::lvgl_cpp::Container* getPanel() const;

private:
    std::unique_ptr<uitk::lvgl_cpp::Container> _pannel;
};

/**
 * @brief Capsule eye that changes shape instead of being covered by eyelids.
 * Weight = how open the eye is: 100 is the capsule, 0 squashes it into a thin wide line (blink, sleep).
 * Rotation slants the top edge (angry / sad); |rotation| > 900 turns it into a happy arc (a thick
 * round-capped arc, the capsule cross-fades into it),
 * matching how DefaultEyes uses a flipped eyelid for Happy.
 *
 */
class AibiEyes : public Feature {
public:
    AibiEyes(lv_obj_t* parent, lv_color_t primaryColor, lv_color_t secondaryColor, bool isLeftEye);
    ~AibiEyes();

    void setPosition(const uitk::Vector2i& position) override;
    void setWeight(int weight) override;
    void setRotation(int rotation) override;
    void setEmotion(const Emotion& emotion) override;
    void setVisible(bool visible) override;
    void setSize(int size) override;

private:
    // Everything that defines how the eye looks, so it can be tweened as one
    struct Shape {
        int eyeW, eyeH, eyeOpa;
        int arcRadius, arcWidth, arcSweep, arcOpa;  // arcRadius = center line, arcSweep = degrees each side
        int spiralOpa;
        int lidY, lidRotation;
    };

    bool _is_left_eye = false;
    bool _animate     = false;  // off while constructing, so the first shape snaps into place
    Shape _from{};
    Shape _to{};
    Shape _shown{};

    void update_shape();
    void apply_shape(const Shape& shape);
    static void anim_exec_cb(lv_anim_t* anim, int32_t progress);

    // Dizzy: the capsule cross-fades into a spinning spiral, drawn as a smooth polyline
    static constexpr int _spiral_points = 64;
    lv_obj_t* _spiral = nullptr;  // lv_line, owned (and deleted) by _container
    std::array<lv_point_precise_t, _spiral_points> _spiral_pts{};
    bool _dizzy = false;

    void draw_spiral(int rotation);
    void start_spin();
    void stop_spin();
    static void spin_exec_cb(lv_anim_t* anim, int32_t rotation);

    std::unique_ptr<uitk::lvgl_cpp::Container> _container;
    std::unique_ptr<uitk::lvgl_cpp::Container> _eye;
    lv_obj_t* _arc = nullptr;  // happy eye, owned (and deleted) by _container
    std::unique_ptr<uitk::lvgl_cpp::Container> _lid;     // black slanted cut for angry / sad
};

/**
 * @brief Stream of hearts floating from above the eyes to the top right corner while the head is petted.
 * Lives as long as the avatar, listens to the head pet gesture itself, so in-flight hearts are never cut off.
 *
 */
class AibiPetHeartsDecorator : public Decorator {
public:
    AibiPetHeartsDecorator(lv_obj_t* parent);
    ~AibiPetHeartsDecorator();

    void _update() override;

private:
    struct Heart {
        lv_obj_t* obj = nullptr;  // lv_image, owned by the parent
        int x0 = 0, y0 = 0;       // start, relative to the screen center
        int x1 = 0, y1 = 0;       // end
        int sway = 0;             // sideways wobble amplitude, signed
    };
    static constexpr int _heart_count = 8;

    std::array<Heart, _heart_count> _hearts{};
    int _next_heart        = 0;
    size_t _signal_id      = 0;
    volatile bool _petted  = false;  // set from the touch task, consumed in _update
    uint32_t _emit_until   = 0;
    uint32_t _last_emit    = 0;

    void emit_heart();
    static void heart_anim_cb(lv_anim_t* anim, int32_t progress);
};

/**
 * @brief AIBI has no mouth. Keeps the Feature interface so modifiers (speaking, imu...) still work.
 *
 */
class AibiMouth : public Feature {};

}  // namespace stackchan::avatar
