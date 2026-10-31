#include "fk_blend_paths.h"
#include "fk_cancel.h"
#include "fk_curve_algo.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <set>
#include <stdexcept>
namespace ForgeCad::Kernel {
std::vector<BlendPath> blendPaths(const Body &body,
                                  const std::vector<EdgeId> &selected) {
  struct End {
    int edge, side;
  };
  std::vector<EdgeId> edges;
  std::set<int> unique;
  for (EdgeId e : selected) {
    if (!body.contains(e) || body.isLaminar(e))
      throw std::domain_error(
          "raggio variabile: seleziona bordi tra due facce cucite");
    if (!unique.insert(e.index).second)
      throw std::domain_error("raggio variabile: bordo selezionato due volte");
    edges.push_back(e);
  }
  std::map<int, std::vector<End>> vertices;
  std::vector<std::array<int, 2>> next(edges.size(), {-1, -1});
  const auto away = [&](End end) {
    const auto &e = body.edge(edges[std::size_t(end.edge)]);
    return normalized(e.curve->derivative(end.side ? e.range.hi : e.range.lo)) *
           (end.side ? -1.0 : 1.0);
  };
  for (int i = 0; i < int(edges.size()); ++i) {
    vertices[body.edgeStart(edges[std::size_t(i)]).index].push_back({i, 0});
    vertices[body.edgeEnd(edges[std::size_t(i)]).index].push_back({i, 1});
  }
  for (const auto &entry : vertices)
    for (const End a : entry.second) {
      cancellationPoint();
      int candidate = -1, count = 0;
      for (const End b : entry.second)
        if (a.edge != b.edge && dot(away(a), away(b)) < -1.0 + 1e-7) {
          candidate = 2 * b.edge + b.side;
          ++count;
        }
      if (count == 1)
        next[std::size_t(a.edge)][std::size_t(a.side)] = candidate;
    }
  for (int i = 0; i < int(edges.size()); ++i)
    for (int side = 0; side < 2; ++side) {
      const int other = next[std::size_t(i)][std::size_t(side)];
      if (other >= 0 &&
          next[std::size_t(other / 2)][std::size_t(other % 2)] != 2 * i + side)
        next[std::size_t(i)][std::size_t(side)] = -1;
    }
  std::vector<bool> used(edges.size(), false);
  std::vector<BlendPath> paths;
  for (int seed = 0; seed < int(edges.size()); ++seed) {
    if (used[std::size_t(seed)])
      continue;
    // Cerca l'estremo aperto percorrendo in avanti la componente.
    int start = 2 * seed, cursor = start;
    std::set<int> walked;
    while (walked.insert(cursor / 2).second) {
      const int at = next[std::size_t(cursor / 2)][std::size_t(cursor % 2)];
      if (at < 0) {
        start = cursor;
        break;
      }
      cursor = at ^ 1;
    }
    BlendPath path;
    cursor = start;
    while (!used[std::size_t(cursor / 2)]) {
      cancellationPoint();
      const int i = cursor / 2, side = cursor % 2;
      const auto &e = body.edge(edges[std::size_t(i)]);
      const double length = arcLength(*e.curve, e.range);
      if (!(length > 1e-12) || !std::isfinite(length))
        throw std::domain_error("raggio variabile: bordo degenere");
      path.edges.push_back(
          {edges[std::size_t(i)], side != 0, path.length, length});
      path.length += length;
      used[std::size_t(i)] = true;
      const int following = next[std::size_t(i)][std::size_t(1 - side)];
      if (following < 0)
        break;
      if (following == start) {
        path.closed = true;
        break;
      }
      cursor = following;
    }
    if (path.edges.size() == 1 &&
        body.edgeStart(path.edges[0].edge) == body.edgeEnd(path.edges[0].edge))
      path.closed = true;
    paths.push_back(std::move(path));
  }
  return paths;
}
BlendPathPoint blendPathPoint(const Body &body, const BlendPath &path,
                              double fraction) {
  if (path.edges.empty() || !(path.length > 0))
    throw std::domain_error("percorso del raccordo vuoto");
  const double distance = std::clamp(fraction, 0.0, 1.0) * path.length;
  const BlendPathEdge *piece = &path.edges.back();
  for (const auto &e : path.edges)
    if (distance <= e.offset + e.length) {
      piece = &e;
      break;
    }
  const auto &e = body.edge(piece->edge);
  double target = std::clamp(distance - piece->offset, 0.0, piece->length);
  if (piece->reversed)
    target = piece->length - target;
  double lo = e.range.lo, hi = e.range.hi;
  for (int i = 0; i < 48; ++i) {
    cancellationPoint();
    const double mid = .5 * (lo + hi);
    if (arcLength(*e.curve, {e.range.lo, mid}) < target)
      lo = mid;
    else
      hi = mid;
  }
  const double t = .5 * (lo + hi);
  return {piece->edge, t, e.curve->point(t),
          normalized(e.curve->derivative(t)) * (piece->reversed ? -1.0 : 1.0)};
}
} // namespace ForgeCad::Kernel
