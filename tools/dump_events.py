#!/usr/bin/env python3
"""
dump_events.py -- inspect the event commands inside Map*.rxdata files.

The event command histogram tells you which RGSS engine features a game actually
exercises; dumping the parameters of the interesting ones (509 = message
continuation, 355/655 = script calls, 111 = conditional branch) shows exactly
what the engine has to support.

Usage:
    python dump_events.py <MapNNN.rxdata> [--codes 355,655] [--limit 20]
"""

import argparse
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from rxdata_dump import RubyObject, load, summarize  # noqa: E402


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("mapfile")
    ap.add_argument("--codes", default="", help="comma separated code filter")
    ap.add_argument("--limit", type=int, default=20)
    ap.add_argument("--max-param", type=int, default=140)
    a = ap.parse_args()

    want = set()
    if a.codes:
        want = set(int(x) for x in a.codes.split(",") if x.strip())

    m = load(a.mapfile)
    if not isinstance(m, RubyObject):
        raise SystemExit("unexpected top-level object: %r" % type(m))
    print("%s: class=%s width=%s height=%s tileset=%s events=%s"
          % (os.path.basename(a.mapfile), m.cls, m.ivars.get("@width"),
             m.ivars.get("@height"), m.ivars.get("@tileset_id"),
             len(m.ivars.get("@events") or {})))

    shown = 0
    for eid, ev in (m.ivars.get("@events") or {}).items():
        if not isinstance(ev, RubyObject):
            continue
        for pi, pg in enumerate(ev.ivars.get("@pages") or []):
            if not isinstance(pg, RubyObject):
                continue
            for ci, cmd in enumerate(pg.ivars.get("@list") or []):
                if not isinstance(cmd, RubyObject):
                    continue
                code = cmd.ivars.get("@code")
                if want and code not in want:
                    continue
                params = json.dumps(summarize(cmd.ivars.get("@parameters")),
                                    ensure_ascii=False)
                if len(params) > a.max_param:
                    params = params[:a.max_param] + "..."
                print("event %-4s page %-2d cmd %-4d code %-4s indent %-2s %s"
                      % (eid, pi, ci, code, cmd.ivars.get("@indent"), params))
                shown += 1
                if shown >= a.limit:
                    print("... (limit reached)")
                    return


if __name__ == "__main__":
    main()
