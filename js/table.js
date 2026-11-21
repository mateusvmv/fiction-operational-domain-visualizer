// table.js
const Table = (() => {
  let allData             = [];
  let currentData         = [];
  let availableGenerators = [];
  let selectedGenerators  = new Set();
  let sortKey     = "_score";
  let sortAsc     = false;
  let onRowClick  = null;

  const COLUMNS = [
    { key: "generator",       label: "Generator" },
    { key: "num_canvas_dbs",  label: "#DBs" },
    { key: "critical_temp_k", label: "Tc (K)" },
    { key: "domain_distance", label: "Domain Dist" },
    {
      key: "_score",
      label: "Score",
      title: "Tc/400 + DD/max(DD) + (0.005 − #DBs/1000)  — higher is better; the #DBs term is a tiny tie-breaker",
      format: "fixed3"
    },
    { key: "num_op_samples",  label: "Op. Samples" },
    { key: "runtime_sec",     label: "Runtime (s)" }
  ];

  function setData(data) {
    const source = Array.isArray(data) ? data : (data.gates || []);

    // Normalisation must be filter-independent, so compute over the whole set.
    const maxDD = source.reduce(
      (m, r) => Math.max(m, Number(r.domain_distance) || 0), 0
    ) || 1;

    allData = source.map(r => ({
      ...r,
      _score: (Number(r.critical_temp_k) || 0) / 400
            + (Number(r.domain_distance) || 0) / maxDD
            + (0.005 - (Number(r.num_canvas_dbs) || 0) / 1000)
    }));

    const next = [...new Set(allData.map(r => r.generator))].sort();

    const prevAllSelected =
      selectedGenerators.size === 0 ||
      availableGenerators.length === 0 ||
      availableGenerators.every(g => selectedGenerators.has(g));

    if (prevAllSelected) {
      selectedGenerators = new Set(next);
    } else {
      selectedGenerators = new Set(next.filter(g => selectedGenerators.has(g)));
      if (selectedGenerators.size === 0) {
        selectedGenerators = new Set(next);
      }
    }

    availableGenerators = next;

    renderFilterBar();
    render();
  }

  function setRowClickHandler(fn) {
    onRowClick = fn;
  }

  function renderFilterBar() {
    const bar = document.getElementById("table-filter-bar");
    if (!bar) return;

    if (availableGenerators.length <= 1) {
      bar.innerHTML = "";
      return;
    }

    const allChecked = selectedGenerators.size === availableGenerators.length;

    bar.innerHTML = `
      <span class="filter-label">Generators:</span>
      ${availableGenerators.map(g => `
        <label class="filter-chip">
          <input type="checkbox" data-generator="${g}"
                 ${selectedGenerators.has(g) ? "checked" : ""}>
          <span>${g}</span>
        </label>
      `).join("")}
      <button type="button" class="filter-toggle">
        ${allChecked ? "Deselect all" : "Select all"}
      </button>
    `;

    bar.querySelectorAll("input[type=checkbox]").forEach(cb => {
      cb.addEventListener("change", () => {
        const g = cb.dataset.generator;
        if (cb.checked) selectedGenerators.add(g);
        else            selectedGenerators.delete(g);
        renderFilterBar();
        render();
      });
    });

    bar.querySelector(".filter-toggle").addEventListener("click", () => {
      if (allChecked) selectedGenerators = new Set();
      else            selectedGenerators = new Set(availableGenerators);
      renderFilterBar();
      render();
    });
  }

  function sortData(arr) {
    arr.sort((a, b) => {
      const av = a[sortKey];
      const bv = b[sortKey];
      if (typeof av === "string" || typeof bv === "string") {
        const as = av == null ? "" : String(av);
        const bs = bv == null ? "" : String(bv);
        return sortAsc ? as.localeCompare(bs) : bs.localeCompare(as);
      }
      const an = Number(av) || 0;
      const bn = Number(bv) || 0;
      return sortAsc ? an - bn : bn - an;
    });
  }

  function render() {
    const container = document.getElementById("table-container");

    if (!allData.length) {
      container.innerHTML = "<p>No results loaded.</p>";
      return;
    }

    currentData = allData.filter(r => selectedGenerators.has(r.generator));
    if (!currentData.length) {
      container.innerHTML =
        "<p>All generators are filtered out. Use the checkboxes above to re-enable some.</p>";
      return;
    }

    sortData(currentData);

    container.innerHTML = `
      <table>
        <thead>
          <tr>${COLUMNS.map(c => {
            const cls   = c.key === sortKey
              ? (sortAsc ? "sorted-asc" : "sorted-desc")
              : "";
            const title = c.title ? ` title="${c.title}"` : "";
            return `<th data-key="${c.key}" class="${cls}"${title}>${c.label}</th>`;
          }).join("")}</tr>
        </thead>
        <tbody>
          ${currentData.map((row, idx) => `
            <tr data-index="${idx}">
              ${COLUMNS.map(c => `<td>${formatCell(row[c.key], c)}</td>`).join("")}
            </tr>
          `).join("")}
        </tbody>
      </table>
    `;

    container.querySelectorAll("th").forEach(th => {
      th.addEventListener("click", () => {
        const key = th.dataset.key;
        if (key === sortKey) sortAsc = !sortAsc;
        else { sortKey = key; sortAsc = false; }
        render();
      });
    });

    container.querySelectorAll("tbody tr").forEach(tr => {
      tr.addEventListener("click", () => {
        const idx = parseInt(tr.dataset.index, 10);
        if (onRowClick) onRowClick(currentData[idx]);
      });
    });
  }

  function formatCell(v, col) {
    if (v === null || v === undefined) return "";
    if (col && col.format === "fixed3") return Number(v).toFixed(3);
    if (typeof v === "number" && !Number.isInteger(v)) return v.toFixed(3);
    return v;
  }

  return { setData, setRowClickHandler };
})();

window.Table = Table;