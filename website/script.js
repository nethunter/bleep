document.documentElement.classList.add("js");
const navToggle = document.querySelector("[data-nav-toggle]");
const nav = document.querySelector("[data-nav]");
function closeMenu(returnFocus = false) {
  nav?.classList.remove("is-open");
  navToggle?.setAttribute("aria-expanded", "false");
  if (returnFocus) navToggle?.focus();
}
navToggle?.addEventListener("click", () => {
  const open = nav.classList.toggle("is-open");
  navToggle.setAttribute("aria-expanded", String(open));
});
nav
  ?.querySelectorAll("a")
  .forEach((link) => link.addEventListener("click", () => closeMenu()));
document.addEventListener("keydown", (event) => {
  if (event.key === "Escape" && nav?.classList.contains("is-open"))
    closeMenu(true);
});
document.addEventListener("click", (event) => {
  if (!event.target.closest(".site-header")) closeMenu();
});
matchMedia("(min-width: 761px)").addEventListener("change", () => closeMenu());
document.querySelectorAll("[data-year]").forEach((el) => {
  el.textContent = new Date().getFullYear();
});

const demo = document.querySelector("[data-demo]");
const motion = matchMedia("(prefers-reduced-motion: reduce)");
if (demo) {
  const button = demo.querySelector("[data-play-demo]");
  const steps = [...demo.querySelectorAll("[data-scene-step]")];
  steps.forEach((el) => {
    el.disabled = false;
  });
  const captions = [
    "Your next take starts here.",
    "The look is set. Step into the light.",
    "Camera rolling. The frame is yours.",
    "Sound rolling. Now make something good.",
  ];
  let timer;
  let playing = false;
  let step = 0;
  const showStep = (value) => {
    step = value;
    demo.dataset.step = String(value);
    steps.forEach((el) =>
      el.setAttribute(
        "aria-pressed",
        String(Number(el.dataset.sceneStep) === value),
      ),
    );
    demo.querySelector("[data-scene-caption]").textContent = captions[value];
    demo.querySelector("[data-demo-status]").textContent = value
      ? `Step ${value} of 3. ${captions[value]}`
      : captions[value];
  };
  const stop = () => {
    clearTimeout(timer);
    playing = false;
    button.textContent = step
      ? "↻ Replay the sequence"
      : "▶ Play the sequence";
  };
  const advance = () => {
    showStep(step + 1);
    if (step < 3) timer = setTimeout(advance, 2200);
    else timer = setTimeout(stop, 2200);
  };
  button.addEventListener("click", () => {
    if (playing) {
      stop();
      return;
    }
    clearTimeout(timer);
    if (motion.matches) {
      showStep(3);
      stop();
      return;
    }
    playing = true;
    button.textContent = "Ⅱ Pause the sequence";
    showStep(0);
    advance();
  });
  steps.forEach((el) =>
    el.addEventListener("click", () => {
      showStep(Number(el.dataset.sceneStep));
      stop();
    }),
  );
  document.addEventListener("visibilitychange", () => {
    if (document.hidden) stop();
  });
  new IntersectionObserver(
    (entries) => {
      if (!entries[0].isIntersecting) stop();
    },
    { threshold: 0 },
  ).observe(demo);
  motion.addEventListener("change", () => {
    if (motion.matches) stop();
  });
}
const brightness = document.querySelector("#brightness");
brightness?.addEventListener("input", () => {
  const value = Number(brightness.value);
  document.querySelector("[data-brightness-value]").innerHTML =
    `${value}<span>%</span>`;
  const panel = document.querySelector("[data-light-playground]");
  panel.style.setProperty("--brightness", String(value / 100));
  panel.style.setProperty("--dial", `${value}%`);
});
// The poster and all marketing content work before, or without, WebGL.
const model = document.querySelector("[data-model-wrap]");
if (model && !navigator.connection?.saveData) {
  import("./product-model.js?v=20260906-8")
    .then((module) => module.mountProduct(model))
    .catch(() => {
      document.querySelector("[data-model-controls]").hidden = true;
    });
}
