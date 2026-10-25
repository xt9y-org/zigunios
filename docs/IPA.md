# Build a Zigunios IPA with GitHub Actions

This repository uses the [MobAI ios-builder](https://github.com/MobAI-App/ios-builder) GitHub Actions build template in `.github/workflows/ios-build.yml`, configured by [`builder.json`](../builder.json).

## Download from GitHub (no Mac build required)

1. Open [Actions → iOS Build](https://github.com/xt9y-org/zigunios/actions/workflows/ios-build.yml).
2. Every push to `main` builds automatically. For an on-demand build, choose **Run workflow**, select `main`, leave the optional inputs at their defaults and click the green button.
3. Open the latest successful run and scroll to **Artifacts**.
4. Download the **ipa** artifact (a ZIP), extract it, and use the `.ipa` inside.

The build targets **physical iPads** (ARM64, generic iOS device). The IPA contains the Zigunios v0.1 terminal prototype; it does **not** yet include Git, Zig, `c build run`, or a game runner.

## Install it without paying for Apple Developer

GitHub Actions can produce an **unsigned** IPA without an Apple Developer account, but an unsigned IPA **cannot be installed directly** on a stock iPad. Use one of these methods to sign the IPA with your free Apple Account:

- **SideStore or AltStore:** perform their initial pairing/setup, import the extracted IPA and let the app sign/install it.
- **Sideloadly (Mac or Windows):** connect your iPad over USB, select the IPA, sign/install using your free Apple Account.
- **MobAI (if you have it configured):** ios-builder documents an on-device installation/re-signing workflow using `builder mobai install <ipa>`. This is optional; GitHub Actions does not require a MobAI account.

Free Apple ID installations generally expire after **7 days**, at which point you must refresh/re-sign. Your locally stored data should be preserved by signing with the same app identity and updating the existing install; deleting an app can delete its sandbox.

**An unsigned IPA is not a signed, one-tap iPad installation file.** A paid developer membership is not needed for local free-account sideloading, but that signing step cannot be skipped.

## Optional MobAI Builder CLI

The checked-in workflow is compatible with the [ios-builder CLI](https://github.com/MobAI-App/ios-builder), which can trigger a build and download the artifact into `./dist/`:

```sh
brew install mobai-app/tap/ios-builder
builder auth github
builder ios build
```

Its configuration is kept in `builder.json`. Do not enable `use_signing` unless you have a supported provisioning profile and code-signing certificate configured; leave it disabled for free-Apple-ID sideloading.

## Local Xcode

You can still run `open Zigunios.xcodeproj` and select your free Personal Team to install directly through Xcode; see [DEVICE.md](DEVICE.md).

## CI implementation

The workflow is based on MobAI's [ios-build.yml](https://github.com/MobAI-App/ios-builder/blob/main/internal/workflow/templates/ios-build.yml), with these Zigunios-specific changes:

- Run on pushes to `main` as well as manual workflow dispatch and `ios-build/*` tags.
- Allow one-click manual runs with no mandatory `build_id`.
- Default to unsigned **Release**, set the iOS project path to `.`, and scheme to `Zigunios` via `builder.json`.

CI performs `xcodebuild` for `generic/platform=iOS`, creates a ZIP containing `Payload/Zigunios.app`, gives that file the `.ipa` extension and uploads it as artifact `ipa`.

For private repositories, macOS Actions minutes consume your plan's available quota. The workflow never commits signing credentials.
