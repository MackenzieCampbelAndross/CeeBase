---
version: alpha
name: CeeBase
description: CeeBase's design language — an open-source database and backend platform built on an onyx-black foundation, paper-white typography, and a signature dark-azure primary. The brand pairs a restrained technical interface with a distinctive azure gradient, charcoal surfaces, and light-azure supporting tones.

colors:
  primary: "#00638E"
  primary-deep: "#004A6B"
  primary-soft: "#8CB9CC"
  ink: "#FFFFFF"
  ink-secondary: "#BFD8E3"
  ink-mute: "#8CB9CC"
  ink-mute-2: "#BFD8E3"
  ink-faint: "#8CB9CC"
  on-primary: "#FFFFFF"
  on-dark: "#FFFFFF"
  canvas: "#000000"
  canvas-soft: "#141414"
  canvas-night: "#000000"
  canvas-night-soft: "#141414"
  hairline: "#2B2B2B"
  hairline-strong: "#BFD8E3"
  hairline-cool: "#2B2B2B"
  hairline-cool-2: "#141414"
  hairline-cool-3: "#2B2B2B"
  azure-gradient-start: "#000000"
  azure-gradient-mid: "#004A6B"
  azure-gradient-end: "#00638E"
  azure-gradient-highlight: "#8CB9CC"
  onyx: "#000000"
  charcoal: "#2B2B2B"
  light-azure: "#BFD8E3"

typography:
  display-xxl:
    fontFamily: "Circular, 'Helvetica Neue', Helvetica, Arial, sans-serif"
    fontSize: 64px
    fontWeight: 500
    lineHeight: 1.1
    letterSpacing: -1.92px
  display-xl:
    fontFamily: "Circular, 'Helvetica Neue', Helvetica, Arial, sans-serif"
    fontSize: 48px
    fontWeight: 500
    lineHeight: 1.1
    letterSpacing: -1.44px
  display-lg:
    fontFamily: "Circular, 'Helvetica Neue', Helvetica, Arial, sans-serif"
    fontSize: 36px
    fontWeight: 500
    lineHeight: 1.15
    letterSpacing: -0.72px
  display-md:
    fontFamily: "Circular, 'Helvetica Neue', Helvetica, Arial, sans-serif"
    fontSize: 28px
    fontWeight: 500
    lineHeight: 1.2
    letterSpacing: -0.42px
  heading-lg:
    fontFamily: "Circular, 'Helvetica Neue', Helvetica, Arial, sans-serif"
    fontSize: 22px
    fontWeight: 500
    lineHeight: 1.2
    letterSpacing: 0
  heading-md:
    fontFamily: "Circular, 'Helvetica Neue', Helvetica, Arial, sans-serif"
    fontSize: 18px
    fontWeight: 500
    lineHeight: 1.4
    letterSpacing: 0
  body-lg:
    fontFamily: "Circular, 'Helvetica Neue', Helvetica, Arial, sans-serif"
    fontSize: 18px
    fontWeight: 400
    lineHeight: 1.55
    letterSpacing: 0
  body-md:
    fontFamily: "Circular, 'Helvetica Neue', Helvetica, Arial, sans-serif"
    fontSize: 16px
    fontWeight: 400
    lineHeight: 1.5
    letterSpacing: 0
  button-md:
    fontFamily: "Circular, 'Helvetica Neue', Helvetica, Arial, sans-serif"
    fontSize: 14px
    fontWeight: 500
    lineHeight: 1.0
    letterSpacing: 0
  caption:
    fontFamily: "Circular, 'Helvetica Neue', Helvetica, Arial, sans-serif"
    fontSize: 13px
    fontWeight: 400
    lineHeight: 1.45
    letterSpacing: 0
  micro:
    fontFamily: "Circular, 'Helvetica Neue', Helvetica, Arial, sans-serif"
    fontSize: 12px
    fontWeight: 400
    lineHeight: 1.45
    letterSpacing: 0
  code:
    fontFamily: "ui-monospace, Menlo, Monaco, Consolas, 'Liberation Mono', monospace"
    fontSize: 14px
    fontWeight: 400
    lineHeight: 1.5
    letterSpacing: 0

