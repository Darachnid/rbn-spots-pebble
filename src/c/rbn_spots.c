#include <pebble.h>
#include <string.h>

#define SPOT_MAX 10
#define CALL_MAX 13
#define GRID_MAX 7
#define WHERE_MAX 17
#define FREQ_MAX 8
#define MODE_MAX 4
#define PAYLOAD_MAX 960
#define CHUNK_SLOTS 8

#define HEADER_H 28
#define FOOTER_H 28
#define PERSIST_KEY_CALL 1
#define HOLD_REFRESH_MS 550
#define HOLD_ANIM_MS 40
#define AGE_TICK_MS 10000
#define SYNC_TICK_MS 5000
#define FRESH_PULSE_SEC 3

typedef struct {
  char de[13];
  char freq[FREQ_MAX];
  int snr;
  int age;
  char band[4];
  char mode[MODE_MAX];
  char grid[GRID_MAX];
  char where[WHERE_MAX];
} Spot;

static Window *s_window;
static Window *s_detail_window;
static Layer *s_detail_layer;
static int s_detail_index = -1;
static MenuLayer *s_menu;
static Layer *s_header;
static Layer *s_footer;
static char s_callsign[CALL_MAX] = "W1AW";
static Spot s_spots[SPOT_MAX];
static int s_spot_count;
static bool s_loading = true;
static bool s_error;
static bool s_have_data;
static bool s_outbox_busy;
static bool s_refreshing;
static time_t s_fetched_at;
static AppTimer *s_refresh_timer;
static AppTimer *s_age_timer;
static AppTimer *s_retry_timer;
static AppTimer *s_hold_anim_timer;
static AppTimer *s_sync_timer;
static bool s_parsing;
static bool s_holding_up;
static int s_hold_progress;
static time_t s_fresh_until;
static uint32_t s_last_payload_hash;

static char s_asm_buf[PAYLOAD_MAX];
static int s_asm_total;
static int s_asm_got;
static char s_chunk_store[CHUNK_SLOTS][168];
static char *s_asm_parts[CHUNK_SLOTS];

static void request_spots(void);
static void parse_payload(const char *payload);
static void draw_header(Layer *layer, GContext *ctx);
static void draw_footer(Layer *layer, GContext *ctx);
static void mark_ui_dirty(void);
static void spots_data_changed(void);
static void persist_callsign_save(void);
static void cancel_timers(void);

static void trim_copy(char *dst, size_t dst_len, const char *src) {
  if (!dst_len) return;
  if (!src) {
    dst[0] = '\0';
    return;
  }
  size_t n = 0;
  while (src[n] && n < dst_len - 1) n++;
  memcpy(dst, src, n);
  dst[n] = '\0';
}

static void mark_ui_dirty(void) {
  if (s_header) layer_mark_dirty(s_header);
  if (s_footer) layer_mark_dirty(s_footer);
  if (s_menu) layer_mark_dirty(menu_layer_get_layer(s_menu));
}

static void persist_callsign_save(void) {
  char buf[CALL_MAX];
  trim_copy(buf, sizeof(buf), s_callsign);
  persist_write_string(PERSIST_KEY_CALL, buf);
}

static void cancel_timers(void) {
  if (s_refresh_timer) {
    app_timer_cancel(s_refresh_timer);
    s_refresh_timer = NULL;
  }
  if (s_age_timer) {
    app_timer_cancel(s_age_timer);
    s_age_timer = NULL;
  }
  if (s_retry_timer) {
    app_timer_cancel(s_retry_timer);
    s_retry_timer = NULL;
  }
  if (s_hold_anim_timer) {
    app_timer_cancel(s_hold_anim_timer);
    s_hold_anim_timer = NULL;
  }
  if (s_sync_timer) {
    app_timer_cancel(s_sync_timer);
    s_sync_timer = NULL;
  }
  s_holding_up = false;
  s_hold_progress = 0;
}

static bool menu_at_top(void) {
  if (!s_menu) return true;
  if (s_loading || s_spot_count == 0) return true;
  MenuIndex idx = menu_layer_get_selected_index(s_menu);
  return idx.row == 0;
}

static void hold_anim_tick(void *data) {
  s_hold_anim_timer = NULL;
  if (!s_holding_up || !menu_at_top()) {
    s_holding_up = false;
    s_hold_progress = 0;
    mark_ui_dirty();
    return;
  }
  s_hold_progress += (100 * HOLD_ANIM_MS) / HOLD_REFRESH_MS;
  if (s_hold_progress > 100) s_hold_progress = 100;
  mark_ui_dirty();
  if (s_hold_progress < 100) {
    s_hold_anim_timer = app_timer_register(HOLD_ANIM_MS, hold_anim_tick, NULL);
  }
}

static void hold_anim_begin(void) {
  if (!menu_at_top()) return;
  s_holding_up = true;
  s_hold_progress = 0;
  if (s_hold_anim_timer) {
    app_timer_cancel(s_hold_anim_timer);
    s_hold_anim_timer = NULL;
  }
  hold_anim_tick(NULL);
}

