#!/usr/bin/env python3
"""
Cognitive complexity analyzer for C++ source files.

Scoring (SonarSource specification):
  Structural (+1 + nesting depth):  if, else-if, else, switch, for, for-range,
                                    while, do-while, catch, ternary (?:)
  Flat (+1, no nesting bonus):      each unbroken run of && or ||, goto
  Nesting-only (no own score):      lambda expressions

Thresholds (default):
  OK < 16  |  WARNING 16–25  |  CRITICAL >= 26

Exit code: 0 = no critical violations, 1 = one or more critical violations

Output formats (--format):
  text   Human-readable aligned table (default)
  json   JSON array — one object per function
  grep   Machine-readable, one line per function: file:line:score:STATUS:name

Usage:
  python3 tools/cognitive_complexity.py src/
  python3 tools/cognitive_complexity.py src/simulation/CitySimulation.cpp
  python3 tools/cognitive_complexity.py --only-violations src/
  python3 tools/cognitive_complexity.py --sort score src/
  python3 tools/cognitive_complexity.py --format json src/ > results.json
  python3 tools/cognitive_complexity.py --format grep src/ | grep CRITICAL
"""

import argparse
import json
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import List, Optional

import tree_sitter_cpp as tscpp
from tree_sitter import Language, Parser, Node

_LANGUAGE = Language(tscpp.language())
_PARSER   = Parser(_LANGUAGE)

DEFAULT_WARN     = 16
DEFAULT_CRITICAL = 26


# ── Data ─────────────────────────────────────────────────────────────────────

@dataclass
class FunctionScore:
    name:  str
    file:  str
    line:  int
    score: int


# ── Node helpers ─────────────────────────────────────────────────────────────

def _text(node: Node) -> str:
    return node.text.decode("utf-8") if node.text else ""


def _bool_op(node: Optional[Node]) -> Optional[str]:
    """Return '&&' or '||' if node is a bool binary_expression, else None."""
    if node is None or node.type != "binary_expression":
        return None
    op = node.child_by_field_name("operator")
    s  = _text(op) if op else ""
    return s if s in ("&&", "||") else None


def _collect_bool_ops(node: Node) -> List[str]:
    """
    Flatten the chain of &&/|| operators rooted at node into an ordered list.
    Stops at non-bool-binary-expression boundaries, so parenthesized
    sub-expressions are treated as separate chains.
    """
    if _bool_op(node) is None:
        return []
    left  = node.child_by_field_name("left")
    right = node.child_by_field_name("right")
    op    = node.child_by_field_name("operator")
    ops: List[str] = []
    if left:  ops.extend(_collect_bool_ops(left))
    if op:    ops.append(_text(op))
    if right: ops.extend(_collect_bool_ops(right))
    return ops


def _bool_groups(node: Node) -> int:
    """Count unbroken same-operator runs (each run = +1 flat increment)."""
    ops = _collect_bool_ops(node)
    if not ops:
        return 0
    groups = 1
    for i in range(1, len(ops)):
        if ops[i] != ops[i - 1]:
            groups += 1
    return groups


def _is_bool_root(node: Node) -> bool:
    """True when this &&/|| expression is the root of its operator chain."""
    return _bool_op(node.parent) is None


def _same(a: Optional[Node], b: Optional[Node]) -> bool:
    """True if two nodes cover the same source bytes."""
    return (a is not None and b is not None
            and a.start_byte == b.start_byte
            and a.end_byte   == b.end_byte)


def _function_name(fn: Node) -> str:
    """Walk the declarator chain to extract a readable function name."""
    decl = fn.child_by_field_name("declarator")
    while decl is not None:
        if decl.type == "function_declarator":
            inner = decl.child_by_field_name("declarator")
            return _text(inner).split("(")[0].strip() if inner else "<unnamed>"
        nxt = decl.child_by_field_name("declarator")
        if nxt is None:
            return _text(decl).split("(")[0].strip() or "<unnamed>"
        decl = nxt
    return "<unnamed>"


# ── Complexity visitor ────────────────────────────────────────────────────────

