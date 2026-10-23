# Discoverability and launch checklist — Knack II PC Port

This checklist accompanies the documentation-only discoverability PR. **Do not describe the alpha as fully playable** or claim complete campaign certification. Keep all download links pointed at the official GitHub release; do not mirror or bundle game content.

## 1. GitHub repository metadata (manual settings)

Open the repository page and click **About → gear icon** (or repository settings where applicable). Set:

- **Description:** `Unofficial Knack II (Knack 2) PC port for Windows. Experimental Vulkan alpha, launcher and local co-op; requires your own PS4 game dump.`
- **Topics:** `knack-ii`, `knack-2`, `pc-port`, `windows`, `vulkan`, `ps4`, `local-coop`, `fan-project`
- **Website:** `https://hyant4.github.io/Knack-II-pc-port/` **only after Pages deployment works**.

The repository currently has no description, topics, or GitHub Pages site. These account-level settings are not changed by merging this PR.

## 2. Publish GitHub Pages (manual setting after merging this PR)

1. Open **Settings → Pages** for this repository.
2. Under **Build and deployment**, select **Deploy from a branch**.
3. Select branch **main** and folder **/docs**, then save.
4. Verify the new page loads at **https://hyant4.github.io/Knack-II-pc-port/** and both screenshots load.
5. Set the GitHub repository **Website** field to that URL.

The page uses plain static HTML at `docs/index.html`, with images already tracked in `docs/screenshots/`. There is no separate publishing workflow, build tool, analytics script, or third-party tracking.

## 3. Google Search Console

After the Pages URL is live, add the **URL-prefix property** `https://hyant4.github.io/Knack-II-pc-port/` to [Google Search Console](https://search.google.com/search-console). Follow Google's available ownership-verification instructions. Then:

- Inspect the homepage URL and request indexing if eligible.
- Submit `https://hyant4.github.io/Knack-II-pc-port/sitemap.xml` if verification succeeds.
- Confirm the indexed URL and monitor coverage/impressions over time.

Indexing and search ranking are **not guaranteed**. GitHub Pages being enabled does not itself establish Search Console ownership; do not claim that verification is automatic.

## 4. Community announcement

Publish one accurate, non-spammy announcement with:
- Title: **Knack II (Knack 2) PC port — experimental Windows alpha v0.10 released**
- Official release and source links, two genuine screenshots, and ideally a short capture of the tested early section.
- The requirement for the user's own CUSA08014 v01.01 dump.
- Up-front disclosure that early gameplay and co-op have been tested, but the full campaign has not.

Respect each community's self-promotion rules and clearly label the port as unofficial. Do not post duplicate promotional links into unrelated discussions or promise compatibility not supported by tests.

## 5. After updates

When a newer public release exists, update **both** README files where needed, `docs/index.html`, and the homepage structured data together. Check that linked release assets, game-version requirements, and status statements still match the new release notes. Keep the legal notice and third-party credits.

## 6. Acceptance checks

- Only documentation / site files change. No runtime, saves, licensing, executable assets, submodules, or build workflows modified.
- All links point at real repository paths or the recorded GitHub alpha v0.10 release.
- English and Portuguese content agree on requirements and known limitations.
- The Pages URL is a **planned URL**, not a claim of an already deployed site.
- No claims of complete campaign playability, official Sony affiliation, or game download included.
