# exp072 spotmicro_v6_1_6_prefall_tilt_recovery_v613_base

## Purpose

- Base policy: stable real-robot v6.1.3 checkpoint.
- Scope: normal locomotion plus pre-fall tilt recovery in one locomotion policy.
- Excluded: full get-up after body contact or complete fall.
- Real target: keep walking at about `vx=0.05~0.10 m/s`, tolerate large roll/pitch before full fall, and recover toward normal gait.
- Priority: do not sacrifice the stable v6.1.3 gait.

## Minimal Training Plan

Time is limited, so use only two practical stages:

1. Train this config as-is:
   - resume from `May20_14-29-52_spotmicro_v6_1_3_stronger_transition_push`, checkpoint `4100`
   - `recovery_roll_pitch_range_deg = 18.0`
   - recovery gating begins at `14 deg`, reaches full effect near `25 deg`
2. If normal gait diagnostics still pass, raise only `recovery_roll_pitch_range_deg` to `28.0~30.0` and run a shorter follow-up fine-tune.

Do not add full get-up randomization or body-contact recovery in these stages.

## Code Changes

- Added tilt-based recovery gating using `norm(projected_gravity[:, :2])`.
- Kept observation size unchanged at 47.
- Added `effective_commands`:
  - normal tilt: unchanged commands
  - large tilt: commands ramp down to `25%`
  - used consistently for phase, IK reference, observation, and velocity tracking rewards
- Added recovery phase slowdown:
  - normal tilt: normal phase progression
  - large tilt: phase progression ramps down to `20%`
  - freeze option exists but defaults to `False`
- Added recovery-only residual action authority:
  - normal action scale: `0.25`
  - large tilt action scale: ramps toward `0.35`
- Changed gait/IK relief from hard early relief to conservative ramp:
  - starts at `14 deg`
  - full near `25 deg`
  - IK/gait reward influence only drops to `65%`, not `25~35%`
- Restored v6.1.3-like reward/gait priorities:
  - `tracking_ik = 0.6`
  - `trot_contact = 0.35`
  - `swing_contact = -0.45`
  - `orientation = -10.0`
  - `feet_clearance = 0.03`
  - `no_stuck_feet = -0.2`
- Replaced v6.1.5 aggressive randomization with mild DR:
  - friction `[0.35, 1.10]`
  - base mass `[-0.05, 0.15] kg`
  - base COM x/y/z `[-0.020, 0.010] / [-0.008, 0.008] / [-0.008, 0.015] m`
  - motor strength `[0.85, 1.10]`
  - PD gain and joint-observation randomization still disabled
  - push/action-delay settings returned close to v6.1.3 baseline

## Pass Criteria

- Normal slow walking still stable at `vx=0.05~0.10 m/s`.
- Normal gait metrics do not regress materially from v6.1.3.
- Transition recovery success remains above about `85%`.
- Horizon-end roll/pitch returns near the stable threshold.
- Torque saturation, action rate, dof acceleration, and real actuator tracking error do not spike.

## Diagnostic / Report Update

- `play_diagnostic.py` now reports tilt recovery by bands:
  - `12-18 deg`
  - `18-25 deg`
  - `25-30 deg`
  - `30+ deg`
  - `18+ deg overall`
- `experiment_report.py` includes those band tables in both Recovery and Transition Recovery sections.
- The headline diagnostic now warns when there are no `18+ deg` transition/pre-fall trials, because then the 30 deg objective is not actually tested.
- To stress-test the current checkpoint near the final objective, run diagnostic with:

```bash
python legged_gym/legged_gym/scripts/play_diagnostic.py --task=spotmicro_test --with_dr --prefall-eval
```

Equivalent explicit override:

```bash
python legged_gym/legged_gym/scripts/play_diagnostic.py --task=spotmicro_test --with_dr --recovery-range-deg 30
```

## Next Stage After exp073

exp073 preserved normal gait and learned the `12-18 deg` band well, but `18+ deg` transition/pre-fall recovery was still weak and sparsely sampled. The next stage should keep all other axes fixed and raise only the reset recovery tilt range:

- run: `spotmicro_v6_2_1_prefall_tilt_recovery_25deg`
- resume: `May25_11-03-49_spotmicro_v6_2_prefall_tilt_recovery`, checkpoint `4900`
- `recovery_roll_pitch_range_deg = 25.0`
- `max_iterations = 600`

Do not expand DR, command range, yaw range, action scale, or relief settings in this stage. Those should wait until `18-25 deg` recovery is reliable and normal gait still passes.

