# Dynamic Soundscape

## Main Menu Audio

- **Main menu music**: `music_main_menu_01` and `music_main_menu_02` (OGG, stereo, 90–180 s bar-aligned seamless loop, `AL_SOURCE_RELATIVE = AL_TRUE`) stream on **sources[58..59] — the same music stream sources used by gameplay stems**. Main menu and gameplay are mutually exclusive states, so these sources are never simultaneously needed by both. Main menu music uses the same stream partition mechanism as gameplay music. `UIManager` invokes `AudioSystem::transitionToGameplay()` to hand off sources[58..59] to the gameplay music system: the main menu music is crossfaded out over 1 s (constant-power, same curve as gameplay music crossfades) and then the gameplay music system takes over those sources to play the first gameplay stem. **Do not abruptly stop main menu music** — a hard stop produces a jarring cut; the 1 s fade-out bridges the transition. Main menu music loops indefinitely with the same bar-boundary constraint as gameplay stems (both variants authored to seamless loop at bar boundary; JSON sidecar mandatory: `music_main_menu_01.json`, `music_main_menu_02.json`). Variant selection: same random-excluding-repeat policy as gameplay stems. Authored to **−16 LUFS / −1 dBTP** (same as gameplay music stems).
- **Main menu audio source count**: Main menu music **reuses the gameplay music stream sources (sources[58..59])** — main menu and gameplay are mutually exclusive states, so their music sources are never simultaneously active. No new stream sources are required. During `GameState::MainMenu`: sources[58..59] play main menu music; sources[60..61] (ambient bed) are idle. During `GameState::Gameplay`: sources[58..59] switch to gameplay stems; sources[60..61] play the active ambient bed. `UIManager::transitionToGameplay()` triggers a 1 s constant-power fade-out of the main menu music on sources[58..59], then the gameplay music system takes over those sources. Total stream partition: **4 sources** (2 music + 2 ambient = kStreamSourceCount), unchanged from the gameplay-only design.

