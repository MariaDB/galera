# Copyright (c) 2019, Codership Oy. All rights reserved.
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; version 2 of the License.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301 USA

FILE(GLOB SRC
  "*.h"
  "*.c"
  )

ADD_EXECUTABLE(node ${SRC})

TARGET_LINK_LIBRARIES(node wsrep dl pthread)

CONFIGURE_FILE(${CMAKE_CURRENT_SOURCE_DIR}/node.sh
               ${CMAKE_CURRENT_BINARY_DIR}/node.sh COPYONLY)