static void hold_anim_cancel(void) {
  if (s_hold_progress >= 100) return;
  s_holding_up = false;
  s_hold_progress = 0;
  if (s_hold_anim_timer) {
    app_timer_cancel(s_hold_anim_timer);
    s_hold_anim_timer = NULL;
  }
  mark_ui_dirty();
}

static int16_t clamp_width(int32_t w, int16_t max_w) {
  if (w < 1) return 1;
  if (w > max_w) return max_w;
  return (int16_t)w;
}

static int layout_pad_x(void) {
  return PBL_IF_ROUND_ELSE(44, 0);
}

static int layout_pad_top(void) {
  return PBL_IF_ROUND_ELSE(36, 0);
}

static int layout_pad_bottom(void) {
  return PBL_IF_ROUND_ELSE(32, 0);
}

static int layout_header_h(void) {
  return PBL_IF_ROUND_ELSE(22, HEADER_H);
}

static int layout_footer_h(void) {
  return PBL_IF_ROUND_ELSE(16, FOOTER_H);
}

static int menu_text_x(void) {
  return PBL_IF_ROUND_ELSE(6, 8);
}

static int menu_text_w(int16_t cell_w) {
  int margin = PBL_IF_ROUND_ELSE(12, 10);
  return (int)cell_w - menu_text_x() - margin;
}

static void spots_data_changed(void) {
  if (!s_menu) {
    mark_ui_dirty();
    return;
  }
  menu_layer_reload_data(s_menu);
  mark_ui_dirty();
}

static long freq_khz_value(const char *raw) {
  long khz = 0;
  if (!raw) return -1;
  while (*raw >= '0' && *raw <= '9') {
    khz = khz * 10 + (*raw - '0');
    raw++;
  }
  return khz > 0 ? khz : -1;
}

static void reset_chunks(void) {
  s_asm_total = 0;
  s_asm_got = 0;
  for (int i = 0; i < CHUNK_SLOTS; i++) {
    s_asm_parts[i] = NULL;
    s_chunk_store[i][0] = '\0';
  }
  s_asm_buf[0] = '\0';
}

static int tuple_int(Tuple *t) {
  if (!t) return -1;
  if (t->type == TUPLE_INT) return t->value->int32;
  if (t->type == TUPLE_UINT) return (int)t->value->uint32;
  return -1;
}

static int spot_age_sec(const Spot *spot) {
  int sec = spot->age;
  if (s_fetched_at) sec += (int)(time(NULL) - s_fetched_at);
  return sec < 0 ? 0 : sec;
}

static void format_age(int sec, char *buf, size_t len) {
  if (sec < 0) sec = 0;
  if (sec < 60) {
    snprintf(buf, len, "%ds", sec);
    return;
  }
  if (sec < 3600) {
    snprintf(buf, len, "%dm%02ds", sec / 60, sec % 60);
    return;
  }
  int hr = sec / 3600;
  if (hr < 48) {
    snprintf(buf, len, "%dh%02dm", hr, (sec / 60) % 60);
    return;
  }
  snprintf(buf, len, "%dd", (sec + 43200) / 86400);
}

static void format_sync_age(time_t since, char *buf, size_t len) {
  if (!since) {
    snprintf(buf, len, "RBN");
    return;
  }
  int sec = (int)(time(NULL) - since);
  if (sec < 0) sec = 0;
  if (sec < 60) snprintf(buf, len, "%ds", sec);
  else if (sec < 3600) snprintf(buf, len, "%dm", sec / 60);
  else snprintf(buf, len, "%dh", sec / 3600);
}

static uint32_t hash_payload_spots(void) {
  uint32_t h = 2166136261u;
  for (int i = 0; i < s_spot_count; i++) {
    Spot *s = &s_spots[i];
    const char *parts[] = { s->de, s->freq, s->band, s->mode, NULL };
    for (int p = 0; parts[p]; p++) {
      const char *c = parts[p];
      while (c && *c) {
        h ^= (uint8_t)*c++;
        h *= 16777619u;
      }
      h ^= 0xff;
      h *= 16777619u;
    }
    h ^= (uint32_t)s->snr;
    h *= 16777619u;
  }
  return h;
}

static void format_freq(const char *raw, char *buf, size_t len) {
  if (!raw || !raw[0]) {
    buf[0] = '\0';
    return;
  }
  long khz = freq_khz_value(raw);
  if (khz <= 0) {
    trim_copy(buf, len, raw);
    return;
  }
  if (khz < 1000) {
    snprintf(buf, len, "%ld", khz);
  } else {
    snprintf(buf, len, "%ld.%03ld", khz / 1000, khz % 1000);
  }
}

static GColor band_color(const char *band) {
  if (!band || !band[0]) return GColorWhite;
  if (!strcmp(band, "160")) return GColorMelon;
  if (!strcmp(band, "80")) return GColorOrange;
  if (!strcmp(band, "60")) return GColorFolly;
  if (!strcmp(band, "40")) return GColorYellow;
  if (!strcmp(band, "30")) return GColorRajah;
  if (!strcmp(band, "20")) return GColorGreen;
  if (!strcmp(band, "17")) return GColorJaegerGreen;
  if (!strcmp(band, "15")) return GColorCyan;
  if (!strcmp(band, "12")) return GColorBlue;
  if (!strcmp(band, "10")) return GColorVividCerulean;
  if (!strcmp(band, "6")) return GColorPurple;
  if (!strcmp(band, "2")) return GColorMagenta;
  return GColorWhite;
}

