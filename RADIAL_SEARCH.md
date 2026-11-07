# Radial operational-domain search

This document describes the radial search implemented by
`operational_domain_radial_search` in
`fiction/technology/sidb/simulation/logic/operational_domain.hpp`.

The algorithm is a parallel, n-dimensional sampling heuristic for exploring the
operational status around a user-supplied seed parameter point. It expands along
coordinate axes to estimate a useful outer radius, then contracts a sequence of
axis-aligned hypercubes until it finds a shell whose sampled points all have the
same operational status as the seed.

It is **not** an exhaustive operational-domain computation. In particular, it
does not fill or infer the interior of the accepted shell.

## API

```cpp
operational_domain_radial_search(
    layout,
    specification,
    seed_parameter_point,
    operational_domain_params,
    statistics
);
```

The public entry point is declared in
`fiction/technology/sidb/simulation/logic/operational_domain.hpp`:

```cpp
[[nodiscard]] operational_domain
operational_domain_radial_search(
    const layout& lyt,
    const std::vector<kitty::dynamic_truth_table>& spec,
    const parameter_point& start_point,
    const operational_domain_params& params = {},
    operational_domain_stats* stats = nullptr);
```

`spec` contains the expected Boolean functions, one truth table per output.
The layout and simulation requirements are the same as for the other
operational-domain algorithms. In particular, the implementation expects the
layout's output BDL pair and the input ordering to agree with the specification.

### Example

```cpp
using namespace fiction::sidb::simulation::logic;

operational_domain_params params{};
params.sweep_dimensions = {
    {.dimension = sweep_parameter::EPSILON_R,
     .min = 1.0, .max = 10.0, .step = 0.1},
    {.dimension = sweep_parameter::LAMBDA_TF,
     .min = 1.0, .max = 10.0, .step = 0.1},
    {.dimension = sweep_parameter::MU_MINUS,
     .min = -0.5, .max = 0.5, .step = 0.01},
};
params.number_of_threads = 8;

const parameter_point seed{std::vector<double>{5.0, 5.0, 0.0}};
operational_domain_stats stats{};

const auto domain = operational_domain_radial_search(
    lyt, specification, seed, params, &stats);
```

The number of sweep dimensions is not fixed at two. The radial search supports
any positive number of dimensions, subject to available memory and representable
shell sizes.

## Sweep-grid coordinates

The search operates on integer indices in the generated sweep grid, not directly
on physical parameter values. For dimension `d`, let the generated coordinate
values be

```text
values[d][0], values[d][1], ..., values[d][N_d - 1]
```

A seed coordinate is mapped independently to the first generated value greater
than or equal to it (`lower_bound`). Thus:

- A seed coordinate may lie between grid values; it is rounded upward to the
  next grid point.
- A seed at either generated endpoint is valid.
- A non-finite coordinate or a coordinate outside the generated grid is
  rejected.
- The configured `max` is a grid endpoint only when it is reached by the
  generated step sequence. If it is not aligned with `step`, the last generated
  coordinate can be below `max`.

The step sizes may differ between dimensions. Radius is consequently measured
in **grid cells**, not in physical units, and one cell in one dimension is not
assumed to represent the same physical distance as one cell in another.

## Algorithm

Let the seed's grid-index vector be

\[
\mathbf{s} = (s_0, s_1, \ldots, s_{n-1}),
\]

and let dimension `d` contain `N_d` grid points. Let `S` be the operational
status evaluated at the seed. The algorithm searches for nearby points whose
status differs from `S`, then tests progressively smaller shells for agreement
with `S`.

### 1. Evaluate the seed

The seed is converted to a grid-index point and evaluated first. Its status is
the reference status for the rest of the search. The algorithm works symmetrically
whether the seed is operational or non-operational: a matching shell means that
all of its evaluated points have the same status as the seed, not necessarily
that they are operational.

### 2. Expand along the coordinate axes

There are `2 * n` axis-aligned rays: the positive and negative direction of
each dimension. Each ray starts one grid cell away from the seed and advances
until one of the following happens:

1. It finds a point whose status differs from the seed's status.
2. It reaches the edge of the generated grid without finding a transition.
3. Another ray has already found a smaller transition distance, so this ray
   cannot improve the current minimum.

The smallest actual transition distance found by any ray becomes the initial
contraction radius. A ray that reaches a grid edge without finding a transition
does **not** constrain the radius.

If no ray finds a transition, the initial radius is the maximum Chebyshev
distance needed for a seed-centered hypercube to cover the complete grid:

\[
R_{\max} = \max_d\left(\max(s_d,\; N_d - 1 - s_d)\right).
\]

This fallback is important for off-center seeds. At `R_max`, the clipped
hypercube is the entire sweep grid, so its shell is the complete grid border.

### 3. Define a clipped hypercube and its shell

For an integer radius `r`, define the lower and upper grid-index bounds in each
dimension as

\[
L_d(r) = \max(0,\; s_d-r),
\qquad
U_d(r) = \min(N_d-1,\; s_d+r).
\]

The clipped hypercube is

\[
B_r = \prod_{d=0}^{n-1} [L_d(r), U_d(r)]\cap\mathbb{Z}^n.
\]

Its shell is the boundary of that clipped hypercube:

\[
\partial B_r = \{\mathbf{p}\in B_r :
  \exists d,\ p_d=L_d(r)\ \text{or}\ p_d=U_d(r)\}.
\]

Clipping is performed independently in every dimension. Therefore, a face
that reaches a grid edge becomes part of the clipped hypercube boundary even
when that edge is closer to the seed than `r`. At `R_max`, `B_r` is the entire
grid and `\partial B_r` is its complete border.

