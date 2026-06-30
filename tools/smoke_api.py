#!/usr/bin/env python3
"""Run a small live smoke test against video-on-demand-server.

The default checks are read-mostly and safe to run repeatedly. They verify that
the service is reachable, seed data is available, and the main client-facing
query endpoints return the expected JSON shape.
"""

from __future__ import annotations

import argparse
import io
import json
import sys
import urllib.parse
import urllib.request
import uuid
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


def request_bytes(base_url: str, path_or_url: str) -> bytes:
    if path_or_url.startswith("http://") or path_or_url.startswith("https://"):
        url = path_or_url
    else:
        url = urllib.parse.urljoin(base_url.rstrip("/") + "/", path_or_url.lstrip("/"))
    request = urllib.request.Request(url, headers={"Accept": "*/*"}, method="GET")
    with urllib.request.urlopen(request, timeout=5) as response:
        return response.read()


def multipart_request_json(
    base_url: str,
    path: str,
    fields: dict[str, str],
    files: dict[str, tuple[str, bytes, str]],
) -> dict[str, Any]:
    boundary = f"----vod-smoke-{uuid.uuid4().hex}"
    body = io.BytesIO()
    for name, value in fields.items():
        body.write(f"--{boundary}\r\n".encode("utf-8"))
        body.write(f'Content-Disposition: form-data; name="{name}"\r\n\r\n'.encode("utf-8"))
        body.write(value.encode("utf-8"))
        body.write(b"\r\n")
    for name, (filename, content, content_type) in files.items():
        body.write(f"--{boundary}\r\n".encode("utf-8"))
        body.write(
            (
                f'Content-Disposition: form-data; name="{name}"; '
                f'filename="{filename}"\r\n'
                f"Content-Type: {content_type}\r\n\r\n"
            ).encode("utf-8")
        )
        body.write(content)
        body.write(b"\r\n")
    body.write(f"--{boundary}--\r\n".encode("utf-8"))

    url = urllib.parse.urljoin(base_url.rstrip("/") + "/", path.lstrip("/"))
    request = urllib.request.Request(
        url,
        data=body.getvalue(),
        headers={
            "Accept": "application/json",
            "Content-Type": f"multipart/form-data; boundary={boundary}",
        },
        method="POST",
    )
    with urllib.request.urlopen(request, timeout=10) as response:
        return json.loads(response.read().decode("utf-8"))


def expect(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)
    print(f"[PASS] {message}")


def run_write_checks(base_url: str) -> None:
    marker = f"Codex smoke write check {uuid.uuid4().hex[:8]}"
    video_bytes = b"fake mp4 payload from smoke write checks"
    cover_bytes = b"fake jpg payload from smoke write checks"
    avatar_bytes = b"fake png payload from smoke write checks"
    uploaded_video_id = ""
    stored_video_path = ""
    stored_cover_path = ""
    avatar_path = ""
    previous_avatar_path = ""

    try:
        profile_before = request_json(base_url, "GET", "/users/profile?account=bit-user-001")
        if isinstance(profile_before, dict):
            previous_avatar_path = profile_before.get("user", {}).get("avatarPath", "")

        metadata = {
            "title": marker,
            "account": "bit-user-001",
            "userName": "BIT 用户",
            "category": "科技",
            "tags": ["smoke", "upload"],
            "description": "smoke write checks",
            "videoFileName": "smoke-video.mp4",
            "coverFileName": "smoke-cover.jpg",
        }
        upload = multipart_request_json(
            base_url,
            "/videos/upload",
            {"metadata": json.dumps(metadata, ensure_ascii=False)},
            {
                "videoFile": ("smoke-video.mp4", video_bytes, "video/mp4"),
                "coverFile": ("smoke-cover.jpg", cover_bytes, "image/jpeg"),
            },
        )
        expect(isinstance(upload, dict) and upload.get("success") is True, "POST /videos/upload accepts video bytes")
        video = upload.get("video", {})
        uploaded_video_id = video.get("id", "")
        stored_video_path = video.get("storedVideoPath", "")
        stored_cover_path = video.get("storedCoverPath", "")
        play_url = video.get("playUrl", "")
        expect(bool(uploaded_video_id and play_url), "upload response returns video id and playUrl")
        expect(request_bytes(base_url, play_url) == video_bytes, "GET uploaded video bytes through /uploads")

        loaded_play_url = request_json(base_url, "GET", f"/videos/play-url?videoId={urllib.parse.quote(uploaded_video_id)}")
        expect(
            isinstance(loaded_play_url, dict)
            and loaded_play_url.get("success") is True
            and loaded_play_url.get("playUrl") == play_url,
            "GET /videos/play-url returns uploaded playUrl",
        )

        avatar = multipart_request_json(
            base_url,
            "/users/avatar",
            {"account": "bit-user-001"},
            {"avatarFile": ("smoke-avatar.png", avatar_bytes, "image/png")},
        )
        expect(isinstance(avatar, dict) and avatar.get("success") is True, "POST /users/avatar accepts avatar bytes")
        avatar_path = avatar.get("avatarPath", "")
        expect(bool(avatar_path), "avatar upload returns avatarPath")
        expect(request_bytes(base_url, avatar_path) == avatar_bytes, "GET uploaded avatar bytes through /uploads")
    finally:
        cleanup_payload = {
            "videoId": uploaded_video_id,
            "videoTitle": marker,
            "account": "bit-user-001",
            "storedVideoPath": stored_video_path,
            "storedCoverPath": stored_cover_path,
            "avatarPath": avatar_path,
            "previousAvatarPath": previous_avatar_path,
        }
        cleanup = request_json(base_url, "POST", "/__smoke-cleanup", cleanup_payload)
        if isinstance(cleanup, dict) and cleanup.get("success") is True:
            print("[PASS] smoke write-check data cleaned")
        else:
            print(f"[WARN] cleanup did not complete: {cleanup}", file=sys.stderr)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--base-url",
        default="http://127.0.0.1:9000",
        help="server base URL, for example http://192.168.19.129:9000",
    )
    parser.add_argument(
        "--write-checks",
        action="store_true",
        help="also verify upload/avatar/static-resource write flows and clean up test data",
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

        if args.write_checks:
            run_write_checks(base_url)

        print(f"Smoke test passed against {base_url}")
        return 0
    except Exception as exc:  # noqa: BLE001 - CLI should print a concise failure.
        print(f"[FAIL] {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