- **Ambient bed layer**: Looping environmental tracks crossfading by terrain type / camera altitude; ambient beds use the **stream partition** (2 dedicated non-evictable sources: active + crossfade target)
- **Minimum hold time**: An ambient bed must play for at least **one full crossfade duration (default 3 s real time)** before it can begin crossfading out. If the next time-of-day transition is requested before the minimum hold time has elapsed, the transition is **queued** and executes only after hold time passes. At simulation speeds ≥ 3×, dawn (05:00–06:00) and dusk (20:00–23:00) ambient beds are **collapsed** — transitions go directly day→night and night→day respectively, avoiding an ambient bed that never reaches full volume. **The collapse is pre-emptive, not reactive**: at simulation speed >= x3 (3× or 10×), if the simulation speed is >= x3 when the dawn/dusk time threshold is crossed, the system does NOT begin the crossfade at all — the current bed continues. If the speed drops to x1 while a dawn/dusk threshold is pending, the crossfade initiates at that point. Mid-crossfade reactive abort is NOT supported — once a crossfade begins (at speed x1), it completes normally even if speed increases during the crossfade. A speed increase during an ongoing crossfade does NOT trigger an abort.
- **Crossfade duration runs in real time** (not simulation time) — `m_ambientCrossfadeT` is advanced by wall-clock delta, not simulation delta. Ensures transitions feel consistent at all simulation speeds.
- **City zone layer**: Per-zone positional loops (R/C/I) culled beyond 300 m; max 16 simultaneous zone sources; zone layer sources assigned LOW priority
- **Adaptive music**: Stems organized by intensity (calm / growth / crisis); crossfade by `AL_GAIN` per stem **each audio thread wake (10 ms interval)** using a **constant-power crossfade curve**. The audio thread is the SOLE owner of `alSourcef(AL_GAIN)` calls for all streaming sources — the main thread MUST NOT call `alSourcef(AL_GAIN)` directly on streaming sources. NOTE: This threading constraint MUST be explicitly stated in the OAL-2 main-loop comment at the main game loop call site (Phase 1 exit criterion). The OAL-2 comment must convey that `AudioSystem::update()` QUEUES crossfade command objects to the audio thread via a mutex-protected command queue — it does NOT call `alSourcef(AL_GAIN)` directly on streaming sources. See the OAL-2 section in `implementation/phase-1.md` for the required comment language. All gain changes to streaming sources are executed by the audio thread; the main thread queues crossfade requests via a mutex-protected command:
  - Crossfade duration: **minimum 2 seconds** (configurable; default 3 s)
  - Gain curves: `gain_in = sin(t × π/2)`, `gain_out = cos(t × π/2)` where `t` goes from 0→1 over the crossfade duration. Constant-power preserves perceived loudness through the transition.
  - All stems authored at **same key, mode, and tempo** (target: **90 BPM**, 4/4 time signature) for additive mixing compatibility. Stems must be an integer number of bars in total length so loop points align to bar boundaries. **Cross-tier harmonic compatibility requirement**: ALL 6 gameplay music stems (calm_01, calm_02, growth_01, growth_02, crisis_01, crisis_02) MUST share the **same root key and mode** (e.g., all in C major, or all in A minor). During crossfades, two stems are simultaneously audible — if they are in different keys or modes, harmonic clashes produce dissonance that breaks the player's immersion. The intensity tier differentiation (calm vs. growth vs. crisis) must come from instrumentation, dynamics, and rhythm density — NOT from key or mode changes. This constraint applies equally to main menu music variants. **Asset validation**: the music production brief must specify the shared root key/mode, and each stem must be approved against this constraint before delivery. The sound artist must submit a crossfade audibility test (mixing calm_01 with growth_01 for 3 seconds) as part of the delivery package. The music production brief delivered in Phase 4 MUST explicitly confirm 44100 Hz stereo as the authoring sample rate. Delivering stems at 48000 Hz or any other rate is a hard asset error — AudioSystem validates the OGG header at load time and refuses to play mismatched assets.
  - **Variant selection policy**: On each loop restart of a music stem, randomly select between variant 01 and 02 for that intensity tier, provided the selected variant is not the currently playing one (no immediate repeat). Both variants within the same intensity tier must be authored to be interchangeable (same key, same BPM, compatible harmonic content). **At most 2 music stems are simultaneously active** (outgoing + incoming). Variant switching occurs only at loop boundaries, never mid-crossfade. State escalation (calm→growth→crisis) queues the second transition until the first crossfade completes; no more than one pending transition can be queued at a time.
  - **Beat-boundary synchronization**: Crossfades must not begin mid-phrase. `AudioSystem` uses a **software sample counter** (not `AL_SAMPLE_OFFSET`) to track stream position. `AL_SAMPLE_OFFSET` on a buffer-queue source returns offset within the current buffer only, not absolute stream position — it cannot reliably compute bar boundaries. The software counter is maintained per `AudioStream` and incremented by the number of samples decoded and queued each update cycle. **Critical timing distinction**: `m_samplesQueued` counts samples pushed to the AL buffer queue, NOT samples already played. At steady state with 8 buffers queued (64 KB each at 44100 Hz stereo ≈ 371 ms per buffer: 16384 frames / 44100 Hz ≈ 0.371 s), the queue is ~2.97 s ahead of playback — using `m_samplesQueued` directly would fire the crossfade ~1.1 bars early (at 90 BPM, 1 bar = 2.67 s). The crossfade condition must use the estimated playback position:

    ```cpp
    // In AudioStream:
    uint64_t m_samplesQueued{0};    // total samples pushed to AL queue (software counter)
    uint64_t m_nextBarBoundary{0};  // absolute sample index of next bar boundary.
    // INITIALIZATION: m_nextBarBoundary is zero-initialized. The guard condition
    // (m_nextBarBoundary > 0) in the crossfade check (see audio thread wake loop below)
    // prevents crossfade from firing before the first bar boundary is computed.
    // Do NOT initialize to kSamplesPerBar or any non-zero value.
    // The first bar boundary is computed by computeNextBarBoundary() on the audio thread
    // after stream start. Initializing to a non-zero value would cause the guard to
    // pass immediately on the first audio thread wake, potentially firing a false crossfade
    // before any audio has been queued.
    // kSamplesPerBuffer: 64 KB buffer, stereo int16 format (2 channels × 2 bytes/sample = 4 bytes/frame)
    // 64 KB = 65536 bytes / 4 bytes per stereo frame = 16384 frames per buffer.
    // At 44100 Hz: 16384 / 44100 ≈ 371 ms per buffer (8 buffers = ~2.97 s total queue depth).
    // "samples" throughout this spec means FRAMES (per-channel samples), NOT interleaved PCM values.
    // m_samplesQueued is incremented by the frame count returned by the OGG decoder
    // (e.g., ov_read() returns bytes — divide by channels * sizeof(short) to get frames).
    // Using interleaved sample count (2× for stereo) would double m_samplesQueued and cause
    // crossfades to fire at half the expected position — a hard-to-diagnose timing bug.
    // All music stems MUST be authored at 44100 Hz stereo. Attempting to load a stem
    // at a different sample rate or channel count is a hard asset error — AudioSystem must
    // validate the OGG header at load time and refuse to play mismatched assets.
    // kSamplesPerBuffer is sample-rate-independent (byte-based) but the bar boundary formula
    // uses 'sr' (sample rate from the JSON sidecar) — these must be consistent.
    static constexpr uint32_t kSamplesPerBuffer = 64 * 1024 / (2 * 2);  // = 16384 frames per buffer

    // CRITICAL: Read AL_BUFFERS_QUEUED exactly once per audio thread wake AND read it
    // BEFORE calling alSourceUnqueueBuffers. After unqueuing, AL_BUFFERS_QUEUED decreases
    // by the number of processed buffers removed from the queue. If buffersQueued is read
    // AFTER unqueuing, the samplesPlayed estimate uses the post-unqueue queue depth, which
    // underestimates the actual queue depth at the time of reading and computes a higher
    // samplesPlayed than the true value — potentially firing crossfades early.
    // Correct audio thread wake order: (1) read AL_BUFFERS_QUEUED, (2) read AL_BUFFERS_PROCESSED,
    // (3) call alSourceUnqueueBuffers, (4) decode PCM, (5) call alSourceQueueBuffers.
    // Reading it twice (once in computeNextBarBoundary, once in the crossfade check)
    // introduces a race: between the two reads the AL driver may dequeue a processed buffer,
    // causing the two calls to return different values and producing an inconsistent
    // samplesPlayed estimate. Store in a local and pass as a parameter to both uses.
    // Also guard against underflow: at stream start m_samplesQueued may be less than
    // buffersQueued * kSamplesPerBuffer (buffers queued but none played yet); in that
    // case samplesPlayed = 0 which correctly triggers no crossfade (m_nextBarBoundary starts > 0).

    static uint64_t computeSamplesPlayed(uint64_t samplesQueued, ALint buffersQueued) {
        uint64_t queued = static_cast<uint64_t>(buffersQueued) * kSamplesPerBuffer;
        return (samplesQueued > queued) ? samplesQueued - queued : 0;
    }

    void computeNextBarBoundary(uint32_t sr, float bpm, int beatsPerBar, ALint buffersQueued) {
        uint64_t spb = static_cast<uint64_t>((sr * 60.0 / bpm) * beatsPerBar);
        uint64_t samplesPlayed = computeSamplesPlayed(m_samplesQueued, buffersQueued);
        m_nextBarBoundary = ((samplesPlayed / spb) + 1) * spb;
    }

    // Audio thread wake loop (called every 10 ms):
    ALint buffersQueued = 0;
    alGetSourcei(sourceHandle, AL_BUFFERS_QUEUED, &buffersQueued);  // read ONCE per wake
    uint64_t samplesPlayed = computeSamplesPlayed(m_samplesQueued, buffersQueued);
    if (samplesPlayed >= m_nextBarBoundary && m_nextBarBoundary > 0) {
        /* fire crossfade; guard m_nextBarBoundary > 0 prevents false fire at stream start */
    }
    // Pass the same buffersQueued value when recomputing the boundary:
    // computeNextBarBoundary(sampleRate, bpm, beatsPerBar, buffersQueued);
    ```

    Accuracy: within ±1 buffer period (**~371 ms** at 44100 Hz stereo with 64 KB buffers: 16384 frames / 44100 Hz ≈ 371 ms per buffer), which is perceptually acceptable at 90 BPM (bar = 2.67 s). Maximum queue wait: 1 bar (~2.67 s at 90 BPM). Each stem must expose BPM and beats_per_bar metadata (in a companion JSON sidecar `<stem_name>.json`).
  - **Interrupted crossfade**: If a new crossfade is requested before the current one completes, the OUTGOING source's current gain (not 1.0) becomes the starting gain for the new outgoing fade; `m_musicCrossfadeT` resets to a **computed offset `t_offset`** such that `cos(t_offset × π/2) == current_gain_out` — this ensures the constant-power curve continues smoothly from the current gain level rather than snapping back to 1.0 at the interruption point. A jump-to-zero reset would produce an audible pop. `t_offset = (2/π) × arccos(current_gain_out)`. (**Note**: the gain_out formula is `cos(t × π/2)`, therefore the inverse is arccos — not arcsin.) **Clarification on which stem is "outgoing"**: During a crossfade from stem A → stem B, stem A is the outgoing stem (gain_out decreasing) and stem B is the incoming stem (gain_in increasing). When a NEW crossfade interrupts this with target stem C: **stem B becomes the new outgoing stem** (it was incoming during the interrupted fade, but is now fading out in favor of C). `current_gain_out` refers to stem B's current gain AT THE MOMENT OF INTERRUPTION — i.e., stem B's gain_in value from the interrupted crossfade (`sin(interrupted_t × π/2)`). Stem A is discarded at interruption (if its outgoing gain is already near 0, discard immediately; if not, it may be combined with stem B as a simultaneous outgoing). The t_offset is computed from stem B's interruption gain so the new B→C crossfade begins smoothly from B's current level.
  - Crossfade progress tracked separately: `AudioSystem::m_musicCrossfadeT` for music stems; `AudioSystem::m_ambientCrossfadeT` for ambient bed crossfades. Both are `std::atomic<float>` (0–1); updated by the audio thread each wake (10 ms interval). Music crossfade is read atomically by the main thread for stinger duck-ramp coordination; ambient crossfade is audio-thread-internal only.
