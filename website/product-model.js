import * as THREE from "./assets/vendor/three.module.min.js";

// Binary STL exports share the assembly coordinates in hardware/Bleep Remote.step.
async function loadPart(name, material) {
  const response = await fetch(
    new URL(`./assets/models/${name}.stl`, import.meta.url),
  );
  if (!response.ok) throw new Error(`Model part unavailable: ${name}`);
  const data = new DataView(await response.arrayBuffer());
  const count = data.getUint32(80, true);
  if (data.byteLength !== 84 + count * 50)
    throw new Error("Invalid model geometry");
  const positions = new Float32Array(count * 9);
  const normals = new Float32Array(count * 9);
  for (let face = 0; face < count; face++) {
    const offset = 84 + face * 50;
    for (let vertex = 0; vertex < 3; vertex++) {
      for (let axis = 0; axis < 3; axis++) {
        const index = face * 9 + vertex * 3 + axis;
        positions[index] = data.getFloat32(
          offset + 12 + vertex * 12 + axis * 4,
          true,
        );
        normals[index] = data.getFloat32(offset + axis * 4, true);
      }
    }
  }
  const geometry = new THREE.BufferGeometry();
  geometry.setAttribute("position", new THREE.BufferAttribute(positions, 3));
  geometry.setAttribute("normal", new THREE.BufferAttribute(normals, 3));
  if (name === "display-cover") {
    // Replace the solid front cap with the screen and its annular border.
    // Leaving CAD triangles immediately underneath causes mobile depth fighting.
    const indices = [];
    for (let face = 0; face < count; face++) {
      const offset = face * 9;
      const isFront = [2, 5, 8].every(
        (axis) => Math.abs(positions[offset + axis] - 1.9) < 0.001,
      );
      if (!isFront) indices.push(face * 3, face * 3 + 1, face * 3 + 2);
    }
    geometry.setIndex(indices);
  }
  const mesh = new THREE.Mesh(geometry, material);
  mesh.name = name;
  return mesh;
}