## Next Stage After exp074

exp074 improved reset recovery in the `18-25 deg` band but transition/pre-fall recovery is not reliable enough for 30 deg:

- reset `18-25 deg`: `84.1%`
- transition `18-25 deg`: `56.3%`
- transition `25-30 deg`: `40.0%`
- normal gait still acceptable: timeout `97.0%`, early death `0.95%`, torque saturation `4.1%`

Continue the 25 deg stage instead of reverting or moving to 30 deg:

- run: `spotmicro_v6_2_2_prefall_tilt_recovery_25deg_continue`
- resume: `May25_11-31-19_spotmicro_v6_2_1_prefall_tilt_recovery_25deg`, checkpoint `5500`
- keep `recovery_roll_pitch_range_deg = 25.0`
- `max_iterations = 500`

Still do not expand DR, command range, action scale, reward relief, or push strength in this run.

## Next Stage After exp075

exp075 improved the continued 25 deg run enough to start 30 deg exposure:

- reset `18-25 deg`: `86.8%`
- transition `18-25 deg`: `63.4%`
- transition `18+ overall`: `65.0%`
- transition `25-30 deg`: `80.0%`, but only 5 trials
- normal gait remained stable: timeout `98.8%`, early death `0.19%`, torque saturation `4.0%`

Move to 30 deg reset exposure while keeping other settings fixed:

- run: `spotmicro_v6_2_3_prefall_tilt_recovery_30deg`
- resume: `May25_11-58-44_spotmicro_v6_2_2_prefall_tilt_recovery_25deg_continue`, checkpoint `6000`
- `recovery_roll_pitch_range_deg = 30.0`
- `max_iterations = 600`

Next pass criteria:

- normal gait: timeout `95%+`, early death `<2-3%`, torque saturation `<6-8%`
- reset `25-30 deg`: `75%+`
- transition `18+ overall`: `70%+`
- transition `25-30 deg`: enough trials and `60%+`

## Assessment After exp076

exp076 exposed the policy to the full 30 deg reset range, but it is not ready to expand beyond 30 deg yet:

- normal gait remained usable but regressed: timeout `92.3%`, early death `6.1%`, torque saturation `4.0%`
- reset recovery:
  - `18-25 deg`: `91.1%`
  - `25-30 deg`: `67.1%`, horizon-end tilt `9.0 deg`
- transition recovery:
  - `18-25 deg`: `73.0%`
  - `25-30 deg`: `52.5%` over 40 trials
  - `30+ deg`: `25.0%` over 32 stress trials, with high early failure
  - `18+ overall`: `60.8%`

The report contains the right banded data, but the automatic PASS/FAIL criterion is too loose for the current 30 deg pre-fall objective because warnings do not fail the overall result and the `25-30 deg` transition band is not a hard target.

Recommended next step:

- do not train beyond 30 deg yet
- keep `recovery_roll_pitch_range_deg = 30.0`
- either continue from exp076 or make one small recovery-only adjustment, not a broad expansion
- treat `30+ deg` as diagnostic stress only until `25-30 deg` transition recovery is reliable

Applied next config:

- run: `spotmicro_v6_2_4_prefall_tilt_recovery_30deg_continue`
- resume: `May25_13-32-12_spotmicro_v6_2_3_prefall_tilt_recovery_30deg`, checkpoint `6600`
- `max_iterations = 500`
- keep `recovery_roll_pitch_range_deg = 30.0`
- reduce recovery-only internal command scale from `0.25` to `0.15`
- reduce recovery-only gait phase progression from `0.2` to `0.1`
- keep residual recovery action scale at `0.35`

Report auto-judge was tightened for this pre-fall objective:

- normal gait timeout must be near `95%+`
- warning-level pre-fall metrics now fail the headline PASS in pre-fall runs
- reset `25-30 deg` and transition `25-30 deg` bands are checked explicitly
- transition `25-30 deg` requires at least 20 trials and about `65%+` success for PASS

Suggested readiness criteria before real testing or 30+ expansion:

- normal gait timeout `95%+` and early death `<3-5%`
- reset `25-30 deg` recovery `75-80%+`
- transition `25-30 deg` recovery `65-70%+`
- transition `18+ overall` `70-75%+`
- no rise in torque saturation/action rate

## Assessment After exp077

exp077 improved the stability side of the 30 deg stage but did not solve the main `25-30 deg` transition recovery bottleneck:

