/* PS3 File Manager - homebrew for PSL1GHT
 * Browse, copy, move, delete files and create folders on HDD / USB / BD.
 */
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <dirent.h>
#include <errno.h>
#include <sys/stat.h>
#include <io/pad.h>
#include <sysutil/sysutil.h>
#include "gfx.h"

#define MAXE    4096
#define PATHLEN 768
#define CHUNK   (512 * 1024)

#define C_BG     0x00101820
#define C_HEAD   0x00203050
#define C_SEL    0x00305080
#define C_TEXT   0x00E0E0E0
#define C_DIR    0x0080D0FF
#define C_DIM    0x00909090
#define C_WARN   0x00FFC040
#define C_ERR    0x00FF6060

typedef struct {
    char name[256];
    int dir;
    unsigned long long size;
} Entry;

enum {
    B_UP = 1, B_DOWN = 2, B_LEFT = 4, B_RIGHT = 8, B_CROSS = 16, B_CIRCLE = 32,
    B_SQUARE = 64, B_TRI = 128, B_START = 256, B_SELECT = 512, B_L1 = 1024, B_R1 = 2048
};

static Entry ents[MAXE];
static int nent, sel, top;
static char cwd[PATHLEN] = "/";
static char clip[PATHLEN];
static int have_clip, clip_move;
static char msg[200];
static int msg_err;
static int running = 1;
static int confirm_delete;
static unsigned char iobuf[CHUNK];

static void draw_ui(const char *busy);

/* ---------- helpers ---------- */

static void setmsg(int err, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);
    msg_err = err;
}

static int exists(const char *p) { struct stat st; return stat(p, &st) == 0; }
static int is_dir(const char *p) { struct stat st; return stat(p, &st) == 0 && S_ISDIR(st.st_mode); }

static void join(char *out, size_t n, const char *dir, const char *name)
{
    if (strcmp(dir, "/") == 0) snprintf(out, n, "/%s", name);
    else snprintf(out, n, "%s/%s", dir, name);
}

static const char *base_name(const char *p)
{
    const char *s = strrchr(p, '/');
    return s ? s + 1 : p;
}

static void fmt_size(unsigned long long s, char *o, size_t n)
{
    if (s < 1024ULL) snprintf(o, n, "%llu B", s);
    else if (s < 1024ULL * 1024) snprintf(o, n, "%llu KB", s / 1024);
    else if (s < 1024ULL * 1024 * 1024) {
        unsigned long long t = s * 10 / (1024ULL * 1024);
        snprintf(o, n, "%llu.%llu MB", t / 10, t % 10);
    } else {
        unsigned long long t = s * 10 / (1024ULL * 1024 * 1024);
        snprintf(o, n, "%llu.%llu GB", t / 10, t % 10);
    }
}

static void progress(const char *what)
{
    draw_ui(what);
    gfx_flip();
    sysUtilCheckCallback();
}

/* ---------- directory listing ---------- */

static int cmp_entries(const void *a, const void *b)
{
    const Entry *x = (const Entry *)a, *y = (const Entry *)b;
    if (x->dir != y->dir) return y->dir - x->dir;
    return strcasecmp(x->name, y->name);
}

static void add_entry(const char *name, int dir, unsigned long long size)
{
    if (nent >= MAXE) return;
    strncpy(ents[nent].name, name, 255);
    ents[nent].name[255] = 0;
    ents[nent].dir = dir;
    ents[nent].size = size;
    nent++;
}

static void load_dir(void)
{
    nent = 0;
    if (strcmp(cwd, "/") == 0) {
        static const char *mounts[] = {
            "dev_hdd0", "dev_hdd1", "dev_usb000", "dev_usb001", "dev_usb002",
            "dev_usb003", "dev_usb004", "dev_usb005", "dev_usb006", "dev_usb007",
            "dev_bdvd", "dev_flash", "dev_flash2", "dev_flash3", "app_home", NULL
        };
        int i;
        for (i = 0; mounts[i]; i++) {
            char p[64];
            snprintf(p, sizeof(p), "/%s", mounts[i]);
            if (is_dir(p)) add_entry(mounts[i], 1, 0);
        }
    } else {
        DIR *d = opendir(cwd);
        struct dirent *e;
        if (!d) {
            setmsg(1, "Cannot open folder (errno %d)", errno);
            return;
        }
        while ((e = readdir(d)) != NULL && nent < MAXE) {
            char p[PATHLEN];
            struct stat st;
            if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
            join(p, sizeof(p), cwd, e->d_name);
            if (stat(p, &st) == 0)
                add_entry(e->d_name, S_ISDIR(st.st_mode), (unsigned long long)st.st_size);
            else
                add_entry(e->d_name, 0, 0);
        }
        closedir(d);
        qsort(ents, nent, sizeof(Entry), cmp_entries);
    }
    if (sel >= nent) sel = nent - 1;
    if (sel < 0) sel = 0;
}