rounded:
  xs: 4px
  sm: 6px
  md: 8px
  lg: 12px
  xl: 16px
  full: 9999px

spacing:
  xxs: 2px
  xs: 4px
  sm: 8px
  md: 12px
  lg: 16px
  xl: 24px
  xxl: 32px
  huge: 64px

components:
  button-primary-azure:
    backgroundColor: "{colors.primary}"
    textColor: "{colors.on-primary}"
    typography: "{typography.button-md}"
    rounded: "{rounded.sm}"
    padding: 8px 16px
  button-primary-azure-pressed:
    backgroundColor: "{colors.primary-deep}"
    textColor: "{colors.on-primary}"
    typography: "{typography.button-md}"
    rounded: "{rounded.sm}"
    padding: 8px 16px
  button-secondary-outline:
    backgroundColor: "{colors.canvas}"
    textColor: "{colors.ink}"
    typography: "{typography.button-md}"
    rounded: "{rounded.sm}"
    padding: 8px 16px
  button-on-dark:
    backgroundColor: "{colors.canvas-night}"
    textColor: "{colors.on-dark}"
    typography: "{typography.button-md}"
    rounded: "{rounded.sm}"
    padding: 8px 16px
  button-link:
    backgroundColor: "{colors.canvas}"
    textColor: "{colors.ink}"
    typography: "{typography.button-md}"
    rounded: "{rounded.xs}"
    padding: 0px
  text-input:
    backgroundColor: "{colors.canvas}"
    textColor: "{colors.ink}"
    typography: "{typography.body-md}"
    rounded: "{rounded.sm}"
    padding: 8px 12px
  card-feature-light:
    backgroundColor: "{colors.canvas}"
    textColor: "{colors.ink}"
    typography: "{typography.body-md}"
    rounded: "{rounded.lg}"
    padding: 32px
  card-pricing:
    backgroundColor: "{colors.canvas}"
    textColor: "{colors.ink}"
    typography: "{typography.body-md}"
    rounded: "{rounded.lg}"
    padding: 32px
  card-pricing-featured:
    backgroundColor: "{colors.canvas-night}"
    textColor: "{colors.on-dark}"
    typography: "{typography.body-md}"
    rounded: "{rounded.lg}"
    padding: 32px
  card-feature-dark:
    backgroundColor: "{colors.canvas-night}"
    textColor: "{colors.on-dark}"
    typography: "{typography.body-md}"
    rounded: "{rounded.lg}"
    padding: 32px
  code-block:
    backgroundColor: "{colors.canvas-night}"
    textColor: "{colors.on-dark}"
    typography: "{typography.code}"
    rounded: "{rounded.sm}"
    padding: 16px
  pill-tag-azure:
    backgroundColor: "{colors.primary}"
    textColor: "{colors.on-primary}"
    typography: "{typography.micro}"
    rounded: "{rounded.full}"
    padding: 2px 8px
  pill-tag-soft:
    backgroundColor: "{colors.canvas-soft}"
    textColor: "{colors.ink}"
    typography: "{typography.micro}"
    rounded: "{rounded.full}"
    padding: 2px 8px
  nav-bar-light:
    backgroundColor: "{colors.canvas}"
    textColor: "{colors.ink}"
    typography: "{typography.body-md}"
    rounded: "{rounded.xs}"
    padding: 16px 24px
  link-on-light:
    backgroundColor: "{colors.canvas}"
    textColor: "{colors.ink}"
    typography: "{typography.body-md}"
    rounded: "{rounded.xs}"
    padding: 0px
  footer-light:
    backgroundColor: "{colors.canvas}"
    textColor: "{colors.ink-mute}"
    typography: "{typography.caption}"
    rounded: "{rounded.xs}"
    padding: 64px 24px
