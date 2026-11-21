// js/scatter.js
const Scatter = (() => {
  const COLORS = {
    background:     "#fdfdfd",
    axes:           "#555",
    grid:           "#eee",
    operational:    "#2e7d32",
    nonOperational: "#c62828",
    sourcePoint:    "#ffffff"
  };

  const views = new WeakMap();      // canvas -> per-canvas view state
  let tooltipEl = null;

  function createView(sweep) {
    const xMin = sweep?.epsilon_r?.min ?? 1;
    const xMax = sweep?.epsilon_r?.max ?? 10;
    const yMin = sweep?.lambda_tf?.min ?? 1;
    const yMax = sweep?.lambda_tf?.max ?? 10;
    return {
      data: null,
      sourcePoint: null,
      xMin, xMax, yMin, yMax,
      initial: { xMin, xMax, yMin, yMax },
      pad: 50, plotW: 0, plotH: 0,
      toX: () => 0,
      toY: () => 0,
      dragging: false,
      dragStartX: 0,
      dragStartY: 0,
      dragStartView: null
    };
  }

  function getView(canvas, sweep) {
    if (!views.has(canvas)) views.set(canvas, createView(sweep));
    const v = views.get(canvas);

    const s = {
      xMin: sweep?.epsilon_r?.min ?? 1,
      xMax: sweep?.epsilon_r?.max ?? 10,
      yMin: sweep?.lambda_tf?.min ?? 1,
      yMax: sweep?.lambda_tf?.max ?? 10
    };
    // If the sweep for this experiment differs from the one the view was
    // initialised with, drop zoom/pan so the new experiment starts centred.
    if (v.initial.xMin !== s.xMin || v.initial.xMax !== s.xMax ||
        v.initial.yMin !== s.yMin || v.initial.yMax !== s.yMax) {
      v.initial = s;
      v.xMin = s.xMin; v.xMax = s.xMax;
      v.yMin = s.yMin; v.yMax = s.yMax;
    }
    return v;
  }

  // ---------- draw ----------
  function draw(canvas, radialData, sourcePoint, sweep) {
    const view = getView(canvas, sweep);
    view.data = radialData;
    view.sourcePoint = sourcePoint;
    attachHandlers(canvas);
    render(canvas);
  }

  function niceTicks(min, max, targetCount) {
    const range = max - min;
    if (range <= 0) return [];
    const rawStep = range / targetCount;
    const mag = Math.pow(10, Math.floor(Math.log10(rawStep)));
    const norm = rawStep / mag;
    let step;
    if (norm < 1.5) step = 1 * mag;
    else if (norm < 3) step = 2 * mag;
    else if (norm < 7) step = 5 * mag;
    else step = 10 * mag;

    const ticks = [];
    const start = Math.ceil(min / step) * step;
    for (let v = start; v <= max + 1e-9; v += step) ticks.push(v);
    return ticks;
  }

  function fmtTick(v) {
    if (Math.abs(v - Math.round(v)) < 1e-9) return v.toFixed(0);
    return v.toFixed(1);
  }

  function render(canvas) {
    const view = views.get(canvas);
    if (!view || !view.data) return;

    const ctx = canvas.getContext("2d");
    const W = canvas.width;
    const H = canvas.height;

    ctx.fillStyle = COLORS.background;
    ctx.fillRect(0, 0, W, H);

    const pad   = 50;
    const plotW = W - pad * 2;
    const plotH = H - pad * 2;

    const toX = v => pad + (v - view.xMin) / (view.xMax - view.xMin) * plotW;
    const toY = v => pad + plotH - (v - view.yMin) / (view.yMax - view.yMin) * plotH;

    const xTicks = niceTicks(view.xMin, view.xMax, 8);
    const yTicks = niceTicks(view.yMin, view.yMax, 8);

    // grid
    ctx.strokeStyle = COLORS.grid;
    ctx.lineWidth = 1;
    for (const t of xTicks) {
      const px = toX(t);
      if (px < pad || px > pad + plotW) continue;
      ctx.beginPath();
      ctx.moveTo(px, pad);
      ctx.lineTo(px, pad + plotH);
      ctx.stroke();
    }
    for (const t of yTicks) {
      const py = toY(t);
      if (py < pad || py > pad + plotH) continue;
      ctx.beginPath();
      ctx.moveTo(pad, py);
      ctx.lineTo(pad + plotW, py);
      ctx.stroke();
    }

    // axes
    ctx.strokeStyle = COLORS.axes;
    ctx.lineWidth = 1;
    ctx.beginPath();
    ctx.moveTo(pad, pad);
    ctx.lineTo(pad, pad + plotH);
    ctx.lineTo(pad + plotW, pad + plotH);
    ctx.stroke();

    // axis titles
    ctx.fillStyle = "#333";
    ctx.font = "12px sans-serif";
    ctx.textAlign = "center";
    ctx.fillText("epsilon_r", W / 2, H - 10);
    ctx.save();
    ctx.translate(12, H / 2);
    ctx.rotate(-Math.PI / 2);
    ctx.fillText("lambda_tf", 0, 0);
    ctx.restore();

    // tick labels
    ctx.textAlign = "center";
    for (const t of xTicks) {
      const px = toX(t);
      if (px < pad - 5 || px > pad + plotW + 5) continue;
      ctx.fillText(fmtTick(t), px, pad + plotH + 16);
    }
    ctx.textAlign = "right";
    for (const t of yTicks) {
      const py = toY(t);
      if (py < pad - 5 || py > pad + plotH + 5) continue;
      ctx.fillText(fmtTick(t), pad - 6, py + 4);
    }

    // points (only those inside the current view)
    for (const p of view.data.points) {
      if (p.epsilon_r < view.xMin || p.epsilon_r > view.xMax) continue;
      if (p.lambda_tf < view.yMin || p.lambda_tf > view.yMax) continue;
      ctx.fillStyle = p.operational
        ? COLORS.operational
        : COLORS.nonOperational;
      ctx.beginPath();
      ctx.arc(toX(p.epsilon_r), toY(p.lambda_tf), 3, 0, 2 * Math.PI);
      ctx.fill();
    }

    // source point
    if (view.sourcePoint) {
      const sx = toX(view.sourcePoint.epsilon_r);
      const sy = toY(view.sourcePoint.lambda_tf);
      ctx.fillStyle   = COLORS.sourcePoint;
      ctx.strokeStyle = "#000";
      ctx.lineWidth   = 2;
      ctx.beginPath();
      ctx.arc(sx, sy, 6, 0, 2 * Math.PI);
      ctx.fill();
      ctx.stroke();

      ctx.lineWidth = 1;
      ctx.beginPath();
      ctx.moveTo(sx - 10, sy); ctx.lineTo(sx + 10, sy);
      ctx.moveTo(sx, sy - 10); ctx.lineTo(sx, sy + 10);
      ctx.stroke();
    }

    view.pad   = pad;
    view.plotW = plotW;
    view.plotH = plotH;
    view.toX   = toX;
    view.toY   = toY;
  }

  // ---------- tooltip ----------
  function getTooltip() {
    if (!tooltipEl) {
      tooltipEl = document.createElement("div");
      tooltipEl.className = "scatter-tooltip";
      document.body.appendChild(tooltipEl);
    }
    return tooltipEl;
  }

  function showTooltip(clientX, clientY, point, isSource) {
    const tip = getTooltip();
    const status = point.operational ? "OPERATIONAL" : "NON-OP";
    const color  = point.operational ? "#4caf50" : "#ef5350";
    tip.innerHTML =
      `<strong>(${point.epsilon_r.toFixed(3)}, ${point.lambda_tf.toFixed(3)})</strong><br>` +
      `<span style="color:${color}">${status}</span>` +
      (isSource ? " <em>(source)</em>" : "");
    tip.style.left    = (clientX + 12) + "px";
    tip.style.top     = (clientY + 12) + "px";
    tip.style.display = "block";
  }

  function hideTooltip() {
    if (tooltipEl) tooltipEl.style.display = "none";
  }

  // ---------- interaction ----------
  function attachHandlers(canvas) {
    if (canvas.__scatterAttached) return;
    canvas.__scatterAttached = true;

    canvas.addEventListener("mousemove", onMouseMove);
    canvas.addEventListener("mouseleave", onMouseLeave);
    canvas.addEventListener("mousedown", onMouseDown);
    canvas.addEventListener("mouseup", onMouseUp);
    canvas.addEventListener("wheel", onWheel, { passive: false });
    canvas.addEventListener("dblclick", onDblClick);
  }

  function canvasCoords(canvas, e) {
    const rect = canvas.getBoundingClientRect();
    return {
      mx: (e.clientX - rect.left) * (canvas.width  / rect.width),
      my: (e.clientY - rect.top ) * (canvas.height / rect.height),
      scaleX: canvas.width  / rect.width,
      scaleY: canvas.height / rect.height
    };
  }

  function onMouseDown(e) {
    const canvas = e.currentTarget;
    const view = views.get(canvas);
    if (!view) return;
    view.dragging = true;
    view.dragStartX = e.clientX;
    view.dragStartY = e.clientY;
    view.dragStartView = {
      xMin: view.xMin, xMax: view.xMax,
      yMin: view.yMin, yMax: view.yMax
    };
    canvas.style.cursor = "grabbing";
  }

  function onMouseUp(e) {
    const canvas = e.currentTarget;
    const view = views.get(canvas);
    if (!view) return;
    view.dragging = false;
    canvas.style.cursor = "default";
  }

  function onMouseMove(e) {
    const canvas = e.currentTarget;
    const view = views.get(canvas);
    if (!view) return;

    // ---- drag pan ----
    if (view.dragging) {
      const { scaleX, scaleY } = canvasCoords(canvas, e);
      const dx = (e.clientX - view.dragStartX) * scaleX;
      const dy = (e.clientY - view.dragStartY) * scaleY;

      const rangeX = view.dragStartView.xMax - view.dragStartView.xMin;
      const rangeY = view.dragStartView.yMax - view.dragStartView.yMin;

      const shiftX = -dx / view.plotW * rangeX;
      const shiftY =  dy / view.plotH * rangeY;

      view.xMin = view.dragStartView.xMin + shiftX;
      view.xMax = view.dragStartView.xMax + shiftX;
      view.yMin = view.dragStartView.yMin + shiftY;
      view.yMax = view.dragStartView.yMax + shiftY;

      render(canvas);
      hideTooltip();
      return;
    }

    // ---- hover tooltip ----
    if (!view.data) return;
    const { mx, my } = canvasCoords(canvas, e);
    if (mx < view.pad || mx > view.pad + view.plotW ||
        my < view.pad || my > view.pad + view.plotH) {
      hideTooltip();
      return;
    }

    let best = null, bestD2 = Infinity, bestIsSource = false;
    const consider = (p, isSource) => {
      const dx = view.toX(p.epsilon_r) - mx;
      const dy = view.toY(p.lambda_tf) - my;
      const d2 = dx * dx + dy * dy;
      if (d2 < bestD2) { bestD2 = d2; best = p; bestIsSource = isSource; }
    };
    for (const p of view.data.points) consider(p, false);
    if (view.sourcePoint) consider(view.sourcePoint, true);

    if (best && bestD2 < 100) {
      showTooltip(e.clientX, e.clientY, best, bestIsSource);
    } else {
      hideTooltip();
    }
  }

  function onMouseLeave(e) {
    hideTooltip();
    const canvas = e.currentTarget;
    const view = views.get(canvas);
    if (view) view.dragging = false;
    canvas.style.cursor = "default";
  }

  function onWheel(e) {
    e.preventDefault();
    const canvas = e.currentTarget;
    const view = views.get(canvas);
    if (!view) return;

    const { mx, my } = canvasCoords(canvas, e);

    // data coordinates of cursor
    const dataX = view.xMin + (mx - view.pad) / view.plotW * (view.xMax - view.xMin);
    const dataY = view.yMax - (my - view.pad) / view.plotH * (view.yMax - view.yMin);

    const factor = e.deltaY < 0 ? 0.85 : 1 / 0.85;
    const newRangeX = (view.xMax - view.xMin) * factor;
    const newRangeY = (view.yMax - view.yMin) * factor;

    const fx = (dataX - view.xMin) / (view.xMax - view.xMin);
    const fy = (dataY - view.yMin) / (view.yMax - view.yMin);

    view.xMin = dataX - fx * newRangeX;
    view.xMax = dataX + (1 - fx) * newRangeX;
    view.yMin = dataY - fy * newRangeY;
    view.yMax = dataY + (1 - fy) * newRangeY;

    render(canvas);
  }

  function onDblClick(e) {
    const canvas = e.currentTarget;
    const view = views.get(canvas);
    if (!view) return;
    view.xMin = view.initial.xMin;
    view.xMax = view.initial.xMax;
    view.yMin = view.initial.yMin;
    view.yMax = view.initial.yMax;
    render(canvas);
  }

  return { draw };
})();

window.Scatter = Scatter;