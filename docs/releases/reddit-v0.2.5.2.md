# Reddit post: v0.2.5.2

## Title (pick one)
- Venture: Unknown Source Edition, a Venture fork for running dev builds and non-Meta-store apps on PC VR
- I forked Venture to add an opt-in dev identity for testing your own Quest apps on PC

## Body

I made a small fork of [Venture](https://github.com/Villagers654/Venture), Villagers654's free, open source Quest emulator for PC VR, and I want to credit him up front: everything that makes it work is his. I only added one feature.

**The problem:** Venture signs in with Meta and checks you own each game. That's right for store games, but it means you can't test your *own* build, or run an app from another store, unless it's listed on the Meta store.

**The fix:** an opt-in **dev identity**, per app and off by default. With it on, the app starts as a local "Developer" user, with no Meta account and no ownership check. Apps you don't opt in behave exactly as in upstream Venture.

- Press Play on a game whose package has a `dev` segment (e.g. `com.dev.studio.game`) and it asks once. Otherwise: game menu → *Use dev identity*.
- CLI: `venture-cli play GAME --dev-identity`
- A DEV IDENTITY badge shows while it's on.
- The updater follows the fork's releases.

**Please read:** it's for developers and for software that isn't sold through the Meta store. It isn't meant for, and isn't supported for, running store games you haven't bought. There's a disclaimer in the README.

**Honest status:** it's new and lightly tested. Dev apps get canned platform answers (no friends/multiplayer, empty leaderboards and purchases). I'd love bug reports from other developers.

Download (Windows installer and zip, Linux packages): https://github.com/DeliciousMeatPop/Venture-UnknownSourceEdition/releases
Upstream project and wiki: https://github.com/Villagers654/Venture

## Before you post
- Read each subreddit's rules on self-promotion and flair. Some limit posts about emulators or modified clients.
- Post it where it fits as a **developer tool**, and don't tag or frame it as a way to get free games. That framing is inaccurate and tends to get posts removed.
- If a sub requires AI-use disclosure, follow it.
- Replace the title with whichever you pick, and answer comments by pointing to the upstream wiki for general Venture questions.