static void select_name(const char *name)
{
    int i;
    for (i = 0; i < nent; i++)
        if (!strcmp(ents[i].name, name)) { sel = i; return; }
}

/* ---------- file operations ---------- */

static int copy_file(const char *src, const char *dst)
{
    FILE *in = fopen(src, "rb");
    FILE *out;
    size_t n;
    unsigned long long total = 0;
    int tick = 0, rc = 0;
    char line[PATHLEN + 64];

    if (!in) return -1;
    out = fopen(dst, "wb");
    if (!out) { fclose(in); return -1; }

    while ((n = fread(iobuf, 1, CHUNK, in)) > 0) {
        if (fwrite(iobuf, 1, n, out) != n) { rc = -1; break; }
        total += n;
        if ((++tick & 7) == 0) {
            snprintf(line, sizeof(line), "Copying %s  (%llu MB)", base_name(src), total >> 20);
            progress(line);
        }
    }
    fclose(in);
    if (fclose(out) != 0) rc = -1;
    return rc;
}

static int copy_tree(const char *src, const char *dst)
{
    if (is_dir(src)) {
        DIR *d;
        struct dirent *e;
        int rc = 0;
        if (mkdir(dst, 0777) != 0 && !is_dir(dst)) return -1;
        d = opendir(src);
        if (!d) return -1;
        while ((e = readdir(d)) != NULL) {
            char s[PATHLEN], t[PATHLEN];
            if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
            join(s, sizeof(s), src, e->d_name);
            join(t, sizeof(t), dst, e->d_name);
            if (copy_tree(s, t) != 0) rc = -1;
        }
        closedir(d);
        return rc;
    }
    return copy_file(src, dst);
}

static int delete_tree(const char *path)
{
    if (is_dir(path)) {
        DIR *d = opendir(path);
        struct dirent *e;
        int rc = 0;
        if (!d) return -1;
        while ((e = readdir(d)) != NULL) {
            char p[PATHLEN];
            if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
            join(p, sizeof(p), path, e->d_name);
            if (delete_tree(p) != 0) rc = -1;
        }
        closedir(d);
        if (rmdir(path) != 0) rc = -1;
        return rc;
    } else {
        char line[PATHLEN + 32];
        snprintf(line, sizeof(line), "Deleting %s", base_name(path));
        progress(line);
        return unlink(path);
    }
}

static void unique_path(char *p, size_t n)
{
    char base[PATHLEN];
    int i = 1;
    if (!exists(p)) return;
    strncpy(base, p, sizeof(base) - 1);
    base[sizeof(base) - 1] = 0;
    do {
        if (i == 1) snprintf(p, n, "%.*s_copy", (int)(n - 16), base);
        else        snprintf(p, n, "%.*s_copy%d", (int)(n - 16), base, i);
        i++;
    } while (exists(p) && i < 1000);
}

static void do_paste(void)
{
    char dst[PATHLEN];
    size_t l;
    int rc;

    if (!have_clip) { setmsg(1, "Clipboard is empty"); return; }
    if (!strcmp(cwd, "/")) { setmsg(1, "Open a drive first"); return; }
    if (!exists(clip)) { setmsg(1, "Source no longer exists"); have_clip = 0; return; }

    l = strlen(clip);
    if (is_dir(clip) && !strncmp(cwd, clip, l) && (cwd[l] == '/' || cwd[l] == 0)) {
        setmsg(1, "Cannot paste a folder into itself");
        return;
    }

    join(dst, sizeof(dst), cwd, base_name(clip));

    if (clip_move) {
        if (!strcmp(dst, clip)) { setmsg(0, "Already here"); return; }
        unique_path(dst, sizeof(dst));
        progress("Moving...");
        if (rename(clip, dst) == 0) rc = 0;
        else {
            rc = copy_tree(clip, dst);
            if (rc == 0) rc = delete_tree(clip);
        }
        if (rc == 0) have_clip = 0;
    } else {
        unique_path(dst, sizeof(dst));
        progress("Copying...");
        rc = copy_tree(clip, dst);
    }

    if (rc == 0) setmsg(0, "%s done", clip_move ? "Move" : "Copy");
    else setmsg(1, "Operation failed (read-only or no space?)");
    load_dir();
    select_name(base_name(dst));
}