- normal gait improved versus exp076:
  - timeout `92.3% -> 94.8%`
  - early death `6.1% -> 3.3%`
  - torque saturation `4.0% -> 4.4%`
- reset recovery improved slightly:
  - `25-30 deg`: `67.1% -> 69.0%`
  - `18+ overall`: `79.8% -> 80.3%`
- transition recovery improved in the easier band:
  - `18-25 deg`: `73.0% -> 78.2%`
  - `18+ overall`: `60.8% -> 65.2%`
- but the target band is nearly flat:
  - transition `25-30 deg`: `52.5% -> 54.2%`

Conclusion: the command/phase slowdown was useful and should stay. Do not expand past 30 deg. For the next run, give the policy a small amount of extra high-tilt residual authority and slightly more relief from IK/gait constraints, but keep normal gait settings unchanged.

Applied next config:

- run: `spotmicro_v6_2_5_prefall_tilt_recovery_30deg_authority`
- resume: `May25_14-02-38_spotmicro_v6_2_4_prefall_tilt_recovery_30deg_continue`, checkpoint `7100`
- keep `recovery_roll_pitch_range_deg = 30.0`
- keep recovery command/phase slowdown: `command_scale = 0.15`, `phase_scale = 0.1`
- increase recovery-only residual action scale from `0.35` to `0.40`
- relax high-tilt gait/IK reward influence from `0.65` to `0.60`
- keep `max_iterations = 500`

Watch for torque/action-rate regression. If transition `25-30 deg` does not move above about `60%`, the next issue is likely not just more training; consider changing recovery sampling or reward shape rather than continuing indefinitely.

## Assessment After exp078

exp078 shows that the extra high-tilt authority was not a good next direction:

- reset recovery improved, but that is not the main real-robot target:
  - reset `25-30 deg`: `69.0% -> 74.4%`
  - reset `18+ overall`: `80.3% -> 84.3%`
- normal gait did not materially improve:
  - timeout `94.8% -> 94.1%`
  - early death `3.3% -> 3.7%`
- the key target regressed:
  - transition `25-30 deg`: `54.2% -> 44.4%`
  - transition `18+ overall`: `65.2% -> 63.5%`
- action rate rose (`0.0066 -> 0.0071`), which is not ideal for sim-to-real actuator tracking.

Decision: roll back the v6.2.5 authority change. The likely issue is not simply residual authority; the policy can recover from static/reset tilt, but walking-transition cases are not being sampled or shaped in a way that produces a visible bracing/stop behavior.

Applied rollback config:

- run: `spotmicro_v6_2_6_prefall_tilt_recovery_30deg_rollback`
- resume from exp077, not exp078: `May25_14-02-38_spotmicro_v6_2_4_prefall_tilt_recovery_30deg_continue`, checkpoint `7100`
- restore `recovery_action_scale = 0.35`
- restore `recovery_gait_relief_scale = 0.65`
- restore `recovery_ik_relief_scale = 0.65`
- keep recovery command/phase slowdown: `command_scale = 0.15`, `phase_scale = 0.1`
- keep `recovery_roll_pitch_range_deg = 30.0`
- set `max_iterations = 400`

Also added a visual inspection script:

```bash
python legged_gym/legged_gym/scripts/play_prefall_visual.py --task=spotmicro_test --tilt-deg 28 --axis pitch --cmd-x 0.08
```

This runs a single robot, walks at slow `vx`, injects a 28 deg pre-fall tilt, and prints the recovery blend and effective command scaling. Use this to visually confirm whether recovery mode looks like bracing/settling or just continuing gait. Add `--record-frames` to save viewer frames under `logs/spotmicro_test/exported/prefall_visual_frames`.

## Assessment After exp079

exp079 confirms that simply continuing the exp077 rollback settings is not enough:

- normal gait worsened relative to exp077:
  - timeout `94.8% -> 90.8%`
  - early death `3.3% -> 6.7%`
  - action rate `0.0066 -> 0.0074`
- reset recovery also worsened:
  - reset `25-30 deg`: `69.0% -> 68.0%`
  - reset `18+ overall`: `80.3% -> 77.4%`
- transition recovery worsened:
  - transition `18-25 deg`: `78.2% -> 73.2%`
  - transition `25-30 deg`: `54.2% -> 48.7%`
  - transition `18+ overall`: `65.2% -> 58.5%`

