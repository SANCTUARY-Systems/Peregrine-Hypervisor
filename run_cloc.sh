#!/bin/bash

# Copyright (c) 2023 SANCTUARY Systems GmbH
#
# This file is free software: you may copy, redistribute and/or modify it
# under the terms of the GNU General Public License as published by the
# Free Software Foundation, either version 2 of the License, or (at your
# option) any later version.
#
# This file is distributed in the hope that it will be useful, but
# WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
# General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program. If not, see <http://www.gnu.org/licenses/>.
#
# For a commercial license, please contact SANCTUARY Systems GmbH
# directly at info@sanctuary.dev

cloc --json --out=cloc.json --not-match-f=".*_(test|fake).(c|cc|h|hh)" --not-match-d="(fake|hftest)" src inc
