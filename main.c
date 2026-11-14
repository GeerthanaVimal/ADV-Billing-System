/*
 * BillMate - Restaurant Order & Billing Terminal
 *
 * How it works:
 *   - Staff log in with a PIN (admin or cashier)
 *   - Dishes come from an editable menu file (menu.txt)
 *   - A cashier builds an order by dish code, can remove lines, then checks out
 *   - Bill rules: promo discount on big orders, service charge for dine-in,
 *     GST, automatic round-off, cash change calculation
 *   - Every bill is appended to a readable text log (sales_log.txt)
 *   - Reports: find a bill by number, today's sales and top dishes
 *
 * Build:  gcc -Wall -Wextra -o billmate billmate.c
 * Run:    ./billmate        (Windows: .\billmate.exe)
 *
 * Default logins:  admin / 1234    cashier / 0000
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>

#ifdef _WIN32
  #include <io.h>
  #define IS_TTY() _isatty(_fileno(stdout))
#else
  #include <unistd.h>
  #define IS_TTY() isatty(STDOUT_FILENO)
#endif

/*  shop settings: edit these  */
#define SHOP_NAME     "ADV. RESTAURANT"
#define CURRENCY      "Rs."
#define SERVICE_RATE  0.05     /* dine-in only          */
#define GST_RATE      0.05
#define PROMO_MIN     2000.0   /* promo starts at this subtotal */
#define PROMO_RATE    0.10

#define MENU_FILE     "menu.txt"
#define LOG_FILE      "sales_log.txt"

#define MAX_DISHES    100
#define MAX_CART      40
#define LINE_BUF      256
#define W             44       /* receipt width */

/*  data types  */
typedef struct {
    int    code;
    char   name[32];
    char   category[16];
    double price;
} Dish;

typedef struct {
    int code;
    int qty;
} CartLine;

typedef struct {
    char   name[32];
    int    qty;
    double price;
} BillLine;

typedef struct {
    int      no;
    char     stamp[24];
    int      table;            /* 0 = takeaway */
    char     cashier[16];
    char     payMode[8];
    double   subtotal, promo, service, gst, roundOff, payable;
    int      nLines;
    BillLine lines[MAX_CART];
} Bill;

typedef struct {
    const char *user;
    const char *pin;
    int         isAdmin;
} Staff;

static const Staff STAFF[] = {
    { "admin",   "1234", 1 },
    { "cashier", "0000", 0 },
};

/* session state */
static Dish dishes[MAX_DISHES];
static int  dishCount = 0;
static char whoAmI[16] = "";
static int  amAdmin = 0;

/* Console helpers */
static void clear_screen(void) {
    if (!IS_TTY()) { printf("\n\n"); return; }   /* online compilers / redirected output */
#ifdef _WIN32
    system("cls");
#else
    printf("\033[2J\033[H");
#endif
}

static int get_line(const char *msg, char *dst, size_t cap) {
    if (msg) { fputs(msg, stdout); fflush(stdout); }
    if (!fgets(dst, (int)cap, stdin)) return 0;
    size_t n = strcspn(dst, "\r\n");
    if (dst[n] != '\0') {
        dst[n] = '\0';
    } else {
        int ch;
        while ((ch = getchar()) != '\n' && ch != EOF) { /* drop extra */ }
    }
    return 1;
}

static void ask_text(const char *msg, char *dst, size_t cap) {
    for (;;) {
        if (!get_line(msg, dst, cap)) exit(0);
        for (char *p = dst; *p; p++) if (*p == '|') *p = '-';  /* '|' is our file separator */
        if (dst[0]) return;
        puts("  (cannot be empty)");
    }
}

static int ask_int(const char *msg, int lo, int hi) {
    char buf[32], *end;
    for (;;) {
        if (!get_line(msg, buf, sizeof buf)) exit(0);
        long v = strtol(buf, &end, 10);
        if (end != buf && *end == '\0' && v >= lo && v <= hi) return (int)v;
        printf("  (enter a whole number from %d to %d)\n", lo, hi);
    }
}

static double ask_money(const char *msg, double lo) {
    char buf[32], *end;
    for (;;) {
        if (!get_line(msg, buf, sizeof buf)) exit(0);
        double v = strtod(buf, &end);
        if (end != buf && *end == '\0' && v >= lo) return v;
        printf("  (enter an amount of at least %.2f)\n", lo);
    }
}

