#include "ui_shell.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <vector>

#include "../assets/zeta_k230_logo.h"

namespace reconclave::ui {

namespace {
struct TileSize {
  lv_coord_t w;
  lv_coord_t h;
};

struct StatusRefs {
  lv_obj_t* clock;
  lv_obj_t* battery;
  lv_obj_t* dot;
};

std::vector<StatusRefs> g_status_bars;
lv_group_t* g_nav_group = nullptr;

// This app never applies an LVGL theme (no lv_theme_default_init()/
// lv_display_set_theme() call anywhere) - every visual is this app's own
// explicit style, so there is no default focus indicator to fall back on.
// Applied with the LV_STATE_FOCUSED selector below, which LVGL toggles on
// an object automatically as group focus moves onto/off of it - no manual
// add/remove synchronization needed, this style just activates itself.
lv_style_t g_focus_style;
bool g_focus_style_ready = false;

void ensureFocusStyle() {
  if (g_focus_style_ready) return;
  lv_style_init(&g_focus_style);
  lv_style_set_outline_width(&g_focus_style, 3);
  lv_style_set_outline_color(&g_focus_style, lv_color_hex(kColorAccent));
  lv_style_set_outline_opa(&g_focus_style, LV_OPA_COVER);
  lv_style_set_outline_pad(&g_focus_style, 2);
  g_focus_style_ready = true;
}

// Recursively adds every genuinely-interactive descendant of `obj` to
// `group`, so a screen's tiles/buttons/fields become keyboard-navigable
// regardless of how deeply they're nested in layout containers.
//
// Deliberately checks widget *class*, not LV_OBJ_FLAG_CLICK_FOCUSABLE:
// lv_obj's base constructor sets that flag on every object by default
// (lv_obj.c), and lv_label only clears LV_OBJ_FLAG_CLICKABLE, not
// CLICK_FOCUSABLE - so a flag check adds every decorative label/icon
// inside a card (title, detail, badge, the "01"/"02" index, the symbol
// glyph) as its own navigation stop alongside the real button, and every
// plain lv_obj_create() layout wrapper (the card itself, row/column
// containers, ...) too, since those never had the flag cleared either.
// Confirmed on real hardware as the reason arrow-key focus wasn't visibly
// landing on anything sensible even after group membership was correctly
// scoped to the active screen - it was moving, just between dozens of
// non-visual targets. lv_obj_has_class() (not lv_obj_check_type(), which
// is an exact match only) also picks up lv_list's row buttons, since
// lv_list_button_class's base_class is lv_button_class.
bool isNavigable(lv_obj_t* obj) {
  return lv_obj_has_class(obj, &lv_button_class) ||
         lv_obj_has_class(obj, &lv_textarea_class) ||
         lv_obj_has_class(obj, &lv_slider_class);
}

void addFocusableDescendants(lv_group_t* group, lv_obj_t* obj) {
  const std::uint32_t count = lv_obj_get_child_count(obj);
  for (std::uint32_t i = 0; i < count; ++i) {
    lv_obj_t* child = lv_obj_get_child(obj, static_cast<std::int32_t>(i));
    if (isNavigable(child)) {
      lv_group_add_obj(group, child);
      ensureFocusStyle();
      lv_obj_add_style(child, &g_focus_style, LV_STATE_FOCUSED);
    }
    addFocusableDescendants(group, child);
  }
}

void onScreenLoaded(lv_event_t* event) {
  if (g_nav_group == nullptr) return;
  // Clears the *group's* membership list, not the screens or widgets
  // themselves - safe to call on every navigation, and is what actually
  // confines arrow-key/next-key focus movement to the screen now visible.
  lv_group_remove_all_objs(g_nav_group);
  addFocusableDescendants(g_nav_group, static_cast<lv_obj_t*>(lv_event_get_target(event)));
}
}  // namespace

void setKeyboardGroup(lv_group_t* group) { g_nav_group = group; }

lv_obj_t* createScreen() {
  lv_obj_t* screen = lv_obj_create(nullptr);
  lv_obj_set_style_bg_color(screen, lv_color_hex(kColorPanel), 0);
  lv_obj_set_style_pad_all(screen, 0, 0);
  lv_obj_set_style_border_width(screen, 0, 0);
  lv_obj_add_event_cb(screen, onScreenLoaded, LV_EVENT_SCREEN_LOADED, nullptr);
  return screen;
}

void addBackButton(lv_obj_t* screen, lv_obj_t* home_screen) {
  lv_obj_t* button = lv_button_create(screen);
  lv_obj_set_size(button, 84, kBackButtonHeight);
  lv_obj_align(button, LV_ALIGN_TOP_LEFT, kSafeMargin, kSafeMargin + kStatusBarHeight + 8);
  lv_obj_set_style_bg_color(button, lv_color_hex(kColorPanelLight), 0);
  lv_obj_set_style_border_color(button, lv_color_hex(kColorAccent), 0);
  lv_obj_set_style_border_width(button, 1, 0);
  lv_obj_set_style_radius(button, 8, 0);
  lv_obj_set_style_shadow_width(button, 0, 0);
  lv_obj_add_flag(button, LV_OBJ_FLAG_USER_1);
  lv_obj_add_event_cb(
      button,
      [](lv_event_t* event) {
        auto* target = static_cast<lv_obj_t*>(lv_event_get_user_data(event));
        lv_screen_load(target);
      },
      LV_EVENT_CLICKED, home_screen);

  lv_obj_t* label = lv_label_create(button);
  lv_label_set_text(label, LV_SYMBOL_LEFT " back");
  lv_obj_set_style_text_color(label, lv_color_hex(kColorAccent), 0);
  lv_obj_center(label);
}

void addStatusBar(lv_obj_t* screen, const char* title, bool connected) {
  // Inset from the full width (rather than flush against both edges) and
  // dropped down from y=0, so the bar's own end caps - where the logo,
  // title label, and connectivity dot actually sit - clear the panel's
  // rounded corners instead of sitting inside the curve.
  lv_obj_t* bar = lv_obj_create(screen);
  const lv_coord_t display_width = lv_display_get_horizontal_resolution(lv_obj_get_display(screen));
  lv_obj_set_size(bar, display_width - 2 * kSafeMargin, kStatusBarHeight);
  lv_obj_align(bar, LV_ALIGN_TOP_MID, 0, kSafeMargin);
  lv_obj_set_style_bg_color(bar, lv_color_hex(kColorPanelLight), 0);
  lv_obj_set_style_border_width(bar, 0, 0);
  lv_obj_set_style_radius(bar, 10, 0);
  lv_obj_set_style_pad_all(bar, 6, 0);
  lv_obj_set_style_shadow_width(bar, 12, 0);
  lv_obj_set_style_shadow_color(bar, lv_color_hex(0x000000), 0);
  lv_obj_set_style_shadow_opa(bar, LV_OPA_30, 0);
  lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t* logo = lv_image_create(bar);
  lv_image_set_src(logo, &zeta_k230_logo);
  // Scale the 56x56 source down to fit the bar's inner height cleanly.
  lv_image_set_scale(logo, 180);  // 256 = 100%; ~40px effective.
  lv_obj_align(logo, LV_ALIGN_LEFT_MID, 0, 0);

  lv_obj_t* label = lv_label_create(bar);
  lv_label_set_text(label, title);
  lv_obj_set_style_text_color(label, lv_color_hex(kColorForeground), 0);
  lv_obj_set_style_text_font(label, &lv_font_montserrat_22, 0);
  lv_obj_align(label, LV_ALIGN_LEFT_MID, 44, 0);

  lv_obj_t* clock = lv_label_create(bar);
  lv_label_set_text(clock, "--:--");
  lv_obj_set_style_text_color(clock, lv_color_hex(kColorSecondary), 0);
  lv_obj_set_style_text_font(clock, &lv_font_montserrat_16, 0);
  lv_obj_align(clock, LV_ALIGN_RIGHT_MID, -118, 0);

  lv_obj_t* battery = lv_label_create(bar);
  lv_label_set_text(battery, "--%");
  lv_obj_set_style_text_color(battery, lv_color_hex(kColorForeground), 0);
  lv_obj_set_style_text_font(battery, &lv_font_montserrat_16, 0);
  lv_obj_align(battery, LV_ALIGN_RIGHT_MID, -48, 0);

  lv_obj_t* dot = lv_obj_create(bar);
  lv_obj_set_size(dot, 14, 14);
  lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_border_width(dot, 0, 0);
  lv_obj_set_style_bg_color(dot, lv_color_hex(connected ? kColorAccent : kColorUnavailable), 0);
  lv_obj_align(dot, LV_ALIGN_RIGHT_MID, -4, 0);
  g_status_bars.push_back({clock, battery, dot});
}

void updateStatusBars(const char* clock_text, const char* battery_text, bool connected) {
  for (const auto& refs : g_status_bars) {
    lv_label_set_text(refs.clock, clock_text);
    lv_label_set_text(refs.battery, battery_text);
    lv_obj_set_style_bg_color(
        refs.dot, lv_color_hex(connected ? kColorAccent : kColorUnavailable), 0);
  }
}

lv_obj_t* createTileGrid(lv_obj_t* screen, int cols, int rows) {
  constexpr lv_coord_t kGap = 16;
  lv_display_t* disp = lv_obj_get_display(screen);
  lv_coord_t disp_w = lv_display_get_horizontal_resolution(disp);
  lv_coord_t disp_h = lv_display_get_vertical_resolution(disp);

  lv_coord_t content_top = kSafeMargin + kStatusBarHeight + 12;
  lv_coord_t container_w = disp_w - 2 * kSafeMargin;
  lv_coord_t container_h = disp_h - content_top - kSafeMargin;

  lv_obj_t* grid = lv_obj_create(screen);
  lv_obj_set_size(grid, container_w, container_h);
  lv_obj_align(grid, LV_ALIGN_TOP_MID, 0, content_top);
  lv_obj_set_style_bg_opa(grid, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(grid, 0, 0);
  lv_obj_set_style_pad_all(grid, 0, 0);
  lv_obj_set_style_pad_gap(grid, kGap, 0);
  lv_obj_clear_flag(grid, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
  lv_obj_set_flex_align(grid, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

  // Fixed per-tile size (rather than letting flex size them) so exactly
  // `cols` tiles fit one row width with even gaps and wrapping is
  // predictable, matching how many rows the caller actually wants.
  // Stashed on the grid (heap-allocated, never freed - screens live for
  // the whole process, same lifetime reasoning as elsewhere in this file)
  // so createTile() can read it back without every caller needing to
  // recompute or pass it through separately.
  auto* tile_size = new TileSize{
      (container_w - (cols - 1) * kGap) / cols,
      (container_h - (rows - 1) * kGap) / rows,
  };
  lv_obj_set_user_data(grid, tile_size);
  return grid;
}

lv_obj_t* createTile(lv_obj_t* grid, const char* label_text, TileState state,
                     const char* symbol, const char* subtitle) {
  const auto* tile_size = static_cast<const TileSize*>(lv_obj_get_user_data(grid));
  lv_coord_t tile_w = tile_size->w;
  lv_coord_t tile_h = tile_size->h;

  lv_obj_t* tile = lv_obj_create(grid);
  lv_obj_set_size(tile, tile_w, tile_h);
  // Root cause of a real bug (see README's Home screen section): without
  // this, the tile inherits a large default theme padding that every
  // *other* container in this file explicitly zeroes - lv_obj_get_content_height()
  // (what LV_ALIGN_BOTTOM_LEFT/etc. actually compute against) came back
  // 78px against an 224px-tall tile, so the subtitle landed nowhere near
  // the bottom. TOP_LEFT (the title, above) doesn't depend on content
  // height, which is why it looked fine and masked this for a long time.
  lv_obj_set_style_pad_all(tile, 0, 0);
  lv_obj_set_style_radius(tile, 12, 0);
  lv_obj_set_style_border_width(tile, state == TileState::Live ? 1 : 0, 0);
  lv_obj_set_style_border_color(tile, lv_color_hex(kColorAccent), 0);
  lv_obj_set_style_border_opa(tile, LV_OPA_40, 0);
  lv_obj_set_style_shadow_width(tile, state == TileState::Live ? 10 : 0, 0);
  lv_obj_set_style_shadow_color(tile, lv_color_hex(0x000000), 0);
  lv_obj_set_style_shadow_opa(tile, LV_OPA_20, 0);
  lv_obj_clear_flag(tile, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_color(
      tile, lv_color_hex(state == TileState::Live ? kColorPanelLight : kColorUnavailable), 0);
  if (state == TileState::Live) {
    lv_obj_add_flag(tile, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(tile, lv_color_hex(kColorAccent), LV_STATE_PRESSED);
  }

  if (symbol != nullptr) {
    lv_obj_t* icon = lv_label_create(tile);
    lv_label_set_text(icon, symbol);
    lv_obj_set_style_text_font(icon, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(
        icon, lv_color_hex(state == TileState::Live ? kColorAccent : kColorSecondary), 0);
    lv_obj_align(icon, LV_ALIGN_TOP_RIGHT, -14, 12);
  }

  lv_obj_t* label = lv_label_create(tile);
  lv_label_set_text(label, label_text);
  lv_obj_set_style_text_font(label, &lv_font_montserrat_22, 0);
  lv_obj_set_style_text_color(
      label, lv_color_hex(state == TileState::Live ? kColorForeground : kColorSecondary), 0);
  lv_obj_align(label, LV_ALIGN_TOP_LEFT, 14, 14);

  if (subtitle != nullptr) {
    lv_obj_t* detail = lv_label_create(tile);
    lv_label_set_text(detail, subtitle);
    lv_obj_set_width(detail, tile_w - 28);
    lv_label_set_long_mode(detail, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_style_text_color(detail, lv_color_hex(kColorSecondary), 0);
    lv_obj_set_style_text_font(detail, &lv_font_montserrat_14, 0);
    // lv_obj_align() doesn't compute a position immediately - it just sets
    // a style property + raw offset for the layout system to resolve
    // later, and that resolution was landing BOTTOM_LEFT right under the
    // title instead of near the tile's actual bottom (confirmed via
    // lv_obj_get_y() while debugging - see README's Home screen section).
    // lv_obj_align_to() (confirmed correct by reading lv_obj_pos.c
    // directly: it computes off lv_obj_get_content_height(base) right
    // away) doesn't have that problem.
    lv_obj_update_layout(tile);  // resolve tile's own content-box/padding before using it as a base
    lv_obj_align_to(detail, tile, LV_ALIGN_BOTTOM_LEFT, 14, -14);
  }

  if (state != TileState::Live) {
    lv_obj_t* sub = lv_label_create(tile);
    lv_label_set_text(sub, "not available");
    lv_obj_set_style_text_color(sub, lv_color_hex(kColorSecondary), 0);
    lv_obj_set_style_text_font(sub, &lv_font_montserrat_12, 0);
    lv_obj_align(sub, LV_ALIGN_BOTTOM_RIGHT, -14, -14);
  }

  return tile;
}

}  // namespace reconclave::ui
