#!/usr/bin/env python3
"""Check the three-way relation that makes hcs parameters actually reach a driver.

  device_info.hcs : deviceMatchAttr = "X"   (which config node to use)
  <periph>.hcs    : match_attr      = "X"   (where the values live)
  hdf.hcs         : #include the <periph>.hcs

Comments are stripped first: comments in this project quote the syntax, and
matching them produces phantom duplicates. Template defaults and framework
manager nodes are reported as warnings, not errors.

Usage: check_hcs.py <hdf_config_dir>
Exit 0 = no errors (warnings may be printed), 1 = errors, 2 = bad usage.
"""
import os
import re
import sys

ATTR = re.compile(r'\bmatch_attr\s*=\s*"([^"]+)"')
DEV = re.compile(r'\bdeviceMatchAttr\s*=\s*"([^"]*)"')
INC = re.compile(r'#include\s+"([^"]+)"')
COMMENT = re.compile(r'/\*.*?\*/', re.S)
LINE_COMMENT = re.compile(r'//[^\n]*')

errors, warnings = [], []


def clean(text):
    text = COMMENT.sub("", text)
    return LINE_COMMENT.sub("", text)


def split_template(text):
    """Return (real_text, template_text) by brace depth after a `template` token."""
    real, tmpl, depth, in_tmpl, buf = [], [], 0, False, ""
    for line in text.splitlines():
        if re.search(r'\btemplate\b.*\{', line):
            in_tmpl, depth = True, 0
        depth += line.count("{") - line.count("}")
        (tmpl if in_tmpl else real).append(line)
        if in_tmpl and depth <= 0:
            in_tmpl = False
    return "\n".join(real), "\n".join(tmpl)


def main():
    if len(sys.argv) != 2:
        print(__doc__)
        return 2
    root = sys.argv[1]
    if not os.path.isdir(root):
        print("not a directory: %s" % root)
        return 2

    attrs, devs, incs, files = {}, [], {}, []
    for dirpath, _, names in os.walk(root):
        for name in sorted(names):
            if not name.endswith(".hcs"):
                continue
            path = os.path.join(dirpath, name)
            files.append(path)
            text = clean(open(path, encoding="utf-8", errors="ignore").read())
            incs[path] = INC.findall(text)
            real, _tmpl = split_template(text)
            for m in ATTR.finditer(real):
                attrs.setdefault(m.group(1), []).append(path)
            for m in DEV.finditer(real):
                devs.append((m.group(1), path))

    for value, paths in attrs.items():
        if len(paths) > 1:
            errors.append('duplicate match_attr="%s" in %s' % (value, ", ".join(paths)))

    for value, path in devs:
        if value == "":
            warnings.append('empty deviceMatchAttr in %s (ok only if the driver hardcodes its parameters)' % path)
        elif value.endswith("_manager"):
            warnings.append('deviceMatchAttr="%s" has no config node (framework driver) - expected' % value)
        elif value not in attrs:
            errors.append('deviceMatchAttr="%s" (%s) has no match_attr anywhere' % (value, path))

    included = set()
    for path, names in incs.items():
        base = os.path.dirname(path)
        for name in names:
            target = os.path.normpath(os.path.join(base, name))
            included.add(target)
            if not os.path.exists(target):
                errors.append('#include %r in %s does not exist' % (name, path))

    for path, _ in attrs.items():
        pass
    for path in files:
        if os.path.basename(path) in ("hdf.hcs", "device_info.hcs"):
            continue
        if path in attrs.get("__never__", []):
            continue
        defines = any(path in paths for paths in attrs.values())
        if defines and path not in included:
            errors.append("%s defines match_attr but is not #included by hdf.hcs" % path)

    print("scanned %d hcs files, %d match_attr, %d deviceMatchAttr" % (len(files), len(attrs), len(devs)))
    for w in warnings:
        print("  WARN  " + w)
    if errors:
        print("PROBLEMS:")
        for e in errors:
            print("  ERROR " + e)
        return 1
    print("RESULT: pass" + (" (with warnings)" if warnings else ""))
    return 0


if __name__ == "__main__":
    sys.exit(main())