---
<!-- MIT licensed. Copyright and license: https://opendesignmd.com/licenses/ -->

# How CeeBase is designed

## Overview

CeeBase's design language is engineered for clarity and technical confidence. The interface is grounded in an **Onyx** canvas (`{colors.canvas}` — `#000000`) with **Paper White** typography (`{colors.ink}` — `#FFFFFF`). **Dark Azure** (`{colors.primary}` — `#00638E`) is the signature brand color, supported by **Deep Azure** (`{colors.primary-deep}` — `#004A6B`) and **Light Azure** (`{colors.primary-soft}` — `#8CB9CC`). Charcoal (`{colors.charcoal}` — `#2B2B2B`) defines elevated surfaces and borders, while the four-stop azure gradient moves from Onyx through Deep Azure and Dark Azure to Light Azure.

Typography runs **Circular** at weight 500 for display and 400 for body. The display tier uses tight negative letter-spacing (-1.92px at 64px) to pull the rounded humanist letterforms into editorial density. The brand embraces a dark-canvas system. Its signature gradient is reserved for deliberate brand moments rather than applied indiscriminately; product surfaces remain restrained and functional.

The product itself appears as composited UI screenshots on every page: dashboard tables, SQL editors, query builders, log streams. These screenshots are the brand's argument. They sit inside `{rounded.lg}` 12px containers with subtle 1px hairlines, often arranged 2-up or in a floating "stacked panes" composition above the hero band.

**Key Characteristics:**
- Dark Azure (`{colors.primary}` `#00638E`) is the signature primary, supported by Deep Azure, Light Azure, Onyx, Paper White, and Charcoal.
- Onyx canvas with Paper White text and charcoal surface hierarchy.
- Custom humanist sans display tier at weight 500 with negative letter-spacing of -1.92px to -0.42px.
- Composited product UI screenshots (dashboard, SQL editor, log stream) are the dominant decorative element — never photography, never illustrations.
- Tight 6px / 8px button radii — square-ish, technical, never pill-shaped.
- Code blocks rendered in Onyx `{colors.canvas-night}` (`#000000`) with monospace inline code; the brand's developer DNA is visible in every snippet.
- Pricing tiers use the same dark foundation with azure accents to distinguish primary actions and featured states.

## Colors

### Brand & Accent
- **Dark Azure** (`{colors.primary}` — `#00638E`): The signature brand and CTA color. Use for primary buttons, active states, key indicators, and brand marks.
- **Deep Azure** (`{colors.primary-deep}` — `#004A6B`): Pressed state and the second stop in the signature gradient.
- **Light Azure** (`{colors.primary-soft}` — `#8CB9CC`): Highlight tone for gradient transitions, subtle emphasis, and supporting UI details.
- **Azure Gradient**: Four stops — Onyx (`#000000`), Deep Azure (`#004A6B`), Dark Azure (`#00638E`), and Light Azure (`#8CB9CC`). Use selectively for brand surfaces and prominent visual moments.

### Surface
- **Onyx** (`{colors.canvas}` — `#000000`): Default page and application background.
- **Onyx Raised** (`{colors.canvas-soft}` — `#141414`): Elevated panels, menus, and nested surfaces.
- **Charcoal** (`{colors.charcoal}` — `#2B2B2B`): Card surfaces, dividers, borders, and secondary containers.
- **Paper White** (`{colors.ink}` — `#FFFFFF`): Primary text and high contrast foreground elements.
- **Light Azure** (`{colors.light-azure}` — `#BFD8E3`): Secondary text, muted labels, and cool-toned supporting details.
- **Hairlines** (`{colors.hairline}`): Subtle boundaries between dark surfaces; use sparingly to preserve visual clarity.

