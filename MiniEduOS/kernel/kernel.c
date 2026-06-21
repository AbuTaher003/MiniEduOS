// I/O Helper Functions
#include "font8x8_basic.h"
#include "logo_data.h"

struct multiboot_info {
  unsigned int flags;
  unsigned int mem_lower;
  unsigned int mem_upper;
  unsigned int boot_device;
  unsigned int cmdline;
  unsigned int mods_count;
  unsigned int mods_addr;
  unsigned int syms[4];
  unsigned int mmap_length;
  unsigned int mmap_addr;
  unsigned int drives_length;
  unsigned int drives_addr;
  unsigned int config_table;
  unsigned int boot_loader_name;
  unsigned int apm_table;
  unsigned int vbe_control_info;
  unsigned int vbe_mode_info;
  unsigned short vbe_mode;
  unsigned short vbe_interface_seg;
  unsigned short vbe_interface_off;
  unsigned short vbe_interface_len;
  unsigned int framebuffer_addr_low;
  unsigned int framebuffer_addr_high;
  unsigned int framebuffer_pitch;
  unsigned int framebuffer_width;
  unsigned int framebuffer_height;
  unsigned char framebuffer_bpp;
  unsigned char framebuffer_type;
} __attribute__((packed));

static void draw_pixel(unsigned int x, unsigned int y, unsigned char r,
                       unsigned char g, unsigned char b, unsigned int fb_addr,
                       unsigned int pitch, unsigned int bpp) {
  if (bpp == 32) {
    unsigned char *pixel = (unsigned char *)(fb_addr + y * pitch + x * 4);
    pixel[0] = b; // Blue
    pixel[1] = g; // Green
    pixel[2] = r; // Red
    pixel[3] = 0; // Alpha/unused
  } else if (bpp == 24) {
    unsigned char *pixel = (unsigned char *)(fb_addr + y * pitch + x * 3);
    pixel[0] = b;
    pixel[1] = g;
    pixel[2] = r;
  }
}

static void draw_char_scale(char c, int start_x, int start_y, unsigned char r,
                            unsigned char g, unsigned char b,
                            unsigned int fb_addr, unsigned int pitch,
                            unsigned int bpp, int scale) {
  unsigned char uc = (unsigned char)c;
  if (uc >= 128)
    return;
  unsigned char *bitmap = (unsigned char *)font8x8_basic[uc];
  for (int row = 0; row < 8; row++) {
    unsigned char row_data = bitmap[row];
    for (int col = 0; col < 8; col++) {
      if ((row_data >> col) & 1) {
        for (int sy = 0; sy < scale; sy++) {
          for (int sx = 0; sx < scale; sx++) {
            draw_pixel(start_x + col * scale + sx, start_y + row * scale + sy,
                       r, g, b, fb_addr, pitch, bpp);
          }
        }
      }
    }
  }
}

static void draw_string(const char *str, int start_x, int start_y,
                        unsigned char r, unsigned char g, unsigned char b,
                        unsigned int fb_addr, unsigned int pitch,
                        unsigned int bpp, int scale) {
  int x = start_x;
  for (int i = 0; str[i] != '\0'; i++) {
    draw_char_scale(str[i], x, start_y, r, g, b, fb_addr, pitch, bpp, scale);
    x += 8 * scale;
  }
}

static const char *cursor_mask[12] = {
    "X*      ", "XX*     ", "XXX*    ", "XXXX*   ", "XXXXX*  ", "XXXXXX* ",
    "XXXXXXX*", "XXXX*** ", "X*XX*   ", "* *XX*  ", "   *XX* ", "    *** "};

static int mouse_x = 400;
static int mouse_y = 300;
static int left_button_pressed = 0;
static int mouse_clicked_event = 0;
static int right_button_pressed = 0;
static int mouse_right_clicked_event = 0;
static int force_redraw = 0;
static int kb_click_active = 0;
static int graphics_mode = 0;

static int mouse_cycle = 0;
static unsigned char mouse_packet[3];

static inline unsigned char inb(unsigned short port) {
  unsigned char val;
  __asm__ volatile("inb %1, %0" : "=a"(val) : "Nd"(port));
  return val;
}