static GColor snr_color(int snr) {
  if (snr >= 10) return GColorGreen;
  if (snr >= 0) return GColorRajah;
  return GColorRed;
}

static char *next_field(char **cursor) {
  if (!cursor || !*cursor) return NULL;
  char *start = *cursor;
  char *tab = strchr(start, '\t');
  char *nl = strchr(start, '\n');
  char *end = tab;
  if (!end || (nl && nl < end)) end = nl;
  if (!end) {
    *cursor = NULL;
    return start;
  }
  *end = '\0';
  *cursor = end + 1;
  return start;
}

static void parse_payload(const char *payload) {
  if (s_parsing) return;
  s_parsing = true;

  int new_count = 0;
  s_error = false;
  if (!payload || !payload[0]) {
    s_parsing = false;
    return;
  }

  static char copy[PAYLOAD_MAX];
  strncpy(copy, payload, sizeof(copy) - 1);
  copy[sizeof(copy) - 1] = '\0';
  char *cursor = copy;

  char *ver = next_field(&cursor);
  if (!ver || strcmp(ver, "2") != 0) {
    s_parsing = false;
    return;
  }

  char *call = next_field(&cursor);
  if (call && call[0]) trim_copy(s_callsign, sizeof(s_callsign), call);

  char *count_str = next_field(&cursor);
  int expect = count_str ? atoi(count_str) : 0;
  if (expect > SPOT_MAX) expect = SPOT_MAX;

  for (int i = 0; i < expect && cursor; i++) {
    char *line_start = cursor;
    char *line_end = strchr(cursor, '\n');
    if (line_end) {
      *line_end = '\0';
      cursor = line_end + 1;
    } else {
      cursor = NULL;
    }

    char *fields = line_start;
    if (new_count >= SPOT_MAX) break;
    Spot *spot = &s_spots[new_count];
    memset(spot, 0, sizeof(*spot));

    char *de = next_field(&fields);
    char *freq = next_field(&fields);
    char *snr = next_field(&fields);
    char *age = next_field(&fields);
    char *band = next_field(&fields);
    char *mode = next_field(&fields);
    char *grid = next_field(&fields);
    char *where = next_field(&fields);
    if (!de || !de[0]) continue;

    trim_copy(spot->de, sizeof(spot->de), de);
    trim_copy(spot->freq, sizeof(spot->freq), freq ? freq : "");
    spot->snr = snr ? atoi(snr) : 0;
    spot->age = age ? atoi(age) : 0;
    trim_copy(spot->band, sizeof(spot->band), band ? band : "");
    trim_copy(spot->mode, sizeof(spot->mode), mode ? mode : "");
    trim_copy(spot->grid, sizeof(spot->grid), grid ? grid : "");
    trim_copy(spot->where, sizeof(spot->where), where ? where : "");
    new_count++;
  }

  s_spot_count = new_count;
  s_loading = false;
  s_refreshing = false;
  s_have_data = true;
  s_fetched_at = time(NULL);
  if (s_spot_count > 0) {
    uint32_t h = hash_payload_spots();
    if (h != s_last_payload_hash) {
      s_last_payload_hash = h;
      s_fresh_until = s_fetched_at + FRESH_PULSE_SEC;
    }
  }
  APP_LOG(APP_LOG_LEVEL_INFO, "rbn got %d spots", s_spot_count);
  spots_data_changed();
  persist_callsign_save();
  s_parsing = false;
}

static void try_finish_chunks(void) {
  if (!s_asm_total || s_asm_got != s_asm_total) return;
  int at = 0;
  s_asm_buf[0] = '\0';
  for (int i = 0; i < s_asm_total; i++) {
    if (!s_asm_parts[i]) return;
    int len = strlen(s_asm_parts[i]);
    if (at + len >= (int)sizeof(s_asm_buf) - 1) return;
    memcpy(s_asm_buf + at, s_asm_parts[i], len);
    at += len;
    s_asm_buf[at] = '\0';
  }
  reset_chunks();
  parse_payload(s_asm_buf);
}

static void handle_chunk(DictionaryIterator *iter) {
  Tuple *ti = dict_find(iter, MESSAGE_KEY_SpotsChunkIdx);
  Tuple *tc = dict_find(iter, MESSAGE_KEY_SpotsChunkCount);
  Tuple *td = dict_find(iter, MESSAGE_KEY_SpotsChunkData);
  if (!ti || !tc || !td || td->type != TUPLE_CSTRING) return;
  int idx = tuple_int(ti);
  int count = tuple_int(tc);
  if (count < 1 || count > CHUNK_SLOTS || idx < 0 || idx >= count) return;
  if (idx == 0) reset_chunks();
  if (!s_asm_total) s_asm_total = count;
  if (count != s_asm_total || s_asm_parts[idx]) return;
  strncpy(s_chunk_store[idx], td->value->cstring, sizeof(s_chunk_store[idx]) - 1);
  s_chunk_store[idx][sizeof(s_chunk_store[idx]) - 1] = '\0';
  s_asm_parts[idx] = s_chunk_store[idx];
  s_asm_got++;
  try_finish_chunks();
}

