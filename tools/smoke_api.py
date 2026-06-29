#!/usr/bin/env python3
"""Run a small live smoke test against video-on-demand-server.

The default checks are read-mostly and safe to run repeatedly. They verify that
the service is reachable, seed data is available, and the main client-facing
query endpoints return the expected JSON shape.
"""

from __future__ import annotations

import argparse
import json
import sys
import urllib.parse
import urllib.request
from typing import Any


def request_json(base_url: str, method: str, path: str, payload: dict[str, Any] | None = None) -> dict[str, Any] | list[Any]:
    url = urllib.parse.urljoin(base_url.rstrip("/") + "/", path.lstrip("/"))
    data = None
    headers = {"Accept": "application/json"}
    if payload is not None:
        data = json.dumps(payload, ensure_ascii=False).encode("utf-8")
        headers["Content-Type"] = "application/json; charset=utf-8"
    request = urllib.request.Request(url, data=data, headers=headers, method=method)
    with urllib.request.urlopen(request, timeout=5) as response:
        body = response.read().decode("utf-8")
        return json.loads(body)


def expect(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)
    print(f"[PASS] {message}")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--base-url",
        default="http://127.0.0.1:9000",
        help="server base URL, for example http://192.168.19.129:9000",
    )
    args = parser.parse_args()

    base_url = args.base_url
    try:
        health = request_json(base_url, "GET", "/health")
        expect(isinstance(health, dict) and health.get("code") == 0, "GET /health is healthy")

        login = request_json(
            base_url,
            "POST",
            "/login",
            {"account": "bit-user-001", "password": "123456"},
        )
        expect(isinstance(login, dict) and login.get("success") is True, "POST /login accepts seed user")

        videos = request_json(base_url, "GET", "/videos")
        expect(isinstance(videos, list) and len(videos) > 0, "GET /videos returns seed videos")
        video_id = videos[0]["id"]

        detail = request_json(base_url, "GET", f"/videos/detail?id={urllib.parse.quote(video_id)}")
        expect(isinstance(detail, dict) and detail.get("success") is True, "GET /videos/detail returns a video")

        play_url = request_json(base_url, "GET", f"/videos/play-url?videoId={urllib.parse.quote(video_id)}")
        expect(isinstance(play_url, dict) and play_url.get("success") is True, "GET /videos/play-url returns a URL")

        profile = request_json(base_url, "GET", "/users/profile?account=bit-user-001")
        expect(isinstance(profile, dict) and profile.get("success") is True, "GET /users/profile returns seed user")

        comments = request_json(base_url, "GET", f"/videos/comments?videoId={urllib.parse.quote(video_id)}")
        expect(isinstance(comments, dict) and comments.get("success") is True, "GET /videos/comments is reachable")

        barrages = request_json(base_url, "GET", f"/videos/barrages?videoId={urllib.parse.quote(video_id)}")
        expect(isinstance(barrages, dict) and barrages.get("success") is True, "GET /videos/barrages is reachable")

        admin_reviews = request_json(base_url, "GET", "/admin/reviews")
        expect(isinstance(admin_reviews, dict) and admin_reviews.get("success") is True, "GET /admin/reviews is reachable")

        print(f"Smoke test passed against {base_url}")
        return 0
    except Exception as exc:  # noqa: BLE001 - CLI should print a concise failure.
        print(f"[FAIL] {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
