#!/usr/bin/env python3
"""Envoy's JSON-transcoder service list must equal the services in the descriptor set.

@author Olumuyiwa Oluwasanmi

`backend/envoy.yaml` names the services the grpc_json_transcoder translates, and
`api_descriptor.pb` (built by protoc from backend/proto/*.proto) is what Envoy resolves
those names against. The two are edited in different files and nothing connects them:

  - A service NAMED in envoy.yaml and ABSENT from the descriptor makes Envoy refuse the
    whole configuration at startup, before it binds :8080. /healthz is answered by Envoy
    itself, so it never responds and the deploy fails its healthcheck while the previous
    deployment keeps serving (backend/Dockerfile repeats this check at image-build time).
  - A service in the descriptor and NOT named in envoy.yaml is not fatal and is worse:
    its JSON route silently does not exist, and only a caller notices.

So the two sets must be EQUAL, and this fails on either difference. Removing a service
from the engine is exactly the edit that breaks the first case, which is why the check
exists: the proto list in CMake and the services list in envoy.yaml have to move in the
same commit, and a reviewer cannot see the descriptor.

The descriptor is decoded with the SAME protoc that built it, so no Python protobuf
package is required.

USAGE
  check_envoy_services.py <envoy.yaml> <api_descriptor.pb> <protoc> <protobuf-src-dir>
  check_envoy_services.py --self-test
"""

from __future__ import annotations

import re
import subprocess
import sys
from pathlib import Path

_SERVICES_BLOCK = re.compile(
    r"^(?P<indent>[ \t]*)services:[ \t]*\n(?P<items>(?:(?P=indent)[ \t]+-[ \t]*[\w.]+[ \t]*(?:#.*)?\n)+)",
    re.MULTILINE,
)
_ITEM = re.compile(r"-[ \t]*([\w.]+)")


def envoy_services(text: str) -> set[str]:
    """The services named under every grpc_json_transcoder filter."""
    out: set[str] = set()
    for chunk in text.split("grpc_json_transcoder")[1:]:
        block = _SERVICES_BLOCK.search(chunk)
        if block:
            out.update(_ITEM.findall(block.group("items")))
    return out


def descriptor_services(decoded: str) -> set[str]:
    """`package.Service` for every service in a protoc text-format FileDescriptorSet."""
    out: set[str] = set()
    stack: list[str] = []
    package = ""
    names: list[str] = []
    for raw in decoded.splitlines():
        line = raw.strip()
        if line.endswith("{"):
            stack.append(line[:-1].strip())
        elif line == "}":
            if stack == ["file"]:
                out.update(f"{package}.{n}" if package else n for n in names)
                package, names = "", []
            stack.pop()
        elif stack == ["file"] and line.startswith("package:"):
            package = line.split('"')[1]
        elif stack == ["file", "service"] and line.startswith("name:"):
            names.append(line.split('"')[1])
    return out


def decode_descriptor(descriptor: Path, protoc: str, protobuf_src: str) -> str:
    with descriptor.open("rb") as stdin:
        done = subprocess.run(
            [protoc, "--decode=google.protobuf.FileDescriptorSet", f"-I{protobuf_src}",
             "google/protobuf/descriptor.proto"],
            stdin=stdin, capture_output=True, text=True, check=False)
    if done.returncode != 0:
        sys.exit(f"FAIL: protoc could not decode {descriptor}: {done.stderr.strip()}")
    return done.stdout


def verdict(envoy: set[str], descriptor: set[str]) -> list[str]:
    """Every reason the pair is wrong; empty means equal and non-empty."""
    problems: list[str] = []
    if not envoy or not descriptor:
        problems.append(f"compared NOTHING (envoy {len(envoy)}, descriptor {len(descriptor)}) -- "
                        "a parser that finds no services reports a pass on a broken tree")
    problems += [f"envoy.yaml names {s}, which the descriptor does not contain: Envoy refuses "
                 "the configuration at startup and the deploy fails its healthcheck" for s in sorted(envoy - descriptor)]
    problems += [f"the descriptor contains {s}, which envoy.yaml does not list: its JSON route "
                 "does not exist" for s in sorted(descriptor - envoy)]
    return problems


_YAML = """\
filters:
  - name: envoy.filters.http.grpc_json_transcoder
    typed_config:
      services:
        - a.One   # trailing comment
        - b.Two
      auto_mapping: true
"""
_DECODED = """\
file {
  name: "a.proto"
  package: "a"
  message_type {
    name: "Not"
  }
  service {
    name: "One"
    method {
      name: "M"
    }
  }
}
file {
  name: "b.proto"
  package: "b"
  service {
    name: "Two"
  }
}
"""


def self_test() -> int:
    """The parsers read what they should, and the verdict can fail in each direction."""
    failures = 0

    def check(ok: bool, what: str) -> None:
        nonlocal failures
        print(f"  {'ok  ' if ok else 'FAIL'}  {what}")
        failures += 0 if ok else 1

    yaml_set, desc_set = envoy_services(_YAML), descriptor_services(_DECODED)
    check(yaml_set == {"a.One", "b.Two"}, "envoy parser reads both services past a trailing comment")
    check(desc_set == {"a.One", "b.Two"}, "descriptor parser reads package-qualified names, not messages or methods")
    check(verdict(yaml_set, desc_set) == [], "equal sets pass")
    check(len(verdict(yaml_set | {"c.Gone"}, desc_set)) == 1, "a service envoy names and the descriptor lacks FAILS")
    check(len(verdict(yaml_set, desc_set | {"c.New"})) == 1, "a service the descriptor has and envoy omits FAILS")
    check(len(verdict(set(), desc_set)) >= 1, "an empty envoy list FAILS rather than passing on zero")
    check(envoy_services("nothing here") == set(), "no transcoder filter yields the empty set")
    print(f"{failures} failure(s)")
    return 1 if failures else 0


def main(argv: list[str]) -> int:
    if argv == ["--self-test"]:
        return self_test()
    if len(argv) != 4:
        sys.exit(__doc__)
    envoy_path, descriptor_path, protoc, protobuf_src = argv
    if self_test() != 0:
        return 1
    envoy = envoy_services(Path(envoy_path).read_text())
    descriptor = descriptor_services(decode_descriptor(Path(descriptor_path), protoc, protobuf_src))
    problems = verdict(envoy, descriptor)
    for p in problems:
        print(f"FAIL: {p}")
    print(f"envoy.yaml: {sorted(envoy)}\ndescriptor: {sorted(descriptor)}")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