static uint16_t menu_rows(MenuLayer *menu, uint16_t section, void *data) {
  if (s_loading) return 1;
  if (s_spot_count == 0) return 1;
  return (uint16_t)s_spot_count;
}

static int16_t menu_height(MenuLayer *menu, MenuIndex *idx, void *data) {
  if (s_loading || s_spot_count == 0) return PBL_IF_ROUND_ELSE(52, 56);
  if (idx->row >= (uint16_t)s_spot_count) return PBL_IF_ROUND_ELSE(40, 44);
#ifdef PBL_ROUND
  return 40;
#else
  Spot *spot = &s_spots[idx->row];
  return spot->where[0] ? 52 : 44;
#endif
}

static void menu_draw(GContext *ctx, const Layer *cell, MenuIndex *idx, void *data) {
  GRect b = layer_get_bounds(cell);

  if (s_loading) {
    menu_cell_basic_draw(ctx, cell, "Listening", "for spots on phone", NULL);
    return;
  }
  if (s_spot_count == 0) {
    const char *title = s_error ? "No link" : "No spots";
    const char *sub = s_error ? "Open Pebble app on phone" : "Try wider band/mode filters";
    menu_cell_basic_draw(ctx, cell, title, sub, NULL);
    return;
  }

  if (idx->row >= (uint16_t)s_spot_count) return;
  Spot *spot = &s_spots[idx->row];
  GFont bold = fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD);
  GFont reg = fonts_get_system_font(FONT_KEY_GOTHIC_14);
  GFont sm = fonts_get_system_font(FONT_KEY_GOTHIC_14);

  int tx = menu_text_x();
  int tw = menu_text_w(b.size.w);

  graphics_context_set_fill_color(ctx, band_color(spot->band));
  graphics_fill_rect(ctx, GRect(PBL_IF_ROUND_ELSE(2, 0), 0, PBL_IF_ROUND_ELSE(3, 4), b.size.h), 0, GCornerNone);

  char snr_buf[8];
  snprintf(snr_buf, sizeof(snr_buf), "%s%d", spot->snr > 0 ? "+" : "", spot->snr);
  GSize snr_sz = graphics_text_layout_get_content_size(snr_buf, bold, GRect(0, 0, 60, 24),
      GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft);

  int16_t snr_w = clamp_width(snr_sz.w + 2, 60);
  int16_t de_w = clamp_width((int32_t)tw - snr_w - 4, tw);
  int32_t snr_x_i = (int32_t)tx + tw - snr_w;
  int16_t snr_x = snr_x_i < tx ? tx : (int16_t)snr_x_i;

  graphics_context_set_text_color(ctx, GColorWhite);
  graphics_draw_text(ctx, spot->de, bold, GRect(tx, 2, de_w, 22),
      GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);

  graphics_context_set_text_color(ctx, snr_color(spot->snr));
  graphics_draw_text(ctx, snr_buf, bold, GRect(snr_x, 2, snr_w, 22),
      GTextOverflowModeFill, GTextAlignmentLeft, NULL);

  char freq_buf[12];
  char age_buf[12];
  format_freq(spot->freq, freq_buf, sizeof(freq_buf));
  format_age(spot_age_sec(spot), age_buf, sizeof(age_buf));

  char line2[56];
  snprintf(line2, sizeof(line2), "%s %sm", freq_buf, spot->band);
  if (spot->mode[0]) {
    size_t u = strlen(line2);
    snprintf(line2 + u, sizeof(line2) - u, " %s", spot->mode);
  }
#ifndef PBL_ROUND
  if (spot->grid[0]) {
    size_t u = strlen(line2);
    snprintf(line2 + u, sizeof(line2) - u, " %s", spot->grid);
  }
#endif
  {
    size_t u = strlen(line2);
    snprintf(line2 + u, sizeof(line2) - u, " %s", age_buf);
  }
#ifdef PBL_ROUND
  if (spot->where[0]) {
    size_t u = strlen(line2);
    snprintf(line2 + u, sizeof(line2) - u, " · %s", spot->where);
  }
#endif

  graphics_context_set_text_color(ctx, GColorLightGray);
  graphics_draw_text(ctx, line2, reg, GRect(tx, 22, tw, 18),
      GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);

#ifndef PBL_ROUND
  if (spot->where[0]) {
    graphics_draw_text(ctx, spot->where, sm, GRect(tx, 38, tw, 16),
        GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
  }
#endif
}