- **Music ducking for stingers**: `m_musicDuckGain` is driven by a **DuckState machine** (audio thread only for gain writes; main thread reads `m_musicDuckGain` atomically):
  - **`m_duckTimer` advancement**: The duck timer must use **actual IClock elapsed time** (not a fixed 10 ms assumption). The audio thread may wake late under scheduler load. **Capture `nowSeconds()` exactly once per wake into a local variable**: `double now = m_clock->nowSeconds(); float dt = static_cast<float>(now - m_lastDuckWakeTime); m_lastDuckWakeTime = now;` — store `m_lastDuckWakeTime` as a `double` member of `AudioSystem` (declared in audio-system.md class member list). **Do NOT call `m_clock->nowSeconds()` twice in the same wake cycle** (e.g., `dt = nowSeconds() - m_lastDuckWakeTime; m_lastDuckWakeTime = nowSeconds();`): the two calls may return different values if the clock advances between them, causing `m_lastDuckWakeTime` to be slightly ahead of the `dt`-subtracted value — introducing accumulated drift over time. Advancing by a fixed `0.01f` per wake over-counts time when the thread wakes late and under-counts when it wakes early, causing DUCKING/RELEASING to finish at incorrect wall-clock times.

  **`m_lastDuckWakeTime` initialization**: `m_lastDuckWakeTime` MUST be initialized to `m_clock->nowSeconds()` **before** `notify_one()` signals init complete — specifically after `alcSetThreadContext` succeeds but before the constructor is unblocked. This placement is mandatory because the constructor returns as soon as `notify_one()` fires, and the main thread may call `triggerStinger()` immediately after construction completes. If `m_lastDuckWakeTime` is still `0.0` at that point, the first audio thread wake computes `dt = nowSeconds() - 0.0`, which equals the current epoch timestamp in seconds (~1,700,000,000 s), instantly driving the duck state machine through all transition thresholds. Initializing `m_lastDuckWakeTime` before `notify_one()` guarantees that by the time the constructor returns, the member already holds a valid timestamp:

  ```cpp
  // Audio thread startup sequence (in AudioSystem::audioThreadFunc):
  // 1. alcSetThreadContext(m_context) — exits on failure
  // 2. Initialize m_lastDuckWakeTime BEFORE signaling init complete:
  m_lastDuckWakeTime = m_clock->nowSeconds();
  // 3. Signal m_initDone / m_initCV to unblock constructor:
  { std::lock_guard<std::mutex> lk(m_initMutex); m_initDone = true; }
  m_initCV.notify_one();
  // 4. Enter streaming loop:
  while (true) {
      { std::unique_lock<std::mutex> lock(m_streamMutex);
        m_streamCV.wait_for(lock, std::chrono::milliseconds(10),
            [this]{ return m_stopThread.load(); }); }
      if (m_stopThread.load()) break;
      double now = m_clock->nowSeconds();
      float dt = static_cast<float>(now - m_lastDuckWakeTime);
      m_lastDuckWakeTime = now;
      updateStreams();
      updateOcclusion();
      updateDuckState(dt);
  }
  ```

  **Why this initialization point is required**: If `m_lastDuckWakeTime` is left at its default-initialised value (`0.0`), the first `dt = now - m_lastDuckWakeTime` produces a value equal to the current epoch timestamp in seconds (e.g., ~1,700,000,000 s). Even a single such wake with an epoch-sized `dt` would advance `m_duckTimer` by a billion seconds, instantly driving the duck state machine through all transition thresholds and producing undefined audio behaviour on the very first stinger trigger. Initialising `m_lastDuckWakeTime` immediately before the streaming loop guarantees that the first real `dt` is approximately 10 ms — the actual inter-wake interval.
  - **IDLE** (`m_musicDuckGain = 1.0f`): Normal. On any stinger trigger → set `m_duckStartGain = 1.0f`, reset `m_duckTimer = 0`, transition to DUCKING.
  - **DUCKING** (fast attack, 0.2 s): Ramp `m_musicDuckGain` from `m_duckStartGain` → 0.4 linearly: `m_musicDuckGain = m_duckStartGain + (0.4f - m_duckStartGain) * (m_duckTimer / 0.2f)`. Advance `m_duckTimer` by actual elapsed time (dt from `IClock`). When `m_duckTimer >= 0.2s` → transition to DUCKED. If any stinger trigger arrives during DUCKING, reset `m_duckTimer = 0` and set `m_duckStartGain = m_musicDuckGain` (capture current gain); stay in DUCKING. This prevents a gain snap when a stinger arrives while already ducking — the ramp continues smoothly from the current position toward 0.4. **`m_duckStartGain` must be declared as a member of `AudioSystem`** alongside `m_duckTimer` and `m_musicDuckGain`.
  - **DUCKED** (`m_musicDuckGain = 0.4f`): Hold during stinger playback. Each audio thread wake, query `AL_SOURCE_STATE` for **both V1 stinger sources** (crisis at sources[55], milestone at sources[56]). Transition to RELEASING only when **ALL** active stinger sources have exited `AL_PLAYING` — i.e., no stinger of any type is currently playing. This prevents the duck from releasing while a second concurrent stinger is still active (e.g., crisis stinger finishes while milestone stinger is still playing). Checking only the single triggering source leaves a gap: the duck releases mid-second-stinger, causing music to partially re-duck when the second stinger's duck re-engages. (Post-V1: when `stinger_game_over` is added at sources[57], extend this check to all three stinger sources.)
  - **RELEASING** (slow release, 1.5 s): Ramp `m_musicDuckGain` from 0.4 → 1.0 linearly over 1.5 s. Advance `m_duckTimer` by actual elapsed time (dt from `IClock`). When `m_duckTimer >= 1.5s` → transition to IDLE. If any new stinger fires during RELEASING, set `m_duckStartGain = m_musicDuckGain` (capture current gain at interruption), reset `m_duckTimer = 0`, transition to DUCKING. The DUCKING ramp then runs from `m_duckStartGain` → 0.4 using the formula above — preventing a snap back to 1.0.
  - Applied multiplicatively to each stem's computed crossfade gain each audio thread wake.
  - **Note: ambient beds are NOT ducked during stinger playback.** `m_musicDuckGain` applies only to music stems. Ambient bed gain remains at its configured level throughout stinger playback. The stinger-to-ambient ratio is acceptable because ambient beds run at lower gain than music stems; the stinger loudness target (−18 LUFS) is calibrated against the music level, not the ambient level.
  - **Game-over duck interaction** *(post-V1 — requires Scenario mode and `stinger_game_over`; not present in V1)*: When `stinger_game_over` fires (Scenario mode only), the duck state machine runs normally (IDLE → DUCKING → DUCKED → RELEASING). However, after the game-over state is entered, all gameplay music stems must be stopped once the stinger finishes playing and the duck ramp completes. The game-over screen has its own audio context (typically silence or a separate ambient). The sequence: (1) stinger fires → music ducks to 0.4 over 0.2 s; (2) stinger plays (3–4 s); (3) duck enters RELEASING, game-over screen is displayed — simultaneously fade out all gameplay stems over 2 s using `m_gameOverFade` (bool, set true by `setGameOverState()`) and `m_gameOverFadeT` (float accumulator, 0.0→2.0 s, advanced by audio thread dt each wake, used to compute per-stem gain during the fade); (4) when stems reach gain 0, stop them and mark all stream sources as intentionally stopped. This prevents the music from re-ducking or re-releasing after the game-over screen because there are no active stems. `AudioSystem` exposes `void setGameOverState()` which sets `m_gameOverFade = true`; the audio thread checks `m_gameOverFade` and advances `m_gameOverFadeT` during each stem gain update.
  - **Stinger loudness target**: Stingers must be authored to an integrated loudness of **−18 LUFS** (ITU-R BS.1770-3) with a **true peak ceiling of −1 dBTP**. This calibration is designed for playback alongside music ducked to 0.4 gain. Music stems are authored at −16 LUFS; 0.4 gain = 20×log₁₀(0.4) ≈ −8 dB, so ducked music ≈ −24 LUFS; stinger at −18 LUFS is approximately **+6 dB** above the ducked music level.
  - **Stinger interruption policy**: If a stinger source is already playing when a new stinger event of the same type fires, the new event is **dropped** (not queued). The in-progress duck cycle completes normally. This prevents overlapping duck ramps.
  - **Minimum time between stinger triggers**: 5 real seconds minimum between eligible triggers of the same stinger type to prevent rapid-fire spam in high-crisis scenarios.
  - **Stinger_milestone trigger thresholds**: `stinger_milestone` fires **only at City Rating tier transitions** — population thresholds 1K, 10K, 50K, and 500K. Population milestones that do NOT coincide with a City Rating transition (e.g., 100K) trigger a toast notification but do **not** fire the stinger. At population levels that coincide with City Rating transitions, the City Rating transition stinger fires (one stinger total); the population milestone toast is still shown. This prevents double-stinger at shared thresholds and ensures `stinger_milestone` is always meaningful (tied to tier unlocks, not raw population counts). Implementation: the game event dispatcher checks whether a population milestone also corresponds to a City Rating tier transition; only in that case is the stinger event emitted (while still posting the population milestone toast).

  Note: `stinger_milestone` fires for every City Rating tier transition — the "no double stinger" rule prevents two simultaneous stingers of different types at coinciding population/rating thresholds, but does not suppress the stinger.

