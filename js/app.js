// app.js
(async function () {
  const missing = [];
  try { if (typeof Table    === "undefined") missing.push("Table");    } catch { missing.push("Table"); }
  try { if (typeof Scatter  === "undefined") missing.push("Scatter");  } catch { missing.push("Scatter"); }
  try { if (typeof Explorer === "undefined") missing.push("Explorer"); } catch { missing.push("Explorer"); }
  if (missing.length) {
    document.getElementById("table-container").innerHTML =
      `<p style="color:#c00">Missing module(s): ${missing.join(", ")}.</p>`;
    return;
  }

  const DATA_ROOT = "data";
  const STORAGE_KEY = "sidb_exp";

  // ---------------------------------------------------------------------------
  // Experiment manifest
  // ---------------------------------------------------------------------------
  // Primary source: data/experiments.json with shape { experiments: [...] }
  // Fallback:      treat data/index.json as a single experiment with path=""
  async function loadManifest() {
    try {
      const res = await fetch(`${DATA_ROOT}/experiments.json`, { cache: "no-store" });
      if (res.ok) {
        const json = await res.json();
        if (Array.isArray(json.experiments) && json.experiments.length > 0) {
          return json.experiments.map(e => ({
            id:          String(e.id || e.path),
            label:       String(e.label || e.id || e.path),
            path:        String(e.path || "").replace(/^\/+|\/+$/g, ""),
            truth_table: String(e.truth_table || ""),
            description: String(e.description || "")
          }));
        }
      }
    } catch (e) {
      console.warn("experiments.json not found; falling back to single experiment.", e);
    }
    return [{
      id:          "default",
      label:       "Default",
      path:        "",
      truth_table: "",
      description: "Single experiment (data/index.json)"
    }];
  }

  function expDataRoot(exp) {
    return exp && exp.path ? `${DATA_ROOT}/${exp.path}` : DATA_ROOT;
  }
  function joinExpPath(exp, rel) {
    return rel ? `${expDataRoot(exp)}/${rel}` : expDataRoot(exp);
  }

  // ---------------------------------------------------------------------------
  // State
  // ---------------------------------------------------------------------------
  let experiments  = [];
  let currentExp   = null;
  let sourcePoint  = { epsilon_r: 5.0, lambda_tf: 5.0 };
  let sweep        = { epsilon_r: { min: 1, max: 10 }, lambda_tf: { min: 1, max: 10 } };

  // ---------------------------------------------------------------------------
  // DOM refs
  // ---------------------------------------------------------------------------
  const experimentSelect = document.getElementById("experiment-select");
  const experimentInfo   = document.getElementById("experiment-info");
  const tableSection     = document.getElementById("table-view");
  const detailSection    = document.getElementById("detail-view");
  const backButton       = document.getElementById("back-button");
  const scatterCanvas    = document.getElementById("scatter-canvas");
  const statsPanel       = document.getElementById("stats-panel");
  const downloadSqdBtn   = document.getElementById("download-sqd-btn");

  // ---------------------------------------------------------------------------
  // Boot
  // ---------------------------------------------------------------------------
  experiments = await loadManifest();

  experimentSelect.innerHTML = experiments
    .map(e => `<option value="${e.id}">${e.label}</option>`)
    .join("");

  // Pick initial experiment: URL hash > localStorage > first
  const hashMatch = window.location.hash.match(/(?:#|&)exp=([^&]+)/);
  const hashId    = hashMatch ? decodeURIComponent(hashMatch[1]) : null;
  const savedId   = localStorage.getItem(STORAGE_KEY);
  const initialId = [hashId, savedId].find(id => id && experiments.some(e => e.id === id))
                    || experiments[0].id;

  Table.setRowClickHandler(showDetail);

  backButton.addEventListener("click", () => {
    detailSection.classList.add("hidden");
    tableSection.classList.remove("hidden");
    backButton.blur();
    downloadSqdBtn.disabled = true;
    downloadSqdBtn.onclick  = null;
  });

  experimentSelect.addEventListener("change", () => {
    selectExperiment(experimentSelect.value);
  });

  window.addEventListener("hashchange", () => {
    const m = window.location.hash.match(/(?:#|&)exp=([^&]+)/);
    if (!m) return;
    const id = decodeURIComponent(m[1]);
    if (id !== currentExp?.id && experiments.some(e => e.id === id)) {
      selectExperiment(id);
    }
  });

  await selectExperiment(initialId);

  // ---------------------------------------------------------------------------
  // Experiment switching
  // ---------------------------------------------------------------------------
  async function selectExperiment(id) {
    const exp = experiments.find(e => e.id === id);
    if (!exp) return;

    currentExp = exp;
    localStorage.setItem(STORAGE_KEY, id);
    experimentSelect.value = id;

    // reflect selection in URL without a history entry
    const newHash = `#exp=${encodeURIComponent(id)}`;
    if (window.location.hash !== newHash) {
      history.replaceState(null, "", newHash);
    }

    // reset view state
    detailSection.classList.add("hidden");
    tableSection.classList.remove("hidden");
    downloadSqdBtn.disabled = true;
    downloadSqdBtn.onclick  = null;

    // experiment info strip
    const bits = [];
    if (exp.truth_table) bits.push(`TT: ${exp.truth_table}`);
    if (exp.description) bits.push(exp.description);
    experimentInfo.textContent = bits.join("  ·  ");

    // fetch this experiment's index
    let indexJson;
    try {
      const res = await fetch(`${joinExpPath(exp, "index.json")}`, { cache: "no-store" });
      if (!res.ok) throw new Error(`HTTP ${res.status}`);
      indexJson = await res.json();
    } catch (e) {
      console.error("Could not load experiment index.json", e);
      document.getElementById("table-container").textContent =
        `Could not load index.json for experiment "${exp.label}".`;
      return;
    }

    sourcePoint = indexJson.source_point || { epsilon_r: 5.0, lambda_tf: 5.0 };
    sweep       = indexJson.sweep       || {
      epsilon_r: { min: 1, max: 10 },
      lambda_tf: { min: 1, max: 10 }
    };

    Table.setData(indexJson);
  }

  // ---------------------------------------------------------------------------
  // Detail view
  // ---------------------------------------------------------------------------
  async function showDetail(row) {
    tableSection.classList.add("hidden");
    detailSection.classList.remove("hidden");

    downloadSqdBtn.disabled = true;
    downloadSqdBtn.onclick  = null;

    const expRoot = expDataRoot(currentExp);

    // ---- radial json ----
    let radialData = null;
    try {
      const res = await fetch(`${expRoot}/${row.radial_json}`);
      if (!res.ok) throw new Error(`HTTP ${res.status}`);
      radialData = await res.json();
    } catch (e) {
      console.error("Could not load radial json", e);
    }
    if (radialData) {
      Scatter.draw(scatterCanvas, radialData, sourcePoint, sweep);
      renderStats(row, radialData);
    } else {
      const ctx = scatterCanvas.getContext("2d");
      ctx.clearRect(0, 0, scatterCanvas.width, scatterCanvas.height);
      statsPanel.textContent = "Radial-search data unavailable.";
    }

    // ---- patterns json (drives the Explorer + slider) ----
    try {
      const res = await fetch(`${expRoot}/${row.patterns_json}`);
      if (!res.ok) throw new Error(`HTTP ${res.status}`);
      Explorer.setPatternData(await res.json());
    } catch (e) {
      console.error("Could not load patterns json", e);
      Explorer.setPatternError(e.message);
    }

    // ---- sqd text (only for the download button) ----
    let sqdText = null;
    try {
      const res = await fetch(`${expRoot}/${row.sqd_file}`);
      if (!res.ok) throw new Error(`HTTP ${res.status} for ${row.sqd_file}`);
      sqdText = await res.text();
    } catch (e) {
      console.error("Could not load SQD file", e);
    }

    if (sqdText !== null) {
      const filename = row.sqd_file.split("/").pop();
      downloadSqdBtn.disabled = false;
      downloadSqdBtn.onclick  = () => {
        const blob = new Blob([sqdText], { type: "application/xml" });
        const url  = URL.createObjectURL(blob);
        const a    = document.createElement("a");
        a.href     = url;
        a.download = filename;
        document.body.appendChild(a);
        a.click();
        document.body.removeChild(a);
        URL.revokeObjectURL(url);
      };
    }
  }

  // ---------------------------------------------------------------------------
  // Stats panel (preserved per-mille points-per-ms from your manual change)
  // ---------------------------------------------------------------------------
  function renderStats(row, radial) {
    const opPoints    = radial.points.filter(p => p.operational === 1).length;
    const nonOpPoints = radial.points.length - opPoints;
    const totalMs     = (row.runtime_sec || 0) * 1000;
    const pointsPerMs = totalMs > 0 ? radial.points.length / totalMs : 0;

    statsPanel.innerHTML = `
      <dl>
        <dt>Generator</dt>              <dd>${row.generator}</dd>
        <dt>Canvas SiDBs</dt>           <dd>${row.num_canvas_dbs}</dd>
        <dt>Critical Temp.</dt>         <dd>${row.critical_temp_k.toFixed(2)} K</dd>
        <dt>Domain Distance</dt>        <dd>${row.domain_distance.toFixed(4)}</dd>
        <dt>Operational Samples</dt>    <dd>${row.num_op_samples}</dd>
        <dt>Total Runtime</dt>          <dd>${totalMs.toFixed(1)} ms</dd>
        <dt>Points Evaluated</dt>       <dd>${radial.points.length}</dd>
        <dt>Points per Ms</dt>          <dd>${pointsPerMs.toFixed(2)} pts/ms</dd>
        <dt>Op. Points (Radial)</dt>    <dd>${opPoints}</dd>
        <dt>Non-Op. Points (Radial)</dt><dd>${nonOpPoints}</dd>
      </dl>
      <p class="plot-hint">Scroll to zoom · Drag to pan · Double-click to reset</p>
    `;
  }
})();