static void detail_draw(Layer *layer, GContext *ctx) {
  GRect b = layer_get_bounds(layer);
  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_fill_rect(ctx, b, 0, GCornerNone);

  if (s_detail_index < 0 || s_detail_index >= s_spot_count) {
    graphics_context_set_text_color(ctx, GColorLightGray);
    GFont f = fonts_get_system_font(FONT_KEY_GOTHIC_18);
    graphics_draw_text(ctx, "No spot data", f, GRect(8, 80, b.size.w - 16, 24),
        GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
    return;
  }

  Spot *spot = &s_spots[s_detail_index];
#ifdef PBL_ROUND
  GFont title = fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD);
#else
  GFont title = fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD);
#endif
  GFont label = fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD);
  GFont body = fonts_get_system_font(FONT_KEY_GOTHIC_14);

#ifndef PBL_ROUND
  graphics_context_set_fill_color(ctx, band_color(spot->band));
  graphics_fill_rect(ctx, GRect(0, 0, 6, b.size.h), 0, GCornerNone);
#endif

  char snr_buf[12];
  snprintf(snr_buf, sizeof(snr_buf), "SNR %s%ddB", spot->snr > 0 ? "+" : "", spot->snr);

#ifdef PBL_ROUND
  char head[24];
  snprintf(head, sizeof(head), "%s  %s", spot->de, snr_buf);
  graphics_context_set_text_color(ctx, GColorWhite);
  graphics_draw_text(ctx, head, title, GRect(0, 4, b.size.w, 22),
      GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
  graphics_context_set_fill_color(ctx, band_color(spot->band));
  graphics_fill_rect(ctx, GRect(b.size.w / 2 - 20, 26, 40, 3), 0, GCornerNone);
#else
  graphics_context_set_text_color(ctx, snr_color(spot->snr));
  GSize snr_sz = graphics_text_layout_get_content_size(snr_buf, label, GRect(0, 0, 90, 18),
      GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft);
  int16_t call_w = clamp_width((int32_t)b.size.w - snr_sz.w - 18, b.size.w - 20);

  graphics_context_set_text_color(ctx, GColorWhite);
  graphics_draw_text(ctx, spot->de, title, GRect(10, 6, call_w, 28),
      GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
  graphics_context_set_text_color(ctx, snr_color(spot->snr));
  graphics_draw_text(ctx, snr_buf, label, GRect(b.size.w - snr_sz.w - 8, 12, snr_sz.w + 2, 18),
      GTextOverflowModeFill, GTextAlignmentLeft, NULL);
#endif

  char freq_buf[16];
  char age_buf[16];
  format_freq(spot->freq, freq_buf, sizeof(freq_buf));
  format_age(spot_age_sec(spot), age_buf, sizeof(age_buf));

  char heard[20];
  snprintf(heard, sizeof(heard), "%s", s_callsign);

  char freq_line[20];
  snprintf(freq_line, sizeof(freq_line), "%s MHz", freq_buf);

  char band_line[8];
  snprintf(band_line, sizeof(band_line), "%sm", spot->band);

  char age_line[16];
  snprintf(age_line, sizeof(age_line), "%s ago", age_buf);

  int16_t text_w = clamp_width((int32_t)b.size.w - 16, b.size.w - 8);
  int y = PBL_IF_ROUND_ELSE(36, 40);
  int max_y = b.size.h - PBL_IF_ROUND_ELSE(22, 20);
#ifdef PBL_ROUND
  GTextAlignment align = GTextAlignmentCenter;
  int dx = 0;
#else
  GTextAlignment align = GTextAlignmentLeft;
  int dx = 8;
#endif

  graphics_context_set_text_color(ctx, GColorRajah);
  {
    char line[64];
    snprintf(line, sizeof(line), "Heard %s", heard);
    if (y + 18 <= max_y) {
      graphics_draw_text(ctx, line, label, GRect(dx, y, text_w, 18), GTextOverflowModeTrailingEllipsis, align, NULL);
      y += 18;
    }
  }
#define DETAIL_ROW(lbl, val, col) do { \
    if (y + 18 <= max_y && (val)[0]) { \
      char line[64]; \
      snprintf(line, sizeof(line), "%s%s", (lbl), (val)); \
      graphics_context_set_text_color(ctx, (col)); \
      graphics_draw_text(ctx, line, body, GRect(dx, y, text_w, 18), GTextOverflowModeTrailingEllipsis, align, NULL); \
      y += 18; \
    } \
  } while (0)
  DETAIL_ROW("Freq ", freq_line, GColorWhite);
  DETAIL_ROW("Band ", band_line, GColorWhite);
  DETAIL_ROW("Mode ", spot->mode, GColorWhite);
  DETAIL_ROW("Grid ", spot->grid, GColorWhite);
  DETAIL_ROW("From ", spot->where, GColorWhite);
  DETAIL_ROW("Age ", age_line, GColorLightGray);
#undef DETAIL_ROW

  graphics_context_set_text_color(ctx, GColorDarkGray);
  GFont hint = fonts_get_system_font(FONT_KEY_GOTHIC_14);
  graphics_draw_text(ctx, "BACK", hint, GRect(0, b.size.h - PBL_IF_ROUND_ELSE(14, 18), b.size.w, 16),
      GTextOverflowModeFill, GTextAlignmentCenter, NULL);
}

