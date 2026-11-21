// table.js
const Table = (() => {
  let allData             = [];
  let currentData         = [];
  let availableGenerators = [];
  let selectedGenerators  = new Set();
  let sortKey     = "_score";
  let sortAsc     = false;
  let onRowClick  = null;
  let currentPage = 1;
  let pageSize    = 50;

  const COLUMNS = [
    { key: "generator",       label: "Generator" },
    { key: "num_canvas_dbs",  label: "#DBs" },
    { key: "critical_temp_k", label: "Tc (K)" },
    { key: "domain_distance", label: "Domain Dist" },
    {
      key: "_score",
      label: "Score",
      title: "Tc/400 + DD/2 - #DBs/1000  — higher is better; the #DBs term is a tiny tie-breaker",
      format: "fixed3"
    },
    { key: "num_op_samples",  label: "Op. Samples" },
    { key: "runtime_sec",     label: "Runtime (s)" }
  ];

  function setData(data) {
    const source = Array.isArray(data) ? data : (data.gates || []);

    allData = source.map(r => ({
      ...r,
      _score: (Number(r.critical_temp_k) || 0) / 400
            + (Number(r.domain_distance) || 0) / 2.0
            - (Number(r.num_canvas_dbs) || 0) / 1000
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
    currentPage = 1;

    renderFilterBar();
    render();
  }

  function setRowClickHandler(fn) {
    onRowClick = fn;
  }

  function setPageSize(size) {
    const parsed = parseInt(size, 10);
    if (!Number.isNaN(parsed) && parsed > 0) {
      pageSize = parsed;
      currentPage = 1;
      render();
    }
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
        currentPage = 1;
        renderFilterBar();
        render();
      });
    });

    bar.querySelector(".filter-toggle").addEventListener("click", () => {
      if (allChecked) selectedGenerators = new Set();
      else            selectedGenerators = new Set(availableGenerators);
      currentPage = 1;
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

    const totalItems = currentData.length;
    const totalPages = Math.max(1, Math.ceil(totalItems / pageSize));
    if (currentPage > totalPages) currentPage = totalPages;
    if (currentPage < 1) currentPage = 1;

    const startIdx = (currentPage - 1) * pageSize;
    const endIdx   = Math.min(startIdx + pageSize, totalItems);
    const pageRows = currentData.slice(startIdx, endIdx);

    container.innerHTML = `
      <div class="table-pagination">
        <button type="button" class="page-prev" ${currentPage <= 1 ? "disabled" : ""}>Prev</button>
        <span class="page-info">Page ${currentPage} of ${totalPages} (${startIdx + 1}–${endIdx} of ${totalItems})</span>
        <button type="button" class="page-next" ${currentPage >= totalPages ? "disabled" : ""}>Next</button>
        <label class="page-size-label">
          Rows:
          <select class="page-size-select">
            ${[25, 50, 100, 250].map(n => `
              <option value="${n}" ${n === pageSize ? "selected" : ""}>${n}</option>
            `).join("")}
          </select>
        </label>
      </div>
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
          ${pageRows.map((row, i) => {
            const dataIdx = startIdx + i;
            return `
              <tr data-index="${dataIdx}">
                ${COLUMNS.map(c => `<td>${formatCell(row[c.key], c)}</td>`).join("")}
              </tr>
            `;
          }).join("")}
        </tbody>
      </table>
    `;

    const prevBtn = container.querySelector(".page-prev");
    const nextBtn = container.querySelector(".page-next");
    const sizeSel = container.querySelector(".page-size-select");

    if (prevBtn) {
      prevBtn.addEventListener("click", () => {
        if (currentPage > 1) {
          currentPage -= 1;
          render();
        }
      });
    }

    if (nextBtn) {
      nextBtn.addEventListener("click", () => {
        if (currentPage < totalPages) {
          currentPage += 1;
          render();
        }
      });
    }

    if (sizeSel) {
      sizeSel.addEventListener("change", () => {
        setPageSize(sizeSel.value);
      });
    }

    container.querySelectorAll("th").forEach(th => {
      th.addEventListener("click", () => {
        const key = th.dataset.key;
        if (key === sortKey) sortAsc = !sortAsc;
        else { sortKey = key; sortAsc = false; }
        currentPage = 1;
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

  return { setData, setRowClickHandler, setPageSize };
})();

window.Table = Table;