class _Visitor:
    """Walk a function body AST and accumulate cognitive complexity."""

    def __init__(self) -> None:
        self.score = 0

    def visit(self, node: Node, nesting: int) -> None:
        t = node.type

        # ── if / else if / else ──────────────────────────────────────────────
        if t == "if_statement":
            self._visit_if(node, nesting, is_else_if=False)

        # ── loops ────────────────────────────────────────────────────────────
        elif t in ("for_statement", "for_range_loop",
                   "while_statement", "do_statement"):
            self.score += 1 + nesting
            body = node.child_by_field_name("body")
            for child in node.children:
                self.visit(child, nesting + 1 if _same(child, body) else nesting)

        # ── switch ───────────────────────────────────────────────────────────
        elif t == "switch_statement":
            self.score += 1 + nesting
            body = node.child_by_field_name("body")
            for child in node.children:
                self.visit(child, nesting + 1 if _same(child, body) else nesting)

        # ── catch ────────────────────────────────────────────────────────────
        elif t == "catch_clause":
            self.score += 1 + nesting
            body = node.child_by_field_name("body")
            for child in node.children:
                self.visit(child, nesting + 1 if _same(child, body) else nesting)

        # ── ternary ──────────────────────────────────────────────────────────
        elif t == "conditional_expression":
            self.score += 1 + nesting
            cond = node.child_by_field_name("condition")
            cons = node.child_by_field_name("consequence")
            alt  = node.child_by_field_name("alternative")
            if cond: self.visit(cond, nesting)
            if cons: self.visit(cons, nesting + 1)
            if alt:  self.visit(alt,  nesting + 1)

        # ── lambda (nesting-only, no own score) ──────────────────────────────
        elif t == "lambda_expression":
            body = node.child_by_field_name("body")
            for child in node.children:
                self.visit(child, nesting + 1 if _same(child, body) else nesting)

        # ── boolean sequences ────────────────────────────────────────────────
        elif t == "binary_expression":
            if _bool_op(node) and _is_bool_root(node):
                self.score += _bool_groups(node)
            # Recurse through the bool chain to catch nested control flow
            self._descend_bool_chain(node, nesting)

        # ── goto ─────────────────────────────────────────────────────────────
        elif t == "goto_statement":
            self.score += 1

        # ── nested function/method — analyzed separately ──────────────────────
        elif t == "function_definition":
            pass

        # ── default: recurse ─────────────────────────────────────────────────
        else:
            for child in node.children:
                self.visit(child, nesting)

    def _visit_if(self, node: Node, nesting: int, is_else_if: bool) -> None:
        # else-if: flat +1.  Regular if: +1 + nesting.
        self.score += 1 if is_else_if else 1 + nesting

        cond = node.child_by_field_name("condition")
        cons = node.child_by_field_name("consequence")
        alt  = node.child_by_field_name("alternative")

        if cond: self.visit(cond, nesting)        # condition at current nesting
        if cons: self.visit(cons, nesting + 1)    # body at nesting+1

        if alt and alt.type == "else_clause":
            # First named child: if_statement → else-if; anything else → else body
            inner = next((c for c in alt.children if c.is_named), None)
            if inner:
                if inner.type == "if_statement":
                    self._visit_if(inner, nesting, is_else_if=True)
                else:
                    self.score += 1                   # plain else: flat +1
                    self.visit(inner, nesting + 1)    # else body at nesting+1

    def _descend_bool_chain(self, node: Node, nesting: int) -> None:
        """
        Walk a boolean chain, recursing into non-bool children normally so that
        any embedded control flow (e.g. lambdas in call arguments) is counted.
        """
        for child in node.children:
            if _bool_op(child) is not None:
                self._descend_bool_chain(child, nesting)
            else:
                self.visit(child, nesting)


# ── File analysis ─────────────────────────────────────────────────────────────

def _all_function_nodes(root: Node) -> List[Node]:
    """Iterative pre-order walk; returns every function_definition node."""
    results: List[Node] = []
    stack = [root]
    while stack:
        n = stack.pop()
        if n.type == "function_definition":
            results.append(n)
        # Push children in reverse so left-to-right order is preserved
        stack.extend(reversed(list(n.children)))
    return results


def analyze_file(path: str) -> List[FunctionScore]:
    try:
        source = Path(path).read_bytes()
    except OSError as e:
        print(f"warning: cannot read {path}: {e}", file=sys.stderr)
        return []

    tree   = _PARSER.parse(source)
    scores: List[FunctionScore] = []

    for fn_node in _all_function_nodes(tree.root_node):
        body = fn_node.child_by_field_name("body")
        if body is None:
            continue
        v = _Visitor()
        v.visit(body, nesting=0)
        scores.append(FunctionScore(
            name  = _function_name(fn_node),
            file  = path,
            line  = fn_node.start_point[0] + 1,
            score = v.score,
        ))

    return scores