Decision: stop doing same-setting continuation. The limiting problem is that walking-transition `25-30 deg` samples are too sparse/indirect, while static reset tilt is already learnable. Add targeted transition pre-fall sampling during push events instead of increasing residual authority or continuing indefinitely.

Applied next config/code:

- run: `spotmicro_v6_2_7_prefall_transition_tilt_sampler`
- resume from exp077: `May25_14-02-38_spotmicro_v6_2_4_prefall_tilt_recovery_30deg_continue`, checkpoint `7100`
- keep v6.2.4 recovery mode:
  - `recovery_action_scale = 0.35`
  - `recovery_gait_relief_scale = 0.65`
  - `recovery_ik_relief_scale = 0.65`
  - `command_scale = 0.15`
  - `phase_scale = 0.1`
- add transition tilt push sampler:
  - enabled during normal push events
  - `35%` of envs get direct pre-fall roll or pitch tilt
  - tilt range `18-28 deg`
  - extra roll/pitch angular velocity up to `0.60 rad/s`
  - selected env command is constrained to `vx=0.05-0.10`, yaw `0`
- `max_iterations = 600`

Expected effect:

- more direct learning signal for walking `25-30 deg` recovery
- visible pre-fall bracing/settling should become more likely because the policy sees that state during locomotion rather than only from reset
- do not expand beyond 30 deg yet

Abort/revise if:

- normal timeout falls below `90%`
- action rate or torque saturation rises materially
- transition `25-30 deg` stays below `55-60%`

## Assessment After exp080

exp080 shows that the transition tilt sampler is useful but too strong:

- target transition band improved clearly:
  - transition `25-30 deg`: exp077 `54.2%` over 48 trials, exp079 `48.7%` over 39 trials, exp080 `63.7%` over 201 trials
  - transition `18+ overall`: exp077 `65.2%`, exp080 `71.7%`
- reset recovery also improved:
  - reset `25-30 deg`: exp077 `69.0%`, exp080 `80.0%`
- normal/stability regressed too much:
  - timeout `94.8% -> 76.3%`
  - action rate `0.0066 -> 0.0081`
  - torque saturation `4.4% -> 5.1%`
  - mean power `3.24W -> 3.66W`

The poor `30+ deg` transition result is expected because this project still targets pre-fall recovery up to about 30 deg, not get-up or post-body-contact recovery. Exp080's `30+ deg` transition band averaged about `45.7 deg`, which is outside the current target and was likely caused by transition tilt plus angular push accumulation.

Applied next config/code:

- run: `spotmicro_v6_3_1_prefall_transition_tilt_sampler_soft`
- resume from exp080: `May25_16-21-04_spotmicro_v6_3_prefall_transition_tilt_sampler`, checkpoint `7700`
- `max_iterations = 400`
- soften transition tilt sampler:
  - `transition_tilt_push_prob = 0.20`
  - `transition_tilt_push_max_deg = 27.0`
  - `transition_tilt_push_ang_vel_xy = 0.40`
- keep `transition_tilt_push_min_deg = 18.0`
- selected transition-tilt envs now overwrite roll/pitch angular velocity instead of adding on top of the normal push angular velocity

Expected effect:

- keep the improved `25-30 deg` transition exposure
- reduce excessive `30+ deg` / `40+ deg` stress cases
- recover normal timeout and action/torque margin

## Assessment After exp081

exp081 recovered much of the stability lost in exp080 while keeping the transition-sampler benefit:

- normal/stability improved versus exp080:
  - timeout `76.3% -> 88.9%`
  - early death `5.4% -> 3.1%`
  - torque saturation `5.1% -> 4.4%`
  - mean power `3.66W -> 3.40W`
- recovery stayed strong:
  - reset `25-30 deg`: `80.0% -> 79.5%`
  - reset `18+ overall`: `85.9% -> 87.0%`
- transition improved overall:
  - transition recovery total: `75.2% -> 80.7%`
  - transition `18+ overall`: `71.7% -> 76.8%`
- target band became under-sampled/weaker:
  - transition `25-30 deg`: `63.7%` over 201 trials -> `58.3%` over 72 trials
- `30+ deg` is still outside the current target and should be treated as stress only.

Decision: do not simply continue unchanged. Keep the soft sampler probability to protect gait, but shift the injected tilt range upward so sampled transition cases land more often in the `25-30 deg` band without reintroducing excessive angular velocity.

Applied next config:

