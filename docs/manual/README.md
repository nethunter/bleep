# Editing and building the Ble(e)p owner's guide

The owner-facing source is [`manual.md`](manual.md). Typography, tables, page
furniture, and PDF rendering live in [`build_manual.py`](build_manual.py), and
images live in `assets/`. The stable output is
`output/pdf/bleep-instruction-manual.pdf` at the repository root.

## Before editing

Edit or rebuild the manual only when the user explicitly requests it. Adding a
device to the support list is the exception: that change also requires a manual
update.

For a full manual refresh:

1. Find the last commit that changed `manual.md` or the generated PDF.
2. Audit every later implementation and documentation change for setup,
   controls, compatibility, safety, recovery, and troubleshooting impact.
3. Complete the implementation, manual source, generated PDF, and synchronized
   website copy on the same scoped feature branch.
4. Render and inspect the manual, rebase the branch onto current `main`, rerun
   the affected artifact checks, and merge it from `main` with `--no-ff`.

If there are no later changes, record that finding and continue on the scoped
branch. A manual refresh is a content audit, not a request to regenerate an
unchanged PDF.

Keep claims aligned with `README.md`, `docs/device-support.md`, and
`docs/progress.md`. Protocol evidence belongs under `docs/protocols/`; session
history and verification detail belong in `docs/progress.md`.

## Writing standard

Write for someone using Ble(e)p during a shoot. Keep a sentence when it helps
the reader:

- complete a task;
- understand surprising behavior;
- avoid a likely mistake or safety hazard; or
- judge whether an exact device and function are supported.

Cut obvious interface narration, defensive reassurance, protocol internals,
test-history prose, and repeated limitation lists. For example, setup
instructions do not need to promise that a failed pairing will not create an
incomplete device. Preserve real boundaries such as optimistic state, toggle
commands, connection limits, destructive reset behavior, open Portal traffic,
movement risk, and exact-model compatibility.

Prefer short steps and direct verbs. Put information where the reader first
needs it, then link or refer back instead of repeating it. Do not add a second
function-reference layer that restates the task chapters. Keep electronics,
printing, assembly, repair, and firmware work in the final
**Advanced: developers and builders** section.

## Source format

The builder supports headings, paragraphs, ordered and unordered lists,
blockquote callouts, fenced code blocks, Markdown tables, page-break comments,
and figures:

```md
![Caption](assets/example.png){width=3.2 rotate=90}
```

Use `<!-- pagebreak -->` only when a deliberate chapter boundary is worth the
space. Recheck pagination after cutting or adding text; a useful break can turn
into a mostly empty page after an editorial change.

Hardware views are line illustrations generated from user-supplied reference
photographs. Regenerate them only when the enclosure changes. UI figures are
simulator captures from the source snapshot recorded in `docs/progress.md`;
replace a figure when the illustrated screen materially changes.

## Build the PDF

From `docs/manual/`:

```sh
python3 -m venv .venv
.venv/bin/python -m pip install -r requirements.txt
make PYTHON=.venv/bin/python pdf
```

If `.venv` already exists, run only the `make` command. Copy the finished PDF to
the website download before committing:

```sh
cp ../../output/pdf/bleep-instruction-manual.pdf \
  ../../website/downloads/bleep-instruction-manual.pdf
```

The two files must have the same checksum. Publishing the website is a separate
step; deploy it only when the user asks for a live or mobile-review copy.

## Render and inspect

Render every page after any content or layout change, not only the page you
expected to move. Use a fresh empty directory so pages from an older, longer
build cannot linger:

```sh
render_dir=../../tmp/pdfs/manual-pages-review-1
test ! -e "$render_dir"
mkdir -p "$render_dir"
pdftoppm -r 120 -png ../../output/pdf/bleep-instruction-manual.pdf \
  "$render_dir/page"
pdfinfo ../../output/pdf/bleep-instruction-manual.pdf
```

Review all pages, preferably in contact sheets, then inspect changed pages and
dense tables at full resolution. Check for:

- clipped or overlapping text and images;
- captions colliding with the following paragraph;
- broken or overflowing tables;
- unexpected blank or mostly empty pages;
- awkward headings or lists split across pages; and
- incorrect headers, footers, page numbers, or contents entries.

Rebuild and rerender after every layout fix. Text extraction is useful for
confirming key phrases, exact models, dates, and page count, but it does not
replace visual inspection.

## Finish the update

Add a dated `docs/progress.md` entry with the audit base commit, material edits,
output page count, render settings, pages inspected at full resolution, defects
found and fixed, and whether firmware was built or flashed. Manual-only edits do
not require a firmware build or flash.

Commit the source, generated PDF, synchronized website copy, and progress entry
together. Remove temporary renders after verification; keep only the stable PDF
and maintained source artifacts.
