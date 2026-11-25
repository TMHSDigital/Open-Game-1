---
id: 2026-11-25-01
date: 2026-11-25
goal: "Session 7: the first melee attack, counted by runs"
start_sha: 558dfd9cb6529abd78921aa1488cc7c953af81d7
end_sha: 7d0f709d99690906ce76c5fa7aaec84108a9f6c0
decisions: [ADR-0016]
cameras: []
clip: null
---

# 2026-11-25-01: Session 7: the first melee attack, counted by runs

## Narrative

This session built the second half of decision 0016: the first melee attack. It was not recorded.

The attack is a straight punch on the left mouse button or the right trigger, thrown with alternating hands, and it has no animation. The arms were already simulated bodies that physical animation pulls toward the animated pose. A punch takes one hand away from that for a moment and steers its body: back to the shoulder, then out to a point on the centre line along the aim while the chest is pushed the same way, then lets go, and the arm swings back to the pose by itself. Whatever the fist runs into stops it.

The hit goes through the entry everything else uses. The path of the fist is swept each frame of the strike, and the first body of another character it meets takes a contact hit as if a set weight had arrived at the fist's speed: the weight behind the punch, not the weight of the hand. So a punch staggers by the same rules as a thrown ball, lands on the body part it touched, and was counted by runs without the run code knowing what a punch is. Runs now also count punches thrown and landed. Seven values (the three phase times, reach, speed, lean and weight) are sliders on the Tuning tab under Punch.

Three things were wrong on the way. The first version threw the fist straight out from the shoulder, and it passed beside the dummy: two characters cannot stand closer than their capsules, and an arm alone barely crosses that gap. The fist now goes to the centre line and the upper body leans into it. Then the fist was visibly stopped by the dummy and still no hit was reported, because the sweep was centred on the wrist and ended two centimetres short of what the knuckles touched. And a fixed-size array could not be exposed the way the first header declared it.

A fresh review found five problems, all fixed. A punched prop got a raw impulse with no ceiling, enough to shoot a light box out of the map. The last frame of a strike, the one at full reach, was never swept. An attacker who had brushed the dummy a moment before had its punch refused as a repeat. A punch on a character that was already down counted as landed and inflated the statistics. A reset in the middle of a strike was swept as if the fist had travelled the whole way.

With the default values the fist reaches about 60 cm at just under 1000 cm/s and a punch is a stagger of about 150 cm/s; a knockdown needs 500, so one punch never knocks a dummy down. Checked in the running game: the Run tab and its rows, a run with three dummies, the line on screen, and punches thrown with the mouse, six thrown and three landed on the chest and head, shown on the tab. This was also the first time the Run tab from the last build was seen on screen, and it looked right. Not seen: a punch in mid-swing, which a still capture was too slow to catch, so how it looks is not judged. Not tried: the gamepad trigger, and punching a physics box.

All 110 tests pass, two of them new. Uploaded as build 0.1.7 on the development branch. Next: the owner plays it and says how the punch feels and looks. The animation block (#37, #38) is still waiting, and the punch will need it most.

## Changes

- 7cc3c48 feat: the first melee attack, a punch steered through the physics body (decision 0016)
- 0123510 fix: punch review points, and the strike in the systems map
- 4f72d62 chore: release 0.1.7
- 7d0f709 docs: record Steam build 0.1.7 (BuildID 26919197)

## Decisions touched

- ADR-0016

## Captures

_No captures._

## Clip

_No clip._
