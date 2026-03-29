#include <stdint.h>

#include <zgdk.h>

#include "videopoker.h"
#include "gameplay.h"

static uint8_t* g_deck = NULL;
static uint8_t* g_deck_pos = NULL;

void gameplay_bind(const GameplayBindings* bindings)
{
    g_deck = bindings->deck;
    g_deck_pos = bindings->deck_pos;
}

/* Card IDs are 0..51, grouped by suits in blocks of 13. */
static uint8_t card_rank(uint8_t card)
{
    return card % 13;
}

/* Suit index: 0..3 from card ID. */
static uint8_t card_suit(uint8_t card)
{
    return card / 13;
}

static uint8_t is_straight(const uint8_t ranks[13])
{
    /*
     * ranks[0] is Ace.
     * Supports:
     * - normal 5-card runs
     * - A-2-3-4-5
     * - 10-J-Q-K-A
     */
    uint8_t run = 0;

    for (uint8_t r = 0; r < 13; r++) {
        if (ranks[r]) {
            run++;
            if (run == 5) {
                return true;
            }
        } else {
            run = 0;
        }
    }

    if (ranks[0] && ranks[1] && ranks[2] && ranks[3] && ranks[4]) {
        return true;
    }

    if (ranks[0] && ranks[9] && ranks[10] && ranks[11] && ranks[12]) {
        return true;
    }

    return false;
}

static uint8_t is_royal(const uint8_t ranks[13])
{
    /* Royal uses 10/J/Q/K/A ranks; suit is checked separately by flush logic. */
    return ranks[0] && ranks[9] && ranks[10] && ranks[11] && ranks[12];
}

void shuffle_deck(void)
{
    /* Fisher-Yates shuffle over full 52-card deck. */
    for (uint8_t i = 0; i < DECK_SIZE; i++) {
        g_deck[i] = i;
    }

    for (uint8_t i = DECK_SIZE - 1; i > 0; i--) {
        uint8_t j = rand8_quick() % (i + 1);
        uint8_t tmp = g_deck[i];
        g_deck[i] = g_deck[j];
        g_deck[j] = tmp;
    }

    *g_deck_pos = 0;
}

uint8_t pop_deck(void)
{
    /* Safety fallback if deck is exhausted unexpectedly. */
    if (*g_deck_pos >= DECK_SIZE) {
        shuffle_deck();
    }
    return g_deck[(*g_deck_pos)++];
}

HandResult evaluate_hand(const uint8_t hand[CARD_COUNT])
{
    uint8_t rank_counts[13] = {0};
    uint8_t suit_counts[4] = {0};

    uint8_t pairs = 0;
    uint8_t trips = 0;
    uint8_t quads = 0;

    /* Histogram ranks/suits once, then derive all categories from counts. */
    for (uint8_t i = 0; i < CARD_COUNT; i++) {
        rank_counts[card_rank(hand[i])]++;
        suit_counts[card_suit(hand[i])]++;
    }

    uint8_t flush = false;
    for (uint8_t s = 0; s < 4; s++) {
        if (suit_counts[s] == 5) {
            flush = true;
            break;
        }
    }

    uint8_t straight = is_straight(rank_counts);

    for (uint8_t r = 0; r < 13; r++) {
        if (rank_counts[r] == 2) {
            pairs++;
        } else if (rank_counts[r] == 3) {
            trips++;
        } else if (rank_counts[r] == 4) {
            quads++;
        }
    }

    /*
     * Ranking priority follows the pay table from highest to lowest.
     * First match returns immediately.
     */
    if (straight && flush && is_royal(rank_counts)) {
        HandResult result = {250, "ROYAL"};
        return result;
    }
    if (straight && flush) {
        HandResult result = {50, "STRFL"};
        return result;
    }
    if (quads) {
        HandResult result = {25, "4KIND"};
        return result;
    }
    if (trips && pairs) {
        HandResult result = {9, "FULLHS"};
        return result;
    }
    if (flush) {
        HandResult result = {6, "FLUSH"};
        return result;
    }
    if (straight) {
        HandResult result = {4, "STRAIT"};
        return result;
    }
    if (trips) {
        HandResult result = {3, "3KIND"};
        return result;
    }
    if (pairs == 2) {
        HandResult result = {2, "2PAIR"};
        return result;
    }

    if (pairs == 1) {
        HandResult result = {1, "PAIR"};
        return result;
    }

    {
        HandResult result = {0, "NOWIN"};
        return result;
    }
}
