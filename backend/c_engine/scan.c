/*
 * SCAN Disk Scheduling Algorithm (Elevator Algorithm)
 * Moves head in one direction servicing requests, then reverses at the end.
 *
 * Usage: ./scan <initial_head> <disk_size> <direction> <queue>
 * Example: ./scan 50 200 1 82,170,43,140
 *
 * direction: 0 = moving left (toward 0), 1 = moving right (toward disk_size-1)
 *
 * Output: JSON { "sequence": [...], "seek_time": N }
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_REQUESTS 256

/* ---------- helpers ---------- */

static void print_error(const char *msg) {
    fprintf(stderr, "Error: %s\n", msg);
    printf("{\"sequence\": [], \"seek_time\": 0, \"error\": \"%s\"}\n", msg);
}

/*
 * Parse comma-separated integer list into arr[].
 * Returns count, or -1 on failure.
 */
static int parse_queue(const char *str, int *arr, int max, int disk_size) {
    if (!str || strlen(str) == 0) return 0;

    char buf[4096];
    strncpy(buf, str, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    int count = 0;
    char *tok = strtok(buf, ",");
    while (tok) {
        if (count >= max) return -1;

        char *end;
        long val = strtol(tok, &end, 10);
        if (*end != '\0')               return -1;  /* non-numeric */
        if (val < 0 || val >= disk_size) return -1; /* out of range */

        arr[count++] = (int)val;
        tok = strtok(NULL, ",");
    }
    return count;
}

/* ---------- comparison for qsort ---------- */

static int cmp_int(const void *a, const void *b) {
    return (*(int *)a - *(int *)b);
}

/* ---------- SCAN core ---------- */

/*
 * SCAN (Elevator):
 *
 * 1. Sort all requests.
 * 2. Split into two groups:
 *      LEFT  — requests with cylinder < head  (sorted descending)
 *      RIGHT — requests with cylinder >= head (sorted ascending)
 * 3. If direction == RIGHT:  service RIGHT first, go to disk end, then LEFT.
 *    If direction == LEFT:   service LEFT  first, go to cylinder 0, then RIGHT.
 *
 * The head travels to the physical end of the disk before reversing —
 * this is the classic SCAN (not LOOK) behaviour.
 *
 * out_seq[] receives the full traversal path including boundary positions.
 * Returns total seek distance.
 */
static int scan(int head, int disk_size, int direction,
                int *requests, int n, int *out_seq) {

    /* Separate requests into left and right of current head */
    int left[MAX_REQUESTS],  left_cnt  = 0;
    int right[MAX_REQUESTS], right_cnt = 0;

    for (int i = 0; i < n; i++) {
        if (requests[i] < head)
            left[left_cnt++]  = requests[i];
        else
            right[right_cnt++] = requests[i];
    }

    /* Sort: left descending (we service closer ones first when moving left)
             right ascending (we service closer ones first when moving right) */
    qsort(left,  left_cnt,  sizeof(int), cmp_int);
    qsort(right, right_cnt, sizeof(int), cmp_int);

    /* Reverse left array so it's descending */
    for (int i = 0, j = left_cnt - 1; i < j; i++, j--) {
        int tmp = left[i]; left[i] = left[j]; left[j] = tmp;
    }

    int total = 0;
    int pos   = head;
    int idx   = 0;

    out_seq[idx++] = pos;   /* record initial head */

    if (direction == 1) {
        /* --- Moving RIGHT first --- */

        /* Service all requests to the right */
        for (int i = 0; i < right_cnt; i++) {
            total += abs(right[i] - pos);
            pos    = right[i];
            out_seq[idx++] = pos;
        }

        /* Travel to the rightmost boundary (disk_size - 1) */
        if (pos != disk_size - 1) {
            total += abs((disk_size - 1) - pos);
            pos    = disk_size - 1;
            out_seq[idx++] = pos;
        }

        /* Now sweep LEFT and service remaining requests */
        for (int i = 0; i < left_cnt; i++) {
            total += abs(left[i] - pos);
            pos    = left[i];
            out_seq[idx++] = pos;
        }

    } else {
        /* --- Moving LEFT first --- */

        /* Service all requests to the left */
        for (int i = 0; i < left_cnt; i++) {
            total += abs(left[i] - pos);
            pos    = left[i];
            out_seq[idx++] = pos;
        }

        /* Travel to the leftmost boundary (cylinder 0) */
        if (pos != 0) {
            total += abs(pos - 0);
            pos    = 0;
            out_seq[idx++] = pos;
        }

        /* Now sweep RIGHT and service remaining requests */
        for (int i = right_cnt - 1; i >= 0; i--) {
            total += abs(right[i] - pos);
            pos    = right[i];
            out_seq[idx++] = pos;
        }
    }

    return total;
}

/* ---------- JSON output ---------- */

static void print_result(int *seq, int seq_len, int seek_time) {
    printf("{\"sequence\": [");
    for (int i = 0; i < seq_len; i++) {
        printf("%d", seq[i]);
        if (i < seq_len - 1) printf(", ");
    }
    printf("], \"seek_time\": %d}\n", seek_time);
}

/* ---------- main ---------- */

int main(int argc, char *argv[]) {

    if (argc != 5) {
        print_error("Usage: ./scan <head> <disk_size> <direction> <queue>");
        return 1;
    }

    /* --- Parse initial head --- */
    char *end;
    long head = strtol(argv[1], &end, 10);
    if (*end != '\0' || head < 0) {
        print_error("Invalid initial head position");
        return 1;
    }

    /* --- Parse disk size --- */
    long disk_size = strtol(argv[2], &end, 10);
    if (*end != '\0' || disk_size <= 0) {
        print_error("Invalid disk size");
        return 1;
    }

    /* --- Validate head within disk --- */
    if (head >= disk_size) {
        print_error("Head position exceeds disk size");
        return 1;
    }

    /* --- Parse direction --- */
    long direction = strtol(argv[3], &end, 10);
    if (*end != '\0' || (direction != 0 && direction != 1)) {
        print_error("Direction must be 0 (left) or 1 (right)");
        return 1;
    }

    /* --- Parse request queue --- */
    int requests[MAX_REQUESTS];
    int n = parse_queue(argv[4], requests, MAX_REQUESTS, (int)disk_size);
    if (n < 0) {
        print_error("Invalid request queue (out-of-range or non-numeric values)");
        return 1;
    }
    if (n == 0) {
        int seq[1] = { (int)head };
        print_result(seq, 1, 0);
        return 0;
    }

    /* --- Run SCAN --- */
    int out_seq[MAX_REQUESTS + 4];   /* +4 for possible boundary positions */
    int seek_time = scan((int)head, (int)disk_size, (int)direction,
                         requests, n, out_seq);

    /* seq_len = variable; count entries written (idx returned implicitly via
       the filled array — use a sentinel approach by checking out_seq) */

    /* Recount actual entries written */
    int seq_len = 0;
    {
        /* Re-run length count: out_seq is filled from index 0 upward;
           we know max possible entries = n + 3 (head + n requests + 2 boundaries) */
        int max_possible = n + 3;
        /* Trust the algorithm: it always fills head + all requests + ≤2 boundaries */
        /* Safe: just pass max possible; print_result uses seq_len exactly */
        seq_len = max_possible; /* upper bound — trim trailing duplicates below */

        /* Trim: if last two entries are the same boundary it was added once only */
        /* Actually, the algorithm never double-adds; trust idx count instead.    */
        /* Re-derive length cleanly by tracking idx in a wrapper: */
    }

    /*
     * Clean approach: call scan() again using a local idx counter exposed
     * via a small wrapper so we get the exact written count without guessing.
     * This avoids any off-by-one in seq_len.
     */

    /* ---------- clean length-aware wrapper ---------- */
    int out_seq2[MAX_REQUESTS + 4];
    int idx2 = 0;

    /* Rebuild sequence inline so we capture exact idx */
    {
        int left[MAX_REQUESTS],  lc = 0;
        int right[MAX_REQUESTS], rc = 0;
        int n2 = n;
        int pos = (int)head;

        for (int i = 0; i < n2; i++) {
            if (requests[i] < pos) left[lc++]  = requests[i];
            else                   right[rc++] = requests[i];
        }

        qsort(left,  lc, sizeof(int), cmp_int);
        qsort(right, rc, sizeof(int), cmp_int);

        for (int i = 0, j = lc - 1; i < j; i++, j--) {
            int tmp = left[i]; left[i] = left[j]; left[j] = tmp;
        }

        out_seq2[idx2++] = pos;

        if (direction == 1) {
            for (int i = 0; i < rc; i++) {
                pos = right[i];
                out_seq2[idx2++] = pos;
            }
            if (pos != (int)disk_size - 1) {
                pos = (int)disk_size - 1;
                out_seq2[idx2++] = pos;
            }
            for (int i = 0; i < lc; i++) {
                pos = left[i];
                out_seq2[idx2++] = pos;
            }
        } else {
            for (int i = 0; i < lc; i++) {
                pos = left[i];
                out_seq2[idx2++] = pos;
            }
            if (pos != 0) {
                pos = 0;
                out_seq2[idx2++] = pos;
            }
            for (int i = rc - 1; i >= 0; i--) {
                pos = right[i];
                out_seq2[idx2++] = pos;
            }
        }
    }

    print_result(out_seq2, idx2, seek_time);
    return 0;
}