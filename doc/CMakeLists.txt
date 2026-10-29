# SPDX-FileCopyrightText: 2026 Reverblabr contributors
# SPDX-License-Identifier: MPL-2.0
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

# **The handbook, under the application's own name**: help:/reverblabr,
# which Help → Handbook opens (app/appinfo.cpp), is
# share/doc/HTML/<language>/reverblabr. The build checks it against the
# DocBook DTD as it compiles it, so a handbook that does not validate fails
# the build.
kdoctools_create_handbook(index.docbook INSTALL_DESTINATION ${KDE_INSTALL_DOCBUNDLEDIR}/en SUBDIR reverblabr)
