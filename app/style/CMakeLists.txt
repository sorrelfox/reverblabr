# SPDX-FileCopyrightText: 2026 Reverblabr contributors
# SPDX-License-Identifier: MPL-2.0
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

# **The Reverblabr control style** ([ADR-0012]): a Qt Quick Controls
# style whose module name is the style's name, so that
# `QQuickStyle::setStyle("Reverblabr")` finds it. It draws the controls the
# way the design prototype does, from the tokens in io.github.reverblabr,
# and leaves every control it does not draw yet to Qt's Basic style.
#
# **A module of its own, not part of the interface's.** A style is what
# `import QtQuick.Controls` turns into at run time; the interface's files
# must not know which one they got, and the style must not be what
# *Follow system* draws with.
add_library(reverblabr-style STATIC)
ecm_add_qml_module(reverblabr-style
    URI Reverblabr
    GENERATE_PLUGIN_SOURCE
    DEPENDENCIES
        QtQuick
        QtQuick.Templates
        QtQuick.Controls.impl
        QtQuick.Controls.Basic.impl
        QtQuick.Shapes
        org.kde.kirigami
        org.kde.kirigami.primitives
        io.github.reverblabr
    # **Anything this style does not draw, Basic does**: that is what makes
    # a style out of a handful of files rather than all of them.
    IMPORTS
        QtQuick.Controls.Basic
)
qt_target_qml_sources(reverblabr-style QML_FILES
    Button.qml
    CheckBox.qml
    FocusRing.qml
    ItemDelegate.qml
    MenuBar.qml
    MenuBarItem.qml
    RadioButton.qml
    SplitView.qml
    Switch.qml
    TabBar.qml
    TabButton.qml
    ToolBar.qml
    ToolButton.qml
    # Value controls: sliders, fields, lists and scroll bars, and the
    # frame and chevron they share.
    Chevron.qml
    ComboBox.qml
    FieldFrame.qml
    ScrollBar.qml
    ScrollView.qml
    Slider.qml
    SpinBox.qml
    TextArea.qml
    TextField.qml
)
# Popups and messages: what pops up over the window or frames what is
# in it, and the window itself, whose type every label inherits.
qt_target_qml_sources(reverblabr-style QML_FILES
    ApplicationWindow.qml
    Dialog.qml
    DialogButtonBox.qml
    Menu.qml
    MenuItem.qml
    MenuSeparator.qml
    Pane.qml
    Popup.qml
    ToolTip.qml
)
target_link_libraries(reverblabr-style PRIVATE Qt6::Quick Qt6::QuickControls2)
