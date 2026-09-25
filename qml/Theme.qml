pragma Singleton
import QtQuick
QtObject {
    id: theme
    // Cohort has no imagery to take a colour from. The scheme comes from the
    // desktop's palette where the desktop publishes one, from the accent the
    // person picked where they picked one, and from a fixed seed otherwise.
    // Every role in the window is read off that one source through Material's
    // tonal palettes (src/m3color.cpp, after MCU's color_spec_2021.ts), so the
    // surfaces carry a trace of the accent's hue and the window reads as one.
    readonly property color defaultSeed: "#b75f38"
    readonly property bool followDesktop: app.theme === "system" && !app.accentChosen && desktopTheme.available
    readonly property color seed: app.accentChosen ? app.accentColor
                                : followDesktop ? desktopTheme.colors.primary : defaultSeed
    readonly property bool dark: followDesktop ? desktopTheme.dark
                               : app.theme === "dark" || (app.theme === "system" && Application.styleHints.colorScheme === Qt.Dark)
    // The desktop's own anchors are used as they are where it gives them, so
    // the window sits in the desktop rather than beside it. MCU fills in the
    // roles the desktop file does not name (src/desktoptheme.cpp).
    readonly property var roles: (app.colorContrast, app.colorVariant,
                                  followDesktop && app.colorVariant === "tonalSpot" && app.colorContrast === 0
                                      ? desktopRoles() : app.colorScheme(seed, dark))
    function desktopRoles() {
        const generated = app.colorScheme(seed, dark)
        const d = desktopTheme.colors
        return Object.assign({}, generated, {
            background: d.background, surface: d.background, surfaceContainerLow: d.surface,
            surfaceContainer: d.container, surfaceContainerHigh: d.high,
            onSurface: d.text, onSurfaceVariant: d.muted, primary: d.primary,
            onPrimary: d.primaryText, primaryContainer: d.primaryContainer,
            onPrimaryContainer: d.containerText, secondary: d.secondary,
            outline: d.outline, outlineVariant: d.outlineVariant
        })
    }
    function role(name, fallback) { const c = roles[name]; return c === undefined ? fallback : c }
    function alpha(c, a) { return Qt.rgba(c.r, c.g, c.b, a) }

    readonly property color background: role("background", dark ? "#181211" : "#fff8f6")
    readonly property color surface: role("surface", dark ? "#181211" : "#fff8f6")
    readonly property color surfaceLow: role("surfaceContainerLow", dark ? "#201a18" : "#fff1ec")
    readonly property color container: role("surfaceContainer", dark ? "#2b2320" : "#f6e5de")
    readonly property color high: role("surfaceContainerHigh", dark ? "#382c28" : "#efddd5")
    readonly property color highest: role("surfaceContainerHighest", dark ? "#433733" : "#e9d8d0")
    readonly property color text: role("onSurface", dark ? "#f5ded5" : "#281912")
    readonly property color muted: role("onSurfaceVariant", dark ? "#d5bfb5" : "#705c53")
    // Material has two outline roles and they do different jobs. The outline is
    // a boundary that has to hold on its own: a switch track, a text field.
    // The variant is decorative separation between things already legible.
    readonly property color outline: role("outline", dark ? "#a08d85" : "#85736b")
    readonly property color outlineVariant: role("outlineVariant", dark ? "#57443b" : "#dcc5b9")
    readonly property color primary: role("primary", dark ? "#ffb596" : "#964829")
    readonly property color primaryText: role("onPrimary", dark ? "#572008" : "#ffffff")
    readonly property color primaryContainer: role("primaryContainer", dark ? "#75351b" : "#ffdbcb")
    readonly property color containerText: role("onPrimaryContainer", dark ? "#ffdbcb" : "#743419")
    readonly property color secondary: role("secondary", dark ? "#d8c4a0" : "#6c5b3b")
    readonly property color secondaryText: role("onSecondary", dark ? "#3b2f15" : "#ffffff")
    readonly property color secondaryContainer: role("secondaryContainer", dark ? "#54432a" : "#f5e0bb")
    readonly property color secondaryContainerText: role("onSecondaryContainer", dark ? "#f5e0bb" : "#221a04")
    readonly property color tertiary: role("tertiary", dark ? "#b8ceb0" : "#3b5236")
    readonly property color tertiaryText: role("onTertiary", dark ? "#243420" : "#ffffff")
    readonly property color tertiaryContainer: role("tertiaryContainer", dark ? "#3b5236" : "#d4eacb")
    readonly property color tertiaryContainerText: role("onTertiaryContainer", dark ? "#d4eacb" : "#233a1f")
    // The inverse roles. A snackbar or a tooltip sits against the theme rather
    // than in it, so it takes the surface and accent the other theme would use.
    readonly property color inverseSurface: role("inverseSurface", dark ? "#f5ded5" : "#3c2c25")
    readonly property color inverseSurfaceText: role("inverseOnSurface", dark ? "#392e2a" : "#ffede6")
    readonly property color inversePrimary: role("inversePrimary", dark ? "#964829" : "#ffb596")
    readonly property color error: role("error", dark ? "#ffb4ab" : "#ba1a1a")
    readonly property color errorText: role("onError", dark ? "#690005" : "#ffffff")
    readonly property color errorContainer: role("errorContainer", dark ? "#93000a" : "#ffdad6")
    readonly property color errorContainerText: role("onErrorContainer", dark ? "#ffdad6" : "#410002")
    // Material's scrim, and the opacity it dims with (ScrimTokens).
    readonly property color scrim: role("scrim", "#000000")
    readonly property real scrimOpacity: 0.32
    function scrimColor(amount) { return alpha(scrim, amount === undefined ? scrimOpacity : amount) }
    // The ring a keyboard leaves around whatever it reached. Material draws it
    // in secondary so it still reads as a ring on something painted in primary.
    readonly property color focusRing: secondary

    // --- State layers ----------------------------------------------------------
    // Material's four state layers, and what it does to a disabled control: the
    // container drops to a tenth or twelfth of onSurface and the content to 38%,
    // rather than the whole control fading together.
    readonly property real hoverOpacity: 0.08
    readonly property real focusOpacity: 0.10
    readonly property real pressedOpacity: 0.10
    readonly property real draggedOpacity: 0.16
    readonly property real disabledContainerOpacity: 0.10
    readonly property real disabledSurfaceOpacity: 0.12
    readonly property real disabledContentOpacity: 0.38

    // --- Spacing ---------------------------------------------------------------
    // Material lays out on a 4dp grid and names the steps it uses.
    readonly property int spaceSmall: 4
    readonly property int space: 8
    readonly property int spaceMedium: 12
    readonly property int spaceLarge: 16
    readonly property int spaceExtraLarge: 24
    // The window's body margins at each width class (Material's layout
    // guidance: 16dp compact, 24dp medium and up).
    function margin(width) { return width < 600 ? 16 : 24 }

    // --- Shape -----------------------------------------------------------------
    // Material's corner scale (ShapeTokens.kt). Components map to a step by how
    // round they should look, not by how big they are.
    readonly property int shapeNone: 0
    readonly property int shapeExtraSmall: 4
    readonly property int shapeSmall: 8
    readonly property int shapeMedium: 12
    readonly property int shapeLarge: 16
    readonly property int shapeLargeIncreased: 20
    readonly property int shapeExtraLarge: 28
    readonly property int shapeExtraLargeIncreased: 32
    readonly property int shapeExtraExtraLarge: 48
    function shapeFull(size) { return size/2 }
    // Nested shapes look unbalanced sharing a radius. Material subtracts the
    // padding between them instead.
    function shapeInside(outer, padding) { return Math.max(0, outer-padding) }

    // --- Elevation -------------------------------------------------------------
    // Material's six levels, and the two shadows each casts: a tight key light
    // at 30% and a wider ambient one at 15%. [offset, blur, spread].
    readonly property var elevationKey: [[0,0,0],[1,2,0],[1,2,0],[1,3,0],[2,3,0],[4,4,0]]
    readonly property var elevationAmbient: [[0,0,0],[1,3,1],[2,6,2],[4,8,3],[6,10,4],[8,12,6]]
    readonly property real elevationKeyOpacity: 0.30
    readonly property real elevationAmbientOpacity: 0.15

    // --- Buttons ---------------------------------------------------------------
    // Material's button sizes. A size carries its own height, its own squarer
    // corner for pressed and selected, its own icon, padding and label style.
    // A precision pointer does not shrink the target: Material asks for 48dp
    // around anything interactive whatever drives the pointer.
    readonly property int minimumTarget: 48
    readonly property var buttonHeights: ({xsmall:32, small:40, medium:56, large:96})
    readonly property var buttonSquare: ({xsmall:shapeMedium, small:shapeMedium, medium:shapeLarge, large:shapeExtraLarge})
    readonly property var buttonIcon: ({xsmall:20, small:20, medium:24, large:32})
    // XSmall..LargeIconButtonTokens keep their container heights everywhere.
    readonly property var iconButtonHeights: ({xsmall:32, small:40, medium:56, large:96})
    readonly property var iconButtonIcon: ({xsmall:20, small:24, medium:24, large:32})
    readonly property var buttonInset: ({xsmall:16, small:16, medium:24, large:48})
    readonly property var buttonGap: ({xsmall:8, small:8, medium:8, large:12})
    // The room either side of an icon button's glyph, [narrow, default, wide].
    readonly property var iconButtonSpace: ({xsmall:[4,6,10], small:[4,8,14], medium:[12,16,24], large:[16,32,48]})
    readonly property var buttonPressed: ({xsmall:shapeSmall, small:shapeSmall, medium:shapeMedium, large:shapeLarge})
    readonly property var buttonOutline: ({xsmall:1, small:1, medium:1, large:2})
    readonly property var buttonLabel: ({xsmall:labelLarge, small:labelLarge, medium:titleMedium, large:headlineSmall})
    readonly property var buttonLabelRole: ({xsmall:"labelLarge", small:"labelLarge", medium:"titleMedium", large:"headlineSmall"})
    // Material's optical centering: content inside an asymmetric shape is
    // nudged by this much of the difference between its two corner radii.
    readonly property real opticalCentering: 0.11
    function opticalShift(startRadius, endRadius) { return opticalCentering*(startRadius-endRadius) }

    // --- Sliders ---------------------------------------------------------------
    // Material's expressive slider: a track you can see, a 4dp handle bar the
    // height of the target, and a 6dp gap held open either side of it
    // (SliderTokens.kt, Slider.kt).
    readonly property var sliderTrack: ({xsmall:16, small:24, medium:40, large:56, xlarge:96})
    readonly property var sliderHandleHeight: ({xsmall:44, small:44, medium:52, large:68, xlarge:108})
    readonly property int sliderHandle: 4
    readonly property int sliderHandlePressed: 2
    readonly property int sliderGap: 6
    readonly property int sliderStop: 4
    readonly property real disabledTrackOpacity: 0.12
    function sliderQuiet(amount) { return alpha(text, amount) }

    // --- Lists -----------------------------------------------------------------
    // ListTokens: one line 56dp, two lines 72dp; 16dp at each end and 12dp
    // between the things in a row. A segmented run of items is set apart by a
    // 2dp gap rather than a rule, with the full corner on the ends of the run
    // and the resting corner inside it.
    readonly property int rowHeight: 56
    readonly property int rowHeightTwoLine: 72
    readonly property int listLeadingSpace: 16
    readonly property int listTrailingSpace: 16
    readonly property int listBetweenSpace: 12
    readonly property int listSegmentedGap: 2
    readonly property int listRest: shapeExtraSmall
    readonly property int listOuter: shapeLarge

    // --- Selection controls ----------------------------------------------------
    readonly property int radioSize: 20
    readonly property int selectionStateLayer: 40
    readonly property int selectionTarget: 48

    // --- Snackbar --------------------------------------------------------------
    readonly property int snackbarHeight: 48
    readonly property int snackbarMaxWidth: 600
    // Material's long duration, which is what a message that explains a refusal
    // needs to be read.
    readonly property int snackbarDuration: 10000

    // --- Motion ----------------------------------------------------------------
    // Material describes motion as springs, published as a damping ratio and a
    // stiffness (MotionSchemeKeyTokens, StandardMotionTokens,
    // ExpressiveMotionTokens). src/m3motion.cpp solves each spring's own step
    // response and fits the curve Qt Quick animates on to it; the duration is
    // the settling time that falls out of the physics. Nothing here is chosen
    // by eye. Spatial springs move things and may ring; effects springs carry
    // colour and opacity and are critically damped so they never overshoot.
    readonly property bool expressiveMotion: app.motionScheme !== "standard"
    readonly property var springs: app.motionSprings(expressiveMotion)
    readonly property var springFastSpatial: springs.fastSpatial.curve
    readonly property var springSpatial: springs.defaultSpatial.curve
    readonly property var springSlowSpatial: springs.slowSpatial.curve
    readonly property var springFastEffects: springs.fastEffects.curve
    readonly property var springEffects: springs.defaultEffects.curve
    readonly property var springSlowEffects: springs.slowEffects.curve
    readonly property int springFastSpatialMs: app.motion ? springs.fastSpatial.ms : 0
    readonly property int springSpatialMs: app.motion ? springs.defaultSpatial.ms : 0
    readonly property int springSlowSpatialMs: app.motion ? springs.slowSpatial.ms : 0
    readonly property int springFastEffectsMs: app.motion ? springs.fastEffects.ms : 0
    readonly property int springEffectsMs: app.motion ? springs.defaultEffects.ms : 0
    readonly property int springSlowEffectsMs: app.motion ? springs.slowEffects.ms : 0
    // The names the components use, resolved through the springs above so the
    // motion setting reaches every animation at once.
    readonly property int fast: springFastEffectsMs
    readonly property int normal: springEffectsMs
    readonly property int slow: springSlowEffectsMs
    readonly property int enterDuration: springFastEffectsMs
    // Menu.kt and Tooltip.kt close on FastEffects too: opacity must settle
    // without passing through a wrong value.
    readonly property int exitDuration: springFastEffectsMs
    readonly property var enterCurve: springFastEffects
    readonly property var exitCurve: springFastEffects
    readonly property var fastSpatialCurve: springFastSpatial
    readonly property var effectsCurve: springEffects
    readonly property var fastEffectsCurve: springFastEffects
    readonly property var curve: springSpatial

    // --- Typography ------------------------------------------------------------
    // The desktop's own font. Cohort bundles none: the family is whatever the
    // system resolves sans-serif to, which main.cpp hands the application.
    // Material's type scale is set in Google Sans Flex, and on a system where
    // that is the default font the emphasized roles use its real weight and
    // width axes; elsewhere the axes are ignored and the weights apply.
    readonly property string fontFamily: Qt.application.font.family
    readonly property int emphasizedWidth: 110
    readonly property int regularWidth: 100
    readonly property int displayLarge: 57
    readonly property int displayMedium: 45
    readonly property int displaySmall: 36
    readonly property int headlineLarge: 32
    readonly property int headlineMedium: 28
    readonly property int headlineSmall: 24
    readonly property int titleLarge: 22
    readonly property int titleMedium: 16
    readonly property int titleSmall: 14
    readonly property int bodyLarge: 16
    readonly property int bodyMedium: 14
    readonly property int bodySmall: 12
    readonly property int labelLarge: 14
    readonly property int labelMedium: 12
    readonly property int labelSmall: 11
    // [size, line height, tracking, emphasized tracking, weight, emphasized
    // weight] for each of the fifteen roles, from TypeScaleTokens.kt with the
    // weights of TypefaceTokens.kt. Emphasis is a role of its own rather than
    // a weight laid over one: the tracking moves with it, so body large
    // tightens from 0.5 to 0.15 while body medium opens from 0.2 to 0.25.
    readonly property var typeScale: ({
        displayLarge:  [57, 64, -0.2, 0.0,  Font.Normal, Font.Medium],
        displayMedium: [45, 52,  0.0, 0.0,  Font.Normal, Font.Medium],
        displaySmall:  [36, 44,  0.0, 0.0,  Font.Normal, Font.Medium],
        headlineLarge: [32, 40,  0.0, 0.0,  Font.Normal, Font.Medium],
        headlineMedium:[28, 36,  0.0, 0.0,  Font.Normal, Font.Medium],
        headlineSmall: [24, 32,  0.0, 0.0,  Font.Normal, Font.Medium],
        titleLarge:    [22, 28,  0.0, 0.0,  Font.Normal, Font.Medium],
        titleMedium:   [16, 24,  0.2, 0.15, Font.Medium, Font.Bold],
        titleSmall:    [14, 20,  0.1, 0.1,  Font.Medium, Font.Bold],
        bodyLarge:     [16, 24,  0.5, 0.15, Font.Normal, Font.Medium],
        bodyMedium:    [14, 20,  0.2, 0.25, Font.Normal, Font.Medium],
        bodySmall:     [12, 16,  0.4, 0.4,  Font.Normal, Font.Medium],
        labelLarge:    [14, 20,  0.1, 0.1,  Font.Medium, Font.Bold],
        labelMedium:   [12, 16,  0.5, 0.5,  Font.Medium, Font.Bold],
        labelSmall:    [11, 16,  0.5, 0.5,  Font.Medium, Font.Bold]
    })
    // A size alone does not name a role: 16 is both title medium and body
    // large. Whether the text is a label settles the ones that matter, and a
    // style can name its role outright.
    function typeRole(size, label, named) {
        if (named && typeScale[named]) return named
        if (label) return size <= 11 ? "labelSmall" : size <= 12 ? "labelMedium" : "labelLarge"
        if (size >= 51) return "displayLarge"
        if (size >= 40) return "displayMedium"
        if (size >= 34) return "displaySmall"
        if (size >= 30) return "headlineLarge"
        if (size >= 26) return "headlineMedium"
        if (size >= 23) return "headlineSmall"
        if (size >= 20) return "titleLarge"
        if (size >= 15) return "bodyLarge"
        if (size >= 13) return "bodyMedium"
        return "bodySmall"
    }
    function weightFor(emphasized, label, size, named) {
        const r = typeScale[typeRole(size, label, named)]
        return emphasized ? r[5] : r[4]
    }
    function lineFor(size, label, named) {
        const r = typeScale[typeRole(size, label, named)]
        return Math.round(size*(r[1]/r[0]))
    }
    function trackingFor(size, label, named, emphasized) {
        return typeScale[typeRole(size, label, named)][emphasized ? 3 : 2]
    }
}
