# SPDX-FileCopyrightText: 2026 Reverblabr contributors
# SPDX-License-Identifier: MPL-2.0
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

#[=======================================================================[.rst:
RbLayering
----------

Checks at configure time that each layer links only what it is allowed to.

::

  rb_check_layering(TARGETS <target>...
                    FORBID <group>...
                    REASON <text>)

Walks the complete link closure of every listed target: direct and transitive
dependencies, imported targets, and targets named inside generator
expressions. If any link item belongs to a forbidden group, configuration
stops with a message that names the offending item and the path to it.

Groups:

``qt``
  any Qt module
``qt-gui``
  Qt modules beyond Core and DBus: Gui, Widgets, OpenGL, Qml, Quick, Svg, ShaderTools
``kf``
  any KDE Frameworks library
``pipewire``
  libpipewire and SPA

Call it after all targets are defined, usually at the end of the top-level
CMakeLists.txt.
#]=======================================================================]

# Regular expressions matched against lower-cased link items.
set(_RB_LAYERING_PATTERN_qt "(^|[^a-z0-9_])qt[0-9]*::|libqt[0-9]")
set(_RB_LAYERING_PATTERN_qt-gui
    "(^|[^a-z0-9_])qt[0-9]*::(gui|widgets|opengl|qml|quick|svg|shadertools)|libqt[0-9]*(gui|widgets|opengl|qml|quick|svg|shadertools)")
set(_RB_LAYERING_PATTERN_kf "(^|[^a-z0-9_])kf[0-9]+::|libkf[0-9]")
set(_RB_LAYERING_PATTERN_pipewire "pipewire|libspa|(^|[^a-z0-9_])pkgconfig::spa")

set(_RB_LAYERING_NAME_qt "Qt")
set(_RB_LAYERING_NAME_qt-gui "Qt GUI modules")
set(_RB_LAYERING_NAME_kf "KDE Frameworks")
set(_RB_LAYERING_NAME_pipewire "PipeWire")

function(rb_check_layering)
    cmake_parse_arguments(PARSE_ARGV 0 arg "" "REASON" "TARGETS;FORBID")
    if(arg_UNPARSED_ARGUMENTS OR NOT arg_TARGETS OR NOT arg_FORBID)
        message(FATAL_ERROR "Usage: rb_check_layering(TARGETS <target>... FORBID <group>... REASON <text>)")
    endif()
    foreach(group IN LISTS arg_FORBID)
        if(NOT DEFINED _RB_LAYERING_PATTERN_${group})
            message(FATAL_ERROR "rb_check_layering: unknown group \"${group}\"")
        endif()
    endforeach()

    foreach(root IN LISTS arg_TARGETS)
        if(NOT TARGET ${root})
            message(FATAL_ERROR "rb_check_layering: no target named \"${root}\"")
        endif()

        # Breadth-first walk. path_<id> remembers how each target was reached,
        # so the error message can show the chain that pulled in a dependency.
        set(queue ${root})
        set(visited ${root})
        string(MAKE_C_IDENTIFIER "${root}" root_id)
        set(path_${root_id} "${root}")

        while(queue)
            list(POP_FRONT queue current)
            string(MAKE_C_IDENTIFIER "${current}" current_id)

            get_target_property(type ${current} TYPE)
            get_target_property(imported ${current} IMPORTED)
            set(properties INTERFACE_LINK_LIBRARIES)
            if(NOT imported AND NOT type STREQUAL "INTERFACE_LIBRARY")
                list(APPEND properties LINK_LIBRARIES)
            endif()

            set(items "")
            foreach(property IN LISTS properties)
                get_target_property(values ${current} ${property})
                if(values)
                    list(APPEND items ${values})
                endif()
            endforeach()

            foreach(item IN LISTS items)
                string(TOLOWER "${item}" lowered)
                foreach(group IN LISTS arg_FORBID)
                    if(lowered MATCHES "${_RB_LAYERING_PATTERN_${group}}")
                        message(FATAL_ERROR
                            "Layering violation: ${root} must not link ${_RB_LAYERING_NAME_${group}}.\n"
                            "  Link path: ${path_${current_id}} -> ${item}\n"
                            "  ${arg_REASON}\n"
                            "  See docs/architecture.md for the layer rules.")
                    endif()
                endforeach()

                # Follow every target named in the item, including targets
                # inside generator expressions such as $<LINK_ONLY:...>.
                string(REGEX MATCHALL "[A-Za-z0-9_.+-]+(::[A-Za-z0-9_.+-]+)*" names "${item}")
                foreach(name IN LISTS names)
                    if(TARGET "${name}" AND NOT name IN_LIST visited)
                        list(APPEND visited "${name}")
                        list(APPEND queue "${name}")
                        string(MAKE_C_IDENTIFIER "${name}" name_id)
                        set(path_${name_id} "${path_${current_id}} -> ${name}")
                    endif()
                endforeach()
            endforeach()
        endwhile()
    endforeach()
endfunction()
