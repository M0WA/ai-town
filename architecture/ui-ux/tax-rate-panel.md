# Tax Rate Panel

## Visual Design — Glass City

The Tax Rate Panel uses the Glass City deep-navy panel style:

- **Background**: `rgba(13, 27, 42, 0.85)` deep navy, **8 px corner radius** on all edges
- **Panel title** ("Tax Rates"): `#EBF4F6` near-white
- **Zone type row labels** (R / C / I): `#EBF4F6` near-white
- **Current rate numeric readout**: `#F0B429` amber, monospace font
  (`setElementMonoFont(handle)` applied to the numeric `IGUIStaticText` element)
- **Projected revenue change**: `#F0B429` amber (positive or neutral); `#F04E37` red when the
  rate change produces a projected deficit or negative delta
- **Rate bounds label** ("Max" / "Min" when a ± button is grayed out): `#4A7FA5` mid-blue
- **Pending rate change indicator** ("Tax rates updating next budget cycle"): `#E8960C`
  warning amber
- **"Tax changes cannot be undone" label**: `#4A7FA5` mid-blue

### Increment / Decrement Buttons

`+1%` and `−1%` buttons use the Glass City button tile:

- **Enabled**: `rgba(255, 255, 255, 0.08)` fill, 1 px `rgba(255, 255, 255, 0.18)` border
- **Hover**: `rgba(255, 255, 255, 0.15)` fill, 1 px `rgba(255, 255, 255, 0.35)` border
- **Grayed out at bounds**: `setElementEnabled(..., false)` — teal border absent; opacity
  reduced; standard disabled appearance

The active teal state (`rgba(0, 201, 200, 0.22)` wash + 2 px border) is NOT used for +/−
buttons — it applies only to toggle/mode-selection buttons, not momentary action buttons.
The +/− buttons use hover state as their highest visual state.

---

**Tax Rate Panel**: **Mutual exclusion with QueryPanel**: Opening the Tax Rate Panel closes the QueryPanel if it is open — only one floating panel may be active at a time (see `input-arbitration.md` Priority 4). Accessible by clicking the resource/budget bar (or pressing **T** — keyboard shortcut; rebindable in Settings > Controls with standard conflict detection). A 300×200 px floating panel anchored below the resource bar, horizontally centered. Contains three rows (one per zone type R/C/I). Each row shows: zone label, current tax rate %, decrement button (−1%), numeric readout, increment button (+1%). Rate changes take effect at the next budget tick. The current tax rate is shown alongside the projected revenue change for the next tick based on current population. Rate bounds: floor 1%, ceiling 25% (buttons grayed at bounds). **Key-repeat**: holding a +/− button triggers initial repeat after 400 ms, then every 150 ms. **Key-repeat rate cap**: to prevent runaway accidental changes, holding a +/− button is capped at a maximum cumulative delta of **±5 percentage points per continuous hold event**. Releasing and re-pressing the button resets the cap. Players who need larger adjustments must use direct text entry (see below). **Direct entry**: clicking the numeric readout field activates a text input (1 px border highlight); player types a value 1–25, confirmed by Enter or Tab, reverted by Escape. **Dismiss priority**: when the panel is open, a click anywhere on the resource/budget bar is treated as a panel-dismiss event only and does not simultaneously re-open the panel (dismiss handler at panel priority level consumes the click first). Panel also dismissed by pressing T again, clicking elsewhere, or pressing Escape. **Dismiss click event consumption**: When the panel is dismissed by a click elsewhere (outside the panel and outside the resource bar), that click is **NOT consumed** — it passes through to the underlying game-world handlers (camera pan, tool placement, etc.). This is consistent with `input-arbitration.md` Priority 4: the Tax Rate Panel is a non-destructive floating panel and players may want to pan the camera and close the panel in a single gesture. Because tax rate changes are non-spatial and non-destructive, consuming the dismiss-click provides no safety benefit and would frustrate players. Clicks within the panel bounds and clicks on the resource bar are still consumed normally. This panel is opened by the resource bar click or T key, not by the Zone/Utilities tool. **Undo integration**: Tax rate changes are **NOT included in the Ctrl+Z undo stack** (they are economy events, not spatial placement actions — consistent with the Undo System spec which excludes economy events from undo scope). To communicate this to the player: the Tax Rate Panel displays a small label below the increment/decrement buttons: "Tax changes cannot be undone". **Unsaved changes tracking**: Although tax rate changes are not undoable, they DO set `UIManager::m_hasUnsavedChanges = true` (same as zone placement, road placement, and demolition) — the player will be prompted to save on Quit if tax rates were changed. This is intentional: a city with a manually tuned tax rate represents meaningful game state that should be saved. Tax changes are both non-undoable AND save-tracked; these are orthogonal properties. **Pending rate change HUD indicator**: after the Tax Rate Panel is closed with changed rates, the resource/budget bar displays a small amber label: "Tax rates updating next budget cycle" until the next budget tick commits the change. This prevents players from being surprised when rates take effect.
