// navigation.js
//
// Hash-based routing. State schema:
//   { exp: string | null, gate: number | null }
//
// Usage:
//   Navigation.init();                 // read current URL into state
//   Navigation.onChange(applyRoute);   // subscribe
//   Navigation.push({ exp: "and", gate: 3 });
//   Navigation.replace({ exp: "or", gate: null });
//   Navigation.getState();             // { exp, gate } snapshot
//
// `push` adds a history entry; `replace` overwrites the current one.
// Both notify listeners synchronously. Browser back/forward (popstate)
// and manual URL edits (hashchange) also notify listeners.
const Navigation = (() => {
  let currentState = { exp: null, gate: null };
  const listeners = [];

  function parseHash() {
    const raw = window.location.hash.startsWith("#")
      ? window.location.hash.slice(1)
      : window.location.hash;
    const params = new URLSearchParams(raw);

    const exp  = params.get("exp")  || null;
    const gateRaw = params.get("gate");
    const gateNum = gateRaw !== null ? Number(gateRaw) : NaN;

    return {
      exp,
      gate: Number.isFinite(gateNum) ? gateNum : null
    };
  }

  function buildHash(state) {
    const params = new URLSearchParams();
    if (state.exp) params.set("exp", state.exp);
    if (state.gate !== null && state.gate !== undefined) {
      params.set("gate", String(state.gate));
    }
    const s = params.toString();
    return s ? `#${s}` : "";
  }

  function sameState(a, b) {
    return (a.exp  || null) === (b.exp  || null) &&
           (a.gate ?? null) === (b.gate ?? null);
  }

  function notify(state) {
    currentState = { exp: state.exp || null, gate: state.gate ?? null };
    for (const cb of listeners) {
      try { cb({ ...currentState }); }
      catch (e) { console.error("Navigation listener error:", e); }
    }
  }

  function init() {
    currentState = parseHash();
  }

  function onChange(cb) {
    if (typeof cb === "function") listeners.push(cb);
  }

  function getState() {
    return { ...currentState };
  }

  function push(state) {
    const hash = buildHash(state);
    const nextUrl = hash || window.location.pathname + window.location.search;
    if (window.location.hash !== hash) {
      window.history.pushState(state, "", nextUrl);
    }
    notify(state);
  }

  function replace(state) {
    const hash = buildHash(state);
    const nextUrl = hash || window.location.pathname + window.location.search;
    if (window.location.hash !== hash) {
      window.history.replaceState(state, "", nextUrl);
    }
    notify(state);
  }

  window.addEventListener("popstate", () => {
    const s = parseHash();
    if (!sameState(s, currentState)) notify(s);
  });

  window.addEventListener("hashchange", () => {
    const s = parseHash();
    if (!sameState(s, currentState)) notify(s);
  });

  return { init, onChange, getState, push, replace };
})();

window.Navigation = Navigation;