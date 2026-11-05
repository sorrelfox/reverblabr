# SPDX-FileCopyrightText: 2026 Reverblabr contributors
# SPDX-License-Identifier: MPL-2.0
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

# Model: runs the sound through a trained model from a folder.

rb_add_block(modelblock
    REQUIRES RB_BUILD_MODEL_BLOCK
    ENGINE
        modelblock.cpp
        modelblock.h
        modelhostlink.cpp
        modelhostlink.h
    UI
        ui/ModelFace.qml
        ui/modelfolderreader.cpp
        ui/modelfolderreader.h
        ui/ModelHelp.qml
        ui/ModelPanel.qml
        ui/ModelState.qml
        ui/ModelUi.qml
    ICONS
        ui/effect.model.svg
    TESTS
        tests/test_modelblock.cpp
    TEST_TARGET reverblabr-model-test
    QML_TESTS
        tests/tst_model.qml
)

if(RB_BUILD_MODEL_BLOCK)
    target_compile_definitions(reverblabr-blocks PUBLIC RB_HAVE_MODEL_BLOCK)
    # Where its host is, installed or in the build (ADR-0031).
    target_compile_definitions(reverblabr-blocks PRIVATE
        RB_MODEL_HOST_INSTALLED="${KDE_INSTALL_FULL_LIBEXECDIR}/reverblabr-model-host"
        RB_MODEL_HOST_BUILT="$<TARGET_FILE:reverblabr-model-host>"
    )
    target_link_libraries(reverblabr-blocks PUBLIC reverblabr-modelproto)
endif()
