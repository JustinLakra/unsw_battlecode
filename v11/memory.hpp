#pragma once

#include "helper.hpp"

#include <algorithm>
#include <array>
#include <climits>
#include <cstdint>
#include <vector>

constexpr int MEMORY_PEARL_ROUNDS =
    20; // trust a remembered pearl for this long
constexpr int MEMORY_COUNTDOWN_CAP =
    40; // ignore remembered spawns further off than this

// Everything we've seen of the map, kept across turns.
class BeliefMap {
public:
  void init(int width, int height) {
    w = width;
    h = height;
    int const n = w * h;
    seen.assign(n, 0);
    edges.assign(
        n, std::array<std::uint8_t, 4>{UNKNOWN, UNKNOWN, UNKNOWN, UNKNOWN});
    countdown.assign(n, -1);
    countdown_round.assign(n, -1);
    pearl_round.assign(n, -1);
    blocked_round.assign(n, -1);
    dist.assign(n, -1);
    first_dir.assign(n, -1);
    queue.assign(n, 0);
  }

  // Copy this turn's 7x7 view into the map.
  void update(unswbc::Controller const &ct, int round) {
    auto const dirs = unswbc::Direction::get_direction_list();
    for (auto const &tile : ct.get_tiles()) {
      auto const p = tile.get_position();
      int const i = index(p.x, p.y);

      seen[i] = 1;
      countdown[i] = tile.get_pearl_time();
      countdown_round[i] = round;
      pearl_round[i] = tile.has_pearl() ? round : -1;
      if (tile.get_dragon() != nullptr) {
        blocked_round[i] = round;
      }

      for (int d = 0; d < 4; d++) {
        auto const &e = tile.get_edge(dirs[d]);
        std::uint8_t const kind =
            !e.is_passable() ? KELP : (e.is_portal() ? PORTAL : OPEN);
        edges[i][d] = kind;

        // the same edge, seen from the neighbouring tile's side
        auto const [dx, dy] = dirs[d].get_offset();
        int const j = index(wrapi(p.x + dx, w), wrapi(p.y + dy, h));
        edges[j][(d + 2) % 4] = kind;
      }
    }
  }

  // A teammate told us about a pearl at (x, y), seen in `seen_round`.
  void note_pearl(int x, int y, int seen_round) {
    if (x < 0 || y < 0 || x >= w || y >= h) {
      return;
    }
    int const i = index(x, y);
    if (seen_round > pearl_round[i] && seen_round > countdown_round[i]) {
      pearl_round[i] = seen_round; // newer than anything we saw ourselves
      seen[i] = 1;
    }
  }

  // First step (0..3) toward the best goal outside our view: a remembered
  // pearl or spawn tile, otherwise the nearest unseen tile. -1 if none.
  int plan(unswbc::Controller const &ct, int round, bool exploring) {
    int const start = start_bfs(ct);
    int best = -1;
    int best_score = INT_MAX;
    int frontier = -1;

    int qhead = 0;
    while (qhead != qtail) {
      int const cur = queue[qhead++];
      if (cur != start) {
        if (!seen[cur]) {
          if (frontier == -1) {
            frontier = cur;
          }
        } else if (countdown_round[cur] != round) { // in view: handled locally
          int const score = goal_score(cur, round, dist[cur], exploring);
          if (score < best_score) {
            best_score = score;
            best = cur;
          }
        }
      }
      expand(cur, start, round, -1);
    }

    int const goal = best != -1 ? best : frontier;
    return goal == -1 ? -1 : first_dir[goal];
  }

  // First step (0..3) toward map position (gx, gy), which may be occupied.
  // -1 if it's unreachable or more than max_dist steps away.
  int plan_to(unswbc::Controller const &ct, int round, int gx, int gy,
              int max_dist) {
    if (gx < 0 || gy < 0 || gx >= w || gy >= h) {
      return -1;
    }
    int const start = start_bfs(ct);
    int const goal = index(gx, gy);
    if (goal == start) {
      return -1;
    }

    int qhead = 0;
    while (qhead != qtail && dist[goal] == -1) {
      int const cur = queue[qhead++];
      if (dist[cur] >= max_dist) {
        continue;
      }
      expand(cur, start, round, goal);
    }
    return dist[goal] == -1 ? -1 : first_dir[goal];
  }

private:
  static constexpr std::uint8_t UNKNOWN = 0;
  static constexpr std::uint8_t OPEN = 1;
  static constexpr std::uint8_t KELP = 2;
  static constexpr std::uint8_t PORTAL = 3;

  int w = 0;
  int h = 0;
  std::vector<std::uint8_t> seen;
  std::vector<std::array<std::uint8_t, 4>> edges; // N, E, S, W
  std::vector<int> countdown; // countdown when last seen (-1: never spawns)
  std::vector<int> countdown_round; // round it was last seen
  std::vector<int> pearl_round; // round a pearl was last seen here, -1 if none
  std::vector<int> blocked_round; // round a dragon was last seen here
  std::vector<int> dist;          // BFS scratch space, reused every turn
  std::vector<int> first_dir;
  std::vector<int> queue;
  int qtail = 0;

  int index(int x, int y) const { return y * w + x; }
  static int wrapi(int v, int size) { return (v % size + size) % size; }

  // Resets the BFS and puts our head in the queue. Returns the start cell.
  int start_bfs(unswbc::Controller const &ct) {
    auto const head = ct.get_position();
    int const start = index(head.x, head.y);
    std::fill(dist.begin(), dist.end(), -1);
    qtail = 0;
    dist[start] = 0;
    first_dir[start] = -1;
    queue[qtail++] = start;
    return start;
  }

  // Adds cur's neighbours to the BFS. `goal` may be entered even if occupied.
  void expand(int cur, int start, int round, int goal) {
    auto const dirs = unswbc::Direction::get_direction_list();
    int const cx = cur % w;
    int const cy = cur / w;
    for (int d = 0; d < 4; d++) {
      std::uint8_t const e = edges[cur][d];
      if (e == KELP || e == PORTAL) {
        continue; // unknown edges are assumed open
      }
      auto const [dx, dy] = dirs[d].get_offset();
      int const next = index(wrapi(cx + dx, w), wrapi(cy + dy, h));
      if (dist[next] != -1) {
        continue;
      }
      if (blocked_round[next] == round && next != goal) {
        continue; // a dragon is there right now
      }
      dist[next] = dist[cur] + 1;
      first_dir[next] = cur == start ? d : first_dir[cur];
      queue[qtail++] = next;
    }
  }

  // Lower is better; INT_MAX means "not a goal".
  int goal_score(int cell, int round, int distance, bool exploring) const {
    if (pearl_round[cell] != -1 &&
        round - pearl_round[cell] <= MEMORY_PEARL_ROUNDS) {
      return distance; // a pearl we (or a teammate) saw recently
    }
    if (exploring) {
      return INT_MAX;
    }
    int const cd = countdown[cell];
    if (cd < 0 || countdown_round[cell] < 0) {
      return INT_MAX;
    }
    int const now = cd - (round - countdown_round[cell]);
    if (now < 0) {
      return -now <= MEMORY_PEARL_ROUNDS ? distance : INT_MAX;
    }
    if (now > MEMORY_COUNTDOWN_CAP) {
      return INT_MAX;
    }
    return std::max(distance, now);
  }
};