static int ask_yes(const char *msg) {
    char buf[8];
    for (;;) {
        if (!get_line(msg, buf, sizeof buf)) exit(0);
        if (tolower((unsigned char)buf[0]) == 'y') return 1;
        if (tolower((unsigned char)buf[0]) == 'n') return 0;
        puts("  (y or n)");
    }
}

static void wait_key(void) {
    char tmp[8];
    get_line("\nPress Enter to continue...", tmp, sizeof tmp);
}

static void rule(FILE *o, char c) {
    for (int i = 0; i < W; i++) fputc(c, o);
    fputc('\n', o);
}

static void centered(FILE *o, const char *s) {
    int pad = (W - (int)strlen(s)) / 2;
    if (pad < 0) pad = 0;
    fprintf(o, "%*s%s\n", pad, "", s);
}

static void money_row(FILE *o, const char *label, double v) {
    fprintf(o, "%-*s%*.2f\n", W - 12, label, 12, v);
}

/* Menu catalogue (menu.txt:  code|name|category|price) */
static void write_default_menu(void) {
    static const Dish seed[] = {
        {  1, "Spring Rolls",        "Starters",  450 },
        {  2, "Chicken Wings",       "Starters",  650 },
        {  3, "Garlic Bread",        "Starters",  350 },
        {  4, "Chicken Fried Rice",  "Mains",     900 },
        {  5, "Veg Noodles",         "Mains",     750 },
        {  6, "Grilled Fish",        "Mains",    1400 },
        {  7, "Margherita Pizza",    "Mains",    1200 },
        {  8, "Fresh Lime Juice",    "Drinks",    250 },
        {  9, "Iced Coffee",         "Drinks",    400 },
        { 10, "Soft Drink",          "Drinks",    200 },
        { 11, "Ice Cream",           "Desserts",  350 },
        { 12, "Chocolate Brownie",   "Desserts",  500 },
    };
    FILE *f = fopen(MENU_FILE, "w");
    if (!f) return;
    for (size_t i = 0; i < sizeof seed / sizeof seed[0]; i++)
        fprintf(f, "%d|%s|%s|%.2f\n", seed[i].code, seed[i].name, seed[i].category, seed[i].price);
    fclose(f);
}

static void load_menu(void) {
    FILE *f = fopen(MENU_FILE, "r");
    if (!f) {
        write_default_menu();
        f = fopen(MENU_FILE, "r");
        if (!f) return;
    }
    char line[LINE_BUF];
    dishCount = 0;
    while (dishCount < MAX_DISHES && fgets(line, sizeof line, f)) {
        Dish d;
        if (sscanf(line, "%d|%31[^|]|%15[^|]|%lf", &d.code, d.name, d.category, &d.price) == 4)
            dishes[dishCount++] = d;
    }
    fclose(f);
}

static int save_menu(void) {
    FILE *f = fopen(MENU_FILE, "w");
    if (!f) return 0;
    for (int i = 0; i < dishCount; i++)
        fprintf(f, "%d|%s|%s|%.2f\n", dishes[i].code, dishes[i].name,
                dishes[i].category, dishes[i].price);
    fclose(f);
    return 1;
}

static Dish *find_dish(int code) {
    for (int i = 0; i < dishCount; i++)
        if (dishes[i].code == code) return &dishes[i];
    return NULL;
}

static void show_menu(void) {
    printf("\n  %-5s %-24s %10s\n", "Code", "Dish", "Price");
    for (int i = 0; i < dishCount; i++) {
        int seen = 0;
        for (int j = 0; j < i; j++)
            if (strcmp(dishes[j].category, dishes[i].category) == 0) { seen = 1; break; }
        if (seen) continue;
        printf("  --- %s ---\n", dishes[i].category);
        for (int k = 0; k < dishCount; k++)
            if (strcmp(dishes[k].category, dishes[i].category) == 0)
                printf("  %-5d %-24s %10.2f\n", dishes[k].code, dishes[k].name, dishes[k].price);
    }
}

/* 
 *  Bill log (sales_log.txt)
 *    B|no|stamp|table|cashier|pay|subtotal|promo|service|gst|round|payable|nLines
 *    I|dish name|qty|unit price          (one per line item)
 */
