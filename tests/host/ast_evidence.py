"""Stable project-owned AST evidence for host source contracts."""

import ast
import hashlib
import json


def _new_field_is_default(node):
    type_params = getattr(node, "type_params", ())
    if type_params:
        raise AssertionError("unreviewed nonempty type parameters")
    is_lazy = getattr(node, "is_lazy", 0)
    if is_lazy != 0:
        raise AssertionError("unreviewed lazy import")


def ast_dump(node):
    """Serialize an AST with the complete pre-3.15 field representation."""

    if not isinstance(node, ast.AST):
        raise TypeError(f"expected AST, got {type(node).__name__!r}")

    def format_value(value):
        if isinstance(value, ast.AST):
            _new_field_is_default(value)
            fields = []
            for name in value._fields:
                if name in ("type_params", "is_lazy"):
                    continue
                try:
                    child = getattr(value, name)
                except AttributeError:
                    continue
                if child is None and getattr(type(value), name, ...) is None:
                    continue
                fields.append(f"{name}={format_value(child)}")
            return f"{type(value).__name__}({', '.join(fields)})"
        if isinstance(value, list):
            return f"[{', '.join(format_value(child) for child in value)}]"
        return repr(value)

    return format_value(node)


def ast_key(value):
    """Return the stable semantic tuple used by source-contract comparisons."""

    if isinstance(value, ast.AST):
        _new_field_is_default(value)
        if type(value).__name__ == "Index":
            return ast_key(value.value)
        return (
            type(value).__name__,
            tuple(
                (name, ast_key(child))
                for name, child in ast.iter_fields(value)
                if name not in ("type_params", "is_lazy")
            ),
        )
    if isinstance(value, (list, tuple)):
        return tuple(ast_key(child) for child in value)
    if isinstance(value, bytes):
        return ("bytes", value.hex())
    if value is Ellipsis:
        return ("ellipsis",)
    return value


def ast_digest(node):
    encoded = json.dumps(ast_key(node), separators=(",", ":"), ensure_ascii=True)
    return hashlib.sha256(encoded.encode("ascii")).hexdigest()
