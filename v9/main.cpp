#include "grid.hpp"
#include "helper.hpp"
#include "memory.hpp"
#include "queen.hpp"
#include "targeting.hpp"

#include <algorithm>
#include <array>
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

int split_size(unswbc::Controller const &ct, int const min_units, int const cap,
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

  // The queen seeds a worker early, then never splits for growth again.
  constexpr int EARLY_SPLIT_ROUND = 100; // only before this round
  constexpr int QUEEN_SPLITS = 1;        // at most this many splits in total
  constexpr int QUEEN_SPLIT_LENGTH = 8;  // and only when at least this long
  int queen_splits_done = 0;

  auto const [width, height] = game.get_map_size();
  int const area = width * height;

  BeliefMap memory;
  memory.init(width, height);

  // STARVATION: explore when we haven't eaten in a while or see nothing coming
  constexpr int STARVE_TURNS = 30; // rounds without eating = starving
  constexpr int BARREN_COUNTDOWN =
      60;                          // a spawn further off than this isn't "soon"
  constexpr int BARREN_TURNS = 10; // turns in a row seeing nothing soon
  constexpr int EXPLORE_TURNS =
      20; // how long explore mode lasts once triggered
  int last_ate_round = 0;
  int last_length = -1;
  int barren_turns = 0;
  int explore_until = -1;

  int const min_units = std::clamp(area / 250, 3, 6);
  int const cap = std::clamp(area / 90, 8, 32);

  constexpr int countdown_cap = 15;

  int heading = -1;

  while (unswbc::update(ct, game)) {
    // fallback so we always output an action; overwritten by our real choice
    ct.make_move(ct.get_dir());

    if (heading == -1) {
      heading =
          ct.get_id() % 4; // different dragons start in different directions
    }

    auto const directions = unswbc::Direction::get_direction_list();

    std::optional<Candidate> best;

    Grid const grid = build_grid(ct);

    // MEMORY: record what we see, before anything can end the turn early
    int const round = game.get_round_num();
    memory.update(ct, round);

    // did we eat? (or is this our first turn)
    if (last_length == -1 || ct.get_length() > last_length) {
      last_ate_round = round;
      explore_until = -1;
    }
    last_length = ct.get_length();

    if (length_at_last_split == -1) {
      length_at_last_split = ct.get_length();
    }

    // RAM: a worker rams the enemy queen, or any enemy head whose death would
    // cost them more than us, if it can reach it this turn.
    if (!is_queen) {
      auto const path = choose_ram(ct, grid);
      if (!path.empty()) {
        ct.make_moves(path);
        unswbc::end_turn();
        continue;
      }
    }

    // the queen works out where enemies could ram it this round
    std::array<int, CELL_COUNT> margins;
    margins.fill(NOT_REACHABLE);
    if (is_queen) {
      margins = hunt_margins(ct, grid);
    }

    BFS_result const search = bfs(ct, grid);

    // is anything worth waiting for in view?
    bool something_soon = false;
    for (int i = 0; i < CELL_COUNT; i++) {
      int const cd = grid.grid[i].pearl_countdown;
      if (grid.grid[i].pearl || (cd >= 0 && cd <= BARREN_COUNTDOWN)) {
        something_soon = true;
        break;
      }
    }
    barren_turns = something_soon ? 0 : barren_turns + 1;

    if ((round - last_ate_round >= STARVE_TURNS ||
         barren_turns >= BARREN_TURNS) &&
        explore_until < round) {
      explore_until = round + EXPLORE_TURNS;
    }
    bool const exploring = round < explore_until;

    // exploring: don't wait on countdowns (a cap of -1 skips them all)
    int target =
        choose_target(ct, grid, search, exploring ? -1 : countdown_cap);

    // exploring workers: if we're on a portal tile, we'll take it (below);
    // the edge behind us doesn't count (it's the one we just came through)
    auto const behind = ct.get_dir().get_opposite();
    bool on_portal = false;
    for (auto const d : directions) {
      if (d != behind && ct.get_tiles()[CENTRE_INDEX].get_edge(d).is_portal()) {
        on_portal = true;
      }
    }

    // otherwise, exploring workers head for the nearest portal in view
    if (target == -1 && exploring && !is_queen && !on_portal) {
      int best_dist = CELL_COUNT;
      for (int i = 0; i < CELL_COUNT; i++) {
        if (i == CENTRE_INDEX || search.distance[i] == -1 ||
            search.distance[i] >= best_dist) {
          continue;
        }
        for (auto const d : directions) {
          if (ct.get_tiles()[i].get_edge(d).is_portal()) {
            target = i;
            best_dist = search.distance[i];
          }
        }
      }
    }

    // nothing in view: let the belief map pick a direction
    bool const take_portal =
        exploring && !is_queen && on_portal && target == -1;
    int const explore_dir =
        (target == -1 && !take_portal) ? memory.plan(ct, round, exploring) : -1;

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

      // space and exits only mean something for safe moves
      if (rating.tier == 2) {
        int const dest = neighbour(CENTRE_INDEX, direction);
        Region const region = flood_fill(ct, grid, dest);
        rating.space = space_verdict(region, ct.get_length());
        rating.exits = count_exits(ct, grid, dest);
      }

      // head for the target in view
      if (target != -1 && search.first_move[target] == d) {
        rating.progress = 1;
      }

      // no target in view: follow the belief map
      if (target == -1 && d == explore_dir) {
        rating.progress = 1;
      }

      // no target and no plan: keep our heading
      if (target == -1 && explore_dir == -1 && d == heading) {
        rating.progress = 1;
      }

      // an exploring worker on a portal tile steps through it (never backwards)
      if (take_portal && direction != behind &&
          ct.get_tiles()[CENTRE_INDEX].get_edge(direction).is_portal()) {
        rating.tier = 2;
        rating.space = 2;
        rating.progress = 1;
      }

      if (!best || rating > best->rating) {
        best = Candidate{rating, direction, d};
      }
    }

    // if we were forced off our heading, adopt the new direction
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
        // workers give their length to the other end of the body
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
      // this round: no neighbouring tile reachable with a step to spare.
      bool safe_to_stand = true;
      for (auto const d : directions) {
        int const n = neighbour(CENTRE_INDEX, d);
        if (n != -1 && margins[n] >= 1) {
          safe_to_stand = false;
        }
      }

      if (queen_splits_done < QUEEN_SPLITS && round < EARLY_SPLIT_ROUND &&
          ct.get_length() >= QUEEN_SPLIT_LENGTH && best->rating.space == 2 &&
          safe_to_stand && ct.can_split(2)) {
        child = 2;
      }
    } else {
      child = split_size(ct, min_units, cap, best->rating.space, round,
                         length_at_last_split);
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
