# 0019: Next block: an opponent that fights back, with a block and a dodge

Status: Accepted
Date: 2026-11-25
Issue: [#65](https://github.com/TMHSDigital/Open-Game-1/issues/65)
Supersedes: none

## Context

After decision 0018, punches find their target and land from wherever the player stands, the body faces the opponent in a combat stance, and a scripted play-test records and measures every change. The dummy never acts, though. It stands on its spot and takes hits, so there is no fight: nothing to read, avoid or answer, and a run can only count what the player dealt.

The design leans toward real-time melee duels and fights against several opponents. Both should be configurable per run, with patterned enemies the player can learn and a defence that is part cover, part dodge. There are still no animation clips beyond the stand-ins (decision 0017), so anything new has to be driven through the physics body and code, as the punch is.

The Game-Systems catalogue has a telegraphed opponent state machine from slap-boxing-simulator (Godot). It approaches, winds up, strikes and recovers, its data is separate from the body, and the length of the wind-up is the one knob that matters most. The idea ports; the code does not.

## Decision

The next block makes the dummy an opponent that fights back. It steps in and punches on a pattern the player can learn, and the player gets a defence: a block that takes most of a hit's force and a dodge that steps out of it. Runs count hits taken as well as given, and the scripted play-test gets scenarios for both sides.

Specifically:

- **The opponent:** a dummy in a run can be set to fight back. It is then driven by an AI controller with a small state machine: close in, wind up, punch, step back, circle, repeat. Its punch is the player's punch (the same strike component and target assist). Its wind-up is slowed to a length the run sets, which is the tell. It waits while the player is down and stops while it is itself staggered or down. A dummy set to stand still stays the passive test bed it is now.
- **The block:** held on its own key. Both fists are steered up in front of the head through the physics body, movement slows, and there is no sprint. A hit from the front while blocking keeps only a share of its force and counts as blocked, not landed. A hit big enough can still break through. A punch thrown from the guard opens it until the punch is over.
- **The dodge:** a press. A quick step in the direction of movement (backward when standing still) at a speed well above walking, with a short cooldown. It is a real step and grants no immunity, so a punch misses because the body is no longer where it was going.
- **Runs:** the setup gets a choice for whether the dummies fight back and a value for their wind-up. The statistics gain blocked hits and the opponents' punches thrown, landed and blocked, and the player's dodges.
- **Play-test:** scenarios for taking a punch, blocking one, dodging one and trading punches, each with the counts noted so the summary says what happened.

## Consequences

- There is a fight to play for the first time. A run against dummies that fight back has a way to fail: the player can be knocked down.
- The dummy gains a second life as an AI pawn. Code that assumed a dummy has no controller now has to tell the two apart. The combat stance changes from "a player's character" to "any controlled character", so an opponent faces its target the same way the player does.
- Block and dodge are new inputs (two more keys and gamepad buttons), and every new action needs a stable meaning across mods later (OD-003).
- The block is an arm pose made in code with no clip, like the punch. It will look stiff until real clips arrive (decision 0017 still holds). The dodge plays the existing locomotion faster, with the legs turned by the leg-yaw node.
- Hit results gain a value: Blocked. Anything that counts hits (the run statistics, the hit feedback, the strike's landed check) has to treat it as contact that did not land.
- The opponent's behaviour is a fixed pattern, not a behaviour tree. That is enough for a learnable duel and cheap to test. Smarter opponents (reading the player, blocking, combos chosen in response) are later work and would need their own decision.
- The opponent uses the same tuning as the player. Only the wind-up length is its own, so a change to the punch changes both sides, which keeps the duel fair while the numbers move.

## Alternatives

- **More attacks and stamina** (directional and combo attacks, a kick, a stamina cost per attack). This deepens the one verb that exists, but against a target that never answers, more attacks add little to play. They are easier to judge once there is a duel.
- **A first melee weapon** (a club or sword swung with the same physics strike). This moves toward the weapon melee the design leans on, but it has the same problem: a weapon against a passive dummy is still target practice. It also needs grip and swing work that is easier on top of a working duel.
- **Better animation clips through asset-import** (a fighting idle, a shallower crouch walk, punch and get-up motions). The game would look better but play the same. Clips are best chosen once the moves they must cover (block, dodge, more attacks) are known.

## Links

- Open decision: OD-019, Issue #65
- Decision 0016 (run skeleton and first melee attack), 0017 (animation in code, no new clips), 0018 (overhaul of the play-test findings)
- Game-Systems: slap-boxing-simulator, telegraphed opponent AI state machine
