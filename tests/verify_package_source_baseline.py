"""Reuse only the exact source-tested tree, never arbitrary skipped source tests."""
import json
import os
import pathlib
import subprocess
import urllib.request

BASE = '35ee147f3cdbe9ef051296021e098bda0b21176c'
RUN = 41049665327
REPO = 'juliansoul250/tiled'
WORKFLOW = '.github/workflows/layer-ui-validation.yml'
PACKAGE_PATHS = [
    'dist/macos/build-fork-package.sh',
    'dist/macos/verify-fork-package.py',
    'tests/test_macos_package_audit.py',
    'tests/verify_package_source_baseline.py',
]
ALLOWED = set(PACKAGE_PATHS + [WORKFLOW, '.github/workflows/macos-package-validation.yml',
                             'docs/development/mac-windows-build.md'])

def git(*args):
    return subprocess.check_output(['git', *args], text=True).strip()

def require(condition, message):
    if not condition:
        raise RuntimeError(str(message))

def api(path):
    request = urllib.request.Request('https://api.github.com/repos/' + REPO + path,
                                    headers={'Authorization': 'Bearer ' + os.environ['GH_TOKEN'],
                                             'Accept': 'application/vnd.github+json',
                                             'X-GitHub-Api-Version': '2022-11-28'})
    with urllib.request.urlopen(request, timeout=30) as response:
        return json.load(response)

require(os.environ.get('GITHUB_REPOSITORY') == REPO, 'Unexpected repository')
head = git('rev-parse', 'HEAD')
require(head == os.environ['GITHUB_SHA'], 'Checkout SHA differs from workflow SHA')
require(not git('status', '--porcelain', '--untracked-files=no'), 'Tracked checkout is dirty')
subprocess.run(['git', 'merge-base', '--is-ancestor', BASE, head], check=True)
changed = git('diff', '--name-only', BASE, head).splitlines()
require(set(changed) <= ALLOWED, 'Source changed: full native/source validation required: ' + str(set(changed) - ALLOWED))
# The existing source workflow may differ ONLY by these exact path exclusions.
# Its SDK, matrix, build/test commands, timeouts and gating cannot be changed.
baseline_workflow = git('show', BASE + ':' + WORKFLOW)
exclusions = '\n'.join("      - '!" + path + "'" for path in PACKAGE_PATHS)
expected = baseline_workflow.replace("      - 'translations/**'", "      - 'translations/**'\n" + exclusions)
require(pathlib.Path(WORKFLOW).read_text().strip() == expected, 'Source workflow changed beyond exact package path exclusions')
run = api('/actions/runs/' + str(RUN))
require(run['head_sha'] == BASE and run['head_repository']['full_name'] == REPO,
        'Source evidence run does not match pinned baseline/repository')
require(run['path'] == WORKFLOW, 'Unexpected source workflow path')
require(run['status'] == 'completed', 'Source evidence run is not terminal')
expected_jobs = {'windows', 'macos (macos-14, 6.11.3)', 'macos (xcode-27, 6.11.3)'}
jobs = api('/actions/runs/' + str(RUN) + '/jobs?per_page=100')['jobs']
verified = []
for name in sorted(expected_jobs):
    matches = [j for j in jobs if j['name'] == name]
    require(len(matches) == 1 and matches[0]['status'] == 'completed'
            and matches[0]['conclusion'] == 'success', 'Source job did not pass: ' + name)
    verified.append({'name': name, 'job_id': matches[0]['id'], 'conclusion': 'success'})
record = {'source_validation_commit': BASE, 'source_validation_run': RUN,
          'source_validation_url': run['html_url'], 'package_commit': head,
          'qt_version': '6.11.3', 'unchanged_source_and_native_tests': True,
          'changed_package_only_paths': changed, 'verified_source_jobs': verified,
          'note': 'Baseline package job failed an audit-parser check; only its successful source jobs are reused. This package must pass fresh deployment audit and 900-second smoke.'}
out = pathlib.Path('package-results/source-validation.json')
out.parent.mkdir(parents=True, exist_ok=True)
out.write_text(json.dumps(record, indent=2) + '\n')
print(json.dumps(record, indent=2))
