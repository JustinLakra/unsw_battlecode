#include "grid.hpp"
#include "helper.hpp"
#include "queen.hpp"
#include "targeting.hpp"

#include <algorithm>
#include <optional>

struct Candidate {
  Rating rating;
  unswbc::Direction direction;
  int index;
};

int count_exits(unswbc::Controller &ct, Grid const &grid, int index) {
  if (index == -1) {
    return 0;
  }

  int exits = 0;
  for (auto const direction : unswbc::Direction::get_direction_list()) {
    Rating rating;
    check_step(ct, grid, index, direction, rating);
    if (rating.tier == 2) {
      exits++;
    }
  }
  return exits;
}

int split_size(auto const &ct, int const min_units, int const cap,
               int const space, int const round,
               int const length_at_last_split) {
  if (space != 2) {
    return 0;
  }

  // emergency: low dragon count
  if (ct.get_unit_count() < min_units && ct.get_length() >= 6 &&
      ct.can_split(2)) {
    return 2;
  }

  // late game: no splitting
  if (round > 400) {
    return 0;
  }

  // normal splitting
  if (ct.get_unit_count() < cap && ct.get_length() >= 8 &&
      ct.get_length() - length_at_last_split >= 6 && ct.can_split(2)) {
    return 2;
  }
  return 0;
}

int main() {
  auto [ct, game] = unswbc::init();

  int length_at_last_split = -1; // set on the first turn
  bool const is_queen = ct.get_id() <= 1;

  // The queen seeds a few workers early, then never splits for growth again.
  constexpr int EARLY_SPLIT_ROUND = 100; // only before this round
  constexpr int QUEEN_SPLITS = 3;        // at most this many splits in total
  constexpr int QUEEN_SPLIT_LENGTH = 8;  // and only when at least this long
  int queen_splits_done = 0;

  auto const [width, height] = game.get_map_size();
  int const area = width * height;

  int const min_units = std::clamp(area / 250, 3, 6);
  int const cap = std::clamp(area / 90, 8, 32);

  constexpr int countdown_cap = 15;

  int heading = -1;

  while (unswbc::update(ct, game)) {
    // fallback so we always output an action; gets overwritten after our code
    // runs
    ct.make_move(ct.get_dir());

    if (heading == -1) {
      heading = ct.get_id() % 4;
    }

    auto directions = unswbc::Direction::get_direction_list();

    std::optional<Candidate> best;

    Grid const grid = build_grid(ct);

    // HUNT: a worker that can reach the enemy queen's head this turn rams it.
    // Both queens act before every worker, so its head won't move first.
    if (!is_queen) {
      int const enemy_queen = find_enemy_queen(ct);
      if (enemy_queen != -1) {
        auto const path = hunt_path(ct, grid, enemy_queen);
        if (!path.empty() &&
            static_cast<int>(path.size()) <= sprint_reach(ct.get_length())) {
          ct.make_moves(path);
          unswbc::end_turn();
          continue;
        }
      }
    }

    // the queen works out where enemies could ram it this round
    std::array<int, CELL_COUNT> margins;
    margins.fill(NOT_REACHABLE);
    if (is_queen) {
      margins = hunt_margins(ct, grid);
    }

    BFS_result const search = bfs(ct, grid);

    if (length_at_last_split == -1) {
      length_at_last_split = ct.get_length();
    }

    int const target = choose_target(ct, grid, search, countdown_cap);

    for (int d = 0; d < 4; d++) {

      auto const direction = directions[d];

      Rating rating;

      check_step(ct, grid, CENTRE_INDEX, direction, rating);

      // the queen never risks a blind portal: to her it's as bad as kelp
      if (is_queen &&
          ct.get_tiles()[CENTRE_INDEX].get_edge(direction).is_portal()) {
        rating.tier = 0;
      }

      if (is_queen) {
        int const dest = neighbour(CENTRE_INDEX, direction);
        int const m = dest == -1 ? NOT_REACHABLE : margins[dest];
        rating.hunt_safety = m < 0 ? 1000 : -m; // unreachable is best
      }

      // exits only mean something for safe moves
      if (rating.tier == 2) {
        int const dest = neighbour(CENTRE_INDEX, direction);
        Region const region = flood_fill(ct, grid, dest);
        rating.space = space_verdict(region, ct.get_length());
        rating.exits = count_exits(ct, grid, dest);
      }

      if (target != -1 && search.first_move[target] == d) {
        rating.progress = 1;
      }

      if (target == -1 && d == heading) {
        rating.progress = 1;
      }

      // check for the best move
      if (!best || rating > best->rating) {
        best = Candidate{rating, direction, d};
      }
    }

    // if we were forced off our heading, adopt the new direction.
    if (target == -1 && best->index != heading) {
      heading = best->index;
    }
    // TRAPPED: every move is fatal.
    if (best->rating.tier == 0) {
      if (is_queen) {
        // Shed 2 tail segments to stand still for a turn; the child crawls
        // away, which may also loosen our own coil.
        if (ct.can_split(2)) {
          ct.do_split(2);
        } else {
          ct.make_move(best->direction);
        }
      } else {
        // workers give their length to the other end, as before
        int const child = ct.get_length() - 2;
        if (ct.can_split(child)) {
          ct.do_split(child);
        } else {
          ct.make_move(best->direction);
        }
      }
      unswbc::end_turn();
      continue;
    }

    // GROWTH SPLITS
    int child = 0;
    if (is_queen) {
      // Standing still to split is only safe if no enemy can reach our head
      // this round, i.e. no neighbouring tile is reachable with a step to
      // spare.
      bool safe_to_stand = true;
      for (auto const d : unswbc::Direction::get_direction_list()) {
        int const n = neighbour(CENTRE_INDEX, d);
        if (n != -1 && margins[n] >= 1) {
          safe_to_stand = false;
        }
      }

      if (queen_splits_done < QUEEN_SPLITS &&
          game.get_round_num() < EARLY_SPLIT_ROUND &&
          ct.get_length() >= QUEEN_SPLIT_LENGTH && best->rating.space == 2 &&
          safe_to_stand && ct.can_split(2)) {
        child = 2;
      }
    } else {
      child = split_size(ct, min_units, cap, best->rating.space,
                         game.get_round_num(), length_at_last_split);
    }

    if (child > 0) {
      ct.do_split(child);
      length_at_last_split = ct.get_length() - child;
      if (is_queen) {
        queen_splits_done++;
      }
    } else {
      ct.make_move(best->direction);
    }
    unswbc::end_turn();
  }
}
