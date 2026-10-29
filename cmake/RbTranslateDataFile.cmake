# SPDX-FileCopyrightText: 2026 Reverblabr contributors
# SPDX-License-Identifier: MPL-2.0
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

# **The desktop's words, translated, in the files that are installed**:
# the desktop entry, the notification events and the AppStream data.
#
# Their strings are extracted by Messages.sh into reverblabr-data.pot. Here
# each po/<language>/reverblabr-data.po, KDE's layout, is merged back into a
# copy of the file, one language at a time: `msgfmt --desktop` adds
# `Name[de]=…` lines, `msgfmt --xml` adds `xml:lang="de"` elements. With no
# translations yet, the copy is the file as written.
#
#   rb_translate_data_file(<target> <kind> <source> <output>)
#
# <kind> is DESKTOP (a desktop entry, or a .notifyrc, which is written the
# same way) or XML (AppStream metainfo). The output is made when <target> is
# built, and again whenever the file or a translation changes.
#
# Also runs as a script, which is what the build calls and what the tests
# call directly:
#
#   cmake -DKIND=… -DSOURCE=… -DOUTPUT=… -DPODIR=… -P RbTranslateDataFile.cmake

set(RB_DATA_DOMAIN reverblabr-data)

function(rb_translate_data_file target kind source output)
    file(GLOB translations "${PROJECT_SOURCE_DIR}/po/*/${RB_DATA_DOMAIN}.po")
    add_custom_command(
        OUTPUT "${output}"
        COMMAND ${CMAKE_COMMAND} -DKIND=${kind} "-DSOURCE=${source}" "-DOUTPUT=${output}"
                "-DPODIR=${PROJECT_SOURCE_DIR}/po" -P "${PROJECT_SOURCE_DIR}/cmake/RbTranslateDataFile.cmake"
        DEPENDS "${source}" ${translations} "${PROJECT_SOURCE_DIR}/cmake/RbTranslateDataFile.cmake"
        COMMENT "Merging translations into ${output}"
        VERBATIM)
    get_filename_component(name "${output}" NAME)
    add_custom_target(${target}-data-${name} ALL DEPENDS "${output}")
    add_dependencies(${target} ${target}-data-${name})
endfunction()

if(CMAKE_SCRIPT_MODE_FILE)
    foreach(variable KIND SOURCE OUTPUT PODIR)
        if(NOT DEFINED ${variable})
            message(FATAL_ERROR "RbTranslateDataFile.cmake needs -D${variable}=…")
        endif()
    endforeach()
    if(KIND STREQUAL "DESKTOP")
        set(mode --desktop)
    elseif(KIND STREQUAL "XML")
        set(mode --xml)
    else()
        message(FATAL_ERROR "KIND is DESKTOP or XML, not '${KIND}'")
    endif()

    get_filename_component(output_dir "${OUTPUT}" DIRECTORY)
    get_filename_component(output_name "${OUTPUT}" NAME)
    file(MAKE_DIRECTORY "${output_dir}")
    # msgfmt tells an AppStream file by its name, so every step keeps it.
    set(step_dir "${output_dir}/.${output_name}.merge")
    file(REMOVE_RECURSE "${step_dir}")
    file(MAKE_DIRECTORY "${step_dir}")
    set(current "${SOURCE}")

    file(GLOB translations "${PODIR}/*/${RB_DATA_DOMAIN}.po")
    list(SORT translations)
    if(translations)
        find_program(RB_MSGFMT msgfmt REQUIRED)
    endif()
    set(index 0)
    foreach(translation IN LISTS translations)
        get_filename_component(language_dir "${translation}" DIRECTORY)
        get_filename_component(language "${language_dir}" NAME)
        math(EXPR index "${index} + 1")
        file(MAKE_DIRECTORY "${step_dir}/${index}")
        set(next "${step_dir}/${index}/${output_name}")
        execute_process(
            COMMAND ${RB_MSGFMT} ${mode} --template=${current} -l ${language} -o ${next} ${translation}
            RESULT_VARIABLE result
            ERROR_VARIABLE error)
        if(NOT result EQUAL 0)
            message(FATAL_ERROR "msgfmt could not merge ${translation} into ${output_name}: ${error}")
        endif()
        set(current "${next}")
    endforeach()

    file(COPY_FILE "${current}" "${OUTPUT}" ONLY_IF_DIFFERENT)
    file(REMOVE_RECURSE "${step_dir}")
endif()
