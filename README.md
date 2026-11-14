# BillMate – Restaurant Billing System

A console-based restaurant order and billing terminal written in **C**. Cashiers log in with a PIN, build an order from an editable dish menu, take payment, and print a formatted receipt. Every bill is saved to a readable text log, and the manager can view daily sales reports.

---

## Features

- **Staff login** with PIN (admin and cashier roles, 3 attempts)
- **Editable menu** stored in `menu.txt` (add, remove and re-price dishes from inside the program)
- **Order builder**: add dishes by code, remove lines, review a running subtotal
- **Dine-in or takeaway** orders with table numbers
- **Smart billing rules**
  - Promo discount on large orders
  - Service charge for dine-in only
  - GST calculation
  - Automatic round-off
- **Payment modes**: Cash, Card, UPI, with change calculation for cash
- **Receipts** shown on screen and optionally saved as `receipt_00001.txt`
- **Sales log** saved as readable text (`sales_log.txt`)
- **Find any bill** by its bill number
- **Daily report**: total sales, average bill, payment split, dine-in vs takeaway, top 3 dishes
- **Safe input handling**: invalid input is rejected without crashing

---

## Getting Started

### Requirements

- A C compiler such as **GCC** (MinGW-w64 on Windows)

### Default logins

| Role    | Username  | PIN  |
|---------|-----------|------|
| Admin   | `admin`   | 1234 |
| Cashier | `cashier` | 0000 |

> Change these in the `STAFF` list at the top of `billmate.c` before real use.

---

## How to Use

1. Log in with a username and PIN.
2. Choose **1. New order** and enter a table number (`0` for takeaway).
3. Type a dish code, then a quantity. Repeat for each dish.
   - `r` removes a line, `d` finishes the order, `q` cancels it.
4. Choose a payment method. For cash, enter the amount received and the change is shown.
5. The receipt is printed and the bill is saved to the log.

### Main menu

| Option | What it does |
|--------|--------------|
| 1. New order | Create an order and print the bill |
| 2. View menu | Show all dishes grouped by category |
| 3. Find a bill | Look up a past bill by number |
| 4. Today's sales report | Totals, payment split and top dishes |
| 5. Manage menu | Add, re-price or remove dishes (admin only) |
| 6. Switch user | Log out and log in as someone else |
| 7. Quit | Exit the program |

---

## Sample Receipt

```
============================================
              ADV. RESTAURANT
                TAX INVOICE
============================================
Bill No : 00001
Date    : 14-Nov-2026 14:36
Order   : Dine-in, Table 2
Cashier : cashier
Paid by : Cash
--------------------------------------------
Dish                 Qty    Price    Amount
--------------------------------------------
Grilled Fish           2  1400.00   2800.00
--------------------------------------------
Subtotal                             2800.00
Promo (10% off)                      -280.00
Service charge (5%)                   126.00
GST (5%)                              132.30
Round off                              -0.30
============================================
TOTAL (Rs.)                          2778.00
============================================
         Thank you, see you again!
```

---

## Configuration

Shop settings are defined at the top of `billmate.c`:

```c
#define SHOP_NAME     "ADV. RESTAURANT"
#define CURRENCY      "Rs."
#define SERVICE_RATE  0.05     /* dine-in only */
#define GST_RATE      0.05
#define PROMO_MIN     2000.0   /* promo starts at this subtotal */
#define PROMO_RATE    0.10
```

---

## Project Files

```
BillMate/
├── billmate.c        # Complete source code
├── README.md         # This file
├── menu.txt          # Created automatically on first run
└── sales_log.txt     # Created automatically when the first bill is saved
```

### Data formats

`menu.txt` – one dish per line:
```
code|name|category|price
4|Chicken Fried Rice|Mains|900.00
```

`sales_log.txt` – one `B` line per bill, followed by its `I` item lines:
```
B|no|date|table|cashier|payment|subtotal|promo|service|gst|roundoff|payable|itemCount
I|dish name|qty|unit price
```

---

## Concepts Used

- Structures, arrays and static data
- File handling (text files, append mode, line parsing with `sscanf`)
- Input validation with `fgets` and `strtol`
- String handling and formatted output
- Modular design with small, single-purpose functions
- Basic role-based access (admin vs cashier)

---
