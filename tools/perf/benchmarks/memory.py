# Copyright (c) 2013 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
from telemetry import test

from measurements import memory


@test.Disabled('android')  # crbug.com/370977
class MemoryMobile(test.Test):
  test = memory.Memory
  page_set = 'page_sets/mobile_memory.py'


class MemoryTop25(test.Test):
  test = memory.Memory
  page_set = 'page_sets/top_25.py'


class Reload2012Q3(test.Test):
  tag = 'reload'
  test = memory.Memory
  page_set = 'page_sets/top_desktop_sites_2012Q3.py'


@test.Disabled('android')  # crbug.com/371153
class MemoryToughDomMemoryCases(test.Test):
  test = memory.Memory
  page_set = 'page_sets/tough_dom_memory_cases.py'