static void detail_window_load(Window *window) {
  Layer *root = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(root);
  int px = layout_pad_x();
  if (px > 0) {
    bounds = GRect(px, layout_pad_top(), bounds.size.w - 2 * px,
        bounds.size.h - layout_pad_top() - layout_pad_bottom());
  }
  s_detail_layer = layer_create(bounds);
  layer_set_update_proc(s_detail_layer, detail_draw);
  layer_add_child(root, s_detail_layer);
}

static void detail_window_unload(Window *window) {
  if (s_detail_layer) {
    layer_destroy(s_detail_layer);
    s_detail_layer = NULL;
  }
}

static void detail_window_appear(Window *window) {
  if (s_detail_layer) layer_mark_dirty(s_detail_layer);
}

static void menu_select(MenuLayer *menu, MenuIndex *idx, void *data) {
  if (s_loading || s_spot_count == 0) return;
  if (idx->row >= (uint16_t)s_spot_count) return;
  s_detail_index = (int)idx->row;
  window_stack_push(s_detail_window, true);
}

static void draw_header(Layer *layer, GContext *ctx) {
  GRect b = layer_get_bounds(layer);
  char tag[12];
  if (s_loading || s_refreshing) {
    snprintf(tag, sizeof(tag), "...");
  } else {
    format_sync_age(s_fetched_at, tag, sizeof(tag));
  }

#ifdef PBL_ROUND
  char title[28];
  snprintf(title, sizeof(title), "%s  ·  %s", s_callsign, tag);
  GFont call_font = fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD);
  graphics_context_set_text_color(ctx, GColorRajah);
  graphics_draw_text(ctx, title, call_font, GRect(0, 2, b.size.w, 20),
      GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
  if (s_hold_progress > 0) {
    int16_t pw = (int16_t)((int32_t)b.size.w * s_hold_progress / 100);
    if (pw < 1) pw = 1;
    graphics_context_set_fill_color(ctx, GColorGreen);
    graphics_fill_rect(ctx, GRect(0, b.size.h - 3, pw, 3), 0, GCornerNone);
  } else if (menu_at_top() && s_spot_count > 0 && !s_loading) {
    graphics_context_set_fill_color(ctx, GColorJaegerGreen);
    graphics_fill_rect(ctx, GRect(0, b.size.h - 2, b.size.w, 2), 0, GCornerNone);
  }
#else
  graphics_context_set_fill_color(ctx, GColorDarkGray);
  graphics_fill_rect(ctx, b, 0, GCornerNone);
  graphics_context_set_fill_color(ctx, GColorRajah);
  graphics_fill_rect(ctx, GRect(0, 0, b.size.w, 3), 0, GCornerNone);

  if (s_hold_progress > 0) {
    int16_t pw = (int16_t)((int32_t)b.size.w * s_hold_progress / 100);
    if (pw < 1) pw = 1;
    graphics_context_set_fill_color(ctx, GColorGreen);
    graphics_fill_rect(ctx, GRect(0, b.size.h - 4, pw, 4), 0, GCornerNone);
  } else if (menu_at_top() && s_spot_count > 0 && !s_loading) {
    graphics_context_set_fill_color(ctx, GColorJaegerGreen);
    graphics_fill_rect(ctx, GRect(0, b.size.h - 2, b.size.w, 2), 0, GCornerNone);
  }

  GFont call_font = fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD);
  GFont tag_font = fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD);
  graphics_context_set_text_color(ctx, GColorRajah);
  graphics_draw_text(ctx, s_callsign, call_font, GRect(6, 6, b.size.w - 70, 20),
      GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
  if (s_fresh_until && time(NULL) < s_fresh_until) {
    graphics_context_set_fill_color(ctx, GColorJaegerGreen);
    graphics_fill_rect(ctx, GRect(b.size.w - 44, 4, 38, 20), 2, GCornersAll);
  }
  graphics_context_set_text_color(ctx, GColorLightGray);
  GSize tw = graphics_text_layout_get_content_size(tag, tag_font, GRect(0, 0, 48, 16),
      GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft);
  graphics_draw_text(ctx, tag, tag_font, GRect(b.size.w - tw.w - 6, 8, tw.w + 2, 16),
      GTextOverflowModeFill, GTextAlignmentRight, NULL);
#endif
}

