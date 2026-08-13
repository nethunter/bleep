const header = document.querySelector("[data-header]");
const navToggle = document.querySelector("[data-nav-toggle]");
const nav = document.querySelector("[data-nav]");

const updateHeader = () => header?.classList.toggle("is-scrolled", window.scrollY > 20);
updateHeader();
window.addEventListener("scroll", updateHeader, { passive: true });

navToggle?.addEventListener("click", () => {
  const isOpen = nav.classList.toggle("is-open");
  navToggle.setAttribute("aria-expanded", String(isOpen));
  document.body.style.overflow = isOpen ? "hidden" : "";
});

nav?.querySelectorAll("a").forEach((link) => {
  link.addEventListener("click", () => {
    nav.classList.remove("is-open");
    navToggle?.setAttribute("aria-expanded", "false");
    document.body.style.overflow = "";
  });
});

const observer = new IntersectionObserver((entries) => {
  entries.forEach((entry) => {
    if (entry.isIntersecting) {
      entry.target.classList.add("is-visible");
      observer.unobserve(entry.target);
    }
  });
}, { threshold: 0.12 });

document.querySelectorAll(".reveal").forEach((element) => observer.observe(element));

const tabs = [...document.querySelectorAll("[data-tab]")];
const panels = [...document.querySelectorAll("[data-panel]")];

const selectTab = (name) => {
  tabs.forEach((tab) => tab.setAttribute("aria-selected", String(tab.dataset.tab === name)));
  panels.forEach((panel) => {
    const selected = panel.dataset.panel === name;
    panel.hidden = !selected;
    panel.classList.toggle("is-active", selected);
  });
};

tabs.forEach((tab, index) => {
  tab.addEventListener("click", () => selectTab(tab.dataset.tab));
  tab.addEventListener("keydown", (event) => {
    if (!["ArrowLeft", "ArrowRight", "ArrowUp", "ArrowDown"].includes(event.key)) return;
    event.preventDefault();
    const direction = ["ArrowRight", "ArrowDown"].includes(event.key) ? 1 : -1;
    const next = tabs[(index + direction + tabs.length) % tabs.length];
    next.focus();
    selectTab(next.dataset.tab);
  });
});

document.querySelector("[data-year]").textContent = new Date().getFullYear();