- **Stinger source allocation (V1)**: 2 dedicated non-evictable SFX pool slots — sources[55] for `stinger_crisis` (`StingerType::CRISIS`) and sources[56] for `stinger_milestone` (`StingerType::MILESTONE`). In V1, `kStingerCount = 2` and `kEvictableSFXCount = 55` (sources[0..54] only). sources[57] is reserved as the post-V1 game-over stinger slot; it is idle in V1 (allocated by alGenSources but never acquired by any code path — acquireSFXSource(), acquireStingerSource(), and acquireStreamSource() all exclude this index) and does NOT receive an EFX occlusion filter — it is excluded from the general SFX pool range that receives filters (indices 0..54). These stinger sources are separate from the stream partition and cannot be evicted by normal SFX events. Acquired via `acquireStingerSource(StingerType)` only — see Source Pool section. *(Post-V1: `stinger_game_over` at sources[57] will be added when Scenario mode is implemented, setting `kStingerCount = 3` and keeping `kEvictableSFXCount = 55`.)*

### Vehicle Engine Audio

Vehicle engine uses two simultaneously-playing SFX pool sources per vehicle:

- **Crossblend**: `sfx_vehicle_engine_idle` (gain 1.0 at speed < 3 m/s, linearly to 0.0 at speed ≥ 8 m/s); `sfx_vehicle_engine_move` (gain 0.0 at speed < 3 m/s, linearly to 1.0 at speed ≥ 8 m/s). Both sources run simultaneously; blend via `alSourcef(src, AL_GAIN, ...)` per frame. Never stop/restart sources at threshold (causes clicks).
- **Speed pitch modulation**: `AL_PITCH` range 0.75 (stopped) to 1.35 (max speed), linear with speed ratio.
- **Vehicle class base pitch**: Car = 1.0; Bus/Truck = 0.85 (lower idle register). Applied as a multiplier on the speed pitch.
- **Vehicle source AL_VELOCITY**: Both vehicle engine sources (idle and move) must have `AL_VELOCITY` set to `(0, 0, 0)` at acquisition time and must NOT be updated with the vehicle's actual movement velocity. **Doppler effect is explicitly disabled for vehicle engine SFX.** Speed-dependent pitch is handled entirely by the `AL_PITCH` modulation above; the OpenAL Doppler formula would produce a separate pitch shift on top of `AL_PITCH` and interact with it in non-trivial ways, producing incorrect double-pitch artifacts. Setting `AL_VELOCITY = 0` ensures Doppler contributes zero pitch shift. **Implementation**: call `alSource3f(src, AL_VELOCITY, 0.f, 0.f, 0.f)` immediately after `acquireVehicleEnginePair()` for both acquired sources, before the first `alSourcePlay()`. The listener velocity is already set to 0 in `syncListenerToCamera()`, so both sides of the Doppler formula produce no shift when vehicle velocity is also 0.
- **Two SFX pool slots per vehicle** are consumed while the vehicle is active. Maximum 12 simultaneous vehicles — see `source-pool.md — Source Budget Allocation` for the derivation. When more than 12 vehicles are within audio range, priority-based eviction (lowest priority + greatest distance) selects which engine pairs to cut. The eviction algorithm must treat both slots of a vehicle pair as a single unit — partial eviction (one source of a pair) is prohibited.
- **Paired acquisition requirement**: Both idle and moving source slots must be acquired together or neither is acquired. If the pool cannot grant 2 free slots simultaneously, the vehicle receives no engine audio for that activation cycle. This prevents orphaned single-source vehicles where only one blend layer plays, producing incorrect audio (pure idle or pure moving tone with no blend).
- **Audio LOD cull distance**: Vehicle engine SFX are culled (both sources returned to pool atomically) when the vehicle is more than **150 m** from the listener. When the vehicle re-enters range, a new paired acquisition is attempted. The 150 m cull distance aligns with the `AL_INVERSE_DISTANCE_CLAMPED` max distance for traffic/vehicles.