def analyze_path(root: str) -> List[FunctionScore]:
    p = Path(root)
    if p.is_file():
        return analyze_file(str(p))
    results: List[FunctionScore] = []
    for ext in ("*.cpp", "*.h", "*.cc", "*.cxx", "*.hpp"):
        for f in sorted(p.rglob(ext)):
            results.extend(analyze_file(str(f)))
    return results


# ── Output ────────────────────────────────────────────────────────────────────

def _label(score: int, warn: int, critical: int) -> str:
    if score >= critical: return "CRITICAL"
    if score >= warn:     return "WARN"
    return "OK"


def _print_text(visible: List[FunctionScore], warn: int, critical: int) -> None:
    if not visible:
        return
    w_loc  = max(len(f"{f.file}:{f.line}") for f in visible)
    w_name = max(len(f.name)               for f in visible)
    for f in visible:
        loc   = f"{f.file}:{f.line}"
        label = _label(f.score, warn, critical)
        print(f"  {loc:<{w_loc}}  {f.name:<{w_name}}  {f.score:>4}  [{label}]")


def _print_json(visible: List[FunctionScore], warn: int, critical: int) -> None:
    data = [
        {
            "file":   f.file,
            "line":   f.line,
            "name":   f.name,
            "score":  f.score,
            "status": _label(f.score, warn, critical),
        }
        for f in visible
    ]
    print(json.dumps(data, indent=2))


def _print_grep(visible: List[FunctionScore], warn: int, critical: int) -> None:
    # file:line:score:STATUS:name  — easy to grep, cut, sort
    for f in visible:
        label = _label(f.score, warn, critical)
        print(f"{f.file}:{f.line}:{f.score}:{label}:{f.name}")


# ── CLI ───────────────────────────────────────────────────────────────────────

def main() -> int:
    ap = argparse.ArgumentParser(
        description="Cognitive complexity analyzer for C++ source files.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__,
    )
    ap.add_argument("paths", nargs="+", metavar="path",
                    help="File or directory to analyze")
    ap.add_argument("--warn", type=int, default=DEFAULT_WARN, metavar="N",
                    help=f"Warning threshold, inclusive (default {DEFAULT_WARN})")
    ap.add_argument("--critical", type=int, default=DEFAULT_CRITICAL, metavar="N",
                    help=f"Critical threshold, inclusive (default {DEFAULT_CRITICAL})")
    ap.add_argument("--only-violations", action="store_true",
                    help="Show only WARN and CRITICAL functions")
    ap.add_argument("--min-score", type=int, default=0, metavar="N",
                    help="Show only functions with score >= N")
    ap.add_argument("--sort", choices=("score", "file", "name"), default="file",
                    help="Sort order: score (desc), file (default), name (alpha)")
    ap.add_argument("--format", choices=("text", "json", "grep"), default="text",
                    help="Output format: text (default), json, grep")
    args = ap.parse_args()

    all_scores: List[FunctionScore] = []
    for path in args.paths:
        all_scores.extend(analyze_path(path))

    # Sort
    if args.sort == "score":
        all_scores.sort(key=lambda f: -f.score)
    elif args.sort == "name":
        all_scores.sort(key=lambda f: f.name.lower())
    # default "file": already returned in file/line order

    # Filter
    visible = [
        f for f in all_scores
        if f.score >= args.min_score
        and (not args.only_violations or f.score >= args.warn)
    ]

    # Emit
    if args.format == "json":
        _print_json(visible, args.warn, args.critical)
    elif args.format == "grep":
        _print_grep(visible, args.warn, args.critical)
    else:
        _print_text(visible, args.warn, args.critical)

    # Summary (always to stdout unless json, where it would break parsing)
    n_warn     = sum(1 for f in all_scores if args.warn <= f.score < args.critical)
    n_critical = sum(1 for f in all_scores if f.score >= args.critical)
    n_ok       = len(all_scores) - n_warn - n_critical

    if args.format != "json":
        print()
        print(f"Functions: {len(all_scores)}  OK: {n_ok}"
              f"  WARN(>={args.warn}): {n_warn}"
              f"  CRITICAL(>={args.critical}): {n_critical}")

    return 1 if n_critical > 0 else 0


if __name__ == "__main__":
    sys.exit(main())