static void log_bill(const Bill *b) {
    FILE *f = fopen(LOG_FILE, "a");
    if (!f) { puts("  !! could not write to the sales log"); return; }
    fprintf(f, "B|%d|%s|%d|%s|%s|%.2f|%.2f|%.2f|%.2f|%.2f|%.2f|%d\n",
            b->no, b->stamp, b->table, b->cashier, b->payMode, b->subtotal,
            b->promo, b->service, b->gst, b->roundOff, b->payable, b->nLines);
    for (int i = 0; i < b->nLines; i++)
        fprintf(f, "I|%s|%d|%.2f\n", b->lines[i].name, b->lines[i].qty, b->lines[i].price);
    fclose(f);
}

/* Reads the next bill from an open log. Returns 1 if one was read. */
static int read_next_bill(FILE *f, Bill *b) {
    char line[LINE_BUF];
    while (fgets(line, sizeof line, f)) {
        int n;
        if (line[0] != 'B') continue;
        if (sscanf(line, "B|%d|%23[^|]|%d|%15[^|]|%7[^|]|%lf|%lf|%lf|%lf|%lf|%lf|%d",
                   &b->no, b->stamp, &b->table, b->cashier, b->payMode,
                   &b->subtotal, &b->promo, &b->service, &b->gst,
                   &b->roundOff, &b->payable, &n) != 12)
            continue;
        b->nLines = 0;
        for (int i = 0; i < n; i++) {
            if (!fgets(line, sizeof line, f)) return 0;
            if (b->nLines >= MAX_CART) continue;
            BillLine *bl = &b->lines[b->nLines];
            if (sscanf(line, "I|%31[^|]|%d|%lf", bl->name, &bl->qty, &bl->price) == 3)
                b->nLines++;
        }
        return 1;
    }
    return 0;
}

static int next_bill_number(void) {
    FILE *f = fopen(LOG_FILE, "r");
    if (!f) return 1;
    Bill b;
    int top = 0;
    while (read_next_bill(f, &b)) if (b.no > top) top = b.no;
    fclose(f);
    return top + 1;
}

/* Receipt */
static void print_receipt(FILE *o, const Bill *b) {
    char buf[48];
    fputc('\n', o);
    rule(o, '=');
    centered(o, SHOP_NAME);
    centered(o, "TAX INVOICE");
    rule(o, '=');
    fprintf(o, "Bill No : %05d\n", b->no);
    fprintf(o, "Date    : %s\n", b->stamp);
    if (b->table > 0) fprintf(o, "Order   : Dine-in, Table %d\n", b->table);
    else              fprintf(o, "Order   : Takeaway\n");
    fprintf(o, "Cashier : %s\n", b->cashier);
    fprintf(o, "Paid by : %s\n", b->payMode);
    rule(o, '-');
    fprintf(o, "%-20s %3s %8s %9s\n", "Dish", "Qty", "Price", "Amount");
    rule(o, '-');
    for (int i = 0; i < b->nLines; i++)
        fprintf(o, "%-20.20s %3d %8.2f %9.2f\n", b->lines[i].name, b->lines[i].qty,
                b->lines[i].price, b->lines[i].qty * b->lines[i].price);
    rule(o, '-');
    money_row(o, "Subtotal", b->subtotal);
    if (b->promo > 0) {
        snprintf(buf, sizeof buf, "Promo (%.0f%% off)", PROMO_RATE * 100);
        money_row(o, buf, -b->promo);
    }
    if (b->service > 0) {
        snprintf(buf, sizeof buf, "Service charge (%.0f%%)", SERVICE_RATE * 100);
        money_row(o, buf, b->service);
    }
    snprintf(buf, sizeof buf, "GST (%.0f%%)", GST_RATE * 100);
    money_row(o, buf, b->gst);
    if (b->roundOff > 0.004 || b->roundOff < -0.004) money_row(o, "Round off", b->roundOff);
    rule(o, '=');
    snprintf(buf, sizeof buf, "TOTAL (%s)", CURRENCY);
    money_row(o, buf, b->payable);
    rule(o, '=');
    centered(o, "Thank you, see you again!");
}

/* Taking an order */
static double cart_subtotal(const CartLine *cart, int n) {
    double s = 0;
    for (int i = 0; i < n; i++) {
        Dish *d = find_dish(cart[i].code);
        if (d) s += d->price * cart[i].qty;
    }
    return s;
}

