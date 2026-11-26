// Shared CLAP UI edit delivery. Include with Plugin and its parameter helpers in scope.
bool applyScreenEdits (Plugin& p, const hdui::Edits& edits)
{
    bool changed = false, pressed = false;
    auto set = [&](int index, float value) {
        const auto& param = paramTable[index];
        value = clampParam (param, value);
        if (p.values[index].exchange (value, std::memory_order_relaxed) == value) return;
        p.outgoing.fetch_or (1ull << index, std::memory_order_release);
        p.mainChanged.store (true, std::memory_order_release);
        textMoved (p, param.id);
        changed = true;
    };
    for (int i = 0; i < edits.count; ++i)
    {
        const auto& edit = edits.list[i];
        const int index = paramIndex (edit.id);
        if (index < 0)
            continue;
        if (edit.press)
        {
            if (edit.id == ParamId::regen && valueOf(p, ParamId::seedMode) >= .5f)
                set(paramIndex(ParamId::seed), nextPanelSeed(uint16_t(valueOf(p, ParamId::seed))));
            p.pressedAt.store (steadyMs(), std::memory_order_release);
            p.presses.fetch_or (1ull << index, std::memory_order_release);
            pressed = true;
        }
        else set(index, edit.value);
    }
    // The host's own copy of the values: read again now (this is a main-thread call), since a host may not run the
    // plug-in soon enough to hear them from process() (Melodee does not run a sleeping instrument, and its
    // request_flush waits for the next block). The output events still follow for hosts that record them.
    if (changed && p.hostParams != nullptr)
    {
        if (p.hostParams->rescan != nullptr)
            p.hostParams->rescan (p.host, CLAP_PARAM_RESCAN_VALUES);
        if (p.hostParams->request_flush != nullptr)
            p.hostParams->request_flush (p.host);
    }
    // A press, or a value the engine should take: ask to be run (a host that put the plug-in to sleep wakes it).
    if ((changed || pressed) && p.host != nullptr && p.host->request_process != nullptr)
        p.host->request_process (p.host);
    return changed || pressed;
}
