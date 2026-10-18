"""Fail closed unless guest state proves squad death and consumed mission replay.

This checks state and checkpoint evidence, not the completed-frame/control gate.
"""
import argparse
import json
from pathlib import Path
from guest_state import XBE_SHA256

PREPARE, ACCEPT, CONSUME, RELOAD = 0x8468F, 0x10B524, 0x8A38E, 0x8A4CF


def qualify(samples, route, deaths):
    def stamp(event):
        return event["qpc"] / event["qpc_hz"]

    def compatible(left, right):
        if left.get("interface") != right.get("interface"):
            return False
        if left.get("request") == right.get("request") and left.get("request"):
            return True
        a, b = left.get("payload", [])[1:], right.get("payload", [])[1:]
        return len(a) == 4 and a == b

    valid = [(row["monotonic_seconds"], row["state"]) for row in samples
             if row.get("state", {}).get("code_signature_matches") is True
             and row["state"].get("source_xbe_sha256") == XBE_SHA256
             and row["state"].get("campaign_roots_stable") is True
             and row["state"].get("death_flag") in (False, True)
             and isinstance(row.get("monotonic_seconds"), (float, int))]
    valid.sort(key=lambda item: item[0])
    events = [e for e in route if e.get("qpc_hz", 0) > 0 and e.get("flags") == 6]
    events.sort(key=stamp)
    predicates = [e for e in deaths if e.get("qpc_hz", 0) > 0
                  and e.get("pc") == 0x111D43 and e.get("predicate") == 1]
    clear = {}
    already_dead = set()
    for dead_at, state in valid:
        campaign = state.get("campaign_pointer")
        if state["death_flag"] is False:
            clear[campaign] = dead_at
            already_dead.discard(campaign)
            continue
        if campaign not in clear or campaign in already_dead:
            continue
        already_dead.add(campaign)
        for accepted in events:
            if accepted.get("pc") != ACCEPT or stamp(accepted) < dead_at:
                continue
            if not any(e.get("pc") == PREPARE and dead_at <= stamp(e) <= stamp(accepted)
                       and compatible(e,accepted) for e in events):
                continue
            if not any(e.get("campaign") == campaign and
                       clear[campaign] <= stamp(e) <= stamp(accepted) for e in predicates):
                continue
            for consumed in events:
                if consumed.get("pc") != CONSUME or stamp(consumed) < stamp(accepted) or not compatible(accepted, consumed):
                    continue
                for reloaded in events:
                    if reloaded.get("pc") != RELOAD or stamp(reloaded) < stamp(consumed) or not compatible(consumed, reloaded):
                        continue
                    reset = [(t, s) for t, s in valid if t > stamp(reloaded)
                             and s["death_flag"] is False and isinstance(s.get("sdk_swap_serial"), int)]
                    first_by_campaign = {}
                    for last_t, last in reset:
                        key = last.get("campaign_pointer")
                        if key not in first_by_campaign:
                            first_by_campaign[key] = (last_t, last)
                            continue
                        first_t, first = first_by_campaign[key]
                        delta = (last["sdk_swap_serial"] - first["sdk_swap_serial"]) & 0xFFFFFFFF
                        if last_t <= first_t or not 0 < delta < 0x80000000:
                            continue
                        return dict(guest_state_checks_passed=True,
                            status="guest_death_and_replay_observed_pending_capture_control_review",
                            death_campaign=campaign, death_clear_at=clear[campaign], death_set_at=dead_at,
                            accepted=accepted, consumed=consumed, reload_returned=reloaded,
                            resumed_campaign=first.get("campaign_pointer"), resumed_at=first_t,
                            later_at=last_t, resumed_swap_delta=delta,
                            limits="Does not qualify rendered HUD, movement or firing; review completed-frame evidence.")
    return dict(guest_state_checks_passed=False, status="guest_death_replay_not_proven",
                valid_state_samples=len(valid), replay_checkpoint_events=len(events),
                positive_death_predicates=len(predicates),
                errors=["Need clear-to-set failure flag, full Squad Killed predicate, Replay Mission preparation, accepted and consumed request, reload return, and resumed swaps with flag clear."])


def summarize(prefix):
    def side(suffix):
        return prefix.with_name(prefix.name + suffix)
    metadata = json.loads(side("-process.json").read_text())
    if metadata.get("source_xbe_sha256") != XBE_SHA256:
        return dict(guest_state_checks_passed=False, status="source_xbe_identity_not_verified")
    if "QueryPerformanceCounter" not in metadata.get("monotonic_clock", ""):
        return dict(guest_state_checks_passed=False, status="clock_alignment_not_verified")
    config = metadata.get("configuration", {})
    if config.get("RECOMP_BRUTE_FORCE_ROUTE_STATE") != "1":
        return dict(guest_state_checks_passed=False, status="guest_checkpoint_observer_not_enabled")
    samples = [json.loads(line) for line in side("-state.jsonl").read_text().splitlines()]
    route, deaths = [], []
    for line in side(".stderr.log").read_text(errors="replace").splitlines():
        if line.startswith("[GUEST ROUTE] "):
            route.append(json.loads(line[len("[GUEST ROUTE] "):]))
        elif line.startswith("[GUEST DEATH] "):
            deaths.append(json.loads(line[len("[GUEST DEATH] "):]))
    return qualify(samples, route, deaths)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("prefix", type=Path)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    result = summarize(args.prefix)
    text = json.dumps(result, indent=2) + "\n"
    if args.output:
        args.output.write_text(text)
    else:
        print(text)
    raise SystemExit(0 if result["guest_state_checks_passed"] else 1)
