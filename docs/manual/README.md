# Ble(e)p instruction manual source

The manual is maintained in [`manual.md`](manual.md). Images live in
`assets/`, while typography, tables, page furniture, and PDF rendering live in
[`build_manual.py`](build_manual.py). Keep user instructions and compatibility
claims synchronized with the repository's `README.md`, `docs/device-support.md`,
and `docs/progress.md`.

Build from this directory:

```sh
python3 -m venv .venv
.venv/bin/python -m pip install -r requirements.txt
make PYTHON=.venv/bin/python pdf
```

The stable output path is `output/pdf/bleep-instruction-manual.pdf` at the
repository root. The source supports headings, paragraphs, ordered and
unordered lists, blockquote callouts, fenced code blocks, Markdown tables,
page-break comments, and figures using this syntax:

```md
![Caption](assets/example.png){width=3.2 rotate=90}
```

After changing content or layout, render every PDF page to PNG and inspect it:

```sh
mkdir -p ../../tmp/pdfs/manual-pages
pdftoppm -png ../../output/pdf/bleep-instruction-manual.pdf \
  ../../tmp/pdfs/manual-pages/page
```

Hardware views are line illustrations generated from the user-supplied reference
photographs. UI figures are simulator captures from the same `e97d0b6` source
snapshot. Regenerate hardware illustrations only when the enclosure changes,
and replace UI captures whenever the illustrated screens materially change.
