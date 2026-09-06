// Real simulator captures; the taps and crossfades are illustrative, not a live connection.
const CHAPTERS = [
  {
    name: "Sequences",
    detail: "Cue the whole take",
    before: "sequence-ready",
    after: "sequence-running",
    tap: [80, 140],
  },
  {
    name: "Lighting",
    detail: "Find your color",
    before: "light-cct",
    after: "light-rgb",
    tap: [152, 85],
  },
  {
    name: "Cameras",
    detail: "Ready. Set. Record.",
    before: "camera-ready",
    after: "camera-recording",
    tap: [120, 167],
  },
  {
    name: "Audio",
    detail: "Keep sound rolling",
    before: "audio-ready",
    after: "audio-recording",
  },
  {
    name: "Motion",
    detail: "Put the shot in motion",
    before: "motion-keypoints",
    after: "motion-run",
  },
];
const DURATION = 5200;
const TAP_AT = 1200;
const CHANGE_AT = 1950;
const HOLD_AT = 2350;
const FADE_AT = 4700;

function loadImage(name) {
  return new Promise((resolve, reject) => {
    const image = new Image();
    image.onload = () => resolve(image);
    image.onerror = () => reject(new Error(`Screen unavailable: ${name}`));
    image.src = new URL(`./assets/screens/${name}.png`, import.meta.url).href;
  });
}

export async function createScreenTour(onFrame) {
  const panel = document.querySelector("[data-screen-tour]");
  const label = panel.querySelector("[data-screen-name]");
  const detail = panel.querySelector("[data-screen-detail]");
  const pauseButton = panel.querySelector("[data-screen-pause]");
  const nextButton = panel.querySelector("[data-screen-next]");
  const announcement = panel.querySelector("[data-screen-announcement]");
  const reduced = matchMedia("(prefers-reduced-motion: reduce)");
  const results = await Promise.allSettled(
    CHAPTERS.map(async (chapter) => ({
      ...chapter,
      beforeImage: await loadImage(chapter.before),
      afterImage: await loadImage(chapter.after),
    })),
  );
  const chapters = results
    .filter((result) => result.status === "fulfilled")
    .map((result) => result.value);
  if (chapters.length < 2) throw new Error("Screen tour unavailable");
  const canvas = document.createElement("canvas");
  canvas.width = 240;
  canvas.height = 240;
  const ctx = canvas.getContext("2d", { alpha: false });
  if (!ctx) throw new Error("Screen texture unavailable");
  let index = 0;
  let elapsed = 0;
  let lastTick = null;
  let timer = null;
  let active = false;
  let paused = false;
  let destroyed = false;
  const canPlay = () => active && !paused && !reduced.matches && !destroyed;
  const updateLabels = () => {
    const chapter = chapters[index];
    label.textContent = chapter.name;
    detail.textContent = chapter.detail;
    panel.dataset.screenFunction = chapter.name.toLowerCase();
    panel.dataset.screenPlaying = String(canPlay());
    pauseButton.disabled = reduced.matches;
    pauseButton.setAttribute(
      "aria-label",
      reduced.matches
        ? "Screen animation disabled for reduced motion"
        : paused
          ? "Play screen demo"
          : "Pause screen demo",
    );
    pauseButton.querySelector("span").textContent =
      paused || reduced.matches ? "▶" : "Ⅱ";
  };
  const paint = () => {
    const chapter = chapters[index];
    ctx.globalAlpha = 1;
    ctx.fillStyle = "#03080a";
    ctx.fillRect(0, 0, 240, 240);
    ctx.drawImage(
      elapsed < CHANGE_AT ? chapter.beforeImage : chapter.afterImage,
      0,
      0,
      240,
      240,
    );
    if (!reduced.matches && elapsed >= CHANGE_AT && elapsed < HOLD_AT) {
      ctx.globalAlpha = 1 - (elapsed - CHANGE_AT) / (HOLD_AT - CHANGE_AT);
      ctx.drawImage(chapter.beforeImage, 0, 0, 240, 240);
    } else if (!reduced.matches && elapsed >= FADE_AT) {
      ctx.globalAlpha = (elapsed - FADE_AT) / (DURATION - FADE_AT);
      ctx.drawImage(
        chapters[(index + 1) % chapters.length].beforeImage,
        0,
        0,
        240,
        240,
      );
    }
    ctx.globalAlpha = 1;
    if (
      chapter.tap &&
      !reduced.matches &&
      elapsed >= TAP_AT &&
      elapsed < CHANGE_AT
    ) {
      const progress = (elapsed - TAP_AT) / (CHANGE_AT - TAP_AT);
      const [x, y] = chapter.tap;
      ctx.beginPath();
      ctx.arc(x, y, 7 + progress * 17, 0, Math.PI * 2);
      ctx.strokeStyle = `rgba(255,255,255,${1 - progress})`;
      ctx.lineWidth = 2.5;
      ctx.stroke();
      ctx.beginPath();
      ctx.arc(x, y, 4, 0, Math.PI * 2);
      ctx.fillStyle = `rgba(255,255,255,${Math.sin(progress * Math.PI) * 0.75})`;
      ctx.fill();
    }
    onFrame();
  };
  const tick = () => {
    timer = null;
    if (!canPlay()) return;
    const now = performance.now();
    elapsed += lastTick === null ? 0 : now - lastTick;
    lastTick = now;
    if (elapsed >= DURATION) {
      index = (index + Math.floor(elapsed / DURATION)) % chapters.length;
      elapsed %= DURATION;
      updateLabels();
    }
    paint();
    // Upload new textures only during the tap/transition windows; hold frames sleep.
    let delay = 1000 / 24;
    if (elapsed < TAP_AT) delay = TAP_AT - elapsed;
    else if (elapsed < CHANGE_AT && !chapters[index].tap)
      delay = CHANGE_AT - elapsed;
    else if (elapsed >= HOLD_AT && elapsed < FADE_AT) delay = FADE_AT - elapsed;
    timer = setTimeout(tick, delay);
  };
  const stopClock = () => {
    clearTimeout(timer);
    timer = null;
    if (lastTick !== null)
      elapsed = Math.min(elapsed + performance.now() - lastTick, DURATION - 1);
    lastTick = null;
  };
  const startClock = () => {
    if (canPlay() && timer === null) {
      lastTick = performance.now();
      tick();
    }
  };
  const toggle = () => {
    stopClock();
    paused = !paused;
    updateLabels();
    startClock();
  };
  const next = () => {
    stopClock();
    index = (index + 1) % chapters.length;
    elapsed = 0;
    updateLabels();
    paint();
    startClock();
    announcement.textContent = `${chapters[index].name}: ${chapters[index].detail}.`;
  };
  const motionChanged = () => {
    stopClock();
    elapsed = 0;
    updateLabels();
    paint();
    startClock();
  };
  pauseButton.addEventListener("click", toggle);
  nextButton.addEventListener("click", next);
  reduced.addEventListener("change", motionChanged);
  updateLabels();
  paint();
  return {
    canvas,
    setActive(value) {
      if (active === value) return;
      stopClock();
      active = value;
      updateLabels();
      startClock();
    },
    destroy() {
      destroyed = true;
      stopClock();
      pauseButton.removeEventListener("click", toggle);
      nextButton.removeEventListener("click", next);
      reduced.removeEventListener("change", motionChanged);
      panel.hidden = true;
    },
  };
}