- **Time-of-day audio schedule**:

| In-game hours | Active ambient bed | Music intensity | Crossfade duration |
|---|---|---|---|
| 06:00–20:00 (day) | Day ambient (city hum, birds) | Calm or Growth | 3 s constant-power |
| 20:00–23:00 (dusk) | Evening ambient (quieter traffic) | Calm | 3 s constant-power |
| 23:00–05:00 (night) | Night ambient (minimal traffic, insects) | Calm | 3 s constant-power |
| 05:00–06:00 (dawn) | Dawn ambient (birds, early traffic) | Calm | 3 s constant-power |

**Note**: All crossfade durations use the constant-power curve (`gain_in = sin(t × π/2)`, `gain_out = cos(t × π/2)`) — never a linear ramp. The label "linear" in earlier drafts was incorrect. The Crossfade Duration column above always means "constant-power duration".

**Authoring note — dawn/dusk collapse at default simulation speed**: At the default simulation speed of x3, the one-hour dawn window (05:00–06:00) and the three-hour dusk window (20:00–23:00) pass faster than the minimum hold time, so the ambient system collapses them entirely — transitions go directly day→night and night→day without ever activating the dawn or dusk intermediate bed. This is the expected behavior specified in the collapse logic above (pre-emptive collapse at speed >= x3). Sound artists MUST author the day and night ambient beds such that a direct day→night crossfade and a direct night→day crossfade both sound natural with no dawn/dusk intermediate. Specifically:

- The day ambient bed must have a tail character (lower energy, less prominent bird activity toward its end) that blends acceptably into the night bed without needing the dusk bed as a bridge.
- The night ambient bed must have an opening character that works as a direct continuation of the day bed at x3 speed and also works after a dawn bed introduction at x1 speed.
- Deliver a crossfade audibility test (day→night at 3 s constant-power, no intermediate) as part of the ambient bed delivery package. This test must be reviewed and approved before asset lock.
- The dawn and dusk beds are still authored and still used at x1 speed (e.g., during slow-play or pause-and-resume sessions); they must not be omitted. The authoring constraint above applies to the day and night beds only.

#### Time-of-Day Music Intensity Override

The time-of-day schedule takes **absolute precedence** over the simulation-state intensity selector for all non-day time periods. The simulation state (calm/growth/crisis) drives music intensity **only during day hours (06:00–20:00)**:

| Time-of-day period | Music intensity authority |
|---|---|
| 06:00–20:00 (day) | Simulation state (calm / growth / crisis) |
| 20:00–23:00 (dusk) | **Forced Calm**, regardless of simulation state |
| 23:00–05:00 (night) | **Forced Calm**, regardless of simulation state |
| 05:00–06:00 (dawn) | **Forced Calm**, regardless of simulation state |