static inline void outb(unsigned short port, unsigned char val) {
  __asm__ volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline void outw(unsigned short port, unsigned short val) {
  __asm__ volatile("outw %0, %1" : : "a"(val), "Nd"(port));
}

// String Helper Functions
static int strcmp(const char *s1, const char *s2) {
  while (*s1 && (*s1 == *s2)) {
    s1++;
    s2++;
  }
  return *(unsigned char *)s1 - *(unsigned char *)s2;
}

static int strncmp(const char *s1, const char *s2, int n) {
  while (n > 0 && *s1 && (*s1 == *s2)) {
    s1++;
    s2++;
    n--;
  }
  if (n == 0)
    return 0;
  return *(unsigned char *)s1 - *(unsigned char *)s2;
}

static int atoi(const char *str) {
  int val = 0;
  int sign = 1;
  int i = 0;
  while (str[i] == ' ')
    i++;
  if (str[i] == '-') {
    sign = -1;
    i++;
  } else if (str[i] == '+') {
    i++;
  }
  while (str[i] >= '0' && str[i] <= '9') {
    val = val * 10 + (str[i] - '0');
    i++;
  }
  return val * sign;
}

static int redirect_to_gui_term = 0;
static void gui_term_write_char(char c);
static int vfs_strlen(const char *s);
static void vfs_strcpy(char *dest, const char *src);
static void vfs_strncpy(char *dest, const char *src, int n);

// Graphical Terminal Scrollback Buffer
#define TERM_SCROLLBACK_MAX 300
static char term_scrollback[TERM_SCROLLBACK_MAX][60];
static int term_scrollback_count = 0;
static int term_scroll_offset = 0;

// Keyboard Layouts (US QWERTY)
static const char kbd_us[128] = {
    0, 27, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',
    '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n', 0,
    'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`', 0, '\\', 'z',
    'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0, '*', 0, ' ', 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0x81, 0x85, '-', 0x82, 0, 0x83, '+',
    0, // 0x48 is mapped to 0x81 (Up Arrow), 0x49 to 0x85 (PageUp), 0x4B to 0x82
       // (Left), 0x4D to 0x83 (Right)
    0x84, 0x86, 0, 0, 0, 0, 0, 0, 0,
    0, // 0x50 is mapped to 0x84 (Down Arrow), 0x51 to 0x86 (PageDown)
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

static const char kbd_us_shift[128] = {
    0,    27,   '!',  '@',  '#', '$',  '%', '^',  '&', '*', '(', ')', '_',  '+',
    '\b', '\t', 'Q',  'W',  'E', 'R',  'T', 'Y',  'U', 'I', 'O', 'P', '{',  '}',
    '\n', 0,    'A',  'S',  'D', 'F',  'G', 'H',  'J', 'K', 'L', ':', '\"', '~',
    0,    '|',  'Z',  'X',  'C', 'V',  'B', 'N',  'M', '<', '>', '?', 0,    '*',
    0,    ' ',  0,    0,    0,   0,    0,   0,    0,   0,   0,   0,   0,    0,
    0,    0,    0x81, 0x85, '-', 0x82, 0,   0x83, '+',
    0, // Up, PageUp, Left, Right arrows mapped on shift layout too
    0x84, 0x86, 0,    0,    0,   0,    0,   0,    0,   0, // Down, PageDown
                                                          // mapped on shift
                                                          // layout too
    0,    0,    0,    0,    0,   0,    0,   0,    0,   0,   0,   0,   0,    0,
    0,    0,    0,    0,    0,   0,    0,   0,    0,   0,   0,   0,   0,    0,
    0,    0,    0,    0,    0,   0,    0,   0,    0,   0};

// Keyboard Polling State
static int shift_pressed = 0;
static int alt_pressed = 0;
static int ctrl_pressed = 0;

static unsigned char get_scancode() {
  while ((inb(0x64) & 1) == 0)
    ;               // Poll status register until Output Buffer Full is 1
  return inb(0x60); // Read the scancode from data port
}

static char get_char() {
  while (1) {
    unsigned char scancode = get_scancode();
    if (scancode == 0x2A || scancode == 0x36) {
      shift_pressed = 1;
    } else if (scancode == 0xAA || scancode == 0xB6) {
      shift_pressed = 0;
    } else if (scancode == 0x38) {
      alt_pressed = 1;
    } else if (scancode == 0xB8) {
      alt_pressed = 0;
    } else if (scancode == 0x1D) {
      ctrl_pressed = 1;
    } else if (scancode == 0x9D) {
      ctrl_pressed = 0;
    } else if (scancode == 0x3C) { // F2
      return (char)0x87;
    } else if (scancode == 0x53) { // Delete
      return (char)0x88;
    } else if ((scancode & 0x80) == 0) { // Make code (key press)
      char c = shift_pressed ? kbd_us_shift[scancode] : kbd_us[scancode];
      if (c != 0) {
        return c;
      }
    }
  }
}

// VGA Text-Mode Terminal Variables
static volatile char *video = (volatile char *)0xb8000;
static char virtual_video[4000];
static unsigned int saved_multiboot_magic;
static unsigned int saved_multiboot_info_addr;
static int cursor_x = 0;
static int cursor_y = 1; // Row 0 is reserved for the Title Bar

// Default color scheme states (White on Black)
static char shell_bg = 0x0;
static char shell_fg = 0x0F;
static char shell_text_color = 0x0F;

static void update_cursor(int x, int y) {
  unsigned short temp = y * 80 + x;
  outb(0x3D4, 14);
  outb(0x3D5, temp >> 8);
  outb(0x3D4, 15);
  outb(0x3D5, temp & 0xFF);
}

static void draw_title_bar() {
  const char *title = "MiniEduOS v1.0 - Educational Operating System";
  int title_len = 0;
  while (title[title_len] != '\0')
    title_len++;

  int start_col = (80 - title_len) / 2;

  for (int x = 0; x < 80; x++) {
    video[x * 2] = ' ';
    video[x * 2 + 1] = 0x1F; // White on Blue background
  }

  for (int i = 0; i < title_len; i++) {
    video[(start_col + i) * 2] = title[i];
  }
}

static void term_clear() {
  // Clear rows 1 to 24 with the active background attribute
  for (int i = 80; i < 80 * 25; i++) {
    video[i * 2] = ' ';
    video[i * 2 + 1] = shell_text_color;
  }
  cursor_x = 0;
  cursor_y = 1;
  update_cursor(cursor_x, cursor_y);
}

static void term_scroll() {
  // Copy rows 2-24 to 1-23
  for (int y = 2; y < 25; y++) {
    for (int x = 0; x < 80; x++) {
      video[((y - 1) * 80 + x) * 2] = video[(y * 80 + x) * 2];
      video[((y - 1) * 80 + x) * 2 + 1] = video[(y * 80 + x) * 2 + 1];
    }
  }
  // Clear row 24 with current terminal color
  for (int x = 0; x < 80; x++) {
    video[(24 * 80 + x) * 2] = ' ';
    video[(24 * 80 + x) * 2 + 1] = shell_text_color;
  }
  cursor_y = 24;
}

static void term_putc_color(char c, char attribute) {
  if (redirect_to_gui_term) {
    gui_term_write_char(c);
    return;
  }
  if (c == '\n') {
    cursor_x = 0;
    cursor_y++;
  } else {
    video[(cursor_y * 80 + cursor_x) * 2] = c;
    video[(cursor_y * 80 + cursor_x) * 2 + 1] = attribute;
    cursor_x++;
    if (cursor_x >= 80) {
      cursor_x = 0;
      cursor_y++;
    }
  }

  if (cursor_y >= 25) {
    term_scroll();
  }
  update_cursor(cursor_x, cursor_y);
}

static void term_print(const char *str) {
  for (int i = 0; str[i] != '\0'; i++) {
    term_putc_color(str[i], shell_text_color);
  }
}

static void term_print_color(const char *str, char attribute) {
  for (int i = 0; str[i] != '\0'; i++) {
    term_putc_color(str[i], attribute);
  }
}

static void term_backspace() {
  if (cursor_x > 0) {
    cursor_x--;
    video[(cursor_y * 80 + cursor_x) * 2] = ' ';
    video[(cursor_y * 80 + cursor_x) * 2 + 1] = shell_text_color;
    update_cursor(cursor_x, cursor_y);
  }
}

static void print_int(int val) {
  char buf[32];
  int i = 0;
  int is_neg = 0;
  if (val < 0) {
    is_neg = 1;
    val = -val;
  }
  if (val == 0) {
    term_putc_color('0', shell_text_color);
    return;
  }
  while (val > 0) {
    buf[i++] = '0' + (val % 10);
    val /= 10;
  }
  if (is_neg) {
    term_putc_color('-', shell_text_color);
  }
  while (i > 0) {
    term_putc_color(buf[--i], shell_text_color);
  }
}

static void print_int_leading_zero(int val, int digits) {
  char buf[32];
  int i = 0;
  while (val > 0) {
    buf[i++] = '0' + (val % 10);
    val /= 10;
  }
  while (i < digits) {
    buf[i++] = '0';
  }
  while (i > 0) {
    term_putc_color(buf[--i], shell_text_color);
  }
}

static void print_hex(unsigned int val) {
  const char *hex_chars = "0123456789ABCDEF";
  char buf[8];
  for (int i = 7; i >= 0; i--) {
    buf[i] = hex_chars[val & 0xF];
    val >>= 4;
  }
  for (int i = 0; i < 8; i++) {
    term_putc_color(buf[i], shell_text_color);
  }
}

static void print_size(unsigned int size) {
  if (size >= 1024 * 1024) {
    print_int(size / (1024 * 1024));
    term_print(" MB");
  } else if (size >= 1024) {
    print_int(size / 1024);
    term_print(" KB");
  } else {
    print_int(size);
    term_print(" B");
  }
}

static void delay_ms(int ms) {
  for (volatile int i = 0; i < ms * 1300000; i++) {
    __asm__ volatile("nop");
  }
}

// CMOS RTC Helpers
static int is_rtc_updating() {
  outb(0x70, 0x0A);
  return (inb(0x71) & 0x80);
}

static unsigned char read_rtc_register(int reg) {
  outb(0x70, reg);
  return inb(0x71);
}

static int rtc_is_bcd() {
  unsigned char regB = read_rtc_register(0x0B);
  return !(regB & 0x04);
}

static unsigned int bcd_to_binary(unsigned int val) {
  return ((val & 0xF0) >> 4) * 10 + (val & 0x0F);
}

static void get_rtc_time(int *sec, int *min, int *hour, int *day, int *month,
                         int *year) {
  while (is_rtc_updating())
    ;

  *sec = read_rtc_register(0x00);
  *min = read_rtc_register(0x02);
  *hour = read_rtc_register(0x04);
  *day = read_rtc_register(0x07);
  *month = read_rtc_register(0x08);
  *year = read_rtc_register(0x09);

  if (rtc_is_bcd()) {
    *sec = bcd_to_binary(*sec);
    *min = bcd_to_binary(*min);
    *hour = bcd_to_binary(*hour);
    *day = bcd_to_binary(*day);
    *month = bcd_to_binary(*month);
    *year = bcd_to_binary(*year);
  }

  unsigned char regB = read_rtc_register(0x0B);
  if (!(regB & 0x02) && (*hour & 0x80)) {
    *hour = ((*hour & 0x7F) + 12) % 24;
  }
}

// Simulated Memory Manager Core
#define MAX_MEM_BLOCKS 64
#define SIMULATED_RAM_SIZE (64 * 1024 * 1024) // 64 MB
#define SIMULATED_RAM_START 0x2000000         // 32 MB

struct mem_block {
  unsigned int addr;
  unsigned int size;
  int is_allocated;
  int id;
};

static struct mem_block mem_blocks[MAX_MEM_BLOCKS];
static int mem_block_count = 0;
static int next_alloc_id = 1;

void init_memory_manager() {
  mem_blocks[0].addr = SIMULATED_RAM_START;
  mem_blocks[0].size = SIMULATED_RAM_SIZE;
  mem_blocks[0].is_allocated = 0;
  mem_blocks[0].id = 0;
  mem_block_count = 1;
  next_alloc_id = 1;
}

int sim_malloc(unsigned int size, unsigned int *out_addr) {
  if (size == 0)
    return 0;
  size = (size + 3) & ~3; // 4-byte align size

  for (int i = 0; i < mem_block_count; i++) {
    if (!mem_blocks[i].is_allocated && mem_blocks[i].size >= size) {
      unsigned int remaining = mem_blocks[i].size - size;

      if (remaining > 0 && mem_block_count < MAX_MEM_BLOCKS) {
        // Shift subsequent blocks right
        for (int j = mem_block_count; j > i + 1; j--) {
          mem_blocks[j] = mem_blocks[j - 1];
        }

        // Set up the next free block
        mem_blocks[i + 1].addr = mem_blocks[i].addr + size;
        mem_blocks[i + 1].size = remaining;
        mem_blocks[i + 1].is_allocated = 0;
        mem_blocks[i + 1].id = 0;
        mem_block_count++;
      } else {
        // Exact fit or no metadata blocks slot available
        size = mem_blocks[i].size;
      }

      mem_blocks[i].size = size;
      mem_blocks[i].is_allocated = 1;
      mem_blocks[i].id = next_alloc_id++;
      *out_addr = mem_blocks[i].addr;
      return mem_blocks[i].id;
    }
  }
  return 0; // Allocation failure
}

int sim_free(int id) {
  for (int i = 0; i < mem_block_count; i++) {
    if (mem_blocks[i].is_allocated && mem_blocks[i].id == id) {
      mem_blocks[i].is_allocated = 0;
      mem_blocks[i].id = 0;

      // Merge with next block if it is free
      if (i < mem_block_count - 1 && !mem_blocks[i + 1].is_allocated) {
        mem_blocks[i].size += mem_blocks[i + 1].size;
        for (int j = i + 1; j < mem_block_count - 1; j++) {
          mem_blocks[j] = mem_blocks[j + 1];
        }
        mem_block_count--;
      }
      // Merge with previous block if it is free
      if (i > 0 && !mem_blocks[i - 1].is_allocated) {
        mem_blocks[i - 1].size += mem_blocks[i].size;
        for (int j = i; j < mem_block_count - 1; j++) {
          mem_blocks[j] = mem_blocks[j + 1];
        }
        mem_block_count--;
      }
      return 1; // Released successfully
    }
  }
  return 0; // ID not found
}

int get_last_allocated_id() {
  int max_id = 0;
  for (int i = 0; i < mem_block_count; i++) {
    if (mem_blocks[i].is_allocated && mem_blocks[i].id > max_id) {
      max_id = mem_blocks[i].id;
    }
  }
  return max_id;
}

void get_mem_stats(unsigned int *used, unsigned int *free) {
  *used = 0;
  *free = 0;
  for (int i = 0; i < mem_block_count; i++) {
    if (mem_blocks[i].is_allocated) {
      *used += mem_blocks[i].size;
    } else {
      *free += mem_blocks[i].size;
    }
  }
}

// Visual Memory Map Layout (1 slot = 1 MB)
void draw_visual_memmap() {
  term_print("Visual Map (1 char = 1 MB, '#'=Allocated, '.'=Free):\n");
  term_print("[");
  for (int m = 0; m < 64; m++) {
    unsigned int addr = SIMULATED_RAM_START + m * 1024 * 1024;
    int allocated = 0;
    for (int i = 0; i < mem_block_count; i++) {
      if (mem_blocks[i].is_allocated) {
        if (addr >= mem_blocks[i].addr &&
            addr < mem_blocks[i].addr + mem_blocks[i].size) {
          allocated = 1;
          break;
        }
      }
    }
    if (allocated) {
      term_print_color("#", 0x0C); // Red '#' for allocated
    } else {
      term_print_color(".", 0x0A); // Green '.' for free
    }
  }
  term_print("]\n\n");

  // Text block details
  term_print("Detailed segments:\n");
  for (int i = 0; i < mem_block_count; i++) {
    term_print("  Address: 0x");
    print_hex(mem_blocks[i].addr);
    term_print(" - Size: ");
    print_size(mem_blocks[i].size);
    if (mem_blocks[i].is_allocated) {
      term_print_color(" [ALLOCATED, ID: ", 0x0C);
      print_int(mem_blocks[i].id);
      term_print_color("]\n", 0x0C);
    } else {
      term_print_color(" [FREE]\n", 0x0A);
    }
  }
}

static void run_mem() {
  unsigned int used = 0, free = 0;
  get_mem_stats(&used, &free);
  term_print("Total Memory : ");
  print_size(SIMULATED_RAM_SIZE);
  term_print("\nUsed Memory  : ");
  print_size(used);
  term_print("\nFree Memory  : ");
  print_size(free);
  term_print("\n");
}

// Simulated Process Manager Core
enum proc_state { READY, RUNNING, WAITING, TERMINATED };

struct process {
  int pid;
  char name[32];
  int priority;
  enum proc_state state;
  int cpu_time;
  int burst_time;
  int arrival_time;
  int remaining_time;
};

#define MAX_PROCESSES 32
static struct process proc_table[MAX_PROCESSES];
static int proc_count = 0;
static int next_pid = 1;

static const char *get_state_str(enum proc_state state) {
  switch (state) {
  case READY:
    return "READY";
  case RUNNING:
    return "RUNN";
  case WAITING:
    return "WAIT";
  case TERMINATED:
    return "TERM";
  default:
    return "UNKN";
  }
}

static void add_process_silent(const char *name, enum proc_state state,
                               int priority, int cpu_time) {
  if (proc_count >= MAX_PROCESSES)
    return;
  int slot = proc_count;
  proc_table[slot].pid = next_pid++;
  int i = 0;
  while (name[i] != '\0' && i < 31) {
    proc_table[slot].name[i] = name[i];
    i++;
  }
  proc_table[slot].name[i] = '\0';
  proc_table[slot].priority = priority;
  proc_table[slot].state = state;
  proc_table[slot].cpu_time = cpu_time;

  // Varying burst_time deterministically
  proc_table[slot].burst_time = (proc_table[slot].pid * 3) % 10 + 4;
  proc_table[slot].arrival_time = 0;
  proc_table[slot].remaining_time = proc_table[slot].burst_time;

  proc_count++;
}

void init_process_manager() {
  proc_count = 0;
  next_pid = 1;
  add_process_silent("idle", READY, 1, 100);
  add_process_silent("shell", RUNNING, 5, 25);
  add_process_silent("kworker", READY, 3, 10);
}

static void print_process_row(int pid, const char *name, const char *state_str,
                              int prio, int cpu) {
  // PID (4 chars)
  print_int(pid);
  int pid_len = (pid >= 1000) ? 4 : ((pid >= 100) ? 3 : ((pid >= 10) ? 2 : 1));
  for (int i = pid_len; i < 4; i++) {
    term_putc_color(' ', shell_text_color);
  }

  term_print("|");

  // NAME (6 chars: 1 space + name up to 4 chars + spaces to pad to 4 + 1 space)
  term_putc_color(' ', shell_text_color);
  int name_len = 0;
  while (name[name_len] != '\0' && name_len < 4) {
    term_putc_color(name[name_len], shell_text_color);
    name_len++;
  }
  for (int i = name_len; i < 4; i++) {
    term_putc_color(' ', shell_text_color);
  }
  term_putc_color(' ', shell_text_color);

  term_print("|");

  // STATE (7 chars: 1 space + state up to 5 chars + spaces to pad to 5 + 1
  // space)
  term_putc_color(' ', shell_text_color);
  int state_len = 0;
  while (state_str[state_len] != '\0' && state_len < 5) {
    term_putc_color(state_str[state_len], shell_text_color);
    state_len++;
  }
  for (int i = state_len; i < 5; i++) {
    term_putc_color(' ', shell_text_color);
  }
  term_putc_color(' ', shell_text_color);

  term_print("|");

  // PRIO (6 chars: 1 space + prio + spaces to pad to 4 + 1 space)
  term_putc_color(' ', shell_text_color);
  print_int(prio);
  int prio_len =
      (prio >= 1000) ? 4 : ((prio >= 100) ? 3 : ((prio >= 10) ? 2 : 1));
  for (int i = prio_len; i < 4; i++) {
    term_putc_color(' ', shell_text_color);
  }
  term_putc_color(' ', shell_text_color);

  term_print("| ");

  // CPU
  print_int(cpu);
  term_print("\n");
}

static void run_ps() {
  term_print("PID | NAME | STATE | PRIO | CPU\n");
  term_print("----|------|-------|------|----\n");
  for (int i = 0; i < proc_count; i++) {
    print_process_row(proc_table[i].pid, proc_table[i].name,
                      get_state_str(proc_table[i].state),
                      proc_table[i].priority, proc_table[i].cpu_time);
  }
}

static void run_new_proc(const char *args) {
  int i = 0;
  while (args[i] == ' ')
    i++;
  if (args[i] == '\0') {
    term_print("Usage: new <name>\n");
    return;
  }

  if (proc_count >= MAX_PROCESSES) {
    term_print("Error: Process table full (max 32 processes).\n");
    return;
  }

  int slot = proc_count;
  proc_table[slot].pid = next_pid++;

  int j = 0;
  while (args[i] != '\0' && args[i] != ' ' && j < 31) {
    proc_table[slot].name[j++] = args[i++];
  }
  proc_table[slot].name[j] = '\0';

  proc_table[slot].priority = 5;
  proc_table[slot].state = READY;
  proc_table[slot].cpu_time = 0;
  proc_table[slot].burst_time = (proc_table[slot].pid * 3) % 10 + 4;
  proc_table[slot].arrival_time = 0;
  proc_table[slot].remaining_time = proc_table[slot].burst_time;

  proc_count++;

  term_print("Created process ");
  term_print(proc_table[slot].name);
  term_print(" (PID: ");
  print_int(proc_table[slot].pid);
  term_print(").\n");
}

static void run_run_proc(const char *args) {
  int i = 0;
  while (args[i] == ' ')
    i++;
  if (args[i] == '\0') {
    term_print("Usage: run <pid>\n");
    return;
  }

  int pid = atoi(args + i);
  for (int idx = 0; idx < proc_count; idx++) {
    if (proc_table[idx].pid == pid) {
      if (proc_table[idx].state == TERMINATED) {
        term_print("Error: Cannot run a terminated process.\n");
        return;
      }

      proc_table[idx].state = RUNNING;
      term_print("Process ");
      print_int(pid);
      term_print(" is RUNNING...\n");

      delay_ms(100);
      proc_table[idx].cpu_time += 5;

      proc_table[idx].state = READY;
      term_print("Execution finished. CPU Time: ");
      print_int(proc_table[idx].cpu_time);
      term_print("\n");
      return;
    }
  }
  term_print("Error: Process ID ");
  print_int(pid);
  term_print(" not found.\n");
}

static void run_kill_proc(const char *args) {
  int i = 0;
  while (args[i] == ' ')
    i++;
  if (args[i] == '\0') {
    term_print("Usage: kill <pid>\n");
    return;
  }

  int pid = atoi(args + i);
  for (int idx = 0; idx < proc_count; idx++) {
    if (proc_table[idx].pid == pid) {
      proc_table[idx].state = TERMINATED;
      term_print("Process ");
      print_int(pid);
      term_print(" terminated.\n");
      return;
    }
  }
  term_print("Error: Process ID ");
  print_int(pid);
  term_print(" not found.\n");
}

static int has_key() { return (inb(0x64) & 1); }

// System Monitor commands moved to the bottom of the file

// CPU Scheduling Simulator Core
struct timeline_slot {
  int pid;
  char name[32];
  int start_time;
  int end_time;
};

#define MAX_TIMELINE_SLOTS 512
static struct timeline_slot timeline[MAX_TIMELINE_SLOTS];
static int timeline_count = 0;

static void add_timeline_slot(int pid, const char *name, int start, int end) {
  if (timeline_count >= MAX_TIMELINE_SLOTS)
    return;
  timeline[timeline_count].pid = pid;
  int i = 0;
  while (name[i] != '\0' && i < 31) {
    timeline[timeline_count].name[i] = name[i];
    i++;
  }
  timeline[timeline_count].name[i] = '\0';
  timeline[timeline_count].start_time = start;
  timeline[timeline_count].end_time = end;
  timeline_count++;
}

static void print_gantt_chart() {
  term_print("Gantt Chart:\n");
  for (int i = 0; i < timeline_count; i++) {
    term_print("| ");
    term_print(timeline[i].name);
    term_print(" ");
  }
  if (timeline_count > 0) {
    term_print("|\n\n");
  } else {
    term_print("\n");
  }

  term_print("Time: ");
  if (timeline_count > 0) {
    print_int(timeline[0].start_time);
    for (int i = 0; i < timeline_count; i++) {
      term_print(" ");
      print_int(timeline[i].end_time);
    }
  }
  term_print("\n\n");
}

struct sched_process {
  int pid;
  char name[32];
  int burst_time;
  int priority;
  int arrival_time;
  int remaining_time;
  int completion_time;
  int waiting_time;
  int turnaround_time;
  int is_completed;
};

static struct sched_process last_jobs[32];
static int last_job_count = 0;
static char last_algo[64] = "";
static int last_quantum = 0;
static struct timeline_slot last_timeline[MAX_TIMELINE_SLOTS];
static int last_timeline_count = 0;
static int has_last_results = 0;

static void run_scheduler(const char *algo, int quantum) {
  static struct sched_process jobs[32];
  int job_count = 0;
  for (int i = 0; i < proc_count; i++) {
    if (proc_table[i].state == READY) {
      jobs[job_count].pid = proc_table[i].pid;
      int n = 0;
      while (proc_table[i].name[n] != '\0' && n < 31) {
        jobs[job_count].name[n] = proc_table[i].name[n];
        n++;
      }
      jobs[job_count].name[n] = '\0';
      jobs[job_count].burst_time = proc_table[i].burst_time;
      jobs[job_count].priority = proc_table[i].priority;
      jobs[job_count].arrival_time = proc_table[i].arrival_time;
      jobs[job_count].remaining_time = proc_table[i].burst_time;
      jobs[job_count].is_completed = 0;
      jobs[job_count].completion_time = 0;
      jobs[job_count].waiting_time = 0;
      jobs[job_count].turnaround_time = 0;
      job_count++;
    }
  }

  if (job_count == 0) {
    term_print("Error: No processes in READY state to schedule.\n");
    return;
  }

  term_print("---------------------------------\n");
  term_print("Algorithm: ");
  term_print(algo);
  if (strcmp(algo, "Round Robin") == 0) {
    term_print(" (Quantum: ");
    print_int(quantum);
    term_print(")");
  }
  term_print("\n---------------------------------\n\n");

  int current_time = 0;
  timeline_count = 0;

  if (strcmp(algo, "FCFS") == 0) {
    for (int i = 0; i < job_count; i++) {
      int start = current_time;
      current_time += jobs[i].burst_time;
      add_timeline_slot(jobs[i].pid, jobs[i].name, start, current_time);

      jobs[i].completion_time = current_time;
      jobs[i].turnaround_time = jobs[i].completion_time - jobs[i].arrival_time;
      jobs[i].waiting_time = jobs[i].turnaround_time - jobs[i].burst_time;
    }
  } else if (strcmp(algo, "SJF") == 0) {
    int completed = 0;
    while (completed < job_count) {
      int best_idx = -1;
      int min_burst = 999999;
      for (int i = 0; i < job_count; i++) {
        if (!jobs[i].is_completed && jobs[i].arrival_time <= current_time) {
          if (jobs[i].burst_time < min_burst) {
            min_burst = jobs[i].burst_time;
            best_idx = i;
          }
        }
      }
      if (best_idx == -1) {
        current_time++;
      } else {
        int start = current_time;
        current_time += jobs[best_idx].burst_time;
        add_timeline_slot(jobs[best_idx].pid, jobs[best_idx].name, start,
                          current_time);

        jobs[best_idx].completion_time = current_time;
        jobs[best_idx].turnaround_time =
            jobs[best_idx].completion_time - jobs[best_idx].arrival_time;
        jobs[best_idx].waiting_time =
            jobs[best_idx].turnaround_time - jobs[best_idx].burst_time;
        jobs[best_idx].is_completed = 1;
        completed++;
      }
    }
  } else if (strcmp(algo, "Priority") == 0) {
    int completed = 0;
    while (completed < job_count) {
      int best_idx = -1;
      int max_prio = -999999;
      for (int i = 0; i < job_count; i++) {
        if (!jobs[i].is_completed && jobs[i].arrival_time <= current_time) {
          if (jobs[i].priority > max_prio) {
            max_prio = jobs[i].priority;
            best_idx = i;
          }
        }
      }
      if (best_idx == -1) {
        current_time++;
      } else {
        int start = current_time;
        current_time += jobs[best_idx].burst_time;
        add_timeline_slot(jobs[best_idx].pid, jobs[best_idx].name, start,
                          current_time);

        jobs[best_idx].completion_time = current_time;
        jobs[best_idx].turnaround_time =
            jobs[best_idx].completion_time - jobs[best_idx].arrival_time;
        jobs[best_idx].waiting_time =
            jobs[best_idx].turnaround_time - jobs[best_idx].burst_time;
        jobs[best_idx].is_completed = 1;
        completed++;
      }
    }
  } else if (strcmp(algo, "Round Robin") == 0) {
    int completed = 0;
    static int queue[512];
    int head = 0;
    int tail = 0;
    int in_queue[32] = {0};

    for (int i = 0; i < job_count; i++) {
      if (jobs[i].arrival_time <= current_time) {
        if (tail < 512) {
          queue[tail++] = i;
          in_queue[i] = 1;
        }
      }
    }

    while (completed < job_count) {
      if (head == tail) {
        current_time++;
        for (int i = 0; i < job_count; i++) {
          if (!jobs[i].is_completed && !in_queue[i] &&
              jobs[i].arrival_time <= current_time) {
            if (tail < 512) {
              queue[tail++] = i;
              in_queue[i] = 1;
            }
          }
        }
        continue;
      }

      int curr_idx = queue[head++];
      int run_time = (jobs[curr_idx].remaining_time > quantum)
                         ? quantum
                         : jobs[curr_idx].remaining_time;

      int start = current_time;
      current_time += run_time;
      jobs[curr_idx].remaining_time -= run_time;

      add_timeline_slot(jobs[curr_idx].pid, jobs[curr_idx].name, start,
                        current_time);

      for (int i = 0; i < job_count; i++) {
        if (!jobs[i].is_completed && !in_queue[i] && i != curr_idx &&
            jobs[i].arrival_time <= current_time) {
          if (tail < 512) {
            queue[tail++] = i;
            in_queue[i] = 1;
          }
        }
      }

      if (jobs[curr_idx].remaining_time == 0) {
        jobs[curr_idx].completion_time = current_time;
        jobs[curr_idx].turnaround_time =
            jobs[curr_idx].completion_time - jobs[curr_idx].arrival_time;
        jobs[curr_idx].waiting_time =
            jobs[curr_idx].turnaround_time - jobs[curr_idx].burst_time;
        jobs[curr_idx].is_completed = 1;
        completed++;
      } else {
        if (tail < 512) {
          queue[tail++] = curr_idx;
        }
      }
    }
  }

  // Print Gantt Chart
  print_gantt_chart();

  // Print metrics table
  term_print("PID | WT | TAT\n");
  term_print("----|----|-----\n");
  int total_wt = 0;
  int total_tat = 0;
  for (int i = 0; i < job_count; i++) {
    print_int(jobs[i].pid);
    int pid_len =
        (jobs[i].pid >= 1000)
            ? 4
            : ((jobs[i].pid >= 100) ? 3 : ((jobs[i].pid >= 10) ? 2 : 1));
    for (int k = pid_len; k < 4; k++) {
      term_putc_color(' ', shell_text_color);
    }
    term_print("|");

    term_putc_color(' ', shell_text_color);
    print_int(jobs[i].waiting_time);
    int wt_len = (jobs[i].waiting_time >= 1000)
                     ? 4
                     : ((jobs[i].waiting_time >= 100)
                            ? 3
                            : ((jobs[i].waiting_time >= 10) ? 2 : 1));
    for (int k = wt_len; k < 2; k++) {
      term_putc_color(' ', shell_text_color);
    }
    term_putc_color(' ', shell_text_color);
    term_print("| ");

    print_int(jobs[i].turnaround_time);
    term_print("\n");

    total_wt += jobs[i].waiting_time;
    total_tat += jobs[i].turnaround_time;
  }
  term_print("\n");

  int avg_wt_whole = total_wt / job_count;
  int avg_wt_frac = ((total_wt * 10) / job_count) % 10;
  int avg_tat_whole = total_tat / job_count;
  int avg_tat_frac = ((total_tat * 10) / job_count) % 10;

  term_print("Average Waiting Time: ");
  print_int(avg_wt_whole);
  term_print(".");
  print_int(avg_wt_frac);
  term_print("\n");

  term_print("Average Turnaround Time: ");
  print_int(avg_tat_whole);
  term_print(".");
  print_int(avg_tat_frac);
  term_print("\n");

  // Save last results for Gantt visualization
  last_job_count = job_count;
  for (int i = 0; i < job_count; i++) {
    last_jobs[i] = jobs[i];
  }
  vfs_strcpy(last_algo, algo);
  last_quantum = quantum;
  last_timeline_count = timeline_count;
  for (int i = 0; i < timeline_count; i++) {
    last_timeline[i] = timeline[i];
  }
  has_last_results = 1;
}

// Virtual File System (VFS) Simulation
#ifndef NULL
#define NULL ((void *)0)
#endif

#define MAX_VFS_NODES 256
#define MAX_CHILDREN 32
#define MAX_FILE_CONTENT 1024

enum vfs_type { VFS_FILE = 0, VFS_DIR = 1 };

struct vfs_node {
  char name[64];
  int type; // VFS_FILE or VFS_DIR
  char content[MAX_FILE_CONTENT];
  struct vfs_node *children[MAX_CHILDREN];
  int child_count;
  struct vfs_node *parent;
  int is_used;
};

static struct vfs_node vfs_pool[MAX_VFS_NODES];
static struct vfs_node *vfs_root = NULL;
static struct vfs_node *vfs_cwd = NULL;

static int vfs_strlen(const char *s) {
  int len = 0;
  while (s[len] != '\0')
    len++;
  return len;
}

static void vfs_strcpy(char *dest, const char *src) {
  int i = 0;
  while (src[i] != '\0') {
    dest[i] = src[i];
    i++;
  }
  dest[i] = '\0';
}

static void vfs_strncpy(char *dest, const char *src, int n) {
  int i = 0;
  while (i < n && src[i] != '\0') {
    dest[i] = src[i];
    i++;
  }
  while (i < n) {
    dest[i] = '\0';
    i++;
  }
}

static struct vfs_node *allocate_node(const char *name, int type) {
  for (int i = 0; i < MAX_VFS_NODES; i++) {
    if (!vfs_pool[i].is_used) {
      vfs_pool[i].is_used = 1;
      vfs_strncpy(vfs_pool[i].name, name, 63);
      vfs_pool[i].name[63] = '\0';
      vfs_pool[i].type = type;
      vfs_pool[i].content[0] = '\0';
      vfs_pool[i].child_count = 0;
      vfs_pool[i].parent = NULL;
      for (int j = 0; j < MAX_CHILDREN; j++) {
        vfs_pool[i].children[j] = NULL;
      }
      return &vfs_pool[i];
    }
  }
  return NULL;
}

static void free_node_recursive(struct vfs_node *node) {
  if (node == NULL)
    return;

  if (node->type == VFS_DIR) {
    for (int i = 0; i < node->child_count; i++) {
      free_node_recursive(node->children[i]);
      node->children[i] = NULL;
    }
    node->child_count = 0;
  }

  node->is_used = 0;
  node->name[0] = '\0';
  node->content[0] = '\0';
  node->parent = NULL;
}

void init_vfs() {
  for (int i = 0; i < MAX_VFS_NODES; i++) {
    vfs_pool[i].is_used = 0;
  }
  vfs_root = allocate_node("/", VFS_DIR);
  vfs_cwd = vfs_root;

  // Initialize standard directories
  struct vfs_node *home = allocate_node("home", VFS_DIR);
  if (home) {
    home->parent = vfs_root;
    vfs_root->children[vfs_root->child_count++] = home;

    struct vfs_node *documents = allocate_node("documents", VFS_DIR);
    if (documents) {
      documents->parent = home;
      home->children[home->child_count++] = documents;
    }
    struct vfs_node *downloads = allocate_node("downloads", VFS_DIR);
    if (downloads) {
      downloads->parent = home;
      home->children[home->child_count++] = downloads;
    }
    struct vfs_node *desktop = allocate_node("desktop", VFS_DIR);
    if (desktop) {
      desktop->parent = home;
      home->children[home->child_count++] = desktop;
    }
  }
  struct vfs_node *kernel = allocate_node("kernel", VFS_DIR);
  if (kernel) {
    kernel->parent = vfs_root;
    vfs_root->children[vfs_root->child_count++] = kernel;
  }
  struct vfs_node *etc = allocate_node("etc", VFS_DIR);
  if (etc) {
    etc->parent = vfs_root;
    vfs_root->children[vfs_root->child_count++] = etc;
  }

  // Initialize assets directory and logo.png file
  struct vfs_node *assets_dir = allocate_node("assets", VFS_DIR);
  if (assets_dir) {
    assets_dir->parent = vfs_root;
    vfs_root->children[vfs_root->child_count++] = assets_dir;

    struct vfs_node *logo_file = allocate_node("logo.png", VFS_FILE);
    if (logo_file) {
      logo_file->parent = assets_dir;
      assets_dir->children[assets_dir->child_count++] = logo_file;
      vfs_strcpy(logo_file->content, "PNG Image Data");
    }
  }
}

static int get_next_token(const char *path, int start_idx, char *token) {
  int i = start_idx;
  while (path[i] == '/')
    i++;
  if (path[i] == '\0')
    return -1;

  int t_idx = 0;
  while (path[i] != '\0' && path[i] != '/') {
    if (t_idx < 63) {
      token[t_idx++] = path[i];
    }
    i++;
  }
  token[t_idx] = '\0';
  return i;
}

static struct vfs_node *resolve_path(const char *path) {
  if (path == NULL || path[0] == '\0')
    return vfs_cwd;

  struct vfs_node *current = vfs_cwd;
  int idx = 0;
  if (path[0] == '/') {
    current = vfs_root;
    idx = 1;
  }

  char token[64];
  while (1) {
    idx = get_next_token(path, idx, token);
    if (idx == -1)
      break;

    if (strcmp(token, ".") == 0) {
      continue;
    }
    if (strcmp(token, "..") == 0) {
      if (current->parent != NULL) {
        current = current->parent;
      }
      continue;
    }

    int found = 0;
    for (int i = 0; i < current->child_count; i++) {
      if (strcmp(current->children[i]->name, token) == 0) {
        current = current->children[i];
        found = 1;
        break;
      }
    }

    if (!found) {
      return NULL;
    }
  }
  return current;
}

static void get_absolute_path(struct vfs_node *node, char *buf) {
  if (node == vfs_root) {
    vfs_strcpy(buf, "/");
    return;
  }

  struct vfs_node *path_nodes[32];
  int count = 0;
  struct vfs_node *curr = node;

  while (curr != NULL && curr != vfs_root && count < 32) {
    path_nodes[count++] = curr;
    curr = curr->parent;
  }

  buf[0] = '\0';
  for (int i = count - 1; i >= 0; i--) {
    int len = vfs_strlen(buf);
    buf[len] = '/';
    buf[len + 1] = '\0';
    vfs_strcpy(buf + len + 1, path_nodes[i]->name);
  }
}

static void print_tree_recursive(struct vfs_node *node, int depth,
                                 int is_last_mask) {
  if (node == NULL)
    return;

  for (int d = 0; d < depth - 1; d++) {
    if (is_last_mask & (1 << d)) {
      term_print("    ");
    } else {
      char line_str[5] = {(char)0xB3, ' ', ' ', ' ', 0};
      term_print(line_str);
    }
  }

  if (depth > 0) {
    if (is_last_mask & (1 << (depth - 1))) {
      char branch_str[5] = {(char)0xC0, (char)0xC4, (char)0xC4, ' ', 0};
      term_print(branch_str);
    } else {
      char branch_str[5] = {(char)0xC3, (char)0xC4, (char)0xC4, ' ', 0};
      term_print(branch_str);
    }
  }

  term_print(node == vfs_root ? "/" : node->name);
  term_print("\n");

  if (node->type == VFS_DIR) {
    for (int i = 0; i < node->child_count; i++) {
      int next_last_mask = is_last_mask;
      if (i == node->child_count - 1) {
        next_last_mask |= (1 << depth);
      } else {
        next_last_mask &= ~(1 << depth);
      }
      print_tree_recursive(node->children[i], depth + 1, next_last_mask);
    }
  }
}

static void run_vfs_ls() {
  for (int i = 0; i < vfs_cwd->child_count; i++) {
    struct vfs_node *child = vfs_cwd->children[i];
    if (child->type == VFS_DIR) {
      term_print_color(child->name, 0x09);
      term_print_color("/\n", 0x09);
    } else {
      term_print(child->name);
      term_print("\n");
    }
  }
}

static void run_vfs_cd(const char *path) {
  if (path == NULL || path[0] == '\0') {
    vfs_cwd = vfs_root;
    return;
  }

  struct vfs_node *target = resolve_path(path);
  if (target == NULL || target->type != VFS_DIR) {
    term_print("Error: Directory not found\n");
    return;
  }
  vfs_cwd = target;
}

static void run_vfs_mkdir(const char *name) {
  if (name == NULL || name[0] == '\0') {
    term_print("Usage: mkdir <name>\n");
    return;
  }

  int i = 0;
  while (name[i] == ' ')
    i++;
  if (name[i] == '\0') {
    term_print("Usage: mkdir <name>\n");
    return;
  }

  char dir_name[256];
  int d_idx = 0;
  while (name[i] != '\0' && name[i] != ' ') {
    if (d_idx < 255) {
      dir_name[d_idx++] = name[i];
    }
    i++;
  }
  dir_name[d_idx] = '\0';

  struct vfs_node *parent = vfs_cwd;
  char *base_name = dir_name;
  int last_slash = -1;
  for (int k = 0; dir_name[k] != '\0'; k++) {
    if (dir_name[k] == '/')
      last_slash = k;
  }
  if (last_slash != -1) {
    char parent_path[256];
    if (last_slash == 0) {
      vfs_strcpy(parent_path, "/");
    } else {
      vfs_strncpy(parent_path, dir_name, last_slash);
      parent_path[last_slash] = '\0';
    }
    parent = resolve_path(parent_path);
    base_name = dir_name + last_slash + 1;
  }

  if (parent == NULL || parent->type != VFS_DIR) {
    term_print("Error: Parent directory not found\n");
    return;
  }

  for (int j = 0; j < parent->child_count; j++) {
    if (strcmp(parent->children[j]->name, base_name) == 0) {
      term_print("Error: Directory already exists\n");
      return;
    }
  }

  if (parent->child_count >= MAX_CHILDREN) {
    term_print("Error: Directory limit reached\n");
    return;
  }

  struct vfs_node *new_node = allocate_node(base_name, VFS_DIR);
  if (new_node == NULL) {
    term_print("Error: No space left on system\n");
    return;
  }

  new_node->parent = parent;
  parent->children[parent->child_count++] = new_node;
  term_print("Directory created\n");
}

static void run_vfs_touch(const char *name) {
  if (name == NULL || name[0] == '\0') {
    term_print("Usage: touch <name>\n");
    return;
  }

  int i = 0;
  while (name[i] == ' ')
    i++;
  if (name[i] == '\0') {
    term_print("Usage: touch <name>\n");
    return;
  }

  char file_name[256];
  int f_idx = 0;
  while (name[i] != '\0' && name[i] != ' ') {
    if (f_idx < 255) {
      file_name[f_idx++] = name[i];
    }
    i++;
  }
  file_name[f_idx] = '\0';

  struct vfs_node *parent = vfs_cwd;
  char *base_name = file_name;
  int last_slash = -1;
  for (int k = 0; file_name[k] != '\0'; k++) {
    if (file_name[k] == '/')
      last_slash = k;
  }
  if (last_slash != -1) {
    char parent_path[256];
    if (last_slash == 0) {
      vfs_strcpy(parent_path, "/");
    } else {
      vfs_strncpy(parent_path, file_name, last_slash);
      parent_path[last_slash] = '\0';
    }
    parent = resolve_path(parent_path);
    base_name = file_name + last_slash + 1;
  }

  if (parent == NULL || parent->type != VFS_DIR) {
    term_print("Error: Parent directory not found\n");
    return;
  }

  for (int j = 0; j < parent->child_count; j++) {
    if (strcmp(parent->children[j]->name, base_name) == 0) {
      term_print("Error: File or directory already exists\n");
      return;
    }
  }

  if (parent->child_count >= MAX_CHILDREN) {
    term_print("Error: Directory limit reached\n");
    return;
  }

  struct vfs_node *new_node = allocate_node(base_name, VFS_FILE);
  if (new_node == NULL) {
    term_print("Error: No space left on system\n");
    return;
  }

  new_node->parent = parent;
  parent->children[parent->child_count++] = new_node;
  term_print("File created\n");
}

static void run_vfs_cat(const char *path) {
  if (path == NULL || path[0] == '\0') {
    term_print("Usage: cat <filename>\n");
    return;
  }

  int i = 0;
  while (path[i] == ' ')
    i++;
  char filename[64];
  int f_idx = 0;
  while (path[i] != '\0' && path[i] != ' ') {
    if (f_idx < 63) {
      filename[f_idx++] = path[i];
    }
    i++;
  }
  filename[f_idx] = '\0';

  struct vfs_node *target = resolve_path(filename);
  if (target == NULL || target->type != VFS_FILE) {
    term_print("Error: File not found\n");
    return;
  }
  term_print(target->content);
  term_print("\n");
}

static void run_vfs_write(const char *args) {
  if (args == NULL || args[0] == '\0') {
    term_print("Usage: write <filename> <text>\n");
    return;
  }

  char filename[64];
  int i = 0;
  while (args[i] == ' ')
    i++;
  if (args[i] == '\0') {
    term_print("Usage: write <filename> <text>\n");
    return;
  }

  int f_idx = 0;
  while (args[i] != '\0' && args[i] != ' ') {
    if (f_idx < 63) {
      filename[f_idx++] = args[i];
    }
    i++;
  }
  filename[f_idx] = '\0';

  while (args[i] == ' ')
    i++;

  struct vfs_node *target = resolve_path(filename);
  if (target == NULL || target->type != VFS_FILE) {
    term_print("Error: File not found\n");
    return;
  }

  int c_idx = 0;
  while (args[i] != '\0' && c_idx < MAX_FILE_CONTENT - 1) {
    target->content[c_idx++] = args[i++];
  }
  target->content[c_idx] = '\0';
}

static void run_vfs_rm(const char *name) {
  if (name == NULL || name[0] == '\0') {
    term_print("Usage: rm <name>\n");
    return;
  }

  int i = 0;
  while (name[i] == ' ')
    i++;
  char target_name[64];
  int t_idx = 0;
  while (name[i] != '\0' && name[i] != ' ') {
    if (t_idx < 63) {
      target_name[t_idx++] = name[i];
    }
    i++;
  }
  target_name[t_idx] = '\0';

  struct vfs_node *target = resolve_path(target_name);
  if (target == NULL || target == vfs_root) {
    term_print("Error: File or directory not found\n");
    return;
  }

  struct vfs_node *parent = target->parent;
  if (parent == NULL) {
    term_print("Error: Cannot delete node\n");
    return;
  }

  int found_idx = -1;
  for (int j = 0; j < parent->child_count; j++) {
    if (parent->children[j] == target) {
      found_idx = j;
      break;
    }
  }

  if (found_idx != -1) {
    for (int j = found_idx; j < parent->child_count - 1; j++) {
      parent->children[j] = parent->children[j + 1];
    }
    parent->child_count--;
  }

  free_node_recursive(target);
  term_print("Removed\n");
}

// Shell Commands
static void run_calc(const char *args) {
  int i = 0;
  while (args[i] == ' ')
    i++;

  if (args[i] == '\0') {
    term_print("Usage: calc <num1> <op> <num2>\nExample: calc 15 + 3\n");
    return;
  }

  int sign1 = 1;
  if (args[i] == '-') {
    sign1 = -1;
    i++;
  } else if (args[i] == '+') {
    i++;
  }

  int num1 = 0;
  int has_num1 = 0;
  while (args[i] >= '0' && args[i] <= '9') {
    num1 = num1 * 10 + (args[i] - '0');
    has_num1 = 1;
    i++;
  }
  num1 *= sign1;

  while (args[i] == ' ')
    i++;

  char op = args[i];
  if (op != '+' && op != '-' && op != '*' && op != '/') {
    term_print("Error: Invalid operator. Supported operators: +, -, *, /\n");
    return;
  }
  i++;

  while (args[i] == ' ')
    i++;

  int sign2 = 1;
  if (args[i] == '-') {
    sign2 = -1;
    i++;
  } else if (args[i] == '+') {
    i++;
  }

  int num2 = 0;
  int has_num2 = 0;
  while (args[i] >= '0' && args[i] <= '9') {
    num2 = num2 * 10 + (args[i] - '0');
    has_num2 = 1;
    i++;
  }
  num2 *= sign2;

  if (!has_num1 || !has_num2) {
    term_print("Error: Invalid numbers.\n");
    return;
  }

  int result = 0;
  if (op == '+') {
    result = num1 + num2;
  } else if (op == '-') {
    result = num1 - num2;
  } else if (op == '*') {
    result = num1 * num2;
  } else if (op == '/') {
    if (num2 == 0) {
      term_print("Error: Division by zero.\n");
      return;
    }
    result = num1 / num2;
  }

  print_int(num1);
  term_print(" ");
  term_putc_color(op, shell_text_color);
  term_print(" ");
  print_int(num2);
  term_print(" = ");
  print_int(result);
  term_print("\n");
}

static void reboot() {
  outb(0x64, 0xFE);
  while (1) {
    __asm__ volatile("hlt");
  }
}

static void shutdown() {
  outw(0x604, 0x2000);  // QEMU newer (ACPI)
  outw(0xB004, 0x2000); // QEMU older / Bochs BIOS
  outw(0x4004, 0x3400); // VirtualBox
  term_print(
      "Shutdown request sent. Close QEMU manually if it remains open.\n");
  while (1) {
    __asm__ volatile("hlt");
  }
}

static void run_color(const char *args) {
  int i = 0;
  while (args[i] == ' ')
    i++;
  if (args[i] == '\0') {
    term_print("Usage: color <theme>\n"
               "Available themes:\n"
               "  white   : White on Black\n"
               "  green   : Bright Green on Black (Matrix)\n"
               "  red     : Red on Black\n"
               "  blue    : Blue on Black\n"
               "  yellow  : Yellow on Black\n"
               "  retro   : Amber on Black\n"
               "  dos     : White on Blue Background\n"
               "  classic : Grey on Black\n");
    return;
  }

  if (strcmp(args + i, "white") == 0) {
    shell_bg = 0x0;
    shell_fg = 0x0F;
  } else if (strcmp(args + i, "green") == 0 ||
             strcmp(args + i, "matrix") == 0) {
    shell_bg = 0x0;
    shell_fg = 0x0A;
  } else if (strcmp(args + i, "red") == 0) {
    shell_bg = 0x0;
    shell_fg = 0x0C;
  } else if (strcmp(args + i, "blue") == 0) {
    shell_bg = 0x0;
    shell_fg = 0x09;
  } else if (strcmp(args + i, "yellow") == 0) {
    shell_bg = 0x0;
    shell_fg = 0x0E;
  } else if (strcmp(args + i, "retro") == 0) {
    shell_bg = 0x0;
    shell_fg = 0x06; // Brown/Amber
  } else if (strcmp(args + i, "dos") == 0) {
    shell_bg = 0x1;  // Blue
    shell_fg = 0x0F; // White
  } else if (strcmp(args + i, "classic") == 0) {
    shell_bg = 0x0;
    shell_fg = 0x07;
  } else {
    term_print("Unknown color theme.\n");
    return;
  }

  shell_text_color = (shell_bg << 4) | shell_fg;
  term_clear();
}

static void run_memory(unsigned int magic, unsigned int mb_addr) {
  if (magic != 0x2BADB002) {
    term_print("Error: Not booted by a Multiboot-compliant loader.\n");
    return;
  }
  unsigned int *mb_info = (unsigned int *)mb_addr;
  unsigned int flags = mb_info[0];
  if (!(flags & 1)) {
    term_print("Error: Multiboot memory info not provided.\n");
    return;
  }
  unsigned int mem_lower = mb_info[1];
  unsigned int mem_upper = mb_info[2];

  term_print("Lower memory: ");
  print_int(mem_lower);
  term_print(" KiB\n");

  term_print("Upper memory: ");
  print_int(mem_upper / 1024);
  term_print(" MiB (");
  print_int(mem_upper);
  term_print(" KiB)\n");

  unsigned int total = 1024 + mem_upper;
  term_print("Total memory: ");
  print_int(total / 1024);
  term_print(" MiB\n");
}

static void run_cpu() {
  char vendor[13];
  unsigned int eax_val, ebx_val, ecx_val, edx_val;
  __asm__ volatile("cpuid"
                   : "=a"(eax_val), "=b"(ebx_val), "=c"(ecx_val), "=d"(edx_val)
                   : "a"(0));
  for (int i = 0; i < 4; i++) {
    vendor[i] = (ebx_val >> (i * 8)) & 0xFF;
    vendor[i + 4] = (edx_val >> (i * 8)) & 0xFF;
    vendor[i + 8] = (ecx_val >> (i * 8)) & 0xFF;
  }
  vendor[12] = '\0';

  term_print("CPU Vendor: ");
  term_print(vendor);
  term_print("\n");
}

static void run_ascii() {
  int count = 0;
  for (int i = 32; i <= 126; i++) {
    print_int(i);
    term_print(": [");
    term_putc_color((char)i, shell_text_color);
    term_print("]  ");
    count++;
    if (count % 6 == 0) {
      term_print("\n");
    }
  }
  if (count % 6 != 0) {
    term_print("\n");
  }
}

// System Monitor helpers and run_sysinfo moved to the bottom of the file

static void run_about() {
  term_print("==================================================\n");
  term_print("                  ABOUT MINIEDUOS                 \n");
  term_print("==================================================\n");

  // Mini logo
  term_print_color("  __  __ _       _ _____     _       ____  \n",
                   0x0E); // Yellow logo
  term_print_color(" |  \\/  (_)_ __ (_) ____| __| | _   / ___| \n", 0x0E);
  term_print_color(" | |\\/| | | '_ \\| |  _|  / _` || |  \\___ \\ \n", 0x0E);
  term_print_color(" | |  | | | | | | | |___| (_| || |___ ___) |\n", 0x0E);
  term_print_color(" |_|  |_|_|_| |_|_|_____|\\__,_| \\____|____/ \n\n", 0x0E);

  term_print("  MiniEduOS is a lightweight, bootable x86 kernel\n");
  term_print("  designed for educational and demonstration purposes.\n\n");
  term_print("  Developed for the Operating Systems Course.\n");
  term_print("==================================================\n");
}

// Center and render large block ASCII boot logo on the screen
static void draw_centered_logo() {
  // 5-line banner logo (50 chars wide)
  const char *line1 = "  __  __ _       _ _____     _       ____   ____  ";
  const char *line2 = " |  \\/  (_)_ __ (_) ____| __| | _   |  _ \\ / ___| ";
  const char *line3 = " | |\\/| | | '_ \\| |  _|  / _` || |  | | | \\___ \\  ";
  const char *line4 = " | |  | | | | | | | |___| (_| || |__| |_| |___) | ";
  const char *line5 = " |_|  |_|_|_| |_|_|_____|\\__,_| \\__/|____/|____/  ";

  int r = 5;             // Vertical alignment
  int c = (80 - 50) / 2; // Center horizontally

  for (int i = 0; line1[i] != '\0'; i++) {
    video[(r * 80 + c + i) * 2] = line1[i];
    video[(r * 80 + c + i) * 2 + 1] = 0x0B; // Cyan logo
  }
  r++;

  for (int i = 0; line2[i] != '\0'; i++) {
    video[(r * 80 + c + i) * 2] = line2[i];
    video[(r * 80 + c + i) * 2 + 1] = 0x0B;
  }
  r++;

  for (int i = 0; line3[i] != '\0'; i++) {
    video[(r * 80 + c + i) * 2] = line3[i];
    video[(r * 80 + c + i) * 2 + 1] = 0x0B;
  }
  r++;

  for (int i = 0; line4[i] != '\0'; i++) {
    video[(r * 80 + c + i) * 2] = line4[i];
    video[(r * 80 + c + i) * 2 + 1] = 0x0B;
  }
  r++;

  for (int i = 0; line5[i] != '\0'; i++) {
    video[(r * 80 + c + i) * 2] = line5[i];
    video[(r * 80 + c + i) * 2 + 1] = 0x0B;
  }

  // Subtitle
  const char *sub = "OS Lab Demonstration System - v1.0";
  int sub_len = 0;
  while (sub[sub_len] != '\0')
    sub_len++;
  int sub_c = (80 - sub_len) / 2;
  r += 2;
  for (int i = 0; sub[i] != '\0'; i++) {
    video[(r * 80 + sub_c + i) * 2] = sub[i];
    video[(r * 80 + sub_c + i) * 2 + 1] = 0x0E; // Yellow
  }
}

// Progress loading animation bar with dynamic state strings
static void show_loading_animation() {
  int r = 15;
  int start_col = (80 - 45) / 2; // Width 45 columns

  // Spinners
  const char spin[4] = {'\\', '|', '/', '-'};

  for (int pct = 0; pct <= 100; pct += 5) {
    // Render spinner and dynamic boot status messages
    char s_char = spin[(pct / 5) % 4];

    // Print message row
    int msg_r = 14;
    // Clean loading message area
    for (int x = 0; x < 80; x++) {
      video[(msg_r * 80 + x) * 2] = ' ';
      video[(msg_r * 80 + x) * 2 + 1] = 0x0F;
    }

    const char *status_msg;
    if (pct <= 20) {
      status_msg = "Booting kernel core...";
    } else if (pct <= 40) {
      status_msg = "Initializing keyboard driver...";
    } else if (pct <= 60) {
      status_msg = "Probing CMOS RTC clock...";
    } else if (pct <= 80) {
      status_msg = "Querying CPU topology...";
    } else {
      status_msg = "Entering interactive shell...";
    }

    // Print spinner + message centered
    int status_len = 0;
    while (status_msg[status_len] != '\0')
      status_len++;
    int status_c = (80 - (status_len + 4)) / 2;

    video[(msg_r * 80 + status_c) * 2] = '[';
    video[(msg_r * 80 + status_c) * 2 + 1] = 0x0F;
    video[(msg_r * 80 + status_c + 1) * 2] = s_char;
    video[(msg_r * 80 + status_c + 1) * 2 + 1] = 0x0A; // Green spinner
    video[(msg_r * 80 + status_c + 2) * 2] = ']';
    video[(msg_r * 80 + status_c + 2) * 2 + 1] = 0x0F;
    video[(msg_r * 80 + status_c + 3) * 2] = ' ';
    video[(msg_r * 80 + status_c + 3) * 2 + 1] = 0x0F;

    for (int i = 0; i < status_len; i++) {
      video[(msg_r * 80 + status_c + 4 + i) * 2] = status_msg[i];
      video[(msg_r * 80 + status_c + 4 + i) * 2 + 1] = 0x0F;
    }

    // Draw progress bar skeleton
    const char *text = "Progress... [";
    for (int i = 0; text[i] != '\0'; i++) {
      video[(r * 80 + start_col + i) * 2] = text[i];
      video[(r * 80 + start_col + i) * 2 + 1] = 0x0F;
    }
    video[(r * 80 + start_col + 13 + 20) * 2] = ']';
    video[(r * 80 + start_col + 13 + 20) * 2 + 1] = 0x0F;

    // Draw filled blocks
    int filled = pct / 5;
    for (int i = 0; i < 20; i++) {
      int col = start_col + 13 + i;
      if (i < filled) {
        video[(r * 80 + col) * 2] = '=';
        video[(r * 80 + col) * 2 + 1] = 0x0A; // Green blocks
      } else {
        video[(r * 80 + col) * 2] = ' ';
        video[(r * 80 + col) * 2 + 1] = 0x0F;
      }
    }

    // Percentage text
    int col_pct = start_col + 13 + 20 + 2;
    video[(r * 80 + col_pct) * 2] = '0' + (pct / 100);
    video[(r * 80 + col_pct + 1) * 2] = '0' + ((pct % 100) / 10);
    video[(r * 80 + col_pct + 2) * 2] = '0' + (pct % 10);
    video[(r * 80 + col_pct + 3) * 2] = '%';

    video[(r * 80 + col_pct) * 2 + 1] = 0x0E;
    video[(r * 80 + col_pct + 1) * 2 + 1] = 0x0E;
    video[(r * 80 + col_pct + 2) * 2 + 1] = 0x0E;
    video[(r * 80 + col_pct + 3) * 2 + 1] = 0x0E;

    delay_ms(80); // Smooth animation step
  }
  delay_ms(200); // Hold at 100% briefly
}

// VFS Extended Command Helpers
static void parse_two_args(const char *args, char *arg1, char *arg2) {
  int i = 0;
  while (args[i] == ' ')
    i++;
  int len1 = 0;
  while (args[i] != '\0' && args[i] != ' ') {
    if (len1 < 127) {
      arg1[len1++] = args[i];
    }
    i++;
  }
  arg1[len1] = '\0';

  while (args[i] == ' ')
    i++;
  int len2 = 0;
  while (args[i] != '\0' && args[i] != ' ') {
    if (len2 < 127) {
      arg2[len2++] = args[i];
    }
    i++;
  }
  arg2[len2] = '\0';
}

static void parse_append_args(const char *args, char *filename, char *text) {
  int i = 0;
  while (args[i] == ' ')
    i++;
  int f_len = 0;
  while (args[i] != '\0' && args[i] != ' ') {
    if (f_len < 63) {
      filename[f_len++] = args[i];
    }
    i++;
  }
  filename[f_len] = '\0';

  while (args[i] == ' ')
    i++;
  int t_len = 0;
  while (args[i] != '\0') {
    if (t_len < 511) {
      text[t_len++] = args[i];
    }
    i++;
  }
  text[t_len] = '\0';
}

static void int_to_str(int n, char *buf) {
  int i = 0;
  int is_neg = 0;
  if (n < 0) {
    is_neg = 1;
    n = -n;
  }
  if (n == 0) {
    buf[i++] = '0';
  } else {
    while (n > 0) {
      buf[i++] = '0' + (n % 10);
      n /= 10;
    }
  }
  if (is_neg)
    buf[i++] = '-';
  buf[i] = '\0';

  int len = i;
  for (int j = 0; j < len / 2; j++) {
    char temp = buf[j];
    buf[j] = buf[len - 1 - j];
    buf[len - 1 - j] = temp;
  }
}

static char recent_files[5][128];
static int recent_files_count = 0;

static void add_to_recent(const char *path) {
  if (path == NULL || path[0] == '\0')
    return;
  for (int i = 0; i < recent_files_count; i++) {
    if (strcmp(recent_files[i], path) == 0) {
      char temp[128];
      vfs_strcpy(temp, recent_files[i]);
      for (int j = i; j > 0; j--) {
        vfs_strcpy(recent_files[j], recent_files[j - 1]);
      }
      vfs_strcpy(recent_files[0], temp);
      return;
    }
  }
  int limit = recent_files_count < 5 ? recent_files_count : 4;
  for (int i = limit; i > 0; i--) {
    vfs_strcpy(recent_files[i], recent_files[i - 1]);
  }
  vfs_strncpy(recent_files[0], path, 127);
  recent_files[0][127] = '\0';
  if (recent_files_count < 5)
    recent_files_count++;
}

static char shell_history[10][128];
static int shell_history_count = 0;

static void add_to_history(const char *command) {
  if (command == NULL || command[0] == '\0')
    return;
  if (shell_history_count > 0 && strcmp(shell_history[0], command) == 0)
    return;
  int limit = shell_history_count < 10 ? shell_history_count : 9;
  for (int i = limit; i > 0; i--) {
    vfs_strcpy(shell_history[i], shell_history[i - 1]);
  }
  vfs_strncpy(shell_history[0], command, 127);
  shell_history[0][127] = '\0';
  if (shell_history_count < 10)
    shell_history_count++;
}

static char current_scheduler_algo[64] = "FCFS (Default)";

static struct vfs_node *copy_node_recursive(struct vfs_node *src,
                                            struct vfs_node *parent) {
  if (src == NULL)
    return NULL;
  struct vfs_node *new_node = allocate_node(src->name, src->type);
  if (new_node == NULL)
    return NULL;

  new_node->parent = parent;
  if (src->type == VFS_FILE) {
    vfs_strcpy(new_node->content, src->content);
  } else {
    new_node->child_count = 0;
    for (int i = 0; i < src->child_count; i++) {
      if (new_node->child_count < MAX_CHILDREN) {
        struct vfs_node *child_copy =
            copy_node_recursive(src->children[i], new_node);
        if (child_copy != NULL) {
          new_node->children[new_node->child_count++] = child_copy;
        }
      }
    }
  }
  return new_node;
}

static void run_vfs_cp(const char *src_path, const char *dest_path) {
  if (src_path == NULL || src_path[0] == '\0' || dest_path == NULL ||
      dest_path[0] == '\0') {
    term_print("Usage: cp <source> <destination>\n");
    return;
  }
  struct vfs_node *src_node = resolve_path(src_path);
  if (src_node == NULL) {
    term_print("Error: Source not found\n");
    return;
  }
  struct vfs_node *dest_node = resolve_path(dest_path);
  struct vfs_node *dest_parent = NULL;
  char dest_name[64];

  if (dest_node != NULL && dest_node->type == VFS_DIR) {
    dest_parent = dest_node;
    vfs_strcpy(dest_name, src_node->name);
  } else {
    int last_slash = -1;
    int i = 0;
    while (dest_path[i] != '\0') {
      if (dest_path[i] == '/')
        last_slash = i;
      i++;
    }
    if (last_slash == -1) {
      dest_parent = vfs_cwd;
      vfs_strcpy(dest_name, dest_path);
    } else {
      char parent_path[256];
      vfs_strncpy(parent_path, dest_path, last_slash == 0 ? 1 : last_slash);
      parent_path[last_slash == 0 ? 1 : last_slash] = '\0';
      dest_parent = resolve_path(parent_path);
      vfs_strcpy(dest_name, dest_path + last_slash + 1);
    }
  }
  if (dest_parent == NULL || dest_parent->type != VFS_DIR) {
    term_print("Error: Destination parent directory not found\n");
    return;
  }
  if (dest_parent->child_count >= MAX_CHILDREN) {
    term_print("Error: Destination directory limit reached\n");
    return;
  }
  for (int j = 0; j < dest_parent->child_count; j++) {
    if (strcmp(dest_parent->children[j]->name, dest_name) == 0) {
      term_print("Error: Destination already exists\n");
      return;
    }
  }
  struct vfs_node *copied = copy_node_recursive(src_node, dest_parent);
  if (copied == NULL) {
    term_print("Error: Failed to copy\n");
    return;
  }
  vfs_strncpy(copied->name, dest_name, 63);
  copied->name[63] = '\0';
  dest_parent->children[dest_parent->child_count++] = copied;
  term_print("Copied successfully\n");
}

static void run_vfs_mv(const char *src_path, const char *dest_path) {
  if (src_path == NULL || src_path[0] == '\0' || dest_path == NULL ||
      dest_path[0] == '\0') {
    term_print("Usage: mv <source> <destination>\n");
    return;
  }
  struct vfs_node *src_node = resolve_path(src_path);
  if (src_node == NULL) {
    term_print("Error: Source not found\n");
    return;
  }
  if (src_node == vfs_root) {
    term_print("Error: Cannot move root directory\n");
    return;
  }
  struct vfs_node *dest_node = resolve_path(dest_path);
  struct vfs_node *dest_parent = NULL;
  char dest_name[64];

  if (dest_node != NULL && dest_node->type == VFS_DIR) {
    dest_parent = dest_node;
    vfs_strcpy(dest_name, src_node->name);
  } else {
    int last_slash = -1;
    int i = 0;
    while (dest_path[i] != '\0') {
      if (dest_path[i] == '/')
        last_slash = i;
      i++;
    }
    if (last_slash == -1) {
      dest_parent = vfs_cwd;
      vfs_strcpy(dest_name, dest_path);
    } else {
      char parent_path[256];
      vfs_strncpy(parent_path, dest_path, last_slash == 0 ? 1 : last_slash);
      parent_path[last_slash == 0 ? 1 : last_slash] = '\0';
      dest_parent = resolve_path(parent_path);
      vfs_strcpy(dest_name, dest_path + last_slash + 1);
    }
  }
  if (dest_parent == NULL || dest_parent->type != VFS_DIR) {
    term_print("Error: Destination parent directory not found\n");
    return;
  }
  for (int j = 0; j < dest_parent->child_count; j++) {
    if (strcmp(dest_parent->children[j]->name, dest_name) == 0) {
      term_print("Error: Destination already exists\n");
      return;
    }
  }
  struct vfs_node *orig_parent = src_node->parent;
  if (orig_parent != NULL) {
    int found_idx = -1;
    for (int j = 0; j < orig_parent->child_count; j++) {
      if (orig_parent->children[j] == src_node) {
        found_idx = j;
        break;
      }
    }
    if (found_idx != -1) {
      for (int j = found_idx; j < orig_parent->child_count - 1; j++) {
        orig_parent->children[j] = orig_parent->children[j + 1];
      }
      orig_parent->child_count--;
    }
  }
  src_node->parent = dest_parent;
  vfs_strncpy(src_node->name, dest_name, 63);
  src_node->name[63] = '\0';
  dest_parent->children[dest_parent->child_count++] = src_node;
  term_print("Moved successfully\n");
}

static void run_vfs_rename(const char *target_path, const char *new_name) {
  if (target_path == NULL || target_path[0] == '\0' || new_name == NULL ||
      new_name[0] == '\0') {
    term_print("Usage: rename <old_path> <new_name>\n");
    return;
  }
  struct vfs_node *target = resolve_path(target_path);
  if (target == NULL) {
    term_print("Error: Node not found\n");
    return;
  }
  if (target == vfs_root) {
    term_print("Error: Cannot rename root directory\n");
    return;
  }
  struct vfs_node *parent = target->parent;
  if (parent != NULL) {
    for (int j = 0; j < parent->child_count; j++) {
      if (strcmp(parent->children[j]->name, new_name) == 0) {
        term_print("Error: Name already exists in directory\n");
        return;
      }
    }
  }
  vfs_strncpy(target->name, new_name, 63);
  target->name[63] = '\0';
  term_print("Renamed successfully\n");
}

static void run_vfs_find_recursive(struct vfs_node *node, const char *query,
                                   char *curr_path) {
  if (node == NULL)
    return;
  int path_len = vfs_strlen(curr_path);
  if (node != vfs_root) {
    if (path_len > 1) {
      curr_path[path_len] = '/';
      vfs_strcpy(curr_path + path_len + 1, node->name);
    } else {
      curr_path[path_len] = '\0';
      vfs_strcpy(curr_path + path_len, node->name);
    }
  }
  int query_len = vfs_strlen(query);
  int node_name_len = vfs_strlen(node->name);
  int match = 0;
  if (query_len <= node_name_len) {
    for (int i = 0; i <= node_name_len - query_len; i++) {
      if (strncmp(node->name + i, query, query_len) == 0) {
        match = 1;
        break;
      }
    }
  }
  if (match) {
    char abs_path[256];
    get_absolute_path(node, abs_path);
    term_print(abs_path);
    if (node->type == VFS_DIR)
      term_print("/");
    term_print("\n");
  }
  if (node->type == VFS_DIR) {
    for (int i = 0; i < node->child_count; i++) {
      char child_path[256];
      vfs_strcpy(child_path, curr_path);
      run_vfs_find_recursive(node->children[i], query, child_path);
    }
  }
}

static void run_vfs_find(const char *query) {
  if (query == NULL || query[0] == '\0') {
    term_print("Usage: find <name>\n");
    return;
  }
  char path_buf[256];
  vfs_strcpy(path_buf, "/");
  run_vfs_find_recursive(vfs_root, query, path_buf);
}

static void run_vfs_info(const char *path) {
  if (path == NULL || path[0] == '\0') {
    term_print("Usage: info <file>\n");
    return;
  }
  struct vfs_node *node = resolve_path(path);
  if (node == NULL) {
    term_print("Error: File or directory not found\n");
    return;
  }
  term_print("Name: ");
  term_print(node->name);
  term_print("\nType: ");
  term_print(node->type == VFS_FILE ? "FILE" : "DIRECTORY");
  term_print("\nSize: ");
  if (node->type == VFS_FILE) {
    print_int(vfs_strlen(node->content));
    term_print(" bytes");
  } else {
    print_int(node->child_count);
    term_print(" children");
  }
  term_print("\nMemory Node Index: ");
  int idx = -1;
  for (int i = 0; i < MAX_VFS_NODES; i++) {
    if (&vfs_pool[i] == node) {
      idx = i;
      break;
    }
  }
  print_int(idx);
  term_print("\nParent: ");
  if (node->parent != NULL) {
    term_print(node->parent->name);
  } else {
    term_print("None (Root)");
  }
  term_print("\n");
}

static void run_vfs_append(const char *args) {
  if (args == NULL || args[0] == '\0') {
    term_print("Usage: append <filename> <text>\n");
    return;
  }
  char filename[64];
  int i = 0;
  while (args[i] == ' ')
    i++;
  if (args[i] == '\0') {
    term_print("Usage: append <filename> <text>\n");
    return;
  }
  int f_idx = 0;
  while (args[i] != '\0' && args[i] != ' ') {
    if (f_idx < 63) {
      filename[f_idx++] = args[i];
    }
    i++;
  }
  filename[f_idx] = '\0';
  while (args[i] == ' ')
    i++;

  struct vfs_node *target = resolve_path(filename);
  if (target == NULL || target->type != VFS_FILE) {
    term_print("Error: File not found\n");
    return;
  }
  int c_idx = vfs_strlen(target->content);
  while (args[i] != '\0' && c_idx < MAX_FILE_CONTENT - 1) {
    target->content[c_idx++] = args[i++];
  }
  target->content[c_idx] = '\0';
}

static void run_vfs_clearfile(const char *path) {
  if (path == NULL || path[0] == '\0') {
    term_print("Usage: clearfile <filename>\n");
    return;
  }
  struct vfs_node *target = resolve_path(path);
  if (target == NULL || target->type != VFS_FILE) {
    term_print("Error: File not found\n");
    return;
  }
  target->content[0] = '\0';
  term_print("File cleared\n");
}

static void run_vfs_readall_recursive(struct vfs_node *node) {
  if (node == NULL)
    return;
  if (node->type == VFS_FILE) {
    char abs_path[256];
    get_absolute_path(node, abs_path);
    term_print("--- File: ");
    term_print(abs_path);
    term_print(" ---\n");
    term_print(node->content);
    term_print("\n\n");
  } else {
    for (int i = 0; i < node->child_count; i++) {
      run_vfs_readall_recursive(node->children[i]);
    }
  }
}

static void run_vfs_readall() { run_vfs_readall_recursive(vfs_root); }

static void run_vfs_edit(const char *path) {
  if (path == NULL || path[0] == '\0') {
    term_print("Usage: edit <filename>\n");
    return;
  }
  struct vfs_node *target = resolve_path(path);
  if (target == NULL || target->type != VFS_FILE) {
    term_print("Error: File not found\n");
    return;
  }
  term_print("Editing: ");
  term_print(target->name);
  term_print(
      "\nEnter text below. Type ':wq' on a new line to save and exit.\n");

  target->content[0] = '\0';
  int c_idx = 0;
  char line[256];
  while (1) {
    term_print("> ");
    int l_idx = 0;
    while (1) {
      char c = get_char();
      if (c == '\n') {
        term_putc_color('\n', shell_text_color);
        line[l_idx] = '\0';
        break;
      } else if (c == '\b') {
        if (l_idx > 0) {
          l_idx--;
          term_backspace();
        }
      } else if (c >= 32 && c <= 126) {
        if (l_idx < 254) {
          line[l_idx++] = c;
          term_putc_color(c, shell_text_color);
        }
      }
    }
    if (strcmp(line, ":wq") == 0) {
      term_print("File saved.\n");
      break;
    }
    int l_len = vfs_strlen(line);
    if (c_idx + l_len + 2 < MAX_FILE_CONTENT) {
      vfs_strcpy(target->content + c_idx, line);
      c_idx += l_len;
      target->content[c_idx++] = '\n';
      target->content[c_idx] = '\0';
    } else {
      term_print("Warning: File buffer full. Saving what was typed.\n");
      break;
    }
  }
}

static void run_vfs_open(const char *path) {
  if (path == NULL || path[0] == '\0') {
    term_print("Usage: open <filename|directory>\n");
    return;
  }
  struct vfs_node *node = resolve_path(path);
  if (node == NULL) {
    term_print("Error: Path not found\n");
    return;
  }
  if (node->type == VFS_DIR) {
    run_vfs_cd(path);
  } else {
    term_print("=== Reading File: ");
    term_print(node->name);
    term_print(" ===\n");
    term_print(node->content);
    term_print("\n");
    char abs_path[256];
    get_absolute_path(node, abs_path);
    add_to_recent(abs_path);
  }
}

// GUI Module Definitions
#define APP_TERMINAL 0
#define APP_EXPLORER 1
#define APP_CALC 2
#define APP_SETTINGS 3
#define APP_ABOUT 4

struct gui_window {
  int id;
  const char *title;
  int x, y;
  int w, h;
  int is_open;
  int is_focused;

  // Terminal App Buffer
  char term_lines[15][60];
  int term_line_count;
  char term_cmd_buf[256];
  int term_cmd_idx;

  // File Explorer App States
  char explorer_path[256];
  int selected_item_idx;
  int exp_input_mode; // 0=none, 1=create file, 2=create folder, 3=rename,
                      // 4=text edit
  char exp_input_buf[64];
  int exp_input_idx;
  char exp_history[32][256];
  int exp_history_idx;
  int exp_history_count;

  // Text Editor sub-mode
  struct vfs_node *editing_file;
  char edit_buf[512];
  int edit_idx;

  // Calculator States
  char calc_input[32];
  char calc_result[32];
  int calc_op;
  int calc_val1;
  int calc_has_val1;
};

static struct gui_window gui_windows[5];
static int window_stack[5] = {APP_ABOUT, APP_SETTINGS, APP_CALC, APP_EXPLORER,
                              APP_TERMINAL};
static int gui_mode = 1;

static int desktop_color = 0x3F; // White on Teal background
static const char *wallpaper_pattern = ".";
static int theme_dark = 1;
static int clock_format_24h = 1;
static int cursor_visible = 1;
static int start_menu_open = 0;
static int shutdown_dialog_open = 0;

static int desk_menu_open = 0;
static int desk_menu_x = 0;
static int desk_menu_y = 0;
static int desk_menu_sel = 0;
static const char *desk_menu_items[] = {"[o] Refresh",  "[D] New Folder",
                                        "[F] New File", "[>] Terminal",
                                        "[*] Settings", "[i] About"};

static int move_mode_active = 0;
static int move_mode_app_id = -1;
static int resize_mode_active = 0;
static int resize_mode_app_id = -1;
static char copy_src_path[256] = "";
static char move_src_path[256] = "";
static int copy_pending = 0;
static int move_pending = 0;

// Forward declaration of GUI Event Handler Helpers
static void execute_command(const char *cmd, unsigned int magic,
                            unsigned int mb_addr);
static int get_active_process_count();
static void handle_calc_input(struct gui_window *w, char c);
static void handle_app_click(struct gui_window *w, int rx, int ry);
static void init_gui();
static void draw_window_border(int wx, int wy, int ww, int wh,
                               const char *title, int is_focused);
static void draw_text_in_window(struct gui_window *w, int rx, int ry,
                                const char *text, char attr);
static void draw_window(struct gui_window *w);
static void draw_start_menu();
static void draw_shutdown_dialog();
static void draw_gui();

static void focus_window(int app_id) {
  int idx = -1;
  for (int i = 0; i < 5; i++) {
    if (window_stack[i] == app_id) {
      idx = i;
      break;
    }
  }
  if (idx != -1) {
    for (int i = idx; i < 4; i++) {
      window_stack[i] = window_stack[i + 1];
    }
    window_stack[4] = app_id;
  }
  for (int i = 0; i < 5; i++) {
    gui_windows[i].is_focused = (gui_windows[i].id == app_id);
  }
}

static void init_gui() {
  // APP_TERMINAL
  gui_windows[APP_TERMINAL].id = APP_TERMINAL;
  gui_windows[APP_TERMINAL].title = "Graphical Terminal Window";
  gui_windows[APP_TERMINAL].x = 4;
  gui_windows[APP_TERMINAL].y = 2;
  gui_windows[APP_TERMINAL].w = 52;
  gui_windows[APP_TERMINAL].h = 16;
  gui_windows[APP_TERMINAL].is_open = 1;
  gui_windows[APP_TERMINAL].is_focused = 1;
  gui_windows[APP_TERMINAL].term_line_count = 0;
  gui_windows[APP_TERMINAL].term_cmd_idx = 0;
  gui_windows[APP_TERMINAL].term_cmd_buf[0] = '\0';
  for (int i = 0; i < 15; i++)
    gui_windows[APP_TERMINAL].term_lines[i][0] = '\0';
  vfs_strcpy(gui_windows[APP_TERMINAL].term_lines[0],
             "MiniEduOS v1.0 Graphical Terminal");
  vfs_strcpy(gui_windows[APP_TERMINAL].term_lines[1],
             "Type 'help' or 'help fs' for command lists.");
  gui_windows[APP_TERMINAL].term_line_count = 2;

  // Initialize scrollback variables
  for (int i = 0; i < TERM_SCROLLBACK_MAX; i++)
    term_scrollback[i][0] = '\0';
  vfs_strcpy(term_scrollback[0], "MiniEduOS v1.0 Graphical Terminal");
  vfs_strcpy(term_scrollback[1], "Type 'help' or 'help fs' for command lists.");
  term_scrollback_count = 2;
  term_scroll_offset = 0;

  // APP_EXPLORER
  gui_windows[APP_EXPLORER].id = APP_EXPLORER;
  gui_windows[APP_EXPLORER].title = "RAM File Explorer";
  gui_windows[APP_EXPLORER].x = 2;
  gui_windows[APP_EXPLORER].y = 3;
  gui_windows[APP_EXPLORER].w = 76;
  gui_windows[APP_EXPLORER].h = 17;
  gui_windows[APP_EXPLORER].is_open = 0;
  gui_windows[APP_EXPLORER].is_focused = 0;
  vfs_strcpy(gui_windows[APP_EXPLORER].explorer_path, "/");
  gui_windows[APP_EXPLORER].selected_item_idx = 0;
  gui_windows[APP_EXPLORER].exp_input_mode = 0;
  gui_windows[APP_EXPLORER].exp_input_buf[0] = '\0';
  gui_windows[APP_EXPLORER].exp_input_idx = 0;
  gui_windows[APP_EXPLORER].exp_history_idx = 0;
  gui_windows[APP_EXPLORER].exp_history_count = 1;
  vfs_strcpy(gui_windows[APP_EXPLORER].exp_history[0], "/");
  gui_windows[APP_EXPLORER].editing_file = NULL;
  gui_windows[APP_EXPLORER].edit_buf[0] = '\0';
  gui_windows[APP_EXPLORER].edit_idx = 0;

  // APP_CALC
  gui_windows[APP_CALC].id = APP_CALC;
  gui_windows[APP_CALC].title = "Calculator";
  gui_windows[APP_CALC].x = 22;
  gui_windows[APP_CALC].y = 5;
  gui_windows[APP_CALC].w = 26;
  gui_windows[APP_CALC].h = 15;
  gui_windows[APP_CALC].is_open = 0;
  gui_windows[APP_CALC].is_focused = 0;
  gui_windows[APP_CALC].calc_input[0] = '\0';
  gui_windows[APP_CALC].calc_result[0] = '\0';
  gui_windows[APP_CALC].calc_op = 0;
  gui_windows[APP_CALC].calc_val1 = 0;
  gui_windows[APP_CALC].calc_has_val1 = 0;

  // APP_SETTINGS
  gui_windows[APP_SETTINGS].id = APP_SETTINGS;
  gui_windows[APP_SETTINGS].title = "Desktop Settings";
  gui_windows[APP_SETTINGS].x = 18;
  gui_windows[APP_SETTINGS].y = 3;
  gui_windows[APP_SETTINGS].w = 46;
  gui_windows[APP_SETTINGS].h = 18;
  gui_windows[APP_SETTINGS].is_open = 0;
  gui_windows[APP_SETTINGS].is_focused = 0;

  // APP_ABOUT
  gui_windows[APP_ABOUT].id = APP_ABOUT;
  gui_windows[APP_ABOUT].title = "About MiniEduOS";
  gui_windows[APP_ABOUT].x = 14;
  gui_windows[APP_ABOUT].y = 6;
  gui_windows[APP_ABOUT].w = 45;
  gui_windows[APP_ABOUT].h = 12;
  gui_windows[APP_ABOUT].is_open = 0;
  gui_windows[APP_ABOUT].is_focused = 0;

  window_stack[0] = APP_ABOUT;
  window_stack[1] = APP_SETTINGS;
  window_stack[2] = APP_CALC;
  window_stack[3] = APP_EXPLORER;
  window_stack[4] = APP_TERMINAL;

  start_menu_open = 0;
  shutdown_dialog_open = 0;
  if (graphics_mode) {
    mouse_x = 400;
    mouse_y = 300;
  } else {
    mouse_x = 40;
    mouse_y = 12;
  }
}

static void draw_window_border(int wx, int wy, int ww, int wh,
                               const char *title, int is_focused) {
  char border_attr = is_focused ? 0x1F : 0x70;
  char body_attr = theme_dark ? 0x0F : 0x70;
  for (int x = 0; x < ww; x++) {
    int col = wx + x;
    if (col >= 80)
      break;
    video[(wy * 80 + col) * 2] = ' ';
    video[(wy * 80 + col) * 2 + 1] = border_attr;
  }
  if (wx + ww - 3 < 80) {
    video[(wy * 80 + wx + ww - 3) * 2] = '[';
    video[(wy * 80 + wx + ww - 2) * 2] = 'X';
    video[(wy * 80 + wx + ww - 1) * 2] = ']';
    video[(wy * 80 + wx + ww - 3) * 2 + 1] = border_attr;
    video[(wy * 80 + wx + ww - 2) * 2 + 1] = (char)(border_attr | 0x0C);
    video[(wy * 80 + wx + ww - 1) * 2 + 1] = border_attr;
  }
  if (wx + ww - 6 < 80) {
    video[(wy * 80 + wx + ww - 6) * 2] = '[';
    video[(wy * 80 + wx + ww - 5) * 2] = 'M';
    video[(wy * 80 + wx + ww - 4) * 2] = ']';
    video[(wy * 80 + wx + ww - 6) * 2 + 1] = border_attr;
    video[(wy * 80 + wx + ww - 5) * 2 + 1] = (char)(border_attr | 0x0E);
    video[(wy * 80 + wx + ww - 4) * 2 + 1] = border_attr;
  }
  if (wx + ww - 9 < 80) {
    video[(wy * 80 + wx + ww - 9) * 2] = '[';
    video[(wy * 80 + wx + ww - 8) * 2] = 'R';
    video[(wy * 80 + wx + ww - 7) * 2] = ']';
    video[(wy * 80 + wx + ww - 9) * 2 + 1] = border_attr;
    video[(wy * 80 + wx + ww - 8) * 2 + 1] = (char)(border_attr | 0x0A);
    video[(wy * 80 + wx + ww - 7) * 2 + 1] = border_attr;
  }
  int t_len = vfs_strlen(title);
  int title_col = wx + (ww - t_len) / 2;
  if (title_col < wx)
    title_col = wx + 1;
  for (int i = 0; i < t_len && (title_col + i < wx + ww - 7); i++) {
    video[(wy * 80 + title_col + i) * 2] = title[i];
    video[(wy * 80 + title_col + i) * 2 + 1] = border_attr;
  }
  for (int y = 1; y < wh; y++) {
    int row = wy + y;
    if (row >= 24)
      break;
    video[(row * 80 + wx) * 2] = (char)0xBA;
    video[(row * 80 + wx) * 2 + 1] = border_attr;
    if (wx + ww - 1 < 80) {
      video[(row * 80 + wx + ww - 1) * 2] = (char)0xBA;
      video[(row * 80 + wx + ww - 1) * 2 + 1] = border_attr;
    }
    for (int x = 1; x < ww - 1; x++) {
      int col = wx + x;
      if (col >= 80)
        break;
      if (y == wh - 1) {
        video[(row * 80 + col) * 2] = (char)0xCD;
        video[(row * 80 + col) * 2 + 1] = border_attr;
      } else {
        video[(row * 80 + col) * 2] = ' ';
        video[(row * 80 + col) * 2 + 1] = body_attr;
      }
    }
  }
  video[((wy + wh - 1) * 80 + wx) * 2] = (char)0xC8;
  video[((wy + wh - 1) * 80 + wx) * 2 + 1] = border_attr;
  if (wx + ww - 1 < 80) {
    video[((wy + wh - 1) * 80 + wx + ww - 1) * 2] = (char)0xBC;
    video[((wy + wh - 1) * 80 + wx + ww - 1) * 2 + 1] = border_attr;
  }
}

static void draw_text_in_window(struct gui_window *w, int rx, int ry,
                                const char *text, char attr) {
  int wx = w->x + 1 + rx;
  int wy = w->y + 1 + ry;
  if (wy >= w->y + w->h - 1)
    return;
  int len = vfs_strlen(text);
  for (int i = 0; i < len; i++) {
    int col = wx + i;
    if (col >= w->x + w->w - 1)
      break;
    if (col >= 80)
      break;
    video[(wy * 80 + col) * 2] = text[i];
    video[(wy * 80 + col) * 2 + 1] = attr;
  }
}

static void gui_term_write_char(char c) {
  struct gui_window *w = &gui_windows[APP_TERMINAL];

  // Maintain the old term_lines for backward compatibility
  if (c == '\n') {
    if (w->term_line_count < 13) {
      w->term_line_count++;
    } else {
      for (int i = 0; i < 12; i++) {
        vfs_strcpy(w->term_lines[i], w->term_lines[i + 1]);
      }
    }
    w->term_lines[w->term_line_count - 1][0] = '\0';
  } else {
    if (w->term_line_count == 0) {
      w->term_line_count = 1;
      w->term_lines[0][0] = '\0';
    }
    int len = vfs_strlen(w->term_lines[w->term_line_count - 1]);
    if (len < 48) {
      w->term_lines[w->term_line_count - 1][len] = c;
      w->term_lines[w->term_line_count - 1][len + 1] = '\0';
    }
  }

  // Manage our actual scrollback buffer
  if (term_scrollback_count == 0) {
    term_scrollback_count = 1;
    term_scrollback[0][0] = '\0';
  }

  if (c == '\n') {
    if (term_scrollback_count < TERM_SCROLLBACK_MAX) {
      term_scrollback_count++;
    } else {
      for (int i = 0; i < TERM_SCROLLBACK_MAX - 1; i++) {
        vfs_strcpy(term_scrollback[i], term_scrollback[i + 1]);
      }
    }
    term_scrollback[term_scrollback_count - 1][0] = '\0';
    term_scroll_offset = 0; // Reset offset on new output
  } else {
    int len = vfs_strlen(term_scrollback[term_scrollback_count - 1]);
    if (len < 50) {
      term_scrollback[term_scrollback_count - 1][len] = c;
      term_scrollback[term_scrollback_count - 1][len + 1] = '\0';
    } else {
      // Auto-wrap
      if (term_scrollback_count < TERM_SCROLLBACK_MAX) {
        term_scrollback_count++;
      } else {
        for (int i = 0; i < TERM_SCROLLBACK_MAX - 1; i++) {
          vfs_strcpy(term_scrollback[i], term_scrollback[i + 1]);
        }
      }
      term_scrollback[term_scrollback_count - 1][0] = c;
      term_scrollback[term_scrollback_count - 1][1] = '\0';
      term_scroll_offset = 0;
    }
  }
}

static void draw_terminal_app(struct gui_window *w) {
  char body_attr = theme_dark ? 0x0F : 0x70;

  if (term_scroll_offset == 0) {
    // Show prompt at the bottom
    int limit = 13;
    int history_count = term_scrollback_count;
    if (history_count > limit) {
      history_count = limit;
    }

    for (int i = 0; i < history_count; i++) {
      int idx = term_scrollback_count - history_count + i;
      if (idx >= 0 && idx < term_scrollback_count) {
        draw_text_in_window(w, 0, i, term_scrollback[idx], body_attr);
      }
    }

    char prompt_line[80];
    vfs_strcpy(prompt_line, "> ");
    vfs_strcpy(prompt_line + 2, w->term_cmd_buf);
    int p_len = vfs_strlen(prompt_line);
    int sec, min, hour, day, month, year;
    get_rtc_time(&sec, &min, &hour, &day, &month, &year);
    if (w->is_focused && (sec % 2 == 0)) {
      prompt_line[p_len] = '_';
      prompt_line[p_len + 1] = '\0';
    }
    draw_text_in_window(w, 0, history_count, prompt_line, body_attr);
  } else {
    // Scrolled up: show 14 lines of history, no prompt
    int limit = 14;
    int history_count = limit;
    if (term_scrollback_count < limit) {
      history_count = term_scrollback_count;
    }

    for (int i = 0; i < history_count; i++) {
      int idx = term_scrollback_count - history_count - term_scroll_offset + i;
      if (idx >= 0 && idx < term_scrollback_count) {
        draw_text_in_window(w, 0, i, term_scrollback[idx], body_attr);
      }
    }
  }
}

struct dir_entry {
  struct vfs_node *node;
  int depth;
};

static void collect_dirs(struct vfs_node *node, struct dir_entry *list,
                         int max_entries, int depth, int *count) {
  if (node == NULL || *count >= max_entries)
    return;
  if (node->type == VFS_DIR) {
    list[*count].node = node;
    list[*count].depth = depth;
    (*count)++;
    for (int i = 0; i < node->child_count; i++) {
      collect_dirs(node->children[i], list, max_entries, depth + 1, count);
    }
  }
}

static int explorer_view_grid = 0;
static int context_menu_open = 0;
static int context_menu_x = 0;
static int context_menu_y = 0;
static int context_menu_item_idx = -1;
static int context_menu_is_dir = 0;
static int properties_open = 0;
static struct vfs_node *properties_node = NULL;
static char explorer_search_query[64] = "";
static int explorer_search_active = 0;
static struct vfs_node *search_results[16];
static int search_results_count = 0;
static int text_viewer_open = 0;
static int text_editor_open = 0;
static int text_viewer_scroll = 0;

static void search_vfs_nodes(struct vfs_node *node, const char *query,
                             struct vfs_node **results, int *count) {
  if (node == NULL || *count >= 16)
    return;
  int name_len = vfs_strlen(node->name);
  int q_len = vfs_strlen(query);
  int matched = 0;
  if (q_len > 0) {
    for (int i = 0; i <= name_len - q_len; i++) {
      int j = 0;
      while (j < q_len && node->name[i + j] == query[j]) {
        j++;
      }
      if (j == q_len) {
        matched = 1;
        break;
      }
    }
  }
  if (matched && node->type == VFS_FILE) {
    results[*count] = node;
    (*count)++;
  }
  if (node->type == VFS_DIR) {
    for (int i = 0; i < node->child_count; i++) {
      search_vfs_nodes(node->children[i], query, results, count);
    }
  }
}

static void draw_folder_tree_node(struct gui_window *w, struct vfs_node *node,
                                  int depth, int *row, char body_attr,
                                  char highlight_attr) {
  if (node == NULL || node->type != VFS_DIR || *row >= w->h - 3)
    return;
  char line[32];
  int ind = depth * 2;
  if (ind > 10)
    ind = 10;
  int k = 0;
  for (; k < ind; k++)
    line[k] = ' ';
  line[k++] = '/';
  vfs_strcpy(line + k, node->name);
  char node_abs[256];
  get_absolute_path(node, node_abs);
  char attr =
      (strcmp(node_abs, w->explorer_path) == 0) ? highlight_attr : body_attr;
  draw_text_in_window(w, 0, *row, line, attr);
  (*row)++;
  for (int i = 0; i < node->child_count; i++) {
    if (node->children[i]->type == VFS_DIR) {
      draw_folder_tree_node(w, node->children[i], depth + 1, row, body_attr,
                            highlight_attr);
    }
  }
}

static struct vfs_node *resolve_folder_from_row(struct vfs_node *node,
                                                int depth, int *row,
                                                int target_row) {
  if (node == NULL || node->type != VFS_DIR)
    return NULL;
  if (*row == target_row)
    return node;
  (*row)++;
  for (int i = 0; i < node->child_count; i++) {
    if (node->children[i]->type == VFS_DIR) {
      struct vfs_node *res = resolve_folder_from_row(
          node->children[i], depth + 1, row, target_row);
      if (res != NULL)
        return res;
    }
  }
  return NULL;
}

static void execute_context_option(struct gui_window *w, int opt_idx) {
  struct vfs_node *curr = resolve_path(w->explorer_path);
  if (curr == NULL || context_menu_item_idx >= curr->child_count)
    return;

  struct vfs_node *target = curr->children[context_menu_item_idx];

  if (context_menu_is_dir) {
    if (opt_idx == 0) {
      get_absolute_path(target, w->explorer_path);
      w->selected_item_idx = 0;
    } else if (opt_idx == 1) {
      w->exp_input_mode = 3;
      w->exp_input_buf[0] = '\0';
      w->exp_input_idx = 0;
    } else if (opt_idx == 2) {
      char path[256];
      get_absolute_path(target, path);
      run_vfs_rm(path);
      w->selected_item_idx = 0;
    } else if (opt_idx == 3) {
      properties_open = 1;
      properties_node = target;
    }
  } else {
    if (opt_idx == 0) {
      w->editing_file = target;
      vfs_strcpy(w->edit_buf, target->content);
      text_viewer_open = 1;
      text_editor_open = 0;
      text_viewer_scroll = 0;
    } else if (opt_idx == 1) {
      w->exp_input_mode = 3;
      w->exp_input_buf[0] = '\0';
      w->exp_input_idx = 0;
    } else if (opt_idx == 2) {
      get_absolute_path(target, copy_src_path);
      copy_pending = 1;
      move_pending = 0;
    } else if (opt_idx == 3) {
      get_absolute_path(target, move_src_path);
      move_pending = 1;
      copy_pending = 0;
    } else if (opt_idx == 4) {
      char path[256];
      get_absolute_path(target, path);
      run_vfs_rm(path);
      w->selected_item_idx = 0;
    } else if (opt_idx == 5) {
      properties_open = 1;
      properties_node = target;
    }
  }
}

static int is_pixel_on_window(int mx, int my) {
  for (int i = 0; i < 5; i++) {
    int app_id = window_stack[i];
    struct gui_window *w = &gui_windows[app_id];
    if (w->is_open) {
      if (mx >= w->x && mx < w->x + w->w && my >= w->y && my < w->y + w->h) {
        return 1;
      }
    }
  }
  return 0;
}

static int is_pixel_on_desktop_icon(int mx, int my) {
  struct vfs_node *desk_dir = resolve_path("/home/desktop");
  if (desk_dir != NULL) {
    for (int i = 0; i < desk_dir->child_count && i < 8; i++) {
      int r = 4 + i * 2;
      if (my == r && mx >= 2 && mx <= 18) {
        return 1;
      }
    }
  }
  return 0;
}

static void execute_desk_menu_option(int opt_idx) {
  if (opt_idx == 0) {
    draw_gui();
    force_redraw = 1;
  } else if (opt_idx == 1) {
    struct vfs_node *desk = resolve_path("/home/desktop");
    if (desk) {
      char name[64];
      int suffix = 0;
      while (1) {
        char test_name[64];
        if (suffix == 0) {
          vfs_strcpy(test_name, "New Folder");
        } else {
          vfs_strcpy(test_name, "New Folder (");
          char num[8];
          int_to_str(suffix, num);
          vfs_strcpy(test_name + vfs_strlen(test_name), num);
          vfs_strcpy(test_name + vfs_strlen(test_name), ")");
        }

        int exists = 0;
        for (int j = 0; j < desk->child_count; j++) {
          if (strcmp(desk->children[j]->name, test_name) == 0) {
            exists = 1;
            break;
          }
        }
        if (!exists) {
          vfs_strcpy(name, test_name);
          break;
        }
        suffix++;
      }
      struct vfs_node *new_dir = allocate_node(name, VFS_DIR);
      if (new_dir) {
        new_dir->parent = desk;
        desk->children[desk->child_count++] = new_dir;
      }
    }
  } else if (opt_idx == 2) {
    struct vfs_node *desk = resolve_path("/home/desktop");
    if (desk) {
      char name[64];
      int suffix = 0;
      while (1) {
        char test_name[64];
        if (suffix == 0) {
          vfs_strcpy(test_name, "New File.txt");
        } else {
          vfs_strcpy(test_name, "New File (");
          char num[8];
          int_to_str(suffix, num);
          vfs_strcpy(test_name + vfs_strlen(test_name), num);
          vfs_strcpy(test_name + vfs_strlen(test_name), ").txt");
        }

        int exists = 0;
        for (int j = 0; j < desk->child_count; j++) {
          if (strcmp(desk->children[j]->name, test_name) == 0) {
            exists = 1;
            break;
          }
        }
        if (!exists) {
          vfs_strcpy(name, test_name);
          break;
        }
        suffix++;
      }
      struct vfs_node *new_file = allocate_node(name, VFS_FILE);
      if (new_file) {
        new_file->parent = desk;
        desk->children[desk->child_count++] = new_file;
        new_file->content[0] = '\0';
      }
    }
  } else if (opt_idx == 3) {
    gui_windows[APP_TERMINAL].is_open = 1;
    focus_window(APP_TERMINAL);
  } else if (opt_idx == 4) {
    gui_windows[APP_SETTINGS].is_open = 1;
    focus_window(APP_SETTINGS);
  } else if (opt_idx == 5) {
    gui_windows[APP_ABOUT].is_open = 1;
    focus_window(APP_ABOUT);
  }
}

static void handle_gui_right_click(int mx, int my) {
  if (desk_menu_open) {
    desk_menu_open = 0;
  }
  if (context_menu_open) {
    context_menu_open = 0;
  }

  struct gui_window *w = &gui_windows[APP_EXPLORER];
  if (w->is_open && mx >= w->x && mx < w->x + w->w && my >= w->y &&
      my < w->y + w->h) {
    focus_window(APP_EXPLORER);
    int rx = mx - w->x - 1;
    int ry = my - w->y - 1;

    if (rx >= 20 && ry >= 3 && ry < w->h - 3) {
      struct vfs_node *curr = resolve_path(w->explorer_path);
      if (curr != NULL) {
        int item_idx = -1;
        if (explorer_view_grid) {
          int click_row = ry - 3;
          int click_col = (rx - 21) / 12;
          item_idx = click_row * 3 + click_col;
        } else {
          item_idx = ry - 3;
        }

        if (item_idx >= 0 && item_idx < curr->child_count) {
          w->selected_item_idx = item_idx;
          context_menu_open = 1;
          desk_menu_open = 0;
          context_menu_x = mx;
          context_menu_y = my;
          context_menu_item_idx = item_idx;
          context_menu_is_dir = (curr->children[item_idx]->type == VFS_DIR);
        }
      }
    }
  } else {
    if (my < 24 && !is_pixel_on_window(mx, my) &&
        !is_pixel_on_desktop_icon(mx, my)) {
      desk_menu_open = 1;
      desk_menu_x = mx;
      desk_menu_y = my;
      desk_menu_sel = 0;
    }
  }
}

static void draw_explorer_app(struct gui_window *w) {
  char body_attr = theme_dark ? 0x0F : 0x70;
  char highlight_attr = 0x2F;

  draw_text_in_window(
      w, 0, 0, "[Back] [Fwd] [Up] [Ref] [View: L/G]  Search: ", body_attr);

  char search_box[32];
  vfs_strcpy(search_box, "[");
  vfs_strncpy(search_box + 1, explorer_search_query, 20);
  int s_len = vfs_strlen(search_box);
  if (w->exp_input_mode == 5) {
    int sec, min, hour, day, month, year;
    get_rtc_time(&sec, &min, &hour, &day, &month, &year);
    if (sec % 2 == 0) {
      search_box[s_len] = '_';
      search_box[s_len + 1] = '\0';
    }
  }
  vfs_strcpy(search_box + vfs_strlen(search_box), "]");
  draw_text_in_window(w, 42, 0, search_box,
                      (w->exp_input_mode == 5) ? highlight_attr : body_attr);

  char path_bar[60];
  vfs_strcpy(path_bar, "Path: ");
  vfs_strncpy(path_bar + 6, w->explorer_path, 25);
  draw_text_in_window(w, 0, 1, path_bar, (char)(body_attr | 0x0E));
  draw_text_in_window(w, 36, 1, "[+File] [+Dir] [Rename] [Del] [Cp] [Mv]",
                      body_attr);

  if (copy_pending || move_pending) {
    draw_text_in_window(w, w->w - 9, 1, "[Paste]", highlight_attr);
  }

  char sep[128];
  for (int i = 0; i < w->w - 2 && i < 127; i++)
    sep[i] = '-';
  sep[w->w - 2] = '\0';
  draw_text_in_window(w, 0, 2, sep, body_attr);

  draw_text_in_window(w, 0, w->h - 3, sep, body_attr);

  char status[80];
  status[0] = '\0';
  struct vfs_node *cur_dir = resolve_path(w->explorer_path);
  if (cur_dir != NULL) {
    vfs_strcpy(status, "Folder: ");
    vfs_strncpy(status + 8, cur_dir->name, 12);
    status[20] = '\0';

    vfs_strcpy(status + vfs_strlen(status), " | Files: ");
    int fc = 0, dc = 0;
    for (int i = 0; i < cur_dir->child_count; i++) {
      if (cur_dir->children[i]->type == VFS_DIR)
        dc++;
      else
        fc++;
    }
    char num_buf[16];
    int_to_str(fc, num_buf);
    vfs_strcpy(status + vfs_strlen(status), num_buf);

    vfs_strcpy(status + vfs_strlen(status), " | Dirs: ");
    int_to_str(dc, num_buf);
    vfs_strcpy(status + vfs_strlen(status), num_buf);

    vfs_strcpy(status + vfs_strlen(status), " | Sel: ");
    if (w->selected_item_idx >= 0 &&
        w->selected_item_idx < cur_dir->child_count) {
      vfs_strncpy(status + vfs_strlen(status),
                  cur_dir->children[w->selected_item_idx]->name, 12);
    } else {
      vfs_strcpy(status + vfs_strlen(status), "None");
    }
  }
  draw_text_in_window(w, 0, w->h - 2, status, body_attr);

  int start_row = 3;
  draw_folder_tree_node(w, vfs_root, 0, &start_row, body_attr, highlight_attr);

  for (int y = 3; y < w->h - 3; y++) {
    draw_text_in_window(w, 20, y, "|", body_attr);
  }

  int main_limit = w->h - 6;
  int prev_x = w->w - 18;

  for (int y = 3; y < w->h - 3; y++) {
    draw_text_in_window(w, prev_x, y, "|", body_attr);
  }

  if (text_viewer_open) {
    char tv_title[64];
    vfs_strcpy(tv_title, "Viewer: ");
    if (w->editing_file != NULL) {
      vfs_strncpy(tv_title + 8, w->editing_file->name, 20);
    }
    draw_text_in_window(w, 21, 3, tv_title, (char)(body_attr | 0x0E));
    draw_text_in_window(w, prev_x - 14, 3, "[Edit] [Close]", highlight_attr);

    int text_idx = 0;
    int skip = text_viewer_scroll;
    while (skip > 0 && w->edit_buf[text_idx] != '\0') {
      if (w->edit_buf[text_idx] == '\n')
        skip--;
      text_idx++;
    }

    int row_limit = w->h - 4;
    int curr_row = 5;
    while (curr_row < row_limit) {
      char line_buf[64];
      int ch_idx = 0;
      while (w->edit_buf[text_idx] != '\0' && w->edit_buf[text_idx] != '\n' &&
             ch_idx < prev_x - 23) {
        line_buf[ch_idx++] = w->edit_buf[text_idx++];
      }
      line_buf[ch_idx] = '\0';
      draw_text_in_window(w, 21, curr_row++, line_buf, body_attr);
      if (w->edit_buf[text_idx] == '\n')
        text_idx++;
      if (w->edit_buf[text_idx] == '\0')
        break;
    }

    draw_text_in_window(w, 21, w->h - 4, "Arrows: Scroll | Esc: Close",
                        (char)(body_attr | 0x08));
    return;
  }

  if (text_editor_open) {
    char te_title[64];
    vfs_strcpy(te_title, "Editor: ");
    if (w->editing_file != NULL) {
      vfs_strncpy(te_title + 8, w->editing_file->name, 20);
    }
    draw_text_in_window(w, 21, 3, te_title, (char)(body_attr | 0x0B));

    int text_idx = 0;
    int row_limit = w->h - 5;
    int curr_row = 5;
    while (curr_row < row_limit) {
      char line_buf[64];
      int ch_idx = 0;
      while (w->edit_buf[text_idx] != '\0' && w->edit_buf[text_idx] != '\n' &&
             ch_idx < prev_x - 23) {
        line_buf[ch_idx++] = w->edit_buf[text_idx++];
      }
      line_buf[ch_idx] = '\0';
      draw_text_in_window(w, 21, curr_row++, line_buf, body_attr);
      if (w->edit_buf[text_idx] == '\n')
        text_idx++;
      if (w->edit_buf[text_idx] == '\0')
        break;
    }

    char append_line[80];
    vfs_strcpy(append_line, "Append: ");
    vfs_strcpy(append_line + 8, w->exp_input_buf);
    int sec, min, hour, day, month, year;
    get_rtc_time(&sec, &min, &hour, &day, &month, &year);
    if (sec % 2 == 0) {
      vfs_strcpy(append_line + vfs_strlen(append_line), "_");
    }
    draw_text_in_window(w, 21, w->h - 4, append_line, body_attr);
    draw_text_in_window(w, 21, w->h - 3, "[Save] [Append] [Discard]",
                        highlight_attr);
    return;
  }

  int items_count = 0;
  struct vfs_node *items[32];

  if (explorer_search_active) {
    items_count = search_results_count;
    for (int i = 0; i < items_count && i < 32; i++) {
      items[i] = search_results[i];
    }
  } else if (cur_dir != NULL) {
    items_count = cur_dir->child_count;
    for (int i = 0; i < items_count && i < 32; i++) {
      items[i] = cur_dir->children[i];
    }
  }

  if (items_count == 0) {
    draw_text_in_window(w, 22, 3,
                        explorer_search_active ? "(No Search Results)"
                                               : "(Empty Directory)",
                        (char)(body_attr | 0x08));
  } else {
    if (explorer_view_grid) {
      for (int i = 0; i < items_count && i < main_limit * 3; i++) {
        struct vfs_node *child = items[i];
        int grid_row = i / 3;
        int grid_col = i % 3;
        int rx = 21 + grid_col * 12;
        int ry = 3 + grid_row;

        if (ry >= w->h - 3)
          break;

        char cell[16];
        if (child->type == VFS_DIR) {
          vfs_strcpy(cell, "[D] ");
        } else {
          vfs_strcpy(cell, "[F] ");
        }
        vfs_strncpy(cell + 4, child->name, 7);
        cell[11] = '\0';

        char attr = (i == w->selected_item_idx) ? highlight_attr : body_attr;
        draw_text_in_window(w, rx, ry, cell, attr);
      }
    } else {
      for (int i = 0; i < items_count && i < main_limit; i++) {
        struct vfs_node *child = items[i];
        int ry = 3 + i;

        char icon_name[32];
        if (child->type == VFS_DIR) {
          vfs_strcpy(icon_name, "[D] ");
        } else {
          vfs_strcpy(icon_name, "[F] ");
        }
        vfs_strncpy(icon_name + 4, child->name, 12);
        icon_name[16] = '\0';
        int name_len = vfs_strlen(icon_name);
        for (int k = name_len; k < 16; k++)
          icon_name[k] = ' ';
        icon_name[16] = '\0';

        char type_str[10];
        vfs_strcpy(type_str,
                   (child->type == VFS_DIR) ? "Folder  " : "TxtFile ");

        char size_str[10];
        if (child->type == VFS_DIR) {
          char num[8];
          int_to_str(child->child_count, num);
          vfs_strcpy(size_str, num);
          vfs_strcpy(size_str + vfs_strlen(size_str), " items");
        } else {
          char num[8];
          int_to_str(vfs_strlen(child->content), num);
          vfs_strcpy(size_str, num);
          vfs_strcpy(size_str + vfs_strlen(size_str), " B");
        }
        int size_len = vfs_strlen(size_str);
        for (int k = size_len; k < 8; k++)
          size_str[k] = ' ';
        size_str[8] = '\0';

        char time_str[8];
        int h = (child->name[0] % 12) + 8;
        int m = (child->name[1] % 60);
        time_str[0] = '0' + (h / 10);
        time_str[1] = '0' + (h % 10);
        time_str[2] = ':';
        time_str[3] = '0' + (m / 10);
        time_str[4] = '0' + (m % 10);
        time_str[5] = '\0';

        char item_line[64];
        vfs_strcpy(item_line, icon_name);
        vfs_strcpy(item_line + 16, " ");
        vfs_strcpy(item_line + 17, type_str);
        vfs_strcpy(item_line + 25, " ");
        vfs_strcpy(item_line + 26, size_str);
        vfs_strcpy(item_line + 34, " ");
        vfs_strcpy(item_line + 35, time_str);

        char attr = (i == w->selected_item_idx) ? highlight_attr : body_attr;
        draw_text_in_window(w, 21, ry, item_line, attr);
      }
    }
  }

  if (w->selected_item_idx >= 0 && w->selected_item_idx < items_count) {
    struct vfs_node *selected = items[w->selected_item_idx];
    draw_text_in_window(w, prev_x + 1, 3, "Preview", (char)(body_attr | 0x0E));

    char name_l[16];
    vfs_strcpy(name_l, "Name:");
    vfs_strncpy(name_l + 5, selected->name, 9);
    name_l[14] = '\0';
    draw_text_in_window(w, prev_x + 1, 4, name_l, body_attr);

    draw_text_in_window(w, prev_x + 1, 5,
                        (selected->type == VFS_DIR) ? "Type:Dir" : "Type:File",
                        body_attr);

    if (selected->type == VFS_DIR) {
      char items_l[16];
      vfs_strcpy(items_l, "Items: ");
      char num[8];
      int_to_str(selected->child_count, num);
      vfs_strcpy(items_l + 7, num);
      draw_text_in_window(w, prev_x + 1, 7, items_l, body_attr);
    } else {
      char size_l[16];
      vfs_strcpy(size_l, "Size: ");
      char num[8];
      int_to_str(vfs_strlen(selected->content), num);
      vfs_strcpy(size_l + 6, num);
      vfs_strcpy(size_l + vfs_strlen(size_l), "B");
      draw_text_in_window(w, prev_x + 1, 6, size_l, body_attr);

      draw_text_in_window(w, prev_x + 1, 8, "Lines:", (char)(body_attr | 0x08));
      int text_idx = 0;
      for (int l = 0; l < 3; l++) {
        char line_preview[16];
        int ch_idx = 0;
        while (selected->content[text_idx] != '\0' &&
               selected->content[text_idx] != '\n' && ch_idx < 13) {
          line_preview[ch_idx++] = selected->content[text_idx++];
        }
        line_preview[ch_idx] = '\0';
        draw_text_in_window(w, prev_x + 1, 9 + l, line_preview, body_attr);
        if (selected->content[text_idx] == '\n')
          text_idx++;
        if (selected->content[text_idx] == '\0')
          break;
      }
    }
  } else {
    draw_text_in_window(w, prev_x + 1, 3, "No Selection",
                        (char)(body_attr | 0x08));
  }

  if (properties_open && properties_node != NULL) {
    int dw = 32;
    int dh = 10;
    int dx = 21 + (w->w - 21 - dw) / 2;
    int dy = 3 + (w->h - 6 - dh) / 2;

    char dbg = 0x70;
    for (int y = 0; y < dh; y++) {
      for (int x = 0; x < dw; x++) {
        char ch = ' ';
        if (y == 0 || y == dh - 1)
          ch = '=';
        else if (x == 0 || x == dw - 1)
          ch = '|';

        int vx = w->x + 1 + dx + x;
        int vy = w->y + 1 + dy + y;
        if (vx >= 0 && vx < 80 && vy >= 0 && vy < 25) {
          video[(vy * 80 + vx) * 2] = ch;
          video[(vy * 80 + vx) * 2 + 1] = dbg;
        }
      }
    }

    char line[64];
    vfs_strcpy(line, "--- Properties ---");
    draw_text_in_window(w, dx + 6, dy + 1, line, (char)(dbg | 0x0E));

    vfs_strcpy(line, "Name: ");
    vfs_strcpy(line + 6, properties_node->name);
    draw_text_in_window(w, dx + 2, dy + 2, line, dbg);

    vfs_strcpy(line, "Type: ");
    vfs_strcpy(line + 6,
               (properties_node->type == VFS_DIR) ? "Directory" : "Text File");
    draw_text_in_window(w, dx + 2, dy + 3, line, dbg);

    vfs_strcpy(line, "Size: ");
    if (properties_node->type == VFS_DIR) {
      char num_buf[16];
      int_to_str(properties_node->child_count, num_buf);
      vfs_strcpy(line + 6, num_buf);
      vfs_strcpy(line + vfs_strlen(line), " items");
    } else {
      char num_buf[16];
      int_to_str(vfs_strlen(properties_node->content), num_buf);
      vfs_strcpy(line + 6, num_buf);
      vfs_strcpy(line + vfs_strlen(line), " bytes");
    }
    draw_text_in_window(w, dx + 2, dy + 4, line, dbg);

    vfs_strcpy(line, "Path: ");
    char node_path[256];
    get_absolute_path(properties_node, node_path);
    vfs_strncpy(line + 6, node_path, 23);
    line[29] = '\0';
    draw_text_in_window(w, dx + 2, dy + 5, line, dbg);

    vfs_strcpy(line, "Parent: ");
    if (properties_node->parent != NULL) {
      char p_path[256];
      get_absolute_path(properties_node->parent, p_path);
      vfs_strncpy(line + 8, p_path, 21);
    } else {
      vfs_strcpy(line + 8, "None");
    }
    line[29] = '\0';
    draw_text_in_window(w, dx + 2, dy + 6, line, dbg);

    vfs_strcpy(line, "Modified: 06-21 00:36");
    draw_text_in_window(w, dx + 2, dy + 7, line, dbg);

    draw_text_in_window(w, dx + (dw - 8) / 2, dy + 8, "[ OK ]", 0x2F);
  }

  if (context_menu_open) {
    int cmx = context_menu_x - w->x - 1;
    int cmy = context_menu_y - w->y - 1;
    int count = context_menu_is_dir ? 4 : 6;
    const char *opts_dir[] = {"Open", "Rename", "Delete", "Properties"};
    const char *opts_file[] = {"Open", "Rename", "Copy",
                               "Move", "Delete", "Properties"};
    const char **opts = context_menu_is_dir ? opts_dir : opts_file;

    int box_w = 14;
    int box_h = count + 2;

    if (cmx + box_w > w->w - 2)
      cmx = w->w - 2 - box_w;
    if (cmy + box_h > w->h - 2)
      cmy = w->h - 2 - box_h;
    if (cmx < 0)
      cmx = 0;
    if (cmy < 0)
      cmy = 0;

    char menu_bg = 0x70;
    for (int y = 0; y < box_h; y++) {
      for (int x = 0; x < box_w; x++) {
        char ch = ' ';
        if (y == 0 || y == box_h - 1)
          ch = '-';
        else if (x == 0 || x == box_w - 1)
          ch = '|';

        int vx = w->x + 1 + cmx + x;
        int vy = w->y + 1 + cmy + y;
        if (vx >= 0 && vx < 80 && vy >= 0 && vy < 25) {
          video[(vy * 80 + vx) * 2] = ch;
          video[(vy * 80 + vx) * 2 + 1] = menu_bg;
        }
      }
    }

    for (int i = 0; i < count; i++) {
      int vy = w->y + 1 + cmy + 1 + i;
      int vx_start = w->x + 1 + cmx + 2;
      int opt_len = vfs_strlen(opts[i]);
      for (int j = 0; j < opt_len; j++) {
        if (vx_start + j < 80 && vy >= 0 && vy < 25) {
          video[(vy * 80 + vx_start + j) * 2] = opts[i][j];
          video[(vy * 80 + vx_start + j) * 2 + 1] = menu_bg;
        }
      }
    }
  }

  if (w->exp_input_mode >= 1 && w->exp_input_mode <= 3) {
    char label[32];
    if (w->exp_input_mode == 1)
      vfs_strcpy(label, "New File: ");
    else if (w->exp_input_mode == 2)
      vfs_strcpy(label, "New Folder: ");
    else
      vfs_strcpy(label, "Rename To: ");

    char line[80];
    vfs_strcpy(line, label);
    vfs_strcpy(line + vfs_strlen(label), w->exp_input_buf);
    int sec, min, hour, day, month, year;
    get_rtc_time(&sec, &min, &hour, &day, &month, &year);
    if (sec % 2 == 0) {
      vfs_strcpy(line + vfs_strlen(line), "_");
    }
    draw_text_in_window(w, 21, w->h - 4, line, body_attr);
    draw_text_in_window(w, 21, w->h - 3, "Press [Enter] to OK, [Esc] to Cancel",
                        body_attr);
  }
}

static void draw_calc_app(struct gui_window *w) {
  char body_attr = theme_dark ? 0x0F : 0x70;
  draw_text_in_window(w, 0, 0, "+----------------------+", body_attr);
  char disp[24];
  disp[0] = '|';
  int len = vfs_strlen(w->calc_input);
  int pad = 20 - len;
  for (int i = 1; i <= pad; i++)
    disp[i] = ' ';
  vfs_strcpy(disp + pad + 1, w->calc_input);
  disp[21] = ' ';
  disp[22] = '|';
  disp[23] = '\0';
  draw_text_in_window(w, 0, 1, disp, (char)(body_attr | 0x0A));
  draw_text_in_window(w, 0, 2, "+----------------------+", body_attr);
  draw_text_in_window(w, 0, 4, "  [ 7 ]  [ 8 ]  [ 9 ]  [ / ]", body_attr);
  draw_text_in_window(w, 0, 6, "  [ 4 ]  [ 5 ]  [ 6 ]  [ * ]", body_attr);
  draw_text_in_window(w, 0, 8, "  [ 1 ]  [ 2 ]  [ 3 ]  [ - ]", body_attr);
  draw_text_in_window(w, 0, 10, "  [ 0 ]  [ C ]  [ = ]  [ + ]", body_attr);
  draw_text_in_window(w, 0, 12, "  [ % ]", body_attr);
}

static void draw_settings_app(struct gui_window *w) {
  char body_attr = theme_dark ? 0x0F : 0x70;
  draw_text_in_window(w, 2, 1, "Desktop Color Background:", body_attr);
  draw_text_in_window(
      w, 4, 2, desktop_color == 0x3F ? "[X] Teal " : "[ ] Teal ", body_attr);
  draw_text_in_window(
      w, 24, 2, desktop_color == 0x1F ? "[X] Blue " : "[ ] Blue ", body_attr);
  draw_text_in_window(
      w, 4, 3, desktop_color == 0x4F ? "[X] Red  " : "[ ] Red  ", body_attr);
  draw_text_in_window(
      w, 24, 3, desktop_color == 0x5F ? "[X] Purple" : "[ ] Purple", body_attr);

  draw_text_in_window(w, 2, 4, "Wallpaper Pattern Style:", body_attr);
  draw_text_in_window(
      w, 4, 5, strcmp(wallpaper_pattern, " ") == 0 ? "[X] Solid" : "[ ] Solid",
      body_attr);
  draw_text_in_window(w, 24, 5,
                      strcmp(wallpaper_pattern, ".") == 0 ? "[X] Dotted"
                                                          : "[ ] Dotted",
                      body_attr);
  draw_text_in_window(
      w, 4, 6, strcmp(wallpaper_pattern, "+") == 0 ? "[X] Grid " : "[ ] Grid ",
      body_attr);
  draw_text_in_window(
      w, 24, 6, strcmp(wallpaper_pattern, "~") == 0 ? "[X] Wave " : "[ ] Wave ",
      body_attr);

  draw_text_in_window(w, 2, 7, "System Visual Theme style:", body_attr);
  draw_text_in_window(w, 4, 8, theme_dark ? "[X] Dark Theme" : "[ ] Dark Theme",
                      body_attr);
  draw_text_in_window(
      w, 24, 8, !theme_dark ? "[X] Light Theme" : "[ ] Light Theme", body_attr);

  draw_text_in_window(w, 2, 9, "Clock Formats:", body_attr);
  draw_text_in_window(
      w, 4, 10, clock_format_24h ? "[X] 24-Hour" : "[ ] 24-Hour", body_attr);
  draw_text_in_window(
      w, 24, 10, !clock_format_24h ? "[X] 12-Hour" : "[ ] 12-Hour", body_attr);

  draw_text_in_window(w, 2, 11, "Terminal Text Color Style:", body_attr);
  draw_text_in_window(w, 4, 12,
                      shell_text_color == 0x0A ? "[X] Green" : "[ ] Green",
                      body_attr);
  draw_text_in_window(w, 24, 12,
                      shell_text_color == 0x0F ? "[X] White" : "[ ] White",
                      body_attr);
  draw_text_in_window(w, 4, 13,
                      shell_text_color == 0x70 ? "[X] Black" : "[ ] Black",
                      body_attr);
  draw_text_in_window(w, 24, 13,
                      shell_text_color == 0x0E ? "[X] Amber" : "[ ] Amber",
                      body_attr);

  draw_text_in_window(w, 2, 14, "Cursor Visibility Setting:", body_attr);
  draw_text_in_window(
      w, 4, 15, cursor_visible ? "[X] Visible Cursor" : "[ ] Visible Cursor",
      body_attr);
  draw_text_in_window(
      w, 24, 15, !cursor_visible ? "[X] Hidden Cursor" : "[ ] Hidden Cursor",
      body_attr);
}

static void draw_about_app(struct gui_window *w) {
  char body_attr = theme_dark ? 0x0F : 0x70;
  draw_text_in_window(w, 2, 1, "MiniEduOS - Graphical Lab Edition",
                      (char)(body_attr | 0x0E));
  draw_text_in_window(w, 2, 2, "=================================", body_attr);
  draw_text_in_window(w, 2, 4, "OS Name          : MiniEduOS", body_attr);
  draw_text_in_window(w, 2, 5, "Version          : v1.0.0-GUI", body_attr);
  draw_text_in_window(w, 2, 6, "Developer        : Abu Taher", body_attr);
  draw_text_in_window(w, 2, 7, "Architecture     : x86 (i386)", body_attr);
  char sched_line[64];
  vfs_strcpy(sched_line, "Scheduler        : ");
  vfs_strcpy(sched_line + vfs_strlen(sched_line), current_scheduler_algo);
  draw_text_in_window(w, 2, 8, sched_line, body_attr);
  unsigned int used = 0, free = 0;
  get_mem_stats(&used, &free);
  char ram_line[64];
  vfs_strcpy(ram_line, "Memory           : ");
  char num_buf[16];
  int_to_str(used / 1024, num_buf);
  vfs_strcpy(ram_line + vfs_strlen(ram_line), num_buf);
  vfs_strcpy(ram_line + vfs_strlen(ram_line), " KB / 65536 KB");
  draw_text_in_window(w, 2, 9, ram_line, body_attr);
}

static void draw_window(struct gui_window *w) {
  draw_window_border(w->x, w->y, w->w, w->h, w->title, w->is_focused);
  if (w->id == APP_TERMINAL)
    draw_terminal_app(w);
  else if (w->id == APP_EXPLORER)
    draw_explorer_app(w);
  else if (w->id == APP_CALC)
    draw_calc_app(w);
  else if (w->id == APP_SETTINGS)
    draw_settings_app(w);
  else if (w->id == APP_ABOUT)
    draw_about_app(w);
}

static void draw_start_menu() {
  int sx = 0, sy = 15, sw = 18, sh = 9;
  char attr = theme_dark ? 0x0F : 0x70;
  for (int y = 0; y < sh; y++) {
    int r = sy + y;
    for (int x = 0; x < sw; x++) {
      int c = sx + x;
      char cell_char = ' ';
      if (y == 0 && x == 0)
        cell_char = (char)0xDA;
      else if (y == 0 && x == sw - 1)
        cell_char = (char)0xBF;
      else if (y == sh - 1 && x == 0)
        cell_char = (char)0xC0;
      else if (y == sh - 1 && x == sw - 1)
        cell_char = (char)0xD9;
      else if (y == 0 || y == sh - 1)
        cell_char = (char)0xC4;
      else if (x == 0 || x == sw - 1)
        cell_char = (char)0xB3;
      video[(r * 80 + c) * 2] = cell_char;
      video[(r * 80 + c) * 2 + 1] = attr;
    }
  }
  const char *options[7] = {"1. Terminal", "2. File Explorer", "3. Calculator",
                            "4. Settings", "5. About System",  "6. Restart",
                            "7. Shutdown"};
  for (int i = 0; i < 7; i++) {
    int r = sy + 1 + i;
    int opt_len = vfs_strlen(options[i]);
    int highlight =
        (mouse_x >= sx + 1 && mouse_x < sx + sw - 1 && mouse_y == r);
    char opt_attr = highlight ? 0x2F : attr;
    for (int x = 0; x < sw - 2; x++) {
      video[(r * 80 + sx + 1 + x) * 2] = (x < opt_len) ? options[i][x] : ' ';
      video[(r * 80 + sx + 1 + x) * 2 + 1] = opt_attr;
    }
  }
}

static void draw_shutdown_dialog() {
  int sx = 24, sy = 8, sw = 32, sh = 8;
  char attr = theme_dark ? 0x0F : 0x70;
  for (int y = 0; y < sh; y++) {
    int r = sy + y;
    for (int x = 0; x < sw; x++) {
      int c = sx + x;
      char cell_char = ' ';
      if (y == 0 && x == 0)
        cell_char = (char)0xDA;
      else if (y == 0 && x == sw - 1)
        cell_char = (char)0xBF;
      else if (y == sh - 1 && x == 0)
        cell_char = (char)0xC0;
      else if (y == sh - 1 && x == sw - 1)
        cell_char = (char)0xD9;
      else if (y == 0 || y == sh - 1)
        cell_char = (char)0xC4;
      else if (x == 0 || x == sw - 1)
        cell_char = (char)0xB3;
      else if (y == 2)
        cell_char = (char)0xC4;
      video[(r * 80 + c) * 2] = cell_char;
      video[(r * 80 + c) * 2 + 1] = attr;
    }
  }
  const char *header = "Shutdown MiniEduOS?";
  int h_len = vfs_strlen(header);
  int h_col = sx + (sw - h_len) / 2;
  for (int i = 0; i < h_len; i++) {
    video[(sy * 80 + h_col + i) * 2] = header[i];
    video[(sy * 80 + h_col + i) * 2 + 1] = (char)(attr | 0x0C);
  }
  const char *line1 = "Are you sure you want to exit?";
  int l_len = vfs_strlen(line1);
  int l_col = sx + (sw - l_len) / 2;
  for (int i = 0; i < l_len; i++) {
    video[((sy + 3) * 80 + l_col + i) * 2] = line1[i];
    video[((sy + 3) * 80 + l_col + i) * 2 + 1] = attr;
  }
  const char *btn_yes = "[ Yes ]";
  const char *btn_no = "[ No ]";
  const char *btn_res = "[Reset]";
  int hl_yes = (mouse_x >= sx + 3 && mouse_x <= sx + 9 && mouse_y == sy + 5);
  int hl_no = (mouse_x >= sx + 12 && mouse_x <= sx + 17 && mouse_y == sy + 5);
  int hl_res = (mouse_x >= sx + 20 && mouse_x <= sx + 28 && mouse_y == sy + 5);
  for (int i = 0; i < 7; i++) {
    video[((sy + 5) * 80 + sx + 3 + i) * 2] = btn_yes[i];
    video[((sy + 5) * 80 + sx + 3 + i) * 2 + 1] = hl_yes ? 0x2F : attr;
  }
  for (int i = 0; i < 6; i++) {
    video[((sy + 5) * 80 + sx + 12 + i) * 2] = btn_no[i];
    video[((sy + 5) * 80 + sx + 12 + i) * 2 + 1] = hl_no ? 0x2F : attr;
  }
  for (int i = 0; i < 7; i++) {
    video[((sy + 5) * 80 + sx + 20 + i) * 2] = btn_res[i];
    video[((sy + 5) * 80 + sx + 20 + i) * 2 + 1] = hl_res ? 0x2F : attr;
  }
}

static void draw_gui() {
  char fill_char = wallpaper_pattern[0];
  char attr = (char)desktop_color;
  for (int i = 0; i < 80 * 24; i++) {
    video[i * 2] = fill_char;
    video[i * 2 + 1] = attr;
  }
  const char *d_title = "MiniEduOS Desktop Environment";
  int d_len = vfs_strlen(d_title);
  int d_col = (80 - d_len) / 2;
  for (int i = 0; i < d_len; i++) {
    video[(2 * 80 + d_col + i) * 2] = d_title[i];
    video[(2 * 80 + d_col + i) * 2 + 1] = (char)(desktop_color | 0x08);
  }

  // Draw Desktop Icons
  struct vfs_node *desk_dir = resolve_path("/home/desktop");
  if (desk_dir != NULL) {
    for (int i = 0; i < desk_dir->child_count && i < 8; i++) {
      struct vfs_node *child = desk_dir->children[i];
      char icon_buf[20];
      if (child->type == VFS_DIR) {
        vfs_strcpy(icon_buf, "[D] ");
      } else {
        vfs_strcpy(icon_buf, "[F] ");
      }
      vfs_strncpy(icon_buf + 4, child->name, 12);
      icon_buf[16] = '\0';

      int r = 4 + i * 2;
      int len = vfs_strlen(icon_buf);
      char icon_attr = theme_dark ? 0x0F : 0x3F;
      for (int x = 0; x < len; x++) {
        video[(r * 80 + 2 + x) * 2] = icon_buf[x];
        video[(r * 80 + 2 + x) * 2 + 1] = icon_attr;
      }
    }
  }

  for (int i = 0; i < 5; i++) {
    int app_id = window_stack[i];
    struct gui_window *w = &gui_windows[app_id];
    if (w->is_open) {
      draw_window(w);
    }
  }
  char taskbar_attr = theme_dark ? 0x8F : 0x70;
  for (int x = 0; x < 80; x++) {
    video[(24 * 80 + x) * 2] = ' ';
    video[(24 * 80 + x) * 2 + 1] = taskbar_attr;
  }
  const char *start_txt = "[Start]";
  char start_attr = 0x2F;
  for (int i = 0; i < 7; i++) {
    video[(24 * 80 + i) * 2] = start_txt[i];
    video[(24 * 80 + i) * 2 + 1] = start_attr;
  }

  int focused_app = -1;
  for (int i = 4; i >= 0; i--) {
    int app_id = window_stack[i];
    if (gui_windows[app_id].is_open) {
      focused_app = app_id;
      break;
    }
  }
  if (focused_app != -1) {
    const char *f_title = gui_windows[focused_app].title;
    int ft_len = vfs_strlen(f_title);
    video[(24 * 80 + 8) * 2] = '|';
    video[(24 * 80 + 8) * 2 + 1] = taskbar_attr;
    for (int i = 0; i < ft_len && i < 12; i++) {
      video[(24 * 80 + 10 + i) * 2] = f_title[i];
      video[(24 * 80 + 10 + i) * 2 + 1] = (char)(taskbar_attr | 0x0E);
    }
  }

  char m_buf[16];
  vfs_strcpy(m_buf, "M:");
  char m_num[8];
  int_to_str(mouse_x, m_num);
  vfs_strcpy(m_buf + 2, m_num);
  vfs_strcpy(m_buf + vfs_strlen(m_buf), ",");
  int_to_str(mouse_y, m_num);
  vfs_strcpy(m_buf + vfs_strlen(m_buf), m_num);
  int m_len = vfs_strlen(m_buf);
  for (int i = 0; i < m_len && i < 11; i++) {
    video[(24 * 80 + 13 + i) * 2] = m_buf[i];
    video[(24 * 80 + 13 + i) * 2 + 1] = taskbar_attr | 0x0A;
  }

  char status_buf[64];
  vfs_strcpy(status_buf, "Dir:");
  char cwd_buf[64];
  get_absolute_path(vfs_cwd, cwd_buf);
  int cwd_len = vfs_strlen(cwd_buf);
  if (cwd_len > 8) {
    vfs_strcpy(status_buf + 4, "..");
    vfs_strcpy(status_buf + 6, cwd_buf + cwd_len - 6);
  } else {
    vfs_strcpy(status_buf + 4, cwd_buf);
  }

  vfs_strcpy(status_buf + vfs_strlen(status_buf), " | P:");
  char num_buf[16];
  int_to_str(get_active_process_count(), num_buf);
  vfs_strcpy(status_buf + vfs_strlen(status_buf), num_buf);

  vfs_strcpy(status_buf + vfs_strlen(status_buf), " | Mem:");
  unsigned int used = 0, free = 0;
  get_mem_stats(&used, &free);
  int_to_str(used / (1024 * 1024), num_buf);
  vfs_strcpy(status_buf + vfs_strlen(status_buf), num_buf);
  vfs_strcpy(status_buf + vfs_strlen(status_buf), "M/64M | Sched:");
  vfs_strcpy(status_buf + vfs_strlen(status_buf), current_scheduler_algo);

  int sb_len = vfs_strlen(status_buf);
  for (int i = 0; i < sb_len && i < 56; i++) {
    video[(24 * 80 + 24 + i) * 2] = status_buf[i];
    video[(24 * 80 + 24 + i) * 2 + 1] = taskbar_attr;
  }
  if (start_menu_open)
    draw_start_menu();
  if (shutdown_dialog_open)
    draw_shutdown_dialog();

  if (desk_menu_open) {
    int dx = desk_menu_x;
    int dy = desk_menu_y;
    int dw = 18;
    int dh = 8;

    if (dx + dw > 80)
      dx = 80 - dw;
    if (dy + dh > 24)
      dy = 24 - dh;
    if (dx < 0)
      dx = 0;
    if (dy < 0)
      dy = 0;

    // Draw drop shadow
    for (int y = 1; y <= dh; y++) {
      int vx = dx + dw;
      int vy = dy + y;
      if (vx >= 0 && vx < 80 && vy >= 0 && vy < 24) {
        int idx = (vy * 80 + vx) * 2;
        video[idx + 1] = (video[idx + 1] & 0x0F) | 0x80;
      }
    }
    for (int x = 1; x <= dw; x++) {
      int vx = dx + x;
      int vy = dy + dh;
      if (vx >= 0 && vx < 80 && vy >= 0 && vy < 24) {
        int idx = (vy * 80 + vx) * 2;
        video[idx + 1] = (video[idx + 1] & 0x0F) | 0x80;
      }
    }

    char menu_attr = 0x70;
    char highlight_attr = 0x2F;

    for (int y = 0; y < dh; y++) {
      for (int x = 0; x < dw; x++) {
        char ch = ' ';
        if (y == 0 && x == 0)
          ch = (char)0xDA;
        else if (y == 0 && x == dw - 1)
          ch = (char)0xBF;
        else if (y == dh - 1 && x == 0)
          ch = (char)0xC0;
        else if (y == dh - 1 && x == dw - 1)
          ch = (char)0xD9;
        else if (y == 0 || y == dh - 1)
          ch = (char)0xC4;
        else if (x == 0 || x == dw - 1)
          ch = (char)0xB3;

        int vx = dx + x;
        int vy = dy + y;
        video[(vy * 80 + vx) * 2] = ch;
        video[(vy * 80 + vx) * 2 + 1] = menu_attr;
      }
    }

    for (int i = 0; i < 6; i++) {
      int r = dy + 1 + i;
      int len = vfs_strlen(desk_menu_items[i]);
      int is_hovered =
          (mouse_x >= (dx + 1) * 10 && mouse_x < (dx + dw - 1) * 10 &&
           mouse_y >= r * 24 && mouse_y < (r + 1) * 24);
      int is_selected = (desk_menu_sel == i);
      char opt_attr = (is_hovered || is_selected) ? highlight_attr : menu_attr;

      for (int x = 0; x < dw - 2; x++) {
        int vx = dx + 1 + x;
        video[(r * 80 + vx) * 2] = (x < len) ? desk_menu_items[i][x] : ' ';
        video[(r * 80 + vx) * 2 + 1] = opt_attr;
      }
    }
  }

  if (cursor_visible) {
    int idx = (mouse_y * 80 + mouse_x) * 2;
    video[idx + 1] = 0x4F;
  }
}

static int get_active_process_count() {
  int count = 0;
  for (int i = 0; i < MAX_PROCESSES; i++) {
    if (proc_table[i].pid != 0 && proc_table[i].state != TERMINATED) {
      count++;
    }
  }
  return count;
}

static void handle_calc_input(struct gui_window *w, char c) {
  if (c >= '0' && c <= '9') {
    int len = vfs_strlen(w->calc_input);
    if (len < 19) {
      w->calc_input[len] = c;
      w->calc_input[len + 1] = '\0';
    }
  } else if (c == 'C') {
    w->calc_input[0] = '\0';
    w->calc_result[0] = '\0';
    w->calc_op = 0;
    w->calc_val1 = 0;
    w->calc_has_val1 = 0;
  } else if (c == '+' || c == '-' || c == '*' || c == '/' || c == '%') {
    w->calc_val1 = atoi(w->calc_input);
    w->calc_has_val1 = 1;
    w->calc_op = c;
    w->calc_input[0] = '\0';
  } else if (c == '=') {
    if (w->calc_has_val1) {
      int val2 = atoi(w->calc_input);
      int res = 0;
      if (w->calc_op == '+')
        res = w->calc_val1 + val2;
      else if (w->calc_op == '-')
        res = w->calc_val1 - val2;
      else if (w->calc_op == '*')
        res = w->calc_val1 * val2;
      else if (w->calc_op == '/') {
        if (val2 != 0)
          res = w->calc_val1 / val2;
        else
          res = 0;
      } else if (w->calc_op == '%') {
        if (val2 != 0)
          res = w->calc_val1 % val2;
        else
          res = 0;
      }
      int_to_str(res, w->calc_input);
      w->calc_has_val1 = 0;
      w->calc_op = 0;
    }
  }
}

static void handle_app_click(struct gui_window *w, int rx, int ry) {
  if (w->id == APP_EXPLORER) {
    if (properties_open) {
      properties_open = 0;
      properties_node = NULL;
      return;
    }
    if (text_viewer_open) {
      int prev_x = w->w - 18;
      if (ry == 3) {
        if (rx >= prev_x - 14 && rx <= prev_x - 9) {
          text_viewer_open = 0;
          text_editor_open = 1;
        } else if (rx >= prev_x - 7 && rx <= prev_x - 2) {
          text_viewer_open = 0;
          w->editing_file = NULL;
        }
      }
      return;
    }
    if (text_editor_open) {
      if (ry == w->h - 3) {
        if (rx >= 21 && rx <= 26) {
          if (w->editing_file != NULL) {
            vfs_strcpy(w->editing_file->content, w->edit_buf);
          }
          text_editor_open = 0;
          w->editing_file = NULL;
        } else if (rx >= 28 && rx <= 35) {
          int e_len = vfs_strlen(w->edit_buf);
          int i_len = vfs_strlen(w->exp_input_buf);
          if (e_len + i_len + 2 < 512) {
            vfs_strcpy(w->edit_buf + e_len, w->exp_input_buf);
            w->edit_buf[e_len + i_len] = '\n';
            w->edit_buf[e_len + i_len + 1] = '\0';
          }
          w->exp_input_idx = 0;
          w->exp_input_buf[0] = '\0';
        } else if (rx >= 37 && rx <= 46) {
          text_editor_open = 0;
          w->editing_file = NULL;
        }
      }
      return;
    }
    if (context_menu_open) {
      int cmx = context_menu_x - w->x - 1;
      int cmy = context_menu_y - w->y - 1;
      int count = context_menu_is_dir ? 4 : 6;
      int box_w = 14;
      int box_h = count + 2;

      if (cmx + box_w > w->w - 2)
        cmx = w->w - 2 - box_w;
      if (cmy + box_h > w->h - 2)
        cmy = w->h - 2 - box_h;
      if (cmx < 0)
        cmx = 0;
      if (cmy < 0)
        cmy = 0;

      if (rx >= cmx && rx < cmx + box_w && ry >= cmy && ry < cmy + box_h) {
        int opt_idx = ry - cmy - 1;
        if (opt_idx >= 0 && opt_idx < count) {
          execute_context_option(w, opt_idx);
        }
      }
      context_menu_open = 0;
      return;
    }
    if (ry == 0) {
      if (rx >= 0 && rx <= 5) {
        if (w->exp_history_idx > 0) {
          w->exp_history_idx--;
          vfs_strcpy(w->explorer_path, w->exp_history[w->exp_history_idx]);
          w->selected_item_idx = 0;
        }
      } else if (rx >= 7 && rx <= 11) {
        if (w->exp_history_idx < w->exp_history_count - 1) {
          w->exp_history_idx++;
          vfs_strcpy(w->explorer_path, w->exp_history[w->exp_history_idx]);
          w->selected_item_idx = 0;
        }
      } else if (rx >= 13 && rx <= 16) {
        struct vfs_node *curr = resolve_path(w->explorer_path);
        if (curr != NULL && curr->parent != NULL) {
          char parent_path[256];
          get_absolute_path(curr->parent, parent_path);
          vfs_strcpy(w->explorer_path, parent_path);
          w->selected_item_idx = 0;

          if (w->exp_history_idx < 31) {
            w->exp_history_idx++;
            vfs_strcpy(w->exp_history[w->exp_history_idx], parent_path);
            w->exp_history_count = w->exp_history_idx + 1;
          }
        }
      } else if (rx >= 18 && rx <= 22) {
        explorer_search_active = 0;
        explorer_search_query[0] = '\0';
        w->selected_item_idx = 0;
      } else if (rx >= 24 && rx <= 34) {
        explorer_view_grid = !explorer_view_grid;
        w->selected_item_idx = 0;
      } else if (rx >= 36) {
        w->exp_input_mode = 5;
        explorer_search_query[0] = '\0';
        w->exp_input_idx = 0;
      }
    } else if (ry == 1) {
      if (rx >= 36 && rx <= 42) {
        w->exp_input_mode = 1;
        w->exp_input_buf[0] = '\0';
        w->exp_input_idx = 0;
      } else if (rx >= 44 && rx <= 49) {
        w->exp_input_mode = 2;
        w->exp_input_buf[0] = '\0';
        w->exp_input_idx = 0;
      } else if (rx >= 51 && rx <= 58) {
        w->exp_input_mode = 3;
        w->exp_input_buf[0] = '\0';
        w->exp_input_idx = 0;
      } else if (rx >= 60 && rx <= 64) {
        int items_count = 0;
        struct vfs_node *items[32];
        if (explorer_search_active) {
          items_count = search_results_count;
          for (int i = 0; i < items_count && i < 32; i++)
            items[i] = search_results[i];
        } else {
          struct vfs_node *curr = resolve_path(w->explorer_path);
          if (curr != NULL) {
            items_count = curr->child_count;
            for (int i = 0; i < items_count && i < 32; i++)
              items[i] = curr->children[i];
          }
        }
        if (w->selected_item_idx < items_count) {
          struct vfs_node *target = items[w->selected_item_idx];
          char target_abs[256];
          get_absolute_path(target, target_abs);
          run_vfs_rm(target_abs);
          w->selected_item_idx = 0;
        }
      } else if (rx >= 66 && rx <= 69) {
        int items_count = 0;
        struct vfs_node *items[32];
        if (explorer_search_active) {
          items_count = search_results_count;
          for (int i = 0; i < items_count && i < 32; i++)
            items[i] = search_results[i];
        } else {
          struct vfs_node *curr = resolve_path(w->explorer_path);
          if (curr != NULL) {
            items_count = curr->child_count;
            for (int i = 0; i < items_count && i < 32; i++)
              items[i] = curr->children[i];
          }
        }
        if (w->selected_item_idx < items_count) {
          struct vfs_node *target = items[w->selected_item_idx];
          get_absolute_path(target, copy_src_path);
          copy_pending = 1;
          move_pending = 0;
        }
      } else if (rx >= 71 && rx <= 74) {
        int items_count = 0;
        struct vfs_node *items[32];
        if (explorer_search_active) {
          items_count = search_results_count;
          for (int i = 0; i < items_count && i < 32; i++)
            items[i] = search_results[i];
        } else {
          struct vfs_node *curr = resolve_path(w->explorer_path);
          if (curr != NULL) {
            items_count = curr->child_count;
            for (int i = 0; i < items_count && i < 32; i++)
              items[i] = curr->children[i];
          }
        }
        if (w->selected_item_idx < items_count) {
          struct vfs_node *target = items[w->selected_item_idx];
          get_absolute_path(target, move_src_path);
          move_pending = 1;
          copy_pending = 0;
        }
      } else if ((copy_pending || move_pending) && rx >= w->w - 9 &&
                 rx <= w->w - 3) {
        char dest_abs[256];
        vfs_strcpy(dest_abs, w->explorer_path);
        if (copy_pending) {
          run_vfs_cp(copy_src_path, dest_abs);
          copy_pending = 0;
        } else if (move_pending) {
          run_vfs_mv(move_src_path, dest_abs);
          move_pending = 0;
        }
      }
    } else if (ry >= 3 && ry < w->h - 3) {
      if (rx >= 0 && rx <= 19) {
        int tree_row = 3;
        struct vfs_node *clicked_dir =
            resolve_folder_from_row(vfs_root, 0, &tree_row, ry);
        if (clicked_dir != NULL) {
          char path[256];
          get_absolute_path(clicked_dir, path);
          vfs_strcpy(w->explorer_path, path);
          w->selected_item_idx = 0;

          if (w->exp_history_idx < 31) {
            w->exp_history_idx++;
            vfs_strcpy(w->exp_history[w->exp_history_idx], path);
            w->exp_history_count = w->exp_history_idx + 1;
          }
        }
      } else if (rx >= 21 && rx < w->w - 18) {
        int items_count = 0;
        struct vfs_node *items[32];
        if (explorer_search_active) {
          items_count = search_results_count;
          for (int i = 0; i < items_count && i < 32; i++)
            items[i] = search_results[i];
        } else {
          struct vfs_node *curr = resolve_path(w->explorer_path);
          if (curr != NULL) {
            items_count = curr->child_count;
            for (int i = 0; i < items_count && i < 32; i++)
              items[i] = curr->children[i];
          }
        }

        int item_idx = -1;
        if (explorer_view_grid) {
          int click_row = ry - 3;
          int click_col = (rx - 21) / 12;
          item_idx = click_row * 3 + click_col;
        } else {
          item_idx = ry - 3;
        }

        if (item_idx >= 0 && item_idx < items_count) {
          if (w->selected_item_idx == item_idx) {
            struct vfs_node *target = items[item_idx];
            if (target->type == VFS_DIR) {
              get_absolute_path(target, w->explorer_path);
              w->selected_item_idx = 0;

              if (w->exp_history_idx < 31) {
                w->exp_history_idx++;
                vfs_strcpy(w->exp_history[w->exp_history_idx],
                           w->explorer_path);
                w->exp_history_count = w->exp_history_idx + 1;
              }
            } else {
              w->editing_file = target;
              vfs_strcpy(w->edit_buf, target->content);
              text_viewer_open = 1;
              text_editor_open = 0;
              text_viewer_scroll = 0;
            }
          } else {
            w->selected_item_idx = item_idx;
          }
        }
      }
    }
  } else if (w->id == APP_CALC) {
    int col_btn = -1;
    if (rx >= 2 && rx <= 6)
      col_btn = 0;
    else if (rx >= 9 && rx <= 13)
      col_btn = 1;
    else if (rx >= 16 && rx <= 20)
      col_btn = 2;
    else if (rx >= 23 && rx <= 27)
      col_btn = 3;
    if (col_btn != -1) {
      char clicked_char = 0;
      if (ry == 4) {
        const char keys[4] = {'7', '8', '9', '/'};
        clicked_char = keys[col_btn];
      } else if (ry == 6) {
        const char keys[4] = {'4', '5', '6', '*'};
        clicked_char = keys[col_btn];
      } else if (ry == 8) {
        const char keys[4] = {'1', '2', '3', '-'};
        clicked_char = keys[col_btn];
      } else if (ry == 10) {
        const char keys[4] = {'0', 'C', '=', '+'};
        clicked_char = keys[col_btn];
      } else if (ry == 12) {
        const char keys[4] = {'%', ' ', ' ', ' '};
        clicked_char = keys[col_btn];
      }
      if (clicked_char != 0 && clicked_char != ' ') {
        handle_calc_input(w, clicked_char);
      }
    }
  } else if (w->id == APP_SETTINGS) {
    if (ry == 2)
      desktop_color = 0x3F;
    else if (ry == 3)
      desktop_color = 0x1F;
    else if (ry == 4)
      desktop_color = 0x4F;
    else if (ry == 5)
      desktop_color = 0x5F;
    else if (ry == 7)
      wallpaper_pattern = " ";
    else if (ry == 8)
      wallpaper_pattern = ".";
    else if (ry == 9)
      wallpaper_pattern = "+";
    else if (ry == 10)
      wallpaper_pattern = "~";
    else if (ry == 12) {
      if (rx >= 4 && rx <= 16)
        theme_dark = 1;
      else if (rx >= 22 && rx <= 34)
        theme_dark = 0;
    } else if (ry == 14) {
      if (rx >= 4 && rx <= 15)
        clock_format_24h = 1;
      else if (rx >= 18 && rx <= 28)
        clock_format_24h = 0;
    } else if (ry == 16)
      shell_text_color = 0x0A;
    else if (ry == 17)
      shell_text_color = 0x0F;
    else if (ry == 18)
      shell_text_color = 0x70;
    else if (ry == 19)
      shell_text_color = 0x0E;
    else if (ry == 21) {
      if (rx >= 4 && rx <= 20)
        cursor_visible = 1;
      else if (rx >= 24 && rx <= 40)
        cursor_visible = 0;
    }
  }
}

static void handle_gui_click(int mx, int my) {
  // If the explorer context menu is open, and we click outside the explorer
  // window bounds, close it
  struct gui_window *w_exp = &gui_windows[APP_EXPLORER];
  if (context_menu_open) {
    if (!w_exp->is_open || mx < w_exp->x || mx >= w_exp->x + w_exp->w ||
        my < w_exp->y || my >= w_exp->y + w_exp->h) {
      context_menu_open = 0;
    }
  }

  if (desk_menu_open) {
    int dx = desk_menu_x;
    int dy = desk_menu_y;
    int dw = 18;
    int dh = 8;
    if (dx + dw > 80)
      dx = 80 - dw;
    if (dy + dh > 24)
      dy = 24 - dh;
    if (dx < 0)
      dx = 0;
    if (dy < 0)
      dy = 0;

    if (mx >= dx && mx < dx + dw && my >= dy && my < dy + dh) {
      int opt_idx = my - dy - 1;
      if (opt_idx >= 0 && opt_idx < 6) {
        execute_desk_menu_option(opt_idx);
      }
      desk_menu_open = 0;
      return;
    } else {
      desk_menu_open = 0;
      return;
    }
  }

  if (shutdown_dialog_open) {
    int sx = 24, sy = 8;
    if (mx >= sx + 3 && mx <= sx + 9 && my == sy + 5) {
      term_clear();
      term_print("\n\n\n\n\n\n\n\n\n\n");
      term_print("                  ======================================\n");
      term_print("                        MiniEduOS has been shut down.   \n");
      term_print("                  ======================================\n");
      term_print("\n                      You can safely close the emulator.");
      while (1) {
        __asm__ volatile("hlt");
      }
    }
    if (mx >= sx + 12 && mx <= sx + 17 && my == sy + 5) {
      shutdown_dialog_open = 0;
      return;
    }
    if (mx >= sx + 20 && mx <= sx + 28 && my == sy + 5) {
      shutdown_dialog_open = 0;
      init_memory_manager();
      init_process_manager();
      init_vfs();
      init_gui();
      term_clear();
      draw_gui();
      return;
    }
    return;
  }
  if (start_menu_open) {
    int sx = 0, sy = 15;
    if (mx >= sx + 1 && mx < sx + 17 && my >= sy + 1 && my <= sy + 7) {
      int opt_idx = my - (sy + 1);
      if (opt_idx == 0) {
        gui_windows[APP_TERMINAL].is_open = 1;
        focus_window(APP_TERMINAL);
      } else if (opt_idx == 1) {
        gui_windows[APP_EXPLORER].is_open = 1;
        focus_window(APP_EXPLORER);
      } else if (opt_idx == 2) {
        gui_windows[APP_CALC].is_open = 1;
        focus_window(APP_CALC);
      } else if (opt_idx == 3) {
        gui_windows[APP_SETTINGS].is_open = 1;
        focus_window(APP_SETTINGS);
      } else if (opt_idx == 4) {
        gui_windows[APP_ABOUT].is_open = 1;
        focus_window(APP_ABOUT);
      } else if (opt_idx == 5) {
        init_memory_manager();
        init_process_manager();
        init_vfs();
        init_gui();
        term_clear();
        draw_gui();
      } else if (opt_idx == 6) {
        shutdown_dialog_open = 1;
      }
      start_menu_open = 0;
      return;
    } else {
      start_menu_open = 0;
    }
  }
  if (my == 24) {
    if (mx >= 0 && mx <= 6) {
      start_menu_open = !start_menu_open;
    }
    return;
  }
  for (int i = 4; i >= 0; i--) {
    int app_id = window_stack[i];
    struct gui_window *w = &gui_windows[app_id];
    if (w->is_open) {
      if (mx >= w->x && mx < w->x + w->w && my >= w->y && my < w->y + w->h) {
        focus_window(app_id);
        if (mx == w->x + w->w - 2 && my == w->y) {
          w->is_open = 0;
          return;
        }
        if (mx == w->x + w->w - 5 && my == w->y) {
          move_mode_active = 1;
          move_mode_app_id = app_id;
          return;
        }
        if (mx == w->x + w->w - 8 && my == w->y) {
          resize_mode_active = 1;
          resize_mode_app_id = app_id;
          return;
        }
        int rx = mx - w->x - 1;
        int ry = my - w->y - 1;
        if (rx >= 0 && rx < w->w - 2 && ry >= 0 && ry < w->h - 2) {
          handle_app_click(w, rx, ry);
        }
        return;
      }
    }
  }
}

static void handle_gui_input(char key) {
  if (context_menu_open) {
    if (key == 27) { // ESC
      context_menu_open = 0;
      return;
    }
  }

  if (desk_menu_open) {
    if (key == (char)0x81) {
      desk_menu_sel--;
      if (desk_menu_sel < 0)
        desk_menu_sel = 5;
      return;
    } else if (key == (char)0x84) {
      desk_menu_sel++;
      if (desk_menu_sel > 5)
        desk_menu_sel = 0;
      return;
    } else if (key == '\n') {
      execute_desk_menu_option(desk_menu_sel);
      desk_menu_open = 0;
      return;
    } else if (key == 27) {
      desk_menu_open = 0;
      return;
    }
  }

  if (move_mode_active) {
    int app_id = move_mode_app_id;
    if (key == (char)0x81) {
      if (gui_windows[app_id].y > 1)
        gui_windows[app_id].y--;
    } else if (key == (char)0x84) {
      if (gui_windows[app_id].y + gui_windows[app_id].h < 24)
        gui_windows[app_id].y++;
    } else if (key == (char)0x82) {
      if (gui_windows[app_id].x > 0)
        gui_windows[app_id].x--;
    } else if (key == (char)0x83) {
      if (gui_windows[app_id].x + gui_windows[app_id].w < 80)
        gui_windows[app_id].x++;
    } else if (key == '\n' || key == 27) {
      move_mode_active = 0;
    }
    return;
  }

  if (resize_mode_active) {
    int app_id = resize_mode_app_id;
    if (key == (char)0x81) {
      if (gui_windows[app_id].h > 10)
        gui_windows[app_id].h--;
    } else if (key == (char)0x84) {
      if (gui_windows[app_id].y + gui_windows[app_id].h < 24)
        gui_windows[app_id].h++;
    } else if (key == (char)0x82) {
      if (gui_windows[app_id].w > 30)
        gui_windows[app_id].w--;
    } else if (key == (char)0x83) {
      if (gui_windows[app_id].x + gui_windows[app_id].w < 80)
        gui_windows[app_id].w++;
    } else if (key == '\n' || key == 27) {
      resize_mode_active = 0;
    }
    return;
  }

  if (alt_pressed) {
    if (key == (char)0x81) {
      if (mouse_y > 0)
        mouse_y--;
    } else if (key == (char)0x84) {
      if (mouse_y < 24)
        mouse_y++;
    } else if (key == (char)0x82) {
      if (mouse_x > 0)
        mouse_x--;
    } else if (key == (char)0x83) {
      if (mouse_x < 79)
        mouse_x++;
    } else if (key == '\n' || key == ' ') {
      handle_gui_click(mouse_x, mouse_y);
    }
    return;
  }

  if (key == '\t') {
    for (int k = 3; k >= 0; k--) {
      int next_app = window_stack[k];
      if (gui_windows[next_app].is_open) {
        focus_window(next_app);
        break;
      }
    }
    return;
  }

  int focused_app = -1;
  for (int i = 4; i >= 0; i--) {
    int app_id = window_stack[i];
    if (gui_windows[app_id].is_open) {
      focused_app = app_id;
      break;
    }
  }

  if (focused_app != -1) {
    struct gui_window *w = &gui_windows[focused_app];
    if (focused_app == APP_TERMINAL) {
      if (key == (char)0x85) { // PageUp
        term_scroll_offset += 3;
        int max_offset =
            term_scrollback_count > 14 ? term_scrollback_count - 14 : 0;
        if (term_scroll_offset > max_offset) {
          term_scroll_offset = max_offset;
        }
      } else if (key == (char)0x86) { // PageDown
        term_scroll_offset -= 3;
        if (term_scroll_offset < 0) {
          term_scroll_offset = 0;
        }
      } else if (key == (char)0x81) { // Up Arrow
        term_scroll_offset += 1;
        int max_offset =
            term_scrollback_count > 14 ? term_scrollback_count - 14 : 0;
        if (term_scroll_offset > max_offset) {
          term_scroll_offset = max_offset;
        }
      } else if (key == (char)0x84) { // Down Arrow
        term_scroll_offset -= 1;
        if (term_scroll_offset < 0) {
          term_scroll_offset = 0;
        }
      } else if (key == '\n') {
        w->term_cmd_buf[w->term_cmd_idx] = '\0';

        gui_term_write_char('\n');
        gui_term_write_char('>');
        gui_term_write_char(' ');
        for (int i = 0; w->term_cmd_buf[i] != '\0'; i++) {
          gui_term_write_char(w->term_cmd_buf[i]);
        }
        gui_term_write_char('\n');

        if (w->term_cmd_idx > 0) {
          redirect_to_gui_term = 1;
          execute_command(w->term_cmd_buf, saved_multiboot_magic,
                          saved_multiboot_info_addr);
          redirect_to_gui_term = 0;
        }
        w->term_cmd_idx = 0;
        w->term_cmd_buf[0] = '\0';
      } else if (key == '\b') {
        if (w->term_cmd_idx > 0) {
          w->term_cmd_idx--;
          w->term_cmd_buf[w->term_cmd_idx] = '\0';
        }
      } else if (key >= 32 && key <= 126) {
        if (w->term_cmd_idx < 250) {
          w->term_cmd_buf[w->term_cmd_idx++] = key;
          w->term_cmd_buf[w->term_cmd_idx] = '\0';
        }
      }
    } else if (focused_app == APP_EXPLORER) {
      if (text_viewer_open) {
        if (key == 27) {
          text_viewer_open = 0;
          w->editing_file = NULL;
        } else if (key == (char)0x81) {
          if (text_viewer_scroll > 0)
            text_viewer_scroll--;
        } else if (key == (char)0x84) {
          int lines = 0;
          for (int i = 0; w->edit_buf[i] != '\0'; i++) {
            if (w->edit_buf[i] == '\n')
              lines++;
          }
          if (text_viewer_scroll < lines - 5)
            text_viewer_scroll++;
        }
        return;
      }
      if (text_editor_open) {
        if (key == 27) {
          text_editor_open = 0;
          w->editing_file = NULL;
        } else if (key == '\n') {
          int e_len = vfs_strlen(w->edit_buf);
          int i_len = vfs_strlen(w->exp_input_buf);
          if (e_len + i_len + 2 < 512) {
            vfs_strcpy(w->edit_buf + e_len, w->exp_input_buf);
            w->edit_buf[e_len + i_len] = '\n';
            w->edit_buf[e_len + i_len + 1] = '\0';
          }
          w->exp_input_idx = 0;
          w->exp_input_buf[0] = '\0';
        } else if (key == '\b') {
          if (w->exp_input_idx > 0) {
            w->exp_input_idx--;
            w->exp_input_buf[w->exp_input_idx] = '\0';
          }
        } else if (key >= 32 && key <= 126) {
          if (w->exp_input_idx < 60) {
            w->exp_input_buf[w->exp_input_idx++] = key;
            w->exp_input_buf[w->exp_input_idx] = '\0';
          }
        }
        return;
      }
      if (w->exp_input_mode == 5) {
        if (key == 27) {
          w->exp_input_mode = 0;
        } else if (key == '\n') {
          search_results_count = 0;
          search_vfs_nodes(vfs_root, explorer_search_query, search_results,
                           &search_results_count);
          explorer_search_active = (vfs_strlen(explorer_search_query) > 0);
          w->exp_input_mode = 0;
          w->selected_item_idx = 0;
        } else if (key == '\b') {
          int len = vfs_strlen(explorer_search_query);
          if (len > 0) {
            explorer_search_query[len - 1] = '\0';
          }
        } else if (key >= 32 && key <= 126) {
          int len = vfs_strlen(explorer_search_query);
          if (len < 30) {
            explorer_search_query[len] = key;
            explorer_search_query[len + 1] = '\0';
          }
        }
        return;
      }

      if (w->exp_input_mode >= 1 && w->exp_input_mode <= 3) {
        if (key == 27) {
          w->exp_input_mode = 0;
        } else if (key == '\n') {
          w->exp_input_buf[w->exp_input_idx] = '\0';
          char full_path[256];
          vfs_strcpy(full_path, w->explorer_path);
          int fp_len = vfs_strlen(full_path);
          if (fp_len > 1) {
            full_path[fp_len] = '/';
            vfs_strcpy(full_path + fp_len + 1, w->exp_input_buf);
          } else {
            vfs_strcpy(full_path + fp_len, w->exp_input_buf);
          }
          if (w->exp_input_mode == 1) {
            run_vfs_touch(full_path);
          } else if (w->exp_input_mode == 2) {
            run_vfs_mkdir(full_path);
          } else if (w->exp_input_mode == 3) {
            int items_count = 0;
            struct vfs_node *items[32];
            if (explorer_search_active) {
              items_count = search_results_count;
              for (int i = 0; i < items_count && i < 32; i++)
                items[i] = search_results[i];
            } else {
              struct vfs_node *curr = resolve_path(w->explorer_path);
              if (curr != NULL) {
                items_count = curr->child_count;
                for (int i = 0; i < items_count && i < 32; i++)
                  items[i] = curr->children[i];
              }
            }
            if (w->selected_item_idx < items_count) {
              struct vfs_node *target = items[w->selected_item_idx];
              char target_abs[256];
              get_absolute_path(target, target_abs);
              run_vfs_rename(target_abs, w->exp_input_buf);
            }
          }
          w->exp_input_mode = 0;
          w->exp_input_idx = 0;
          w->exp_input_buf[0] = '\0';
        } else if (key == '\b') {
          if (w->exp_input_idx > 0) {
            w->exp_input_idx--;
            w->exp_input_buf[w->exp_input_idx] = '\0';
          }
        } else if (key >= 32 && key <= 126) {
          if (w->exp_input_idx < 60) {
            w->exp_input_buf[w->exp_input_idx++] = key;
            w->exp_input_buf[w->exp_input_idx] = '\0';
          }
        }
      } else {
        if (ctrl_pressed) {
          if (key == 'c' || key == 'C') {
            int items_count = 0;
            struct vfs_node *items[32];
            if (explorer_search_active) {
              items_count = search_results_count;
              for (int i = 0; i < items_count && i < 32; i++)
                items[i] = search_results[i];
            } else {
              struct vfs_node *curr = resolve_path(w->explorer_path);
              if (curr != NULL) {
                items_count = curr->child_count;
                for (int i = 0; i < items_count && i < 32; i++)
                  items[i] = curr->children[i];
              }
            }
            if (w->selected_item_idx < items_count) {
              get_absolute_path(items[w->selected_item_idx], copy_src_path);
              copy_pending = 1;
              move_pending = 0;
            }
          } else if (key == 'x' || key == 'X') {
            int items_count = 0;
            struct vfs_node *items[32];
            if (explorer_search_active) {
              items_count = search_results_count;
              for (int i = 0; i < items_count && i < 32; i++)
                items[i] = search_results[i];
            } else {
              struct vfs_node *curr = resolve_path(w->explorer_path);
              if (curr != NULL) {
                items_count = curr->child_count;
                for (int i = 0; i < items_count && i < 32; i++)
                  items[i] = curr->children[i];
              }
            }
            if (w->selected_item_idx < items_count) {
              get_absolute_path(items[w->selected_item_idx], move_src_path);
              move_pending = 1;
              copy_pending = 0;
            }
          } else if (key == 'v' || key == 'V') {
            char dest_abs[256];
            vfs_strcpy(dest_abs, w->explorer_path);
            if (copy_pending) {
              run_vfs_cp(copy_src_path, dest_abs);
              copy_pending = 0;
            } else if (move_pending) {
              run_vfs_mv(move_src_path, dest_abs);
              move_pending = 0;
            }
          } else if (key == 'n' || key == 'N') {
            if (shift_pressed) {
              w->exp_input_mode = 1;
            } else {
              w->exp_input_mode = 2;
            }
            w->exp_input_buf[0] = '\0';
            w->exp_input_idx = 0;
          }
        } else if (key == (char)0x87) {
          w->exp_input_mode = 3;
          w->exp_input_buf[0] = '\0';
          w->exp_input_idx = 0;
        } else if (key == (char)0x88) {
          int items_count = 0;
          struct vfs_node *items[32];
          if (explorer_search_active) {
            items_count = search_results_count;
            for (int i = 0; i < items_count && i < 32; i++)
              items[i] = search_results[i];
          } else {
            struct vfs_node *curr = resolve_path(w->explorer_path);
            if (curr != NULL) {
              items_count = curr->child_count;
              for (int i = 0; i < items_count && i < 32; i++)
                items[i] = curr->children[i];
            }
          }
          if (w->selected_item_idx < items_count) {
            char path[256];
            get_absolute_path(items[w->selected_item_idx], path);
            run_vfs_rm(path);
            w->selected_item_idx = 0;
          }
        } else if (key == 27 || key == '\b') {
          struct vfs_node *curr = resolve_path(w->explorer_path);
          if (curr != NULL && curr->parent != NULL) {
            char parent_path[256];
            get_absolute_path(curr->parent, parent_path);
            vfs_strcpy(w->explorer_path, parent_path);
            w->selected_item_idx = 0;
            if (w->exp_history_idx < 31) {
              w->exp_history_idx++;
              vfs_strcpy(w->exp_history[w->exp_history_idx], parent_path);
              w->exp_history_count = w->exp_history_idx + 1;
            }
          }
        } else if (key == '\n') {
          int items_count = 0;
          struct vfs_node *items[32];
          if (explorer_search_active) {
            items_count = search_results_count;
            for (int i = 0; i < items_count && i < 32; i++)
              items[i] = search_results[i];
          } else {
            struct vfs_node *curr = resolve_path(w->explorer_path);
            if (curr != NULL) {
              items_count = curr->child_count;
              for (int i = 0; i < items_count && i < 32; i++)
                items[i] = curr->children[i];
            }
          }
          if (w->selected_item_idx < items_count) {
            struct vfs_node *target = items[w->selected_item_idx];
            if (target->type == VFS_DIR) {
              get_absolute_path(target, w->explorer_path);
              w->selected_item_idx = 0;
              if (w->exp_history_idx < 31) {
                w->exp_history_idx++;
                vfs_strcpy(w->exp_history[w->exp_history_idx],
                           w->explorer_path);
                w->exp_history_count = w->exp_history_idx + 1;
              }
            } else {
              w->editing_file = target;
              vfs_strcpy(w->edit_buf, target->content);
              text_viewer_open = 1;
              text_editor_open = 0;
              text_viewer_scroll = 0;
            }
          }
        } else if (key == (char)0x81) {
          if (w->selected_item_idx > 0)
            w->selected_item_idx--;
        } else if (key == (char)0x84) {
          int items_count = 0;
          if (explorer_search_active) {
            items_count = search_results_count;
          } else {
            struct vfs_node *curr = resolve_path(w->explorer_path);
            if (curr != NULL)
              items_count = curr->child_count;
          }
          if (w->selected_item_idx < items_count - 1) {
            w->selected_item_idx++;
          }
        }
      }
    } else if (focused_app == APP_CALC) {
      if ((key >= '0' && key <= '9') || key == '+' || key == '-' ||
          key == '*' || key == '/' || key == '%' || key == '=' || key == 'C' ||
          key == 'c' || key == '\n') {
        char click_key = key;
        if (key == '\n')
          click_key = '=';
        if (key == 'c')
          click_key = 'C';
        handle_calc_input(w, click_key);
      }
    }
  } else {
    if (key == '\n' || key == ' ') {
      handle_gui_click(mouse_x, mouse_y);
    }
  }
}

// ==========================================================
//                 SYSTEM MONITOR MODULE
// ==========================================================

// Boot Time & Uptime Tracking Globals
static int boot_hour = -1;
static int boot_min = -1;
static int boot_sec = -1;

static void init_boot_time() {
  if (boot_hour == -1) {
    int d, m, y;
    get_rtc_time(&boot_sec, &boot_min, &boot_hour, &d, &m, &y);
  }
}

static void get_uptime(int *hours, int *mins, int *secs) {
  init_boot_time();
  int s, m, h, dy, mn, yr;
  get_rtc_time(&s, &m, &h, &dy, &mn, &yr);
  int boot_total = boot_hour * 3600 + boot_min * 60 + boot_sec;
  int current_total = h * 3600 + m * 60 + s;
  if (current_total < boot_total) {
    current_total += 24 * 3600;
  }
  int diff = current_total - boot_total;
  *hours = diff / 3600;
  *mins = (diff % 3600) / 60;
  *secs = diff % 60;
}

static int get_system_cpu_usage() {
  int total = 0;
  for (int i = 0; i < proc_count; i++) {
    if (proc_table[i].state != TERMINATED) {
      total += proc_table[i].cpu_time;
    }
  }
  int usage = 10 + (total % 65);
  return usage;
}

static int get_process_cpu(struct process *p) {
  if (p->state == RUNNING) {
    return 5 + (p->cpu_time % 10);
  } else if (p->state == READY) {
    return 1 + (p->cpu_time % 5);
  } else if (p->state == WAITING) {
    return 1;
  }
  return 0;
}

static int get_process_mem(struct process *p) {
  if (p->pid == 1)
    return 2;
  if (p->pid == 2)
    return 1;
  return 1 + (p->pid % 2);
}

static void get_mem_stats_mb(unsigned int *used_mb, unsigned int *free_mb) {
  unsigned int used = 0, free = 0;
  get_mem_stats(&used, &free);
  *used_mb = (used + 1024 * 1024 - 1) / (1024 * 1024);
  *free_mb = free / (1024 * 1024);
  if (*used_mb + *free_mb != 64) {
    *free_mb = 64 - *used_mb;
  }
}

static void print_padded_str(const char *str, int width) {
  term_print(str);
  int len = vfs_strlen(str);
  for (int i = len; i < width; i++) {
    term_putc_color(' ', shell_text_color);
  }
}

static void print_padded_int_left(int val, int width) {
  print_int(val);
  int len = (val >= 1000) ? 4 : ((val >= 100) ? 3 : ((val >= 10) ? 2 : 1));
  for (int i = len; i < width; i++) {
    term_putc_color(' ', shell_text_color);
  }
}

static void run_sysinfo(unsigned int magic, unsigned int mb_addr) {
  (void)magic;
  (void)mb_addr;
  term_print("--------------------------------\n");
  term_print("MiniEduOS System Monitor\n");
  term_print("--------------------------------\n");
  term_print("Kernel Version : MiniEduOS v1.0\n");
  term_print("Architecture   : x86_64\n");

  term_print("Uptime         : ");
  int h, m, s;
  get_uptime(&h, &m, &s);
  print_int_leading_zero(h, 2);
  term_print(":");
  print_int_leading_zero(m, 2);
  term_print(":");
  print_int_leading_zero(s, 2);
  term_print("\n");

  term_print("CPU Usage      : ");
  print_int(get_system_cpu_usage());
  term_print("%\n");

  unsigned int used_mb, free_mb;
  get_mem_stats_mb(&used_mb, &free_mb);
  term_print("Memory Usage   : ");
  print_int(used_mb);
  term_print("MB / 64MB\n");

  term_print("Processes      : ");
  int active_p = 0;
  for (int i = 0; i < proc_count; i++) {
    if (proc_table[i].state != TERMINATED)
      active_p++;
  }
  print_int(active_p);
  term_print("\n");

  term_print("Scheduler      : ");
  if (strncmp(current_scheduler_algo, "Round Robin", 11) == 0) {
    term_print("Round Robin");
  } else {
    term_print(current_scheduler_algo);
  }
  term_print("\n");

  term_print("Load Average   : 0.12 0.15 0.10\n");
  term_print("--------------------------------\n");
}

static void run_top() {
  term_print("PID | NAME   | STATE   | CPU | MEM\n");
  term_print("----|--------|---------|-----|----\n");

  struct process sorted[32];
  int count = 0;
  for (int idx = 0; idx < proc_count; idx++) {
    sorted[count++] = proc_table[idx];
  }

  // Sort descending by CPU usage
  for (int i = 0; i < count - 1; i++) {
    for (int j = 0; j < count - i - 1; j++) {
      int cpu_a = get_process_cpu(&sorted[j]);
      int cpu_b = get_process_cpu(&sorted[j + 1]);
      if (cpu_a < cpu_b) {
        struct process temp = sorted[j];
        sorted[j] = sorted[j + 1];
        sorted[j + 1] = temp;
      }
    }
  }

  for (int idx = 0; idx < count; idx++) {
    // PID
    print_padded_int_left(sorted[idx].pid, 4);
    term_print("| ");

    // NAME
    print_padded_str(sorted[idx].name, 6);
    term_print(" | ");

    // STATE
    const char *state_str = "READY";
    if (sorted[idx].state == RUNNING)
      state_str = "RUNNING";
    else if (sorted[idx].state == READY)
      state_str = "READY";
    else if (sorted[idx].state == WAITING)
      state_str = "WAITING";
    else if (sorted[idx].state == TERMINATED)
      state_str = "TERMINATED";
    print_padded_str(state_str, 7);
    term_print(" | ");

    // CPU
    print_padded_int_left(get_process_cpu(&sorted[idx]), 3);
    term_print(" | ");

    // MEM
    term_print(" ");
    print_int(get_process_mem(&sorted[idx]));
    term_print("MB\n");
  }
}

static void run_uptime() {
  int h, m, s;
  get_uptime(&h, &m, &s);
  term_print("System Uptime: ");
  print_int_leading_zero(h, 2);
  term_print(":");
  print_int_leading_zero(m, 2);
  term_print(":");
  print_int_leading_zero(s, 2);
  term_print("\n");
}

static void run_meminfo() {
  unsigned int used_mb, free_mb;
  get_mem_stats_mb(&used_mb, &free_mb);
  term_print("Total Memory : 64MB\n");
  term_print("Used Memory  : ");
  print_int(used_mb);
  term_print("MB\n");
  term_print("Free Memory  : ");
  print_int(free_mb);
  term_print("MB\n");
}

static void run_procinfo() {
  int total = 0;
  int running = 0;
  int ready = 0;
  int terminated = 0;
  int waiting = 0;
  for (int i = 0; i < proc_count; i++) {
    total++;
    if (proc_table[i].state == RUNNING)
      running++;
    else if (proc_table[i].state == READY)
      ready++;
    else if (proc_table[i].state == TERMINATED)
      terminated++;
    else if (proc_table[i].state == WAITING)
      waiting++;
  }
  term_print("Total Processes: ");
  print_int(total);
  term_print("\nRunning: ");
  print_int(running);
  term_print("\nReady: ");
  print_int(ready);
  term_print("Terminated: ");
  print_int(terminated);
  term_print("\n");
}

#define GANTT_DISPLAY_WIDTH 32

static void visualize_gantt_chart() {
  term_print("---------------------------------\n");
  term_print("Algorithm: ");
  term_print(last_algo);
  if (strcmp(last_algo, "Round Robin") == 0) {
    term_print(" (Quantum: ");
    print_int(last_quantum);
    term_print(")");
  }
  term_print("\n---------------------------------\n\n");

  // Get distinct processes from last timeline
  int unique_pids[32];
  char unique_names[32][32];
  int unique_count = 0;

  for (int i = 0; i < last_timeline_count; i++) {
    int pid = last_timeline[i].pid;
    int found = 0;
    for (int j = 0; j < unique_count; j++) {
      if (unique_pids[j] == pid) {
        found = 1;
        break;
      }
    }
    if (!found && unique_count < 32) {
      unique_pids[unique_count] = pid;
      vfs_strcpy(unique_names[unique_count], last_timeline[i].name);
      unique_count++;
    }
  }

  int total_time = 0;
  if (last_timeline_count > 0) {
    total_time = last_timeline[last_timeline_count - 1].end_time;
  }

  if (total_time <= 0) {
    term_print("No execution timeline to visualize.\n");
    return;
  }

  // Draw timeline for each unique process
  static char patterns[] = {'#', '=', '*', '@', '%', '+',
                            'x', 'o', '.', '$', '?', '!'};
  static int bg_colors[] = {1, 2, 3, 4, 5, 6, 9, 10, 11, 12, 13, 14};

  for (int p = 0; p < unique_count; p++) {
    int pid = unique_pids[p];

    char name_buf[5];
    int name_len = vfs_strlen(unique_names[p]);
    int limit = name_len < 4 ? name_len : 4;
    vfs_strncpy(name_buf, unique_names[p], limit);
    name_buf[limit] = '\0';
    term_print(name_buf);
    for (int k = limit; k < 4; k++) {
      term_putc_color(' ', shell_text_color);
    }
    term_print("|");

    char pat = patterns[p % 12];
    int bg = bg_colors[p % 12];
    char attr = (bg << 4) | 0x0F;

    // Build active array for this process
    char active[GANTT_DISPLAY_WIDTH];
    for (int col = 0; col < GANTT_DISPLAY_WIDTH; col++) {
      active[col] = 0;
    }

    for (int i = 0; i < last_timeline_count; i++) {
      if (last_timeline[i].pid == pid) {
        int start_col =
            (last_timeline[i].start_time * GANTT_DISPLAY_WIDTH) / total_time;
        int end_col =
            (last_timeline[i].end_time * GANTT_DISPLAY_WIDTH) / total_time;

        if (end_col == start_col &&
            last_timeline[i].end_time > last_timeline[i].start_time) {
          end_col = start_col + 1;
        }

        for (int col = start_col; col < end_col; col++) {
          if (col < GANTT_DISPLAY_WIDTH) {
            active[col] = 1;
          }
        }
      }
    }

    for (int col = 0; col < GANTT_DISPLAY_WIDTH; col++) {
      if (active[col]) {
        if (redirect_to_gui_term) {
          term_putc_color(pat, shell_text_color);
        } else {
          term_putc_color(' ', attr);
        }
      } else {
        term_putc_color(' ', shell_text_color);
      }
    }
    term_print("|\n");
  }

  // Draw Time Axis
  int distinct_times[64];
  int dt_count = 0;
  distinct_times[dt_count++] = 0;
  for (int i = 0; i < last_timeline_count; i++) {
    int t = last_timeline[i].end_time;
    int found = 0;
    for (int j = 0; j < dt_count; j++) {
      if (distinct_times[j] == t) {
        found = 1;
        break;
      }
    }
    if (!found && dt_count < 64) {
      distinct_times[dt_count++] = t;
    }
  }

  char axis_line[80];
  for (int i = 0; i < 80; i++) {
    axis_line[i] = ' ';
  }
  vfs_strcpy(axis_line, "Time: ");
  axis_line[6] = ' ';

  int last_written_pos = 5;
  for (int i = 0; i < dt_count; i++) {
    int t = distinct_times[i];
    int col = (t * GANTT_DISPLAY_WIDTH) / total_time;
    int target_idx = 6 + col;

    char t_str[16];
    int_to_str(t, t_str);
    int t_len = vfs_strlen(t_str);

    if (i == dt_count - 1 && target_idx < last_written_pos + 2) {
      // Overlap with previous label! Clear previous label characters
      for (int k = last_written_pos - 4; k < target_idx; k++) {
        if (k >= 6) {
          axis_line[k] = ' ';
        }
      }
      last_written_pos = target_idx - 2;
    }

    if (target_idx >= last_written_pos + 2 || i == 0) {
      for (int k = 0; k < t_len && (target_idx + k) < 79; k++) {
        axis_line[target_idx + k] = t_str[k];
      }
      last_written_pos = target_idx + t_len - 1;
    }
  }

  int end_idx = last_written_pos + 1;
  if (end_idx > 79)
    end_idx = 79;
  axis_line[end_idx] = '\0';

  term_print(axis_line);
  term_print("\n\n");

  // Print metrics table
  term_print("PID | WT | TAT\n");
  term_print("----|----|-----\n");
  int total_wt = 0;
  int total_tat = 0;
  for (int i = 0; i < last_job_count; i++) {
    print_int(last_jobs[i].pid);
    int pid_len =
        (last_jobs[i].pid >= 1000)
            ? 4
            : ((last_jobs[i].pid >= 100) ? 3
                                         : ((last_jobs[i].pid >= 10) ? 2 : 1));
    for (int k = pid_len; k < 4; k++) {
      term_putc_color(' ', shell_text_color);
    }
    term_print("|");

    term_putc_color(' ', shell_text_color);
    print_int(last_jobs[i].waiting_time);
    int wt_len = (last_jobs[i].waiting_time >= 1000)
                     ? 4
                     : ((last_jobs[i].waiting_time >= 100)
                            ? 3
                            : ((last_jobs[i].waiting_time >= 10) ? 2 : 1));
    for (int k = wt_len; k < 2; k++) {
      term_putc_color(' ', shell_text_color);
    }
    term_putc_color(' ', shell_text_color);
    term_print("| ");

    print_int(last_jobs[i].turnaround_time);
    term_print("\n");

    total_wt += last_jobs[i].waiting_time;
    total_tat += last_jobs[i].turnaround_time;
  }
  term_print("\n");

  int avg_wt_whole = total_wt / last_job_count;
  int avg_wt_frac = ((total_wt * 10) / last_job_count) % 10;
  int avg_tat_whole = total_tat / last_job_count;
  int avg_tat_frac = ((total_tat * 10) / last_job_count) % 10;

  term_print("Average Waiting Time: ");
  print_int(avg_wt_whole);
  term_print(".");
  print_int(avg_wt_frac);
  term_print("\n");

  term_print("Average Turnaround Time: ");
  print_int(avg_tat_whole);
  term_print(".");
  print_int(avg_tat_frac);
  term_print("\n");
}

static void run_gantt(const char *args) {
  while (*args == ' ')
    args++;
  if (*args != '\0') {
    if (strcmp(args, "fcfs") == 0) {
      vfs_strcpy(current_scheduler_algo, "FCFS");
      run_scheduler("FCFS", 0);
    } else if (strcmp(args, "sjf") == 0) {
      vfs_strcpy(current_scheduler_algo, "SJF");
      run_scheduler("SJF", 0);
    } else if (strcmp(args, "priority") == 0) {
      vfs_strcpy(current_scheduler_algo, "Priority");
      run_scheduler("Priority", 0);
    } else if (strncmp(args, "rr", 2) == 0) {
      const char *q_ptr = args + 2;
      while (*q_ptr == ' ')
        q_ptr++;
      int quantum = 2;
      if (*q_ptr != '\0') {
        quantum = atoi(q_ptr);
      }
      if (quantum <= 0) {
        quantum = 2;
      }
      vfs_strcpy(current_scheduler_algo, "Round Robin (q=");
      char q_buf[16];
      int_to_str(quantum, q_buf);
      vfs_strcpy(current_scheduler_algo + vfs_strlen(current_scheduler_algo),
                 q_buf);
      vfs_strcpy(current_scheduler_algo + vfs_strlen(current_scheduler_algo),
                 ")");
      run_scheduler("Round Robin", quantum);
    } else {
      term_print("Usage: gantt [fcfs | sjf | priority | rr [q]]\n");
      return;
    }
  }

  if (!has_last_results || last_job_count == 0) {
    term_print("Error: No scheduling results available. Run a scheduler "
               "command first.\n");
    return;
  }

  visualize_gantt_chart();
}

// Unified Command Execution Dispatcher
static void execute_command(const char *cmd, unsigned int magic,
                            unsigned int mb_addr) {
  add_to_history(cmd);

  if (strcmp(cmd, "help") == 0) {
    term_print(
        "Available commands:\n"
        "  help, clear, cls, about, version, echo\n"
        "  date, time, calc, reboot, shutdown\n"
        "  color, memory, cpu, ascii, sysinfo\n"
        "  mem, malloc <bytes>, free [id], memmap\n"
        "  ps, new <name>, run <pid>, kill <pid>, top\n"
        "  schedule, fcfs, sjf, priority, rr <quantum>\n"
        "  ls, pwd, cd [path], mkdir [name], touch [name]\n"
        "  cat [file], write [file] [text], rm [name], tree\n"
        "  open [path], cp [src] [dst], mv [src] [dst], rename [old] [new]\n"
        "  find [query], info [path], edit [path], append [path] [text]\n"
        "  clearfile [path], readall, history, recent, help fs, gui\n");
  } else if (strcmp(cmd, "help fs") == 0) {
    term_print("File System Commands:\n"
               "  ls                    - List current directory items\n"
               "  pwd                   - Print current directory path\n"
               "  cd [path]             - Change directory\n"
               "  mkdir [name]          - Create directory\n"
               "  touch [name]          - Create empty file\n"
               "  cat [file]            - Read file content\n"
               "  write [file] [text]   - Write text to file\n"
               "  rm [name]             - Delete file/directory recursively\n"
               "  tree                  - Print visual file system tree\n"
               "  open [path]           - Open file or change directory\n"
               "  cp [src] [dst]        - Copy file or directory\n"
               "  mv [src] [dst]        - Move file or directory\n"
               "  rename [old] [new]    - Rename file or directory\n"
               "  find [query]          - Search files matching name\n"
               "  info [path]           - Display file metadata\n"
               "  edit [path]           - Interactive file editor\n"
               "  append [path] [text]  - Append text to file\n"
               "  clearfile [path]      - Truncate file to empty\n"
               "  readall               - Read contents of all files\n"
               "  recent                - Print recently accessed files\n");
  } else if (strcmp(cmd, "clear") == 0 || strcmp(cmd, "cls") == 0) {
    if (redirect_to_gui_term) {
      struct gui_window *w = &gui_windows[APP_TERMINAL];
      w->term_line_count = 0;
      for (int i = 0; i < 15; i++)
        w->term_lines[i][0] = '\0';
      term_scrollback_count = 0;
      term_scroll_offset = 0;
      for (int i = 0; i < TERM_SCROLLBACK_MAX; i++)
        term_scrollback[i][0] = '\0';
    } else {
      term_clear();
    }
  } else if (strcmp(cmd, "about") == 0) {
    run_about();
  } else if (strcmp(cmd, "version") == 0) {
    term_print("MiniEduOS v1.0 (OS Lab Demo Edition)\n");
  } else if (strncmp(cmd, "echo ", 5) == 0) {
    term_print(cmd + 5);
    term_print("\n");
  } else if (strcmp(cmd, "echo") == 0) {
    term_print("\n");
  } else if (strcmp(cmd, "date") == 0) {
    int sec, min, hour, day, month, year;
    get_rtc_time(&sec, &min, &hour, &day, &month, &year);
    term_print("Date: ");
    print_int_leading_zero(day, 2);
    term_print("/");
    print_int_leading_zero(month, 2);
    term_print("/20");
    print_int_leading_zero(year, 2);
    term_print("\n");
  } else if (strcmp(cmd, "time") == 0) {
    int sec, min, hour, day, month, year;
    get_rtc_time(&sec, &min, &hour, &day, &month, &year);
    term_print("Time: ");
    print_int_leading_zero(hour, 2);
    term_print(":");
    print_int_leading_zero(min, 2);
    term_print(":");
    print_int_leading_zero(sec, 2);
    term_print("\n");
  } else if (strncmp(cmd, "calc ", 5) == 0) {
    run_calc(cmd + 5);
  } else if (strcmp(cmd, "calc") == 0) {
    run_calc("");
  } else if (strcmp(cmd, "reboot") == 0) {
    reboot();
  } else if (strcmp(cmd, "shutdown") == 0) {
    shutdown();
  } else if (strncmp(cmd, "color ", 6) == 0) {
    run_color(cmd + 6);
  } else if (strcmp(cmd, "color") == 0) {
    run_color("");
  } else if (strcmp(cmd, "memory") == 0) {
    run_memory(magic, mb_addr);
  } else if (strcmp(cmd, "cpu") == 0) {
    run_cpu();
  } else if (strcmp(cmd, "ascii") == 0) {
    run_ascii();
  } else if (strcmp(cmd, "sysinfo") == 0 || strcmp(cmd, "sys") == 0) {
    run_sysinfo(magic, mb_addr);
  } else if (strcmp(cmd, "uptime") == 0) {
    run_uptime();
  } else if (strcmp(cmd, "meminfo") == 0) {
    run_meminfo();
  } else if (strcmp(cmd, "procinfo") == 0) {
    run_procinfo();
  } else if (strcmp(cmd, "mem") == 0) {
    run_mem();
  } else if (strncmp(cmd, "malloc ", 7) == 0) {
    unsigned int req_size = atoi(cmd + 7);
    unsigned int allocated_addr = 0;
    int block_id = sim_malloc(req_size, &allocated_addr);
    if (block_id > 0) {
      term_print("Allocated ");
      print_int(req_size);
      term_print(" bytes (ID: ");
      print_int(block_id);
      term_print(", Addr: 0x");
      print_hex(allocated_addr);
      term_print(").\n");
    } else {
      term_print("Error: Allocation failed (Out of memory or blocks limit).\n");
    }
  } else if (strcmp(cmd, "free") == 0) {
    int last_id = get_last_allocated_id();
    if (last_id > 0) {
      if (sim_free(last_id)) {
        term_print("Memory released (ID: ");
        print_int(last_id);
        term_print(").\n");
      }
    } else {
      term_print("Error: No active allocations to free.\n");
    }
  } else if (strncmp(cmd, "free ", 5) == 0) {
    int free_id = atoi(cmd + 5);
    if (sim_free(free_id)) {
      term_print("Memory released (ID: ");
      print_int(free_id);
      term_print(").\n");
    } else {
      term_print("Error: Allocation ID ");
      print_int(free_id);
      term_print(" not found.\n");
    }
  } else if (strcmp(cmd, "memmap") == 0) {
    draw_visual_memmap();
  } else if (strcmp(cmd, "ps") == 0) {
    run_ps();
  } else if (strncmp(cmd, "new ", 4) == 0) {
    run_new_proc(cmd + 4);
  } else if (strcmp(cmd, "new") == 0) {
    run_new_proc("");
  } else if (strncmp(cmd, "run ", 4) == 0) {
    run_run_proc(cmd + 4);
  } else if (strcmp(cmd, "run") == 0) {
    run_run_proc("");
  } else if (strncmp(cmd, "kill ", 5) == 0) {
    run_kill_proc(cmd + 5);
  } else if (strcmp(cmd, "kill") == 0) {
    run_kill_proc("");
  } else if (strcmp(cmd, "top") == 0) {
    run_top();
  } else if (strcmp(cmd, "schedule") == 0) {
    term_print("=================================\n");
    term_print("    CPU Scheduling Simulator     \n");
    term_print("=================================\n");
    term_print("Supported algorithms:\n");
    term_print("  fcfs      - First Come First Serve\n");
    term_print("  sjf       - Shortest Job First (Non-preemptive)\n");
    term_print("  priority  - Priority Scheduling (Non-preemptive)\n");
    term_print("  rr <q>    - Round Robin (Quantum q)\n\n");
    term_print("Reschedule ready processes anytime!\n");
  } else if (strcmp(cmd, "fcfs") == 0) {
    vfs_strcpy(current_scheduler_algo, "FCFS");
    run_scheduler("FCFS", 0);
  } else if (strcmp(cmd, "sjf") == 0) {
    vfs_strcpy(current_scheduler_algo, "SJF");
    run_scheduler("SJF", 0);
  } else if (strcmp(cmd, "priority") == 0) {
    vfs_strcpy(current_scheduler_algo, "Priority");
    run_scheduler("Priority", 0);
  } else if (strncmp(cmd, "rr ", 3) == 0) {
    int quantum = atoi(cmd + 3);
    if (quantum <= 0) {
      term_print("Usage: rr <time_quantum> (must be > 0)\n");
    } else {
      vfs_strcpy(current_scheduler_algo, "Round Robin (q=");
      char q_buf[16];
      int_to_str(quantum, q_buf);
      vfs_strcpy(current_scheduler_algo + vfs_strlen(current_scheduler_algo),
                 q_buf);
      vfs_strcpy(current_scheduler_algo + vfs_strlen(current_scheduler_algo),
                 ")");
      run_scheduler("Round Robin", quantum);
    }
  } else if (strcmp(cmd, "rr") == 0) {
    term_print("Usage: rr <time_quantum>\n");
  } else if (strncmp(cmd, "gantt ", 6) == 0) {
    run_gantt(cmd + 6);
  } else if (strcmp(cmd, "gantt") == 0) {
    run_gantt("");
  } else if (strcmp(cmd, "ls") == 0) {
    run_vfs_ls();
  } else if (strcmp(cmd, "pwd") == 0) {
    char pwd_buf[256];
    get_absolute_path(vfs_cwd, pwd_buf);
    term_print(pwd_buf);
    term_print("\n");
  } else if (strncmp(cmd, "cd ", 3) == 0) {
    run_vfs_cd(cmd + 3);
  } else if (strcmp(cmd, "cd") == 0) {
    run_vfs_cd("");
  } else if (strncmp(cmd, "mkdir ", 6) == 0) {
    run_vfs_mkdir(cmd + 6);
  } else if (strcmp(cmd, "mkdir") == 0) {
    run_vfs_mkdir("");
  } else if (strncmp(cmd, "touch ", 6) == 0) {
    run_vfs_touch(cmd + 6);
  } else if (strcmp(cmd, "touch") == 0) {
    run_vfs_touch("");
  } else if (strncmp(cmd, "cat ", 4) == 0) {
    run_vfs_cat(cmd + 4);
  } else if (strcmp(cmd, "cat") == 0) {
    run_vfs_cat("");
  } else if (strncmp(cmd, "write ", 6) == 0) {
    run_vfs_write(cmd + 6);
  } else if (strcmp(cmd, "write") == 0) {
    run_vfs_write("");
  } else if (strncmp(cmd, "rm ", 3) == 0) {
    run_vfs_rm(cmd + 3);
  } else if (strcmp(cmd, "rm") == 0) {
    run_vfs_rm("");
  } else if (strcmp(cmd, "tree") == 0) {
    print_tree_recursive(vfs_root, 0, 0);
  }
  // NEW VFS EXTENSION COMMANDS
  else if (strncmp(cmd, "open ", 5) == 0) {
    run_vfs_open(cmd + 5);
  } else if (strcmp(cmd, "open") == 0) {
    run_vfs_open("");
  } else if (strncmp(cmd, "cp ", 3) == 0) {
    char arg1[128], arg2[128];
    parse_two_args(cmd + 3, arg1, arg2);
    run_vfs_cp(arg1, arg2);
  } else if (strcmp(cmd, "cp") == 0) {
    term_print("Usage: cp <source> <destination>\n");
  } else if (strncmp(cmd, "mv ", 3) == 0) {
    char arg1[128], arg2[128];
    parse_two_args(cmd + 3, arg1, arg2);
    run_vfs_mv(arg1, arg2);
  } else if (strcmp(cmd, "mv") == 0) {
    term_print("Usage: mv <source> <destination>\n");
  } else if (strncmp(cmd, "rename ", 7) == 0) {
    char arg1[128], arg2[128];
    parse_two_args(cmd + 7, arg1, arg2);
    run_vfs_rename(arg1, arg2);
  } else if (strcmp(cmd, "rename") == 0) {
    term_print("Usage: rename <old_path> <new_name>\n");
  } else if (strncmp(cmd, "find ", 5) == 0) {
    run_vfs_find(cmd + 5);
  } else if (strcmp(cmd, "find") == 0) {
    run_vfs_find("");
  } else if (strncmp(cmd, "info ", 5) == 0) {
    run_vfs_info(cmd + 5);
  } else if (strcmp(cmd, "info") == 0) {
    run_vfs_info("");
  } else if (strncmp(cmd, "edit ", 5) == 0) {
    run_vfs_edit(cmd + 5);
  } else if (strcmp(cmd, "edit") == 0) {
    run_vfs_edit("");
  } else if (strncmp(cmd, "append ", 7) == 0) {
    char filename[64], text[512];
    parse_append_args(cmd + 7, filename, text);
    run_vfs_append(cmd + 7);
  } else if (strcmp(cmd, "append") == 0) {
    term_print("Usage: append <filename> <text>\n");
  } else if (strncmp(cmd, "clearfile ", 10) == 0) {
    run_vfs_clearfile(cmd + 10);
  } else if (strcmp(cmd, "clearfile") == 0) {
    term_print("Usage: clearfile <filename>\n");
  } else if (strcmp(cmd, "readall") == 0) {
    run_vfs_readall();
  } else if (strcmp(cmd, "history") == 0) {
    term_print("Command History:\n");
    for (int i = shell_history_count - 1; i >= 0; i--) {
      print_int(shell_history_count - i);
      term_print(": ");
      term_print(shell_history[i]);
      term_print("\n");
    }
  } else if (strcmp(cmd, "recent") == 0) {
    term_print("Recently Accessed Files:\n");
    for (int i = 0; i < recent_files_count; i++) {
      term_print(recent_files[i]);
      term_print("\n");
    }
  } else if (strcmp(cmd, "gui") == 0) {
    if (!gui_mode) {
      gui_mode = 1;
      init_gui();
      draw_gui();
    }
  } else if (strcmp(cmd, "exit") == 0 || strcmp(cmd, "text") == 0) {
    if (gui_mode) {
      gui_mode = 0;
      term_clear();
      term_print("Returned to text mode shell.\n");
    } else {
      term_print("Already in text mode shell. Use 'gui' to enter GUI.\n");
    }
  } else {
    term_print("Unknown command: ");
    term_print(cmd);
    term_print("\n");
  }
}

static int mouse_wait(unsigned char type) {
  unsigned int timeout = 100000;
  if (type == 0) {
    while (!(inb(0x64) & 1)) {
      timeout--;
      if (timeout == 0)
        return 0;
    }
    return 1;
  } else {
    while (inb(0x64) & 2) {
      timeout--;
      if (timeout == 0)
        return 0;
    }
    return 1;
  }
}

static void mouse_write(unsigned char data) {
  if (!mouse_wait(1))
    return;
  outb(0x64, 0xD4);
  if (!mouse_wait(1))
    return;
  outb(0x60, data);
}

static unsigned char mouse_read() {
  if (!mouse_wait(0))
    return 0;
  return inb(0x60);
}

static void mouse_init() {
  outb(0x64, 0xA8);
  mouse_wait(1);

  outb(0x64, 0x20);
  if (mouse_wait(0)) {
    unsigned char status = inb(0x60);
    status |= 0x02;
    status &= ~0x20;
    outb(0x64, 0x60);
    if (mouse_wait(1)) {
      outb(0x60, status);
    }
  }

  mouse_write(0xFF);
  mouse_read();
  mouse_read();
  mouse_read();

  mouse_write(0xF4);
  mouse_read();
}

static void handle_mouse_byte(unsigned char data) {
  if (mouse_cycle == 0) {
    if (data & 0x08) {
      mouse_packet[0] = data;
      mouse_cycle = 1;
    }
  } else if (mouse_cycle == 1) {
    mouse_packet[1] = data;
    mouse_cycle = 2;
  } else if (mouse_cycle == 2) {
    mouse_packet[2] = data;
    mouse_cycle = 0;

    unsigned char flags = mouse_packet[0];
    int dx = mouse_packet[1];
    int dy = mouse_packet[2];

    if (flags & 0x10)
      dx -= 256;
    if (flags & 0x20)
      dy -= 256;

    mouse_x += dx;
    mouse_y -= dy;

    if (mouse_x < 0)
      mouse_x = 0;
    if (mouse_x >= 800)
      mouse_x = 799;
    if (mouse_y < 0)
      mouse_y = 0;
    if (mouse_y >= 600)
      mouse_y = 599;

    int new_left = flags & 0x01;
    if (new_left && !left_button_pressed) {
      left_button_pressed = 1;
      mouse_clicked_event = 1;
      force_redraw = 1;
    } else if (!new_left) {
      left_button_pressed = 0;
    }
    int new_right = flags & 0x02;
    if (new_right && !right_button_pressed) {
      right_button_pressed = 1;
      mouse_right_clicked_event = 1;
      force_redraw = 1;
    } else if (!new_right) {
      right_button_pressed = 0;
    }
  }
}

static void handle_keyboard_char(char c) {
  if (alt_pressed) {
    int step = 15;
    if (c == (char)0x81) {
      mouse_y -= step;
    } else if (c == (char)0x84) {
      mouse_y += step;
    } else if (c == (char)0x82) {
      mouse_x -= step;
    } else if (c == (char)0x83) {
      mouse_x += step;
    } else if (c == ' ' || c == '\n') {
      mouse_clicked_event = 1;
      kb_click_active = 1;
    }

    if (mouse_x < 0)
      mouse_x = 0;
    if (mouse_x >= 800)
      mouse_x = 799;
    if (mouse_y < 0)
      mouse_y = 0;
    if (mouse_y >= 600)
      mouse_y = 599;

    force_redraw = 1;
  } else {
    handle_gui_input(c);
    force_redraw = 1;
  }
}

static void handle_keyboard_byte(unsigned char scancode) {
  if (scancode == 0x2A || scancode == 0x36) {
    shift_pressed = 1;
  } else if (scancode == 0xAA || scancode == 0xB6) {
    shift_pressed = 0;
  } else if (scancode == 0x38) {
    alt_pressed = 1;
  } else if (scancode == 0xB8) {
    alt_pressed = 0;
  } else if (scancode == 0x1D) {
    ctrl_pressed = 1;
  } else if (scancode == 0x9D) {
    ctrl_pressed = 0;
  } else if (scancode == 0x3C) { // F2
    handle_keyboard_char((char)0x87);
  } else if (scancode == 0x53) { // Delete
    handle_keyboard_char((char)0x88);
  } else if ((scancode & 0x80) == 0) {
    char c = shift_pressed ? kbd_us_shift[scancode] : kbd_us[scancode];
    if (c != 0) {
      handle_keyboard_char(c);
    }
  }
}

static void draw_mouse_cursor(int mx, int my, unsigned int fb_addr,
                              unsigned int pitch, unsigned int bpp) {
  for (int y = 0; y < 12; y++) {
    for (int x = 0; x < 8; x++) {
      int px = mx + x;
      int py = my + y;
      if (px >= 0 && px < 800 && py >= 0 && py < 600) {
        char pixel_type = cursor_mask[y][x];
        if (pixel_type == 'X') {
          draw_pixel(px, py, 255, 255, 255, fb_addr, pitch, bpp);
        } else if (pixel_type == '*') {
          draw_pixel(px, py, 0, 0, 0, fb_addr, pitch, bpp);
        }
      }
    }
  }
}

static void get_vga_color(unsigned char attr, unsigned char *r,
                          unsigned char *g, unsigned char *b) {
  static const unsigned char vga_palette[16][3] = {
      {0, 0, 0},       // 0: Black
      {0, 0, 170},     // 1: Blue
      {0, 170, 0},     // 2: Green
      {0, 170, 170},   // 3: Cyan
      {170, 0, 0},     // 4: Red
      {170, 0, 170},   // 5: Magenta
      {170, 85, 0},    // 6: Brown
      {170, 170, 170}, // 7: Light Gray
      {85, 85, 85},    // 8: Dark Gray
      {85, 85, 255},   // 9: Light Blue
      {85, 255, 85},   // 10: Light Green
      {85, 255, 255},  // 11: Light Cyan
      {255, 85, 85},   // 12: Light Red
      {255, 85, 255},  // 13: Light Magenta
      {255, 255, 85},  // 14: Yellow
      {255, 255, 255}  // 15: White
  };
  unsigned char color_index = attr & 0x0F;
  *r = vga_palette[color_index][0];
  *g = vga_palette[color_index][1];
  *b = vga_palette[color_index][2];
}

static int load_logo_asset() {
  struct vfs_node *node = resolve_path("/assets/logo.png");
  if (node != NULL && node->type == VFS_FILE) {
    return 1;
  }
  return 0;
}

static void redraw_screen(unsigned int fb_addr, unsigned int pitch,
                          unsigned int width, unsigned int height,
                          unsigned int bpp) {
  unsigned char r1 = 15, g1 = 23, b1 = 42;
  unsigned char r2 = 49, g2 = 46, b2 = 129;

  char fill_char = wallpaper_pattern[0];
  char wall_attr = (char)desktop_color;

  static int logo_loaded = -1;
  static int logo_draw_w = 0;
  static int logo_draw_h = 0;
  static int logo_start_x = 0;
  static int logo_start_y = 0;

  if (logo_loaded == -1) {
    logo_loaded = load_logo_asset();
    if (logo_loaded && LOGO_PNG_WIDTH > 0 && LOGO_PNG_HEIGHT > 0) {
      int max_w = width / 2;
      int max_h = height / 2;
      logo_draw_w = LOGO_PNG_WIDTH;
      logo_draw_h = LOGO_PNG_HEIGHT;
      if (logo_draw_w > max_w || logo_draw_h > max_h) {
        if (logo_draw_w * max_h > logo_draw_h * max_w) {
          logo_draw_h = (logo_draw_h * max_w) / logo_draw_w;
          logo_draw_w = max_w;
        } else {
          logo_draw_w = (logo_draw_w * max_h) / logo_draw_h;
          logo_draw_h = max_h;
        }
      }
      logo_start_x = (width - logo_draw_w) / 2;
      logo_start_y = (height - logo_draw_h) / 2;
    }
  }

  for (int row = 0; row < 25; row++) {
    for (int col = 0; col < 80; col++) {
      int cell_idx = (row * 80 + col) * 2;
      unsigned char ascii = video[cell_idx];
      unsigned char attr = video[cell_idx + 1];

      unsigned char bg_r, bg_g, bg_b;
      unsigned char fg_r, fg_g, fg_b;

      get_vga_color(attr >> 4, &bg_r, &bg_g, &bg_b);
      get_vga_color(attr & 0x0F, &fg_r, &fg_g, &fg_b);

      int cell_x = col * 10;
      int cell_y = row * 24;

      int is_wallpaper = (row < 24 && ascii == fill_char && attr == wall_attr);

      // 1. Draw cell background (10x24 pixels)
      for (int dy = 0; dy < 24; dy++) {
        for (int dx = 0; dx < 10; dx++) {
          int px = cell_x + dx;
          int py = cell_y + dy;
          if (is_wallpaper) {
            unsigned int scale_y = (py * 256) / height;
            unsigned int scale_x = (px * 256) / width;
            unsigned int scale = (scale_x + scale_y) / 2;
            if (scale > 255)
              scale = 255;
            unsigned char r = (r1 * (255 - scale) + r2 * scale) / 255;
            unsigned char g = (g1 * (255 - scale) + g2 * scale) / 255;
            unsigned char b = (b1 * (255 - scale) + b2 * scale) / 255;

            // Draw centered logo if applicable
            if (logo_loaded && LOGO_PNG_WIDTH > 0 && LOGO_PNG_HEIGHT > 0) {
              int lx = px - logo_start_x;
              int ly = py - logo_start_y;
              if (lx >= 0 && lx < logo_draw_w && ly >= 0 && ly < logo_draw_h) {
                int orig_x = (lx * LOGO_PNG_WIDTH) / logo_draw_w;
                int orig_y = (ly * LOGO_PNG_HEIGHT) / logo_draw_h;
                if (orig_x >= 0 && orig_x < LOGO_PNG_WIDTH && orig_y >= 0 &&
                    orig_y < LOGO_PNG_HEIGHT) {
                  unsigned int idx = (orig_y * LOGO_PNG_WIDTH + orig_x) * 4;
                  unsigned char la_r = logo_png_raw_rgba[idx];
                  unsigned char la_g = logo_png_raw_rgba[idx + 1];
                  unsigned char la_b = logo_png_raw_rgba[idx + 2];
                  unsigned char la_a = logo_png_raw_rgba[idx + 3];

                  // Blend logo on top of wallpaper color
                  r = (unsigned char)((la_r * la_a + r * (255 - la_a)) / 255);
                  g = (unsigned char)((la_g * la_a + g * (255 - la_a)) / 255);
                  b = (unsigned char)((la_b * la_a + b * (255 - la_a)) / 255);
                }
              }
            }

            draw_pixel(px, py, r, g, b, fb_addr, pitch, bpp);
          } else {
            draw_pixel(px, py, bg_r, bg_g, bg_b, fb_addr, pitch, bpp);
          }
        }
      }

      // 2. Draw centered character bitmap scaled to 8x16 (repeat row twice)
      if (!is_wallpaper && ascii < 128) {
        unsigned char *bitmap = (unsigned char *)font8x8_basic[ascii];
        int start_x = cell_x + 1;
        int start_y = cell_y + 4;

        for (int r = 0; r < 8; r++) {
          unsigned char row_data = bitmap[r];
          for (int c = 0; c < 8; c++) {
            if ((row_data >> c) & 1) {
              draw_pixel(start_x + c, start_y + r * 2, fg_r, fg_g, fg_b,
                         fb_addr, pitch, bpp);
              draw_pixel(start_x + c, start_y + r * 2 + 1, fg_r, fg_g, fg_b,
                         fb_addr, pitch, bpp);
            }
          }
        }
      }
    }
  }

  draw_mouse_cursor(mouse_x, mouse_y, fb_addr, pitch, bpp);
}

void kernel_main(unsigned int magic, unsigned int mb_addr) {
  saved_multiboot_magic = magic;
  saved_multiboot_info_addr = mb_addr;

  struct multiboot_info *mb_info = (struct multiboot_info *)mb_addr;
  if (magic == 0x2BADB002 && (mb_info->flags & 0x1000)) {
    graphics_mode = 1;
    unsigned int fb_addr = mb_info->framebuffer_addr_low;
    unsigned int pitch = mb_info->framebuffer_pitch;
    unsigned int width = mb_info->framebuffer_width;
    unsigned int height = mb_info->framebuffer_height;
    unsigned char bpp = mb_info->framebuffer_bpp;

    // Redirect video to virtual buffer in graphics mode
    video = (volatile char *)virtual_video;
    for (int i = 0; i < 80 * 25; i++) {
      video[i * 2] = ' ';
      video[i * 2 + 1] = 0x0F;
    }

    // Initialize OS Subsystems
    init_memory_manager();
    init_process_manager();
    init_vfs();

    // Disable text-mode cursor block
    cursor_visible = 0;

    // Initialize GUI system
    init_gui();
    draw_gui();

    // Initialize hardware mouse
    mouse_init();

    // Initial screen draw
    redraw_screen(fb_addr, pitch, width, height, bpp);

    int last_mouse_x = mouse_x;
    int last_mouse_y = mouse_y;
    int last_left_button = left_button_pressed;

    while (1) {
      int redraw = 0;

      // Poll PS/2 controller data
      unsigned char status = inb(0x64);
      if (status & 1) { // Output buffer full
        unsigned char data = inb(0x60);
        if (status & 0x20) { // Mouse data
          handle_mouse_byte(data);
        } else { // Keyboard data
          handle_keyboard_byte(data);
        }
      }

      // Handle mouse clicks
      if (mouse_clicked_event) {
        mouse_clicked_event = 0;
        int click_y = mouse_y / 24;
        if (mouse_y >= 570) {
          click_y = 24;
        } else if (click_y > 24) {
          click_y = 24;
        }
        handle_gui_click(mouse_x / 10, click_y);
        draw_gui();
        redraw = 1;
      }
      if (mouse_right_clicked_event) {
        mouse_right_clicked_event = 0;
        int click_y = mouse_y / 24;
        if (click_y > 24)
          click_y = 24;
        handle_gui_right_click(mouse_x / 10, click_y);
        draw_gui();
        redraw = 1;
      }

      // If mouse moved or Alt+Space click simulated release
      if (mouse_x != last_mouse_x || mouse_y != last_mouse_y ||
          left_button_pressed != last_left_button || force_redraw) {

        force_redraw = 0;
        draw_gui();
        redraw = 1;

        last_mouse_x = mouse_x;
        last_mouse_y = mouse_y;
        last_left_button = left_button_pressed;

        if (kb_click_active) {
          left_button_pressed = 0;
          kb_click_active = 0;
        }
      }

      if (redraw) {
        redraw_screen(fb_addr, pitch, width, height, bpp);
      }
    }
  }

  draw_title_bar();
  for (int i = 80; i < 80 * 25; i++) {
    video[i * 2] = ' ';
    video[i * 2 + 1] = 0x0F;
  }
  draw_centered_logo();
  show_loading_animation();

  init_memory_manager();
  init_process_manager();
  init_vfs();

  if (gui_mode) {
    init_gui();
    draw_gui();
  } else {
    term_clear();
    term_print_color("> ", 0x0A);
  }

  char cmd[256];
  int cmd_idx = 0;

  char last_cmd[256];
  int last_cmd_len = 0;
  last_cmd[0] = '\0';

  while (1) {
    char c = get_char();

    if (gui_mode) {
      handle_gui_input(c);
      draw_gui();
    } else {
      if (c == '\n') {
        term_putc_color('\n', shell_text_color);
        cmd[cmd_idx] = '\0';

        if (cmd_idx > 0) {
          for (int i = 0; i <= cmd_idx; i++) {
            last_cmd[i] = cmd[i];
          }
          last_cmd_len = cmd_idx;
          execute_command(cmd, magic, mb_addr);
        }
        cmd_idx = 0;
        term_print_color("> ", 0x0A);
      } else if (c == '\b') {
        if (cmd_idx > 0) {
          cmd_idx--;
          term_backspace();
        }
      } else if (c == (char)0x81) { // Up Arrow
        if (last_cmd_len > 0) {
          while (cmd_idx > 0) {
            cmd_idx--;
            term_backspace();
          }
          for (int i = 0; i < last_cmd_len; i++) {
            cmd[cmd_idx++] = last_cmd[i];
            term_putc_color(last_cmd[i], shell_text_color);
          }
        }
      } else if (c >= 32 && c <= 126) {
        if (cmd_idx < 254) {
          cmd[cmd_idx++] = c;
          term_putc_color(c, shell_text_color);
        }
      }
    }
  }
}