#!/usr/bin/env python3
import json
import os
import socket
import subprocess
import tempfile
import time
import urllib.error
import urllib.request
from pathlib import Path


def free_port() -> int:
    with socket.socket() as listener:
        listener.bind(("127.0.0.1", 0))
        return listener.getsockname()[1]


def response(url: str, authorization: str | None = None) -> tuple[int, dict]:
    headers = {"Content-Type": "application/json"}
    if authorization is not None:
        headers["Authorization"] = authorization
    request = urllib.request.Request(
        url, data=b'{"videoId":"video-002","account":"alice"}',
        headers=headers, method="POST")
    try:
        with urllib.request.urlopen(request, timeout=3) as result:
            return result.status, json.loads(result.read())
    except urllib.error.HTTPError as error:
        return error.code, json.loads(error.read())


def run_case(redis_port: int, assertions) -> None:
    gateway_port = free_port()
    with tempfile.TemporaryDirectory(prefix="vod-gateway-auth-") as directory:
        gateway = os.path.join(directory, "gateway.json")
        services = os.path.join(directory, "services.json")
        with open(gateway, "w", encoding="utf-8") as output:
            json.dump({"server": {"port": gateway_port}}, output)
        with open(services, "w", encoding="utf-8") as output:
            json.dump({
                "user_service": "http://127.0.0.1:1",
                "video_service": "http://127.0.0.1:1",
                "file_service": "http://127.0.0.1:1",
                "transcode_service": "http://127.0.0.1:1",
                "timeout_ms": 200,
                "redis": {
                    "enabled": True,
                    "host": "127.0.0.1",
                    "port": redis_port,
                    "session_ttl_seconds": 60,
                },
            }, output)
        executable = Path(__file__).resolve().parents[2] / "api_gateway"
        process = subprocess.Popen(
            [str(executable), gateway, services],
            stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        try:
            health = f"http://127.0.0.1:{gateway_port}/healthz"
            for _ in range(50):
                try:
                    with urllib.request.urlopen(health, timeout=0.2):
                        break
                except (OSError, urllib.error.URLError):
                    time.sleep(0.1)
            else:
                raise AssertionError("gateway did not become healthy")
            assertions(f"http://127.0.0.1:{gateway_port}/videos/like")
        finally:
            process.terminate()
            try:
                process.wait(timeout=3)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait(timeout=3)


def main() -> None:
    redis_port = int(os.environ.get("VIDEO_TEST_REDIS_PORT", "6379"))

    def live_redis(url: str) -> None:
        missing_header = response(url)
        malformed_header = response(url, "Basic invalid")
        missing_session = response(url, "Bearer definitely-missing-token")
        assert missing_header == (
            401, {"message": "invalid authorization header", "success": False})
        assert malformed_header == missing_header
        assert missing_session == (
            401, {"message": "invalid or expired session token", "success": False})
        print("[PASS] missing/malformed Authorization and missing session return 401")

    def unavailable_redis(url: str) -> None:
        assert response(url, "Bearer syntactically-valid-token") == (
            503, {"message": "token verification unavailable", "success": False})
        print("[PASS] Redis lookup failure returns 503")

    run_case(redis_port, live_redis)
    run_case(1, unavailable_redis)


if __name__ == "__main__":
    main()
