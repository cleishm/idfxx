#!/usr/bin/env python3
"""Verify that every component's local version exists in the ESP Component Registry.

The registry processes uploads asynchronously. `compote component upload` polls for
a completion status and gives up early under load, reporting "An unknown error
happened during processing your component" for uploads that in fact succeed moments
later. That makes the upload step's exit status an unreliable signal on its own.

This script checks the outcome that actually matters — whether the version in each
component's manifest is published — polling until every component is present or the
timeout expires. Run it after the upload step to decide whether the job succeeded.
"""

import argparse
import glob
import json
import os
import re
import sys
import time
import urllib.error
import urllib.request

API = 'https://components.espressif.com/api/components'

VERSION_RE = re.compile(r'^version:\s*["\']?([^"\'\s]+)["\']?\s*$', re.MULTILINE)


def local_versions(components_dir: str) -> dict[str, str]:
    """Map component name -> version declared in its manifest."""
    versions: dict[str, str] = {}
    for d in sorted(glob.glob(os.path.join(components_dir, '*/'))):
        name = os.path.basename(d.rstrip('/'))
        manifest = os.path.join(d, 'idf_component.yml')
        if not os.path.isfile(manifest):
            continue
        with open(manifest) as f:
            match = VERSION_RE.search(f.read())
        if not match:
            raise SystemExit(f'{manifest}: no version field')
        versions[name] = match.group(1)
    return versions


def published_versions(namespace: str, name: str, timeout: int) -> list[str] | None:
    """Return the versions published for a component, [] if it does not exist yet.

    Returns None if the registry could not be reached, so the caller can retry
    rather than treat a transient network failure as a missing component.
    """
    url = f'{API}/{namespace}/{name}'
    try:
        with urllib.request.urlopen(url, timeout=timeout) as response:
            data = json.load(response)
    except urllib.error.HTTPError as e:
        return [] if e.code == 404 else None
    except (OSError, json.JSONDecodeError):
        # OSError covers URLError and the bare socket errors urllib lets through
        # (e.g. http.client.RemoteDisconnected on a dropped keep-alive).
        return None
    return [v['version'] for v in data.get('versions', []) if 'version' in v]


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--namespace', default='cleishm', help='registry namespace to check')
    parser.add_argument('--components-dir', default='components', help='directory containing component subdirectories')
    parser.add_argument('--timeout', type=int, default=600, help='give up after this many seconds')
    parser.add_argument('--interval', type=int, default=20, help='seconds between polls')
    parser.add_argument('--request-timeout', type=int, default=30, help='per-request timeout in seconds')
    args = parser.parse_args()

    expected = local_versions(args.components_dir)
    if not expected:
        raise SystemExit(f'No components found under {args.components_dir}')
    print(f'Verifying {len(expected)} components in namespace "{args.namespace}"', flush=True)

    pending = dict(expected)
    deadline = time.monotonic() + args.timeout
    unreachable: dict[str, str] = {}

    while True:
        unreachable = {}
        for name, version in sorted(pending.items()):
            found = published_versions(args.namespace, name, args.request_timeout)
            if found is None:
                unreachable[name] = version
            elif version in found:
                print(f'  ok       {name} {version}', flush=True)
                del pending[name]

        if not pending:
            print(f'\nAll {len(expected)} components are published at their manifest versions.')
            return

        remaining = deadline - time.monotonic()
        if remaining <= 0:
            break
        wait = min(args.interval, remaining)
        print(
            f'  waiting  {len(pending)} not yet published, retrying in {wait:.0f}s '
            f'({remaining:.0f}s left)',
            flush=True,
        )
        time.sleep(wait)

    print(f'\nERROR: {len(pending)} component(s) did not reach the registry:', file=sys.stderr)
    for name, version in sorted(pending.items()):
        why = 'registry unreachable' if name in unreachable else 'not published'
        print(f'  {name} {version} ({why})', file=sys.stderr)
    sys.exit(1)


if __name__ == '__main__':
    main()
