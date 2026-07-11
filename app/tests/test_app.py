#!/usr/bin/env python3
from __future__ import annotations

import os
import sqlite3
import socket
import struct
import subprocess
import sys
import tempfile
import textwrap
import wave
from pathlib import Path


REPO = Path(__file__).resolve().parents[2]
APP = REPO / "build" / "r1_audiobook_app"
ZIG = Path("/home/yetisoldier/tools/zig/zig")


def run(cmd: list[str], **kwargs) -> subprocess.CompletedProcess[str]:
    return subprocess.run(cmd, check=True, text=True, capture_output=True, **kwargs)


def build_app() -> None:
    run(["sh", "app/build.sh"], cwd=REPO)


def write_wav(path: Path, seconds: float = 0.25, rate: int = 44100) -> None:
    samples = int(seconds * rate)
    with wave.open(str(path), "wb") as wf:
        wf.setnchannels(2)
        wf.setsampwidth(2)
        wf.setframerate(rate)
        frames = bytearray()
        for i in range(samples):
            value = int((i % 200) * 150)
            frames += struct.pack("<hh", value, value)
        wf.writeframes(bytes(frames))


def test_schema_and_scanner() -> None:
    with tempfile.TemporaryDirectory() as tmp:
        tmpdir = Path(tmp)
        library_root = tmpdir / "Audiobooks"
        book_dir = library_root / "Demo Book"
        book_dir.mkdir(parents=True)
        write_wav(book_dir / "01 Intro.wav")
        write_wav(book_dir / "02 Chapter.wav")

        db_path = tmpdir / "library.db"
        env = os.environ.copy()
        env["AUDIOBOOK_LIBRARY_ROOT"] = str(library_root)
        env["AUDIOBOOK_DB_PATH"] = str(db_path)
        env["AUDIOBOOK_APP_ROOT"] = str(tmpdir / "approot")
        env["AUDIOBOOK_COVER_CACHE_DIR"] = str(tmpdir / "covers")
        env["AUDIOBOOK_RESUME_SOCKET"] = str(tmpdir / "resume.sock")
        env["AUDIOBOOK_TOUCH_PATH"] = "/dev/null"
        env["AUDIOBOOK_FB_PATH"] = "/dev/null"
        env["AUDIOBOOK_PCM_DEVICE"] = "/dev/null"

        run([str(APP), "--scan-only"], cwd=REPO, env=env)

        conn = sqlite3.connect(db_path)
        try:
            tables = {row[0] for row in conn.execute("SELECT name FROM sqlite_master WHERE type='table'")}
            assert "books" in tables and "tracks" in tables and "progress" in tables
            book_count = conn.execute("SELECT COUNT(*) FROM books").fetchone()[0]
            track_count = conn.execute("SELECT COUNT(*) FROM tracks").fetchone()[0]
            assert book_count == 1, book_count
            assert track_count == 2, track_count
        finally:
            conn.close()


def compile_helper(source: str, out: Path, extra: list[str]) -> None:
    src = out.with_suffix(".c")
    src.write_text(source)
    cmd = [
        str(ZIG), "cc",
        "-std=c11", "-Wall", "-Wextra", "-O2",
        "-I", str(REPO / "app" / "src"),
        "-I", str(REPO / "app" / "assets"),
        str(src),
        *extra,
        "-o", str(out),
    ]
    run(cmd, cwd=REPO)


def test_resume_logic() -> None:
    with tempfile.TemporaryDirectory() as tmp:
        tmpdir = Path(tmp)
        helper = tmpdir / "resume_helper"
        compile_helper(
            textwrap.dedent(
                """
                #include "resume.h"
                #include <stdio.h>
                #include <string.h>
                int main(int argc, char **argv) {
                    if (argc < 2) return 2;
                    printf("%u\\n", resume_smart_rewind_ms(600, 0, 30000));
                    book_row book = {0};
                    progress_row prog = {0};
                    snprintf(book.book_key, sizeof(book.book_key), "demo-book");
                    prog.book_id = 7;
                    prog.track_ordinal = 2;
                    prog.position_ms = 12345;
                    prog.playback_speed = 1.5f;
                    prog.last_saved_at = 777;
                    if (resume_write_record_atomic(argv[1], &prog, &book) != 0) return 3;
                    char record[512];
                    snprintf(record, sizeof(record), "%s/%s.json", argv[1], book.book_key);
                    progress_row out = {0};
                    if (resume_read_record(record, &out) != 0) return 4;
                    printf("%lld %d %lld %.1f %lld\\n",
                        (long long)out.book_id,
                        out.track_ordinal,
                        (long long)out.position_ms,
                        out.playback_speed,
                        (long long)out.last_saved_at);
                    return 0;
                }
                """
            ),
            helper,
            [
                str(REPO / "app" / "src" / "common.c"),
                str(REPO / "app" / "src" / "resume.c"),
            ],
        )
        state_dir = tmpdir / "resume.d"
        state_dir.mkdir()
        result = run([str(helper), str(state_dir)], cwd=REPO)
        lines = result.stdout.strip().splitlines()
        assert lines[0] == "20000", lines
        assert lines[1].startswith("7 2 12345 1.5 777"), lines


def test_ipc_protocol() -> None:
    with tempfile.TemporaryDirectory() as tmp:
        tmpdir = Path(tmp)
        sock_path = tmpdir / "resume.sock"
        helper = tmpdir / "ipc_helper"
        compile_helper(
            textwrap.dedent(
                """
                #include "ipc.h"
                #include <stdio.h>
                #include <string.h>
                int main(int argc, char **argv) {
                    if (argc < 2) return 2;
                    int fd = ipc_client_connect(argv[1]);
                    if (fd < 0) return 3;
                    audiobook_event ev = {0};
                    ev.book_id = 42;
                    ev.track_ordinal = 3;
                    ev.position_ms = 9001;
                    ev.playback_speed_x100 = 125;
                    if (ipc_send_event(fd, AB_EVT_BOOK_OPENED, 17, &ev) != 0) return 4;
                    audiobook_ipc_frame frame = {0};
                    if (ipc_recv_frame(fd, &frame, 3000) != 0) return 5;
                    if (frame.header.magic != 0x50494241u || frame.header.version != 1u || frame.header.type != AB_EVT_PLAYBACK_STARTED) return 6;
                    return 0;
                }
                """
            ),
            helper,
            [
                str(REPO / "app" / "src" / "common.c"),
                str(REPO / "app" / "src" / "ipc.c"),
            ],
        )

        server = socket.socket(socket.AF_UNIX, socket.SOCK_SEQPACKET)
        try:
            server.bind(str(sock_path))
            server.listen(1)
            proc = subprocess.Popen([str(helper), str(sock_path)], cwd=REPO)
            conn, _ = server.accept()
            with conn:
                data = conn.recv(4096)
                assert len(data) >= struct.calcsize("<IHHIQ")
                header = struct.unpack_from("<IHHIQ", data, 0)
                assert header[0] == 0x50494241
                assert header[1] == 1
                assert header[2] == 1
                reply = struct.pack("<IHHIQ", 0x50494241, 1, 2, 0, 99)
                conn.sendall(reply)
            code = proc.wait(timeout=10)
            assert code == 0, code
        finally:
            server.close()


def main() -> int:
    build_app()
    assert APP.exists(), APP
    assert "MIPS" in run(["file", str(APP)], cwd=REPO).stdout
    test_schema_and_scanner()
    test_resume_logic()
    test_ipc_protocol()
    print("all app tests passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
