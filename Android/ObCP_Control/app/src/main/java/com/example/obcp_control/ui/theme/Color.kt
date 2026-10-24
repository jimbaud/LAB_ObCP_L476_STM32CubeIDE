package com.example.obcp_control.ui.theme

import androidx.compose.ui.graphics.Color

// New Palette from the chart
val BleuLogo = Color(0xFF1F82C0)
val VertLogo = Color(0xFF09A358)
val Carmin = Color(0xFFB51620)
val Jaune = Color(0xFFFCE500)
val Orange = Color(0xFFF29400)

// Re-using the well-defined black/white from previous theme for backgrounds
val SupMicrotechBlack = Color(0xFF1A171B)
val White = Color(0xFFFFFFFF)
val LightBackground = Color(0xFFFDFBFF)
val DarkOnPrimary = Color(0xFFE0E0E0)
val DarkOnBackground = Color(0xFFE6E1E5)
val DarkError = Color(0xFFCF6679)
val DarkOnError = Color(0xFF000000)

// Light Theme Colors
val md_theme_light_primary = BleuLogo
val md_theme_light_onPrimary = White
val md_theme_light_secondary = VertLogo
val md_theme_light_onSecondary = White
val md_theme_light_error = Carmin
val md_theme_light_onError = White
val md_theme_light_background = LightBackground
val md_theme_light_onBackground = SupMicrotechBlack
val md_theme_light_surface = LightBackground
val md_theme_light_onSurface = SupMicrotechBlack

// Dark Theme Colors
val md_theme_dark_primary = BleuLogo
val md_theme_dark_onPrimary = DarkOnPrimary
val md_theme_dark_secondary = VertLogo
val md_theme_dark_onSecondary = DarkOnPrimary
val md_theme_dark_error = DarkError // using a standard light red for dark theme error
val md_theme_dark_onError = DarkOnError
val md_theme_dark_background = SupMicrotechBlack
val md_theme_dark_onBackground = DarkOnBackground
val md_theme_dark_surface = SupMicrotechBlack
val md_theme_dark_onSurface = DarkOnBackground
