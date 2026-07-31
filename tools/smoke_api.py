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
import time
import urllib.parse
import urllib.request
import uuid
from typing import Any


def request_json(
    base_url: str,
    method: str,
    path: str,
    payload: dict[str, Any] | None = None,
    token: str = "",
) -> dict[str, Any] | list[Any]:
    url = urllib.parse.urljoin(base_url.rstrip("/") + "/", path.lstrip("/"))
    data = None
    headers = {"Accept": "application/json"}
    if token:
        headers["Authorization"] = f"Bearer {token}"
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
    token: str = "",
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
    headers = {
        "Accept": "application/json",
        "Content-Type": f"multipart/form-data; boundary={boundary}",
    }
    if token:
        headers["Authorization"] = f"Bearer {token}"
    request = urllib.request.Request(
        url,
        data=body.getvalue(),
        headers=headers,
        method="POST",
    )
    with urllib.request.urlopen(request, timeout=10) as response:
        return json.loads(response.read().decode("utf-8"))


def expect(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)
    print(f"[PASS] {message}")


def run_write_checks(
    base_url: str,
    token: str,
    video_bytes: bytes | None = None,
    wait_transcode_seconds: int = 0,
) -> None:
    marker = f"Codex smoke write check {uuid.uuid4().hex[:8]}"
    video_bytes = video_bytes or b"fake mp4 payload from smoke write checks"
    cover_bytes = b"fake jpg payload from smoke write checks"
    avatar_bytes = b"fake png payload from smoke write checks"
    uploaded_video_id = ""
    stored_video_path = ""
    stored_cover_path = ""
    avatar_path = ""
    previous_avatar_path = ""

    try:
        profile_before = request_json(
            base_url, "GET", "/users/profile?account=bit-user-001", token=token
        )
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
            token,
        )
        expect(isinstance(upload, dict) and upload.get("success") is True, "POST /videos/upload accepts video bytes")
        video = upload.get("video", {})
        uploaded_video_id = video.get("id", "")
        stored_video_path = video.get("storedVideoPath", "")
        stored_cover_path = video.get("storedCoverPath", "")
        play_url = video.get("playUrl", "")
        expect(bool(uploaded_video_id and play_url), "upload response returns video id and playUrl")
        expect(request_bytes(base_url, play_url) == video_bytes, "GET uploaded video bytes through /uploads")

        loaded_play_url = request_json(
            base_url, "GET",
            f"/videos/play-url?videoId={urllib.parse.quote(uploaded_video_id)}",
        )
        expect(
            isinstance(loaded_play_url, dict)
            and loaded_play_url.get("success") is False,
            "pending upload has no public play URL",
        )
        owner_videos = request_json(
            base_url, "GET", "/users/videos?account=bit-user-001", token=token
        )
        expect(
            isinstance(owner_videos, dict)
            and any(
                item.get("id") == uploaded_video_id
                for item in owner_videos.get("videos", [])
            ),
            "owner sees pending upload in /users/videos",
        )

        if wait_transcode_seconds > 0:
            deadline = time.monotonic() + wait_transcode_seconds
            last_status = ""
            while time.monotonic() < deadline:
                job = request_json(
                    base_url,
                    "GET",
                    f"/transcode/jobs?videoId={urllib.parse.quote(uploaded_video_id)}",
                    token=token,
                )
                last_status = str(job.get("data", {}).get("status", ""))
                if last_status == "SUCCEEDED":
                    break
                if last_status == "FAILED":
                    raise AssertionError(
                        f"RabbitMQ-driven transcode failed: {job.get('data', {})}"
                    )
                time.sleep(0.5)
            expect(
                last_status == "SUCCEEDED",
                "RabbitMQ event drives transcode job to SUCCEEDED",
            )

        avatar = multipart_request_json(
            base_url,
            "/users/avatar",
            {"account": "bit-user-001"},
            {"avatarFile": ("smoke-avatar.png", avatar_bytes, "image/png")},
            token,
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
        cleanup = request_json(
            base_url, "POST", "/__smoke-cleanup", cleanup_payload, token=token
        )
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
    parser.add_argument(
        "--transcode-video",
        help="use a real MP4 for write checks instead of the tiny fake payload",
    )
    parser.add_argument(
        "--wait-transcode-seconds",
        type=int,
        default=0,
        help="wait for the uploaded video's transcode job to succeed",
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
        token = login.get("token", "") if isinstance(login, dict) else ""

        videos = request_json(base_url, "GET", "/videos")
        expect(isinstance(videos, list) and len(videos) > 0, "GET /videos returns seed videos")
        video_id = videos[0]["id"]

        detail = request_json(base_url, "GET", f"/videos/detail?id={urllib.parse.quote(video_id)}")
        expect(isinstance(detail, dict) and detail.get("success") is True, "GET /videos/detail returns a video")

        play_url = request_json(base_url, "GET", f"/videos/play-url?videoId={urllib.parse.quote(video_id)}")
        expect(isinstance(play_url, dict) and play_url.get("success") is True, "GET /videos/play-url returns a URL")

        profile = request_json(
            base_url, "GET", "/users/profile?account=bit-user-001", token=token
        )
        expect(isinstance(profile, dict) and profile.get("success") is True, "GET /users/profile returns seed user")

        comments = request_json(
            base_url, "GET",
            f"/videos/comments?videoId={urllib.parse.quote(video_id)}",
            token=token,
        )
        expect(isinstance(comments, dict) and comments.get("success") is True, "GET /videos/comments is reachable")

        barrages = request_json(
            base_url, "GET",
            f"/videos/barrages?videoId={urllib.parse.quote(video_id)}",
            token=token,
        )
        expect(isinstance(barrages, dict) and barrages.get("success") is True, "GET /videos/barrages is reachable")

        admin_login = request_json(
            base_url, "POST", "/login",
            {"account": "admin@bit.com", "password": "123456"},
        )
        expect(
            isinstance(admin_login, dict) and admin_login.get("success") is True,
            "POST /login accepts seed administrator",
        )
        admin_token = admin_login.get("token", "") if isinstance(admin_login, dict) else ""
        admin_reviews = request_json(
            base_url, "GET", "/admin/reviews", token=admin_token
        )
        expect(isinstance(admin_reviews, dict) and admin_reviews.get("success") is True, "GET /admin/reviews is reachable")

        if args.write_checks:
            supplied_video = None
            if args.transcode_video:
                with open(args.transcode_video, "rb") as source:
                    supplied_video = source.read()
            run_write_checks(
                base_url,
                token,
                supplied_video,
                max(0, args.wait_transcode_seconds),
            )

        print(f"Smoke test passed against {base_url}")
        return 0
    except Exception as exc:  # noqa: BLE001 - CLI should print a concise failure.
        print(f"[FAIL] {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