### Text
- **Paper White** (`{colors.ink}` — `#FFFFFF`): Default body text and high contrast foreground.
- **Light Azure** (`{colors.ink-secondary}` — `#BFD8E3`): Secondary text and body emphasis.
- **Muted Azure** (`{colors.ink-mute}` — `#8CB9CC`): Secondary text and helper copy.
- **Light Azure** (`{colors.ink-mute-2}` — `#BFD8E3`): Tertiary text.
- **Muted Light Azure** (`{colors.ink-faint}` — `#8CB9CC`): Disabled / placeholder text.
- **On Primary** (`{colors.on-primary}` — `#FFFFFF`): Paper White text on Dark Azure for strong contrast.
- **On Dark** (`{colors.on-dark}` — `#FFFFFF`): Paper White text on Onyx surfaces.

## Typography

### Font Family

The display and UI tier is **Circular** — a proprietary geometric humanist sans by Lineto. Fallback chain: `'Helvetica Neue', Helvetica, Arial`.

For maximum brand fidelity when Circular isn't licensed, use **Inter** (open-source via Google Fonts) at weight 500 for display with `letter-spacing: -1.92px` at 64px. Inter is the closest open-source analogue to Circular's geometric humanist character.

Code blocks use **system mono** (`ui-monospace`, with Menlo / Monaco / Consolas fallbacks).

### Hierarchy

| Token | Size | Weight | Line Height | Letter Spacing | Use |
|---|---|---|---|---|---|
| `{typography.display-xxl}` | 64px | 500 | 1.1 | -1.92px | Hero headline |
| `{typography.display-xl}` | 48px | 500 | 1.1 | -1.44px | Section opener |
| `{typography.display-lg}` | 36px | 500 | 1.15 | -0.72px | Sub-section / pricing tier |
| `{typography.display-md}` | 28px | 500 | 1.2 | -0.42px | Card title |
| `{typography.heading-lg}` | 22px | 500 | 1.2 | 0 | Compact heading |
| `{typography.heading-md}` | 18px | 500 | 1.4 | 0 | Section sub-heading |
| `{typography.body-lg}` | 18px | 400 | 1.55 | 0 | Marketing body lead |
| `{typography.body-md}` | 16px | 400 | 1.5 | 0 | Default UI body |
| `{typography.button-md}` | 14px | 500 | 1.0 | 0 | Button label |
| `{typography.caption}` | 13px | 400 | 1.45 | 0 | Helper, footnote |
| `{typography.micro}` | 12px | 400 | 1.45 | 0 | Pill label, fine print |
| `{typography.code}` | 14px | 400 | 1.5 | 0 | Code block content |

### Principles
- **Weight 500 across display.** Mid-weight reads as engineered, not decorative.
- **Negative tracking on display.** -1.92px at 64px scaling proportionally down — tightens the rounded humanist letterforms into editorial density.
- **Mono for code.** System mono families (Menlo / Monaco) — no proprietary mono webfont.

### Note on Font Substitutes
Circular is proprietary. Use **Inter** at weight 500 with `letter-spacing: -1.92px` for display tiers. **Geist Sans** (open-source from Vercel) is another close alternative for both display and body. Avoid Helvetica defaults — they're heavier and lack the geometric warmth.

## Layout

### Spacing System
- **Base unit**: 8px (with 2 / 4 / 12 sub-tokens for fine work).
- **Tokens**: `{spacing.xxs}` 2px · `{spacing.xs}` 4px · `{spacing.sm}` 8px · `{spacing.md}` 12px · `{spacing.lg}` 16px · `{spacing.xl}` 24px · `{spacing.xxl}` 32px · `{spacing.huge}` 64px.
- **Section padding**: 64–96px on marketing surfaces.
- **Card internal padding**: 32px on feature/pricing cards.

### Grid & Container
- Marketing pages center in a ~1280px container with no edge-bleed; the brand keeps content inside the box.
- Pricing collapses 4-up → 2-up → 1-up at 1024 / 768 breakpoints.
- Product UI mockups stack 2-up or render as overlapping panes inside the same container.

