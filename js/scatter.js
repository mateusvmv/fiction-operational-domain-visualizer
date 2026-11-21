// scatter.js
const Scatter = (() => {
  const COLORS = {
    background:     "#fdfdfd",
    axes:           "#555",
    grid:           "#eee",
    operational:    "#2e7d32",
    nonOperational: "#c62828",
    sourcePoint:    "#ffffff",
    highlight:      "#ff9800"
  };

  const HIT_RADIUS_SQ = 400;
  const DEFAULT_PAD = 50;
  const views = new WeakMap();
  let tooltipEl = null;

  function computeEqualSpanBounds(radialData, sourcePoint, sweep) {
    let minX = Infinity, maxX = -Infinity;
    let minY = Infinity, maxY = -Infinity;
    let pointCount = 0;

    const includePoint = (p, countAsData = false) => {
      if (!p) return;
      let used = false;
      if (Number.isFinite(p.epsilon_r)) {
        if (p.epsilon_r < minX) minX = p.epsilon_r;
        if (p.epsilon_r > maxX) maxX = p.epsilon_r;
        used = true;
      }
      if (Number.isFinite(p.lambda_tf)) {
        if (p.lambda_tf < minY) minY = p.lambda_tf;
        if (p.lambda_tf > maxY) maxY = p.lambda_tf;
        used = true;
      }
      if (countAsData && used) pointCount++;
    };

    if (radialData && Array.isArray(radialData.points)) {
      for (const p of radialData.points) includePoint(p, true);
    }

    if (pointCount === 0) {
      const hasSweep =
        sweep?.epsilon_r?.min != null && sweep?.epsilon_r?.max != null &&
        sweep?.lambda_tf?.min != null && sweep?.lambda_tf?.max != null;
      if (hasSweep) {
        minX = sweep.epsilon_r.min;
        maxX = sweep.epsilon_r.max;
        minY = sweep.lambda_tf.min;
        maxY = sweep.lambda_tf.max;
        pointCount = 1;
      }
    }

    includePoint(sourcePoint, false);

    if (!Number.isFinite(minX) || !Number.isFinite(maxX) ||
        !Number.isFinite(minY) || !Number.isFinite(maxY)) {
      minX = 0; maxX = 10; minY = 0; maxY = 10;
    }

    const rawSpanX = maxX - minX;
    const rawSpanY = maxY - minY;
    const padX = rawSpanX > 0 ? rawSpanX * 0.05 : Math.max(Math.abs(minX) * 0.1, 0.5);
    const padY = rawSpanY > 0 ? rawSpanY * 0.05 : Math.max(Math.abs(minY) * 0.1, 0.5);

    const paddedMinX = minX - padX;
    const paddedMaxX = maxX + padX;
    const paddedMinY = minY - padY;
    const paddedMaxY = maxY + padY;

    const spanX = Math.max(paddedMaxX - paddedMinX, 1e-6);
    const spanY = Math.max(paddedMaxY - paddedMinY, 1e-6);
    const equalSpan = Math.max(spanX, spanY);

    const centerX = (paddedMinX + paddedMaxX) / 2;
    const centerY = (paddedMinY + paddedMaxY) / 2;

    return {
      xMin: centerX - equalSpan / 2,
      xMax: centerX + equalSpan / 2,
      yMin: centerY - equalSpan / 2,
      yMax: centerY + equalSpan / 2
    };
  }

  function createView(bounds) {
    const { xMin, xMax, yMin, yMax } = bounds;
    return {
      initialized: false,
      lastDataRef: null,
      data: null, sourcePoint: null,
      closestNonOp: null,
      xMin, xMax, yMin, yMax,
      initial: { xMin, xMax, yMin, yMax },
      pad: DEFAULT_PAD, plotW: 0, plotH: 0,
      toX: () => 0, toY: () => 0,
      dragging: false, dragStartX: 0, dragStartY: 0, dragStartView: null,
      touchMode: null,
      pinchStartDist: 0,
      pinchStartView: null,
      pinchAnchorData: null,
      pinchAnchorFrac: null,
      touchMoved: false
    };
  }

  function getView(canvas, radialData, sourcePoint, sweep) {
    const fitted = computeEqualSpanBounds(radialData, sourcePoint, sweep);

    if (!views.has(canvas)) {
      views.set(canvas, createView(fitted));
    }
    const v = views.get(canvas);

    const hasData = Boolean(
      (radialData && Array.isArray(radialData.points) && radialData.points.length > 0) ||
      sourcePoint
    );

    const isNewDataset = radialData && radialData !== v.lastDataRef;

    if ((!v.initialized || isNewDataset) && hasData) {
      v.initial = { ...fitted };
      v.xMin = fitted.xMin;
      v.xMax = fitted.xMax;
      v.yMin = fitted.yMin;
      v.yMax = fitted.yMax;
      v.initialized = true;
      v.lastDataRef = radialData;
    }

    return v;
  }

  function computeClosestNonOp(view) {
    if (!view.data || !view.sourcePoint || !view.data.points) return null;
    const sp = view.sourcePoint;
    let best = null, bestD2 = Infinity;
    for (const p of view.data.points) {
      if (p.operational) continue;
      const dx = p.epsilon_r - sp.epsilon_r;
      const dy = p.lambda_tf - sp.lambda_tf;
      const d2 = dx * dx + dy * dy;
      if (d2 < bestD2) { bestD2 = d2; best = p; }
    }
    if (!best) return null;
    return { point: best, distance: Math.sqrt(bestD2) };
  }

  function draw(canvas, radialData, sourcePoint, sweep) {
    const view = getView(canvas, radialData, sourcePoint, sweep);
    view.data         = radialData;
    view.sourcePoint  = sourcePoint;
    view.closestNonOp = computeClosestNonOp(view);
    attachHandlers(canvas);
    render(canvas);
  }

  function niceTicks(min, max, targetCount) {
    const range = max - min;
    if (range <= 0 || !Number.isFinite(range)) return [];
    const rawStep = range / targetCount;
    const mag  = Math.pow(10, Math.floor(Math.log10(rawStep)));
    const norm = rawStep / mag;
    let step;
    if (norm < 1.5)      step = 1 * mag;
    else if (norm < 3)   step = 2 * mag;
    else if (norm < 7)   step = 5 * mag;
    else                 step = 10 * mag;
    const ticks = [];
    const start = Math.ceil(min / step) * step;
    for (let v = start; v <= max + 1e-9; v += step) ticks.push(v);
    return ticks;
  }

  function fmtTick(v) {
    if (Math.abs(v - Math.round(v)) < 1e-9) return v.toFixed(0);
    if (Math.abs(v) < 0.01 && v !== 0) return v.toExponential(1);
    return v.toFixed(2).replace(/\.?0+$/, "");
  }

  function render(canvas) {
    const view = views.get(canvas);
    if (!view || !view.data) return;

    const ctx = canvas.getContext("2d");
    const W = canvas.width;
    const H = canvas.height;

    ctx.fillStyle = COLORS.background;
    ctx.fillRect(0, 0, W, H);

    const pad   = DEFAULT_PAD;
    const plotW = Math.max(1, W - pad * 2);
    const plotH = Math.max(1, H - pad * 2);

    const spanX = (view.xMax - view.xMin) || 1;
    const spanY = (view.yMax - view.yMin) || 1;

    const toX = v => pad + (v - view.xMin) / spanX * plotW;
    const toY = v => pad + plotH - (v - view.yMin) / spanY * plotH;

    const xTicks = niceTicks(view.xMin, view.xMax, 8);
    const yTicks = niceTicks(view.yMin, view.yMax, 8);

    ctx.strokeStyle = COLORS.grid;
    ctx.lineWidth = 1;
    for (const t of xTicks) {
      const px = toX(t);
      if (px < pad || px > pad + plotW) continue;
      ctx.beginPath(); ctx.moveTo(px, pad); ctx.lineTo(px, pad + plotH); ctx.stroke();
    }
    for (const t of yTicks) {
      const py = toY(t);
      if (py < pad || py > pad + plotH) continue;
      ctx.beginPath(); ctx.moveTo(pad, py); ctx.lineTo(pad + plotW, py); ctx.stroke();
    }

    ctx.strokeStyle = COLORS.axes;
    ctx.beginPath();
    ctx.moveTo(pad, pad);
    ctx.lineTo(pad, pad + plotH);
    ctx.lineTo(pad + plotW, pad + plotH);
    ctx.stroke();

    ctx.fillStyle = "#333";
    ctx.font = "12px sans-serif";
    ctx.textAlign = "center";
    ctx.fillText("epsilon_r", W / 2, H - 10);
    ctx.save();
    ctx.translate(12, H / 2);
    ctx.rotate(-Math.PI / 2);
    ctx.fillText("lambda_tf", 0, 0);
    ctx.restore();

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

    // all points
    if (Array.isArray(view.data.points)) {
      for (const p of view.data.points) {
        if (p.epsilon_r < view.xMin || p.epsilon_r > view.xMax) continue;
        if (p.lambda_tf < view.yMin || p.lambda_tf > view.yMax) continue;
        ctx.fillStyle = p.operational ? COLORS.operational : COLORS.nonOperational;
        ctx.beginPath();
        ctx.arc(toX(p.epsilon_r), toY(p.lambda_tf), 3, 0, 2 * Math.PI);
        ctx.fill();
      }
    }

    // closest non-op: dashed connector + halo
    if (view.closestNonOp && view.sourcePoint) {
      const cn = view.closestNonOp;
      const cx = toX(cn.point.epsilon_r);
      const cy = toY(cn.point.lambda_tf);
      const sx = toX(view.sourcePoint.epsilon_r);
      const sy = toY(view.sourcePoint.lambda_tf);

      ctx.strokeStyle = COLORS.highlight;
      ctx.lineWidth = 1.5;
      ctx.setLineDash([4, 3]);
      ctx.beginPath();
      ctx.moveTo(sx, sy);
      ctx.lineTo(cx, cy);
      ctx.stroke();
      ctx.setLineDash([]);

      ctx.strokeStyle = COLORS.highlight;
      ctx.lineWidth = 2;
      ctx.beginPath();
      ctx.arc(cx, cy, 7, 0, 2 * Math.PI);
      ctx.stroke();
    }

    // source point (last so it sits on top of the connector)
    if (view.sourcePoint) {
      const sx = toX(view.sourcePoint.epsilon_r);
      const sy = toY(view.sourcePoint.lambda_tf);
      ctx.fillStyle   = COLORS.sourcePoint;
      ctx.strokeStyle = "#000";
      ctx.lineWidth   = 2;
      ctx.beginPath(); ctx.arc(sx, sy, 6, 0, 2 * Math.PI); ctx.fill(); ctx.stroke();
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
      tooltipEl.style.cssText = [
        "position: fixed",
        "pointer-events: none",
        "background: rgba(20, 20, 20, 0.92)",
        "color: #fff",
        "padding: 6px 10px",
        "border-radius: 4px",
        "font-size: 12px",
        "font-family: ui-monospace, SFMono-Regular, Menlo, monospace",
        "z-index: 99999",
        "display: none",
        "white-space: nowrap",
        "box-shadow: 0 2px 8px rgba(0,0,0,0.25)"
      ].join(";");
      document.body.appendChild(tooltipEl);
    }
    return tooltipEl;
  }

  function showTooltip(clientX, clientY, point, isSource, isClosestNonOp, distanceToSource) {
    const tip = getTooltip();

    let status, color;
    if (isSource) {
      status = "Source point";
      color  = "#ffffff";
    } else {
      status = point.operational ? "OPERATIONAL" : "NON-OP";
      color  = point.operational ? "#4caf50" : "#ef5350";
    }

    let html =
      `<strong>(${Number(point.epsilon_r).toFixed(3)}, ${Number(point.lambda_tf).toFixed(3)})</strong><br>` +
      `<span style="color:${color}">${status}</span>`;

    if (!isSource && distanceToSource !== undefined && distanceToSource !== null) {
      html += `<br>d = ${distanceToSource.toFixed(4)}`;
    }

    if (isClosestNonOp) {
      html += `<br><span style="color:${COLORS.highlight}">Closest non-operational point to the source</span>`;
    }

    tip.innerHTML = html;
    tip.style.display = "block";

    const offsetX = 14, offsetY = 14;
    const rect = tip.getBoundingClientRect();
    let left = clientX + offsetX;
    let top  = clientY + offsetY;
    if (left + rect.width  > window.innerWidth)  left = clientX - rect.width  - offsetX;
    if (top  + rect.height > window.innerHeight) top  = clientY - rect.height - offsetY;
    tip.style.left = Math.max(4, left) + "px";
    tip.style.top  = Math.max(4, top)  + "px";
  }

  function hideTooltip() {
    if (tooltipEl) tooltipEl.style.display = "none";
  }

  function inspectPointAt(canvas, view, mx, my, clientX, clientY) {
    if (!view.data || !Array.isArray(view.data.points)) return false;
    if (mx < view.pad || mx > view.pad + view.plotW ||
        my < view.pad || my > view.pad + view.plotH) {
      hideTooltip();
      return false;
    }

    let best = null, bestD2 = Infinity, bestIsSource = false;
    const consider = (p, isSource) => {
      if (!p) return;
      const dx = view.toX(p.epsilon_r) - mx;
      const dy = view.toY(p.lambda_tf) - my;
      const d2 = dx * dx + dy * dy;
      if (d2 < bestD2) { bestD2 = d2; best = p; bestIsSource = isSource; }
    };
    for (const p of view.data.points) consider(p, false);
    if (view.sourcePoint) consider(view.sourcePoint, true);

    if (best && bestD2 < HIT_RADIUS_SQ) {
      const isClosestNonOp = !bestIsSource &&
                             view.closestNonOp &&
                             view.closestNonOp.point === best;

      const distanceToSource = bestIsSource || !view.sourcePoint
        ? null
        : Math.hypot(
            best.epsilon_r - view.sourcePoint.epsilon_r,
            best.lambda_tf - view.sourcePoint.lambda_tf
          );

      showTooltip(
        clientX, clientY, best, bestIsSource,
        isClosestNonOp, distanceToSource
      );
      return true;
    }

    hideTooltip();
    return false;
  }

  // ---------- interaction ----------
  function attachHandlers(canvas) {
    if (canvas.__scatterAttached) return;
    canvas.__scatterAttached = true;
    canvas.style.touchAction = "none";

    canvas.addEventListener("mousemove", onMouseMove);
    canvas.addEventListener("mouseleave", onMouseLeave);
    canvas.addEventListener("mousedown", onMouseDown);
    canvas.addEventListener("mouseup", onMouseUp);
    canvas.addEventListener("wheel", onWheel, { passive: false });
    canvas.addEventListener("dblclick", onDblClick);

    canvas.addEventListener("touchstart", onTouchStart, { passive: false });
    canvas.addEventListener("touchmove", onTouchMove, { passive: false });
    canvas.addEventListener("touchend", onTouchEnd, { passive: false });
    canvas.addEventListener("touchcancel", onTouchCancel, { passive: false });
  }

  function clientCoords(canvas, clientX, clientY) {
    const rect = canvas.getBoundingClientRect();
    const scaleX = rect.width  > 0 ? canvas.width  / rect.width  : 1;
    const scaleY = rect.height > 0 ? canvas.height / rect.height : 1;
    return {
      mx: (clientX - rect.left) * scaleX,
      my: (clientY - rect.top)  * scaleY,
      scaleX,
      scaleY
    };
  }

  function canvasCoords(canvas, e) {
    return clientCoords(canvas, e.clientX, e.clientY);
  }

  function getTouchDist(t1, t2) {
    return Math.hypot(t2.clientX - t1.clientX, t2.clientY - t1.clientY);
  }

  function getTouchMidpoint(t1, t2) {
    return {
      clientX: (t1.clientX + t2.clientX) / 2,
      clientY: (t1.clientY + t2.clientY) / 2
    };
  }

  function startTouchPan(view, touch) {
    view.touchMode = "pan";
    view.dragStartX = touch.clientX;
    view.dragStartY = touch.clientY;
    view.dragStartView = {
      xMin: view.xMin, xMax: view.xMax,
      yMin: view.yMin, yMax: view.yMax
    };
    view.touchMoved = false;
  }

  function startTouchPinch(canvas, view, t1, t2) {
    const dist = getTouchDist(t1, t2);
    if (dist < 1) return;
    const mid = getTouchMidpoint(t1, t2);
    const { mx, my } = clientCoords(canvas, mid.clientX, mid.clientY);

    const spanX = (view.xMax - view.xMin) || 1;
    const spanY = (view.yMax - view.yMin) || 1;
    const plotW = view.plotW || 1;
    const plotH = view.plotH || 1;

    const fx = Math.min(1, Math.max(0, (mx - view.pad) / plotW));
    const fy = Math.min(1, Math.max(0, (my - view.pad) / plotH));

    const dataX = view.xMin + (mx - view.pad) / plotW * spanX;
    const dataY = view.yMax - (my - view.pad) / plotH * spanY;

    view.touchMode = "pinch";
    view.pinchStartDist = dist;
    view.pinchStartView = {
      xMin: view.xMin, xMax: view.xMax,
      yMin: view.yMin, yMax: view.yMax
    };
    view.pinchAnchorData = { dataX, dataY };
    view.pinchAnchorFrac = { fx, fy };
    view.touchMoved = true;
  }

  function onTouchStart(e) {
    const canvas = e.currentTarget;
    const view = views.get(canvas);
    if (!view) return;
    e.preventDefault();
    hideTooltip();

    if (e.touches.length === 1) {
      startTouchPan(view, e.touches[0]);
    } else if (e.touches.length >= 2) {
      startTouchPinch(canvas, view, e.touches[0], e.touches[1]);
    }
  }

  function onTouchMove(e) {
    const canvas = e.currentTarget;
    const view = views.get(canvas);
    if (!view) return;
    e.preventDefault();

    if (e.touches.length >= 2) {
      if (view.touchMode !== "pinch") {
        startTouchPinch(canvas, view, e.touches[0], e.touches[1]);
      }
      const dist = getTouchDist(e.touches[0], e.touches[1]);
      if (dist < 1 || !view.pinchStartDist || !view.pinchStartView) return;

      const scale = view.pinchStartDist / dist;
      const startSpan = Math.max(
        (view.pinchStartView.xMax - view.pinchStartView.xMin) || 1,
        (view.pinchStartView.yMax - view.pinchStartView.yMin) || 1
      );

      const newRange = startSpan * scale;

      const { dataX, dataY } = view.pinchAnchorData;
      const { fx, fy } = view.pinchAnchorFrac;

      view.xMin = dataX - fx * newRange;
      view.xMax = dataX + (1 - fx) * newRange;
      view.yMin = dataY - (1 - fy) * newRange;
      view.yMax = dataY + fy * newRange;

      render(canvas);
      return;
    }

    if (e.touches.length === 1 && view.touchMode === "pan" && view.dragStartView) {
      const t = e.touches[0];
      const dxClient = t.clientX - view.dragStartX;
      const dyClient = t.clientY - view.dragStartY;
      if (Math.hypot(dxClient, dyClient) > 4) {
        view.touchMoved = true;
      }

      const { scaleX, scaleY } = clientCoords(canvas, t.clientX, t.clientY);
      const dx = dxClient * scaleX;
      const dy = dyClient * scaleY;
      const rangeX = (view.dragStartView.xMax - view.dragStartView.xMin) || 1;
      const rangeY = (view.dragStartView.yMax - view.dragStartView.yMin) || 1;
      const shiftX = -dx / (view.plotW || 1) * rangeX;
      const shiftY =  dy / (view.plotH || 1) * rangeY;

      view.xMin = view.dragStartView.xMin + shiftX;
      view.xMax = view.dragStartView.xMax + shiftX;
      view.yMin = view.dragStartView.yMin + shiftY;
      view.yMax = view.dragStartView.yMax + shiftY;

      render(canvas);
    }
  }

  function onTouchEnd(e) {
    const canvas = e.currentTarget;
    const view = views.get(canvas);
    if (!view) return;
    e.preventDefault();

    if (e.touches.length === 0) {
      if (view.touchMode === "pan" && !view.touchMoved && e.changedTouches.length > 0) {
        const t = e.changedTouches[0];
        const { mx, my } = clientCoords(canvas, t.clientX, t.clientY);
        inspectPointAt(canvas, view, mx, my, t.clientX, t.clientY);
      }
      view.touchMode = null;
      view.dragStartView = null;
      view.pinchStartView = null;
      view.touchMoved = false;
    } else if (e.touches.length === 1) {
      startTouchPan(view, e.touches[0]);
      view.touchMoved = true;
    }
  }

  function onTouchCancel(e) {
    const canvas = e.currentTarget;
    const view = views.get(canvas);
    if (!view) return;
    e.preventDefault();
    view.touchMode = null;
    view.dragStartView = null;
    view.pinchStartView = null;
    view.touchMoved = false;
    hideTooltip();
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

    if (view.dragging) {
      const { scaleX, scaleY } = canvasCoords(canvas, e);
      const dx = (e.clientX - view.dragStartX) * scaleX;
      const dy = (e.clientY - view.dragStartY) * scaleY;
      const rangeX = (view.dragStartView.xMax - view.dragStartView.xMin) || 1;
      const rangeY = (view.dragStartView.yMax - view.dragStartView.yMin) || 1;
      const shiftX = -dx / (view.plotW || 1) * rangeX;
      const shiftY =  dy / (view.plotH || 1) * rangeY;
      view.xMin = view.dragStartView.xMin + shiftX;
      view.xMax = view.dragStartView.xMax + shiftX;
      view.yMin = view.dragStartView.yMin + shiftY;
      view.yMax = view.dragStartView.yMax + shiftY;
      render(canvas);
      hideTooltip();
      return;
    }

    if (!view.data) return;
    const { mx, my } = canvasCoords(canvas, e);
    inspectPointAt(canvas, view, mx, my, e.clientX, e.clientY);
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
    const spanX = (view.xMax - view.xMin) || 1;
    const spanY = (view.yMax - view.yMin) || 1;
    const plotW = view.plotW || 1;
    const plotH = view.plotH || 1;

    const dataX = view.xMin + (mx - view.pad) / plotW * spanX;
    const dataY = view.yMax - (my - view.pad) / plotH * spanY;

    const factor   = e.deltaY < 0 ? 0.85 : 1 / 0.85;
    const newRange = Math.max(spanX, spanY) * factor;
    const fx = (dataX - view.xMin) / spanX;
    const fy = (dataY - view.yMin) / spanY;

    view.xMin = dataX - fx * newRange;
    view.xMax = dataX + (1 - fx) * newRange;
    view.yMin = dataY - fy * newRange;
    view.yMax = dataY + (1 - fy) * newRange;

    render(canvas);
    hideTooltip();
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