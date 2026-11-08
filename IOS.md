# Cadet Prep on iPhone and iPad

The iPhone/iPad version is the same app as the Mac version: the same screens, questions,
Warrior Knowledge cards, and insignia pictures. It uses a phone layout on narrow screens:
any iPhone, and an iPad held upright. A menu button at the top opens the sidebar. On an
iPad held sideways, you get the full Mac layout.

You install it from your Mac with Xcode. With a free Apple ID, the app keeps working for
**7 days** after each install. After that, plug in and press Run again; your progress
stays. A paid Apple Developer account ($99 a year) makes each install last a year.

## One-time setup

1. Install **Xcode** from the Mac App Store (free, about 15 GB). Open it once and let it
   finish installing its components. When it asks which platforms you want, include **iOS**.
2. Point the command-line tools at Xcode (it asks for your Mac password):
   ```bash
   sudo xcode-select -s /Applications/Xcode.app/Contents/Developer
   ```
3. Add your Apple ID: Xcode > Settings > Accounts > **+** > Apple ID.
4. On the iPhone or iPad: plug it into the Mac and tap **Trust**. Then turn on
   Settings > Privacy & Security > **Developer Mode**. The device restarts.

## Build and install

Make the Xcode project (only needed once, or after deleting `build-ios/`):

```bash
cmake -S . -B build-ios -G Xcode -DCMAKE_SYSTEM_NAME=iOS
```

```bash
open build-ios/CadetPrep.xcodeproj
```

In Xcode:

1. In the toolbar at the top, pick the **cadet_mobile** scheme, then your iPhone or iPad
   as the device.
2. The first time only: click the blue **CadetPrep** project in the left sidebar, then
   the **cadet_mobile** target > **Signing & Capabilities**. Check "Automatically manage
   signing" and pick your name under **Team**. If Xcode says the bundle ID is taken,
   change `com.renzo.cadetprep` to something unique, like `com.yourname.cadetprep`.
3. Press **Run** (the ▶ button). The app installs and opens on the device.
4. The first time only, on the device: Settings > General > VPN & Device Management >
   your Apple ID > **Trust**.

## Updating it, and the 7-day limit

After you change anything (code, questions, cards in `data/`), plug in, open the Xcode
project, and press Run. That installs the new version and also restarts the 7-day clock.
Your progress, streaks, and review list are kept, as long as you don't delete the app.

If you skip a week, the icon stays but the app won't open until you press Run again from
the Mac.

### Without opening Xcode

Plug in the iPhone or iPad and unlock it, then run these from the project folder. Use
your device's identifier from the first command in the last one. The project already
has your team (`-DCADET_TEAM_ID=...` when it was set up).

```bash
xcrun devicectl list devices
```

```bash
xcodebuild -project build-ios/CadetPrep.xcodeproj -scheme cadet_mobile -configuration Release -destination 'generic/platform=iOS' -derivedDataPath build-ios/dd -allowProvisioningUpdates build
```

```bash
xcrun devicectl device install app --device <identifier> build-ios/Release-iphoneos/CadetPrep.app
```

Developer Mode has to stay on for the app to open.

## Progress

The phone or iPad keeps its own progress, separate from the Mac app. Deleting the app
deletes its progress.

## Trying the phone layout on the Mac

You don't need Xcode for this. It opens a phone-sized window:

```bash
cmake -S . -B build-mobile -DCADET_MOBILE=ON
cmake --build build-mobile --target cadet_mobile
./build-mobile/cadet_mobile            # iPhone size
./build-mobile/cadet_mobile --ipad     # iPad held upright
./build-mobile/cadet_mobile --touch    # drag with the mouse to scroll, like a finger
```

This uses the same progress as the Mac app.
