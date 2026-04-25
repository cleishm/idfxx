#!/usr/bin/env python3
"""Print idfxx components in topological dependency order.

Output is one component name (or path, with --paths) per line, with
dependencies appearing before their dependents. Used by the bootstrap
script and the upload_components.yml workflow to ensure the registry
processes a component's deps before the component itself.
"""

import argparse
import glob
import os
import re
import sys


def load_deps(components_dir: str) -> dict[str, list[str]]:
    deps: dict[str, list[str]] = {}
    for d in sorted(glob.glob(os.path.join(components_dir, '*/'))):
        name = os.path.basename(d.rstrip('/'))
        manifest = os.path.join(d, 'idf_component.yml')
        if not os.path.isfile(manifest):
            continue
        with open(manifest) as f:
            text = f.read()
        found = sorted(set(re.findall(r'cleishm/(idfxx_[a-z_]+)', text)))
        deps[name] = [x for x in found if x != name]
    return deps


def topo_sort(deps: dict[str, list[str]]) -> list[str]:
    remaining = {n: list(ds) for n, ds in deps.items()}
    order: list[str] = []
    while remaining:
        ready = sorted([n for n, ds in remaining.items() if not any(d in remaining for d in ds)])
        if not ready:
            raise SystemExit('Cycle in component dependencies: ' + ', '.join(remaining))
        for n in ready:
            order.append(n)
            del remaining[n]
    return order


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--paths', action='store_true', help='print components/<name> paths instead of bare names')
    parser.add_argument('--components-dir', default='components', help='directory containing component subdirectories')
    args = parser.parse_args()

    deps = load_deps(args.components_dir)
    for name in topo_sort(deps):
        sys.stdout.write(f'{args.components_dir}/{name}\n' if args.paths else f'{name}\n')


if __name__ == '__main__':
    main()
