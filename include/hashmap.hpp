#ifndef HASHMAP_HPP_
#define HASHMAP_HPP_

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

constexpr size_t kRehasingWork = 128;
constexpr size_t kMaxLoadFactor = 8;

struct HNode {
    HNode *next = nullptr;
    uint64_t hcode = 0;
};

// When allocating a large array, calloc() gets the memory from mmap(), and the pages from mmap()
// are allocated and zeroed on the first access, which is effectively a progressively
// zero-initialized array.
struct HTab {
    HNode **tab = nullptr;  // array of slots
    size_t mask = 0;        // power of 2 array size, 2^n - 1
    size_t size = 0;        // number of keys

    void init(size_t n) {
        assert(n > 0 && ((n - 1) & n) == 0);  // Check if n is power of 2
        this->tab = (HNode **)calloc(n, sizeof(HNode *));
        this->mask = n - 1;
        this->size = 0;
    }

    // +---+-- +---+-- +---+-----+-----+---+
    // | 0 | 1 | 2 | 3 | 4 | ... | n-1 | n |
    // +---+-- +---+-- +---+-----+-----+---+
    //           |
    //           \/
    //         +-----+
    //         |node |
    //         +-----+
    void insert(HNode *node) {
        // Determining where to insert
        size_t pos = get_position(node->hcode);
        // Determining the last node inserted at a position
        HNode *next = this->tab[pos];
        // Attach new node to the last node
        node->next = next;
        // Make the new node as last node
        this->tab[pos] = node;
        this->size++;
    }

    HNode **lookup(HNode *key, bool (*eq)(HNode *, HNode *)) {
        if (!this->tab) {
            return nullptr;
        }

        // We are finding the slot where the node will be, since we insert the node with same
        // position
        size_t pos = get_position(key->hcode);
        // Finding the last node in the linked list attached to this slot
        HNode **from = &this->tab[pos];  // incoming pointer to the target
        for (HNode *cur; (cur = *from) != nullptr; from = &cur->next) {
            // Note that it compares the hash value before calling the callback, this is an
            // optimization to rule out candidates early.
            if (cur->hcode == key->hcode && eq(cur, key)) {
                return from;  // may be a node, may be a slot
            }
        }
        return nullptr;
    }

    HNode *detach(HNode **from) {
        HNode *node = *from;  // the target node
        *from = node->next;   // update the incoming pointer to the target
        this->size--;
        return node;
    }

    bool foreach (bool (*f)(HNode *, void *), void *arg) {
        for (size_t i = 0; this->mask != 0 && i <= this->mask; i++) {
            for (HNode *node = this->tab[i]; node != nullptr; node = node->next) {
                if (!f(node, arg)) {
                    return false;
                }
            }
        }
        return true;
    }

   private:
    size_t get_position(uint64_t hcode) {
        // Example: If hcode is 7 ans mask is 3 (it means 4 nodes)
        // (0111) & (0011) = (0011) = 3
        return hcode & mask;
    }
};

// Two hashtables are used for progressive rehashing.
struct HMap {
    HTab newer;
    HTab older;
    size_t migrate_pos = 0;

    HNode *lookup(HNode *key, bool (*eq)(HNode *, HNode *)) {
        help_rehashing();
        HNode **from = this->newer.lookup(key, eq);
        if (!from) {
            from = this->older.lookup(key, eq);
        }
        return from ? *from : nullptr;
    }

    void insert(HNode *node) {
        if (!this->newer.tab) {
            this->newer.init(4);
        }
        this->newer.insert(node);

        if (!this->older.tab) {
            size_t threshold = (this->newer.mask + 1) * kMaxLoadFactor;
            if (this->newer.size >= threshold) {
                trigger_rehashing();
            }
        }
        help_rehashing();
    }

    HNode *hm_delete(HNode *key, bool (*eq)(HNode *, HNode *)) {
        help_rehashing();
        if (HNode **from = this->newer.lookup(key, eq)) {
            return this->newer.detach(from);
        }
        if (HNode **from = this->older.lookup(key, eq)) {
            return this->older.detach(from);
        }
        return nullptr;
    }

    void clear() {
        free(this->newer.tab);
        free(this->older.tab);
    }

    size_t size() { return this->newer.size + this->older.size; }

    void foreach (bool (*f)(HNode *, void *), void *arg) {
        this->newer.foreach (f, arg) && this->older.foreach (f, arg);
    }

   private:
    void help_rehashing() {
        size_t nwork = 0;
        while (nwork<kRehasingWork &&this->older.size> 0) {
            HNode **from = &this->older.tab[this->migrate_pos];
            if (!*from) {
                this->migrate_pos++;
                continue;  // empty slot
            }
            // move the first list item to the newer table
            newer.insert(older.detach(from));
            nwork++;
        }

        // discard the old table if done
        if (this->older.size == 0 && this->older.tab) {
            free(this->older.tab);
            this->older = HTab{};
        }
    }

    void trigger_rehashing() {
        assert(this->older.tab == nullptr);
        this->older = this->newer;
        this->newer.init((this->newer.mask + 1) * 2);
        this->migrate_pos = 0;
    }
};

#endif  // HASHMAP_HPP_
