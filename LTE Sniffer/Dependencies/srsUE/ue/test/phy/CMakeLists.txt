# Copyright 2015 Software Radio Systems Limited
#
# This file is part of srsUE
#
# srsUE is free software: you can redistribute it and/or modify
# it under the terms of the GNU Affero General Public License as
# published by the Free Software Foundation, either version 3 of
# the License, or (at your option) any later version.
#
# srsUE is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU Affero General Public License for more details.
#
# A copy of the GNU Affero General Public License can be found in
# the LICENSE file in the top-level directory of this distribution
# and at http://www.gnu.org/licenses/.
#

add_executable(ue_itf_test_sib1 ue_itf_test_sib1.cc)
target_link_libraries(ue_itf_test_sib1 srsue_common srsue_phy srsue_radio ${Boost_LIBRARIES})

add_executable(ue_itf_test_prach ue_itf_test_prach.cc)
target_link_libraries(ue_itf_test_prach srsue_common srsue_phy srsue_radio ${Boost_LIBRARIES})
