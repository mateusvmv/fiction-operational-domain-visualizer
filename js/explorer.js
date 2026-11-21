// explorer.js
const Explorer = (() => {
  const COLORS = {
    background: "#fdfdfd",
    grid:       "#eeeeee",
    negative:   "#00ADAE",   // blue — matches opdom-explorer
    neutral:    "#6e7175",   // grey
    positive:   "#E34857",   // red
    inputOutline:  "#1976d2",
    outputOutline: "#388e3c",
    logicOutline:  "#333333",
    emptyOutline:  "#bbbbbb",
    canvasOutline: "#fbc02d" // amber — the "workable area" boundary
  };

  // Single-SiDB row bounds, matching the C++ side's row_of() convention:
  //   row_of({X, 1, 1}) = 2*1 + 1 = 3
  //   row_of({X, 3, 1}) = 2*3 + 1 = 7
  const CANVAS_ROW_BOUNDS = { nMin: 0, nMax: 10, rowMin: 3, rowMax: 7 };

  // Lattice parameters (Si(100) 2x1, in Å).
  const LATTICE = {
    a1:    { x: 3.84, y: 0.0  },
    a2:    { x: 0.0,  y: 7.68 },
    basis: [
      { x: 0.0, y: 0.0  },
      { x: 0.0, y: 2.25 }
    ]
  };

  // Inverse of row_of: (row → y, z), matching fiction::sidb::site_at_row.
  function rowToSite(x, row) {
    const y = Math.floor(row / 2);
    const z = row - 2 * y;
    return { x, y, z };
  }

  function siteToNm(site) {
    const b = LATTICE.basis[site.z] || { x: 0, y: 0 };
    return {
      x_nm: (site.x * LATTICE.a1.x + site.y * LATTICE.a2.x + b.x) * 0.1,
      y_nm: (site.x * LATTICE.a1.y + site.y * LATTICE.a2.y + b.y) * 0.1
    };
  }

  function canvasBoundsNm() {
    const B = CANVAS_ROW_BOUNDS;

    // The four extreme sites of the row-based canvas.
    const corners = [
      rowToSite(B.nMin, B.rowMin),   // (3, 1, 1)
      rowToSite(B.nMax, B.rowMin),   // (7, 1, 1)
      rowToSite(B.nMin, B.rowMax),   // (3, 3, 1)
      rowToSite(B.nMax, B.rowMax)    // (7, 3, 1)
    ].map(siteToNm);

    return {
      xMin: Math.min(...corners.map(c => c.x_nm)),
      xMax: Math.max(...corners.map(c => c.x_nm)),
      yMin: Math.min(...corners.map(c => c.y_nm)),
      yMax: Math.max(...corners.map(c => c.y_nm))
    };
  }

  let patternData = null;
  let currentPattern = 0;

  function drawGrid(ctx, W, H, bbox) {
    const pad = 40;
    const spanX = Math.max(bbox.maxX - bbox.minX, 1e-6);
    const spanY = Math.max(bbox.maxY - bbox.minY, 1e-6);
    const scale = Math.min((W - pad * 2) / spanX, (H - pad * 2) / spanY);
    const toX = v => pad + (v - bbox.minX) * scale;
    const toY = v => H - pad - (v - bbox.minY) * scale;
    return { pad, scale, toX, toY };
  }

  function drawCanvasOutline(ctx, toX, toY) {
    const b = canvasBoundsNm();

    // Inset the rectangle by ~1 dot-radius worth of px so boundary dots
    // clearly sit inside. The C++ canvas is exactly bounded by these sites,
    // so drawing them inside the line is faithful, not a fudge.
    const insetPx = 4;

    const px1 = toX(b.xMin) + insetPx;
    const py1 = toY(b.yMax) + insetPx;   // toY flips: yMax → top of the canvas
    const w   = (toX(b.xMax) - toX(b.xMin)) - 2 * insetPx;
    const h   = (toY(b.yMin) - toY(b.yMax)) - 2 * insetPx;

    ctx.save();
    ctx.strokeStyle = COLORS.canvasOutline;
    ctx.lineWidth   = 2;
    ctx.setLineDash([6, 4]);
    ctx.strokeRect(px1, py1, w, h);
    ctx.restore();
  }

  function drawLayout(canvas, dots, pattern) {
    const ctx = canvas.getContext("2d");
    const W = canvas.width, H = canvas.height;

    ctx.fillStyle = COLORS.background;
    ctx.fillRect(0, 0, W, H);

    if (!dots || !dots.length) {
      ctx.fillStyle = "#c62828";
      ctx.font = "14px sans-serif";
      ctx.textAlign = "center";
      ctx.fillText("No SiDBs in layout", W / 2, H / 2);
      return;
    }

    let minX = Infinity, maxX = -Infinity, minY = Infinity, maxY = -Infinity;
    for (const d of dots) {
      if (d.x_nm < minX) minX = d.x_nm;
      if (d.x_nm > maxX) maxX = d.x_nm;
      if (d.y_nm < minY) minY = d.y_nm;
      if (d.y_nm > maxY) maxY = d.y_nm;
    }
    const bbox = { minX, maxX, minY, maxY };
    const { toX, toY } = drawGrid(ctx, W, H, bbox);

    // grid dots (very faint)
    ctx.fillStyle = COLORS.grid;
    for (let x = Math.ceil(minX / 0.384); x <= Math.floor(maxX / 0.384); x++) {
      for (let y = Math.ceil(minY / 0.384); y <= Math.floor(maxY / 0.384); y++) {
        const px = toX(x * 0.384), py = toY(y * 0.384);
        ctx.beginPath();
        ctx.arc(px, py, 1.5, 0, 2 * Math.PI);
        ctx.fill();
      }
    }

    // SiDBs
    const charges = pattern ? pattern.charge_states : null;
    for (let i = 0; i < dots.length; i++) {
      const d = dots[i];
      const charge = charges ? charges[i] : 127;

      let fill = null;
      if (charge === -1) fill = COLORS.negative;
      else if (charge === 0) fill = COLORS.neutral;
      else if (charge === 1) fill = COLORS.positive;

      let outline = COLORS.emptyOutline;
      if (d.type === "input")  outline = COLORS.inputOutline;
      else if (d.type === "output") outline = COLORS.outputOutline;
      else if (d.type === "logic")  outline = COLORS.logicOutline;

      const px = toX(d.x_nm), py = toY(d.y_nm);

      if (fill === null) {
        ctx.strokeStyle = COLORS.emptyOutline;
        ctx.lineWidth = 1;
        ctx.beginPath();
        ctx.arc(px, py, 3, 0, 2 * Math.PI);
        ctx.stroke();
      } else {
        ctx.fillStyle = fill;
        ctx.strokeStyle = outline;
        ctx.lineWidth = 2;
        ctx.beginPath();
        ctx.arc(px, py, 6, 0, 2 * Math.PI);
        ctx.fill();
        ctx.stroke();
      }
    }

    // workable-area outline — drawn after the SiDBs so it sits on top
    drawCanvasOutline(ctx, toX, toY);

    // legend
    ctx.font = "11px sans-serif";
    ctx.textAlign = "left";
    let lx = 10;
    const ly = H - 12;
    const legend = [
      ["negative", COLORS.negative],
      ["neutral",  COLORS.neutral],
      ["positive", COLORS.positive]
    ];
    for (const [label, color] of legend) {
      ctx.fillStyle = color;
      ctx.beginPath();
      ctx.arc(lx + 4, ly - 3, 4, 0, 2 * Math.PI);
      ctx.fill();
      ctx.fillStyle = "#555";
      ctx.fillText(label, lx + 12, ly);
      lx += 12 + ctx.measureText(label).width + 16;
    }
  }

  function updateStatus() {
    const statusEl = document.getElementById("truth-table-status");
    const valueEl  = document.getElementById("input-value");
    if (!patternData || !patternData.patterns.length) {
      statusEl.textContent = "Pattern data not loaded.";
      statusEl.className   = "";
      valueEl.textContent  = "—";
      return;
    }

    const n = patternData.num_inputs;
    const p = patternData.patterns[currentPattern];
    if (!p) return;

    const bits = currentPattern.toString(2).padStart(n, "0");
    valueEl.textContent = `${bits}  (${currentPattern + 1}/${patternData.patterns.length})`;

    const exp = p.expected_output.join("");
    if (p.operational) {
      statusEl.textContent = `Expected output: [${exp}] — source point is OPERATIONAL ✓`;
      statusEl.className = "ok";
    } else {
      statusEl.textContent = `Expected output: [${exp}] — source point is NON-OPERATIONAL ✗`;
      statusEl.className = "fail";
    }
  }

  function render() {
    if (!patternData) return;
    const canvas = document.getElementById("layout-canvas");
    const pattern = patternData.patterns[currentPattern] || null;
    drawLayout(canvas, patternData.dots, pattern);
    updateStatus();
  }

  function setupSlider() {
    const slider = document.getElementById("input-slider");
    if (!patternData || !patternData.patterns.length) return;

    slider.min   = "0";
    slider.max   = (patternData.patterns.length - 1).toString();
    slider.step  = "1";
    slider.value = Math.min(currentPattern, patternData.patterns.length - 1);

    slider.oninput = () => {
      currentPattern = parseInt(slider.value, 10);
      render();
    };
  }

  function setPatternData(data) {
    patternData = data;
    currentPattern = 0;
    setupSlider();
    render();
  }

  function setPatternError(msg) {
    patternData = null;
    const canvas = document.getElementById("layout-canvas");
    const ctx = canvas.getContext("2d");
    ctx.fillStyle = "#fdfdfd";
    ctx.fillRect(0, 0, canvas.width, canvas.height);
    ctx.fillStyle = "#c62828";
    ctx.font = "14px sans-serif";
    ctx.textAlign = "center";
    ctx.fillText("Could not load pattern data", canvas.width / 2, canvas.height / 2 - 10);
    ctx.fillStyle = "#666";
    ctx.font = "11px ui-monospace, monospace";
    ctx.fillText((msg || "").substring(0, 80), canvas.width / 2, canvas.height / 2 + 14);
  }

  return { setPatternData, setPatternError };
})();

window.Explorer = Explorer;