#include "includes.h"

void Aimbot::knife() {
    // Early exit if there are no targets.
    if (m_targets.empty())
        return;

    struct KnifeTarget_t { bool stab; ang_t angle; LagRecord* record; };
    KnifeTarget_t target{};

    // Store local variables to avoid repeated vector access.
    const auto& knife_ang = m_knife_ang;
    const auto& local_targets = m_targets;

    // Iterate all targets.
    for (const auto& t : local_targets) {
        // This target has no records, skip to the next target.
        if (t->m_records.empty())
            continue;

        // See if target broke lagcomp.
        if (g_lagcomp.StartPrediction(t)) {
            LagRecord* front = t->m_records.front().get();
            front->cache();

            // Trace with front.
            for (const auto& a : knife_ang) {
                // Check if we can knife.
                if (!CanKnife(front, a, target.stab))
                    continue;

                // Set target data.
                target.angle = a;
                target.record = front;
                break;
            }
        }
        else {
            // We can history aim.
            LagRecord* best = g_resolver.FindIdealRecord(t);
            if (!best)
                continue;

            best->cache();

            // Trace with best.
            for (const auto& a : knife_ang) {
                // Check if we can knife.
                if (!CanKnife(best, a, target.stab))
                    continue;

                // Set target data.
                target.angle = a;
                target.record = best;
                break;
            }

            LagRecord* last = g_resolver.FindLastRecord(t);
            if (!last || last == best)
                continue;

            last->cache();

            // Trace with last.
            for (const auto& a : knife_ang) {
                // Check if we can knife.
                if (!CanKnife(last, a, target.stab))
                    continue;

                // Set target data.
                target.angle = a;
                target.record = last;
                break;
            }
        }

        // Target player has been found already.
        if (target.record)
            break;
    }

    // We found a target, set output data and choke.
    if (target.record) {
        // Set target tick.
        g_cl.m_cmd->m_tick = game::TIME_TO_TICKS(target.record->m_pred_time + g_cl.m_lerp);

        // Set view angles.
        g_cl.m_cmd->m_view_angles = target.angle;

        // Set attack1 or attack2.
        g_cl.m_cmd->m_buttons |= target.stab ? IN_ATTACK2 : IN_ATTACK;

        // Choke.
        *g_cl.m_packet = false;
    }
}

bool Aimbot::CanKnife(LagRecord* record, const ang_t& angle, bool& stab) {
    // Convert target angle to direction.
    vec3_t forward;
    math::AngleVectors(angle, &forward);

    // See if we can hit the player with full range, this means no stab.
    CGameTrace trace;
    KnifeTrace(forward, false, &trace);

    // We hit something else than we were looking for.
    if (!trace.m_entity || trace.m_entity != record->m_player)
        return false;

    bool armor = record->m_player->m_ArmorValue() > 0;

    // Smart knifebot.
    int health = record->m_player->m_iHealth();
    int stab_dmg = m_knife_dmg.stab[armor][KnifeIsBehind(record)];
    int slash_dmg = m_knife_dmg.swing[g_cl.m_weapon->m_flNextPrimaryAttack() + 0.4f < g_csgo.m_globals->m_curtime][armor][KnifeIsBehind(record)];
    int swing_dmg = m_knife_dmg.swing[false][armor][KnifeIsBehind(record)];

    if (health <= slash_dmg)
        stab = false;
    else if (health <= stab_dmg)
        stab = true;
    else if (health > (slash_dmg + swing_dmg + stab_dmg))
        stab = true;
    else
        stab = false;

    // Damage-wise a stab would be sufficient here.
    if (stab && !KnifeTrace(forward, true, &trace))
        return false;

    return true;
}

bool Aimbot::KnifeTrace(const vec3_t& dir, bool stab, CGameTrace* trace) {
    float range = stab ? 32.f : 48.f;
    vec3_t start = g_cl.m_shoot_pos;
    vec3_t end = start + (dir * range);

    CTraceFilterSimple filter;
    filter.SetPassEntity(g_cl.m_local);
    g_csgo.m_engine_trace->TraceRay(Ray(start, end), MASK_SOLID, &filter, trace);

    // If the above failed, try a hull trace.
    if (trace->m_fraction >= 1.f) {
        g_csgo.m_engine_trace->TraceRay(Ray(start, end, { -16.f, -16.f, -18.f }, { 16.f, 16.f, 18.f }), MASK_SOLID, &filter, trace);
        return trace->m_fraction < 1.f;
    }

    return true;
}

bool Aimbot::KnifeIsBehind(LagRecord* record) {
    vec3_t delta{ record->m_origin - g_cl.m_shoot_pos };
    delta.z = 0.f;
    delta.normalize();

    vec3_t target;
    math::AngleVectors(record->m_abs_ang, &target);
    target.z = 0.f;

    // Check if the player is behind the target.
    return delta.dot(target) > 0.475f;
}