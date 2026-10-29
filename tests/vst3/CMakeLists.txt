# SPDX-FileCopyrightText: 2026 Reverblabr contributors
# SPDX-License-Identifier: MPL-2.0
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

# The tests' VST3 plugin, with a stand-in for a view that needs no display
# (see testplugin.cpp), in bundles under RB_TEST_VST3_DIR, which a test
# names where VST3 plugins are looked for. Only the VST3 SDK's interface
# headers; it links nothing.
#
#   rb-test-vst3.vst3          the library, and no moduleinfo.json: its
#                              plugins are found by loading it
#   rb-test-vst3-listed.vst3   the same library, with a moduleinfo.json that
#                              names only the stereo plugin
#   rb-test-vst3-empty.vst3    a moduleinfo.json that names no plugin's
#                              sound, and no library: never loaded
set(_rb_vst3_machine "${CMAKE_SYSTEM_PROCESSOR}-linux")
set(_rb_vst3_bundle "${RB_TEST_VST3_DIR}/rb-test-vst3.vst3")
set(_rb_vst3_listed "${RB_TEST_VST3_DIR}/rb-test-vst3-listed.vst3")
set(_rb_vst3_empty "${RB_TEST_VST3_DIR}/rb-test-vst3-empty.vst3")

add_library(rb-test-vst3 MODULE testplugin.cpp)
target_link_libraries(rb-test-vst3 PRIVATE Vst3Sdk::Interfaces)
set_target_properties(rb-test-vst3 PROPERTIES
    PREFIX ""
    SUFFIX ".so"
    AUTOMOC OFF AUTORCC OFF
    CXX_VISIBILITY_PRESET hidden
    LIBRARY_OUTPUT_DIRECTORY "${_rb_vst3_bundle}/Contents/${_rb_vst3_machine}"
)
add_custom_command(TARGET rb-test-vst3 POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${_rb_vst3_listed}/Contents/${_rb_vst3_machine}"
    COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:rb-test-vst3>"
            "${_rb_vst3_listed}/Contents/${_rb_vst3_machine}/rb-test-vst3-listed.so"
    VERBATIM
)
file(WRITE "${_rb_vst3_listed}/Contents/Resources/moduleinfo.json" [=[
{
	"Name": "rb-test-vst3",
	"Version": "1",
	// As the SDK writes it: comments and trailing commas allowed.
	"Factory Info": {
		"Vendor": "Reverblabr",
		"URL": "",
		"E-Mail": "",
	},
	"Classes": [
		{
			"CID": "5242565354334741494E53544552454F",
			"Category": "Audio Module Class",
			"Name": "Reverblabr test gain",
			"Vendor": "Reverblabr",
			"Version": "1",
			"Sub Categories": [ "Fx", "Tools" ],
			"Class Flags": 0,
			"Cardinality": 2147483647,
		},
		{
			"CID": "5242565354334354524C4C4552202020",
			"Category": "Component Controller Class",
			"Name": "Reverblabr test gain controller",
			"Vendor": "Reverblabr",
			"Version": "1",
			"Sub Categories": [],
		},
	],
}
]=])
file(WRITE "${_rb_vst3_empty}/Contents/Resources/moduleinfo.json" [=[
{
	"Name": "rb-test-vst3-empty",
	"Classes": [
		{
			"CID": "5242565354334354524C4C4552202020",
			"Category": "Component Controller Class",
			"Name": "Only a controller",
			"Sub Categories": []
		}
	]
}
]=])