static void draw_footer(Layer *layer, GContext *ctx) {
  GRect b = layer_get_bounds(layer);
  GFont f = fonts_get_system_font(FONT_KEY_GOTHIC_14);
  graphics_context_set_text_color(ctx, GColorLightGray);
  char line[36];

#ifndef PBL_ROUND
  graphics_context_set_fill_color(ctx, GColorDarkGray);
  graphics_fill_rect(ctx, b, 0, GCornerNone);
#endif

  if (s_spot_count > 0 && s_menu) {
    MenuIndex idx = menu_layer_get_selected_index(s_menu);
    int row = (int)idx.row;
    if (row < 0) row = 0;
    if (row >= s_spot_count) row = s_spot_count - 1;
#ifdef PBL_ROUND
    if (row == 0) {
      snprintf(line, sizeof(line), "%d/%d · hold UP", row + 1, s_spot_count);
    } else {
      snprintf(line, sizeof(line), "%d/%d · UP to top", row + 1, s_spot_count);
    }
    graphics_draw_text(ctx, line, f, GRect(0, 0, b.size.w, b.size.h),
        GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
#else
    char sync[8];
    format_sync_age(s_fetched_at, sync, sizeof(sync));
    snprintf(line, sizeof(line), "%d/%d upd %s", row + 1, s_spot_count, sync);
    graphics_draw_text(ctx, line, f, GRect(4, 2, b.size.w - 8, 14),
        GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
    if (row == 0) {
      graphics_draw_text(ctx, "hold UP to refresh", f, GRect(4, 14, b.size.w - 8, 14),
          GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
    } else {
      graphics_draw_text(ctx, "UP to top, hold UP", f, GRect(4, 14, b.size.w - 8, 14),
          GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
    }
#endif
  } else {
    snprintf(line, sizeof(line), PBL_IF_ROUND_ELSE("hold UP refresh", "hold UP to refresh"));
    graphics_draw_text(ctx, line, f, GRect(0, PBL_IF_ROUND_ELSE(0, 7), b.size.w, b.size.h),
        GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
  }
}

static void retry_timer_cb(void *data) {
  s_retry_timer = NULL;
  request_spots();
}

static void schedule_retry(uint32_t ms) {
  if (s_retry_timer) {
    app_timer_cancel(s_retry_timer);
    s_retry_timer = NULL;
  }
  s_retry_timer = app_timer_register(ms, retry_timer_cb, NULL);
}

static void request_spots(void) {
  if (s_outbox_busy) {
    schedule_retry(800);
    return;
  }
  if (!s_have_data) {
    s_loading = true;
    s_error = false;
    spots_data_changed();
  } else {
    s_refreshing = true;
    mark_ui_dirty();
  }

  DictionaryIterator *iter = NULL;
  AppMessageResult r = app_message_outbox_begin(&iter);
  if (r != APP_MSG_OK || !iter) {
    s_loading = false;
    s_refreshing = false;
    if (!s_have_data) s_error = true;
    mark_ui_dirty();
    schedule_retry(3000);
    return;
  }
  dict_write_uint8(iter, MESSAGE_KEY_SpotsRequest, 1);
  dict_write_cstring(iter, MESSAGE_KEY_Callsign, s_callsign);
  r = app_message_outbox_send();
  if (r != APP_MSG_OK) {
    s_outbox_busy = false;
    s_loading = false;
    s_refreshing = false;
    if (!s_have_data) s_error = true;
    schedule_retry(3000);
    return;
  }
  s_outbox_busy = true;
}

static void outbox_sent(DictionaryIterator *iter, void *context) {
  s_outbox_busy = false;
}

static void outbox_failed(DictionaryIterator *iter, AppMessageResult reason, void *context) {
  s_outbox_busy = false;
  s_loading = false;
  s_refreshing = false;
  if (!s_have_data) s_error = true;
  mark_ui_dirty();
  schedule_retry(4000);
}

static void inbox_received(DictionaryIterator *iter, void *context) {
  Tuple *t = dict_find(iter, MESSAGE_KEY_SpotsPayload);
  if (t && t->type == TUPLE_CSTRING) {
    reset_chunks();
    parse_payload(t->value->cstring);
    return;
  }
  if (dict_find(iter, MESSAGE_KEY_SpotsChunkIdx)) {
    handle_chunk(iter);
    return;
  }
  if (dict_find(iter, MESSAGE_KEY_SpotsError)) {
    s_loading = false;
    s_refreshing = false;
    if (!s_have_data) s_error = true;
    mark_ui_dirty();
    schedule_retry(8000);
    return;
  }
  t = dict_find(iter, MESSAGE_KEY_Callsign);
  if (t && t->type == TUPLE_CSTRING && t->value->cstring[0]) {
    trim_copy(s_callsign, sizeof(s_callsign), t->value->cstring);
    persist_callsign_save();
    request_spots();
  }
}

static void refresh_cb(void *data) {
  s_refresh_timer = NULL;
  request_spots();
  s_refresh_timer = app_timer_register(30000, refresh_cb, NULL);
}

static void age_cb(void *data) {
  s_age_timer = NULL;
  if (s_spot_count > 0 || s_fetched_at) mark_ui_dirty();
  s_age_timer = app_timer_register(AGE_TICK_MS, age_cb, NULL);
}

static void sync_cb(void *data) {
  s_sync_timer = NULL;
  if (s_fetched_at && !s_loading) mark_ui_dirty();
  s_sync_timer = app_timer_register(SYNC_TICK_MS, sync_cb, NULL);
}

static void window_load(Window *window) {
  Layer *root = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(root);

  int px = layout_pad_x();
  int top = layout_pad_top();
  int bottom = layout_pad_bottom();
  int hh = layout_header_h();
  int fh = layout_footer_h();

  s_header = layer_create(GRect(px, top, bounds.size.w - 2 * px, hh));
  layer_set_update_proc(s_header, draw_header);
  layer_add_child(root, s_header);

  s_footer = layer_create(GRect(px, bounds.size.h - bottom - fh, bounds.size.w - 2 * px, fh));
  layer_set_update_proc(s_footer, draw_footer);
  layer_add_child(root, s_footer);

  GRect menu_frame = GRect(px, top + hh, bounds.size.w - 2 * px,
      bounds.size.h - top - bottom - hh - fh);
  s_menu = menu_layer_create(menu_frame);
  menu_layer_set_callbacks(s_menu, NULL, (MenuLayerCallbacks) {
    .get_num_rows = menu_rows,
    .get_cell_height = menu_height,
    .draw_row = menu_draw,
    .select_click = menu_select,
  });
  menu_layer_set_highlight_colors(s_menu, GColorJaegerGreen, GColorWhite);
  menu_layer_set_normal_colors(s_menu, GColorBlack, GColorWhite);
  layer_add_child(root, menu_layer_get_layer(s_menu));
}

static void window_unload(Window *window) {
  cancel_timers();
  if (s_menu) {
    menu_layer_destroy(s_menu);
    s_menu = NULL;
  }
  if (s_header) {
    layer_destroy(s_header);
    s_header = NULL;
  }
  if (s_footer) {
    layer_destroy(s_footer);
    s_footer = NULL;
  }
}

static void up_single(ClickRecognizerRef ref, void *ctx) {
  if (!s_menu || s_loading) return;
  MenuIndex idx = menu_layer_get_selected_index(s_menu);
  if (idx.row > 0) {
    menu_layer_set_selected_index(s_menu, MenuIndex(0, idx.row - 1), MenuRowAlignCenter, true);
    mark_ui_dirty();
  }
}

static void down_single(ClickRecognizerRef ref, void *ctx) {
  if (!s_menu || s_loading || s_spot_count == 0) return;
  MenuIndex idx = menu_layer_get_selected_index(s_menu);
  if (idx.row + 1 < (uint16_t)s_spot_count) {
    menu_layer_set_selected_index(s_menu, MenuIndex(0, idx.row + 1), MenuRowAlignCenter, true);
    mark_ui_dirty();
  }
}

static void select_single(ClickRecognizerRef ref, void *ctx) {
  if (!s_menu) return;
  MenuIndex idx = menu_layer_get_selected_index(s_menu);
  menu_select(s_menu, &idx, NULL);
}

static void up_raw_down(ClickRecognizerRef ref, void *ctx) {
  hold_anim_begin();
}

static void up_raw_up(ClickRecognizerRef ref, void *ctx) {
  hold_anim_cancel();
}

static void hold_bar_clear_cb(void *data) {
  hold_anim_cancel();
}

static void up_long_refresh(ClickRecognizerRef ref, void *ctx) {
  if (!menu_at_top()) return;
  s_holding_up = false;
  s_hold_progress = 100;
  mark_ui_dirty();
  request_spots();
  app_timer_register(400, hold_bar_clear_cb, NULL);
}

static void click_config(void *ctx) {
  window_single_click_subscribe(BUTTON_ID_UP, up_single);
  window_single_click_subscribe(BUTTON_ID_DOWN, down_single);
  window_single_click_subscribe(BUTTON_ID_SELECT, select_single);
  window_raw_click_subscribe(BUTTON_ID_UP, up_raw_down, up_raw_up, NULL);
  window_long_click_subscribe(BUTTON_ID_UP, HOLD_REFRESH_MS, up_long_refresh, NULL);
}

static void kickoff(void *data) {
  request_spots();
}

static void init(void) {
  if (persist_exists(PERSIST_KEY_CALL)) {
    persist_read_string(PERSIST_KEY_CALL, s_callsign, sizeof(s_callsign));
    s_callsign[sizeof(s_callsign) - 1] = '\0';
  } else {
    persist_callsign_save();
  }

  app_message_register_inbox_received(inbox_received);
  app_message_register_outbox_sent(outbox_sent);
  app_message_register_outbox_failed(outbox_failed);

  s_detail_window = window_create();
  window_set_background_color(s_detail_window, GColorBlack);
  window_set_window_handlers(s_detail_window, (WindowHandlers) {
    .load = detail_window_load,
    .unload = detail_window_unload,
    .appear = detail_window_appear,
  });

  s_window = window_create();
  window_set_background_color(s_window, GColorBlack);
  window_set_click_config_provider(s_window, click_config);
  window_set_window_handlers(s_window, (WindowHandlers) {
    .load = window_load,
    .unload = window_unload,
  });
  window_stack_push(s_window, true);

  app_message_open(1248, 256);

  app_timer_register(1200, kickoff, NULL);
  s_refresh_timer = app_timer_register(30000, refresh_cb, NULL);
  s_age_timer = app_timer_register(AGE_TICK_MS, age_cb, NULL);
  s_sync_timer = app_timer_register(SYNC_TICK_MS, sync_cb, NULL);
}

static void deinit(void) {
  cancel_timers();
  window_destroy(s_detail_window);
  s_detail_window = NULL;
  window_destroy(s_window);
  s_window = NULL;
}

int main(void) {
  init();
  app_event_loop();
  deinit();
}
