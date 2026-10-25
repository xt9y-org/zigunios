# Install Zigunios on your own iPad without paying $99

You need a Mac capable of running an Xcode release that supports your iPadOS version, an iPad, a USB-C cable (recommended for initial pairing), and a **free Apple Account**. No App Store, TestFlight, paid developer membership or jailbreak.

1. Install Xcode (for iPadOS 27, install a compatible Xcode 27 or newer) and open it at least once to accept the license and install platform support.
2. In **Xcode → Settings → Apple Accounts**, add your regular Apple Account.
3. Clone this private repository on the Mac, then open `Zigunios.xcodeproj`.
4. In the project navigator, select **Zigunios** → target **Zigunios** → **Signing & Capabilities**. Enable **Automatically manage signing**, choose your **Personal Team**, and change the bundle ID only if Xcode reports a conflict.
5. Connect the iPad via USB-C, unlock it, and accept **Trust This Computer** prompts. Select the iPad as the Xcode run destination (use Xcode's Devices and Simulators window if not listed).
6. Enable **Settings → Privacy & Security → Developer Mode** on the iPad if prompted. This requires restarting the iPad and confirming with the passcode. If the switch is missing, pair with Xcode first.
7. Press **Run (▶)** in Xcode. It builds, signs, installs and starts Zigunios on the iPad. Resolve any first-time Apple Account/team setup prompts in Xcode.
8. Open the **Files** app → **On My iPad** → **Zigunios** → `root` to manage your workspace; or use **Import files** inside Zigunios.

## Personal Team restrictions

Apple's free provisioning profiles expire after **7 days**. Rebuild/reinstall using Xcode after expiration. Apple currently limits the free team to **3 installed apps per device**, **up to 10 App IDs** and **up to 3 devices**; those registrations also expire after 7 days. Some managed entitlements and distribution options require a paid team. Existing files normally survive an update using the same bundle ID, but make a backup of important files in Files before uninstalling or reinstalling.

## Troubleshooting

- **No personal team:** ensure your Apple Account appears in Xcode → Settings → Apple Accounts and any agreements are accepted.
- **Signing error:** enable automatic signing and pick a unique Bundle Identifier.
- **Device not visible:** unlock iPad, trust the Mac, use a data-capable cable, and check Xcode → Window → Devices and Simulators.
- **Developer Mode missing:** connect to Xcode and pair first, then check Settings → Privacy & Security.
- **iPadOS 27 unsupported:** update to Xcode 27+ and ensure its macOS system requirements are satisfied.
- **App stopped launching after a week:** re-run via Xcode to renew personal-team provisioning.

Apple documentation:
- https://developer.apple.com/help/account/basics/about-your-developer-account
- https://developer.apple.com/documentation/xcode/running-your-app-on-simulated-or-physical-devices
- https://developer.apple.com/documentation/xcode/enabling-developer-mode-on-a-device
- https://developer.apple.com/documentation/uikit/transitioning-to-the-uikit-scene-based-life-cycle
