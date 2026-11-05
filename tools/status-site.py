#!/usr/bin/env python3
"""Build the progress page (site/index.html + site/progress.json) from README.md.

Uses the same status tables as tools/status-overview.py. CI runs this on every
push to main and publishes site/ with GitHub Pages.
"""
import datetime
import html
import json
import pathlib
import re
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import importlib.util

spec = importlib.util.spec_from_file_location(
    "overview", pathlib.Path(__file__).resolve().parent / "status-overview.py")
ov = importlib.util.module_from_spec(spec)
spec.loader.exec_module(ov)

ROOT = ov.ROOT
OUT = ROOT / "site"
STATE = {
    "✅": ("done", "Working"),
    "🟡": ("partial", "Partial"),
    "❌": ("broken", "Not working"),
    "⬜": ("todo", "Not started"),
}


def md_inline(text):
    """Escape, then render `code`, [links](url) and drop ** markers."""
    t = html.escape(text)
    t = re.sub(r"`([^`]+)`", r"<code>\1</code>", t)
    t = re.sub(r"\[([^\]]+)\]\(([^)]+)\)",
               lambda m: f'<a href="{m.group(2) if m.group(2).startswith("http") else REPO + "/blob/main/" + m.group(2)}">{m.group(1)}</a>', t)
    return t.replace("**", "")


REPO = "https://github.com/JeyKul/gts7xl-mainline"