export async function mountProduct(wrap) {
  let renderer;
  let screenTour;
  const tourPanel = document.querySelector("[data-screen-tour]");
  const scene = new THREE.Scene();
  const resources = new Set();
  const controls = document.querySelector("[data-model-controls]");
  const finishes = document.querySelector("[data-finish-controls]");
  const reduced = matchMedia("(prefers-reduced-motion: reduce)");
  const group = new THREE.Group();
  let frame = 0;
  let visible = true;
  let lost = false;
  let disposed = false;
  let pressTimer;
  const initial = { x: 0.16, y: -0.4 };
  const target = { ...initial };
  const syncTour = () =>
    screenTour?.setActive(visible && !document.hidden && !lost && !disposed);
  const release = () => {
    clearTimeout(pressTimer);
    screenTour?.destroy();
    resources.forEach((resource) => resource.dispose());
    renderer?.dispose();
  };
  try {
    renderer = new THREE.WebGLRenderer({
      alpha: true,
      antialias: true,
      powerPreference: "low-power",
    });
    renderer.setPixelRatio(Math.min(devicePixelRatio, 1.75));
    renderer.setClearColor(0x000000, 0);
    renderer.outputColorSpace = THREE.SRGBColorSpace;
    renderer.toneMapping = THREE.ACESFilmicToneMapping;
    renderer.toneMappingExposure = 1.2;
    const ivory = new THREE.MeshStandardMaterial({
      color: 0xeee9d9,
      roughness: 0.58,
      metalness: 0.05,
    });
    const stone = { value: 0 };
    ivory.onBeforeCompile = (shader) => {
      shader.uniforms.uStone = stone;
      shader.vertexShader = shader.vertexShader
        .replace(
          "#include <common>",
          "#include <common>\nvarying vec3 vFinishPosition;",
        )
        .replace(
          "#include <begin_vertex>",
          "#include <begin_vertex>\nvFinishPosition = position;",
        );
      shader.fragmentShader = shader.fragmentShader
        .replace(
          "#include <common>",
          `#include <common>
          varying vec3 vFinishPosition;
          uniform float uStone;
          float finishGrain(vec3 p) {
            return fract(sin(dot(p, vec3(127.1, 311.7, 74.7))) * 43758.5453);
          }`,
        )
        .replace(
          "#include <color_fragment>",
          `#include <color_fragment>
          float fleck = step(0.94, finishGrain(floor(vFinishPosition * 3.0)));
          float grain = finishGrain(floor(vFinishPosition * 16.0));
          diffuseColor.rgb *= 1.0 - uStone * (fleck * 0.48 + grain * 0.06);`,
        );
    };
    const charcoal = new THREE.MeshStandardMaterial({
      color: 0x151713,
      roughness: 0.45,
      metalness: 0.15,
    });
    const orange = new THREE.MeshStandardMaterial({
      color: 0xeb6030,
      roughness: 0.5,
    });
    const socketMetal = new THREE.MeshStandardMaterial({
      color: 0xb6b8b1,
      metalness: 0.65,
      roughness: 0.3,
    });
    [ivory, charcoal, orange, socketMetal].forEach((item) =>
      resources.add(item),
    );
    const results = await Promise.allSettled([
      loadPart("top", ivory),
      loadPart("bottom", ivory),
      loadPart("button", orange),
      loadPart("usb-port", socketMetal),
      loadPart("power-switch", charcoal),
      loadPart("display-cover", charcoal),
    ]);
    for (const result of results) {
      if (result.status === "fulfilled") {
        resources.add(result.value.geometry);
        group.add(result.value);
      }
    }
    if (results.some((result) => result.status === "rejected"))
      throw new Error("Incomplete model");
    const actionMesh = group.getObjectByName("button");
    const addMesh = (geometry, material, x, y, z) => {
      const mesh = new THREE.Mesh(geometry, material);
      mesh.position.set(x, y, z);
      group.add(mesh);
      resources.add(geometry);
      resources.add(material);
      return mesh;
    };
    // The STEP connector is a single-metal CAD part. Add the dark socket mouth
    // and insulating tongue seen in the physical reference, leaving its rim visible.
    const socketOpening = new THREE.Shape();
    socketOpening.moveTo(-1.2, -2.55);
    socketOpening.lineTo(-1.2, 2.55);
    socketOpening.absarc(0, 2.55, 1.2, Math.PI, 0, true);
    socketOpening.lineTo(1.2, -2.55);
    socketOpening.absarc(0, -2.55, 1.2, 0, -Math.PI, true);
    socketOpening.closePath();
    const socketMouth = addMesh(
      new THREE.ShapeGeometry(socketOpening, 20),
      new THREE.MeshBasicMaterial({ color: 0x090b09 }),
      -17.92,
      0,
      -4.18,
    );
    socketMouth.rotation.y = -Math.PI / 2;
    addMesh(new THREE.BoxGeometry(0.18, 6.2, 0.5), charcoal, -18.02, 0, -4.18);
    const texture = await new THREE.TextureLoader().loadAsync(
      new URL("./assets/ui-sequence.png", import.meta.url).href,
    );
    texture.colorSpace = THREE.SRGBColorSpace;
    resources.add(texture);
    const screenMaterial = new THREE.MeshBasicMaterial({ map: texture });
    // Adjacent surfaces share an edge, with no solid cover beneath the pixels.
    addMesh(new THREE.RingGeometry(16.4, 17.05, 96), charcoal, 0, 0, 1.9);
    addMesh(new THREE.CircleGeometry(16.4, 96), screenMaterial, 0, 0, 1.9);
    const metal = new THREE.MeshStandardMaterial({
      color: 0x4c4e46,
      metalness: 0.75,
      roughness: 0.4,
    });
    for (const [x, y] of [
      [-14.725, -24.5],
      [14.725, -24.5],
      [0, -82.175],
    ]) {
      addMesh(new THREE.CircleGeometry(3.1, 32), metal, x, y, 0.1);
      addMesh(new THREE.BoxGeometry(2.3, 0.65, 0.2), charcoal, x, y, 0.25);
      addMesh(new THREE.BoxGeometry(0.65, 2.3, 0.2), charcoal, x, y, 0.25);
    }
    // Center the enclosure around its full height, preserving all CAD part alignment.
    const assembly = new THREE.Group();
    group.position.set(0, 35, 8.5);
    assembly.add(group);
    assembly.rotation.set(initial.x, initial.y, -0.22);
    scene.add(assembly);
    const camera = new THREE.PerspectiveCamera(30, 1, 168, 388);
    camera.position.set(0, 0, 290);
    scene.add(new THREE.HemisphereLight(0xfff9e8, 0x636a63, 3));
    const key = new THREE.DirectionalLight(0xfff9ee, 4);
    key.position.set(-70, 90, 130);
    scene.add(key);
    const rim = new THREE.DirectionalLight(0xffffff, 2.5);
    rim.position.set(90, 20, -30);
    scene.add(rim);
    const fill = new THREE.DirectionalLight(0xf5a172, 0.7);
    fill.position.set(0, -60, 100);
    scene.add(fill);
    const canvas = renderer.domElement;
    canvas.setAttribute("role", "img");
    canvas.setAttribute(
      "aria-label",
      "Interactive 3D Ble(e)p. Drag or use arrow keys to rotate. Click the side button, or press Enter or Space, to press it. Home resets the view.",
    );
    canvas.tabIndex = 0;
    let pressTarget = 0;
    let pressDepth = 0;
    const draw = () => {
      frame = 0;
      if (!visible || document.hidden || lost || disposed) return;
      const distance =
        Math.abs(assembly.rotation.x - target.x) +
        Math.abs(assembly.rotation.y - target.y);
      const ease = reduced.matches ? 1 : 0.15;
      assembly.rotation.x += (target.x - assembly.rotation.x) * ease;
      assembly.rotation.y += (target.y - assembly.rotation.y) * ease;
      pressDepth += (pressTarget - pressDepth) * (reduced.matches ? 1 : 0.4);
      actionMesh.position.set(-0.48 * pressDepth, -0.48 * pressDepth, 0);
      renderer.render(scene, camera);
      if (
        (distance > 0.001 || Math.abs(pressDepth - pressTarget) > 0.001) &&
        !reduced.matches
      )
        frame = requestAnimationFrame(draw);
    };
    const requestDraw = () => {
      if (!frame && !disposed && !lost) frame = requestAnimationFrame(draw);
    };
    const resize = () => {
      const { width, height } = wrap.getBoundingClientRect();
      if (!width || !height) return;
      camera.aspect = width / height;
      // Keep the entire enclosure visible on narrow portrait canvases.
      camera.position.z = Math.max(278, 155 / camera.aspect);
      // Bound the whole rotating device while preserving depth precision on
      // mobile GPUs; a near plane at 0.1 wastes almost all available precision.
      camera.near = camera.position.z - 110;
      camera.far = camera.position.z + 110;
      camera.updateProjectionMatrix();
      renderer.setSize(width, height, false);
      requestDraw();
    };
    wrap.append(canvas);
    resize();
    renderer.render(scene, camera);
    wrap.classList.add("is-loaded");
    controls.hidden = false;
    finishes.hidden = false;
    const bodyColors = {
      ivory: 0xeee9d9,
      graphite: 0x202321,
      sand: 0xc9b58f,
      stone: 0xd3d2cc,
      orange: 0xed870b,
    };
    const buttonColors = { orange: 0xeb6030, blue: 0x236fa7, black: 0x151713 };
    const bindFinishes = (kind, colors, material) => {
      const buttons = finishes.querySelectorAll(`[data-${kind}-finish]`);
      buttons.forEach((button) =>
        button.addEventListener("click", () => {
          const value = button.getAttribute(`data-${kind}-finish`);
          material.color.setHex(colors[value]);
          if (kind === "body") stone.value = Number(value === "stone");
          buttons.forEach((item) =>
            item.setAttribute("aria-pressed", String(item === button)),
          );
          finishes.querySelector(`[data-${kind}-label]`).textContent =
            button.title;
          requestDraw();
        }),
      );
    };
    bindFinishes("body", bodyColors, ivory);
    bindFinishes("button", buttonColors, orange);
    const setPressed = (value) => {
      pressTarget = Number(value);
      wrap.dataset.buttonPressed = String(value);
      requestDraw();
    };
    const activateAction = () => {
      if (lost || disposed) return;
      clearTimeout(pressTimer);
      setPressed(true);
      pressTimer = setTimeout(() => setPressed(false), 160);
    };
    const reset = () => {
      Object.assign(target, initial);
      requestDraw();
    };
    document.querySelectorAll("[data-rotate]").forEach((button) =>
      button.addEventListener("click", () => {
        target.y += Number(button.dataset.rotate) * 0.45;
        requestDraw();
      }),
    );
    document
      .querySelector("[data-model-reset]")
      .addEventListener("click", reset);
    canvas.addEventListener("keydown", (event) => {
      if (event.key === "Enter" || event.key === " ") {
        event.preventDefault();
        if (!event.repeat) activateAction();
        return;
      }
      if (
        !["ArrowLeft", "ArrowRight", "ArrowUp", "ArrowDown", "Home"].includes(
          event.key,
        )
      )
        return;
      event.preventDefault();
      if (event.key === "Home") reset();
      if (event.key === "ArrowLeft") target.y -= 0.25;
      if (event.key === "ArrowRight") target.y += 0.25;
      if (event.key === "ArrowUp") target.x = Math.max(-0.65, target.x - 0.15);
      if (event.key === "ArrowDown") target.x = Math.min(0.65, target.x + 0.15);
      requestDraw();
    });
    let drag = null;
    const raycaster = new THREE.Raycaster();
    const pointer = new THREE.Vector2();
    const hitsAction = (event) => {
      if (lost || disposed) return false;
      const rect = canvas.getBoundingClientRect();
      // A few nearby rays make the narrow side button easier to tap. Each ray
      // must hit the button first, so the case still occludes it from the back.
      scene.updateMatrixWorld(true);
      for (const [dx, dy] of [
        [0, 0],
        [-5, 0],
        [5, 0],
        [0, -5],
        [0, 5],
      ]) {
        pointer.set(
          ((event.clientX + dx - rect.left) / rect.width) * 2 - 1,
          -((event.clientY + dy - rect.top) / rect.height) * 2 + 1,
        );
        raycaster.setFromCamera(pointer, camera);
        if (
          raycaster.intersectObjects(group.children, false)[0]?.object ===
          actionMesh
        )
          return true;
      }
      return false;
    };
    canvas.addEventListener("pointerdown", (event) => {
      if (event.button !== 0 || drag) return;
      drag = {
        id: event.pointerId,
        x: event.clientX,
        y: event.clientY,
        rx: target.x,
        ry: target.y,
        action: hitsAction(event),
        moved: false,
      };
      canvas.setPointerCapture(event.pointerId);
    });
    canvas.addEventListener("pointermove", (event) => {
      if (!drag) {
        canvas.classList.toggle("over-action", hitsAction(event));
        return;
      }
      if (event.pointerId !== drag.id) return;
      if (
        !drag.moved &&
        Math.hypot(event.clientX - drag.x, event.clientY - drag.y) < 6
      )
        return;
      drag.moved = true;
      target.y = drag.ry + (event.clientX - drag.x) * 0.009;
      if (event.pointerType === "mouse")
        target.x = Math.max(
          -0.65,
          Math.min(0.65, drag.rx + (event.clientY - drag.y) * 0.006),
        );
      requestDraw();
    });
    const endDrag = (event) => {
      if (!drag || event.pointerId !== drag.id) return;
      const activate = event.type === "pointerup" && drag.action && !drag.moved;
      drag = null;
      if (activate) activateAction();
    };
    ["pointerup", "pointercancel", "lostpointercapture"].forEach((name) =>
      canvas.addEventListener(name, endDrag),
    );
    canvas.addEventListener("pointerleave", () =>
      canvas.classList.remove("over-action"),
    );
    const sizeObserver = new ResizeObserver(resize);
    sizeObserver.observe(wrap);
    const intersection = new IntersectionObserver((entries) => {
      visible = entries[0].isIntersecting;
      syncTour();
      if (visible) requestDraw();
    });
    intersection.observe(wrap);
    document.addEventListener("visibilitychange", () => {
      syncTour();
      if (!document.hidden) requestDraw();
    });
    reduced.addEventListener("change", requestDraw);
    canvas.addEventListener("webglcontextlost", (event) => {
      event.preventDefault();
      lost = true;
      syncTour();
      tourPanel.hidden = true;
      cancelAnimationFrame(frame);
      frame = 0;
      wrap.classList.remove("is-loaded");
      controls.hidden = true;
      finishes.hidden = true;
      drag = null;
      clearTimeout(pressTimer);
      setPressed(false);
      canvas.hidden = true;
    });
    canvas.addEventListener("webglcontextrestored", () => {
      lost = false;
      tourPanel.hidden = !screenTour;
      syncTour();
      canvas.hidden = false;
      wrap.classList.add("is-loaded");
      controls.hidden = false;
      finishes.hidden = false;
      requestDraw();
    });
    window.addEventListener("pagehide", (event) => {
      screenTour?.setActive(false);
      if (event.persisted) return;
      disposed = true;
      cancelAnimationFrame(frame);
      sizeObserver.disconnect();
      intersection.disconnect();
      release();
    });
    window.addEventListener("pageshow", () => {
      syncTour();
      requestDraw();
    });
    // Keep the static screen if the optional tour or its captures are unavailable.
    try {
      const { createScreenTour } = await import(
        "./screen-tour.js?v=20260906-3"
      );
      let tourTexture;
      screenTour = await createScreenTour(() => {
        if (tourTexture) {
          tourTexture.needsUpdate = true;
          requestDraw();
        }
      });
      if (disposed) {
        screenTour.destroy();
        return;
      }
      tourTexture = new THREE.CanvasTexture(screenTour.canvas);
      tourTexture.colorSpace = THREE.SRGBColorSpace;
      resources.add(tourTexture);
      screenMaterial.map = tourTexture;
      screenMaterial.needsUpdate = true;
      tourPanel.hidden = lost;
      syncTour();
      requestDraw();
    } catch {
      tourPanel.hidden = true;
    }
  } catch (error) {
    renderer?.domElement.remove();
    wrap.classList.remove("is-loaded");
    controls.hidden = true;
    finishes.hidden = true;
    release();
    // A static device illustration remains available if graphics or assets fail.
  }
}
