# Copyright 2013 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import distutils
import glob
import heapq
import logging
import os
import shutil
import subprocess as subprocess
import sys
import tempfile
import time

from telemetry.core import exceptions
from telemetry.core import util
from telemetry.core.backends import browser_backend
from telemetry.core.backends.chrome import chrome_browser_backend
from telemetry.util import support_binaries


class DesktopBrowserBackend(chrome_browser_backend.ChromeBrowserBackend):
  """The backend for controlling a locally-executed browser instance, on Linux,
  Mac or Windows.
  """
  def __init__(self, browser_options, executable, flash_path, is_content_shell,
               browser_directory, output_profile_path, extensions_to_load):
    super(DesktopBrowserBackend, self).__init__(
        is_content_shell=is_content_shell,
        supports_extensions=not is_content_shell,
        browser_options=browser_options,
        output_profile_path=output_profile_path,
        extensions_to_load=extensions_to_load)

    # Initialize fields so that an explosion during init doesn't break in Close.
    self._proc = None
    self._tmp_profile_dir = None
    self._tmp_output_file = None

    self._executable = executable
    if not self._executable:
      raise Exception('Cannot create browser, no executable found!')

    self._flash_path = flash_path
    if (browser_options.warn_if_no_flash
        and self._flash_path and not os.path.exists(self._flash_path)):
      logging.warning(('Could not find flash at %s. Running without flash.\n\n'
                       'To fix this see http://go/read-src-internal') %
                      self._flash_path)
      self._flash_path = None

    if len(extensions_to_load) > 0 and is_content_shell:
      raise browser_backend.ExtensionsNotSupportedException(
          'Content shell does not support extensions.')

    self._browser_directory = browser_directory
    self._port = None
    self._profile_dir = None
    self._tmp_minidump_dir = tempfile.mkdtemp()
    self._crash_service = None

    self._SetupProfile()

  def _SetupProfile(self):
    if not self.browser_options.dont_override_profile:
      if self._output_profile_path:
        # If both |_output_profile_path| and |profile_dir| are specified then
        # the calling code will throw an exception, so we don't need to worry
        # about that case here.
        self._tmp_profile_dir = self._output_profile_path
      else:
        self._tmp_profile_dir = tempfile.mkdtemp()
      profile_dir = self._profile_dir or self.browser_options.profile_dir
      if profile_dir:
        if self.is_content_shell:
          logging.critical('Profiles cannot be used with content shell')
          sys.exit(1)
        logging.info("Using profile directory:'%s'." % profile_dir)
        shutil.rmtree(self._tmp_profile_dir)
        shutil.copytree(profile_dir, self._tmp_profile_dir)

  def _GetCrashServicePipeName(self):
    # Ensure a unique pipe name by using the name of the temp dir.
    return r'\\.\pipe\%s_service' % os.path.basename(self._tmp_minidump_dir)

  def _StartCrashService(self):
    os_name = self._browser.platform.GetOSName()
    if os_name != 'win':
      return None
    return subprocess.Popen([
        support_binaries.FindPath('crash_service', os_name),
        '--no-window',
        '--dumps-dir=%s' % self._tmp_minidump_dir,
        '--pipe-name=%s' % self._GetCrashServicePipeName()])

  def _GetCdbPath(self):
    search_paths = [os.getenv('PROGRAMFILES(X86)', ''),
                    os.getenv('PROGRAMFILES', ''),
                    os.getenv('LOCALAPPDATA', ''),
                    os.getenv('PATH', '')]
    possible_paths = [
        'Debugging Tools For Windows',
        'Debugging Tools For Windows (x86)',
        'Debugging Tools For Windows (x64)',
        os.path.join('Windows Kits', '8.0', 'Debuggers', 'x86'),
        os.path.join('Windows Kits', '8.0', 'Debuggers', 'x64'),
        os.path.join('win_toolchain', 'vs2013_files', 'win8sdk', 'Debuggers',
                     'x86'),
        os.path.join('win_toolchain', 'vs2013_files', 'win8sdk', 'Debuggers',
                     'x64'),
        ]
    for possible_path in possible_paths:
      path = distutils.spawn.find_executable(
          os.path.join(possible_path, 'cdb'),
          path=os.pathsep.join(search_paths))
      if path:
        return path
    return None

  def HasBrowserFinishedLaunching(self):
    # In addition to the functional check performed by the base class, quickly
    # check if the browser process is still alive.
    if not self.IsBrowserRunning():
      raise exceptions.ProcessGoneException(
          "Return code: %d" % self._proc.returncode)
    return super(DesktopBrowserBackend, self).HasBrowserFinishedLaunching()

  def GetBrowserStartupArgs(self):
    args = super(DesktopBrowserBackend, self).GetBrowserStartupArgs()
    self._port = util.GetUnreservedAvailableLocalPort()
    args.append('--remote-debugging-port=%i' % self._port)
    args.append('--enable-crash-reporter-for-testing')
    args.append('--use-mock-keychain')
    if not self.is_content_shell:
      args.append('--window-size=1280,1024')
      if self._flash_path:
        args.append('--ppapi-flash-path=%s' % self._flash_path)
      if not self.browser_options.dont_override_profile:
        args.append('--user-data-dir=%s' % self._tmp_profile_dir)
    return args

  def SetProfileDirectory(self, profile_dir):
    # Make sure _profile_dir hasn't already been set.
    assert self._profile_dir is None

    if self.is_content_shell:
      logging.critical('Profile creation cannot be used with content shell')
      sys.exit(1)

    self._profile_dir = profile_dir

  def Start(self):
    assert not self._proc, 'Must call Close() before Start()'

    args = [self._executable]
    args.extend(self.GetBrowserStartupArgs())
    if self.browser_options.startup_url:
      args.append(self.browser_options.startup_url)
    env = os.environ.copy()
    env['CHROME_HEADLESS'] = '1'  # Don't upload minidumps.
    env['BREAKPAD_DUMP_LOCATION'] = self._tmp_minidump_dir
    env['CHROME_BREAKPAD_PIPE_NAME'] = self._GetCrashServicePipeName()
    self._crash_service = self._StartCrashService()
    logging.debug('Starting Chrome %s', args)
    if not self.browser_options.show_stdout:
      self._tmp_output_file = tempfile.NamedTemporaryFile('w', 0)
      self._proc = subprocess.Popen(
          args, stdout=self._tmp_output_file, stderr=subprocess.STDOUT, env=env)
    else:
      self._proc = subprocess.Popen(args, env=env)

    try:
      self._WaitForBrowserToComeUp()
      self._PostBrowserStartupInitialization()
    except:
      self.Close()
      raise

  @property
  def pid(self):
    if self._proc:
      return self._proc.pid
    return None

  @property
  def browser_directory(self):
    return self._browser_directory

  @property
  def profile_directory(self):
    return self._tmp_profile_dir

  def IsBrowserRunning(self):
    return self._proc and self._proc.poll() == None

  def GetStandardOutput(self):
    if not self._tmp_output_file:
      if self.browser_options.show_stdout:
        # This can happen in the case that loading the Chrome binary fails.
        # We print rather than using logging here, because that makes a
        # recursive call to this function.
        print >> sys.stderr, "Can't get standard output with --show-stdout"
      return ''
    self._tmp_output_file.flush()
    try:
      with open(self._tmp_output_file.name) as f:
        return f.read()
    except IOError:
      return ''

  def _GetMostRecentMinidump(self):
    dumps = glob.glob(os.path.join(self._tmp_minidump_dir, '*.dmp'))
    if not dumps:
      return None
    most_recent_dump = heapq.nlargest(1, dumps, os.path.getmtime)[0]
    if os.path.getmtime(most_recent_dump) < (time.time() - (5 * 60)):
      logging.warning('Crash dump is older than 5 minutes. May not be correct.')
    return most_recent_dump

  def _GetStackFromMinidump(self, minidump):
    os_name = self._browser.platform.GetOSName()
    if os_name == 'win':
      cdb = self._GetCdbPath()
      if not cdb:
        logging.warning('cdb.exe not found.')
        return None
      output = subprocess.check_output([cdb, '-y', self._browser_directory,
                                        '-c', '.ecxr;k30;q', '-z', minidump])
      stack_start = output.find('ChildEBP')
      stack_end = output.find('quit:')
      return output[stack_start:stack_end]

    stackwalk = support_binaries.FindPath('minidump_stackwalk', os_name)
    if not stackwalk:
      logging.warning('minidump_stackwalk binary not found.')
      return None

    symbols = glob.glob(os.path.join(self._browser_directory, '*.breakpad*'))
    if not symbols:
      logging.warning('No breakpad symbols found.')
      return None

    with open(minidump, 'rb') as infile:
      minidump += '.stripped'
      with open(minidump, 'wb') as outfile:
        outfile.write(''.join(infile.read().partition('MDMP')[1:]))

    symbols_path = os.path.join(self._tmp_minidump_dir, 'symbols')
    for symbol in sorted(symbols, key=os.path.getmtime, reverse=True):
      if not os.path.isfile(symbol):
        continue
      with open(symbol, 'r') as f:
        fields = f.readline().split()
        if not fields:
          continue
        sha = fields[3]
        binary = ' '.join(fields[4:])
      symbol_path = os.path.join(symbols_path, binary, sha)
      if os.path.exists(symbol_path):
        continue
      os.makedirs(symbol_path)
      shutil.copyfile(symbol, os.path.join(symbol_path, binary + '.sym'))

    return subprocess.check_output([stackwalk, minidump, symbols_path],
                                   stderr=open(os.devnull, 'w'))

  def GetStackTrace(self):
    most_recent_dump = self._GetMostRecentMinidump()
    if not most_recent_dump:
      logging.warning('No crash dump found. Returning browser stdout.')
      return self.GetStandardOutput()

    stack = self._GetStackFromMinidump(most_recent_dump)
    if not stack:
      logging.warning('Failed to symbolize minidump. Returning browser stdout.')
      return self.GetStandardOutput()

    return stack

  def __del__(self):
    self.Close()

  def Close(self):
    super(DesktopBrowserBackend, self).Close()

    # First, try to politely shutdown.
    if self.IsBrowserRunning():
      self._proc.terminate()
      try:
        util.WaitFor(lambda: not self.IsBrowserRunning(), timeout=5)
        self._proc = None
      except util.TimeoutException:
        logging.warning('Failed to gracefully shutdown. Proceeding to kill.')

    # If it didn't comply, get more aggressive.
    if self.IsBrowserRunning():
      self._proc.kill()

    try:
      util.WaitFor(lambda: not self.IsBrowserRunning(), timeout=10)
    except util.TimeoutException:
      raise Exception('Could not shutdown the browser.')
    finally:
      self._proc = None

    if self._crash_service:
      self._crash_service.kill()
      self._crash_service = None

    if self._output_profile_path:
      # If we need the output then double check that it exists.
      if not (self._tmp_profile_dir and os.path.exists(self._tmp_profile_dir)):
        raise Exception("No profile directory generated by Chrome: '%s'." %
            self._tmp_profile_dir)
    else:
      # If we don't need the profile after the run then cleanup.
      if self._tmp_profile_dir and os.path.exists(self._tmp_profile_dir):
        shutil.rmtree(self._tmp_profile_dir, ignore_errors=True)
        self._tmp_profile_dir = None

    if self._tmp_output_file:
      self._tmp_output_file.close()
      self._tmp_output_file = None