static void do_delete(void)
{
    char p[PATHLEN];
    if (nent == 0 || !strcmp(cwd, "/")) return;
    join(p, sizeof(p), cwd, ents[sel].name);
    if (delete_tree(p) == 0) setmsg(0, "Deleted");
    else setmsg(1, "Delete failed (read-only?)");
    load_dir();
}

static void do_mkdir(void)
{
    char name[64], p[PATHLEN];
    int i = 1;
    if (!strcmp(cwd, "/")) { setmsg(1, "Open a drive first"); return; }
    do {
        if (i == 1) snprintf(name, sizeof(name), "NewFolder");
        else snprintf(name, sizeof(name), "NewFolder%d", i);
        join(p, sizeof(p), cwd, name);
        i++;
    } while (exists(p) && i < 1000);
    if (mkdir(p, 0777) == 0) { setmsg(0, "Created %s", name); load_dir(); select_name(name); }
    else setmsg(1, "Cannot create folder");
}

static void go_open(void)
{
    char p[PATHLEN];
    if (nent == 0) return;
    if (!ents[sel].dir) {
        char sz[32];
        fmt_size(ents[sel].size, sz, sizeof(sz));
        setmsg(0, "%s - %s", ents[sel].name, sz);
        return;
    }
    join(p, sizeof(p), cwd, ents[sel].name);
    strncpy(cwd, p, sizeof(cwd) - 1);
    cwd[sizeof(cwd) - 1] = 0;
    sel = top = 0;
    setmsg(0, "");
    load_dir();
}

static void go_up(void)
{
    char leave[256];
    char *sl;
    if (!strcmp(cwd, "/")) return;
    sl = strrchr(cwd, '/');
    strncpy(leave, sl + 1, sizeof(leave) - 1);
    leave[sizeof(leave) - 1] = 0;
    if (sl == cwd) strcpy(cwd, "/"); else *sl = 0;
    sel = top = 0;
    setmsg(0, "");
    load_dir();
    select_name(leave);
}

/* ---------- UI ---------- */

static int visible_rows(void) { return (gfx_h - 56 - 72) / 18; }

static void draw_box(const char *l1, const char *l2, unsigned col)
{
    int w = 800, h = 100;
    int x = (gfx_w - w) / 2, y = (gfx_h - h) / 2;
    gfx_rect(x - 4, y - 4, w + 8, h + 8, 0x00FFFFFF);
    gfx_rect(x, y, w, h, 0x00202030);
    gfx_text(x + 16, y + 20, 2, col, "%.48s", l1);
    if (l2) gfx_text(x + 16, y + 60, 2, C_TEXT, "%s", l2);
}

static void draw_ui(const char *busy)
{
    int rows = visible_rows(), i, y;
    char sz[32];

    gfx_clear(C_BG);
    gfx_rect(0, 0, gfx_w, 48, C_HEAD);
    gfx_text(16, 6, 2, 0x00FFFFFF, "PS3 File Manager");
    gfx_text(gfx_w - 16 - 16 * 14, 6, 2, C_DIM, "%d / %d", nent ? sel + 1 : 0, nent);
    gfx_text(16, 28, 2, C_DIR, "%.76s", cwd);

    if (nent == 0)
        gfx_text(32, 64, 2, C_DIM, "(empty)");

    if (sel < top) top = sel;
    if (sel >= top + rows) top = sel - rows + 1;
    if (top < 0) top = 0;

    for (i = 0; i < rows && top + i < nent; i++) {
        Entry *e = &ents[top + i];
        y = 56 + i * 18;
        if (top + i == sel) gfx_rect(8, y - 1, gfx_w - 16, 18, C_SEL);
        gfx_text(16, y, 2, e->dir ? C_DIR : C_TEXT, "%s%.56s", e->dir ? "[D] " : "    ", e->name);
        if (!e->dir) {
            fmt_size(e->size, sz, sizeof(sz));
            gfx_text(gfx_w - 16 - 16 * 12, y, 2, C_DIM, "%12s", sz);
        }
    }

    y = gfx_h - 66;
    if (msg[0]) gfx_text(16, y, 2, msg_err ? C_ERR : C_WARN, "%.76s", msg);
    if (have_clip) gfx_text(16, y + 20, 2, C_DIM, "%s: %.60s", clip_move ? "Cut" : "Copied", base_name(clip));
    gfx_text(16, y + 40, 2, C_DIM, "X Open O Back []Copy /\\Move Start Paste Sel Delete R1 NewDir");

    if (confirm_delete && nent > 0)
        draw_box("Delete this permanently?", "X = Yes     O = No", C_ERR);
    else if (busy)
        draw_box("Working...", busy, C_WARN);
}

