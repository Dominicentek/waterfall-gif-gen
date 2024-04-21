#ifndef Opt_H
#define Opt_H

#define NOCHAR 1

#include <string.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>

#define ALLOC(t) ((t*)memset(malloc(sizeof(t)), 0, sizeof(t)))

struct OptIter {
    int ptr;
    int argc;
    char** argv;
};

typedef bool(*OptCallback)(struct OptIter* iter);

struct Option {
   char* name;
    char letter;
    OptCallback callback;
};

struct OptList {
    struct OptList* next;
    struct OptList* prev;
    struct Option* opt;
};

static struct OptList* opt_create() {
    return ALLOC(struct OptList);
}

static void opt_add(struct OptList* list, const char* name, char letter, OptCallback callback) {
    struct Option* opt = ALLOC(struct Option);
    struct OptList* curr = list;
    while (curr->next) curr = curr->next;
    opt->letter = letter;
    opt->name = (char*)malloc(strlen(name) + 1);
    strcpy(opt->name, name);
    opt->callback = callback;
    curr->next = opt_create();
    curr->next->prev = curr;
    curr->next->opt = opt;
}

static void opt_invalid(struct OptList* list, OptCallback callback) {
    if (!list->opt) list->opt = ALLOC(struct Option);
    list->opt->callback = callback;
}

static const char* opt_get(struct OptIter* iter) {
    if (iter->ptr == iter->argc) return NULL;
    return iter->argv[iter->ptr++];
}

static int opt_run(int argc, char** argv, struct OptList* list) {
    struct OptIter iter;
    iter.ptr = 1;
    iter.argv = argv;
    iter.argc = argc;
    const char* arg;
    start:
    while ((arg = opt_get(&iter))) {
        if (strlen(arg) == 2) {
            struct OptList* curr = list->next;
            while (curr) {
                if (arg[0] == '-' && arg[1] == curr->opt->letter) {
                    if (!curr->opt->callback(&iter)) goto end;
                    goto start;
                }
                curr = curr->next;
            }
            iter.ptr--;
            if (!list->opt->callback(&iter)) goto end;
            continue;
        }
        if (arg[0] != '-' || arg[1] != '-') {
            if (list->opt->callback) {
                iter.ptr--;
                if (!list->opt->callback(&iter)) goto end;
            }
            continue;
        }
        struct OptList* curr = list->next;
        while (curr) {
            if (strcmp(arg + 2, curr->opt->name) == 0) {
                if (!curr->opt->callback(&iter)) goto end;
                goto start;
            }
            curr = curr->next;
        }
        iter.ptr--;
        if (!list->opt->callback(&iter)) goto end;
    }
    end:
    struct OptList* curr = list;
    while (curr->next) curr = curr->next;
    curr = curr->prev;
    while (curr->prev) {
        free(curr->opt->name);
        free(curr->opt);
        free(curr->next);
        curr = curr->prev;
    }
    free(curr);
    return iter.ptr;
}

#undef ALLOC

#endif