static void show_cart(const CartLine *cart, int n) {
    printf("\n  Current order:\n");
    if (n == 0) { puts("   (empty)"); return; }
    for (int i = 0; i < n; i++) {
        Dish *d = find_dish(cart[i].code);
        if (!d) continue;
        printf("   %2d) %-22s x%-3d %9.2f\n", i + 1, d->name, cart[i].qty, d->price * cart[i].qty);
    }
    printf("   %-30s %9.2f\n", "Running subtotal:", cart_subtotal(cart, n));
}

static void add_to_cart(CartLine *cart, int *n, int code, char *note, size_t cap) {
    Dish *d = find_dish(code);
    if (!d) { snprintf(note, cap, "No dish with code %d.", code); return; }
    int qty = ask_int("  Quantity: ", 1, 99);
    for (int i = 0; i < *n; i++)
        if (cart[i].code == code) {
            cart[i].qty += qty;
            snprintf(note, cap, "Added %d more %s.", qty, d->name);
            return;
        }
    if (*n >= MAX_CART) { snprintf(note, cap, "Order is full."); return; }
    cart[*n].code = code;
    cart[*n].qty = qty;
    (*n)++;
    snprintf(note, cap, "Added %d x %s.", qty, d->name);
}

static void build_bill(Bill *b, const CartLine *cart, int n, int table) {
    memset(b, 0, sizeof *b);
    b->no = next_bill_number();
    b->table = table;
    snprintf(b->cashier, sizeof b->cashier, "%s", whoAmI);

    time_t now = time(NULL);
    strftime(b->stamp, sizeof b->stamp, "%d-%b-%Y %H:%M", localtime(&now));

    for (int i = 0; i < n; i++) {
        Dish *d = find_dish(cart[i].code);
        if (!d) continue;
        BillLine *bl = &b->lines[b->nLines++];
        snprintf(bl->name, sizeof bl->name, "%s", d->name);
        bl->qty = cart[i].qty;
        bl->price = d->price;
        b->subtotal += d->price * cart[i].qty;
    }
    b->promo   = (b->subtotal >= PROMO_MIN) ? b->subtotal * PROMO_RATE : 0;
    double taxable = b->subtotal - b->promo;
    b->service = (table > 0) ? taxable * SERVICE_RATE : 0;
    b->gst     = (taxable + b->service) * GST_RATE;
    double raw = taxable + b->service + b->gst;
    b->payable  = (double)(long)(raw + 0.5);
    b->roundOff = b->payable - raw;
}

/* Returns the change to hand back (0 for card / UPI). */
static double take_payment(Bill *b, double *given) {
    static const char *modes[] = { "Cash", "Card", "UPI" };
    printf("\n  Amount to pay: %s %.2f\n", CURRENCY, b->payable);
    printf("  Payment:  1) Cash   2) Card   3) UPI\n");
    int m = ask_int("  Choose: ", 1, 3);
    snprintf(b->payMode, sizeof b->payMode, "%s", modes[m - 1]);
    *given = 0;
    if (m != 1) return 0;
    for (;;) {
        *given = ask_money("  Cash received: ", 0);
        if (*given >= b->payable) break;
        printf("  Not enough - bill is %s %.2f\n", CURRENCY, b->payable);
    }
    return *given - b->payable;
}