### Whitespace Philosophy
The brand uses generous 64–96px section padding without atmospheric gradients filling the space — the onyx canvas is the design. The composited product UI mockups break up sections without requiring decoration.

## Elevation & Depth

| Level | Treatment | Use |
|---|---|---|
| 0 | Flat, 1px hairline | Default cards |
| 1 | `box-shadow: 0 1px 3px rgba(0,0,0,0.06)` | Subtle card lift |
| 2 | `box-shadow: 0 8px 24px rgba(0,0,0,0.08)` | Floating composited UI mockups |
| 3 | `box-shadow: 0 16px 48px rgba(0,0,0,0.12)` | Modal overlays, deep elevation |

### Decorative Depth
The brand's depth is **product UI mockups** rather than gradients. Stacked dashboard / SQL editor / log panes composite together with subtle Level 2 shadows to suggest spatial hierarchy.

## Shapes

### Border Radius Scale

| Token | Value | Use |
|---|---|---|
| `{rounded.xs}` | 4px | Form inputs, hairline tags |
| `{rounded.sm}` | 6px | Buttons (the brand's signature button radius), code blocks |
| `{rounded.md}` | 8px | Compact cards, alerts |
| `{rounded.lg}` | 12px | Pricing cards, feature cards, product mockups |
| `{rounded.xl}` | 16px | Modal dialogs, large container chrome |
| `{rounded.full}` | 9999px | Pill tags, avatars |

### Photography Geometry
The brand uses minimal photography. Customer logo strips display wordmarks at uniform height (~24–32px) in greyscale; case-study cards (rare) use 4:3 photos inset in `{rounded.lg}` containers.

## Components

### Buttons

**`button-primary-azure`** — the signature CTA.
- Background `{colors.primary}`, text `{colors.on-primary}` (Paper White for strong contrast), type `{typography.button-md}`, padding `{spacing.sm} {spacing.lg}` (8px 16px), rounded `{rounded.sm}` 6px.
- Pressed state `button-primary-azure-pressed` shifts to `{colors.primary-deep}`.

**`button-secondary-outline`** — outline alternative on onyx.
- Background `{colors.canvas}`, text `{colors.ink}`, 1px solid `{colors.hairline-strong}` border, same shape.

**`button-on-dark`** — used on dark surfaces / code-block CTAs.
- Background `{colors.canvas-night}`, text `{colors.on-dark}`, same shape.

**`button-link`** — text-only inline button.
- Transparent background, text `{colors.ink}` rendered in `{typography.button-md}`, no padding, with a subtle underline on hover.

### Cards & Containers

**`card-feature-light`** — feature card on onyx.
- Background `{colors.canvas}`, padding `{spacing.xxl}`, rounded `{rounded.lg}` 12px, 1px `{colors.hairline}` border.

**`card-pricing`** — standard pricing tier on the Onyx foundation.
- Background `{colors.canvas}`, padding `{spacing.xxl}`, rounded `{rounded.lg}`, 1px `{colors.hairline}` border. Title in `{typography.heading-lg}`, price in `{typography.display-md}`, body in `{typography.body-md}`, CTA `button-primary-azure` pinned bottom.

**`card-pricing-featured`** — inverted dark featured tier.
- Background `{colors.canvas-night}`, text `{colors.on-dark}`, otherwise identical structure.

**`card-feature-dark`** — feature card with deep dark fill.
- Background `{colors.canvas-night}`, text `{colors.on-dark}`, padding `{spacing.xxl}`, rounded `{rounded.lg}`. Used for code-heavy feature explanations.

**`code-block`** — code snippet container.
- Background `{colors.canvas-night}`, text `{colors.on-dark}` rendered in `{typography.code}`. Padding `{spacing.lg}` 16px, rounded `{rounded.sm}` 6px.

### Inputs & Forms

**`text-input`** — standard form input.
- Background `{colors.canvas}`, text `{colors.ink}`, type `{typography.body-md}`, padding `{spacing.sm} {spacing.md}` (8px 12px), rounded `{rounded.sm}` 6px, 1px `{colors.hairline}` border.

### Navigation

**`nav-bar-light`** — top nav across the application.
- Background `{colors.canvas}`, text `{colors.ink}`, padding `{spacing.lg} {spacing.xl}`. Logo on the left, primary nav center, "Sign In" link + filled `button-primary-azure` on the right.

### Pills, Tags, and Chips

**`pill-tag-azure`** — small azure pill used for "new" or featured indicators.
- Background `{colors.primary}`, text `{colors.on-primary}`, type `{typography.micro}`, padding `{spacing.xxs} {spacing.sm}`, rounded `{rounded.full}`.

**`pill-tag-soft`** — neutral pill on light surfaces.
- Background `{colors.canvas-soft}`, text `{colors.ink}`, otherwise same shape.

### Signature Components

**Composited Product UI Mockups** — multi-layer dashboard / SQL editor / log pane composites with subtle Level 2 shadows. The product is the brand's argument; mockups always sit on onyx canvas with no surrounding decoration.

**`link-on-light`** — inline links in body copy.
- Text `{colors.ink}` rendered in `{typography.body-md}` with a persistent underline.

**`footer-light`** — site-wide footer.
- Background `{colors.canvas}`, text `{colors.ink-mute}`, type `{typography.caption}`, padding `{spacing.huge} {spacing.xl}` (64px 24px). Holds 4–5 columns of link groups, social icons, and a small legal row.

## Do's and Don'ts

### Do
- Reserve `{colors.primary}` azure for filled CTAs and the wordmark accent — it should appear sparingly.
- Render display tiers at weight 500 with negative letter-spacing — the engineered tightness is part of the brand.
- Use `{rounded.sm}` 6px for buttons — square-ish radii, never pill-shaped.
- Composite product UI mockups inside `{rounded.lg}` containers with subtle Level 2 shadows.
- Use Paper White `{colors.on-primary}` on Dark Azure buttons for strong contrast.
- Apply system mono for every code block.

### Don't
- Don't introduce colors outside the defined CeeBase palette as system colors.
- Don't bump display weight above 500 — the brand's calibrated mid-weight breaks at 600+.
- Don't use pill-shaped buttons; the brand's button radius is square-ish 6px.
- Don't use near-black text on Dark Azure buttons; use Paper White for contrast.
- Don't add atmospheric gradients to hero bands — the onyx canvas is the design.

## Responsive Behavior

### Breakpoints

| Name | Width | Key Changes |
|---|---|---|
| Wide | ≥ 1440px | Full container width; product mockups at full scale |
| Desktop | 1024–1440px | Default content max-width; pricing 4-up |
| Tablet | 768–1023px | Pricing 2-up; mockups simplify to single panel |
| Mobile | < 768px | Pricing 1-up; hamburger nav; display drops 64 → 36px |

### Touch Targets
- Buttons hit ≥ 36×36px on mobile; vertical padding scales up to maintain WCAG AA minimum.
- Form fields stay at 36px minimum height.

### Collapsing Strategy
- Display tiers stair-step 64 → 48 → 36 → 28 → 22px.
- Product UI mockups simplify to a single primary panel on mobile.
- Pricing tiers stair-step 4-up → 2-up → 1-up; dark featured tier always distinguished.

### Image Behavior
Product UI mockups use `srcset` with desktop / mobile crops; mobile crops focus on the most actionable inner panel.

## Iteration Guide

1. Focus on ONE component at a time.
2. Reference component names and tokens directly.
3. Run `npx @google/design.md lint DESIGN.md` after edits.
4. Default body to `{typography.body-md}`; use `{typography.code}` for any developer-facing snippet.
5. Use Dark Azure deliberately for primary actions; reserve the azure gradient for prominent brand moments.
6. The Onyx foundation and restrained azure gradient define the brand; avoid unrelated atmospheric effects.