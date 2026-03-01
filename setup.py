from __future__ import annotations

import subprocess
import re
from pathlib import Path

from pybind11.setup_helpers import Pybind11Extension, build_ext
from setuptools import setup


def package_version() -> str:
    meta = Path("python/xsql/_meta.py").read_text(encoding="utf-8")
    match = re.search(r'__version__\s*=\s*"([0-9]+\.[0-9]+\.[0-9]+)"', meta)
    if not match:
        raise RuntimeError("Could not parse __version__ from python/xsql/_meta.py")
    return match.group(1)


PACKAGE_VERSION = package_version()


def git_commit_short() -> str:
    try:
        out = subprocess.check_output(["git", "rev-parse", "--short=12", "HEAD"], text=True)
        return out.strip() or "unknown"
    except Exception:
        return "unknown"


def git_dirty_flag() -> str:
    try:
        result = subprocess.call(["git", "diff", "--quiet", "--ignore-submodules", "HEAD", "--"])
        return "0" if result == 0 else "1"
    except Exception:
        return "0"


def core_sources() -> list[str]:
    sources = [
        "python/xsql/_core.cpp",
        "core/src/version.cpp",
        "core/src/lang/ast.cpp",
        "core/src/lang/diagnostics.cpp",
        "core/src/lang/markql_parser.cpp",
        "core/src/lang/parser/parser.cpp",
        "core/src/lang/parser/parser_expr.cpp",
        "core/src/lang/parser/parser_query.cpp",
        "core/src/lang/parser/parser_select.cpp",
        "core/src/lang/parser/parser_source.cpp",
        "core/src/lang/parser/parser_util.cpp",
        "core/src/lang/parser/lexer.cpp",
        "core/src/dom/html_parser.cpp",
        "core/src/dom/backend/parser_naive.cpp",
        "core/src/dom/backend/parser_libxml2.cpp",
        "core/src/runtime/executor/executor.cpp",
        "core/src/runtime/executor/filter.cpp",
        "core/src/runtime/executor/order.cpp",
        "core/src/util/string_util.cpp",
        "core/src/runtime/engine/execute.cpp",
        "core/src/runtime/engine/io.cpp",
        "core/src/runtime/engine/validation.cpp",
        "core/src/runtime/engine/result_builder.cpp",
        "core/src/runtime/engine/table_extract.cpp",
        "core/src/runtime/engine/tfidf.cpp",
    ]
    return sources


ext_modules = [
    Pybind11Extension(
        "xsql._core",
        core_sources(),
        include_dirs=[
            "core/include",
            "core/src",
            "core/src/lang",
            "core/src/dom",
            "core/src/runtime",
            "core/src/util",
        ],
        define_macros=[
            ("XSQL_VERSION", f'"{PACKAGE_VERSION}"'),
            ("XSQL_GIT_COMMIT", f'"{git_commit_short()}"'),
            ("XSQL_GIT_DIRTY", git_dirty_flag()),
        ],
        cxx_std=20,
    )
]


setup(
    ext_modules=ext_modules,
    cmdclass={"build_ext": build_ext},
)
