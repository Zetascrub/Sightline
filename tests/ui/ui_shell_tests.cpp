#include "src/ui_shell.h"
#include <cassert>
#include <cstdio>
using namespace reconclave::ui;
void tick() { lv_tick_inc(110); lv_timer_handler(); }
void back(lv_obj_t* screen) {
  for (unsigned i = 0; i < lv_obj_get_child_count(screen); ++i) {
    auto* child = lv_obj_get_child(screen, i);
    if (lv_obj_has_flag(child, LV_OBJ_FLAG_USER_1)) {
      lv_obj_send_event(child, LV_EVENT_CLICKED, nullptr);
      return;
    }
  }
  assert(false);
}
int main() {
  lv_init();
  auto* display = lv_display_create(1232, 568);
  static unsigned char pixels[1232 * 568 * 4];
  lv_display_set_buffers(display, pixels, nullptr, sizeof(pixels), LV_DISPLAY_RENDER_MODE_FULL);
  lv_display_set_flush_cb(display, [](lv_display_t* d, const lv_area_t*, unsigned char*) { lv_display_flush_ready(d); });
  auto* group = lv_group_create();
  lv_group_set_default(group);
  setKeyboardGroup(group);
  auto* home = createScreen();
  addStatusBar(home, "SIGHTLINE", false);
  auto* menu = createScreen(); addBackButton(menu, home);
  auto* tool = createScreen(); addBackButton(tool, home);
  lv_screen_load(home);
  navigateToScreen(menu); navigateToScreen(tool);
  back(tool); assert(lv_screen_active() == menu);
  back(menu); assert(lv_screen_active() == home);
  navigateToScreen(tool); back(tool); assert(lv_screen_active() == home);
  auto* row = lv_button_create(home);
  tick(); assert(lv_group_get_obj_count(group) == 1);
  assert(lv_group_get_focused(group) == row);
  auto* hidden = lv_obj_create(home); lv_obj_add_flag(hidden, LV_OBJ_FLAG_HIDDEN);
  lv_button_create(hidden);
  auto* disabled = lv_button_create(home); lv_obj_add_state(disabled, LV_STATE_DISABLED);
  lv_button_create(tool);
  tick(); assert(lv_group_get_obj_count(group) == 1);
  assert(lv_group_get_focused(group) == row);
  lv_obj_remove_state(disabled, LV_STATE_DISABLED);
  tick(); assert(lv_group_get_obj_count(group) == 2);
  assert(lv_group_get_focused(group) == row);
  lv_obj_delete(row);
  tick(); assert(lv_group_get_obj_count(group) == 1);
  assert(lv_group_get_focused(group) == disabled);
  auto* filter = lv_dropdown_create(home);
  lv_dropdown_set_options(filter, "All files\nPhotos\nText");
  tick(); assert(lv_group_get_obj_count(group) == 2);
  lv_group_focus_obj(filter);
  lv_dropdown_open(filter);
  tick(); assert(lv_group_get_focused(group) == filter);
  lv_dropdown_close(filter);
  lv_obj_add_state(filter, LV_STATE_DISABLED);
  tick(); assert(lv_group_get_obj_count(group) == 1);
  updateStatusBars("12:00 BST", "100%", true);
  tick();
  std::puts("UI navigation checks passed");
}
