#!/usr/bin/env python3
"""Add an installer to installer.json, the list stage 1 reads to find its download.

Usage: installer-manifest.py OLD.json SHA256 URL RUN > NEW.json

OLD.json may be missing or empty. Each installer is one line, so the busybox
shell in stage 1 can pick its own with grep and sed; keep it that way.
"""
import json
import sys


def main():
    old, sha, url, run = sys.argv[1:5]
    try:
        with open(old) as f:
            items = json.load(f).get("installers", [])
    except (OSError, ValueError):
        items = []
    items = [i for i in items if not (i.get("sha256") == sha and i.get("url") == url)]
    items.insert(0, {"sha256": sha, "url": url, "run": run})
    lines = ",\n".join("    " + json.dumps(i, separators=(", ", ": ")) for i in items)
    print('{\n  "installers": [\n%s\n  ]\n}' % lines)


if __name__ == "__main__":
    main()
