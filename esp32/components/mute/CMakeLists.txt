# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

# Mute: the voice companion UI that runs beside Home Link on boards with a
# display (CONFIG_MUTE_BOARD_*). Link-only builds register an empty component,
# except the Voice PE, which borrows the Hatch voice session.
#
# Requirements are resolved before Kconfig is known, so everything any board
# needs is listed unconditionally. Managed components (LVGL, codecs, BSPs) come
# from idf_component.yml, where rules keep them out of Link-only builds.
set(srcs)
set(embed)
if(CONFIG_MUTE_ENABLED)
    list(APPEND srcs
        "mute_app.c"
        "mute_board.c"
        "mute_console.c"
        "mute_state.c"
        "mute_battery.c"
        "mute_ui.c"
        "mute_audio.c"
        "mute_pmu.c"
        "mute_input.c"
        "mute_menu.c"
        "mute_voice.c"
        "mute_adpcm.c"
        "mute_settings.c"
        "mute_settings_ui.c"
        "mute_text.c"
        "mute_keypad.c"
        "mute_lv_mem.c"
        "mute_link.c"
        "mute_ble.c")
    if(CONFIG_MUTE_HATCH)
        list(APPEND srcs "mute_chat.c" "mute_account_api.c" "mute_chat_session.cpp")
        list(APPEND embed "test_reply.mp3")   # mute_hatch_mp3_selftest
    else()
        list(APPEND srcs "mute_chat_link.c")   # text replies over Link's session
    endif()
    list(APPEND srcs "mute_chat_text.c")
    # Your own character (tools/mute/AVATAR_RECIPE.md) stands in for the default
    # avatar when avatar/mute_pixel.c exists here; this avatar/ is gitignored. The
    # default is in avatar/ at the top of the project.
    file(GLOB custom_avatar CONFIGURE_DEPENDS "${COMPONENT_DIR}/avatar/mute_pixel.c")
    if(custom_avatar)
        message(STATUS "Custom avatar: components/mute/avatar/mute_pixel.c")
        list(APPEND srcs "avatar/mute_pixel.c")
    else()
        list(APPEND srcs "../../avatar/mute_pixel.c")
    endif()
    if(CONFIG_MUTE_BOARD_WAVESHARE_S3_175C)
        list(APPEND srcs "boards/board_waveshare_s3_175c.c" "boards/mute_lcd_bands.c")
    elseif(CONFIG_MUTE_BOARD_AIPI)
        list(APPEND srcs "boards/board_aipi.c")
    elseif(CONFIG_MUTE_BOARD_WAVESHARE_C6_18)
        list(APPEND srcs "boards/board_waveshare_c6_18.c")
    elseif(CONFIG_MUTE_BOARD_SENSECAP_WATCHER)
        list(APPEND srcs "boards/board_sensecap_watcher.c" "boards/mute_lcd_bands.c")
        if(CONFIG_MUTE_WATCHER_CAMERA)
            list(APPEND srcs "boards/watcher_camera.c")
        endif()
    elseif(CONFIG_MUTE_BOARD_M5STACK_STICKS3)
        list(APPEND srcs "boards/board_m5stack_sticks3.c")
    elseif(CONFIG_MUTE_BOARD_M5STACK_STICKC_PLUS2)
        list(APPEND srcs "boards/board_m5stack_stickc_plus2.c")
    endif()
elseif(CONFIG_HOMEHUB_VOICE)
    # The Voice PE board uses just the Hatch voice session; the project's main/
    # voice_mute_chat.c stands in for the Mute settings, Wi-Fi and Link hooks.
    list(APPEND srcs "mute_chat.c" "mute_account_api.c" "mute_chat_session.cpp" "mute_chat_text.c" "mute_text.c")
    list(APPEND embed "test_reply.mp3")
endif()

set(mute_priv_requires
    esp_timer esp_driver_usb_serial_jtag esp_psram nvs_flash
    bt esp-tls mbedtls esp_app_format esp_http_client
    noise_core minimp3
    esp_lcd esp_adc esp_driver_i2s esp_driver_ledc esp_driver_spi esp_driver_uart esp_wifi
    esp_driver_pcnt)
list(APPEND mute_priv_requires camera)

idf_component_register(
    SRCS ${srcs}
    INCLUDE_DIRS "."
    EMBED_FILES ${embed}
    WHOLE_ARCHIVE   # mute_lv_mem.c provides symbols only LVGL references
    REQUIRES esp_driver_gpio esp_driver_i2c
    PRIV_REQUIRES ${mute_priv_requires}
)

if(CONFIG_MUTE_ENABLED)
    # LVGL spins forever on a failed assert unless LV_ASSERT_HANDLER is set,
    # which its Kconfig can't do. Abort, so the panic handler prints a backtrace
    # and reboots. PUBLIC reaches LVGL and every file that includes lvgl.h.
    idf_component_get_property(lvgl_lib lvgl__lvgl COMPONENT_LIB)
    target_compile_definitions(${lvgl_lib} PUBLIC
        "LV_ASSERT_HANDLER_INCLUDE=<stdlib.h>"
        "LV_ASSERT_HANDLER=abort()\;")
endif()