The implementation partitions each shell into non-overlapping face ranges.
Every boundary point is assigned to the first dimension in which it touches a
face. This avoids evaluating corners and edges more than once. Each face is
represented by inclusive lower and upper coordinate bounds and a flattened
ticket interval; individual points are reconstructed from their ticket indices
when workers evaluate them.

### 4. Contract through the shells

Shell radii are prepared in descending order:

```text
initial radius, initial radius - 1, ..., 1, 0
```

For each shell, workers evaluate points and compare their status with the seed's
status:

- **Mismatch:** one point is enough to reject the shell. The next, smaller shell
  becomes active immediately; other workers do not need to finish evaluations
  they already claimed from the rejected shell.
- **Complete match:** the shell is accepted only after every unique point in it
  has completed evaluation with the seed's status. The contraction then stops.
- **Radius zero:** the shell contains the seed itself, so it provides the final
  fallback if every larger shell is rejected.

The first accepted shell in this outside-to-inside sequence is the stopping
shell. The algorithm does not require the points inside that shell to have the
same status, because those interior points are not exhaustively examined.

## Parallel execution and scheduling

The implementation uses C++20 concurrency facilities, including `std::jthread`,
`std::barrier`, and atomic wait/notify operations.

### Expansion

A shared atomic ticket counter distributes the `2 * n` ray directions among
workers. Each worker reuses a local grid-index point while advancing along its
ray. The best transition radius is maintained with an atomic minimum. Once a
smaller transition has been found, rays that cannot improve it can stop early.

### Shell preparation

The shell work records are allocated before evaluation. Their face descriptors
are prepared in parallel using the same shared task counter after expansion has
finished. Each shell has its own scheduling and completion state.

### Shell evaluation

Each shell has three atomic fields:

| Field | Meaning |
| --- | --- |
| `next_ticket` | Next point index to be claimed by a worker. |
| `completed_points` | Number of points that completed with the seed's status. |
| `status` | `pending`, `failed`, or `passed`. |

A shared `active_shell` identifies the shell currently being processed. Workers
claim point tickets dynamically with `fetch_add`, so work is not assigned in
fixed per-thread ranges. This helps keep the workers busy when individual
operational checks take different amounts of time.

When a worker discovers a mismatch, it atomically changes that shell from
`pending` to `failed` and advances `active_shell` to the next inner shell. A
worker that still holds an old ticket checks the active shell and shell status
before beginning the expensive operational check; already-running checks may
finish, but they do not delay work on the next shell.

A shell changes from `pending` to `passed` only when its completion count reaches
its total unique point count. If all tickets have been issued but some checks are
still running, workers wait on the shell's atomic status. The worker that
completes the final matching point publishes `passed` and wakes the others.

There is no barrier between successive contraction shells. Barriers are used
only at phase boundaries, where they publish initialized shared state and
ensure workers transition between phases safely.

The requested thread count is bounded by the number of expansion rays, with at
least one worker. If creation of an additional operating-system thread fails,
the missing barrier participants are dropped and the successfully created
workers continue.

## Results and interpretation

The returned `operational_domain` contains the parameter points whose statuses
were actually evaluated during this search. This includes the seed, the ray
samples performed during expansion, and shell points whose operational checks
were started during contraction.

It does **not** imply that every point inside the accepted shell has been
checked. In particular:

- The interior of the accepted shell is not filled or inferred.
- A rejected shell may be only partially evaluated because one mismatch is
  sufficient to reject it.
- A few checks already in flight on a rejected shell may finish after the next
  shell has become active.
- Off-axis status changes that do not intersect an evaluated ray or shell may
  remain undiscovered.

Consequently, this algorithm is useful for quickly sampling a local region
around a seed, but it does not provide the completeness guarantees of grid
search. It also does not provide the connected-component exploration performed
by flood fill, nor the contour inference performed by contour tracing.

Operational-status checks are cached by the operational-domain implementation,
so a point sampled in more than one phase is not needlessly simulated again.
The underlying check can require up to `2^k` ground-state evaluations, where
`k` is the number of BDL inputs, and can terminate early when non-operational
behavior is detected.

## Parameters, validation, and errors

The radial search uses the common `operational_domain_params` settings:

- `sweep_dimensions`: one or more finite, ordered sweep ranges with positive,
  finite step sizes.
- `number_of_threads`: requested worker count. Values below one are treated as
  one by the radial worker setup; the effective count is also capped by the
  number of expansion rays.
- `operational_params`: simulation engine, operational analysis strategy,
  deadline, and other settings used by each operational-status check.
- `on_progress`: progress callback used as parameter points are evaluated.

Generated coordinates within a dimension must occupy distinct parameter-point
identity cells; colliding coordinates are rejected. The total grid size and
shell face counts must fit their respective `std::size_t` storage/index ranges.

The public function can throw:

- `std::invalid_argument` for invalid sweep settings, an invalid seed dimension
  count, a non-finite seed coordinate, or a seed outside the generated grid.
  The common operational-domain validation errors also apply (for example,
  unsupported simulation-engine/layout combinations or unmet filter-only
  analysis preconditions).
- `std::length_error` when the number of dimensions, grid, or a shell cannot be
  represented safely by the implementation's index types.
- `std::bad_alloc` if worker or shell storage cannot be allocated.
- `std::logic_error` if a shell ticket is inconsistent with its prepared face
  descriptors; this indicates an internal scheduling/partitioning invariant
  failure.
- The exception raised by a deadline check or an operational-status
  evaluation. Worker exceptions are captured, all workers are allowed to stop,
  and the exception is rethrown on the calling thread.

The implementation is intended to be built as C++20 code.
