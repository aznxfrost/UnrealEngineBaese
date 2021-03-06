# Copyright (c) 2014 The Chromium Embedded Framework Authors. All rights
# reserved. Use of this source code is governed by a BSD-style license that
# can be found in the LICENSE file

from exec_util import exec_cmd
import os
import sys

def is_ue_fork(path):
  return os.path.exists(os.path.join(path, 'UE_VENDOR_REVISION.txt'))

def read_ue_fork_file(path):
  """ Reads the UE_VENDOR_REVISION file. """
  if os.path.exists(os.path.join(path, 'UE_VENDOR_REVISION.txt')):
    fp = open(os.path.join(path, 'UE_VENDOR_REVISION.txt'), 'r')
    data = fp.read()
    fp.close()
  else:
    raise Exception("Path does not exist: %s" % (os.path.join(path, 'UE_VENDOR_REVISION.txt')))

  # Parse the contents.
  return eval(data, {'__builtins__': None}, None)


if sys.platform == 'win32':
  # Force use of the git version bundled with depot_tools.
  git_exe = 'git.bat'
else:
  git_exe = 'git'

def is_checkout(path):
  """ Returns true if the path represents a git checkout or a ue fork. """
  return os.path.isdir(os.path.join(path, '.git')) or is_ue_fork(path)

def get_hash(path = '.', branch = 'HEAD'):
  """ Returns the git hash for the specified branch/tag/hash. """
  if is_ue_fork(path):
    return read_ue_fork_file(path)['commit']
  else:
    cmd = "%s rev-parse %s" % (git_exe, branch)
    result = exec_cmd(cmd, path)
    if result['out'] != '':
      return result['out'].strip()
    return 'Unknown'

def get_url(path = '.'):
  """ Returns the origin url for the specified path. """
  if is_ue_fork(path):
    return read_ue_fork_file(path)['url']
  else:
    cmd = "%s config --get remote.origin.url" % git_exe
    result = exec_cmd(cmd, path)
    if result['out'] != '':
      return result['out'].strip()
    return 'Unknown'

def get_commit_number(path = '.', branch = 'HEAD'):
  """ Returns the number of commits in the specified branch/tag/hash. """
  if is_ue_fork(path):
    return read_ue_fork_file(path)['revision']
  else:
    cmd = "%s rev-list --count %s" % (git_exe, branch)
    result = exec_cmd(cmd, path)
    if result['out'] != '':
      return result['out'].strip()
    return '0'

def get_changed_files(path = '.'):
  """ Retrieves the list of changed files. """
  # not implemented
  return []

def write_indented_output(output):
  """ Apply a fixed amount of intent to lines before printing. """
  if output == '':
    return
  for line in output.split('\n'):
    line = line.strip()
    if len(line) == 0:
      continue
    sys.stdout.write('\t%s\n' % line)

def git_apply_patch_file(patch_path, patch_dir):
  """ Apply |patch_path| to files in |patch_dir|. """
  patch_name = os.path.basename(patch_path)
  sys.stdout.write('\nApply %s in %s\n' % (patch_name, patch_dir))

  if not os.path.isfile(patch_path):
    sys.stdout.write('... patch file does not exist.\n')
    return 'fail'

  patch_string = open(patch_path, 'rb').read()
  if sys.platform == 'win32':
    # Convert the patch to Unix line endings. This is necessary to avoid
    # whitespace errors with git apply.
    patch_string = patch_string.replace('\r\n', '\n')

  # Git apply fails silently if not run relative to a respository root.
  if not is_checkout(patch_dir):
    sys.stdout.write('... patch directory is not a repository root.\n')
    return 'fail'

  # Output patch contents.
  cmd = '%s apply -p0 --numstat' % git_exe
  result = exec_cmd(cmd, patch_dir, patch_string)
  write_indented_output(result['out'].replace('<stdin>', patch_name))

  # Reverse check to see if the patch has already been applied.
  cmd = '%s apply -p0 --reverse --check' % git_exe
  result = exec_cmd(cmd, patch_dir, patch_string)
  if result['err'].find('error:') < 0:
    sys.stdout.write('... already applied (skipping).\n')
    return 'skip'

  # Normal check to see if the patch can be applied cleanly.
  cmd = '%s apply -p0 --check' % git_exe
  result = exec_cmd(cmd, patch_dir, patch_string)
  if result['err'].find('error:') >= 0:
    sys.stdout.write('... failed to apply:\n')
    write_indented_output(result['err'].replace('<stdin>', patch_name))
    return 'fail'

  # Apply the patch file. This should always succeed because the previous
  # command succeeded.
  cmd = '%s apply -p0' % git_exe
  result = exec_cmd(cmd, patch_dir, patch_string)
  if result['err'] == '':
    sys.stdout.write('... successfully applied.\n')
  else:
    sys.stdout.write('... successfully applied (with warnings):\n')
    write_indented_output(result['err'].replace('<stdin>', patch_name))
  return 'apply'