def main():
    sections = ov.parse(ov.README.read_text(encoding="utf-8"))
    states = [r[1] for _, rows in sections for r in rows]
    score = round(100 * sum(ov.WEIGHT[s] for s in states) / len(states))
    tier = next(label for limit, label in ov.TIERS if score >= limit)
    notes = ov.DRAWBACKS.read_text(encoding="utf-8").strip() if ov.DRAWBACKS.exists() else ""
    counts = {STATE[k][0]: states.count(k) for k in STATE}
    now = datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%d %H:%M UTC")

    data = {
        "generated": now, "score": score, "usability": tier, "notes": notes, "counts": counts,
        "areas": [{"name": n, "items": [{"name": r[0], "state": STATE[r[1]][0], "notes": r[2]} for r in rows]}
                  for n, rows in sections],
    }

    seg = "".join(
        f'<div class="seg {cls}" style="width:{100 * counts[cls] / len(states):.2f}%" title="{label}: {counts[cls]}"></div>'
        for cls, label in (STATE[k] for k in STATE) if counts[cls])

    cards = []
    for name, rows in sections:
        done = sum(1 for r in rows if r[1] == "✅")
        part = sum(1 for r in rows if r[1] == "🟡")
        pct = round(100 * (done + part / 2) / len(rows))
        items = "".join(
            f'<tr class="{STATE[r[1]][0]}"><td><span class="dot"></span>{md_inline(r[0])}</td>'
            f'<td class="st">{STATE[r[1]][1]}</td><td class="nt">{md_inline(r[2])}</td></tr>'
            for r in rows)
        cards.append(
            f'<section class="card"><header><h2>{html.escape(name)}</h2><span class="pct">{pct}%</span></header>'
            f'<div class="mini">{"".join(f"<i class={chr(34)}{STATE[r[1]][0]}{chr(34)}></i>" for r in rows)}</div>'
            f'<table>{items}</table></section>')

    page = f"""<!doctype html>
<html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Galaxy Tab S7+ mainline Linux: progress</title>
<style>
:root{{--bg:#0f1115;--card:#171a21;--line:#262b36;--fg:#e6e8ee;--mute:#8b93a7;
--done:#2ea043;--partial:#d4a72c;--broken:#da3633;--todo:#3a4150}}
*{{box-sizing:border-box}}body{{margin:0;background:var(--bg);color:var(--fg);font:15px/1.5 system-ui,-apple-system,Segoe UI,Roboto,sans-serif}}
main{{max-width:1100px;margin:0 auto;padding:32px 20px 60px}}
h1{{margin:0 0 4px;font-size:26px}}.sub{{color:var(--mute);margin:0 0 24px}}a{{color:#58a6ff}}
.hero{{background:var(--card);border:1px solid var(--line);border-radius:14px;padding:22px;margin-bottom:22px}}
.tier{{font-size:20px;font-weight:600}}.score{{float:right;font-size:28px;font-weight:700}}
.bar{{display:flex;height:14px;border-radius:7px;overflow:hidden;background:var(--todo);margin:14px 0 10px}}
.seg.done{{background:var(--done)}}.seg.partial{{background:var(--partial)}}.seg.broken{{background:var(--broken)}}.seg.todo{{background:var(--todo)}}
.legend span{{margin-right:16px;color:var(--mute);white-space:nowrap}}.legend b{{display:inline-block;width:10px;height:10px;border-radius:3px;margin-right:6px}}
.notes{{margin:12px 0 0;padding:10px 14px;border-left:3px solid var(--partial);background:#1d1f16;color:#d9d3b8;border-radius:6px}}
.grid{{display:grid;grid-template-columns:repeat(auto-fill,minmax(480px,1fr));gap:18px}}
@media(max-width:560px){{.grid{{grid-template-columns:1fr}}}}
.card{{background:var(--card);border:1px solid var(--line);border-radius:14px;padding:16px 18px}}
.card header{{display:flex;justify-content:space-between;align-items:baseline}}.card h2{{font-size:17px;margin:0}}.pct{{color:var(--mute);font-weight:600}}
.mini{{display:flex;gap:4px;margin:10px 0 8px}}.mini i{{flex:1;height:6px;border-radius:3px;background:var(--todo)}}
.mini i.done{{background:var(--done)}}.mini i.partial{{background:var(--partial)}}.mini i.broken{{background:var(--broken)}}
table{{width:100%;border-collapse:collapse;table-layout:fixed}}td{{padding:7px 6px;border-top:1px solid var(--line);vertical-align:top}}
td:first-child{{width:40%;font-weight:500}}.st{{width:92px;font-size:13px}}td{{overflow-wrap:anywhere}}.nt{{color:var(--mute);font-size:13px}}
.dot{{display:inline-block;width:9px;height:9px;border-radius:50%;margin-right:8px;background:var(--todo)}}
tr.done .dot{{background:var(--done)}}tr.partial .dot{{background:var(--partial)}}tr.broken .dot{{background:var(--broken)}}
tr.done .st{{color:var(--done)}}tr.partial .st{{color:var(--partial)}}tr.broken .st{{color:var(--broken)}}tr.todo .st{{color:var(--mute)}}
code{{background:#22262f;padding:1px 5px;border-radius:4px;font-size:12px}}footer{{color:var(--mute);margin-top:28px;font-size:13px}}
</style></head><body><main>
<h1>Galaxy Tab S7+ Wi-Fi · mainline Linux</h1>
<p class="sub">SM-T970 (gts7xlwifi), Snapdragon 865, Linux v7.3 + Arch Linux ARM · <a href="{REPO}">repository</a> · <a href="progress.json">progress.json</a></p>
<section class="hero"><span class="score">{score}%</span><div class="tier">{html.escape(tier)}</div>
<div class="bar">{seg}</div>
<div class="legend"><span><b style="background:var(--done)"></b>Working {counts['done']}</span><span><b style="background:var(--partial)"></b>Partial {counts['partial']}</span><span><b style="background:var(--broken)"></b>Not working {counts['broken']}</span><span><b style="background:var(--todo)"></b>Not started {counts['todo']}</span></div>
{f'<p class="notes">{md_inline(notes)}</p>' if notes else ''}</section>
<div class="grid">{''.join(cards)}</div>
<footer>Generated {now} by <code>tools/status-site.py</code> from the status tables in README.md on every push to main. Score: working = 1, partial = ½.</footer>
</main></body></html>
"""
    OUT.mkdir(exist_ok=True)
    (OUT / "index.html").write_text(page, encoding="utf-8")
    (OUT / "progress.json").write_text(json.dumps(data, ensure_ascii=False, indent=1), encoding="utf-8")
    print(f"site/ written: {score}% {tier}")


if __name__ == "__main__":
    main()