- run: `spotmicro_v6_3_2_prefall_transition_tilt_sampler_focused`
- resume from exp081: `May25_16-41-52_spotmicro_v6_3_1_prefall_transition_tilt_sampler_soft`, checkpoint `8100`
- `max_iterations = 500`
- keep `transition_tilt_push_prob = 0.20`
- shift sampler range from `18-27 deg` to `22-27.5 deg`
- reduce sampler roll/pitch angular velocity from `0.40` to `0.35 rad/s`

Expected effect:

- more `25-30 deg` transition trials than exp081
- fewer excessive `30+` stress cases than exp080
- timeout should recover toward `90%+`

## Assessment After exp082

exp082 is worse than exp081 and should not be continued:

- normal/stability regressed:
  - timeout `88.9% -> 79.3%`
  - early death `3.1% -> 5.2%`
  - action rate `0.00806 -> 0.00866`
  - torque saturation `4.4% -> 4.9%`
- transition recovery regressed:
  - total transition recovery `80.7% -> 68.3%`
  - transition `18+ overall`: `76.8% -> 66.3%`
- target band did not really improve:
  - transition `25-30 deg`: `58.3%` over 72 trials -> `58.6%` over 133 trials
- excessive stress increased:
  - `30+ deg` transition trials `51 -> 92`

Conclusion: the focused `22-27.5 deg` sampler made more target-band trials, but it harmed the overall policy and did not raise target-band success. Roll back to the exp081 soft sampler range and only reduce angular velocity to protect normal gait and actuator margin.

Applied next config:

- run: `spotmicro_v6_3_3_prefall_transition_tilt_sampler_stable`
- resume from exp081: `May25_16-41-52_spotmicro_v6_3_1_prefall_transition_tilt_sampler_soft`, checkpoint `8100`
- `max_iterations = 400`
- `transition_tilt_push_prob = 0.20`
- restore sampler range to `18-27 deg`
- reduce sampler roll/pitch angular velocity from exp081 `0.40` to `0.30 rad/s`

Expected effect:

- recover timeout toward `90%+`
- keep transition `18+ overall` near or above `70%`
- keep transition `25-30 deg` around `55-60%` without increasing `30+` stress cases

## Assessment After exp083

exp083 recovered from exp082 but did not beat exp081:

- versus exp082:
  - timeout improved `79.3% -> 86.1%`
  - transition recovery improved `68.3% -> 81.1%`
  - transition `18+ overall` improved `66.3% -> 77.1%`
- versus exp081:
  - timeout is worse `88.9% -> 86.1%`
  - action rate is worse `0.00806 -> 0.00832`
  - transition `25-30 deg` is slightly worse `58.3% -> 56.9%`

Conclusion: sampler tuning alone is saturating. The current recovery implementation is a continuous gated locomotion policy, not a hard mode switch. Large tilt already scales commands/phase/action and relaxes gait/IK rewards, but at full tilt it still keeps `15%` command, `10%` phase progression, and `65%` IK/gait influence. That is not a strong enough "stop and brace" behavior, which explains why visual bracing is weak.

Also, simply making the robot fall more often is not enough. Once it enters full fall/body-contact states, this task terminates or leaves the pre-fall scope, so the policy does not get a useful recoverable gradient. The right next step is to keep sampling recoverable pre-fall states but make the high-tilt behavior more explicitly brace-like inside the locomotion policy.

Applied next config/code:

- run: `spotmicro_v6_3_4_prefall_brace_mode`
- resume from exp081: `May25_16-41-52_spotmicro_v6_3_1_prefall_transition_tilt_sampler_soft`, checkpoint `8100`
- `max_iterations = 500`
- recovery mode begins later to avoid breaking real-world normal gait wobble:
  - `tilt_threshold_deg = 17.0`
  - `full_tilt_deg = 27.0`
  - `recovery_reward_tilt_threshold_deg = 15.0`
  - `recovery_relief_tilt_threshold_deg = 17.0`
  - `recovery_relief_full_tilt_deg = 27.0`
- full high-tilt command scale `0.15 -> 0.0`
- full high-tilt phase scale `0.1 -> 0.0`
- add `recovery_stance_contact = 0.25` reward
- keep normal action scale `0.25` and recovery action scale `0.35`
- keep sampler stable range `18-27 deg`, probability `0.20`, angular velocity `0.30 rad/s`

Expected effect:

- high tilt should look more like stopping/bracing than continuing trot
- normal gait should remain mostly controlled because the change is gated by tilt blend
- watch timeout/action rate carefully; if normal gait degrades, reduce `recovery_stance_contact` or move the full-tilt threshold upward