static void new_order(void) {
    CartLine cart[MAX_CART];
    int  n = 0;
    char note[80] = "";

    clear_screen();
    puts("=== New Order ===");
    int table = ask_int("Table number (0 for takeaway): ", 0, 99);

    for (;;) {
        clear_screen();
        if (table > 0) printf("=== New Order - Table %d ===\n", table);
        else           printf("=== New Order - Takeaway ===\n");
        show_menu();
        show_cart(cart, n);
        if (note[0]) { printf("\n  >> %s\n", note); note[0] = '\0'; }

        char in[16];
        get_line("\n  Dish code | r=remove  d=done  q=cancel > ", in, sizeof in);
        if (in[0] == '\0') continue;

        if (isalpha((unsigned char)in[0])) {
            char c = (char)tolower((unsigned char)in[0]);
            if (c == 'r') {
                if (n == 0) { snprintf(note, sizeof note, "Nothing to remove."); continue; }
                int k = ask_int("  Remove which line number? ", 1, n);
                for (int i = k - 1; i < n - 1; i++) cart[i] = cart[i + 1];
                n--;
                snprintf(note, sizeof note, "Line removed.");
            }
            else if (c == 'q') {
                if (ask_yes("  Cancel this order? [y/n]: ")) return;
            }
            else if (c == 'd') {
                if (n == 0) { snprintf(note, sizeof note, "Add at least one dish first."); continue; }
                break;
            }
            else snprintf(note, sizeof note, "Unknown command '%c'.", in[0]);
        } else {
            char *end;
            long code = strtol(in, &end, 10);
            if (end != in && *end == '\0') add_to_cart(cart, &n, (int)code, note, sizeof note);
            else snprintf(note, sizeof note, "Type a dish code or a command.");
        }
    }

    Bill bill;
    build_bill(&bill, cart, n, table);
    double given = 0;
    double change = take_payment(&bill, &given);
    log_bill(&bill);

    clear_screen();
    print_receipt(stdout, &bill);
    if (strcmp(bill.payMode, "Cash") == 0) {
        printf("\n  Cash received : %s %.2f\n", CURRENCY, given);
        printf("  CHANGE TO GIVE: %s %.2f\n", CURRENCY, change);
    }

    if (ask_yes("\nSave a copy as a text file? [y/n]: ")) {
        char name[32];
        snprintf(name, sizeof name, "receipt_%05d.txt", bill.no);
        FILE *f = fopen(name, "w");
        if (f) { print_receipt(f, &bill); fclose(f); printf("Saved as %s\n", name); }
        else puts("Could not create the file.");
    }
}

/* Reports */
static void find_bill(void) {
    int want = ask_int("Bill number: ", 1, 99999999);
    FILE *f = fopen(LOG_FILE, "r");
    Bill b;
    int hit = 0;
    if (f) {
        while (read_next_bill(f, &b))
            if (b.no == want) { hit = 1; break; }
        fclose(f);
    }
    clear_screen();
    if (hit) print_receipt(stdout, &b);
    else printf("No bill numbered %d.\n", want);
}

typedef struct { char name[32]; int qty; } Tally;

static void todays_report(void) {
    char today[16];
    time_t now = time(NULL);
    strftime(today, sizeof today, "%d-%b-%Y", localtime(&now));

    FILE *f = fopen(LOG_FILE, "r");
    clear_screen();
    if (!f) { puts("No sales recorded yet."); return; }

    Bill b;
    int count = 0, dineIn = 0, takeaway = 0;
    double gross = 0, cash = 0, card = 0, upi = 0;
    Tally tally[MAX_DISHES];
    int tallies = 0;

    while (read_next_bill(f, &b)) {
        if (strncmp(b.stamp, today, strlen(today)) != 0) continue;
        count++;
        gross += b.payable;
        if (b.table > 0) dineIn++; else takeaway++;
        if (strcmp(b.payMode, "Cash") == 0) cash += b.payable;
        else if (strcmp(b.payMode, "Card") == 0) card += b.payable;
        else upi += b.payable;

        for (int i = 0; i < b.nLines; i++) {
            int k;
            for (k = 0; k < tallies; k++)
                if (strcmp(tally[k].name, b.lines[i].name) == 0) break;
            if (k == tallies) {
                if (tallies >= MAX_DISHES) continue;
                snprintf(tally[k].name, sizeof tally[k].name, "%s", b.lines[i].name);
                tally[k].qty = 0;
                tallies++;
            }
            tally[k].qty += b.lines[i].qty;
        }
    }
    fclose(f);

    printf("=== Sales for %s ===\n", today);
    rule(stdout, '-');
    if (count == 0) { puts("No bills today."); return; }
    printf("Bills issued    : %d  (dine-in %d, takeaway %d)\n", count, dineIn, takeaway);
    printf("Total sales     : %s %.2f\n", CURRENCY, gross);
    printf("Average bill    : %s %.2f\n", CURRENCY, gross / count);
    printf("  Cash          : %s %.2f\n", CURRENCY, cash);
    printf("  Card          : %s %.2f\n", CURRENCY, card);
    printf("  UPI           : %s %.2f\n", CURRENCY, upi);
    rule(stdout, '-');

    puts("Top dishes today:");
    for (int rank = 1; rank <= 3; rank++) {
        int best = -1;
        for (int k = 0; k < tallies; k++)
            if (tally[k].qty > 0 && (best < 0 || tally[k].qty > tally[best].qty)) best = k;
        if (best < 0) break;
        printf("  %d. %-24s %d sold\n", rank, tally[best].name, tally[best].qty);
        tally[best].qty = 0;
    }
}

