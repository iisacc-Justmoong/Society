import LVRS 1.0 as LV

// Figma's Environment buttons use the standard LVRS geometry and explicit tone.
LV.LabelButton {
    tone: LV.AbstractButton.Default
    textColor: tone === LV.AbstractButton.Primary ? LV.Theme.panelBackground03 : LV.Theme.bodyColor
}