/* ---------- input ---------- */

static padInfo pinfo;
static padData pdata;

static unsigned read_pad(void)
{
    unsigned m = 0;
    int i;
    ioPadGetInfo(&pinfo);
    for (i = 0; i < MAX_PADS; i++) {
        if (!pinfo.status[i]) continue;
        ioPadGetData(i, &pdata);
        if (pdata.BTN_UP) m |= B_UP;
        if (pdata.BTN_DOWN) m |= B_DOWN;
        if (pdata.BTN_LEFT) m |= B_LEFT;
        if (pdata.BTN_RIGHT) m |= B_RIGHT;
        if (pdata.BTN_CROSS) m |= B_CROSS;
        if (pdata.BTN_CIRCLE) m |= B_CIRCLE;
        if (pdata.BTN_SQUARE) m |= B_SQUARE;
        if (pdata.BTN_TRIANGLE) m |= B_TRI;
        if (pdata.BTN_START) m |= B_START;
        if (pdata.BTN_SELECT) m |= B_SELECT;
        if (pdata.BTN_L1) m |= B_L1;
        if (pdata.BTN_R1) m |= B_R1;
    }
    return m;
}

static void sysutil_cb(u64 status, u64 param, void *usrdata)
{
    (void)param; (void)usrdata;
    if (status == SYSUTIL_EXIT_GAME) running = 0;
}

int main(int argc, char **argv)
{
    unsigned prev = 0;
    int hold = 0, idle = 0, dirty = 1;
    (void)argc; (void)argv;

    sysUtilRegisterCallback(SYSUTIL_EVENT_SLOT0, sysutil_cb, NULL);
    gfx_init();
    ioPadInit(7);
    load_dir();

    while (running) {
        unsigned now, pressed, act;
        int rows = visible_rows();

        sysUtilCheckCallback();
        now = read_pad();
        pressed = now & ~prev;
        act = pressed;
        if (now & (B_UP | B_DOWN)) {
            hold++;
            if (hold > 25 && (hold % 4) == 0) act |= now & (B_UP | B_DOWN);
        } else hold = 0;
        prev = now;

        if (act) {
            if (confirm_delete) {
                if (act & B_CROSS) { confirm_delete = 0; do_delete(); }
                else if (act & B_CIRCLE) confirm_delete = 0;
            } else {
                if (act & B_UP)    { if (sel > 0) sel--; }
                if (act & B_DOWN)  { if (sel < nent - 1) sel++; }
                if (act & B_LEFT)  { sel -= rows; if (sel < 0) sel = 0; }
                if (act & B_RIGHT) { sel += rows; if (sel > nent - 1) sel = nent - 1; if (sel < 0) sel = 0; }
                if (act & (B_UP | B_DOWN | B_LEFT | B_RIGHT)) setmsg(0, "");
                if (act & B_CROSS)  go_open();
                if (act & B_CIRCLE) go_up();
                if (act & (B_SQUARE | B_TRI)) {
                    if (nent > 0 && strcmp(cwd, "/")) {
                        join(clip, sizeof(clip), cwd, ents[sel].name);
                        have_clip = 1;
                        clip_move = (act & B_TRI) ? 1 : 0;
                        setmsg(0, "%s: %s", clip_move ? "Cut" : "Copied", ents[sel].name);
                    }
                }
                if (act & B_START)  do_paste();
                if (act & B_SELECT) {
                    if (nent > 0 && strcmp(cwd, "/")) confirm_delete = 1;
                }
                if (act & B_R1) do_mkdir();
            }
            dirty = 1;
        }

        if (++idle > 120) { idle = 0; dirty = 1; }  /* periodic redraw (e.g. after XMB menu) */

        if (dirty) {
            draw_ui(NULL);
            gfx_flip();
            dirty = 0;
            idle = 0;
        }
        usleep(16000);
    }

    ioPadEnd();
    return 0;
}
