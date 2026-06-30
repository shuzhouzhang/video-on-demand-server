#!/usr/bin/env python3
"""Check whether the backend exposes the route surface expected by the client.

This is intentionally source-based: it catches missing route registration before
you start the service or touch the database.
"""

from __future__ import annotations

import re
from pathlib import Path


EXPECTED_GET = {
    "/health",
    "/videos",
    "/videos/detail",
    "/videos/search",
    "/videos/play-url",
    "/videos/like-status",
    "/videos/watch-progress",
    "/videos/favorite-status",
    "/users/favorites",
    "/users/profile",
    "/users/videos",
    "/videos/comments",
    "/videos/barrages",
    "/admin/reviews",
    "/admin/users",
}

EXPECTED_POST = {
    "/login",
    "/login/email-code",
    "/login/email",
    "/logout",
    "/videos",
    "/videos/upload",
    "/videos/like",
    "/videos/unlike",
    "/videos/watch-progress",
    "/videos/favorite",
    "/videos/unfavorite",
    "/videos/comments",
    "/videos/barrages",
    "/users/profile",
    "/users/avatar",
    "/admin/reviews/action",
    "/admin/users/action",
}

IGNORED_POST = {
    # Development-only route enabled by VIDEO_ENABLE_SMOKE_CLEANUP for
    # tools/smoke_api.py --write-checks cleanup. It is not part of the client
    # contract and normal server startup does not expose it.
    "/__smoke-cleanup",
}


def extract_routes(source: str, method: str) -> set[str]:
    pattern = rf'server_\.{method}\("([^"]+)"'
    return set(re.findall(pattern, source))


def main() -> int:
    root = Path(__file__).resolve().parents[1]
    source = (root / "source" / "http_server.cc").read_text(encoding="utf-8")

    actual_get = extract_routes(source, "Get")
    actual_post = extract_routes(source, "Post")

    missing_get = sorted(EXPECTED_GET - actual_get)
    missing_post = sorted(EXPECTED_POST - actual_post)
    extra_get = sorted(actual_get - EXPECTED_GET)
    extra_post = sorted(actual_post - EXPECTED_POST - IGNORED_POST)

    print("GET routes:", ", ".join(sorted(actual_get)))
    print("POST routes:", ", ".join(sorted(actual_post)))
    print("MISSING_GET:", missing_get)
    print("MISSING_POST:", missing_post)
    print("EXTRA_GET:", extra_get)
    print("EXTRA_POST:", extra_post)
    print("IGNORED_POST:", sorted(actual_post & IGNORED_POST))

    if missing_get or missing_post:
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
