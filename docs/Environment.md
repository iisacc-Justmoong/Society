# Environment

Environment is a persistent main page, separate from application Preferences.
It implements the six Society Figma frames in file `vzGhdYpJ2GeyNfwXADJeAu`:

| Frame | Page |
| --- | --- |
| 252:954 | Overview, hosting and synchronization |
| 252:1324 | Devices, grouped by Desktop / Tablet / Smartphone |
| 252:2676 | Apps / My apps |
| 263:5417 | Apps / All apps |
| 270:5384 | Apps / Other devices |
| 270:5472 | Apps / Web apps |

The original 220px navigation, 320px account panel, 24px content inset, 64px
letter badges and 286px cards are implemented using the installed LVRS library.
Cards reflow to two or one column. Below 1200px the account panel is available
through the toolbar Account button; below 760px section navigation becomes an
inline tab bar. The mobile bottom label is shortened to Env. below 440px while
its accessibility name remains Environment. Apps collection tabs scroll horizontally when necessary.
The Figma SVGs for the Environment navigation are included locally. The profile
avatar is account data, not the example avatar shown in the design.

## Navigation and actions

- Desktop and mobile Environment select the page; neither opens Preferences.
- Preferences remain available through the application menu, Command-comma,
  and hosting settings. The mobile preference sheet is titled Preferences.
- Browse is a main page for connected host files (folder navigation, download,
  pagination). Its tab no longer opens the device pairing sheet.
- Devices / Add device opens the existing NetworkDevices modal. Its QR action
  opens PairingPanel. Incoming pairing invitations and onboarding retain their
  existing entry points and authentication checks.
- Device details select and scroll to the detail section. Browse opens the selected host in
  Browse. Sync, automatic connection, refresh and disconnect use the existing
  NetworkDriveController, with unavailable actions disabled.
- App search filters the current collection; collection counts represent the
  full collection. Updates filters only actual update flags. Details opens the
  installation/license sheet. Local installed apps can be opened.
- Web apps allows HTTPS shortcuts to be added and deduplicated by URL, stored in
  the application's EnvironmentWebApps settings. They open in the system browser.

## Data boundaries

Device rows come from the existing account-scoped StorageNavigation snapshot,
plus the current device. Account identity and avatar come from AccountController.
Demo device counts, storage sizes, license grants and social counts are never
asserted as real data. Missing social counts use an em dash.

EnvironmentAppsModel identifies Society itself and installed Dreamscapes/Vincent
bundles on macOS. Generic product names (Notes, Motion, Studio, etc.) are not
matched to unrelated applications with the same name. The nine Figma catalogue
entries retain their descriptions and visual identities. Install is disabled
without a verified release provider; licenses are explicitly unverified. Other
platforms currently identify only Society. Remote app inventory is a separate
account-scoped input; a connected device alone never implies that it has any
particular app installed. The account web app uses the configured account service.
Additional web apps are user-provided URLs rather than invented service endpoints.

## Verification

`SocietyDriveTests environmentPagesPairingAndResponsiveLayout` checks page
routing, device addition, Browse isolation, collection switching, search,
HTTPS validation and desktop/mobile layouts at 1440, 1024, 760, 390 and 320px.
`environmentCatalogueDoesNotInventInstallationsOrLicenses` checks real catalogue
state and launch rejection for unknown apps. The existing desktop dashboard,
mobile navigation, Preferences and pairing regressions remain part of validation.
Set `SOCIETY_ENVIRONMENT_CAPTURE_DIR` to a path beneath `build/` to capture the
real Overview plus all six populated design fixtures. Fixture screenshots are
visual verification, not evidence of live app licenses or remote inventories.