**Transition into a forced-Calm window** (e.g. at 20:00 while Growth or Crisis is active): `AudioSystem` immediately queues a crossfade from the current intensity stem to a calm stem via the beat-boundary synchronization path. The crossfade executes at the next bar boundary — there is no instantaneous swap. If a music crossfade is already in progress (e.g., a crisis escalation crossfade started 2 s before 20:00), the in-progress crossfade is interrupted and the forced-Calm crossfade takes precedence, using the interrupted-crossfade `t_offset` formula: `t_offset = (2/π) × arccos(current_gain_out)`.

**Transition out of a forced-Calm window** (at 06:00, dawn→day): The simulation state is re-evaluated and the appropriate intensity stem is selected. If the simulation is still in Growth or Crisis at 06:00, the music crossfades from Calm to the active intensity stem at the next bar boundary.

**Crisis audio during nighttime disaster events**: If a crisis begins or continues during a nighttime period, the game does NOT switch to crisis music. Crisis music escalation applies during day hours only. A city under crisis at night retains the night Calm ambient bed and night Calm music stem — the crisis is communicated through notification stingers (`sfx_fire_alert`, `stinger_crisis`) rather than music intensity escalation. This is intentional: nighttime crisis music would disrupt the established day/night audio identity. Music ducking for stingers operates normally regardless of time of day.

**Implementation**: `AudioSystem` tracks `m_currentTimeOfDay` (enum: DAY / DUSK / NIGHT / DAWN). When the in-game clock crosses a time-of-day boundary (supplied by `CitySimulation` via `AudioSystem::setTimeOfDay()`), re-evaluate the current music intensity and queue a crossfade if the forced-Calm override activates or deactivates.
