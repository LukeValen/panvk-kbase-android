#!/usr/bin/env python3
"""Check a PanProbe share/upload zip (stdlib only). Exit 1 on any failure.

usage: verify_zip.py <panprobe-*.zip> [--expect N]   (N = suite test count, default 36)
"""
import json
import sys
import zipfile

CHECKERS = {"dxvk": "dxvk_reqs", "bachata_s4": "bachata_reqs", "vkd3d": "vkd3d_reqs"}
STATUSES = {"PASS", "FAIL", "SKIP", "CRASH", "TIMEOUT"}


def main(argv):
    if len(argv) < 2:
        sys.exit(__doc__)
    expect = int(argv[argv.index("--expect") + 1]) if "--expect" in argv else 36
    zf = zipfile.ZipFile(argv[1])
    names = set(zf.namelist())
    errs = []

    def check(cond, msg):
        if not cond:
            errs.append(msg)
        return cond

    def size(path):
        return zf.getinfo(path).file_size if path in names else 0

    def load(path):
        return json.loads(zf.read(path)) if check(path in names, f"missing {path}") else {}

    # 1. vulkan-info.json compliance: pass + items + tested_by per item.
    comp = load("vulkan-info.json").get("compliance", {})
    for key in CHECKERS:
        rep = comp.get(key)
        if not check(isinstance(rep, dict), f"vulkan-info.json: compliance.{key} missing"):
            continue
        check(isinstance(rep.get("pass"), bool), f"compliance.{key}.pass not bool")
        items = rep.get("items") or []
        check(len(items) > 0, f"compliance.{key}: no items")
        bad = [i.get("name") for i in items if not isinstance(i.get("tested_by"), list)]
        check(not bad, f"compliance.{key}: {len(bad)} items without tested_by (first {bad[:3]})")
        print(f"compliance.{key}: pass={rep.get('pass')} items={len(items)} "
              f"gpu_tested={sum(1 for i in items if i.get('tested_by'))}")

    # 2. Run folder: compliance.json + one checker log each.
    runs = sorted({n.split("/")[0] for n in names if n.count("/") == 1 and n.endswith("/summary.json")})
    if not check(len(runs) == 1, f"expected one run folder, got {runs}"):
        return report(errs)
    run = runs[0]
    check(size(f"{run}/compliance.json") > 0, f"{run}/compliance.json missing/empty")
    for test in CHECKERS.values():
        check(size(f"{run}/compliance-{test}.log") > 0, f"{run}/compliance-{test}.log missing/empty")

    # 3. summary.json and manifest.json results: one entry per suite test, status, existing non-empty log.
    summary = load(f"{run}/summary.json")
    manifest = load("manifest.json")
    for label, doc in (("summary.json", summary), ("manifest.json", manifest)):
        res = doc.get("results") or []
        check(len(res) == expect, f"{label}: {len(res)} results, expected {expect}")
        seen = [r.get("name") for r in res]
        check(len(set(seen)) == len(seen), f"{label}: duplicate test names")
        for r in res:
            n, st, log = r.get("name"), r.get("status"), r.get("log")
            check(bool(n), f"{label}: result without name")
            check(st in STATUSES, f"{label}: {n} status {st!r}")
            if check(bool(log), f"{label}: {n} has no log"):
                check(size(f"{run}/{log}") > 0, f"{label}: {n} log {run}/{log} missing/empty")
    check([r.get("name") for r in summary.get("results", [])] == [r.get("name") for r in manifest.get("results", [])],
          "summary.json and manifest.json result lists differ")

    # 4. driver-load.json: driver identity + load result, listed in the manifest.
    dl = load("driver-load.json")
    check(dl.get("source") in {"bundled", "imported", "downloaded", "system"}, f"driver-load.json: source {dl.get('source')!r}")
    sha = dl.get("soSha256")
    check(isinstance(sha, str) and len(sha) == 64, "driver-load.json: soSha256 missing/not 64 hex")
    check(isinstance(dl.get("loadSuccess"), bool), "driver-load.json: loadSuccess not bool")
    check("loadError" in dl and "driverInfo" in dl and "buildId" in dl and "kbaseUapi" in dl,
          "driver-load.json: missing loadError/driverInfo/buildId/kbaseUapi")
    if dl.get("loadSuccess") is False:
        check(bool(dl.get("loadError")), "driver-load.json: load failed but no loadError")
    check("driver-load.json" in {f.get("path") for f in manifest.get("files", [])}, "manifest.json: driver-load.json not listed")
    print(f"driver-load: source={dl.get('source')} loaded={dl.get('loadSuccess')} sha={str(sha)[:12]} "
          f"buildId={str(dl.get('buildId'))[:12]} kbase={dl.get('kbaseUapi')}")

    counts = {}
    for r in summary.get("results", []):
        counts[r.get("status")] = counts.get(r.get("status"), 0) + 1
    print(f"run {run}: {len(summary.get('results', []))} tests {counts}")
    return report(errs)


def report(errs):
    for e in errs:
        print("FAIL", e)
    print("verify_zip:", "FAIL" if errs else "OK")
    return 1 if errs else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
