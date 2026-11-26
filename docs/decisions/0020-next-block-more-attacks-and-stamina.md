# 0020: Next block: more attacks and stamina

Status: Accepted
Date: 2026-11-26
Issue: [#66](https://github.com/TMHSDigital/Open-Game-1/issues/66)
Supersedes: none

## Context

After decision 0019 there is a duel: an opponent closes in and punches on a pattern, and the player can punch, block and dodge. But there is one attack, and nothing costs anything. The player can hold the block forever, punch as fast as the recovery allows and dodge whenever the cooldown is up, so none of these are choices. The design leans toward real-time melee where attack input mixes buttons with direction, and the resource that limits it mixes restraint with aggression, configurable per run.

There are still no animation clips beyond the stand-ins (decision 0017): every strike is steered through the physics body in code, as the punch is. The legs follow the animation and are not simulated in normal play, so a kick needs one leg simulated for its length.

The Game-Systems catalogue has a data-defined move from slap-boxing-simulator (Godot): every feel value of an attack is a field of one record that both the player and the AI read, so moves are tuned as data and never hard-coded in a controller. The idea ports; the code does not.

## Decision

The next block adds attacks and a stamina cost. The punch becomes a set of strikes (a jab, a hook from either side, a heavier straight, and a kick), each with its own speed, reach, weight and stamina cost. Stamina recovers over time, and blocking a hit costs some. A character out of stamina strikes slower and weaker and its guard gives way. The opponent's pattern uses the new strikes, and runs can set stamina on or off and how fast it recovers.

Specifically:

- **Moves are data.** Each strike is a row (jab, straight, left hook, right hook, kick) that scales the punch tuning: wind-up, strike and recovery time, reach, speed, weight behind it, and its stamina cost. The player and the opponent use the same rows.
- **Input.** The attack button alternates a lead-hand jab and a rear-hand straight (a one-two). Pressed while moving sideways in the combat stance, it throws a hook from that side. A new kick button (F, the right shoulder button) throws a front kick with the rear leg.
- **The kick** simulates the kicking leg for the length of the kick and steers the foot as the punch steers the fist; the standing leg stays animated, so the character keeps its footing.
- **Stamina.** A strike costs its cost; a dodge costs some; a blocked hit costs a share of the force the guard took. It recovers at a steady rate after a short pause. A strike thrown without enough stamina is slower and weaker. At zero the guard drops and stays down until stamina is back to a share of the maximum.
- **The opponent** attacks with sequences of these moves (a jab, a one-two, a hook, a kick) and holds off when its stamina is low.
- **Runs** can switch stamina off and scale how fast it recovers. The results list strikes by move, landed of thrown, and how often the player ran out.
- **Readable on screen:** a stamina bar for the player and one over each opponent that fights.
- **Checked by play-test:** new scenarios throw every move at a dummy and drain stamina on purpose, and the fight scenario plays against the new pattern.

## Consequences

- Attacking, blocking and dodging now trade against each other, and a run can be won or lost on stamina. A run with stamina off plays as before.
- There are five strikes to tune instead of one. Keeping them as multipliers of the punch tuning means a change to the punch moves all of them together, and the moves keep their relative character; the cost is that a single move cannot be pushed far from the punch without its own fields later.
- The kick is the first time a leg is simulated in normal play. It can scrape the floor or snag on the other body, and it ends with a short blend back to the animation. It looks like a code pose, as the punch and guard do, until real clips arrive.
- A hook needs sideways movement in the stance; on a gamepad that is the left stick, so the hook is reachable without a new button, but it is a gesture players have to learn. The pause screen's Keys tab and the README list it.
- New tuning (a Stamina group) shows on the Tuning tab; new run rows show on the Run tab, and saved run setups without them load with defaults.
- The opponent's pattern grows. It is still a fixed pattern, not a reading opponent; that remains a later decision.

## Alternatives

- **A first melee weapon** (a club or sword swung through the physics strike). It moves toward weapon melee, but a weapon on top of one attack and no cost repeats the problem this block fixes: the duel has no choices yet.
- **Real animation clips through asset-import.** The game would look better and play the same. Clips are better chosen once the moves they must cover are known, and this block decides those moves.
- **A smarter opponent** (one that blocks, dodges and punishes). It deepens the duel without new moves, but a smart opponent against one attack has little to read. It is a natural next step after this block.

## Links

- Open decision: OD-020, Issue #66
- Decisions 0017 (animation in code), 0018 (play-test overhaul), 0019 (the opponent, block and dodge)
- Game-Systems: slap-boxing-simulator, data-defined move feel curve
