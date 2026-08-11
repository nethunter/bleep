PYTHON ?= python3
OUTPUT ?= ../../output/pdf/bleep-instruction-manual.pdf

.PHONY: pdf clean

pdf:
	$(PYTHON) build_manual.py --output $(OUTPUT)

clean:
	rm -f $(OUTPUT)
