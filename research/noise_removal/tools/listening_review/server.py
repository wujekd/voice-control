#!/usr/bin/env python3

import argparse
import html
import json
import mimetypes
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import parse_qs, unquote, urlparse


ROOT = Path.cwd()


def rel(path):
    return path.relative_to(ROOT).as_posix()


def find_matches(output_dir, stem):
    return sorted(output_dir.glob(f"{stem}*.wav"), key=lambda p: p.stat().st_mtime, reverse=True)


class ReviewServer(BaseHTTPRequestHandler):
    experiment = None
    notes_path = None

    def do_GET(self):
        parsed = urlparse(self.path)
        if parsed.path == "/":
            self.send_html(self.render_index())
            return
        if parsed.path.startswith("/file/"):
            self.send_file(ROOT / unquote(parsed.path[len("/file/"):]))
            return
        self.send_error(404)

    def do_POST(self):
        parsed = urlparse(self.path)
        if parsed.path != "/notes":
            self.send_error(404)
            return

        length = int(self.headers.get("Content-Length", "0"))
        data = parse_qs(self.rfile.read(length).decode("utf-8"))
        notes = self.load_notes()
        sample = data.get("sample", [""])[0]
        notes[sample] = {
            "noise_removal": data.get("noise_removal", [""])[0],
            "voice_damage": data.get("voice_damage", [""])[0],
            "artifacts": data.get("artifacts", [""])[0],
            "notes": data.get("notes", [""])[0],
        }
        self.notes_path.parent.mkdir(parents=True, exist_ok=True)
        self.notes_path.write_text(json.dumps(notes, indent=2), encoding="utf-8")
        self.send_response(303)
        self.send_header("Location", "/")
        self.end_headers()

    def load_notes(self):
        if self.notes_path.exists():
            return json.loads(self.notes_path.read_text(encoding="utf-8"))
        return {}

    def render_index(self):
        input_dir = self.experiment / "data" / "input"
        output_dir = self.experiment / "data" / "output"
        reference_dir = self.experiment / "data" / "reference"
        notes = self.load_notes()
        rows = []

        for input_file in sorted(input_dir.glob("*.wav")):
            output_files = find_matches(output_dir, input_file.stem)
            reference_files = find_matches(reference_dir, input_file.stem)
            sample_notes = notes.get(input_file.name, {})
            rows.append(self.render_row(input_file, output_files, reference_files, sample_notes))

        body = "\n".join(rows) if rows else "<p>No input WAV files found.</p>"
        return f"""<!doctype html>
<html>
<head>
  <meta charset="utf-8">
  <title>Noise Removal Review</title>
  <style>
    body {{ font-family: -apple-system, BlinkMacSystemFont, sans-serif; margin: 28px; color: #1f2328; }}
    h1 {{ font-size: 22px; margin-bottom: 4px; }}
    .path {{ color: #656d76; margin-bottom: 24px; }}
    .sample {{ border-top: 1px solid #d0d7de; padding: 18px 0; }}
    .name {{ font-weight: 650; margin-bottom: 10px; }}
    .grid {{ display: grid; grid-template-columns: minmax(260px, 720px); gap: 14px; }}
    .label {{ font-size: 12px; color: #656d76; margin-bottom: 4px; }}
    audio {{ width: 100%; }}
    form {{ margin-top: 12px; display: grid; grid-template-columns: repeat(3, 120px) 1fr auto; gap: 8px; align-items: end; }}
    input, textarea, button {{ font: inherit; }}
    input {{ width: 100%; }}
    textarea {{ min-height: 38px; resize: vertical; }}
    button {{ padding: 8px 12px; }}
  </style>
</head>
<body>
  <h1>Noise Removal Review</h1>
  <div class="path">{html.escape(rel(self.experiment))}</div>
  {body}
</body>
</html>"""

    def render_audio(self, label, path):
        if not path:
            return f'<div><div class="label">{label}</div><em>missing</em></div>'
        url = "/file/" + rel(path)
        return f'<div><div class="label">{label}</div><audio controls src="{html.escape(url)}"></audio></div>'

    def render_audio_group(self, title, paths):
        if not paths:
            return self.render_audio(title, None)
        players = []
        for path in paths:
            players.append(self.render_audio(path.stem, path))
        return "\n    ".join(players)

    def render_row(self, input_file, output_files, reference_files, notes):
        return f"""<section class="sample">
  <div class="name">{html.escape(input_file.name)}</div>
  <div class="grid">
    {self.render_audio("Original", input_file)}
    {self.render_audio_group("Processed", output_files)}
    {self.render_audio_group("Reference", reference_files)}
  </div>
  <form method="post" action="/notes">
    <input type="hidden" name="sample" value="{html.escape(input_file.name)}">
    <label><div class="label">Noise removal</div><input name="noise_removal" value="{html.escape(notes.get("noise_removal", ""))}"></label>
    <label><div class="label">Voice damage</div><input name="voice_damage" value="{html.escape(notes.get("voice_damage", ""))}"></label>
    <label><div class="label">Artifacts</div><input name="artifacts" value="{html.escape(notes.get("artifacts", ""))}"></label>
    <label><div class="label">Notes</div><textarea name="notes">{html.escape(notes.get("notes", ""))}</textarea></label>
    <button>Save</button>
  </form>
</section>"""

    def send_html(self, body):
        encoded = body.encode("utf-8")
        self.send_response(200)
        self.send_header("Content-Type", "text/html; charset=utf-8")
        self.send_header("Content-Length", str(len(encoded)))
        self.end_headers()
        self.wfile.write(encoded)

    def send_file(self, path):
        if not path.exists() or not path.is_file():
            self.send_error(404)
            return
        mime = mimetypes.guess_type(path)[0] or "application/octet-stream"
        data = path.read_bytes()
        self.send_response(200)
        self.send_header("Content-Type", mime)
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)


def parse_args():
    parser = argparse.ArgumentParser(description="Serve a local A/B listening review page.")
    parser.add_argument("--experiment", required=True, type=Path)
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", default=8765, type=int)
    return parser.parse_args()


def main():
    args = parse_args()
    ReviewServer.experiment = args.experiment.resolve()
    ReviewServer.notes_path = ReviewServer.experiment / "notes" / "listening_review.json"
    server = ThreadingHTTPServer((args.host, args.port), ReviewServer)
    print(f"Listening review: http://{args.host}:{args.port}")
    server.serve_forever()


if __name__ == "__main__":
    main()
