/**
 ** This file is part of the durchblick project.
 ** Copyright 2022 univrsal <uni@vrsal.xyz>.
 **
 ** This program is free software: you can redistribute it and/or modify
 ** it under the terms of the GNU General Public License as published by
 ** the Free Software Foundation, either version 3 of the License, or
 ** (at your option) any later version.
 **
 ** This program is distributed in the hope that it will be useful,
 ** but WITHOUT ANY WARRANTY; without even the implied warranty of
 ** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 ** GNU General Public License for more details.
 **
 ** You should have received a copy of the GNU General Public License
 ** along with this program.  If not, see <http://www.gnu.org/licenses/>.
 **/

#include "mixer_renderer.hpp"
#include "../items/audio_mixer.hpp"
#include "../items/source_item.hpp"
#include <obs-frontend-api.h>

static void fader_update(void* data, float db)
{
    static_cast<MixerSlider*>(data)->set_db(db);
}

MixerSlider::MixerSlider(OBSSource src, int x, int y, int height, int channel_width)
    : MixerMeter(src, x, y, height, channel_width)
{
}

MixerSlider::~MixerSlider()
{
    obs_fader_remove_callback(m_fader, fader_update, this);
    obs_fader_destroy(m_fader);
}

void MixerSlider::Render(float cell_scale, float source_scale_x, float source_scale_y)
{
    MixerMeter::Render(cell_scale, source_scale_x, source_scale_y);

    gs_matrix_push();
    gs_matrix_translate3f(m_x - 2, m_y - 3, 0.0f);
    gs_matrix_rotaa4f(0, 0, 1, RAD(90));
    obs_source_video_render(m_label);
    gs_matrix_pop();

    const int handle_width = 24;
    const int handle_height = 8;
    const int slider_width = m_channel_width * 1.5;
    const int mute_dim = get_width();
    const int on_length = (m_height - handle_height) * slider_position();

    // Slider line
    gs_matrix_push();
    gs_matrix_translate3f(m_x + get_width() + 15 - slider_width / 2, m_y, 0.0f);
    draw_rectangle(0, on_length, slider_width, m_height - on_length, ARGB32(255, 42, 130, 218));
    draw_rectangle(0, 0, slider_width, on_length, ARGB32(255, 100, 100, 100));
    gs_matrix_pop();

    // Slider position
    gs_matrix_push();
    gs_matrix_translate3f(m_x + get_width() + 15 - handle_width / 2, m_y + on_length, 0.0f);
    draw_rectangle(0, 0, handle_width, handle_height, ARGB32(255, 210, 210, 210));
    gs_matrix_pop();

    // mute/unmute
    draw_rectangle(m_x, m_y + m_height + mute_dim, mute_dim, mute_dim, m_muted ? ARGB32(255, 100, 100, 100) : m_foreground_nominal_color);
}

void MixerSlider::set_source(OBSSource src)
{
    MixerMeter::set_source(src);
    auto name = std::string(obs_source_get_name(src));

    if (name.length() > 30)
        name = name.substr(0, 27) + "...";
    m_label = CreateLabel(name.c_str(), 140, 1);

    obs_fader_detach_source(m_fader);
    obs_fader_attach_source(m_fader, m_source);
    set_db(obs_fader_get_db(m_fader));
}

void MixerSlider::set_type(obs_fader_type t)
{
    MixerMeter::set_type(t);

    obs_fader_remove_callback(m_fader, fader_update, this);
    obs_fader_destroy(m_fader);
    m_fader = obs_fader_create(t);
    obs_fader_add_callback(m_fader, fader_update, this);
}

void MixerSlider::MouseEvent(const LayoutItem::MouseData& e, const DurchblickItemConfig&, uint32_t mx, uint32_t my)
{
    if (e.buttons & Qt::LeftButton) {
        if (mouse_over_slider(mx, my)) {
            if (!m_dragging_volume)
                m_dragging_volume = true;
        }

        if (m_dragging_volume) {
            auto fade = qBound(0.f, float(qMax(m_y, int(my)) - m_y) / m_height, 1.f);
            obs_fader_set_deflection(m_fader, 1 - fade);
            set_db(obs_fader_get_db(m_fader));
        }

        if (mouse_over_mute_area(mx, my) && e.type == QEvent::MouseButtonPress)
            m_lmb_down = true;

    } else {
        m_dragging_volume = false;
        if (!mouse_over_mute_area(mx, my))
            m_lmb_down = false;
    }

    if (e.type == QEvent::MouseButtonRelease && m_lmb_down) {
        obs_source_set_muted(m_source, !m_muted);
        m_lmb_down = false;
    }
}

AudioMixerRenderer::AudioMixerRenderer(AudioMixerItem* parent, int height, int channel_width)
    : m_height(height)
    , m_channel_width(channel_width)
    , m_parent(parent)
{
    UpdateSources();
}

void AudioMixerRenderer::UpdateSources()
{
    m_sliders.clear();
    OBSSourceAutoRelease sc = obs_frontend_get_current_scene();
    std::vector<OBSSource> active_audio_srcs;
    obs_enum_sources(
        [](void* param, obs_source_t* src) {
            uint32_t flags = obs_source_get_output_flags(src);

            if ((flags & OBS_SOURCE_AUDIO) != 0 && obs_source_active(src)) {
                auto* srcs = (std::vector<OBSSource>*)param;
                if (obs_source_audio_active(src)) {
                    srcs->emplace_back(src);
                }
            }
            return true;
        },
        &active_audio_srcs);

    int x = 35;
    for (auto& src : active_audio_srcs) {
        auto* slider = new MixerSlider(src, x, 0, m_height, m_channel_width);
        slider->set_type(OBS_FADER_LOG);
        slider->set_source(src);
        m_sliders.emplace_back(slider);
        x += (m_channel_width * slider->get_width()) * 2.5;
    }
}

void AudioMixerRenderer::Render(float cell_scale, float source_scale_x, float source_scale_y)
{
    for (auto& slider : m_sliders)
        slider->Render(cell_scale, source_scale_x, source_scale_y);
}

void AudioMixerRenderer::Update(const DurchblickItemConfig&)
{
    auto h = m_parent->Height() * 0.8;
    auto y = (m_parent->Height() * .2) / 2;
    for (auto& slider : m_sliders) {
        slider->set_y(y);
        slider->set_height(h);
    }
}

void AudioMixerRenderer::MouseEvent(const LayoutItem::MouseData& e, const DurchblickItemConfig& cfg)
{
    for (auto& slider : m_sliders)
        slider->MouseEvent(e, cfg, m_parent->MouseX(), m_parent->MouseY());
}