/* Admin: menu management */
static void manage_menu(void) {
    if (!amAdmin) { puts("This section is for the admin only."); return; }

    puts("=== Manage Menu ===");
    puts("  1) Add a dish");
    puts("  2) Change a price");
    puts("  3) Remove a dish");
    puts("  4) Back");
    int pick = ask_int("Choose: ", 1, 4);

    if (pick == 1) {
        if (dishCount >= MAX_DISHES) { puts("Menu is full."); return; }
        Dish d;
        int top = 0;
        for (int i = 0; i < dishCount; i++) if (dishes[i].code > top) top = dishes[i].code;
        d.code = top + 1;
        ask_text("Dish name: ", d.name, sizeof d.name);
        ask_text("Category (e.g. Mains): ", d.category, sizeof d.category);
        d.price = ask_money("Price: ", 0.01);
        dishes[dishCount++] = d;
        printf(save_menu() ? "Added as code %d.\n" : "Added in memory, but saving failed (code %d).\n", d.code);
    }
    else if (pick == 2) {
        show_menu();
        Dish *d = find_dish(ask_int("Dish code: ", 1, 99999));
        if (!d) { puts("No such dish."); return; }
        d->price = ask_money("New price: ", 0.01);
        puts(save_menu() ? "Price updated." : "Updated in memory, but saving failed.");
    }
    else if (pick == 3) {
        show_menu();
        int code = ask_int("Dish code to remove: ", 1, 99999);
        for (int i = 0; i < dishCount; i++) {
            if (dishes[i].code != code) continue;
            for (int j = i; j < dishCount - 1; j++) dishes[j] = dishes[j + 1];
            dishCount--;
            puts(save_menu() ? "Dish removed." : "Removed in memory, but saving failed.");
            return;
        }
        puts("No such dish.");
    }
}

/* Login and main loop */
static int log_in(void) {
    for (int attempt = 1; attempt <= 3; attempt++) {
        clear_screen();
        printf("=========== %s ===========\n", SHOP_NAME);
        puts("          BillMate Terminal\n");
        char user[16], pin[16];
        ask_text("Username: ", user, sizeof user);
        ask_text("PIN     : ", pin, sizeof pin);
        for (size_t i = 0; i < sizeof STAFF / sizeof STAFF[0]; i++) {
            if (strcmp(user, STAFF[i].user) == 0 && strcmp(pin, STAFF[i].pin) == 0) {
                snprintf(whoAmI, sizeof whoAmI, "%s", STAFF[i].user);
                amAdmin = STAFF[i].isAdmin;
                return 1;
            }
        }
        printf("\nWrong username or PIN (%d of 3).\n", attempt);
        wait_key();
    }
    return 0;
}

/* Returns 1 to switch user, 0 to quit. */
static int run_session(void) {
    for (;;) {
        clear_screen();
        printf("=========== %s ===========\n", SHOP_NAME);
        printf("Logged in as: %s%s\n\n", whoAmI, amAdmin ? " (admin)" : "");
        puts("  1. New order");
        puts("  2. View menu");
        puts("  3. Find a bill");
        puts("  4. Today's sales report");
        puts("  5. Manage menu (admin)");
        puts("  6. Switch user");
        puts("  7. Quit\n");

        switch (ask_int("Select: ", 1, 7)) {
            case 1: new_order();      break;
            case 2: clear_screen(); puts("=== Menu ==="); show_menu(); break;
            case 3: find_bill();      break;
            case 4: todays_report();  break;
            case 5: clear_screen(); manage_menu(); break;
            case 6: return 1;
            case 7: puts("\nGoodbye!"); return 0;
        }
        wait_key();
    }
}

int main(void) {
    load_menu();
    for (;;) {
        if (!log_in()) {
            puts("Too many failed attempts. Exiting.");
            wait_key();
            return 1;
        }
        if (!run_session()) break;
    }
    wait_key();
    return 0;
}